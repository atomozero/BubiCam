/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamRoster.h"
#include "WebcamDevice.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "WebcamRoster"
#include "ErrorUtils.h"

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
		LOG_ERROR("BMediaRoster not available");
		return B_ERROR;
	}

	// Allocate array for dormant nodes
	const int32 kMaxNodes = 64;
	dormant_node_info* dormantNodes = new dormant_node_info[kMaxNodes];
	int32 dormantCount = kMaxNodes;

	// Get video producer nodes
	status_t status = roster->GetDormantNodes(dormantNodes, &dormantCount,
		NULL, NULL, NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT, 0);

	LOG_INFO("GetDormantNodes (PHYSICAL_INPUT): status=%d, count=%d",
		(int)status, (int)dormantCount);

	if (status != B_OK || dormantCount == 0) {
		// Retry with just B_BUFFER_PRODUCER
		dormantCount = kMaxNodes;
		status = roster->GetDormantNodes(dormantNodes, &dormantCount,
			NULL, NULL, NULL, B_BUFFER_PRODUCER, 0);
		LOG_INFO("GetDormantNodes (BUFFER_PRODUCER): status=%d, count=%d",
			(int)status, (int)dormantCount);
	}

	if (status != B_OK) {
		delete[] dormantNodes;
		return status;
	}

	LOG_SECTION("Enumerating Media Nodes");

	// Filter for video producers (webcams)
	// IMPORTANT: Do NOT instantiate nodes here! The UVC driver only allows
	// one instance at a time. Just add the dormant node info and let
	// StartCapture() instantiate when needed.
	for (int32 i = 0; i < dormantCount; i++) {
		LOG_DEBUG("Node %d: '%s' (addon=%d, flavor=%d)",
			(int)i, dormantNodes[i].name, (int)dormantNodes[i].addon,
			(int)dormantNodes[i].flavor_id);

		if (_IsVideoProducer(dormantNodes[i])) {
			LOG_INFO("Found video producer: %s", dormantNodes[i].name);

			// Create device WITHOUT instantiating - just store dormant info
			// The node will be instantiated on-demand in StartCapture()
			WebcamDevice* device = new WebcamDevice(dormantNodes[i], B_OK);

			// Gather device info (USB info, driver info, formats, audio)
			// This also calls ParseUSBDescriptors() internally
			device->GatherDeviceInfo();

			fDevices.AddItem(device);

			LOG_INFO("Added device: %s", device->Name());
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
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return false;

	dormant_flavor_info flavorInfo;
	status_t status = roster->GetDormantFlavorInfoFor(info, &flavorInfo);
	if (status != B_OK)
		return false;

	// Check if this node produces raw video
	for (int32 i = 0; i < flavorInfo.out_format_count; i++) {
		if (flavorInfo.out_formats[i].type == B_MEDIA_RAW_VIDEO) {
			// Exclude known non-webcam nodes (overlays, outputs, etc.)
			BString name(info.name);
			name.ToLower();
			if (name.FindFirst("overlay") >= 0 ||
				name.FindFirst("output") >= 0 ||
				name.FindFirst("display") >= 0) {
				return false;
			}
			return true;
		}
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
