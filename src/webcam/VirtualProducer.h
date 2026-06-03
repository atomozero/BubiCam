/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VirtualProducer - Virtual webcam media node producer.
 *
 * Creates a virtual video producer node in the Media Kit that other
 * applications can connect to as if it were a real webcam. BubiCam
 * feeds processed frames into this node, allowing other apps to
 * receive the BubiCam video stream with any active filters applied.
 *
 * Status: Scaffold implementation. Register/unregister with the
 * Media Kit works; frame delivery requires a connected consumer.
 */

#ifndef VIRTUAL_PRODUCER_H
#define VIRTUAL_PRODUCER_H

#include <MediaNode.h>
#include <MediaEventLooper.h>
#include <BufferProducer.h>
#include <BufferGroup.h>
#include <Bitmap.h>
#include <Locker.h>

class VirtualProducer : public BMediaEventLooper, public BBufferProducer {
public:
						VirtualProducer(const char* name = "BubiCam Virtual");
	virtual				~VirtualProducer();

	// Feed a frame into the virtual producer
	status_t			PushFrame(BBitmap* bitmap);

	// Configuration
	void				SetFormat(int32 width, int32 height, float fps);
	bool				IsConnected() const { return fConnected; }

	// BMediaNode interface
	virtual BMediaAddOn*	AddOn(int32* internalId) const;
	virtual void		NodeRegistered();

	// BMediaEventLooper interface
	virtual void		HandleEvent(const media_timed_event* event,
							bigtime_t lateness, bool realTimeEvent);

	// BBufferProducer interface
	virtual status_t	FormatSuggestionRequested(media_type type,
							int32 quality, media_format* format);
	virtual status_t	FormatProposal(const media_source& output,
							media_format* format);
	virtual status_t	FormatChangeRequested(const media_source& source,
							const media_destination& destination,
							media_format* ioFormat, int32* _deprecated_);
	virtual status_t	GetNextOutput(int32* cookie,
							media_output* outOutput);
	virtual status_t	DisposeOutputCookie(int32 cookie);
	virtual status_t	SetBufferGroup(const media_source& forSource,
							BBufferGroup* group);
	virtual status_t	PrepareToConnect(const media_source& what,
							const media_destination& where,
							media_format* format, media_source* outSource,
							char* outName);
	virtual void		Connect(status_t error, const media_source& source,
							const media_destination& destination,
							const media_format& format, char* ioName);
	virtual void		Disconnect(const media_source& what,
							const media_destination& where);
	virtual void		EnableOutput(const media_source& what,
							bool enabled, int32* _deprecated_);
	virtual status_t	GetLatency(bigtime_t* outLatency);
	virtual void		LatencyChanged(const media_source& source,
							const media_destination& destination,
							bigtime_t newLatency, uint32 flags);

private:
	media_output		fOutput;
	bool				fConnected;
	bool				fEnabled;
	BBufferGroup*		fBufferGroup;
	BLocker				fLock;

	int32				fWidth;
	int32				fHeight;
	float				fFPS;
};

#endif // VIRTUAL_PRODUCER_H
