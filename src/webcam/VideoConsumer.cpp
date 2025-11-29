/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoConsumer.h"
#include "MainWindow.h"

#include <Buffer.h>
#include <BufferGroup.h>
#include <TimeSource.h>
#include <MediaRoster.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


// Timing constants
const bigtime_t kMaxLatency = 50000;  // 50ms


VideoConsumer::VideoConsumer(const char* name, BLooper* target,
	uint32 frameMessage, uint32 audioMessage)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(B_MEDIA_RAW_VIDEO),
	fTarget(target),
	fFrameMessage(frameMessage),
	fAudioMessage(audioMessage),
	fConnected(false),
	fBitmap(NULL),
	fBitmapWidth(0),
	fBitmapHeight(0),
	fBitmapColorSpace(B_RGB32),
	fFramesReceived(0),
	fFramesDropped(0),
	fCurrentFPS(0.0f),
	fLastFrameTime(0),
	fInternalLatency(kMaxLatency)
{
	memset(&fInput, 0, sizeof(fInput));
	memset(&fFormat, 0, sizeof(fFormat));
}


VideoConsumer::~VideoConsumer()
{
	Quit();
	delete fBitmap;
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
	strcpy(fInput.name, "Video Input");

	// Set up accepted format (any raw video)
	fInput.format.type = B_MEDIA_RAW_VIDEO;
	fInput.format.u.raw_video = media_raw_video_format::wildcard;

	// Set run mode
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
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
			break;

		case BTimedEventQueue::B_STOP:
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
VideoConsumer::AcceptFormat(const media_destination& dest,
	media_format* format)
{
	if (dest != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (format->type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	// Accept common video formats
	color_space colorSpace = format->u.raw_video.display.format;
	if (colorSpace != B_RGB32 && colorSpace != B_RGBA32 &&
		colorSpace != B_RGB24 && colorSpace != B_RGB16 &&
		colorSpace != B_YCbCr422 && colorSpace != B_YCbCr420 &&
		colorSpace != B_YUV422 && colorSpace != B_YUV420) {
		// Try to negotiate to RGB32
		format->u.raw_video.display.format = B_RGB32;
	}

	return B_OK;
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
	if (buffer == NULL)
		return;

	// Check if we're running
	if (RunState() != BMediaEventLooper::B_STARTED) {
		buffer->Recycle();
		return;
	}

	// Queue the buffer for handling
	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);

	status_t status = EventQueue()->AddEvent(event);
	if (status != B_OK) {
		buffer->Recycle();
		fFramesDropped++;
	}
}


void
VideoConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atPerformanceTime)
{
	// Handle producer status changes if needed
}


status_t
VideoConsumer::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* outLatency, media_node_id* outTimesource)
{
	if (forWhom != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	*outLatency = fInternalLatency;
	*outTimesource = TimeSource()->ID();

	return B_OK;
}


status_t
VideoConsumer::Connected(const media_source& producer,
	const media_destination& where, const media_format& withFormat,
	media_input* outInput)
{
	if (where != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	fInput.source = producer;
	fFormat = withFormat;
	fConnected = true;

	// Create bitmap for the format
	status_t status = _CreateBufferBitmap(withFormat);
	if (status != B_OK) {
		fConnected = false;
		return status;
	}

	*outInput = fInput;

	return B_OK;
}


void
VideoConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	if (where != fInput.destination || producer != fInput.source)
		return;

	fConnected = false;
	fInput.source = media_source::null;

	delete fBitmap;
	fBitmap = NULL;
}


status_t
VideoConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	if (consumer != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (producer != fInput.source)
		return B_MEDIA_BAD_SOURCE;

	fFormat = format;

	// Recreate bitmap for new format
	return _CreateBufferBitmap(format);
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
	if (buffer == NULL || fBitmap == NULL)
		return;

	bigtime_t now = system_time();

	// Convert buffer to bitmap
	_ConvertBuffer(buffer);

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

	// Send frame to target
	_SendFrameToTarget();
}


status_t
VideoConsumer::_CreateBufferBitmap(const media_format& format)
{
	delete fBitmap;
	fBitmap = NULL;

	if (format.type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	fBitmapWidth = format.u.raw_video.display.line_width;
	fBitmapHeight = format.u.raw_video.display.line_count;

	if (fBitmapWidth <= 0 || fBitmapHeight <= 0) {
		// Use default size
		fBitmapWidth = 640;
		fBitmapHeight = 480;
	}

	// Always create RGB32 bitmap for display
	fBitmapColorSpace = B_RGB32;

	BRect bounds(0, 0, fBitmapWidth - 1, fBitmapHeight - 1);
	fBitmap = new BBitmap(bounds, fBitmapColorSpace);

	if (!fBitmap->IsValid()) {
		delete fBitmap;
		fBitmap = NULL;
		return B_NO_MEMORY;
	}

	// Clear to black
	memset(fBitmap->Bits(), 0, fBitmap->BitsLength());

	return B_OK;
}


void
VideoConsumer::_ConvertBuffer(BBuffer* buffer)
{
	if (buffer == NULL || fBitmap == NULL)
		return;

	const uint8* src = static_cast<const uint8*>(buffer->Data());
	uint8* dst = static_cast<uint8*>(fBitmap->Bits());
	size_t srcSize = buffer->SizeUsed();

	color_space srcFormat = fFormat.u.raw_video.display.format;
	int32 width = fFormat.u.raw_video.display.line_width;
	int32 height = fFormat.u.raw_video.display.line_count;
	int32 srcBytesPerRow = fFormat.u.raw_video.display.bytes_per_row;
	int32 dstBytesPerRow = fBitmap->BytesPerRow();

	switch (srcFormat) {
		case B_RGB32:
		case B_RGBA32:
			// Direct copy (may need row-by-row if stride differs)
			if (srcBytesPerRow == dstBytesPerRow) {
				memcpy(dst, src, min_c(srcSize, (size_t)fBitmap->BitsLength()));
			} else {
				for (int32 y = 0; y < height; y++) {
					memcpy(dst + y * dstBytesPerRow,
						   src + y * srcBytesPerRow,
						   min_c(srcBytesPerRow, dstBytesPerRow));
				}
			}
			break;

		case B_RGB24:
			// Convert RGB24 to RGB32
			for (int32 y = 0; y < height; y++) {
				const uint8* srcRow = src + y * srcBytesPerRow;
				uint8* dstRow = dst + y * dstBytesPerRow;
				for (int32 x = 0; x < width; x++) {
					dstRow[x * 4 + 0] = srcRow[x * 3 + 0];  // B
					dstRow[x * 4 + 1] = srcRow[x * 3 + 1];  // G
					dstRow[x * 4 + 2] = srcRow[x * 3 + 2];  // R
					dstRow[x * 4 + 3] = 255;                 // A
				}
			}
			break;

		case B_RGB16:
			// Convert RGB16 (565) to RGB32
			for (int32 y = 0; y < height; y++) {
				const uint16* srcRow = reinterpret_cast<const uint16*>(
					src + y * srcBytesPerRow);
				uint8* dstRow = dst + y * dstBytesPerRow;
				for (int32 x = 0; x < width; x++) {
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
			// Unknown format - fill with test pattern
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
	// YUV422 format: YUYV (2 pixels per 4 bytes)
	int32 dstBytesPerRow = fBitmap->BytesPerRow();
	int32 srcBytesPerRow = width * 2;  // 2 bytes per pixel in YUV422

	for (int32 y = 0; y < height; y++) {
		const uint8* srcRow = src + y * srcBytesPerRow;
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width; x += 2) {
			int32 y0 = srcRow[x * 2 + 0];
			int32 u  = srcRow[x * 2 + 1];
			int32 y1 = srcRow[x * 2 + 2];
			int32 v  = srcRow[x * 2 + 3];

			// Convert YUV to RGB for first pixel
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

			// Convert YUV to RGB for second pixel
			c = y1 - 16;

			r = (298 * c + 409 * e + 128) >> 8;
			g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			b = (298 * c + 516 * d + 128) >> 8;

			dstRow[(x + 1) * 4 + 0] = (uint8)max_c(0, min_c(255, b));
			dstRow[(x + 1) * 4 + 1] = (uint8)max_c(0, min_c(255, g));
			dstRow[(x + 1) * 4 + 2] = (uint8)max_c(0, min_c(255, r));
			dstRow[(x + 1) * 4 + 3] = 255;
		}
	}
}


void
VideoConsumer::_ConvertYUV420ToBGRA(const uint8* src, uint8* dst,
	int32 width, int32 height)
{
	// YUV420 planar format: Y plane, then U plane (1/4 size), then V plane
	int32 dstBytesPerRow = fBitmap->BytesPerRow();

	const uint8* yPlane = src;
	const uint8* uPlane = src + width * height;
	const uint8* vPlane = uPlane + (width * height) / 4;

	for (int32 y = 0; y < height; y++) {
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width; x++) {
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
VideoConsumer::_SendFrameToTarget()
{
	if (fTarget == NULL || fBitmap == NULL)
		return;

	BMessage msg(fFrameMessage);
	msg.AddPointer("bitmap", fBitmap);

	fTarget->PostMessage(&msg);
}
