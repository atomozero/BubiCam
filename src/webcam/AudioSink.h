/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * AudioSink - Abstract destination for captured audio.
 *
 * AudioConsumer pushes every captured audio buffer to an AudioSink directly
 * from the real-time audio thread (it bypasses the message loop to avoid
 * buffer loss during video processing). Implement this interface to route
 * audio anywhere - an AVI recorder, an encoder, a network stream - without
 * the capture library having to know about the destination.
 *
 * Contract:
 *  - WriteAudio() may be called from a real-time media thread: be fast and
 *    do your own locking; never block.
 *  - 'data' is valid only for the duration of the call. Copy it to keep it.
 *  - 'data' is raw PCM described by 'format' (sample format, channel count,
 *    byte order, frame rate). The sink is responsible for any conversion it
 *    needs (e.g. float -> int16).
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <MediaDefs.h>
#include <SupportDefs.h>

class AudioSink {
public:
	virtual				~AudioSink() {}

	virtual void		WriteAudio(const void* data, size_t size,
							const media_raw_audio_format& format) = 0;
};

#endif // AUDIO_SINK_H
