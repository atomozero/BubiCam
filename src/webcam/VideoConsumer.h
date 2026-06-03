/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VideoConsumer - Based on CodyCam's VideoConsumer implementation
 * Key difference from original BubiCam: implements BBufferGroup for producer
 */

#ifndef VIDEO_CONSUMER_H
#define VIDEO_CONSUMER_H

#include <BufferConsumer.h>
#include <MediaEventLooper.h>
#include <BufferGroup.h>
#include <Bitmap.h>
#include <Locker.h>
#include <Looper.h>
#include <TimedEventQueue.h>

#include <stdio.h>
#include <jpeglib.h>

// Number of buffers in the shared buffer group for video frames.
// 3 is the standard for webcam applications (CodyCam pattern):
// - One buffer being filled by producer
// - One buffer being processed by consumer
// - One buffer available for next frame
// Using fewer causes dropped frames; more wastes memory without benefit.
#define NUM_BUFFERS 3

class VideoConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
						VideoConsumer(const char* name, BLooper* target,
							uint32 frameMessage);
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

	// Frame access (for MCP server)
	BBitmap*			GetCurrentFrame() const { return fDisplayBitmap; }

	// Raw frame capture for debug/export
	status_t			CaptureRawFrame(void** outData, size_t* outSize,
							color_space* outFormat, int32* outWidth,
							int32* outHeight);

	// Target management (thread-safe)
	void				SetTarget(BLooper* target);

	// Buffer management (CodyCam-style)
	status_t			CreateBuffers(const media_format& format);
	void				DeleteBuffers();

private:
	void				_HandleBuffer(BBuffer* buffer);
	void				_ConvertBuffer(BBuffer* buffer, BBitmap* destBitmap);
	void				_ConvertUYVYToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_ConvertYUV422ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_ConvertYUV420ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_ConvertNV12ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	void				_ConvertNV21ToBGRA(const uint8* src, uint8* dst,
							int32 width, int32 height);
	bool				_DecompressMJPEG(const uint8* src, size_t srcSize,
							BBitmap* destBitmap);
	bool				_IsMJPEGData(const uint8* data, size_t size) const;
	void				_SendFrameToTarget(BBitmap* bitmap);

	BLooper*			fTarget;
	mutable BLocker		fTargetLock;
	uint32				fFrameMessage;

	media_input			fInput;
	media_destination	fDestination;
	bool				fConnected;
	media_format		fFormat;

	// CodyCam-style buffer group with bitmaps
	// This is the CRITICAL piece that was missing!
	BBufferGroup*		fBuffers;
	BBitmap*			fBitmap[NUM_BUFFERS];
	BBuffer*			fBufferMap[NUM_BUFFERS];

	// Display bitmap (for format conversion if needed)
	BBitmap*			fDisplayBitmap;
	int32				fBitmapWidth;
	int32				fBitmapHeight;
	color_space			fBitmapColorSpace;

	// Last raw buffer copy (for raw frame export)
	uint8*				fLastRawData;
	size_t				fLastRawSize;
	BLocker				fRawLock;

	// Statistics
	uint32				fFramesReceived;
	uint32				fFramesDropped;
	float				fCurrentFPS;
	bigtime_t			fLastFrameTime;

	// Latency
	bigtime_t			fInternalLatency;

	// Producer info
	media_source		fProducerSource;
};

#endif // VIDEO_CONSUMER_H
