/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamDevice.h"
#include "VideoConsumer.h"
#include "AudioConsumer.h"
#include "MainWindow.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "WebcamDevice"
#include "ErrorUtils.h"

#include <Autolock.h>
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

#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Helper: call roster->StopNode() in a thread with a 3-second timeout.
// Returns B_TIMED_OUT if the call doesn't complete in time.
struct StopNodeData {
	BMediaRoster*	roster;
	media_node		node;
	volatile bool	done;
	bool			callerOwns;  // true = caller deletes, false = thread deletes
};

static int32
_StopNodeThread(void* data)
{
	StopNodeData* snd = static_cast<StopNodeData*>(data);
	snd->roster->StopNode(snd->node, 0, true);
	snd->done = true;
	// If caller timed out, we own the data and must clean up
	if (!snd->callerOwns)
		delete snd;
	return 0;
}

static status_t
StopNodeWithTimeout(BMediaRoster* roster, const media_node& node,
	bigtime_t timeout = 3000000)
{
	// Heap-allocate to avoid stack corruption if the thread outlives us
	StopNodeData* data = new StopNodeData();
	data->roster = roster;
	data->node = node;
	data->done = false;
	data->callerOwns = true;

	thread_id tid = spawn_thread(_StopNodeThread, "stop_node",
		B_NORMAL_PRIORITY, data);
	if (tid < 0) {
		delete data;
		return roster->StopNode(node, 0, true);  // fallback
	}

	resume_thread(tid);

	bigtime_t deadline = system_time() + timeout;
	while (!data->done && system_time() < deadline)
		snooze(50000);  // 50ms poll

	if (data->done) {
		status_t exitValue;
		wait_for_thread(tid, &exitValue);
		delete data;
		return B_OK;
	}

	// Timed out - hand ownership to the thread so it can clean up
	data->callerOwns = false;
	fprintf(stderr, "WebcamDevice: StopNode timed out after %.1f s\n",
		timeout / 1000000.0);
	return B_TIMED_OUT;
}


// Timing constants for media operations (in microseconds)
// Start delay gives driver time to initialize before first frame
const bigtime_t kMediaStartDelay = 100000;		// 100ms
// Wait time after seek to allow driver to stabilize
const bigtime_t kPostSeekDelay = 50000;			// 50ms
// Drain time before stopping nodes - allows the EHCI isochronous finish
// thread to complete pending USB transfers, preventing a kernel GPF in
// EHCI::FinishIsochronousTransfers (Haiku bug in hrev59722+)
const bigtime_t kIsochronousDrainDelay = 200000;	// 200ms

// Fallback resolution when driver reports invalid dimensions.
// 320x240 (QVGA) is universally supported by USB webcams.
const int32 kFallbackWidth = 320;
const int32 kFallbackHeight = 240;


WebcamDevice::WebcamDevice(const media_node& node, const dormant_node_info& info)
	:
	fSupportsVideo(true),
	fHasRequestedFormat(false),
	fSupportsAudio(false),
	fAudioSampleRate(0),
	fAudioChannels(0),
	fAudioBitsPerSample(0),
	fMediaNode(node),
	fMediaNodeID(node.node),
	fDormantInfo(info),
	fNodeInstantiated(true),
	fInstantiateError(B_OK),
	fVideoConsumer(NULL),
	fAudioConsumer(NULL),
	fVideoConnected(false),
	fAudioConnected(false),
	fIsCapturing(false),
	fTarget(NULL),
	fUsedLiveNode(false),
	fAudioNodeID(-1)
{
	fName = info.name;
	fSupportedFormats.MakeEmpty();

	fVideoOutput = media_output();
	fVideoInput = media_input();
	fAudioOutput = media_output();
	fAudioInput = media_input();
}


WebcamDevice::WebcamDevice(const dormant_node_info& info, status_t instantiateError)
	:
	fSupportsVideo(true),
	fHasRequestedFormat(false),
	fSupportsAudio(false),
	fAudioSampleRate(0),
	fAudioChannels(0),
	fAudioBitsPerSample(0),
	fMediaNodeID(-1),
	fDormantInfo(info),
	fNodeInstantiated(false),
	fInstantiateError(instantiateError),
	fVideoConsumer(NULL),
	fAudioConsumer(NULL),
	fVideoConnected(false),
	fAudioConnected(false),
	fIsCapturing(false),
	fTarget(NULL),
	fUsedLiveNode(false),
	fAudioNodeID(-1)
{
	fName = info.name;
	fSupportedFormats.MakeEmpty();

	fMediaNode = media_node();
	fVideoOutput = media_output();
	fVideoInput = media_input();
	fAudioOutput = media_output();
	fAudioInput = media_input();
}


WebcamDevice::~WebcamDevice()
{
	LOG_DEBUG("Destroying device '%s'", fName.String());

	// Stop capture if active
	if (fIsCapturing)
		StopCapture();

	// CRITICAL: Always ensure the node is released, even if StopCapture wasn't called
	// This handles cases where StartCapture() failed partway through
	_TeardownConnections();

	// Clean up formats
	for (int32 i = 0; i < fSupportedFormats.CountItems(); i++)
		delete fSupportedFormats.ItemAt(i);
	fSupportedFormats.MakeEmpty();
}


uint32
WebcamDevice::FramesCaptured() const
{
	// CRITICAL FIX: Copy pointer under lock, release lock, then use pointer.
	// This prevents deadlock with StopCapture which may be waiting for fTargetLock
	// while we hold fCaptureLock.
	VideoConsumer* consumer = NULL;
	{
		BAutolock lock(fCaptureLock);
		consumer = fVideoConsumer;
	}
	if (consumer != NULL)
		return consumer->FramesReceived();
	return 0;
}


uint32
WebcamDevice::FramesDropped() const
{
	VideoConsumer* consumer = NULL;
	{
		BAutolock lock(fCaptureLock);
		consumer = fVideoConsumer;
	}
	if (consumer != NULL)
		return consumer->FramesDropped();
	return 0;
}


float
WebcamDevice::CurrentFPS() const
{
	VideoConsumer* consumer = NULL;
	{
		BAutolock lock(fCaptureLock);
		consumer = fVideoConsumer;
	}
	if (consumer != NULL)
		return consumer->CurrentFPS();
	return 0.0f;
}


BBitmap*
WebcamDevice::GetCurrentFrame() const
{
	VideoConsumer* consumer = NULL;
	{
		BAutolock lock(fCaptureLock);
		consumer = fVideoConsumer;
	}
	if (consumer != NULL)
		return consumer->GetCurrentFrame();
	return NULL;
}


status_t
WebcamDevice::GatherDeviceInfo()
{
	_GatherUSBInfo();
	_GatherDriverInfo();
	_GatherVideoFormats();
	_GatherAudioInfo();

	// Parse USB descriptors to get actual supported formats
	ParseUSBDescriptors();

	return B_OK;
}


status_t
WebcamDevice::ParseUSBDescriptors()
{
	// Find and parse USB video device descriptors
	// (FindAndParseUSBVideoDevice already logs the results via ErrorUtils)
	FindAndParseUSBVideoDevice(&fUSBVideoInfo);

	if (fUSBVideoInfo.found) {
		// Update our device info with USB info
		fUSBInfo.vendorID = fUSBVideoInfo.vendorID;
		fUSBInfo.productID = fUSBVideoInfo.productID;
		if (fUSBVideoInfo.vendorName.Length() > 0)
			fUSBInfo.vendorName = fUSBVideoInfo.vendorName;
		if (fUSBVideoInfo.productName.Length() > 0)
			fUSBInfo.productName = fUSBVideoInfo.productName;
		if (fUSBVideoInfo.serialNumber.Length() > 0)
			fUSBInfo.serialNumber = fUSBVideoInfo.serialNumber;

		// Clear existing formats
		for (int32 i = 0; i < fSupportedFormats.CountItems(); i++)
			delete fSupportedFormats.ItemAt(i);
		fSupportedFormats.MakeEmpty();

		// Populate fSupportedFormats for Format menu
		for (int32 f = 0; f < fUSBVideoInfo.formats.CountItems(); f++) {
			USBVideoFormat* usbFormat = fUSBVideoInfo.formats.ItemAt(f);
			if (usbFormat == NULL)
				continue;

			for (int32 fr = 0; fr < usbFormat->frames.CountItems(); fr++) {
				USBVideoFrame* frame = usbFormat->frames.ItemAt(fr);
				if (frame == NULL)
					continue;

				VideoFormat* vf = new VideoFormat();
				vf->width = frame->width;
				vf->height = frame->height;
				vf->frameRate = frame->defaultFrameRate;
				strncpy(vf->colorSpace, usbFormat->formatName.String(),
					sizeof(vf->colorSpace) - 1);
				vf->colorSpace[sizeof(vf->colorSpace) - 1] = '\0';
				fSupportedFormats.AddItem(vf);
			}
		}

		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


void
WebcamDevice::_GatherUSBInfo()
{
	// Set default values - USB enumeration requires a subclass of BUSBRoster
	// which is complex to set up for device matching. For now, we use
	// information from the media node name and driver path.
	if (fUSBInfo.vendorName.Length() == 0)
		fUSBInfo.vendorName = "Unknown";
	if (fUSBInfo.productName.Length() == 0)
		fUSBInfo.productName = fName;
	if (fUSBInfo.usbVersion.Length() == 0)
		fUSBInfo.usbVersion = "2.0";

	// Try to detect USB class from name
	if (fName.IFindFirst("uvc") >= 0 ||
		fName.IFindFirst("webcam") >= 0 ||
		fName.IFindFirst("camera") >= 0) {
		fUSBInfo.deviceClass = 0x0E;  // Video class
		fUSBInfo.deviceSubclass = 0x01;  // Video control
		fUSBInfo.deviceProtocol = 0x00;
	}
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
				fDriverInfo.path = path.Path();
				break;
			}
		}
	}

	// Driver name from dormant info
	fDriverInfo.name = fDormantInfo.addon > 0 ?
		"Media Add-on" : "Built-in";

	// Try to get driver version
	BPath addonsPath;
	if (find_directory(B_SYSTEM_ADDONS_DIRECTORY, &addonsPath) == B_OK) {
		addonsPath.Append("media");
		fDriverInfo.version = "Unknown";
	}

	// Look for UVC driver
	if (fName.IFindFirst("uvc") >= 0 ||
		fName.IFindFirst("usb video") >= 0) {
		fDriverInfo.name = "usb_webcam (UVC)";
	}
}


void
WebcamDevice::_GatherVideoFormats()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// IMPORTANT: GetParameterWebFor only works on instantiated (non-dormant) nodes
	if (!fNodeInstantiated)
		return;

	BParameterWeb* web = NULL;
	status_t webStatus = roster->GetParameterWebFor(fMediaNode, &web);
	if (webStatus == B_OK && web != NULL) {
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* param = web->ParameterAt(i);
			if (param == NULL)
				continue;

			// Look for Resolution parameter
			BString paramName = param->Name();
			if (paramName.IFindFirst("Resolution") >= 0 ||
				paramName.IFindFirst("Frame Size") >= 0) {
				BDiscreteParameter* discrete = dynamic_cast<BDiscreteParameter*>(param);
				if (discrete != NULL && discrete->CountItems() > 0) {
					// Clear existing formats and use the driver's list
					for (int32 j = 0; j < fSupportedFormats.CountItems(); j++)
						delete fSupportedFormats.ItemAt(j);
					fSupportedFormats.MakeEmpty();

					for (int32 j = 0; j < discrete->CountItems(); j++) {
						const char* itemName = discrete->ItemNameAt(j);
						if (itemName == NULL || itemName[0] == '\0')
							continue;

						// Parse resolution from item name (e.g., "640x480")
						int width = 0, height = 0;
						if (sscanf(itemName, "%dx%d", &width, &height) == 2 ||
							sscanf(itemName, "%d x %d", &width, &height) == 2) {
							VideoFormat* vf = new VideoFormat();
							vf->width = width;
							vf->height = height;
							vf->frameRate = 30.0f;
							strlcpy(vf->colorSpace, "YUY2", sizeof(vf->colorSpace));
							fSupportedFormats.AddItem(vf);
						}
					}
				}
			}
		}
		delete web;
	}

	// Fallback: if ParameterWeb didn't give us formats, try GetFreeOutputsFor
	if (fSupportedFormats.CountItems() == 0) {
		int32 cookie = 0;
		media_output output;
		status_t status = roster->GetFreeOutputsFor(fMediaNode, &output, 1, &cookie,
			B_MEDIA_RAW_VIDEO);
		if (status == B_OK && cookie > 0) {
			media_format format = output.format;
			if (format.type == B_MEDIA_RAW_VIDEO) {
				VideoFormat* vf = new VideoFormat();
				vf->width = format.u.raw_video.display.line_width;
				vf->height = format.u.raw_video.display.line_count;
				vf->frameRate = format.u.raw_video.field_rate;

				switch (format.u.raw_video.display.format) {
					case B_RGB32:
						strlcpy(vf->colorSpace, "RGB32", sizeof(vf->colorSpace));
						break;
					case B_RGB24:
						strlcpy(vf->colorSpace, "RGB24", sizeof(vf->colorSpace));
						break;
					case B_RGB16:
						strlcpy(vf->colorSpace, "RGB16", sizeof(vf->colorSpace));
						break;
					case B_YCbCr422:
						strlcpy(vf->colorSpace, "YCbCr422", sizeof(vf->colorSpace));
						break;
					case B_YCbCr420:
						strlcpy(vf->colorSpace, "YCbCr420", sizeof(vf->colorSpace));
						break;
					default:
						strlcpy(vf->colorSpace, "Unknown", sizeof(vf->colorSpace));
				}

				fSupportedFormats.AddItem(vf);
				fCurrentFormat = *vf;
			}
		}
	}
}


void
WebcamDevice::_GatherAudioInfo()
{
	if (!fNodeInstantiated)
		return;

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
	LOG_INFO("Starting capture for '%s'", fName.String());

	BAutolock startLock(fCaptureLock);

	if (fIsCapturing) {
		LOG_WARNING("Already capturing");
		return B_BUSY;
	}

	fTarget = target;
	fUsedLiveNode = false;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		LOG_ERROR("BMediaRoster not available");
		return B_ERROR;
	}

	status_t status;

	// Instantiate node if needed
	if (!fNodeInstantiated) {
		LOG_DEBUG("Instantiating node '%s'", fDormantInfo.name);

		status = roster->InstantiateDormantNode(fDormantInfo, &fMediaNode, 0);
		if (status != B_OK) {
			status = roster->InstantiateDormantNode(fDormantInfo, &fMediaNode,
				B_FLAVOR_IS_GLOBAL);
		}

		if (status != B_OK) {
			LOG_ERROR("Failed to instantiate node: %s", strerror(status));
			return status;
		}
		fNodeInstantiated = true;
		fMediaNodeID = fMediaNode.node;
		fUsedLiveNode = false;
		LOG_DEBUG("Node instantiated, ID=%d", fMediaNodeID);
	}

	// Set up video connection
	status = _SetupVideoConnection();
	if (status != B_OK) {
		LOG_ERROR("Video connection failed: %s", strerror(status));
		_TeardownConnections();
		return status;
	}

	// Set up audio if not disabled by user
	if (fAudioNodeID != 0) {
		status = _SetupAudioConnection();
		if (status == B_OK) {
			fSupportsAudio = true;
			LOG_INFO("Audio connection established");
		} else {
			LOG_DEBUG("Audio not available: %s", strerror(status));
		}
	} else {
		LOG_DEBUG("Audio disabled by user");
	}

	// Get the time source
	media_node timeSource;
	status = roster->GetTimeSource(&timeSource);
	if (status != B_OK) {
		LOG_ERROR("Failed to get time source: %s", strerror(status));
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
	bigtime_t startTime = ts->Now() + kMediaStartDelay;
	ts->Release();

	// Preroll and start nodes
	roster->PrerollNode(fMediaNode);
	snooze(kPostSeekDelay);

	status = roster->StartNode(fMediaNode, startTime);
	if (status != B_OK) {
		LOG_WARNING("StartNode returned: %s (continuing anyway)", strerror(status));
	}

	if (fVideoConsumer != NULL) {
		status = roster->StartNode(fVideoConsumer->Node(), startTime);
		if (status != B_OK)
			LOG_WARNING("Video consumer start failed: %s", strerror(status));
	}

	if (fAudioConsumer != NULL) {
		status = roster->StartNode(fAudioConsumer->Node(), startTime);
		if (status != B_OK)
			LOG_DEBUG("Audio consumer start failed: %s", strerror(status));
	}

	fIsCapturing = true;

	// Refresh formats now that ParameterWeb is available
	_GatherVideoFormats();

	LOG_INFO("Capture started successfully");
	return B_OK;
}


void
WebcamDevice::StopCapture()
{
	// Acquire lock to prevent overlap with StartCapture
	BAutolock stopLock(fCaptureLock);

	if (!fIsCapturing)
		return;

	LOG_INFO("Stopping capture");

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		LOG_WARNING("BMediaRoster is NULL during cleanup");
		fIsCapturing = false;
		fTarget = NULL;
		return;
	}

	// Local variables to hold consumer data for cleanup
	VideoConsumer* videoConsumer = NULL;
	AudioConsumer* audioConsumer = NULL;
	media_node videoConsumerNode = {};
	media_node audioConsumerNode = {};
	media_node producerNode = {};
	bool wasVideoConnected = false;
	bool wasAudioConnected = false;
	bool nodeWasInstantiated = false;
	bool usedLiveNode = false;
	media_output videoOutput = {};
	media_input videoInput = {};
	media_output audioOutput = {};
	media_input audioInput = {};

	// CRITICAL FIX: Two-phase cleanup to prevent deadlock.
	// Phase 1: Copy pointers and clear members under lock
	// Phase 2: Call SetTarget() OUTSIDE lock to avoid deadlock with PostMessage

	// Phase 1: Copy and clear state (already under fCaptureLock from caller)
	videoConsumer = fVideoConsumer;
	audioConsumer = fAudioConsumer;

	// Get node info while we have valid pointers
	if (videoConsumer != NULL) {
		videoConsumerNode = videoConsumer->Node();
	}
	if (audioConsumer != NULL) {
		audioConsumerNode = audioConsumer->Node();
	}

	// Save state for cleanup
	producerNode = fMediaNode;
	wasVideoConnected = fVideoConnected;
	wasAudioConnected = fAudioConnected;
	nodeWasInstantiated = fNodeInstantiated;
	usedLiveNode = fUsedLiveNode;
	videoOutput = fVideoOutput;
	videoInput = fVideoInput;
	audioOutput = fAudioOutput;
	audioInput = fAudioInput;

	// Clear member pointers FIRST - this prevents any new messages from being
	// processed that reference these consumers
	fIsCapturing = false;
	fVideoConsumer = NULL;
	fAudioConsumer = NULL;
	fVideoConnected = false;
	fAudioConnected = false;

	// Release lock before blocking operations (SetTarget, StopNode, etc.)
	stopLock.Unlock();

	// Phase 2: Tell consumers to stop sending messages OUTSIDE lock.
	// This is safe because we've already cleared member pointers, so no other
	// code path can access these consumers. SetTarget(NULL) may briefly block
	// on fTargetLock, but won't cause deadlock since we don't hold fCaptureLock.
	if (videoConsumer != NULL) {
		videoConsumer->SetTarget(NULL);
	}
	if (audioConsumer != NULL) {
		audioConsumer->SetTarget(NULL);
	}

	// Allow EHCI isochronous finish thread to drain pending USB transfers
	// before stopping nodes. Without this delay, stopping the producer while
	// isochronous TDs are still queued can trigger a kernel GPF in
	// EHCI::FinishIsochronousTransfers (use-after-free of iTD).
	snooze(kIsochronousDrainDelay);

	// Stop nodes with timeout to prevent hanging on frozen drivers
	if (nodeWasInstantiated)
		StopNodeWithTimeout(roster, producerNode);
	if (videoConsumerNode.node > 0)
		StopNodeWithTimeout(roster, videoConsumerNode);
	if (audioConsumerNode.node > 0)
		StopNodeWithTimeout(roster, audioConsumerNode);

	// Disconnect
	if (wasVideoConnected)
		roster->Disconnect(videoOutput.node.node, videoOutput.source,
			videoInput.node.node, videoInput.destination);
	if (wasAudioConnected)
		roster->Disconnect(audioOutput.node.node, audioOutput.source,
			audioInput.node.node, audioInput.destination);

	// Unregister consumers
	if (videoConsumer != NULL && videoConsumerNode.node > 0)
		roster->UnregisterNode(videoConsumer);
	if (audioConsumer != NULL && audioConsumerNode.node > 0)
		roster->UnregisterNode(audioConsumer);

	// Release the producer node
	if (nodeWasInstantiated) {
		if (!usedLiveNode)
			roster->ReleaseNode(producerNode);
		fNodeInstantiated = false;
		fUsedLiveNode = false;
	}

	fTarget = NULL;
	LOG_DEBUG("Capture stopped");
}


status_t
WebcamDevice::_SetupVideoConnection()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	// Clear diagnostic logs for this connection attempt
	fDriverWarnings = "";
	fFormatNegotiationLog = "";

	status_t status;

	// Create video consumer
	fVideoConsumer = new VideoConsumer("BubiCam Video", fTarget,
		MSG_FRAME_RECEIVED, MSG_AUDIO_LEVEL);

	// Register consumer
	status = roster->RegisterNode(fVideoConsumer);
	if (status != B_OK) {
		LOG_ERROR("RegisterNode failed: %s", strerror(status));
		delete fVideoConsumer;
		fVideoConsumer = NULL;
		return status;
	}
	LOG_DEBUG("Consumer registered: node=%d", fVideoConsumer->Node().node);

	// Get video outputs from producer
	int32 outputCount = 0;
	media_output outputs[10];

	// Approach 1: Get FREE outputs for RAW_VIDEO
	status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
		B_MEDIA_RAW_VIDEO);
	LOG_DEBUG("GetFreeOutputsFor: count=%d", (int)outputCount);

	// Approach 2: If no RAW_VIDEO outputs, try without type filter
	if (outputCount == 0) {
		status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
			B_MEDIA_UNKNOWN_TYPE);
	}

	// Approach 3: If still no outputs, try GetAllOutputsFor
	if (outputCount == 0) {
		status = roster->GetAllOutputsFor(fMediaNode, outputs, 10, &outputCount);
	}

	if (status != B_OK || outputCount == 0) {
		_AddDriverWarning("No video outputs exposed by driver");
		return B_ERROR;
	}

	fVideoOutput = outputs[0];
	media_format& producerFormat = fVideoOutput.format;

	// ===== DRIVER DIAGNOSTIC: Analyze what the driver reports =====
	if (producerFormat.type != B_MEDIA_RAW_VIDEO) {
		_AddDriverWarning(BString().SetToFormat(
			"Driver reports unexpected format type: %d (expected B_MEDIA_RAW_VIDEO=%d)",
			producerFormat.type, B_MEDIA_RAW_VIDEO).String());
	}

	int32 driverWidth = producerFormat.u.raw_video.display.line_width;
	int32 driverHeight = producerFormat.u.raw_video.display.line_count;
	int32 driverBPR = producerFormat.u.raw_video.display.bytes_per_row;
	color_space driverColorSpace = producerFormat.u.raw_video.display.format;
	float driverFrameRate = producerFormat.u.raw_video.field_rate;

	LOG_DEBUG("Driver format: %dx%d %s bpr=%d @%.0ffps",
		(int)driverWidth, (int)driverHeight, ColorSpaceName(driverColorSpace),
		(int)driverBPR, driverFrameRate);

	// Check for invalid dimensions (0x0) and fix them
	if (driverWidth == 0 || driverHeight == 0) {
		_AddDriverWarning(BString().SetToFormat(
			"Driver reports INVALID dimensions: %dx%d", (int)driverWidth, (int)driverHeight).String());

		// FIX: Use dimensions from USB descriptors or default values
		if (fUSBVideoInfo.found && fUSBVideoInfo.formats.CountItems() > 0) {
			USBVideoFormat* usbFormat = fUSBVideoInfo.formats.ItemAt(0);
			if (usbFormat != NULL && usbFormat->frames.CountItems() > 0) {
				USBVideoFrame* frame = usbFormat->frames.ItemAt(0);
				if (frame != NULL && frame->width > 0 && frame->height > 0) {
					driverWidth = frame->width;
					driverHeight = frame->height;
					LOG_DEBUG("Using USB descriptor: %dx%d", (int)driverWidth, (int)driverHeight);
				}
			}
		}
		// If still no dimensions, use safe defaults
		if (driverWidth == 0 || driverHeight == 0) {
			driverWidth = kFallbackWidth;
			driverHeight = kFallbackHeight;
			LOG_DEBUG("Using fallback: %dx%d", (int)kFallbackWidth, (int)kFallbackHeight);
		}

		// Update the producer format with corrected dimensions
		producerFormat.u.raw_video.display.line_width = driverWidth;
		producerFormat.u.raw_video.display.line_count = driverHeight;
		if (producerFormat.u.raw_video.display.bytes_per_row == 0) {
			producerFormat.u.raw_video.display.bytes_per_row = driverWidth * 4;
		}
	}

	// Check for missing bytes_per_row
	if (driverBPR == 0 && driverWidth > 0) {
		_AddDriverWarning("Driver reports bytes_per_row=0");
	}

	// Get consumer input directly from the consumer object
	fVideoInput = fVideoConsumer->Input();
	LOG_DEBUG("Consumer input: port=%d node=%d", fVideoInput.destination.port, fVideoInput.node.node);

	if (fVideoInput.destination.port <= 0) {
		_AddDriverWarning("Consumer input has invalid port");
		return B_ERROR;
	}

	media_format format;

	// ===== APPROACH -1: Try user-requested format first =====
	// Configure the driver's resolution parameter BEFORE connecting
	if (fHasRequestedFormat && fRequestedFormat.width > 0 && fRequestedFormat.height > 0) {
		LOG_DEBUG("Trying user-requested format: %dx%d", (int)fRequestedFormat.width, (int)fRequestedFormat.height);

		// Get ParameterWeb to set the Resolution parameter
		BParameterWeb* web = NULL;
		status_t webStatus = roster->GetParameterWebFor(fMediaNode, &web);
		if (webStatus == B_OK && web != NULL) {
			for (int32 i = 0; i < web->CountParameters(); i++) {
				BParameter* param = web->ParameterAt(i);
				if (param == NULL)
					continue;

				BString paramName = param->Name();
				if (paramName.IFindFirst("Resolution") >= 0) {
					BDiscreteParameter* discrete = dynamic_cast<BDiscreteParameter*>(param);
					if (discrete != NULL) {
						// Find the index matching the requested resolution
						int32 matchIndex = -1;
						for (int32 j = 0; j < discrete->CountItems(); j++) {
							const char* itemName = discrete->ItemNameAt(j);
							if (itemName == NULL)
								continue;
							int w = 0, h = 0;
							if (sscanf(itemName, "%dx%d", &w, &h) == 2 ||
								sscanf(itemName, "%d x %d", &w, &h) == 2) {
								if (w == fRequestedFormat.width && h == fRequestedFormat.height) {
									matchIndex = j;
									break;
								}
							}
						}

						if (matchIndex >= 0) {
							int32 value = matchIndex;
							discrete->SetValue(&value, sizeof(value), -1);
						}
					}
					break;
				}
			}
			delete web;
		}
	}

	if (fHasRequestedFormat && fRequestedFormat.width > 0 && fRequestedFormat.height > 0) {

		format = media_format();
		format.type = B_MEDIA_RAW_VIDEO;
		format.u.raw_video.field_rate = fRequestedFormat.frameRate > 0 ? fRequestedFormat.frameRate : 30.0;
		format.u.raw_video.interlace = 1;
		format.u.raw_video.first_active = 0;
		format.u.raw_video.last_active = fRequestedFormat.height - 1;
		format.u.raw_video.orientation = B_VIDEO_TOP_LEFT_RIGHT;
		format.u.raw_video.pixel_width_aspect = 1;
		format.u.raw_video.pixel_height_aspect = 1;
		format.u.raw_video.display.format = B_RGB32;
		format.u.raw_video.display.line_width = fRequestedFormat.width;
		format.u.raw_video.display.line_count = fRequestedFormat.height;
		format.u.raw_video.display.bytes_per_row = fRequestedFormat.width * 4;
		format.u.raw_video.display.pixel_offset = 0;
		format.u.raw_video.display.line_offset = 0;

		status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
			&format, &fVideoOutput, &fVideoInput);

		if (status == B_OK) {
			LOG_INFO("Connected: %dx%d %s @%.0ffps (user-requested)",
				(int)format.u.raw_video.display.line_width,
				(int)format.u.raw_video.display.line_count,
				ColorSpaceName(format.u.raw_video.display.format),
				format.u.raw_video.field_rate);

			fVideoConnected = true;
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
			return B_OK;
		}

		LOG_DEBUG("User format rejected: %s", strerror(status));
	}

	// ===== APPROACH 0: Use CodyCam-style FULLY SPECIFIED format =====
	LOG_DEBUG("Trying CodyCam-style 320x240 RGB32...");

	// Create a FULLY SPECIFIED format like CodyCam does
	// {field_rate, interlace, first_active, last_active, orientation,
	//  pixel_width_aspect, pixel_height_aspect, display{format, line_width, line_count, bytes_per_row, ...}}
	format = media_format();
	format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format vid_format = {
		30.0,                    // field_rate (fps)
		1,                       // interlace
		0,                       // first_active
		239,                     // last_active (height - 1)
		B_VIDEO_TOP_LEFT_RIGHT,  // orientation
		1,                       // pixel_width_aspect
		1,                       // pixel_height_aspect
		{                        // display
			B_RGB32,             // format (colorspace)
			320,                 // line_width
			240,                 // line_count
			320 * 4,             // bytes_per_row (4 bytes per pixel for RGB32)
			0,                   // pixel_offset
			0                    // line_offset
		}
	};
	format.u.raw_video = vid_format;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		LOG_INFO("Connected: %dx%d %s @%.0ffps (CodyCam-style)",
			(int)format.u.raw_video.display.line_width,
			(int)format.u.raw_video.display.line_count,
			ColorSpaceName(format.u.raw_video.display.format),
			format.u.raw_video.field_rate);

		fVideoConnected = true;

		if (format.type == B_MEDIA_RAW_VIDEO) {
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
		}

		return B_OK;
	}

	LOG_DEBUG("CodyCam-style rejected: %s", strerror(status));

	// ===== APPROACH 1: Pure wildcard format =====
	LOG_DEBUG("Trying wildcard format...");
	format = media_format();
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		LOG_INFO("Connected: %dx%d %s @%.0ffps (wildcard)",
			(int)format.u.raw_video.display.line_width,
			(int)format.u.raw_video.display.line_count,
			ColorSpaceName(format.u.raw_video.display.format),
			format.u.raw_video.field_rate);

		fVideoConnected = true;

		if (format.type == B_MEDIA_RAW_VIDEO) {
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
		}

		return B_OK;
	}

	LOG_DEBUG("Wildcard rejected: %s", strerror(status));

	// ===== APPROACH 2: Try ENCODED_VIDEO (MJPEG) format =====
	LOG_DEBUG("Trying encoded video (MJPEG)...");
	format = media_format();
	format.type = B_MEDIA_ENCODED_VIDEO;
	format.u.encoded_video = media_encoded_video_format::wildcard;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		LOG_INFO("Connected: encoded video type=%d", format.type);
		fVideoConnected = true;
		return B_OK;
	}

	LOG_DEBUG("Encoded video rejected: %s", strerror(status));

	// ===== APPROACH 3: B_MEDIA_UNKNOWN_TYPE - fully permissive =====
	LOG_DEBUG("Trying unknown type (permissive)...");
	format = media_format();
	format.type = B_MEDIA_UNKNOWN_TYPE;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		LOG_INFO("Connected: unknown type, negotiated=%d", format.type);
		fVideoConnected = true;
		return B_OK;
	}

	LOG_DEBUG("Unknown type rejected: %s", strerror(status));

	// ===== BUILD LIST OF RESOLUTIONS TO TRY =====

	// Structure to hold resolution attempts
	struct ResolutionAttempt {
		int32 width;
		int32 height;
		color_space colorSpace;
		const char* source;
	};

	// BObjectList with owning=true (second template param) auto-deletes items
	BObjectList<ResolutionAttempt, true> attempts(20);

	// Priority 1: Use resolutions from USB descriptor parsing (if available)
	if (fUSBVideoInfo.found && fUSBVideoInfo.formats.CountItems() > 0) {
		LOG_DEBUG("Trying USB descriptor resolutions...");
		for (int32 f = 0; f < fUSBVideoInfo.formats.CountItems(); f++) {
			USBVideoFormat* usbFormat = fUSBVideoInfo.formats.ItemAt(f);
			if (usbFormat == NULL) continue;

			for (int32 fr = 0; fr < usbFormat->frames.CountItems(); fr++) {
				USBVideoFrame* frame = usbFormat->frames.ItemAt(fr);
				if (frame == NULL || frame->width == 0 || frame->height == 0) continue;

				ResolutionAttempt* attempt = new ResolutionAttempt();
				attempt->width = frame->width;
				attempt->height = frame->height;
				attempt->colorSpace = B_YCbCr422;  // UVC typically uses YUY2
				attempt->source = "USB";
				attempts.AddItem(attempt);
			}
		}
	} else {
		_AddDriverWarning("No USB descriptor info, using fallbacks");
	}

	// Priority 2: Use driver-declared resolution (if valid)
	if (driverWidth > 0 && driverHeight > 0) {
		ResolutionAttempt* attempt = new ResolutionAttempt();
		attempt->width = driverWidth;
		attempt->height = driverHeight;
		attempt->colorSpace = driverColorSpace != 0 ? driverColorSpace : B_YCbCr422;
		attempt->source = "driver";
		attempts.AddItem(attempt);
	}

	// Priority 3: Common fallback resolutions (320x240 first like CodyCam)
	static const struct { int32 w; int32 h; } fallbackRes[] = {
		{320, 240}, {640, 480}, {800, 600}, {1280, 720}, {352, 288}, {176, 144}
	};
	for (size_t i = 0; i < sizeof(fallbackRes)/sizeof(fallbackRes[0]); i++) {
		ResolutionAttempt* attempt = new ResolutionAttempt();
		attempt->width = fallbackRes[i].w;
		attempt->height = fallbackRes[i].h;
		attempt->colorSpace = B_YCbCr422;
		attempt->source = "fallback";
		attempts.AddItem(attempt);
	}

	// ===== TRY EACH RESOLUTION =====
	status = B_ERROR;
	int32 attemptNum = 0;

	for (int32 i = 0; i < attempts.CountItems() && status != B_OK; i++) {
		ResolutionAttempt* attempt = attempts.ItemAt(i);

		// Skip duplicates
		bool isDuplicate = false;
		for (int32 j = 0; j < i; j++) {
			ResolutionAttempt* prev = attempts.ItemAt(j);
			if (prev->width == attempt->width && prev->height == attempt->height &&
				prev->colorSpace == attempt->colorSpace) {
				isDuplicate = true;
				break;
			}
		}
		if (isDuplicate) continue;

		attemptNum++;
		LOG_DEBUG("Trying %dx%d %s (%s)...",
			attempt->width, attempt->height,
			ColorSpaceName(attempt->colorSpace), attempt->source);

		// Build format structure like CodyCam does
		format = media_format();
		format.type = B_MEDIA_RAW_VIDEO;
		// field_rate = 0 means let producer decide (like CodyCam)
		format.u.raw_video.field_rate = 0;
		format.u.raw_video.interlace = 1;
		format.u.raw_video.first_active = 0;
		format.u.raw_video.last_active = attempt->height - 1;
		format.u.raw_video.orientation = B_VIDEO_TOP_LEFT_RIGHT;
		format.u.raw_video.pixel_width_aspect = 1;
		format.u.raw_video.pixel_height_aspect = 1;
		format.u.raw_video.display.format = attempt->colorSpace;
		format.u.raw_video.display.line_width = attempt->width;
		format.u.raw_video.display.line_count = attempt->height;
		format.u.raw_video.display.bytes_per_row = attempt->width *
			(attempt->colorSpace == B_RGB32 ? 4 : 2);
		format.u.raw_video.display.pixel_offset = 0;
		format.u.raw_video.display.line_offset = 0;

		status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
			&format, &fVideoOutput, &fVideoInput);

		if (status != B_OK) {
			// If YCbCr422 failed, try RGB32 at same resolution
			if (attempt->colorSpace == B_YCbCr422) {
				attemptNum++;
				format.u.raw_video.display.format = B_RGB32;
				format.u.raw_video.display.bytes_per_row = attempt->width * 4;

				status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
					&format, &fVideoOutput, &fVideoInput);
			}
		}
	}

	// Last resort: wildcard format
	if (status != B_OK) {
		attemptNum++;
		LOG_DEBUG("Last resort: wildcard...");

		format.type = B_MEDIA_RAW_VIDEO;
		format.u.raw_video = media_raw_video_format::wildcard;

		status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
			&format, &fVideoOutput, &fVideoInput);
	}

	// ===== RESULT =====
	if (status != B_OK) {
		_AddDriverWarning(BString().SetToFormat(
			"Format negotiation failed after %d attempts", attemptNum).String());
		LOG_ERROR("Connection failed: %s", strerror(status));
		return status;
	}

	// Success!
	LOG_INFO("Connected: %dx%d %s @%.0ffps (attempt %d)",
		(int)format.u.raw_video.display.line_width,
		(int)format.u.raw_video.display.line_count,
		ColorSpaceName(format.u.raw_video.display.format),
		format.u.raw_video.field_rate, attemptNum);

	fVideoConnected = true;

	// Update format info
	if (format.type == B_MEDIA_RAW_VIDEO) {
		fCurrentFormat.width = format.u.raw_video.display.line_width;
		fCurrentFormat.height = format.u.raw_video.display.line_count;
		fCurrentFormat.frameRate = format.u.raw_video.field_rate;
	}

	return B_OK;
}


void
WebcamDevice::UpdateActualResolution(int32 width, int32 height)
{
	if (width > 0 && height > 0
		&& (fCurrentFormat.width != width || fCurrentFormat.height != height)) {
		fprintf(stderr, "WebcamDevice: Actual frame resolution %dx%d "
			"differs from negotiated %dx%d, updating\n",
			(int)width, (int)height,
			(int)fCurrentFormat.width, (int)fCurrentFormat.height);
		fCurrentFormat.width = width;
		fCurrentFormat.height = height;
	}
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
		LOG_DEBUG("Failed to register audio consumer: %s", strerror(status));
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return status;
	}

	// Strategy 1: Try to get audio outputs directly from the video producer node.
	// Some webcam drivers expose both video and audio from the same node.
	int32 outputCount = 0;
	media_output outputs[10];
	status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
		B_MEDIA_RAW_AUDIO);

	if (status != B_OK || outputCount == 0) {
		LOG_DEBUG("No audio output on video node, trying fallback...");

		media_node audioNode;
		bool foundAudioNode = false;

		// If user selected a specific live node, use that
		if (fAudioNodeID > 0) {
			status = roster->GetNodeFor(fAudioNodeID, &audioNode);
			if (status == B_OK) {
				status = roster->GetFreeOutputsFor(audioNode, outputs, 10,
					&outputCount, B_MEDIA_RAW_AUDIO);
				if (status == B_OK && outputCount > 0) {
					foundAudioNode = true;
					LOG_INFO("Using user-selected audio node ID=%d", fAudioNodeID);
				}
			}
		}

		// Otherwise use system default audio input
		if (!foundAudioNode) {
			status = roster->GetAudioInput(&audioNode);
			if (status == B_OK) {
				status = roster->GetFreeOutputsFor(audioNode, outputs, 10,
					&outputCount, B_MEDIA_RAW_AUDIO);
				if (status == B_OK && outputCount > 0) {
					foundAudioNode = true;
					LOG_INFO("Using system audio input for VU meter");
				}
			}
		}

		if (!foundAudioNode) {
			LOG_DEBUG("No audio input available");
			roster->UnregisterNode(fAudioConsumer);
			delete fAudioConsumer;
			fAudioConsumer = NULL;
			return B_ERROR;
		}
	}

	fAudioOutput = outputs[0];

	// Get consumer input
	int32 inputCount = 0;
	media_input inputs[1];
	status = roster->GetFreeInputsFor(fAudioConsumer->Node(), inputs, 1,
		&inputCount, B_MEDIA_RAW_AUDIO);

	if (status != B_OK || inputCount == 0) {
		roster->UnregisterNode(fAudioConsumer);
		delete fAudioConsumer;
		fAudioConsumer = NULL;
		return B_ERROR;
	}

	fAudioInput = inputs[0];

	// Connect producer to consumer.
	// Use the producer's advertised format rather than wildcard to avoid
	// divide-by-zero crashes in buggy drivers (e.g., AudioProducer::Connect
	// divides by channel_count * sample_size, which are 0 in wildcard format).
	media_format format = fAudioOutput.format;
	if (format.type != B_MEDIA_RAW_AUDIO) {
		format.type = B_MEDIA_RAW_AUDIO;
		format.u.raw_audio = media_raw_audio_format::wildcard;
	}
	// Ensure critical fields are non-zero to prevent driver crashes
	if (format.u.raw_audio.channel_count == 0)
		format.u.raw_audio.channel_count = 2;
	if (format.u.raw_audio.frame_rate == 0)
		format.u.raw_audio.frame_rate = 48000.0f;
	if (format.u.raw_audio.format == 0)
		format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;

	status = roster->Connect(fAudioOutput.source, fAudioInput.destination,
		&format, &fAudioOutput, &fAudioInput);

	if (status != B_OK) {
		LOG_DEBUG("Failed to connect audio: %s", strerror(status));
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
	// This function is now only called from destructor as a safety net
	// StopCapture() handles all the cleanup directly
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Just ensure everything is cleared - StopCapture should have done this already
	if (fVideoConnected) {
		roster->Disconnect(fVideoOutput.node.node, fVideoOutput.source,
			fVideoInput.node.node, fVideoInput.destination);
		fVideoConnected = false;
	}

	if (fAudioConnected) {
		roster->Disconnect(fAudioOutput.node.node, fAudioOutput.source,
			fAudioInput.node.node, fAudioInput.destination);
		fAudioConnected = false;
	}

	// Clear consumer pointers (they should already be NULL from StopCapture)
	fVideoConsumer = NULL;
	fAudioConsumer = NULL;

	// Release the producer node only if we instantiated it (not for live nodes)
	// Live nodes are system-managed and should not be released by us
	if (fNodeInstantiated) {
		if (!fUsedLiveNode) {
			roster->ReleaseNode(fMediaNode);
		}
		fNodeInstantiated = false;
		fUsedLiveNode = false;
	}
}


void
WebcamDevice::_AddDriverWarning(const char* warning)
{
	if (fDriverWarnings.Length() > 0)
		fDriverWarnings << "\n";
	fDriverWarnings << "⚠ " << warning;
	LOG_WARNING("%s", warning);
}


void
WebcamDevice::_LogFormatNegotiation(const char* message)
{
	if (fFormatNegotiationLog.Length() > 0)
		fFormatNegotiationLog << "\n";
	fFormatNegotiationLog << message;
	LOG_DEBUG("%s", message);
}
