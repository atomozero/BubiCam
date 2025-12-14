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


// Timing constants
const bigtime_t kMaxLatency = 50000;  // 50ms

// Debug macros (similar to CodyCam)
#define INFO(x...) fprintf(stderr, "VideoConsumer: " x)
#define ERROR(x...) fprintf(stderr, "VideoConsumer ERROR: " x)
#define PROGRESS(x...) fprintf(stderr, "VideoConsumer: " x)


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

	INFO("VideoConsumer created\n");
}


VideoConsumer::~VideoConsumer()
{
	INFO("~VideoConsumer() - cleaning up\n");

	Quit();
	DeleteBuffers();

	delete fDisplayBitmap;
	fDisplayBitmap = NULL;

	INFO("~VideoConsumer() - done\n");
}


void
VideoConsumer::DeleteBuffers()
{
	INFO("DeleteBuffers()\n");

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
	INFO("CreateBuffers() - creating CodyCam-style buffer group\n");

	// Delete any old buffers first
	DeleteBuffers();

	status_t status = B_OK;

	// Get dimensions from format
	uint32 xSize = format.u.raw_video.display.line_width;
	uint32 ySize = format.u.raw_video.display.line_count;
	color_space colorspace = format.u.raw_video.display.format;

	// Validate and fix dimensions
	if (xSize == 0 || ySize == 0) {
		INFO("CreateBuffers() - format has 0x0 dimensions, using 320x240 default\n");
		xSize = 320;
		ySize = 240;
	}

	// Validate colorspace
	if (colorspace == 0) {
		INFO("CreateBuffers() - format has no colorspace, using B_RGB32\n");
		colorspace = B_RGB32;
	}

	INFO("CreateBuffers() - creating %d buffers at %dx%d, colorspace=0x%x\n",
		NUM_BUFFERS, (int)xSize, (int)ySize, (int)colorspace);

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

			PROGRESS("CreateBuffers() - added buffer %d: area=%d, size=%zu\n",
				j, info.area, info.size);
		} else {
			ERROR("CreateBuffers() - bitmap %d is invalid\n", j);
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
				PROGRESS("CreateBuffers() - buffer %d mapped: BBuffer=%p\n",
					j, fBufferMap[j]);
			} else {
				ERROR("CreateBuffers() - buffer %d is NULL in list\n", j);
				DeleteBuffers();
				return B_ERROR;
			}
		}
	} else {
		ERROR("CreateBuffers() - GetBufferList() failed: %s\n", strerror(status));
		DeleteBuffers();
		return status;
	}

	// Create a separate display bitmap for format conversion if needed
	// This is used when the producer's format differs from what we can display
	delete fDisplayBitmap;
	fDisplayBitmap = new BBitmap(bounds, B_RGB32, false, false);
	if (!fDisplayBitmap->IsValid()) {
		ERROR("CreateBuffers() - failed to create display bitmap\n");
		delete fDisplayBitmap;
		fDisplayBitmap = NULL;
		// Not fatal - we can still display directly if format matches
	}

	INFO("CreateBuffers() - SUCCESS! Buffer group created with %d buffers\n", NUM_BUFFERS);
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
	INFO("NodeRegistered() called\n");

	// Set up the input
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.node = Node();
	fDestination = fInput.destination;
	strlcpy(fInput.name, "Video Input", sizeof(fInput.name));

	// Accept raw video with wildcard - let producer decide format
	fInput.format.type = B_MEDIA_RAW_VIDEO;
	fInput.format.u.raw_video = media_raw_video_format::wildcard;

	INFO("NodeRegistered() - input configured: port=%d, node=%d, dest.id=%d\n",
		fInput.destination.port, fInput.node.node, fInput.destination.id);

	// Set run mode and start event loop
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();

	INFO("NodeRegistered() - event loop started, RunState=%d\n", RunState());
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
			INFO("HandleEvent - B_START\n");
			break;

		case BTimedEventQueue::B_STOP:
			INFO("HandleEvent - B_STOP\n");
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
	INFO("AcceptFormat() called\n");
	INFO("  format type = %d (RAW_VIDEO=%d, ENCODED_VIDEO=%d)\n",
		format->type, B_MEDIA_RAW_VIDEO, B_MEDIA_ENCODED_VIDEO);

	// Be permissive - accept destination even if it doesn't match exactly
	// (some buggy drivers might send wrong destination)

	// Log format details for debugging
	if (format->type == B_MEDIA_RAW_VIDEO) {
		INFO("  RAW_VIDEO: %dx%d, colorspace=0x%x, field_rate=%.2f, bpr=%d\n",
			(int)format->u.raw_video.display.line_width,
			(int)format->u.raw_video.display.line_count,
			(int)format->u.raw_video.display.format,
			format->u.raw_video.field_rate,
			(int)format->u.raw_video.display.bytes_per_row);
	}

	// Accept RAW_VIDEO formats
	// CodyCam accepts: B_RGB32, B_RGB16, B_RGB15, B_GRAY8
	// We'll be more permissive and accept YUV formats too since we can convert
	if (format->type == B_MEDIA_RAW_VIDEO) {
		color_space cs = format->u.raw_video.display.format;

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
				INFO("  -> ACCEPTED (colorspace 0x%x)\n", cs);
				return B_OK;

			case B_NO_COLOR_SPACE:
				// Unspecified - default to RGB32 like CodyCam
				format->u.raw_video.display.format = B_RGB32;
				INFO("  -> ACCEPTED (defaulted to RGB32)\n");
				return B_OK;

			default:
				// Accept anyway for driver testing - we'll show a test pattern
				INFO("  -> ACCEPTED (unknown colorspace 0x%x, will use test pattern)\n", cs);
				return B_OK;
		}
	}

	// Accept ENCODED_VIDEO too (for MJPEG drivers)
	if (format->type == B_MEDIA_ENCODED_VIDEO) {
		INFO("  -> ACCEPTED (ENCODED_VIDEO)\n");
		return B_OK;
	}

	// Accept UNKNOWN_TYPE for permissive driver testing
	if (format->type == B_MEDIA_UNKNOWN_TYPE) {
		INFO("  -> ACCEPTED (UNKNOWN_TYPE for driver testing)\n");
		return B_OK;
	}

	INFO("  -> REJECTED (type %d not supported)\n", format->type);
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

	if (bufferCount <= 5 || bufferCount % 100 == 0) {
		INFO("BufferReceived() - buffer #%d, size=%zu, RunState=%d\n",
			bufferCount, buffer ? buffer->SizeUsed() : 0, RunState());
	}

	if (buffer == NULL) {
		ERROR("BufferReceived() - NULL buffer!\n");
		return;
	}

	// Check if we're running
	if (RunState() != BMediaEventLooper::B_STARTED) {
		if (bufferCount <= 10) {
			INFO("BufferReceived() - not started (state=%d), recycling\n", RunState());
		}
		buffer->Recycle();
		return;
	}

	// Queue the buffer for handling
	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);

	status_t status = EventQueue()->AddEvent(event);
	if (status != B_OK) {
		ERROR("BufferReceived() - failed to queue buffer: %s\n", strerror(status));
		buffer->Recycle();
		fFramesDropped++;
	}
}


void
VideoConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atPerformanceTime)
{
	const char* statusStr = "unknown";
	switch (status) {
		case B_DATA_NOT_AVAILABLE: statusStr = "B_DATA_NOT_AVAILABLE"; break;
		case B_DATA_AVAILABLE: statusStr = "B_DATA_AVAILABLE"; break;
		case B_PRODUCER_STOPPED: statusStr = "B_PRODUCER_STOPPED"; break;
	}
	INFO("ProducerDataStatus() - status=%s (%d)\n", statusStr, (int)status);
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
	INFO("Connected() called\n");
	INFO("  producer: port=%d, id=%d\n", producer.port, producer.id);
	INFO("  where: port=%d, id=%d\n", where.port, where.id);
	INFO("  format: type=%d, %dx%d, colorspace=0x%x, bpr=%d, fps=%.2f\n",
		withFormat.type,
		(int)withFormat.u.raw_video.display.line_width,
		(int)withFormat.u.raw_video.display.line_count,
		(int)withFormat.u.raw_video.display.format,
		(int)withFormat.u.raw_video.display.bytes_per_row,
		withFormat.u.raw_video.field_rate);

	// Store connection info (like CodyCam)
	fInput.source = producer;
	fInput.format = withFormat;
	fInput.node = Node();
	strlcpy(fInput.name, "Video Consumer", sizeof(fInput.name));
	fProducerSource = producer;
	fFormat = withFormat;

	*outInput = fInput;

	// THIS IS THE CRITICAL PART - Create buffers and pass them to producer!
	uint32 userData = 0;
	int32 changeTag = 1;

	status_t status = CreateBuffers(withFormat);
	if (status == B_OK) {
		INFO("Connected() - buffers created, calling SetOutputBuffersFor()\n");

		// Pass our buffer group to the producer
		// This tells the producer to use OUR buffers instead of creating its own
		status = BBufferConsumer::SetOutputBuffersFor(producer, fDestination,
			fBuffers, (void*)&userData, &changeTag, true);

		if (status != B_OK) {
			// Not all producers support this, so don't fail
			INFO("Connected() - SetOutputBuffersFor() returned %s (this may be OK)\n",
				strerror(status));
		} else {
			INFO("Connected() - SetOutputBuffersFor() SUCCESS!\n");
		}
	} else {
		ERROR("Connected() - CreateBuffers() failed: %s\n", strerror(status));
		// Don't fail the connection - the producer might create its own buffers
	}

	fConnected = true;
	INFO("Connected() - SUCCESS!\n");
	return B_OK;
}


void
VideoConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	INFO("Disconnected() called\n");

	if (where != fInput.destination || producer != fInput.source) {
		INFO("Disconnected() - wrong source/destination, ignoring\n");
		return;
	}

	fConnected = false;
	fInput.source = media_source::null;
	fProducerSource = media_source::null;

	DeleteBuffers();

	INFO("Disconnected() - cleanup complete\n");
}


status_t
VideoConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	INFO("FormatChanged() called\n");

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

	// Find which bitmap this buffer corresponds to
	// If we created the buffers, we can find the matching bitmap
	BBitmap* srcBitmap = NULL;
	for (int i = 0; i < NUM_BUFFERS; i++) {
		if (fBufferMap[i] == buffer && fBitmap[i] != NULL) {
			srcBitmap = fBitmap[i];
			break;
		}
	}

	// If we found a matching bitmap, we can send it directly
	// Otherwise, convert the buffer data to our display bitmap
	if (srcBitmap != NULL) {
		// Buffer is one of ours - send the bitmap directly
		_SendFrameToTarget(srcBitmap);
	} else {
		// Buffer is from producer's own allocation - convert it
		if (fDisplayBitmap != NULL) {
			_ConvertBuffer(buffer, fDisplayBitmap);
			_SendFrameToTarget(fDisplayBitmap);
		} else if (fBitmap[0] != NULL) {
			// Use first buffer bitmap as fallback
			_ConvertBuffer(buffer, fBitmap[0]);
			_SendFrameToTarget(fBitmap[0]);
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
	if (sConvertCount <= 5 || (sConvertCount % 500) == 0) {
		INFO("_ConvertBuffer #%d: srcSize=%zu, format=0x%x, %dx%d\n",
			(int)sConvertCount, srcSize, (int)srcFormat, (int)width, (int)height);
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
			_ConvertYUV420ToBGRA(src, dst, width, height);
			break;

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

	BAutolock lock(fTargetLock);
	if (fTarget == NULL)
		return;

	BMessage msg(fFrameMessage);
	msg.AddPointer("bitmap", bitmap);

	fTarget->PostMessage(&msg);
}
