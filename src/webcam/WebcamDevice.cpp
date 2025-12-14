/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamDevice.h"
#include "VideoConsumer.h"
#include "AudioConsumer.h"
#include "MainWindow.h"

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Timing constants for media operations (in microseconds)
// Start delay gives driver time to initialize before first frame
const bigtime_t kMediaStartDelay = 100000;		// 100ms
// Wait time after seek to allow driver to stabilize
const bigtime_t kPostSeekDelay = 50000;			// 50ms

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
	fUsedLiveNode(false)
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
	fUsedLiveNode(false)
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
	fprintf(stderr, "WebcamDevice::~WebcamDevice() - destroying '%s'\n", fName.String());

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

	fprintf(stderr, "WebcamDevice::~WebcamDevice() - done\n");
}


// Helper to validate consumer pointer
static inline bool
IsValidPointer(const void* ptr)
{
	if (ptr == NULL)
		return false;
	// Check for obviously invalid addresses (like 0x3f800000 = 1.0f)
	uintptr_t addr = (uintptr_t)ptr;
	return (addr > 0x10000) && ((addr & 0x3) == 0);
}


uint32
WebcamDevice::FramesCaptured() const
{
	BAutolock lock(fCaptureLock);
	VideoConsumer* consumer = fVideoConsumer;
	if (IsValidPointer(consumer))
		return consumer->FramesReceived();
	return 0;
}


uint32
WebcamDevice::FramesDropped() const
{
	BAutolock lock(fCaptureLock);
	VideoConsumer* consumer = fVideoConsumer;
	if (IsValidPointer(consumer))
		return consumer->FramesDropped();
	return 0;
}


float
WebcamDevice::CurrentFPS() const
{
	BAutolock lock(fCaptureLock);
	VideoConsumer* consumer = fVideoConsumer;
	if (IsValidPointer(consumer))
		return consumer->CurrentFPS();
	return 0.0f;
}


BBitmap*
WebcamDevice::GetCurrentFrame() const
{
	BAutolock lock(fCaptureLock);
	VideoConsumer* consumer = fVideoConsumer;
	if (IsValidPointer(consumer))
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
	fprintf(stderr, "WebcamDevice::ParseUSBDescriptors()\n");

	// Find and parse USB video device descriptors
	// Pass pointer to avoid copy (which would cause dangling pointers in BList)
	FindAndParseUSBVideoDevice(&fUSBVideoInfo);

	if (fUSBVideoInfo.found) {
		fprintf(stderr, "  USB Video device found: %s %s\n",
			fUSBVideoInfo.vendorName.String(),
			fUSBVideoInfo.productName.String());
		fprintf(stderr, "  Diagnostic:\n%s\n", fUSBVideoInfo.diagnosticInfo.String());

		// Update our device info with USB info
		fUSBInfo.vendorID = fUSBVideoInfo.vendorID;
		fUSBInfo.productID = fUSBVideoInfo.productID;
		if (fUSBVideoInfo.vendorName.Length() > 0)
			fUSBInfo.vendorName = fUSBVideoInfo.vendorName;
		if (fUSBVideoInfo.productName.Length() > 0)
			fUSBInfo.productName = fUSBVideoInfo.productName;
		if (fUSBVideoInfo.serialNumber.Length() > 0)
			fUSBInfo.serialNumber = fUSBVideoInfo.serialNumber;

		// Log formats found and populate fSupportedFormats
		fprintf(stderr, "  Found %d format(s):\n", (int)fUSBVideoInfo.formats.CountItems());

		// Clear existing formats
		for (int32 i = 0; i < fSupportedFormats.CountItems(); i++)
			delete fSupportedFormats.ItemAt(i);
		fSupportedFormats.MakeEmpty();

		for (int32 f = 0; f < fUSBVideoInfo.formats.CountItems(); f++) {
			USBVideoFormat* usbFormat = fUSBVideoInfo.formats.ItemAt(f);
			if (usbFormat != NULL) {
				fprintf(stderr, "    Format %d: %s (%d frames)\n",
					usbFormat->formatIndex, usbFormat->formatName.String(),
					(int)usbFormat->frames.CountItems());

				for (int32 fr = 0; fr < usbFormat->frames.CountItems(); fr++) {
					USBVideoFrame* frame = usbFormat->frames.ItemAt(fr);
					if (frame != NULL) {
						fprintf(stderr, "      Frame %d: %dx%d @ %.1f fps\n",
							(int)fr, frame->width, frame->height, frame->defaultFrameRate);

						// Add to fSupportedFormats for Format menu
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
			}
		}

		fprintf(stderr, "  Populated %d formats in Format menu\n",
			(int)fSupportedFormats.CountItems());

		return B_OK;
	}

	fprintf(stderr, "  No USB Video device found\n");
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

	// First try to enumerate formats using ParameterWeb
	// This works even when the node is already connected
	// This is more reliable than USB parsing after the device has been used
	// IMPORTANT: GetParameterWebFor only works on instantiated (non-dormant) nodes
	if (!fNodeInstantiated) {
		fprintf(stderr, "_GatherVideoFormats: node not instantiated, skipping ParameterWeb\n");
		return;
	}

	BParameterWeb* web = NULL;
	fprintf(stderr, "_GatherVideoFormats: getting ParameterWeb for node %d\n", fMediaNode.node);
	status_t webStatus = roster->GetParameterWebFor(fMediaNode, &web);
	fprintf(stderr, "_GatherVideoFormats: GetParameterWebFor returned %s, web=%p\n",
		strerror(webStatus), web);
	if (webStatus == B_OK && web != NULL) {
		fprintf(stderr, "_GatherVideoFormats: web has %d parameters\n", (int)web->CountParameters());
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* param = web->ParameterAt(i);
			if (param == NULL)
				continue;

			// Look for Resolution parameter (type=1 is BDiscreteParameter)
			BString paramName = param->Name();
			fprintf(stderr, "_GatherVideoFormats: checking param '%s'\n", paramName.String());

			if (paramName.IFindFirst("Resolution") >= 0 ||
				paramName.IFindFirst("Frame Size") >= 0) {
				BDiscreteParameter* discrete = dynamic_cast<BDiscreteParameter*>(param);
				if (discrete != NULL && discrete->CountItems() > 0) {
					fprintf(stderr, "_GatherVideoFormats: Found Resolution param with %d items\n",
						(int)discrete->CountItems());

					// Clear existing formats and use the driver's list
					for (int32 j = 0; j < fSupportedFormats.CountItems(); j++)
						delete fSupportedFormats.ItemAt(j);
					fSupportedFormats.MakeEmpty();

					for (int32 j = 0; j < discrete->CountItems(); j++) {
						const char* itemName = discrete->ItemNameAt(j);
						fprintf(stderr, "  Item %d: name='%s'\n", (int)j,
							itemName ? itemName : "(null)");

						if (itemName == NULL || itemName[0] == '\0')
							continue;

						// Parse resolution from item name (e.g., "640x480" or "640 x 480")
						int width = 0, height = 0;
						if (sscanf(itemName, "%dx%d", &width, &height) == 2 ||
							sscanf(itemName, "%d x %d", &width, &height) == 2) {
							VideoFormat* vf = new VideoFormat();
							vf->width = width;
							vf->height = height;
							vf->frameRate = 30.0f;  // Default
							strlcpy(vf->colorSpace, "YUY2", sizeof(vf->colorSpace));
							fSupportedFormats.AddItem(vf);
							fprintf(stderr, "  -> Added format: %dx%d\n", width, height);
						}
					}

					fprintf(stderr, "_GatherVideoFormats: total formats: %d\n",
						(int)fSupportedFormats.CountItems());
				}
			}
		}
		delete web;
	}

	// Fallback: if ParameterWeb didn't give us formats, try GetFreeOutputsFor
	// (only works when node is not yet connected)
	if (fSupportedFormats.CountItems() == 0) {
		fprintf(stderr, "_GatherVideoFormats: trying GetFreeOutputsFor fallback\n");
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
				fprintf(stderr, "_GatherVideoFormats: fallback added format %dx%d\n",
					vf->width, vf->height);
			}
		}
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
	fprintf(stderr, "WebcamDevice::StartCapture() called\n");

	if (fIsCapturing) {
		fprintf(stderr, "  -> Already capturing, returning B_BUSY\n");
		return B_BUSY;
	}

	fTarget = target;
	fUsedLiveNode = false;  // Track if we used an existing live node

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fprintf(stderr, "  -> BMediaRoster not available!\n");
		return B_ERROR;
	}

	status_t status;

	// Try to get the video input node
	fprintf(stderr, "  -> fNodeInstantiated = %s\n", fNodeInstantiated ? "true" : "false");
	if (!fNodeInstantiated) {
		// IMPORTANT: Always instantiate a fresh dormant node.
		// Live nodes may have stale internal state (fConnected=true) from previous
		// connections that weren't properly terminated, causing Connect() to fail
		// silently without creating the buffer group.
		// This matches CodyCam's behavior - it always uses InstantiateDormantNode.
		{
			// No live node found, instantiate from dormant
			fprintf(stderr, "  -> No live node found, instantiating dormant node '%s' (addon=%d, flavor=%d)...\n",
				fDormantInfo.name, fDormantInfo.addon, fDormantInfo.flavor_id);

			// Use InstantiateDormantNode to get the specific webcam node
			status = roster->InstantiateDormantNode(fDormantInfo, &fMediaNode, 0);
			if (status != B_OK) {
				fprintf(stderr, "  -> First attempt failed (%s), trying with B_FLAVOR_IS_GLOBAL...\n",
					strerror(status));
				status = roster->InstantiateDormantNode(fDormantInfo, &fMediaNode,
					B_FLAVOR_IS_GLOBAL);
			}

			if (status != B_OK) {
				fprintf(stderr, "  -> Failed to instantiate node: %s (0x%x)\n",
					strerror(status), status);
				return status;
			}
			fNodeInstantiated = true;
			fMediaNodeID = fMediaNode.node;
			fUsedLiveNode = false;
		}

		fprintf(stderr, "  -> Node ready, ID = %d, port = %d, kind = 0x%x, usedLiveNode=%s\n",
			fMediaNodeID, fMediaNode.port, (int)fMediaNode.kind,
			fUsedLiveNode ? "true" : "false");
	} else {
		fprintf(stderr, "  -> Node already instantiated, ID = %d\n", fMediaNodeID);
	}

	// Set up video connection
	fprintf(stderr, "  -> Setting up video connection...\n");
	status = _SetupVideoConnection();
	if (status != B_OK) {
		fprintf(stderr, "  -> Failed to set up video connection: %s (0x%x)\n",
			strerror(status), status);
		_TeardownConnections();
		return status;
	}
	fprintf(stderr, "  -> Video connection established\n");

	// Set up audio connection if available
	if (fSupportsAudio) {
		fprintf(stderr, "  -> Setting up audio connection...\n");
		status = _SetupAudioConnection();
		if (status != B_OK) {
			fprintf(stderr, "  -> Note: Audio connection failed: %s\n",
				strerror(status));
			// Continue without audio
		}
	}

	// Get the time source
	media_node timeSource;
	status = roster->GetTimeSource(&timeSource);
	if (status != B_OK) {
		fprintf(stderr, "  -> Failed to get time source: %s\n", strerror(status));
		_TeardownConnections();
		return status;
	}
	fprintf(stderr, "  -> Got time source, node=%d\n", timeSource.node);

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

	// CRITICAL: Call Preroll() on the producer BEFORE StartNode
	// This gives the hardware a chance to prepare for streaming.
	// The USB webcam driver may need this to initialize properly.
	fprintf(stderr, "  -> Calling Preroll() on producer node...\n");
	roster->PrerollNode(fMediaNode);

	// Small delay to let driver prepare
	snooze(kPostSeekDelay);

	// Start nodes
	// NOTE: We ALWAYS call StartNode on the producer, even for "live" nodes.
	// "Live" in Media Kit means "instantiated and registered", NOT "already producing data".
	// The UVC driver won't start USB streaming until it receives StartNode().
	// This matches how Cortex handles all nodes (see NodeRef::_start() in Cortex source).
	fprintf(stderr, "  -> Starting producer node%s...\n",
		fUsedLiveNode ? " (existing live node)" : " (newly instantiated)");
	status = roster->StartNode(fMediaNode, startTime);
	if (status != B_OK) {
		fprintf(stderr, "  -> Failed to start producer: %s (0x%x)\n", strerror(status), status);
		// Don't fail completely - some drivers return errors but still work
		fprintf(stderr, "  -> Continuing despite StartNode error...\n");
	} else {
		fprintf(stderr, "  -> Producer started successfully\n");
	}

	if (fVideoConsumer != NULL) {
		fprintf(stderr, "  -> Starting video consumer node...\n");
		status = roster->StartNode(fVideoConsumer->Node(), startTime);
		if (status != B_OK) {
			fprintf(stderr, "  -> Failed to start video consumer: %s\n",
				strerror(status));
		} else {
			fprintf(stderr, "  -> Video consumer started at time %ld\n", startTime);
			fprintf(stderr, "  -> Consumer connected=%s, node=%d, port=%d\n",
				fVideoConsumer->IsConnected() ? "true" : "false",
				fVideoConsumer->Node().node,
				fVideoConsumer->Input().destination.port);
		}
	}

	// Verify connection is truly established by checking connected outputs
	fprintf(stderr, "  -> Verifying connection state...\n");
	int32 connCount = 0;
	media_output connOutputs[10];
	status = roster->GetConnectedOutputsFor(fMediaNode, connOutputs, 10, &connCount);
	fprintf(stderr, "  -> GetConnectedOutputsFor: status=%s, count=%d\n",
		strerror(status), (int)connCount);
	for (int32 i = 0; i < connCount; i++) {
		fprintf(stderr, "     Connected output %d: name='%s', dest.node=%d, dest.port=%d\n",
			(int)i, connOutputs[i].name,
			connOutputs[i].destination.id, connOutputs[i].destination.port);
	}

	// Also verify the consumer sees the connection
	if (fVideoConsumer != NULL) {
		media_input input = fVideoConsumer->Input();
		fprintf(stderr, "  -> Consumer input state: source.node=%d, source.port=%d, source.id=%d\n",
			input.source.port != 0 ? fMediaNode.node : -1,
			input.source.port, input.source.id);
	}

	if (fAudioConsumer != NULL) {
		fprintf(stderr, "  -> Starting audio consumer node...\n");
		status = roster->StartNode(fAudioConsumer->Node(), startTime);
		if (status != B_OK) {
			fprintf(stderr, "  -> Failed to start audio consumer: %s\n",
				strerror(status));
		}
	}

	fIsCapturing = true;

	// Refresh formats now that ParameterWeb is available
	fprintf(stderr, "  -> Refreshing video formats from ParameterWeb...\n");
	_GatherVideoFormats();
	fprintf(stderr, "  -> Formats after refresh: %d\n", (int)fSupportedFormats.CountItems());

	fprintf(stderr, "  -> Capture started successfully!\n");
	return B_OK;
}


void
WebcamDevice::StopCapture()
{
	if (!fIsCapturing)
		return;

	fprintf(stderr, "WebcamDevice::StopCapture() called\n");

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fprintf(stderr, "  -> BMediaRoster is NULL, skipping cleanup\n");
		BAutolock lock(fCaptureLock);
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

	// CRITICAL: Hold lock while accessing and clearing member pointers
	// This prevents other threads from accessing consumers during cleanup
	{
		BAutolock lock(fCaptureLock);

		// Save consumer pointers
		videoConsumer = fVideoConsumer;
		audioConsumer = fAudioConsumer;

		// FIRST: Tell consumers to stop sending messages to target
		// This must happen BEFORE we start Media Kit cleanup
		if (videoConsumer != NULL && IsValidPointer(videoConsumer)) {
			fprintf(stderr, "  -> Setting video consumer target to NULL\n");
			videoConsumer->SetTarget(NULL);
			videoConsumerNode = videoConsumer->Node();
		} else if (videoConsumer != NULL) {
			fprintf(stderr, "  -> WARNING: videoConsumer pointer looks invalid: %p\n",
				(void*)videoConsumer);
			videoConsumer = NULL;
		}

		if (audioConsumer != NULL && IsValidPointer(audioConsumer)) {
			fprintf(stderr, "  -> Setting audio consumer target to NULL\n");
			audioConsumer->SetTarget(NULL);
			audioConsumerNode = audioConsumer->Node();
		} else if (audioConsumer != NULL) {
			fprintf(stderr, "  -> WARNING: audioConsumer pointer looks invalid: %p\n",
				(void*)audioConsumer);
			audioConsumer = NULL;
		}

		// Save other state needed for cleanup
		producerNode = fMediaNode;
		wasVideoConnected = fVideoConnected;
		wasAudioConnected = fAudioConnected;
		nodeWasInstantiated = fNodeInstantiated;
		usedLiveNode = fUsedLiveNode;

		// Save connection info for disconnect
		videoOutput = fVideoOutput;
		videoInput = fVideoInput;
		audioOutput = fAudioOutput;
		audioInput = fAudioInput;

		fprintf(stderr, "  -> Saved local copies: video=%p (node=%d), audio=%p (node=%d)\n",
			(void*)videoConsumer, videoConsumerNode.node,
			(void*)audioConsumer, audioConsumerNode.node);

		// Clear ALL member pointers and flags while still holding lock
		// After releasing the lock, other threads will see NULL pointers
		fIsCapturing = false;
		fVideoConsumer = NULL;
		fAudioConsumer = NULL;
		fVideoConnected = false;
		fAudioConnected = false;
	}
	// Lock released here - other threads can now safely check fVideoConsumer (will be NULL)

	// Stop the producer node first (using saved data only)
	if (nodeWasInstantiated) {
		fprintf(stderr, "  -> Stopping producer node %d%s...\n",
			producerNode.node, usedLiveNode ? " (live node)" : "");
		roster->StopNode(producerNode, 0, true);
	}

	// Stop consumers using SAVED node info - NO method calls on consumer objects!
	if (videoConsumerNode.node > 0) {
		fprintf(stderr, "  -> Stopping video consumer node %d...\n", videoConsumerNode.node);
		roster->StopNode(videoConsumerNode, 0, true);
	}

	if (audioConsumerNode.node > 0) {
		fprintf(stderr, "  -> Stopping audio consumer node %d...\n", audioConsumerNode.node);
		roster->StopNode(audioConsumerNode, 0, true);
	}

	// Disconnect video (using saved connection info)
	if (wasVideoConnected) {
		fprintf(stderr, "  -> Disconnecting video...\n");
		roster->Disconnect(videoOutput.node.node, videoOutput.source,
			videoInput.node.node, videoInput.destination);
	}

	// Disconnect audio (using saved connection info)
	if (wasAudioConnected) {
		fprintf(stderr, "  -> Disconnecting audio...\n");
		roster->Disconnect(audioOutput.node.node, audioOutput.source,
			audioInput.node.node, audioInput.destination);
	}

	// Unregister consumers - these calls are safe because UnregisterNode
	// takes a BMediaNode* and only accesses the node ID
	if (videoConsumer != NULL && videoConsumerNode.node > 0) {
		fprintf(stderr, "  -> Unregistering video consumer...\n");
		roster->UnregisterNode(videoConsumer);
	}

	if (audioConsumer != NULL && audioConsumerNode.node > 0) {
		fprintf(stderr, "  -> Unregistering audio consumer...\n");
		roster->UnregisterNode(audioConsumer);
	}

	// Release the producer node only if we instantiated it (not for live nodes)
	if (nodeWasInstantiated) {
		if (!usedLiveNode) {
			fprintf(stderr, "  -> Releasing producer node...\n");
			roster->ReleaseNode(producerNode);
		} else {
			fprintf(stderr, "  -> Not releasing producer (live node, system-managed)\n");
		}
		fNodeInstantiated = false;
		fUsedLiveNode = false;
	}

	fTarget = NULL;
	fprintf(stderr, "  -> StopCapture() completed\n");
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
		_LogFormatNegotiation(BString().SetToFormat(
			"Failed to register video consumer: %s", strerror(status)).String());
		delete fVideoConsumer;
		fVideoConsumer = NULL;
		return status;
	}
	_LogFormatNegotiation(BString().SetToFormat(
		"Video consumer registered (node ID = %d)", fVideoConsumer->Node().node).String());

	// Get video outputs from producer
	// Try multiple approaches - some drivers only expose outputs in certain conditions
	int32 outputCount = 0;
	media_output outputs[10];

	// Approach 1: Get FREE outputs for RAW_VIDEO
	status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
		B_MEDIA_RAW_VIDEO);
	_LogFormatNegotiation(BString().SetToFormat(
		"GetFreeOutputsFor(RAW_VIDEO): status=%s, count=%d",
		strerror(status), (int)outputCount).String());

	// Approach 2: If no RAW_VIDEO outputs, try without type filter
	if (outputCount == 0) {
		status = roster->GetFreeOutputsFor(fMediaNode, outputs, 10, &outputCount,
			B_MEDIA_UNKNOWN_TYPE);
		_LogFormatNegotiation(BString().SetToFormat(
			"GetFreeOutputsFor(UNKNOWN_TYPE): status=%s, count=%d",
			strerror(status), (int)outputCount).String());
	}

	// Approach 3: If still no outputs, try GetAllOutputsFor (includes connected outputs)
	if (outputCount == 0) {
		status = roster->GetAllOutputsFor(fMediaNode, outputs, 10, &outputCount);
		_LogFormatNegotiation(BString().SetToFormat(
			"GetAllOutputsFor: status=%s, count=%d",
			strerror(status), (int)outputCount).String());

		// Log all outputs we found
		for (int32 i = 0; i < outputCount; i++) {
			_LogFormatNegotiation(BString().SetToFormat(
				"  Output %d: name='%s', type=%d, source.id=%d, dest.id=%d",
				i, outputs[i].name, outputs[i].format.type,
				outputs[i].source.id, outputs[i].destination.id).String());
		}
	}

	if (status != B_OK || outputCount == 0) {
		_AddDriverWarning("Driver does not expose any video outputs (tried FREE + ALL)");
		return B_ERROR;
	}

	fVideoOutput = outputs[0];
	media_format& producerFormat = fVideoOutput.format;

	// ===== DRIVER DIAGNOSTIC: Analyze what the driver reports =====
	_LogFormatNegotiation("\n=== DRIVER FORMAT ANALYSIS ===");

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

	_LogFormatNegotiation(BString().SetToFormat(
		"Driver declares: %dx%d, colorspace=0x%x, bpr=%d, fps=%.2f",
		(int)driverWidth, (int)driverHeight, (int)driverColorSpace,
		(int)driverBPR, driverFrameRate).String());

	// Check for invalid dimensions (0x0) and fix them
	if (driverWidth == 0 || driverHeight == 0) {
		_AddDriverWarning(BString().SetToFormat(
			"Driver reports INVALID dimensions: %dx%d (should be non-zero). "
			"The driver is not correctly initializing the format structure.",
			(int)driverWidth, (int)driverHeight).String());

		// FIX: Use dimensions from USB descriptors or default values
		// This is CRITICAL - the producer will use these to allocate buffers
		if (fUSBVideoInfo.found && fUSBVideoInfo.formats.CountItems() > 0) {
			// Try to get first available resolution from USB descriptors
			USBVideoFormat* usbFormat = fUSBVideoInfo.formats.ItemAt(0);
			if (usbFormat != NULL && usbFormat->frames.CountItems() > 0) {
				USBVideoFrame* frame = usbFormat->frames.ItemAt(0);
				if (frame != NULL && frame->width > 0 && frame->height > 0) {
					driverWidth = frame->width;
					driverHeight = frame->height;
					_LogFormatNegotiation(BString().SetToFormat(
						"  -> Using USB descriptor resolution: %dx%d",
						(int)driverWidth, (int)driverHeight).String());
				}
			}
		}
		// If still no dimensions, use safe defaults
		if (driverWidth == 0 || driverHeight == 0) {
			driverWidth = kFallbackWidth;
			driverHeight = kFallbackHeight;
			_LogFormatNegotiation(BString().SetToFormat(
				"  -> Using default resolution: %dx%d",
				(int)kFallbackWidth, (int)kFallbackHeight).String());
		}

		// Update the producer format with corrected dimensions
		producerFormat.u.raw_video.display.line_width = driverWidth;
		producerFormat.u.raw_video.display.line_count = driverHeight;
		// Also set bytes_per_row if missing (assuming 4 bytes per pixel for safety)
		if (producerFormat.u.raw_video.display.bytes_per_row == 0) {
			producerFormat.u.raw_video.display.bytes_per_row = driverWidth * 4;
		}
	}

	// Check for missing bytes_per_row
	if (driverBPR == 0 && driverWidth > 0) {
		_AddDriverWarning("Driver reports bytes_per_row=0 (should be width * bytes_per_pixel)");
	}

	// Get consumer input directly from the consumer object
	// (GetFreeInputsFor sometimes fails to find registered inputs)
	fVideoInput = fVideoConsumer->Input();
	_LogFormatNegotiation(BString().SetToFormat(
		"Consumer input (via Input()): port=%d, node=%d, dest.id=%d",
		fVideoInput.destination.port, fVideoInput.node.node,
		fVideoInput.destination.id).String());

	if (fVideoInput.destination.port <= 0) {
		_AddDriverWarning("Consumer input has invalid port - node may not be properly registered");
		return B_ERROR;
	}

	// ===== FORMAT NEGOTIATION =====
	_LogFormatNegotiation("\n=== FORMAT NEGOTIATION ===");

	media_format format;

	// ===== APPROACH -1: Try user-requested format first =====
	// First, we need to configure the driver's resolution parameter BEFORE connecting
	if (fHasRequestedFormat && fRequestedFormat.width > 0 && fRequestedFormat.height > 0) {
		_LogFormatNegotiation(BString().SetToFormat(
			"Configuring driver for user-requested format (%dx%d)...",
			(int)fRequestedFormat.width, (int)fRequestedFormat.height).String());

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
							status_t setStatus = discrete->SetValue(&value, sizeof(value), -1);
							_LogFormatNegotiation(BString().SetToFormat(
								"  -> Set Resolution parameter to index %d: %s",
								(int)matchIndex, strerror(setStatus)).String());
						} else {
							_LogFormatNegotiation("  -> Resolution not found in driver options");
						}
					}
					break;
				}
			}
			delete web;
		}
	}

	if (fHasRequestedFormat && fRequestedFormat.width > 0 && fRequestedFormat.height > 0) {
		_LogFormatNegotiation(BString().SetToFormat(
			"Attempt -1: User-requested format (%dx%d)...",
			(int)fRequestedFormat.width, (int)fRequestedFormat.height).String());

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
			_LogFormatNegotiation(BString().SetToFormat(
				"\n=== CONNECTION SUCCESSFUL (user-requested format) ===\n"
				"Final format: %dx%d, colorspace=0x%x, fps=%.2f",
				(int)format.u.raw_video.display.line_width,
				(int)format.u.raw_video.display.line_count,
				(int)format.u.raw_video.display.format,
				format.u.raw_video.field_rate).String());

			fVideoConnected = true;
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
			return B_OK;
		}

		_LogFormatNegotiation(BString().SetToFormat(
			"  -> REJECTED: %s (0x%08x), trying fallbacks...",
			strerror(status), status).String());
	}

	// ===== APPROACH 0: Use CodyCam-style FULLY SPECIFIED format =====
	// CodyCam creates a complete format structure with all fields filled in.
	// This is critical because the USB webcam driver has a bug where it doesn't
	// properly initialize its format, so we must pass a complete format.
	_LogFormatNegotiation("Attempt 0: CodyCam-style fully specified format (320x240 B_RGB32)...");

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

	_LogFormatNegotiation(BString().SetToFormat(
		"  Using format: type=%d, %dx%d, cs=0x%x, bpr=%d, fps=%.2f",
		format.type,
		(int)format.u.raw_video.display.line_width,
		(int)format.u.raw_video.display.line_count,
		(int)format.u.raw_video.display.format,
		(int)format.u.raw_video.display.bytes_per_row,
		format.u.raw_video.field_rate).String());

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		// Success!
		_LogFormatNegotiation(BString().SetToFormat(
			"\n=== CONNECTION SUCCESSFUL (CodyCam format) ===\n"
			"Final format: %dx%d, colorspace=0x%x, bpr=%d, fps=%.2f",
			(int)format.u.raw_video.display.line_width,
			(int)format.u.raw_video.display.line_count,
			(int)format.u.raw_video.display.format,
			(int)format.u.raw_video.display.bytes_per_row,
			format.u.raw_video.field_rate).String());

		fVideoConnected = true;

		if (format.type == B_MEDIA_RAW_VIDEO) {
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
		}

		return B_OK;
	}

	_LogFormatNegotiation(BString().SetToFormat(
		"  -> REJECTED: %s (0x%08x)", strerror(status), status).String());

	// ===== APPROACH 1: Pure wildcard format =====
	_LogFormatNegotiation("Attempt 1: Pure wildcard format...");
	format = media_format();
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		_LogFormatNegotiation(BString().SetToFormat(
			"\n=== CONNECTION SUCCESSFUL (wildcard) ===\n"
			"Final format: %dx%d, colorspace=0x%x, fps=%.2f",
			(int)format.u.raw_video.display.line_width,
			(int)format.u.raw_video.display.line_count,
			(int)format.u.raw_video.display.format,
			format.u.raw_video.field_rate).String());

		fVideoConnected = true;

		if (format.type == B_MEDIA_RAW_VIDEO) {
			fCurrentFormat.width = format.u.raw_video.display.line_width;
			fCurrentFormat.height = format.u.raw_video.display.line_count;
			fCurrentFormat.frameRate = format.u.raw_video.field_rate;
		}

		return B_OK;
	}

	_LogFormatNegotiation(BString().SetToFormat(
		"  -> REJECTED: %s (0x%08x)", strerror(status), status).String());

	// ===== APPROACH 2: Try ENCODED_VIDEO (MJPEG) format =====
	_LogFormatNegotiation("Attempt 2: Encoded video (MJPEG) format...");
	format = media_format();
	format.type = B_MEDIA_ENCODED_VIDEO;
	format.u.encoded_video = media_encoded_video_format::wildcard;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		_LogFormatNegotiation(BString().SetToFormat(
			"\n=== CONNECTION SUCCESSFUL (encoded video) ===\n"
			"Format type: %d (ENCODED_VIDEO=%d)",
			format.type, B_MEDIA_ENCODED_VIDEO).String());

		fVideoConnected = true;
		// For encoded video, dimensions might not be in the format
		return B_OK;
	}

	_LogFormatNegotiation(BString().SetToFormat(
		"  -> REJECTED: %s (0x%08x)", strerror(status), status).String());

	// ===== APPROACH 3: B_MEDIA_UNKNOWN_TYPE - fully permissive =====
	_LogFormatNegotiation("Attempt 3: Unknown type (fully permissive)...");
	format = media_format();
	format.type = B_MEDIA_UNKNOWN_TYPE;

	status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
		&format, &fVideoOutput, &fVideoInput);

	if (status == B_OK) {
		_LogFormatNegotiation(BString().SetToFormat(
			"\n=== CONNECTION SUCCESSFUL (unknown type) ===\n"
			"Negotiated type: %d", format.type).String());

		fVideoConnected = true;
		return B_OK;
	}

	_LogFormatNegotiation(BString().SetToFormat(
		"  -> REJECTED: %s (0x%08x)", strerror(status), status).String());
	_LogFormatNegotiation("Trying specific resolutions...");

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
		_LogFormatNegotiation("Using resolutions from USB descriptor parsing:");
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
				attempt->source = "USB descriptor";
				attempts.AddItem(attempt);

				_LogFormatNegotiation(BString().SetToFormat(
					"  - %dx%d from %s (%s)",
					frame->width, frame->height,
					usbFormat->formatName.String(), attempt->source).String());
			}
		}
	} else {
		_AddDriverWarning("No USB descriptor format information available. "
			"Using fallback resolutions.");
	}

	// Priority 2: Use driver-declared resolution (if valid)
	if (driverWidth > 0 && driverHeight > 0) {
		ResolutionAttempt* attempt = new ResolutionAttempt();
		attempt->width = driverWidth;
		attempt->height = driverHeight;
		attempt->colorSpace = driverColorSpace != 0 ? driverColorSpace : B_YCbCr422;
		attempt->source = "driver declaration";
		attempts.AddItem(attempt);
		_LogFormatNegotiation(BString().SetToFormat(
			"  - %dx%d from driver declaration", driverWidth, driverHeight).String());
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
	// Note: 'format' is already declared above for the initial wildcard attempt
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
		_LogFormatNegotiation(BString().SetToFormat(
			"Attempt %d: %dx%d %s (from %s)...",
			attemptNum, attempt->width, attempt->height,
			attempt->colorSpace == B_YCbCr422 ? "YCbCr422" :
			attempt->colorSpace == B_RGB32 ? "RGB32" : "other",
			attempt->source).String());

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
			_LogFormatNegotiation(BString().SetToFormat(
				"  -> REJECTED: %s (0x%08x)",
				strerror(status), status).String());

			// If YCbCr422 failed, try RGB32 at same resolution
			if (attempt->colorSpace == B_YCbCr422) {
				attemptNum++;
				_LogFormatNegotiation(BString().SetToFormat(
					"Attempt %d: %dx%d RGB32 (fallback)...",
					attemptNum, attempt->width, attempt->height).String());

				format.u.raw_video.display.format = B_RGB32;
				format.u.raw_video.display.bytes_per_row = attempt->width * 4;

				status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
					&format, &fVideoOutput, &fVideoInput);

				if (status != B_OK) {
					_LogFormatNegotiation(BString().SetToFormat(
						"  -> REJECTED: %s (0x%08x)",
						strerror(status), status).String());
				}
			}
		}
	}

	// Last resort: wildcard format
	if (status != B_OK) {
		attemptNum++;
		_LogFormatNegotiation(BString().SetToFormat(
			"Attempt %d: Wildcard format (let driver decide)...", attemptNum).String());

		format.type = B_MEDIA_RAW_VIDEO;
		format.u.raw_video = media_raw_video_format::wildcard;

		status = roster->Connect(fVideoOutput.source, fVideoInput.destination,
			&format, &fVideoOutput, &fVideoInput);

		if (status != B_OK) {
			_LogFormatNegotiation(BString().SetToFormat(
				"  -> REJECTED: %s (0x%08x)", strerror(status), status).String());
		}
	}

	// ===== RESULT =====
	if (status != B_OK) {
		_AddDriverWarning(BString().SetToFormat(
			"FORMAT NEGOTIATION FAILED after %d attempts. "
			"The driver rejected all proposed formats. "
			"This may indicate a driver bug or unsupported resolution.",
			attemptNum).String());
		_LogFormatNegotiation("\n=== CONNECTION FAILED ===");
		return status;
	}

	// Success!
	_LogFormatNegotiation(BString().SetToFormat(
		"\n=== CONNECTION SUCCESSFUL ===\n"
		"Final format: %dx%d, colorspace=0x%x, fps=%.2f",
		(int)format.u.raw_video.display.line_width,
		(int)format.u.raw_video.display.line_count,
		(int)format.u.raw_video.display.format,
		format.u.raw_video.field_rate).String());

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
	format = media_format();
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
	fprintf(stderr, "DRIVER WARNING: %s\n", warning);
}


void
WebcamDevice::_LogFormatNegotiation(const char* message)
{
	if (fFormatNegotiationLog.Length() > 0)
		fFormatNegotiationLog << "\n";
	fFormatNegotiationLog << message;
	fprintf(stderr, "%s\n", message);
}
