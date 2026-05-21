/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VideoConsumer - Based on CodyCam's VideoConsumer implementation
 * This version implements BBufferGroup to provide buffers to the producer,
 * which is CRITICAL for driver compatibility.
 */

#include "VideoConsumer.h"
#include "MainWindow.h"

#include <Autolock.h>
#include <Buffer.h>
#include <BufferGroup.h>
#include <TimeSource.h>
#include <MediaRoster.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>


// Maximum acceptable latency for video buffer processing (in microseconds).
// 50ms is chosen as a balance between:
// - Responsiveness (lower values = more responsive but more CPU)
// - Stability (higher values = more buffer time but increased delay)
// At 30fps, each frame takes ~33ms, so 50ms allows some headroom.
const bigtime_t kMaxLatency = 50000;  // 50ms

// Fallback resolution when driver reports invalid (0x0) dimensions.
// 320x240 (QVGA) is a safe minimum supported by virtually all webcams.
const uint32 kFallbackWidth = 320;
const uint32 kFallbackHeight = 240;

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "VideoConsumer"
#include "ErrorUtils.h"

// Legacy macros for compatibility - delegate to standardized logging
#define INFO(x...) LOG_INFO(x)
#define ERROR(x...) LOG_ERROR(x)
#define PROGRESS(x...) LOG_DEBUG(x)


VideoConsumer::VideoConsumer(const char* name, BLooper* target,
	uint32 frameMessage, uint32 audioMessage)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(B_MEDIA_RAW_VIDEO),  // Prefer RAW_VIDEO but accept others in AcceptFormat
	fTarget(target),
	fFrameMessage(frameMessage),
	fAudioMessage(audioMessage),
	fConnected(false),
	fBuffers(NULL),
	fDisplayBitmap(NULL),
	fBitmapWidth(0),
	fBitmapHeight(0),
	fBitmapColorSpace(B_RGB32),
	fFramesReceived(0),
	fFramesDropped(0),
	fCurrentFPS(0.0f),
	fLastFrameTime(0),
	fInternalLatency(kMaxLatency)
{
	// Initialize all bitmap and buffer pointers to NULL
	for (int i = 0; i < NUM_BUFFERS; i++) {
		fBitmap[i] = NULL;
		fBufferMap[i] = NULL;
	}

	fInput = media_input();
	fDestination = media_destination();
	fFormat = media_format();
	fProducerSource = media_source();

	LOG_DEBUG("VideoConsumer created");
}


VideoConsumer::~VideoConsumer()
{
	LOG_DEBUG("~VideoConsumer cleanup");

	Quit();
	DeleteBuffers();

	delete fDisplayBitmap;
	fDisplayBitmap = NULL;
}


void
VideoConsumer::DeleteBuffers()
{
	LOG_TRACE("DeleteBuffers");

	// Delete the buffer group first
	if (fBuffers != NULL) {
		delete fBuffers;
		fBuffers = NULL;
	}

	// Delete all bitmaps
	for (int i = 0; i < NUM_BUFFERS; i++) {
		delete fBitmap[i];
		fBitmap[i] = NULL;
		fBufferMap[i] = NULL;  // These are owned by fBuffers, just clear pointers
	}
}


status_t
VideoConsumer::CreateBuffers(const media_format& format)
{
	// Delete any old buffers first
	DeleteBuffers();

	status_t status = B_OK;

	// Get dimensions from format
	uint32 xSize = format.u.raw_video.display.line_width;
	uint32 ySize = format.u.raw_video.display.line_count;
	color_space colorspace = format.u.raw_video.display.format;

	// Validate and fix dimensions
	if (xSize == 0 || ySize == 0) {
		LOG_DEBUG("CreateBuffers: 0x0 dimensions, using %dx%d fallback",
			(int)kFallbackWidth, (int)kFallbackHeight);
		xSize = kFallbackWidth;
		ySize = kFallbackHeight;
	}

	// Validate colorspace
	if (colorspace == 0) {
		colorspace = B_RGB32;
	}

	LOG_DEBUG("CreateBuffers: %d x %dx%d %s", NUM_BUFFERS, (int)xSize, (int)ySize,
		ColorSpaceName(colorspace));

	fBitmapWidth = xSize;
	fBitmapHeight = ySize;
	fBitmapColorSpace = colorspace;

	// Create a buffer group - THIS IS THE KEY MISSING PIECE!
	fBuffers = new BBufferGroup();
	status = fBuffers->InitCheck();
	if (status != B_OK) {
		ERROR("CreateBuffers() - failed to create BBufferGroup: %s\n", strerror(status));
		delete fBuffers;
		fBuffers = NULL;
		return status;
	}

	// Create bitmaps and attach them to the buffer group
	// This is exactly how CodyCam does it
	BRect bounds(0, 0, xSize - 1, ySize - 1);

	for (int j = 0; j < NUM_BUFFERS; j++) {
		// Create BBitmap with contiguous memory that can be shared
		// The 4th parameter 'true' means "accepts child views" which also
		// ensures contiguous memory allocation suitable for buffer sharing
		fBitmap[j] = new BBitmap(bounds, colorspace, false, true);

		if (fBitmap[j]->IsValid()) {
			buffer_clone_info info;

			// Get the memory area for this bitmap
			info.area = area_for(fBitmap[j]->Bits());
			if (info.area == B_ERROR) {
				ERROR("CreateBuffers() - area_for() failed for bitmap %d\n", j);
				DeleteBuffers();
				return B_ERROR;
			}

			info.offset = 0;
			info.size = (size_t)fBitmap[j]->BitsLength();
			info.flags = j;  // Use index as flags for identification
			info.buffer = 0;

			// Add this buffer to the group
			status = fBuffers->AddBuffer(info);
			if (status != B_OK) {
				ERROR("CreateBuffers() - AddBuffer() failed for bitmap %d: %s\n",
					j, strerror(status));
				DeleteBuffers();
				return status;
			}

			LOG_TRACE("CreateBuffers: buffer %d area=%d size=%zu", j, info.area, info.size);
		} else {
			LOG_ERROR("CreateBuffers: bitmap %d invalid", j);
			DeleteBuffers();
			return B_ERROR;
		}
	}

	// Get the BBuffer pointers from the buffer group
	BBuffer* buffList[NUM_BUFFERS];
	for (int j = 0; j < NUM_BUFFERS; j++)
		buffList[j] = NULL;

	status = fBuffers->GetBufferList(NUM_BUFFERS, buffList);
	if (status == B_OK) {
		for (int j = 0; j < NUM_BUFFERS; j++) {
			if (buffList[j] != NULL) {
				fBufferMap[j] = buffList[j];
				LOG_TRACE("CreateBuffers: buffer %d -> BBuffer=%p", j, fBufferMap[j]);
			} else {
				LOG_ERROR("CreateBuffers: buffer %d NULL", j);
				DeleteBuffers();
				return B_ERROR;
			}
		}
	} else {
		LOG_ERROR("CreateBuffers: GetBufferList failed: %s", strerror(status));
		DeleteBuffers();
		return status;
	}

	// Create a separate display bitmap for format conversion if needed
	// This is used when the producer's format differs from what we can display
	delete fDisplayBitmap;
	fDisplayBitmap = new BBitmap(bounds, B_RGB32, false, false);
	if (!fDisplayBitmap->IsValid()) {
		LOG_WARNING("CreateBuffers: display bitmap failed (not fatal)");
		delete fDisplayBitmap;
		fDisplayBitmap = NULL;
	}

	return B_OK;
}


BMediaAddOn*
VideoConsumer::AddOn(int32* internalId) const
{
	// We're not part of an add-on
	if (internalId != NULL)
		*internalId = 0;
	return NULL;
}


void
VideoConsumer::NodeRegistered()
{
	// Set up the input
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.node = Node();
	fDestination = fInput.destination;
	strlcpy(fInput.name, "Video Input", sizeof(fInput.name));

	// Accept raw video with wildcard - let producer decide format
	fInput.format.type = B_MEDIA_RAW_VIDEO;
	fInput.format.u.raw_video = media_raw_video_format::wildcard;

	// Set run mode and start event loop
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();

	LOG_DEBUG("NodeRegistered: port=%d node=%d", fInput.destination.port, fInput.node.node);
}


status_t
VideoConsumer::RequestCompleted(const media_request_info& info)
{
	return B_OK;
}


void
VideoConsumer::SetTimeSource(BTimeSource* timeSource)
{
	BMediaNode::SetTimeSource(timeSource);
}


void
VideoConsumer::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
			LOG_DEBUG("HandleEvent: START");
			break;

		case BTimedEventQueue::B_STOP:
			LOG_DEBUG("HandleEvent: STOP");
			EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			break;

		case BTimedEventQueue::B_HANDLE_BUFFER:
		{
			BBuffer* buffer = const_cast<BBuffer*>(
				static_cast<const BBuffer*>(event->pointer));
			if (buffer != NULL) {
				if (RunState() == BMediaEventLooper::B_STARTED) {
					_HandleBuffer(buffer);
				}
				buffer->Recycle();
			}
			break;
		}

		default:
			break;
	}
}


status_t
VideoConsumer::AcceptFormat(const media_destination& dest, media_format* format)
{
	// Be permissive - accept destination even if it doesn't match exactly

	if (format->type == B_MEDIA_RAW_VIDEO) {
		color_space cs = format->u.raw_video.display.format;

		LOG_DEBUG("AcceptFormat: %dx%d %s @%.0ffps",
			(int)format->u.raw_video.display.line_width,
			(int)format->u.raw_video.display.line_count,
			ColorSpaceName(cs),
			format->u.raw_video.field_rate);

		// Accept common video colorspaces
		switch (cs) {
			case B_RGB32:
			case B_RGBA32:
			case B_RGB24:
			case B_RGB16:
			case B_RGB15:
			case B_GRAY8:
			case B_YCbCr422:
			case B_YUV422:
			case B_YCbCr420:
			case B_YUV420:
			case B_YCbCr444:
			case B_YUV444:
			case B_YUV9:
			case B_YUV12:
				return B_OK;

			case B_NO_COLOR_SPACE:
				// Unspecified - default to RGB32 like CodyCam
				format->u.raw_video.display.format = B_RGB32;
				return B_OK;

			default:
				// Accept anyway for driver testing
				return B_OK;
		}
	}

	// Accept ENCODED_VIDEO (MJPEG) and UNKNOWN_TYPE for driver testing
	if (format->type == B_MEDIA_ENCODED_VIDEO ||
		format->type == B_MEDIA_UNKNOWN_TYPE) {
		LOG_DEBUG("AcceptFormat: type=%d accepted", format->type);
		return B_OK;
	}

	LOG_DEBUG("AcceptFormat: type=%d rejected", format->type);
	return B_MEDIA_BAD_FORMAT;
}


status_t
VideoConsumer::GetNextInput(int32* cookie, media_input* outInput)
{
	if (*cookie != 0)
		return B_BAD_INDEX;

	*outInput = fInput;
	(*cookie)++;

	return B_OK;
}


void
VideoConsumer::DisposeInputCookie(int32 cookie)
{
	// Nothing to do
}


void
VideoConsumer::BufferReceived(BBuffer* buffer)
{
	static int bufferCount = 0;
	bufferCount++;

	LOG_TRACE("BufferReceived: #%d size=%zu state=%d",
		bufferCount, buffer ? buffer->SizeUsed() : 0, RunState());

	if (buffer == NULL) {
		LOG_ERROR("BufferReceived: NULL buffer");
		return;
	}

	// Check if we're running
	if (RunState() != BMediaEventLooper::B_STARTED) {
		LOG_TRACE("BufferReceived: not started, recycling");
		buffer->Recycle();
		return;
	}

	// Queue the buffer for handling
	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);

	status_t status = EventQueue()->AddEvent(event);
	if (status != B_OK) {
		LOG_ERROR("BufferReceived: queue failed: %s", strerror(status));
		buffer->Recycle();
		fFramesDropped++;
	}
}


void
VideoConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atPerformanceTime)
{
	const char* statusStr = "?";
	switch (status) {
		case B_DATA_NOT_AVAILABLE: statusStr = "NOT_AVAILABLE"; break;
		case B_DATA_AVAILABLE: statusStr = "AVAILABLE"; break;
		case B_PRODUCER_STOPPED: statusStr = "STOPPED"; break;
	}
	LOG_DEBUG("ProducerDataStatus: %s", statusStr);
}


status_t
VideoConsumer::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* outLatency, media_node_id* outTimesource)
{
	*outLatency = fInternalLatency;
	*outTimesource = TimeSource()->ID();
	return B_OK;
}


status_t
VideoConsumer::Connected(const media_source& producer,
	const media_destination& where, const media_format& withFormat,
	media_input* outInput)
{
	LOG_INFO("Connected: %d:%d -> %d:%d, %dx%d %s @%.0ffps",
		producer.port, producer.id, where.port, where.id,
		(int)withFormat.u.raw_video.display.line_width,
		(int)withFormat.u.raw_video.display.line_count,
		ColorSpaceName(withFormat.u.raw_video.display.format),
		withFormat.u.raw_video.field_rate);

	// Store connection info (like CodyCam)
	fInput.source = producer;
	fInput.format = withFormat;
	fInput.node = Node();
	strlcpy(fInput.name, "Video Consumer", sizeof(fInput.name));
	fProducerSource = producer;
	fFormat = withFormat;

	*outInput = fInput;

	// Create buffers and pass them to producer
	uint32 userData = 0;
	int32 changeTag = 1;

	status_t status = CreateBuffers(withFormat);
	if (status == B_OK) {
		// Pass our buffer group to the producer
		status = BBufferConsumer::SetOutputBuffersFor(producer, fDestination,
			fBuffers, (void*)&userData, &changeTag, true);

		if (status != B_OK) {
			LOG_DEBUG("SetOutputBuffersFor: %s (may be OK)", strerror(status));
		}
	} else {
		LOG_ERROR("CreateBuffers failed: %s", strerror(status));
	}

	fConnected = true;
	return B_OK;
}


void
VideoConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	LOG_DEBUG("Disconnected");

	if (where != fInput.destination || producer != fInput.source)
		return;

	fConnected = false;
	fInput.source = media_source::null;
	fProducerSource = media_source::null;

	DeleteBuffers();
}


status_t
VideoConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	LOG_DEBUG("FormatChanged: %dx%d %s",
		(int)format.u.raw_video.display.line_width,
		(int)format.u.raw_video.display.line_count,
		ColorSpaceName(format.u.raw_video.display.format));

	if (consumer != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (producer != fInput.source)
		return B_MEDIA_BAD_SOURCE;

	fFormat = format;

	// Recreate buffers for new format
	return CreateBuffers(format);
}


status_t
VideoConsumer::SeekTagRequested(const media_destination& destination,
	bigtime_t inTargetTime, uint32 inFlags, media_seek_tag* outSeekTag,
	bigtime_t* outTaggedTime, uint32* outFlags)
{
	return B_ERROR;  // Not supported
}


void
VideoConsumer::_HandleBuffer(BBuffer* buffer)
{
	if (buffer == NULL)
		return;

	bigtime_t now = system_time();

	// Check if buffer contains MJPEG data (regardless of negotiated format)
	const uint8* bufData = static_cast<const uint8*>(buffer->Data());
	size_t bufSize = buffer->SizeUsed();

	if (_IsMJPEGData(bufData, bufSize)) {
		// MJPEG frame - decompress to display bitmap
		BBitmap* dest = fDisplayBitmap;
		if (dest == NULL && fBitmap[0] != NULL)
			dest = fBitmap[0];

		if (dest != NULL && _DecompressMJPEG(bufData, bufSize, dest)) {
			_SendFrameToTarget(dest);
		} else {
			fFramesDropped++;
		}
	} else {
		// Raw video frame - use original logic
		// Find which bitmap this buffer corresponds to
		BBitmap* srcBitmap = NULL;
		for (int i = 0; i < NUM_BUFFERS; i++) {
			if (fBufferMap[i] == buffer && fBitmap[i] != NULL) {
				srcBitmap = fBitmap[i];
				break;
			}
		}

		if (srcBitmap != NULL) {
			_SendFrameToTarget(srcBitmap);
		} else {
			if (fDisplayBitmap != NULL) {
				_ConvertBuffer(buffer, fDisplayBitmap);
				_SendFrameToTarget(fDisplayBitmap);
			} else if (fBitmap[0] != NULL) {
				_ConvertBuffer(buffer, fBitmap[0]);
				_SendFrameToTarget(fBitmap[0]);
			}
		}
	}

	// Update statistics
	fFramesReceived++;

	if (fLastFrameTime > 0) {
		bigtime_t elapsed = now - fLastFrameTime;
		if (elapsed > 0) {
			float instantFPS = 1000000.0f / elapsed;
			fCurrentFPS = fCurrentFPS * 0.9f + instantFPS * 0.1f;
		}
	}
	fLastFrameTime = now;
}


void
VideoConsumer::_ConvertBuffer(BBuffer* buffer, BBitmap* destBitmap)
{
	if (buffer == NULL || destBitmap == NULL)
		return;

	const uint8* src = static_cast<const uint8*>(buffer->Data());
	uint8* dst = static_cast<uint8*>(destBitmap->Bits());
	size_t srcSize = buffer->SizeUsed();

	color_space srcFormat = fFormat.u.raw_video.display.format;
	int32 width = fFormat.u.raw_video.display.line_width;
	int32 height = fFormat.u.raw_video.display.line_count;
	int32 srcBytesPerRow = fFormat.u.raw_video.display.bytes_per_row;
	int32 dstBytesPerRow = destBitmap->BytesPerRow();

	// Use bitmap dimensions if format dimensions are invalid
	if (width <= 0 || height <= 0) {
		width = fBitmapWidth > 0 ? fBitmapWidth : 320;
		height = fBitmapHeight > 0 ? fBitmapHeight : 240;
	}

	// Calculate source bytes per row if not specified
	if (srcBytesPerRow <= 0) {
		switch (srcFormat) {
			case B_RGB32:
			case B_RGBA32:
				srcBytesPerRow = width * 4;
				break;
			case B_RGB24:
				srcBytesPerRow = width * 3;
				break;
			case B_RGB16:
			case B_RGB15:
			case B_YCbCr422:
			case B_YUV422:
				srcBytesPerRow = width * 2;
				break;
			default:
				srcBytesPerRow = width * 4;
		}
	}

	// Debug log occasionally
	static int32 sConvertCount = 0;
	sConvertCount++;
	if (sConvertCount <= 2 || (sConvertCount % 500) == 0) {
		LOG_TRACE("ConvertBuffer #%d: %zu bytes %dx%d %s",
			(int)sConvertCount, srcSize, (int)width, (int)height, ColorSpaceName(srcFormat));
	}

	// Handle empty buffer
	if (srcSize == 0) {
		// Fill with blue to indicate no data
		memset(dst, 0x40, destBitmap->BitsLength());
		return;
	}

	switch (srcFormat) {
		case B_RGB32:
		case B_RGBA32:
			if (srcBytesPerRow == dstBytesPerRow) {
				memcpy(dst, src, min_c(srcSize, (size_t)destBitmap->BitsLength()));
			} else {
				for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
					memcpy(dst + y * dstBytesPerRow,
						   src + y * srcBytesPerRow,
						   min_c(srcBytesPerRow, dstBytesPerRow));
				}
			}
			break;

		case B_RGB24:
			for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
				const uint8* srcRow = src + y * srcBytesPerRow;
				uint8* dstRow = dst + y * dstBytesPerRow;
				for (int32 x = 0; x < width && x < fBitmapWidth; x++) {
					dstRow[x * 4 + 0] = srcRow[x * 3 + 0];  // B
					dstRow[x * 4 + 1] = srcRow[x * 3 + 1];  // G
					dstRow[x * 4 + 2] = srcRow[x * 3 + 2];  // R
					dstRow[x * 4 + 3] = 255;                 // A
				}
			}
			break;

		case B_RGB16:
			for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
				const uint16* srcRow = reinterpret_cast<const uint16*>(
					src + y * srcBytesPerRow);
				uint8* dstRow = dst + y * dstBytesPerRow;
				for (int32 x = 0; x < width && x < fBitmapWidth; x++) {
					uint16 pixel = srcRow[x];
					dstRow[x * 4 + 0] = (pixel & 0x001F) << 3;        // B
					dstRow[x * 4 + 1] = ((pixel & 0x07E0) >> 3);      // G
					dstRow[x * 4 + 2] = ((pixel & 0xF800) >> 8);      // R
					dstRow[x * 4 + 3] = 255;                           // A
				}
			}
			break;

		case B_YCbCr422:
		case B_YUV422:
			_ConvertYUV422ToBGRA(src, dst, width, height);
			break;

		case B_YCbCr420:
		case B_YUV420:
		case B_YUV9:
		case B_YUV12:
		{
			// YUV 4:2:0 comes in two layouts:
			//   Planar I420: Y plane, then U plane, then V plane (each separate)
			//   Semi-planar NV12: Y plane, then interleaved UV pairs
			//   Semi-planar NV21: Y plane, then interleaved VU pairs
			// Detect based on buffer size: both are width*height*3/2 bytes,
			// but we try NV12 for B_YUV12 and I420 as default for B_YCbCr420.
			if (srcFormat == B_YUV12)
				_ConvertNV12ToBGRA(src, dst, width, height);
			else if (srcFormat == B_YUV9)
				_ConvertNV21ToBGRA(src, dst, width, height);
			else
				_ConvertYUV420ToBGRA(src, dst, width, height);
			break;
		}

		default:
			// Unknown format - fill with checkerboard test pattern
			for (int32 y = 0; y < fBitmapHeight; y++) {
				uint32* row = reinterpret_cast<uint32*>(dst + y * dstBytesPerRow);
				for (int32 x = 0; x < fBitmapWidth; x++) {
					int check = ((x / 16) + (y / 16)) % 2;
					row[x] = check ? 0xFF404040 : 0xFF808080;
				}
			}
			break;
	}
}


void
VideoConsumer::_ConvertYUV422ToBGRA(const uint8* src, uint8* dst,
	int32 width, int32 height)
{
	int32 dstBytesPerRow = fBitmapWidth * 4;
	int32 srcBytesPerRow = width * 2;

	for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
		const uint8* srcRow = src + y * srcBytesPerRow;
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width && x < fBitmapWidth; x += 2) {
			int32 y0 = srcRow[x * 2 + 0];
			int32 u  = srcRow[x * 2 + 1];
			int32 y1 = srcRow[x * 2 + 2];
			int32 v  = srcRow[x * 2 + 3];

			int32 c = y0 - 16;
			int32 d = u - 128;
			int32 e = v - 128;

			int32 r = (298 * c + 409 * e + 128) >> 8;
			int32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			int32 b = (298 * c + 516 * d + 128) >> 8;

			dstRow[x * 4 + 0] = (uint8)max_c(0, min_c(255, b));
			dstRow[x * 4 + 1] = (uint8)max_c(0, min_c(255, g));
			dstRow[x * 4 + 2] = (uint8)max_c(0, min_c(255, r));
			dstRow[x * 4 + 3] = 255;

			c = y1 - 16;
			r = (298 * c + 409 * e + 128) >> 8;
			g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			b = (298 * c + 516 * d + 128) >> 8;

			if (x + 1 < fBitmapWidth) {
				dstRow[(x + 1) * 4 + 0] = (uint8)max_c(0, min_c(255, b));
				dstRow[(x + 1) * 4 + 1] = (uint8)max_c(0, min_c(255, g));
				dstRow[(x + 1) * 4 + 2] = (uint8)max_c(0, min_c(255, r));
				dstRow[(x + 1) * 4 + 3] = 255;
			}
		}
	}
}


void
VideoConsumer::_ConvertYUV420ToBGRA(const uint8* src, uint8* dst,
	int32 width, int32 height)
{
	int32 dstBytesPerRow = fBitmapWidth * 4;

	const uint8* yPlane = src;
	const uint8* uPlane = src + width * height;
	const uint8* vPlane = uPlane + (width * height) / 4;

	for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width && x < fBitmapWidth; x++) {
			int32 yValue = yPlane[y * width + x];
			int32 u = uPlane[(y / 2) * (width / 2) + (x / 2)];
			int32 v = vPlane[(y / 2) * (width / 2) + (x / 2)];

			int32 c = yValue - 16;
			int32 d = u - 128;
			int32 e = v - 128;

			int32 r = (298 * c + 409 * e + 128) >> 8;
			int32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			int32 b = (298 * c + 516 * d + 128) >> 8;

			dstRow[x * 4 + 0] = (uint8)max_c(0, min_c(255, b));
			dstRow[x * 4 + 1] = (uint8)max_c(0, min_c(255, g));
			dstRow[x * 4 + 2] = (uint8)max_c(0, min_c(255, r));
			dstRow[x * 4 + 3] = 255;
		}
	}
}


void
VideoConsumer::_ConvertNV12ToBGRA(const uint8* src, uint8* dst,
	int32 width, int32 height)
{
	// NV12: Y plane followed by interleaved UV plane
	int32 dstBytesPerRow = fBitmapWidth * 4;

	const uint8* yPlane = src;
	const uint8* uvPlane = src + width * height;

	for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
		uint8* dstRow = dst + y * dstBytesPerRow;
		const uint8* uvRow = uvPlane + (y / 2) * width;

		for (int32 x = 0; x < width && x < fBitmapWidth; x++) {
			int32 yValue = yPlane[y * width + x];
			int32 u = uvRow[(x & ~1) + 0];
			int32 v = uvRow[(x & ~1) + 1];

			int32 c = yValue - 16;
			int32 d = u - 128;
			int32 e = v - 128;

			int32 r = (298 * c + 409 * e + 128) >> 8;
			int32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			int32 b = (298 * c + 516 * d + 128) >> 8;

			dstRow[x * 4 + 0] = (uint8)max_c(0, min_c(255, b));
			dstRow[x * 4 + 1] = (uint8)max_c(0, min_c(255, g));
			dstRow[x * 4 + 2] = (uint8)max_c(0, min_c(255, r));
			dstRow[x * 4 + 3] = 255;
		}
	}
}


void
VideoConsumer::_ConvertNV21ToBGRA(const uint8* src, uint8* dst,
	int32 width, int32 height)
{
	// NV21: Y plane followed by interleaved VU plane (V first, then U)
	int32 dstBytesPerRow = fBitmapWidth * 4;

	const uint8* yPlane = src;
	const uint8* vuPlane = src + width * height;

	for (int32 y = 0; y < height && y < fBitmapHeight; y++) {
		uint8* dstRow = dst + y * dstBytesPerRow;
		const uint8* vuRow = vuPlane + (y / 2) * width;

		for (int32 x = 0; x < width && x < fBitmapWidth; x++) {
			int32 yValue = yPlane[y * width + x];
			int32 v = vuRow[(x & ~1) + 0];
			int32 u = vuRow[(x & ~1) + 1];

			int32 c = yValue - 16;
			int32 d = u - 128;
			int32 e = v - 128;

			int32 r = (298 * c + 409 * e + 128) >> 8;
			int32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			int32 b = (298 * c + 516 * d + 128) >> 8;

			dstRow[x * 4 + 0] = (uint8)max_c(0, min_c(255, b));
			dstRow[x * 4 + 1] = (uint8)max_c(0, min_c(255, g));
			dstRow[x * 4 + 2] = (uint8)max_c(0, min_c(255, r));
			dstRow[x * 4 + 3] = 255;
		}
	}
}


bool
VideoConsumer::_IsMJPEGData(const uint8* data, size_t size) const
{
	// JPEG/MJPEG frames start with SOI marker 0xFFD8
	return size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}


bool
VideoConsumer::_DecompressMJPEG(const uint8* src, size_t srcSize,
	BBitmap* destBitmap)
{
	if (src == NULL || srcSize < 2 || destBitmap == NULL)
		return false;

	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);

	// Override error_exit to prevent libjpeg from calling exit()
	jerr.error_exit = [](j_common_ptr cinfo) {
		// longjmp back to our setjmp point
		// We use the client_data field to store the jmp_buf pointer
		jmp_buf* jumpBuffer = static_cast<jmp_buf*>(cinfo->client_data);
		longjmp(*jumpBuffer, 1);
	};

	jmp_buf jumpBuffer;
	cinfo.client_data = &jumpBuffer;

	if (setjmp(jumpBuffer)) {
		// Error during decompression
		jpeg_destroy_decompress(&cinfo);
		static int32 sErrorCount = 0;
		if (++sErrorCount <= 5)
			LOG_ERROR("MJPEG decompress failed (error %d)", (int)sErrorCount);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, src, srcSize);

	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	// Request BGRA output (Haiku's native B_RGB32 is actually BGRA)
	cinfo.out_color_space = JCS_EXT_BGRA;

	jpeg_start_decompress(&cinfo);

	uint8* dst = static_cast<uint8*>(destBitmap->Bits());
	int32 dstBytesPerRow = destBitmap->BytesPerRow();
	int32 destHeight = destBitmap->Bounds().IntegerHeight() + 1;

	// If the JPEG dimensions differ from the bitmap, we need a new display bitmap
	// For now, decompress what fits
	int32 rows = min_c((int32)cinfo.output_height, destHeight);

	while (cinfo.output_scanline < (JDIMENSION)rows) {
		uint8* rowPtr = dst + cinfo.output_scanline * dstBytesPerRow;
		JSAMPROW scanline = rowPtr;
		jpeg_read_scanlines(&cinfo, &scanline, 1);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	static int32 sDecodeCount = 0;
	sDecodeCount++;
	if (sDecodeCount <= 2 || (sDecodeCount % 500) == 0) {
		LOG_DEBUG("MJPEG decoded #%d: %dx%d from %zu bytes",
			(int)sDecodeCount, (int)cinfo.output_width,
			(int)cinfo.output_height, srcSize);
	}

	return true;
}


void
VideoConsumer::SetTarget(BLooper* target)
{
	BAutolock lock(fTargetLock);
	fTarget = target;
}


void
VideoConsumer::_SendFrameToTarget(BBitmap* bitmap)
{
	if (bitmap == NULL)
		return;

	// CRITICAL FIX: Copy target pointer under lock, then release lock BEFORE
	// calling PostMessage. Holding a lock while calling PostMessage can cause
	// deadlock if the target's message handler tries to acquire a lock that
	// we're waiting for (classic lock inversion deadlock).
	BLooper* target = NULL;
	{
		BAutolock lock(fTargetLock);
		target = fTarget;
	}
	// Lock released here - safe to call PostMessage

	if (target == NULL)
		return;

	BMessage msg(fFrameMessage);
	msg.AddPointer("bitmap", bitmap);

	// Use PostMessage with timeout to avoid blocking indefinitely
	// if the target's message queue is full or target is busy
	status_t status = target->PostMessage(&msg);
	if (status != B_OK) {
		// Target may have been deleted or queue full - not fatal
		LOG_TRACE("PostMessage failed: %s", strerror(status));
	}
}
