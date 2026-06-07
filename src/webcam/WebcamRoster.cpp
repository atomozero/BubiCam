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

#include <new>
#include <stdio.h>
#include <string.h>


// Internal message for coalescing rapid node change events
static const uint32 kMsgCoalesceRefresh = '_crr';


WebcamRoster::WebcamRoster()
	:
	BHandler("WebcamRoster"),
	fLock("webcam roster lock"),
	fWatching(false),
	fCoalesceRunner(NULL)
{
}


WebcamRoster::~WebcamRoster()
{
	StopWatching();
	delete fCoalesceRunner;
	Clear();
}


void
WebcamRoster::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_MEDIA_NODE_CREATED:
		case B_MEDIA_NODE_DELETED:
		{
			// Coalesce rapid-fire events: when a USB device is plugged in,
			// multiple nodes may be created in quick succession. We delay
			// the refresh by 500ms and reset the timer on each new event.
			delete fCoalesceRunner;
			BMessage refreshMsg(kMsgCoalesceRefresh);
			fCoalesceRunner = new BMessageRunner(BMessenger(this),
				&refreshMsg, 500000, 1);  // 500ms, once
			break;
		}

		case kMsgCoalesceRefresh:
		{
			delete fCoalesceRunner;
			fCoalesceRunner = NULL;

			LOG_INFO("Media node change detected, refreshing device list");
			_NotifyDevicesChanged();
			break;
		}

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


status_t
WebcamRoster::StartWatching()
{
	if (fWatching)
		return B_OK;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	// Watch for node creation and deletion
	status_t status = roster->StartWatching(BMessenger(this),
		B_MEDIA_NODE_CREATED | B_MEDIA_NODE_DELETED);

	if (status == B_OK) {
		fWatching = true;
		LOG_INFO("Node watching started");
	} else {
		LOG_ERROR("Failed to start node watching: %s", strerror(status));
	}

	return status;
}


void
WebcamRoster::StopWatching()
{
	if (!fWatching)
		return;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster != NULL) {
		roster->StopWatching(BMessenger(this),
			B_MEDIA_NODE_CREATED | B_MEDIA_NODE_DELETED);
	}

	fWatching = false;
	LOG_INFO("Node watching stopped");
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

	// Allocate array for dormant nodes and zero-initialize to prevent
	// reading garbage data from partially-filled entries
	const int32 kMaxNodes = 64;
	dormant_node_info* dormantNodes = new(std::nothrow) dormant_node_info[kMaxNodes];
	if (dormantNodes == NULL) {
		LOG_ERROR("Failed to allocate dormant node array");
		return B_NO_MEMORY;
	}
	memset(dormantNodes, 0, sizeof(dormant_node_info) * kMaxNodes);
	int32 dormantCount = kMaxNodes;

	// Get video producer nodes
	status_t status = roster->GetDormantNodes(dormantNodes, &dormantCount,
		NULL, NULL, NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT, 0);

	LOG_DEBUG("GetDormantNodes (physical): found %d nodes (status=%s)",
		(int)dormantCount, strerror(status));

	if (status != B_OK || dormantCount == 0) {
		// Retry with just B_BUFFER_PRODUCER
		memset(dormantNodes, 0, sizeof(dormant_node_info) * kMaxNodes);
		dormantCount = kMaxNodes;
		status = roster->GetDormantNodes(dormantNodes, &dormantCount,
			NULL, NULL, NULL, B_BUFFER_PRODUCER, 0);
		LOG_DEBUG("GetDormantNodes (all producers): found %d nodes (status=%s)",
			(int)dormantCount, strerror(status));
	}

	// Clamp count to valid range
	if (dormantCount < 0)
		dormantCount = 0;
	if (dormantCount > kMaxNodes)
		dormantCount = kMaxNodes;

	if (status != B_OK) {
		delete[] dormantNodes;
		return status;
	}

	// Filter for video producers (webcams)
	// IMPORTANT: Do NOT instantiate nodes here! The UVC driver only allows
	// one instance at a time. Just add the dormant node info and let
	// StartCapture() instantiate when needed.
	for (int32 i = 0; i < dormantCount; i++) {
		LOG_TRACE("Node %d: '%s' (addon=%d)",
			(int)i, dormantNodes[i].name, (int)dormantNodes[i].addon);

		if (_IsVideoProducer(dormantNodes[i])) {
			// Create device WITHOUT instantiating - just store dormant info
			WebcamDevice* device = new WebcamDevice(dormantNodes[i], B_OK);

			// Gather device info (USB info, driver info, formats, audio)
			device->GatherDeviceInfo();

			fDevices.AddItem(device);
			LOG_INFO("Found webcam: %s", device->Name());
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
	// Validate the dormant_node_info before passing to Media Kit.
	// Invalid addon IDs can cause segfaults in GetDormantFlavorInfoFor.
	if (info.addon <= 0 || info.flavor_id < 0) {
		LOG_TRACE("Skipping invalid dormant node: addon=%d flavor=%d",
			(int)info.addon, (int)info.flavor_id);
		return false;
	}

	// Sanity check the name field - should be a valid C string
	bool hasValidName = false;
	for (int i = 0; i < (int)sizeof(info.name); i++) {
		if (info.name[i] == '\0') {
			hasValidName = (i > 0);  // non-empty name
			break;
		}
	}
	if (!hasValidName)
		return false;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return false;

	dormant_flavor_info flavorInfo;
	status_t status = roster->GetDormantFlavorInfoFor(info, &flavorInfo);
	if (status != B_OK) {
		LOG_TRACE("GetDormantFlavorInfoFor '%s' failed: %s",
			info.name, strerror(status));
		return false;
	}

	// Check if this node produces raw video
	if (flavorInfo.out_formats == NULL || flavorInfo.out_format_count <= 0)
		return false;

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
WebcamRoster::_NotifyDevicesChanged()
{
	// Send MSG_DEVICES_CHANGED to our looper (MainWindow)
	BLooper* looper = Looper();
	if (looper != NULL)
		looper->PostMessage(MSG_DEVICES_CHANGED);
}


void
WebcamRoster::Clear()
{
	BAutolock lock(fLock);

	for (int32 i = 0; i < fDevices.CountItems(); i++)
		delete fDevices.ItemAt(i);

	fDevices.MakeEmpty();
}
