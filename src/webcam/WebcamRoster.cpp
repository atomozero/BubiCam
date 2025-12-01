/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamRoster.h"
#include "WebcamDevice.h"

#include <Autolock.h>
#include <MediaRoster.h>
#include <MediaAddOn.h>
#include <TimeSource.h>

#include <stdio.h>
#include <string.h>


WebcamRoster::WebcamRoster()
	:
	fLock("webcam roster lock")
{
}


WebcamRoster::~WebcamRoster()
{
	Clear();
}


status_t
WebcamRoster::EnumerateDevices()
{
	BAutolock lock(fLock);

	Clear();

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fprintf(stderr, "WebcamRoster: BMediaRoster not available\n");
		return B_ERROR;
	}

	// Allocate array for dormant nodes
	const int32 kMaxNodes = 64;
	dormant_node_info* dormantNodes = new dormant_node_info[kMaxNodes];
	int32 dormantCount = kMaxNodes;

	// Get video producer nodes
	status_t status = roster->GetDormantNodes(dormantNodes, &dormantCount,
		NULL, NULL, NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT, 0);

	fprintf(stderr, "WebcamRoster: GetDormantNodes (PHYSICAL_INPUT) returned %ld, count=%ld\n",
		status, dormantCount);

	if (status != B_OK || dormantCount == 0) {
		// Retry with just B_BUFFER_PRODUCER
		dormantCount = kMaxNodes;
		status = roster->GetDormantNodes(dormantNodes, &dormantCount,
			NULL, NULL, NULL, B_BUFFER_PRODUCER, 0);
		fprintf(stderr, "WebcamRoster: GetDormantNodes (BUFFER_PRODUCER) returned %ld, count=%ld\n",
			status, dormantCount);
	}

	if (status != B_OK) {
		delete[] dormantNodes;
		return status;
	}

	// Filter for video producers (webcams)
	// IMPORTANT: Do NOT instantiate nodes here! The UVC driver only allows
	// one instance at a time. Just add the dormant node info and let
	// StartCapture() instantiate when needed.
	for (int32 i = 0; i < dormantCount; i++) {
		fprintf(stderr, "WebcamRoster: Node %d: '%s' (addon=%d, flavor=%d)\n",
			(int)i, dormantNodes[i].name, (int)dormantNodes[i].addon,
			(int)dormantNodes[i].flavor_id);

		if (_IsVideoProducer(dormantNodes[i])) {
			fprintf(stderr, "WebcamRoster: -> Detected as video producer, adding to list\n");

			// Create device WITHOUT instantiating - just store dormant info
			// The node will be instantiated on-demand in StartCapture()
			WebcamDevice* device = new WebcamDevice(dormantNodes[i], B_OK);

			// Parse USB descriptors to get device capabilities
			// This talks to USB directly, doesn't need media node instantiated
			device->ParseUSBDescriptors();

			fDevices.AddItem(device);

			fprintf(stderr, "WebcamRoster: -> Added device '%s'\n", device->Name());
		}
	}

	delete[] dormantNodes;

	// Also look for video input devices in /dev/video
	// This catches devices that may not be registered with Media Kit yet
	_EnumerateDevVideoDevices();

	return B_OK;
}


void
WebcamRoster::_EnumerateDevVideoDevices()
{
	// Look for video devices in /dev/video
	BDirectory devVideo("/dev/video");
	if (devVideo.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (devVideo.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);

		// Check if we already have this device
		bool found = false;
		for (int32 i = 0; i < fDevices.CountItems(); i++) {
			WebcamDevice* device = fDevices.ItemAt(i);
			if (device != NULL &&
				strstr(device->DevicePath(), path.Leaf()) != NULL) {
				found = true;
				break;
			}
		}

		if (!found) {
			// Could add device from /dev/video here
			// For now, we rely on Media Kit enumeration
		}
	}
}


bool
WebcamRoster::_IsVideoProducer(const dormant_node_info& info)
{
	// Check if this looks like a video input device
	BString name(info.name);
	name.ToLower();

	// Keywords that suggest video input
	if (name.FindFirst("video") >= 0 ||
		name.FindFirst("camera") >= 0 ||
		name.FindFirst("webcam") >= 0 ||
		name.FindFirst("capture") >= 0 ||
		name.FindFirst("uvc") >= 0 ||
		name.FindFirst("usb") >= 0) {

		// Exclude known non-webcam video nodes
		if (name.FindFirst("overlay") >= 0 ||
			name.FindFirst("output") >= 0 ||
			name.FindFirst("display") >= 0) {
			return false;
		}

		return true;
	}

	return false;
}


int32
WebcamRoster::CountDevices() const
{
	BAutolock lock(fLock);
	return fDevices.CountItems();
}


WebcamDevice*
WebcamRoster::DeviceAt(int32 index) const
{
	BAutolock lock(fLock);
	return fDevices.ItemAt(index);
}


WebcamDevice*
WebcamRoster::DeviceByName(const char* name) const
{
	BAutolock lock(fLock);

	for (int32 i = 0; i < fDevices.CountItems(); i++) {
		WebcamDevice* device = fDevices.ItemAt(i);
		if (device != NULL && strcmp(device->Name(), name) == 0)
			return device;
	}

	return NULL;
}


void
WebcamRoster::Clear()
{
	BAutolock lock(fLock);

	for (int32 i = 0; i < fDevices.CountItems(); i++)
		delete fDevices.ItemAt(i);

	fDevices.MakeEmpty();
}
