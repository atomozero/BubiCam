/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef AUDIO_CONSUMER_H
#define AUDIO_CONSUMER_H

#include <BufferConsumer.h>
#include <Locker.h>
#include <Looper.h>
#include <MediaEventLooper.h>

class AudioConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
						AudioConsumer(const char* name, BLooper* target,
							uint32 levelMessage);
	virtual				~AudioConsumer();

	// BMediaNode interface
	virtual BMediaAddOn* AddOn(int32* internalId) const;
	virtual void		NodeRegistered();
	virtual status_t	RequestCompleted(const media_request_info& info);

	// BMediaEventLooper interface
	virtual void		HandleEvent(const media_timed_event* event,
							bigtime_t lateness, bool realTimeEvent = false);

	// BBufferConsumer interface
	virtual status_t	AcceptFormat(const media_destination& dest,
							media_format* format);
	virtual status_t	GetNextInput(int32* cookie, media_input* outInput);
	virtual void		DisposeInputCookie(int32 cookie);
	virtual void		BufferReceived(BBuffer* buffer);
	virtual void		ProducerDataStatus(
							const media_destination& forWhom,
							int32 status, bigtime_t atPerformanceTime);
	virtual status_t	GetLatencyFor(const media_destination& forWhom,
							bigtime_t* outLatency,
							media_node_id* outTimesource);
	virtual status_t	Connected(const media_source& producer,
							const media_destination& where,
							const media_format& withFormat,
							media_input* outInput);
	virtual void		Disconnected(const media_source& producer,
							const media_destination& where);
	virtual status_t	FormatChanged(const media_source& producer,
							const media_destination& consumer,
							int32 changeTag, const media_format& format);

	// Connection info
	media_input			Input() const { return fInput; }
	bool				IsConnected() const { return fConnected; }

	// Target management (thread-safe)
	void				SetTarget(BLooper* target);

	// Direct recorder access (bypasses message loop for audio data)
	void				SetRecorder(class VideoRecorder* recorder);
	void				ClearRecorder();

private:
	void				_HandleBuffer(BBuffer* buffer);
	void				_CalculateLevels(const void* data, size_t size,
							float* outLeft, float* outRight);

	template<typename T>
	void				_CalculateLevelsTyped(const T* data, size_t samples,
							int32 channels, T maxValue,
							float* outLeft, float* outRight);

	BLooper*			fTarget;
	mutable BLocker		fTargetLock;
	uint32				fLevelMessage;

	media_input			fInput;
	bool				fConnected;
	media_format		fFormat;

	bigtime_t			fInternalLatency;
	bigtime_t			fLastLevelTime;

	// Smoothing state (per-instance, not static)
	float				fSmoothedLeft;
	float				fSmoothedRight;
	int32				fBufferCount;
	int32				fLevelLogCount;

	// Direct recording path (bypasses message loop)
	class VideoRecorder*	fRecorder;
	mutable BLocker		fRecorderLock;
};

#endif // AUDIO_CONSUMER_H
