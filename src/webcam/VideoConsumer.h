/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef VIDEO_CONSUMER_H
#define VIDEO_CONSUMER_H

#include <BufferConsumer.h>
#include <MediaEventLooper.h>
#include <Bitmap.h>
#include <Looper.h>
#include <TimedEventQueue.h>

class VideoConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
						VideoConsumer(const char* name, BLooper* target,
							uint32 frameMessage, uint32



							);
	virtual				~VideoConsumer();

	// BMediaNode interface
	virtual BMediaAddOn* AddOn(int32* internalId) const;
	virtual	void		NodeRegistered();
	virtual status_t	RequestCompleted(const media_request_info& info);
	virtual void		SetTimeSource(BTimeSource* timeSource);

	// BMediaEventLooper interface
	virtual void		HandleEvent(const media_timed_event* event,
							bigtime_t lateness, bool realTimeEvent = false);

	// BBufferConsumer interface
	virtual status_t	AcceptFormat(const media_destination& dest,
							media_format* format);
	virtual status_t	GetNextInput(int32* cookie,
							media_input* outInput);
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

	// Seek functionality
	virtual status_t	SeekTagRequested(
							const media_destination& destination,
							bigtime_t inTargetTime, uint32 inFlags,
							media_seek_tag* outSeekTag,
							bigtime_t* outTaggedTime, uint32* outFlags);

	// Connection management
	media_input			Input() const { return fInput; }
	bool				IsConnected() const { return fConnected; }

	// Statistics
	uint32				FramesReceived() const { return fFramesReceived; }
	uint32				FramesDropped() const { return fFramesDropped; }
	float				CurrentFPS() const { return fCurrentFPS; }

private:
	void				_HandleBuffer(BBuffer* buffer);
	status_t			_CreateBufferBitmap(const media_format& format);
	void				_ConvertBuffer(BBuffer* buffer);
	void				_ConvertYUV422ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_ConvertYUV420ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_SendFrameToTarget();

	BLooper*			fTarget;
	uint32				fFrameMessage;
	uint32				fAudioMessage;

	media_input			fInput;
	bool				fConnected;
	media_format		fFormat;

	BBitmap*			fBitmap;
	int32				fBitmapWidth;
	int32				fBitmapHeight;
	color_space			fBitmapColorSpace;

	// Statistics
	uint32				fFramesReceived;
	uint32				fFramesDropped;
	float				fCurrentFPS;
	bigtime_t			fLastFrameTime;

	// Latency
	bigtime_t			fInternalLatency;
};

#endif // VIDEO_CONSUMER_H
