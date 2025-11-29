/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamDevice.h"
#include "VideoConsumer.h"
#include "AudioConsumer.h"
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
	fNodeInstantiated(false),
	fVideoConsumer(NULL),
	fAudioConsumer(NULL),
	fVideoConnected(false),
	fAudioConnected(false),
	fIsCapturing(false),
	fTarget(NULL)
{
	fName = info.name;
	fSupportedFormats.MakeEmpty();

	memset(&fVideoOutput, 0, sizeof(fVideoOutput));
	memset(&fVideoInput, 0, sizeof(fVideoInput));
	memset(&fAudioOutput, 0, sizeof(fAudioOutput));
	memset(&fAudioInput, 0, sizeof(fAudioInput));
}


WebcamDevice::~WebcamDevice()
{
	StopCapture();

	// Clean up formats
	for (int32 i = 0; i < fSupportedFormats.CountItems(); i++)
		delete fSupportedFormats.ItemAt(i);
	fSupportedFormats.MakeEmpty();
}


uint32
WebcamDevice::FramesCaptured() const
{
	if (fVideoConsumer != NULL)
		return fVideoConsumer->FramesReceived();
	return 0;
}


uint32
WebcamDevice::FramesDropped() const
{
	if (fVideoConsumer != NULL)
		return fVideoConsumer->FramesDropped();
	return 0;
}


float
WebcamDevice::CurrentFPS() const
{
	if (fVideoConsumer != NULL)
		return fVideoConsumer->CurrentFPS();
	return 0.0f;
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
	BDirectory devDir("/dev/video");
	if (devDir.InitCheck() == B_OK) {
		BEntry entry;
		while (devDir.GetNextEntry(&entry) == B_OK) {
			BPath path;
			entry.GetPath(&path);
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

	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_VIDEO;

	status_t status = roster->GetFormatFor(fMediaNode, &format,
		B_MEDIA_OUTPUT);
	if (status != B_OK)
		return;

	if (format.type == B_MEDIA_RAW_VIDEO) {
		VideoFormat* vf = new VideoFormat();
		vf->width = format.u.raw_video.display.line_width;
		vf->height = format.u.raw_video.display.line_count;
		vf->frameRate = format.u.raw_video.field_rate;

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

	// Try to enumerate more formats using ParameterWeb
	BParameterWeb* web = NULL;
	if (roster->GetParameterWebFor(fMediaNode, &web) == B_OK && web != NULL) {
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* param = web->ParameterAt(i);
			if (param != NULL) {
				// Log parameter info for debugging
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
	int32 outputCount = 0;
	media_output outputs[10];
	status_t status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10,
		&outputCount, B_MEDIA_RAW_AUDIO);

	if (status == B_OK && outputCount > 0) {
		fSupportsAudio = true;

		media_format format = outputs[0].format;
		if (format.type == B_MEDIA_RAW_AUDIO) {
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
}


status_t
WebcamDevice::StartCapture(BLooper* target)
{
	if (fIsCapturing)
		return B_BUSY;

	fTarget = target;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	status_t status;

	// Instantiate the dormant node if needed
	if (!fNodeInstantiated) {
		status = roster->InstantiateDormantNode(fDormantInfo, &fMediaNode,
			B_FLAVOR_IS_GLOBAL);
		if (status != B_OK) {
			fprintf(stderr, "Failed to instantiate node: %s\n",
				strerror(status));
			return status;
		}
		fNodeInstantiated = true;
		fMediaNodeID = fMediaNode.node;
	}

	// Set up video connection
	status = _SetupVideoConnection();
	if (status != B_OK) {
		fprintf(stderr, "Failed to set up video connection: %s\n",
			strerror(status));
		_TeardownConnections();
		return status;
	}

	// Set up audio connection if available
	if (fSupportsAudio) {
		status = _SetupAudioConnection();
		if (status != B_OK) {
			fprintf(stderr, "Note: Audio connection failed: %s\n",
				strerror(status));
			// Continue without audio
		}
	}

	// Get the time source
	media_node timeSource;
	status = roster->GetTimeSource(&timeSource);
	if (status != B_OK) {
		fprintf(stderr, "Failed to get time source: %s\n", strerror(status));
		_TeardownConnections();
		return status;
	}

	// Set time source for all nodes
	roster->SetTimeSourceFor(fMediaNode.node, timeSource.node);
	if (fVideoConsumer != NULL)
		roster->SetTimeSourceFor(fVideoConsumer->Node().node, timeSource.node);
	if (fAudioConsumer != NULL)
		roster->SetTimeSourceFor(fAudioConsumer->Node().node, timeSource.node);

	// Get performance time
	BTimeSource* ts = roster->MakeTimeSourceFor(timeSource);
	bigtime_t startTime = ts->Now() + 50000;  // Start in 50ms
	ts->Release();

	// Start nodes
	status = roster->StartNode(fMediaNode, startTime);
	if (status != B_OK) {
		fprintf(stderr, "Failed to start producer: %s\n", strerror(status));
		_TeardownConnections();
		return status;
	}

	if (fVideoConsumer != NULL) {
		status = roster->StartNode(fVideoConsumer->Node(), startTime);
		if (status != B_OK) {
			fprintf(stderr, "Failed to start video consumer: %s\n",
				strerror(status));
		}
	}

	if (fAudioConsumer != NULL) {
		status = roster->StartNode(fAudioConsumer->Node(), startTime);
		if (status != B_OK) {
			fprintf(stderr, "Failed to start audio consumer: %s\n",
				strerror(status));
		}
	}

	fIsCapturing = true;
	return B_OK;
}


void
WebcamDevice::StopCapture()
{
	if (!fIsCapturing)
		return;

	fIsCapturing = false;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster != NULL) {
		// Stop nodes
		roster->StopNode(fMediaNode, 0, true);

		if (fVideoConsumer != NULL)
			roster->StopNode(fVideoConsumer->Node(), 0, true);

		if (fAudioConsumer != NULL)
			roster->StopNode(fAudioConsumer->Node(), 0, true);
	}

	_TeardownConnections();

	fTarget = NULL;
}


status_t
WebcamDevice::_SetupVideoConnection()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	status_t status;

	// Create video consumer
	fVideoConsumer = new VideoConsumer("BubiCam Video", fTarget,
		MSG_FRAME_RECEIVED, MSG_AUDIO_LEVEL);

	// Register consumer
	status = roster->RegisterNode(fVideoConsumer);
	if (status != B_OK) {
		fprintf(stderr, "Failed to register video consumer: %s\n",
			strerror(status));
		delete fVideoConsumer;
		fVideoConsumer = NULL;
		return status;
	}

	// Get free video outputs from producer
	int32 outputCount = 0;
	media_output outputs[10];
	status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
		B_MEDIA_RAW_VIDEO);

	if (status != B_OK || outputCount == 0) {
		fprintf(stderr, "No video outputs available\n");
		return B_ERROR;
	}

	fVideoOutput = outputs[0];

	// Get consumer input
	int32 inputCount = 0;
	media_input inputs[1];
	status = roster->GetFreeInputsFor(fVideoConsumer->Node(), inputs, 1,
		&inputCount, B_MEDIA_RAW_VIDEO);

	if (status != B_OK || inputCount == 0) {
		fprintf(stderr, "No video inputs available on consumer\n");
		return B_ERROR;
	}

	fVideoInput = inputs[0];

	// Connect producer to consumer
	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status != B_OK) {
		fprintf(stderr, "Failed to connect video: %s\n", strerror(status));
		return status;
	}

	fVideoConnected = true;

	// Update format info
	if (format.type == B_MEDIA_RAW_VIDEO) {
		fCurrentFormat.width = format.u.raw_video.display.line_width;
		fCurrentFormat.height = format.u.raw_video.display.line_count;
		fCurrentFormat.frameRate = format.u.raw_video.field_rate;
	}

	return B_OK;
}


status_t
WebcamDevice::_SetupAudioConnection()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	status_t status;

	// Create audio consumer
	fAudioConsumer = new AudioConsumer("BubiCam Audio", fTarget,
		MSG_AUDIO_LEVEL);

	// Register consumer
	status = roster->RegisterNode(fAudioConsumer);
	if (status != B_OK) {
		fprintf(stderr, "Failed to register audio consumer: %s\n",
			strerror(status));
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return status;
	}

	// Get free audio outputs from producer
	int32 outputCount = 0;
	media_output outputs[10];
	status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
		B_MEDIA_RAW_AUDIO);

	if (status != B_OK || outputCount == 0) {
		fprintf(stderr, "No audio outputs available\n");
		roster->UnregisterNode(fAudioConsumer);
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return B_ERROR;
	}

	fAudioOutput = outputs[0];

	// Get consumer input
	int32 inputCount = 0;
	media_input inputs[1];
	status = roster->GetFreeInputsFor(fAudioConsumer->Node(), inputs, 1,
		&inputCount, B_MEDIA_RAW_AUDIO);

	if (status != B_OK || inputCount == 0) {
		fprintf(stderr, "No audio inputs available on consumer\n");
		roster->UnregisterNode(fAudioConsumer);
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return B_ERROR;
	}

	fAudioInput = inputs[0];

	// Connect producer to consumer
	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;

	status = roster->Connect(fAudioOutput.source, fAudioInput.destination,
		&format, &fAudioOutput, &fAudioInput);

	if (status != B_OK) {
		fprintf(stderr, "Failed to connect audio: %s\n", strerror(status));
		roster->UnregisterNode(fAudioConsumer);
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return status;
	}

	fAudioConnected = true;

	// Update audio info
	if (format.type == B_MEDIA_RAW_AUDIO) {
		fAudioSampleRate = format.u.raw_audio.frame_rate;
		fAudioChannels = format.u.raw_audio.channel_count;
	}

	return B_OK;
}


void
WebcamDevice::_TeardownConnections()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Disconnect and unregister video consumer
	if (fVideoConnected) {
		roster->Disconnect(fVideoOutput.node.node, fVideoOutput.source,
			fVideoInput.node.node, fVideoInput.destination);
		fVideoConnected = false;
	}

	if (fVideoConsumer != NULL) {
		roster->UnregisterNode(fVideoConsumer);
		// Consumer will be deleted when it quits
		fVideoConsumer = NULL;
	}

	// Disconnect and unregister audio consumer
	if (fAudioConnected) {
		roster->Disconnect(fAudioOutput.node.node, fAudioOutput.source,
			fAudioInput.node.node, fAudioInput.destination);
		fAudioConnected = false;
	}

	if (fAudioConsumer != NULL) {
		roster->UnregisterNode(fAudioConsumer);
		fAudioConsumer = NULL;
	}

	// Release the producer node
	if (fNodeInstantiated) {
		roster->ReleaseNode(fMediaNode);
		fNodeInstantiated = false;
	}
}
