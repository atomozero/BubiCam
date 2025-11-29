/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamDevice.h"
#include "MainWindow.h"

#include <MediaRoster.h>
#include <MediaFormats.h>
#include <TimeSource.h>
#include <Buffer.h>
#include <BufferGroup.h>
#include <ParameterWeb.h>
#include <Path.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <USBKit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


WebcamDevice::WebcamDevice(const media_node& node, const dormant_node_info& info)
	:
	fVendorID(0),
	fProductID(0),
	fDeviceClass(0),
	fDeviceSubclass(0),
	fDeviceProtocol(0),
	fSupportsVideo(true),
	fSupportsAudio(false),
	fAudioSampleRate(0),
	fAudioChannels(0),
	fAudioBitsPerSample(0),
	fMediaNode(node),
	fMediaNodeID(node.node),
	fDormantInfo(info),
	fIsCapturing(false),
	fCaptureThread(-1),
	fTarget(NULL),
	fFramesCaptured(0),
	fFramesDropped(0),
	fCurrentFPS(0.0f),
	fLastFrameTime(0)
{
	fName = info.name;
	fSupportedFormats.MakeEmpty();
}


WebcamDevice::~WebcamDevice()
{
	StopCapture();

	// Clean up formats
	for (int32 i = 0; i < fSupportedFormats.CountItems(); i++)
		delete fSupportedFormats.ItemAt(i);
	fSupportedFormats.MakeEmpty();
}


status_t
WebcamDevice::GatherDeviceInfo()
{
	_GatherUSBInfo();
	_GatherDriverInfo();
	_GatherVideoFormats();
	_GatherAudioInfo();

	return B_OK;
}


void
WebcamDevice::_GatherUSBInfo()
{
	// Try to get USB information by examining the USB device roster
	BUSBRoster roster;

	// The USB information may be encoded in the node name or
	// we need to correlate with USB devices
	// For now, we'll try to parse common naming conventions

	// Try to extract VID/PID from node name if present
	// Format might be: "USB Video (VID:PID)" or similar
	BString name(fName);

	// Look for USB devices that match video class
	class USBVisitor : public BUSBRoster::Visitor {
	public:
		USBVisitor(WebcamDevice* device) : fDevice(device), fFound(false) {}

		virtual status_t DeviceAdded(BUSBDevice* device)
		{
			// Check if this is a video class device
			if (device->Class() == 0x0E ||  // Video class
				device->Class() == 0xEF) {  // Misc class (often webcams)

				fDevice->fVendorID = device->VendorID();
				fDevice->fProductID = device->ProductID();
				fDevice->fVendorName = device->ManufacturerString();
				fDevice->fProductName = device->ProductString();
				fDevice->fSerialNumber = device->SerialNumberString();
				fDevice->fDeviceClass = device->Class();
				fDevice->fDeviceSubclass = device->Subclass();
				fDevice->fDeviceProtocol = device->Protocol();

				// USB version
				uint16 version = device->USBVersion();
				fDevice->fUSBVersion.SetToFormat("%d.%d",
					(version >> 8) & 0xFF, version & 0xFF);

				// Device path
				fDevice->fDevicePath = device->Location();

				fFound = true;
			}
			return B_OK;
		}

		virtual void DeviceRemoved(BUSBDevice* device) {}

		bool Found() const { return fFound; }

	private:
		WebcamDevice* fDevice;
		bool fFound;
	};

	USBVisitor visitor(this);
	roster.Start();
	roster.Stop();

	// If USB info not found, set defaults
	if (fVendorName.Length() == 0)
		fVendorName = "Unknown";
	if (fProductName.Length() == 0)
		fProductName = fName;
}


void
WebcamDevice::_GatherDriverInfo()
{
	// Try to determine driver information
	// In Haiku, webcam drivers are typically in /dev/video/usb/

	BDirectory devDir("/dev/video");
	if (devDir.InitCheck() == B_OK) {
		BEntry entry;
		while (devDir.GetNextEntry(&entry) == B_OK) {
			BPath path;
			entry.GetPath(&path);
			// Check if this matches our device
			if (strstr(fName.String(), path.Leaf()) != NULL) {
				fDriverPath = path.Path();
				break;
			}
		}
	}

	// Driver name from dormant info
	fDriverName = fDormantInfo.addon_id > 0 ?
		"Media Add-on" : "Built-in";

	// Try to get driver version
	BPath addonsPath;
	if (find_directory(B_SYSTEM_ADDONS_DIRECTORY, &addonsPath) == B_OK) {
		addonsPath.Append("media");
		fDriverVersion = "Unknown";
	}

	// Look for UVC driver
	if (fName.IFindFirst("uvc") >= 0 ||
		fName.IFindFirst("usb video") >= 0) {
		fDriverName = "usb_webcam (UVC)";
	}
}


void
WebcamDevice::_GatherVideoFormats()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Get output formats from the video producer
	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_VIDEO;

	int32 formatCount = 20;
	media_format formats[20];

	status_t status = roster->GetFormatFor(fMediaNode, &format,
		B_MEDIA_OUTPUT);
	if (status != B_OK)
		return;

	// Add discovered format
	if (format.type == B_MEDIA_RAW_VIDEO) {
		VideoFormat* vf = new VideoFormat();
		vf->width = format.u.raw_video.display.line_width;
		vf->height = format.u.raw_video.display.line_count;
		vf->frameRate = format.u.raw_video.field_rate;

		// Color space name
		switch (format.u.raw_video.display.format) {
			case B_RGB32:
				strcpy(vf->colorSpace, "RGB32");
				break;
			case B_RGB24:
				strcpy(vf->colorSpace, "RGB24");
				break;
			case B_RGB16:
				strcpy(vf->colorSpace, "RGB16");
				break;
			case B_YCbCr422:
				strcpy(vf->colorSpace, "YCbCr422");
				break;
			case B_YCbCr420:
				strcpy(vf->colorSpace, "YCbCr420");
				break;
			default:
				strcpy(vf->colorSpace, "Unknown");
		}

		fSupportedFormats.AddItem(vf);
		fCurrentFormat = *vf;
	}

	// Common webcam resolutions to test
	struct Resolution {
		int32 width;
		int32 height;
	} commonRes[] = {
		{640, 480},
		{320, 240},
		{800, 600},
		{1280, 720},
		{1920, 1080}
	};

	// Try to enumerate more formats using ParameterWeb
	BParameterWeb* web = NULL;
	if (roster->GetParameterWebFor(fMediaNode, &web) == B_OK && web != NULL) {
		// Look for resolution/format parameters
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* param = web->ParameterAt(i);
			if (param != NULL) {
				// Log parameter info for debugging
				// This helps us understand what the driver exposes
			}
		}
		delete web;
	}
}


void
WebcamDevice::_GatherAudioInfo()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Check if node has audio output
	int32 inputCount, outputCount;
	if (roster->GetFreeInputsFor(fMediaNode, NULL, 0, &inputCount,
			B_MEDIA_RAW_AUDIO) == B_OK && inputCount > 0) {
		fSupportsAudio = true;
	}

	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_AUDIO;

	status_t status = roster->GetFormatFor(fMediaNode, &format,
		B_MEDIA_OUTPUT);

	if (status == B_OK && format.type == B_MEDIA_RAW_AUDIO) {
		fSupportsAudio = true;
		fAudioSampleRate = format.u.raw_audio.frame_rate;
		fAudioChannels = format.u.raw_audio.channel_count;

		switch (format.u.raw_audio.format) {
			case media_raw_audio_format::B_AUDIO_UCHAR:
				fAudioBitsPerSample = 8;
				break;
			case media_raw_audio_format::B_AUDIO_SHORT:
				fAudioBitsPerSample = 16;
				break;
			case media_raw_audio_format::B_AUDIO_INT:
				fAudioBitsPerSample = 32;
				break;
			case media_raw_audio_format::B_AUDIO_FLOAT:
				fAudioBitsPerSample = 32;
				break;
			default:
				fAudioBitsPerSample = 16;
		}
	}
}


status_t
WebcamDevice::StartCapture(BLooper* target)
{
	if (fIsCapturing)
		return B_BUSY;

	fTarget = target;
	fFramesCaptured = 0;
	fFramesDropped = 0;
	fCurrentFPS = 0.0f;
	fLastFrameTime = system_time();
	fIsCapturing = true;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	// Start the media node
	status_t status = roster->StartNode(fMediaNode, 0);
	if (status != B_OK) {
		fIsCapturing = false;
		return status;
	}

	// Start capture thread
	fCaptureThread = spawn_thread(_CaptureThread, "webcam_capture",
		B_NORMAL_PRIORITY, this);

	if (fCaptureThread < 0) {
		roster->StopNode(fMediaNode, 0, true);
		fIsCapturing = false;
		return fCaptureThread;
	}

	resume_thread(fCaptureThread);
	return B_OK;
}


void
WebcamDevice::StopCapture()
{
	if (!fIsCapturing)
		return;

	fIsCapturing = false;

	if (fCaptureThread >= 0) {
		status_t result;
		wait_for_thread(fCaptureThread, &result);
		fCaptureThread = -1;
	}

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster != NULL) {
		roster->StopNode(fMediaNode, 0, true);
	}

	fTarget = NULL;
}


int32
WebcamDevice::_CaptureThread(void* data)
{
	WebcamDevice* device = static_cast<WebcamDevice*>(data);
	device->_CaptureLoop();
	return 0;
}


void
WebcamDevice::_CaptureLoop()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Create a buffer for receiving video frames
	// This is a simplified capture loop - in a real implementation
	// you would use BBufferConsumer or similar

	int32 width = fCurrentFormat.width > 0 ? fCurrentFormat.width : 640;
	int32 height = fCurrentFormat.height > 0 ? fCurrentFormat.height : 480;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
		B_RGB32);

	bigtime_t frameInterval = 33333;  // ~30 fps
	if (fCurrentFormat.frameRate > 0)
		frameInterval = (bigtime_t)(1000000.0f / fCurrentFormat.frameRate);

	while (fIsCapturing) {
		bigtime_t now = system_time();

		// In a real implementation, this would receive actual video data
		// from the media node. For now, we generate a test pattern.

		// Generate test pattern (checkerboard with timestamp)
		uint32* bits = (uint32*)bitmap->Bits();
		int32 bpr = bitmap->BytesPerRow() / 4;

		for (int32 y = 0; y < height; y++) {
			for (int32 x = 0; x < width; x++) {
				int32 checkX = x / 32;
				int32 checkY = y / 32;
				bool isWhite = ((checkX + checkY) % 2) == 0;

				// Add some animation
				int32 offset = (now / 100000) % 64;
				if (((x + offset) / 32 + (y + offset) / 32) % 2 == 0)
					isWhite = !isWhite;

				bits[y * bpr + x] = isWhite ? 0xFFCCCCCC : 0xFF333333;
			}
		}

		// Add center circle to show it's working
		int32 cx = width / 2;
		int32 cy = height / 2;
		int32 radius = 50 + (int32)(20 * sin((double)now / 200000.0));

		for (int32 y = cy - radius; y <= cy + radius; y++) {
			for (int32 x = cx - radius; x <= cx + radius; x++) {
				if (x >= 0 && x < width && y >= 0 && y < height) {
					int32 dx = x - cx;
					int32 dy = y - cy;
					if (dx * dx + dy * dy <= radius * radius) {
						bits[y * bpr + x] = 0xFF00AA00;  // Green circle
					}
				}
			}
		}

		// Calculate FPS
		bigtime_t elapsed = now - fLastFrameTime;
		if (elapsed > 0) {
			fCurrentFPS = fCurrentFPS * 0.9f + (1000000.0f / elapsed) * 0.1f;
		}
		fLastFrameTime = now;
		fFramesCaptured++;

		// Send frame to target
		if (fTarget != NULL) {
			BMessage msg(MSG_FRAME_RECEIVED);
			msg.AddPointer("bitmap", bitmap);
			fTarget->PostMessage(&msg);

			// Also send simulated audio level
			float audioLevel = 0.3f + 0.3f * sin((double)now / 500000.0);
			BMessage audioMsg(MSG_AUDIO_LEVEL);
			audioMsg.AddFloat("left", audioLevel);
			audioMsg.AddFloat("right", audioLevel * 0.9f);
			fTarget->PostMessage(&audioMsg);
		}

		// Wait for next frame
		snooze(frameInterval);
	}

	delete bitmap;
}
