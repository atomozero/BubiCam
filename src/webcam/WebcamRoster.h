/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef WEBCAM_ROSTER_H
#define WEBCAM_ROSTER_H

#include <ObjectList.h>
#include <MediaRoster.h>
#include <MediaNode.h>
#include <Locker.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

class WebcamDevice;

class WebcamRoster {
public:
						WebcamRoster();
						~WebcamRoster();

	status_t			EnumerateDevices();
	int32				CountDevices() const;
	WebcamDevice*		DeviceAt(int32 index) const;
	WebcamDevice*		DeviceByName(const char* name) const;

	void				Clear();

private:
	bool				_IsVideoProducer(const dormant_node_info& info);
	void				_EnumerateDevVideoDevices();

	BObjectList<WebcamDevice>	fDevices;
	mutable BLocker		fLock;
};

#endif // WEBCAM_ROSTER_H
