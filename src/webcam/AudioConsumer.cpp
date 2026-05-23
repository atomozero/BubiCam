/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "AudioConsumer.h"
#include "MainWindow.h"

#include <Autolock.h>
#include <Buffer.h>
#include <TimeSource.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "AudioConsumer"
#include "ErrorUtils.h"


const bigtime_t kAudioLatency = 10000;  // 10ms
const bigtime_t kLevelUpdateInterval = 50000;  // 50ms between VU updates


AudioConsumer::AudioConsumer(const char* name, BLooper* target,
	uint32 levelMessage)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(B_MEDIA_RAW_AUDIO),
	fTarget(target),
	fLevelMessage(levelMessage),
	fConnected(false),
	fInternalLatency(kAudioLatency),
	fLastLevelTime(0)
{
	fInput = media_input();
	fFormat = media_format();
}


AudioConsumer::~AudioConsumer()
{
	// Stop the event looper and wait for its thread to exit
	// before destroying member locks/state
	thread_id looperThread = ControlThread();
	Quit();
	if (looperThread >= 0) {
		// Wait with timeout to prevent hang on exit
		bigtime_t deadline = system_time() + 2000000;  // 2 seconds
		bool exited = false;
		while (system_time() < deadline) {
			thread_info info;
			if (get_thread_info(looperThread, &info) != B_OK) {
				exited = true;
				break;
			}
			snooze(50000);  // 50ms
		}
		if (exited) {
			status_t exitValue;
			wait_for_thread(looperThread, &exitValue);
		} else {
			fprintf(stderr, "AudioConsumer: looper thread did not exit in time\n");
		}
	}
}


BMediaAddOn*
AudioConsumer::AddOn(int32* internalId) const
{
	if (internalId != NULL)
		*internalId = 0;
	return NULL;
}


void
AudioConsumer::NodeRegistered()
{
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.node = Node();
	strlcpy(fInput.name, "Audio Input", sizeof(fInput.name));

	fInput.format.type = B_MEDIA_RAW_AUDIO;
	fInput.format.u.raw_audio = media_raw_audio_format::wildcard;

	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


status_t
AudioConsumer::RequestCompleted(const media_request_info& info)
{
	return B_OK;
}


void
AudioConsumer::HandleEvent(const media_timed_event* event,
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
AudioConsumer::AcceptFormat(const media_destination& dest,
	media_format* format)
{
	if (dest != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (format->type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	return B_OK;
}


status_t
AudioConsumer::GetNextInput(int32* cookie, media_input* outInput)
{
	if (*cookie != 0)
		return B_BAD_INDEX;

	*outInput = fInput;
	(*cookie)++;

	return B_OK;
}


void
AudioConsumer::DisposeInputCookie(int32 cookie)
{
}


void
AudioConsumer::BufferReceived(BBuffer* buffer)
{
	if (buffer == NULL)
		return;

	if (RunState() != BMediaEventLooper::B_STARTED) {
		buffer->Recycle();
		return;
	}

	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);

	if (EventQueue()->AddEvent(event) != B_OK)
		buffer->Recycle();
}


void
AudioConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atPerformanceTime)
{
}


status_t
AudioConsumer::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* outLatency, media_node_id* outTimesource)
{
	if (forWhom != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	*outLatency = fInternalLatency;
	*outTimesource = TimeSource()->ID();

	return B_OK;
}


status_t
AudioConsumer::Connected(const media_source& producer,
	const media_destination& where, const media_format& withFormat,
	media_input* outInput)
{
	if (where != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	fInput.source = producer;
	fFormat = withFormat;
	fConnected = true;

	*outInput = fInput;

	// Log the negotiated audio format
	if (withFormat.type == B_MEDIA_RAW_AUDIO) {
		const char* fmtName = "unknown";
		switch (withFormat.u.raw_audio.format) {
			case media_raw_audio_format::B_AUDIO_UCHAR: fmtName = "uint8"; break;
			case media_raw_audio_format::B_AUDIO_SHORT: fmtName = "int16"; break;
			case media_raw_audio_format::B_AUDIO_INT:   fmtName = "int32"; break;
			case media_raw_audio_format::B_AUDIO_FLOAT: fmtName = "float"; break;
		}
		LOG_INFO("Audio connected: %g Hz, %d ch, %s, byte_order=%d, buf=%zu",
			withFormat.u.raw_audio.frame_rate,
			(int)withFormat.u.raw_audio.channel_count,
			fmtName,
			(int)withFormat.u.raw_audio.byte_order,
			(size_t)withFormat.u.raw_audio.buffer_size);
	}

	return B_OK;
}


void
AudioConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	if (where != fInput.destination || producer != fInput.source)
		return;

	fConnected = false;
	fInput.source = media_source::null;
}


status_t
AudioConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 changeTag,
	const media_format& format)
{
	if (consumer != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	fFormat = format;
	return B_OK;
}


void
AudioConsumer::SetTarget(BLooper* target)
{
	BAutolock lock(fTargetLock);
	fTarget = target;
}


void
AudioConsumer::_HandleBuffer(BBuffer* buffer)
{
	if (buffer == NULL)
		return;

	// Log first buffer details for debugging
	static int32 sBufferCount = 0;
	if (++sBufferCount <= 2) {
		size_t bufSize = buffer->SizeUsed();
		int32 ch = fFormat.u.raw_audio.channel_count;
		uint32 fmt = fFormat.u.raw_audio.format;
		int32 sampleSize = 2;
		switch (fmt) {
			case media_raw_audio_format::B_AUDIO_UCHAR: sampleSize = 1; break;
			case media_raw_audio_format::B_AUDIO_SHORT: sampleSize = 2; break;
			case media_raw_audio_format::B_AUDIO_INT:   sampleSize = 4; break;
			case media_raw_audio_format::B_AUDIO_FLOAT: sampleSize = 4; break;
		}
		int32 expectedFrameSize = ch * sampleSize;
		LOG_INFO("Audio buffer #%d: %zu bytes, %d ch x %d bytes = %d per frame, "
			"frames=%zu, byte_order=%d",
			(int)sBufferCount, bufSize, (int)ch, (int)sampleSize,
			(int)expectedFrameSize,
			expectedFrameSize > 0 ? bufSize / expectedFrameSize : 0,
			(int)fFormat.u.raw_audio.byte_order);

		// Dump first 16 bytes to see raw data
		if (bufSize >= 16) {
			const uint8* raw = static_cast<const uint8*>(buffer->Data());
			LOG_INFO("Audio raw: %02x %02x %02x %02x %02x %02x %02x %02x "
				"%02x %02x %02x %02x %02x %02x %02x %02x",
				raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
				raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
		}
	}

	// Copy target under lock, then post messages outside lock
	// to prevent deadlock with StopCapture → SetTarget(NULL)
	BLooper* target = NULL;
	{
		BAutolock lock(fTargetLock);
		target = fTarget;
	}

	if (target == NULL)
		return;

	// Always send raw audio data for recording
	size_t dataSize = buffer->SizeUsed();
	if (dataSize > 0) {
		BMessage audioMsg(MSG_AUDIO_BUFFER);
		audioMsg.AddData("audio_data", B_RAW_TYPE, buffer->Data(), dataSize);
		audioMsg.AddInt64("size", (int64)dataSize);
		target->PostMessage(&audioMsg);
	}

	bigtime_t now = system_time();

	// Throttle level updates
	if (now - fLastLevelTime < kLevelUpdateInterval)
		return;

	fLastLevelTime = now;

	// Calculate audio levels
	float left = 0.0f, right = 0.0f;
	_CalculateLevels(buffer->Data(), buffer->SizeUsed(), &left, &right);

	if (target != NULL) {
		BMessage msg(fLevelMessage);
		msg.AddFloat("left", left);
		msg.AddFloat("right", right);
		target->PostMessage(&msg);
	}
}


void
AudioConsumer::_CalculateLevels(const void* data, size_t size,
	float* outLeft, float* outRight)
{
	*outLeft = 0.0f;
	*outRight = 0.0f;

	if (data == NULL || size == 0)
		return;

	uint32 format = fFormat.u.raw_audio.format;
	int32 channels = fFormat.u.raw_audio.channel_count;

	if (channels <= 0)
		channels = 1;  // Default to mono, not stereo

	switch (format) {
		case media_raw_audio_format::B_AUDIO_UCHAR:
		{
			size_t samples = size / sizeof(uint8);
			_CalculateLevelsTyped(static_cast<const uint8*>(data),
				samples, channels, (uint8)128, outLeft, outRight);
			break;
		}

		case media_raw_audio_format::B_AUDIO_SHORT:
		{
			size_t samples = size / sizeof(int16);
			uint32 byteOrder = fFormat.u.raw_audio.byte_order;

			// Check if byte swap is needed
			// Haiku x86 is little-endian (B_MEDIA_LITTLE_ENDIAN = 2)
			if (byteOrder == B_MEDIA_BIG_ENDIAN) {
				// Swap bytes for each int16 sample before computing levels
				// Work on a temporary copy to avoid modifying the buffer
				int16* swapped = new int16[samples];
				const uint8* raw = static_cast<const uint8*>(data);
				for (size_t i = 0; i < samples; i++) {
					swapped[i] = (int16)((raw[i * 2 + 1]) | (raw[i * 2] << 8));
				}
				_CalculateLevelsTyped(swapped, samples, channels,
					(int16)32767, outLeft, outRight);
				delete[] swapped;
			} else {
				_CalculateLevelsTyped(static_cast<const int16*>(data),
					samples, channels, (int16)32767, outLeft, outRight);
			}
			break;
		}

		case media_raw_audio_format::B_AUDIO_INT:
		{
			size_t samples = size / sizeof(int32);
			_CalculateLevelsTyped(static_cast<const int32*>(data),
				samples, channels, (int32)2147483647, outLeft, outRight);
			break;
		}

		case media_raw_audio_format::B_AUDIO_FLOAT:
		{
			const float* samples = static_cast<const float*>(data);
			size_t sampleCount = size / sizeof(float);
			size_t frameCount = sampleCount / channels;

			if (frameCount > 0) {
				// Compute DC offset
				double dcLeft = 0.0, dcRight = 0.0;
				for (size_t i = 0; i < frameCount; i++) {
					dcLeft += samples[i * channels];
					if (channels > 1)
						dcRight += samples[i * channels + 1];
				}
				dcLeft /= frameCount;
				dcRight /= frameCount;

				// RMS with DC removal
				double sumLeft = 0.0, sumRight = 0.0;
				for (size_t i = 0; i < frameCount; i++) {
					double left = samples[i * channels] - dcLeft;
					sumLeft += left * left;
					if (channels > 1) {
						double right = samples[i * channels + 1] - dcRight;
						sumRight += right * right;
					}
				}
				*outLeft = (float)sqrt(sumLeft / frameCount);
				*outRight = channels > 1 ?
					(float)sqrt(sumRight / frameCount) : *outLeft;
			}
			break;
		}

		default:
			// Unknown format - return zero
			break;
	}
}


template<typename T>
void
AudioConsumer::_CalculateLevelsTyped(const T* data, size_t samples,
	int32 channels, T maxValue, float* outLeft, float* outRight)
{
	if (samples == 0)
		return;

	size_t frameCount = samples / channels;
	if (frameCount == 0)
		return;

	// First pass: compute DC offset (mean) per channel
	double dcLeft = 0.0, dcRight = 0.0;
	for (size_t i = 0; i < frameCount; i++) {
		dcLeft += (double)data[i * channels];
		if (channels > 1)
			dcRight += (double)data[i * channels + 1];
	}
	dcLeft /= frameCount;
	dcRight /= frameCount;

	// Second pass: compute RMS with DC offset removed
	// This correctly handles constant DC values (like 0x8000) as silence
	double sumLeft = 0.0, sumRight = 0.0;
	for (size_t i = 0; i < frameCount; i++) {
		double left = ((double)data[i * channels] - dcLeft) / maxValue;
		sumLeft += left * left;

		if (channels > 1) {
			double right = ((double)data[i * channels + 1] - dcRight) / maxValue;
			sumRight += right * right;
		}
	}

	*outLeft = (float)sqrt(sumLeft / frameCount);
	*outRight = channels > 1 ? (float)sqrt(sumRight / frameCount) : *outLeft;
}
