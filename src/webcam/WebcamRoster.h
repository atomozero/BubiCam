/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef WEBCAM_ROSTER_H
#define WEBCAM_ROSTER_H

#include <Handler.h>
#include <ObjectList.h>
#include <MediaRoster.h>
#include <MediaNode.h>
#include <Locker.h>
#include <Looper.h>
#include <MessageRunner.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

class WebcamDevice;

// Message sent to the target looper when devices change
enum {
	MSG_DEVICES_CHANGED		= 'dvch'
};


class WebcamRoster : public BHandler {
public:
						WebcamRoster();
	virtual				~WebcamRoster();

	// BHandler interface
	virtual void		MessageReceived(BMessage* message);

	// Node watching
	status_t			StartWatching();
	void				StopWatching();
	bool				IsWatching() const { return fWatching; }

	status_t			EnumerateDevices();
	int32				CountDevices() const;
	WebcamDevice*		DeviceAt(int32 index) const;
	WebcamDevice*		DeviceByName(const char* name) const;

	void				Clear();

private:
	bool				_IsVideoProducer(const dormant_node_info& info);
	void				_EnumerateDevVideoDevices();
	void				_NotifyDevicesChanged();

	BObjectList<WebcamDevice>	fDevices;
	mutable BLocker		fLock;
	bool				fWatching;
	BMessageRunner*		fCoalesceRunner;
};

#endif // WEBCAM_ROSTER_H
