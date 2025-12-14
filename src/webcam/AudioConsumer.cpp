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
	Quit();
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

	bigtime_t now = system_time();

	// Throttle level updates
	if (now - fLastLevelTime < kLevelUpdateInterval)
		return;

	fLastLevelTime = now;

	// Calculate audio levels
	float left = 0.0f, right = 0.0f;
	_CalculateLevels(buffer->Data(), buffer->SizeUsed(), &left, &right);

	// Send levels to target (thread-safe with lock)
	BAutolock lock(fTargetLock);
	if (fTarget != NULL) {
		BMessage msg(fLevelMessage);
		msg.AddFloat("left", left);
		msg.AddFloat("right", right);
		fTarget->PostMessage(&msg);
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
		channels = 2;

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
			_CalculateLevelsTyped(static_cast<const int16*>(data),
				samples, channels, (int16)32767, outLeft, outRight);
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

			float sumLeft = 0.0f, sumRight = 0.0f;

			for (size_t i = 0; i < frameCount; i++) {
				float left = fabs(samples[i * channels]);
				sumLeft += left * left;

				if (channels > 1) {
					float right = fabs(samples[i * channels + 1]);
					sumRight += right * right;
				}
			}

			if (frameCount > 0) {
				*outLeft = sqrt(sumLeft / frameCount);
				*outRight = channels > 1 ?
					sqrt(sumRight / frameCount) : *outLeft;
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
	float sumLeft = 0.0f, sumRight = 0.0f;

	for (size_t i = 0; i < frameCount; i++) {
		float left = fabs((float)data[i * channels]) / maxValue;
		sumLeft += left * left;

		if (channels > 1) {
			float right = fabs((float)data[i * channels + 1]) / maxValue;
			sumRight += right * right;
		}
	}

	*outLeft = sqrt(sumLeft / frameCount);
	*outRight = channels > 1 ? sqrt(sumRight / frameCount) : *outLeft;
}
