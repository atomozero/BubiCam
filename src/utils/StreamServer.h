/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * StreamServer - MJPEG over HTTP streaming server
 * Streams live video to any browser via multipart/x-mixed-replace
 */

#ifndef STREAM_SERVER_H
#define STREAM_SERVER_H

#include <Bitmap.h>
#include <Locker.h>
#include <Messenger.h>
#include <String.h>
#include <ObjectList.h>
#include <atomic>

// Message constants for StreamServer
enum {
	MSG_STREAM_STARTED		= 'strs',
	MSG_STREAM_STOPPED		= 'strt',
	MSG_STREAM_CLIENT		= 'strc',
	MSG_STREAM_ERROR		= 'stre'
};


class StreamServer {
public:
						StreamServer(BMessenger target);
						~StreamServer();

	// Server control
	status_t			Start(uint16 port = 8080);
	void				Stop();
	bool				IsRunning() const { return fRunning; }
	uint16				Port() const { return fPort; }

	// Feed a new frame (called from MainWindow on each MSG_FRAME_RECEIVED)
	void				FeedFrame(BBitmap* bitmap);

	// Configuration
	void				SetJPEGQuality(int quality);
	void				SetMaxFPS(float fps);
	void				SetMaxClients(int32 max);

	// Statistics
	int32				ClientCount() const { return fClientCount; }
	uint32				FramesServed() const { return fFramesServed; }

private:
	struct ClientInfo {
		int				socket;
		thread_id		thread;
		volatile bool	active;

		ClientInfo() : socket(-1), thread(-1), active(false) {}
	};

	static int32		_ListenerThread(void* data);
	static int32		_ClientThread(void* data);

	status_t			_CompressJPEG(BBitmap* bitmap, uint8** outData,
							unsigned long* outSize);
	void				_RemoveDeadClients();

	BMessenger			fTarget;
	std::atomic<bool>	fRunning;
	uint16				fPort;
	int					fServerSocket;
	thread_id			fListenerThread;

	// Current JPEG frame (shared with all clients)
	uint8*				fCurrentJPEG;
	unsigned long		fCurrentJPEGSize;
	uint32				fFrameSequence;
	BLocker				fFrameLock;

	// Frame rate control
	bigtime_t			fLastFeedTime;
	bigtime_t			fMinFrameInterval;  // microseconds

	// Client management
	BObjectList<ClientInfo>	fClients;
	BLocker				fClientLock;
	std::atomic<int32>	fClientCount;
	int32				fMaxClients;

	// Configuration
	int					fJPEGQuality;
	std::atomic<uint32>	fFramesServed;
};

#endif // STREAM_SERVER_H
