/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef WEBCAM_DEVICE_H
#define WEBCAM_DEVICE_H

#include <String.h>
#include <Bitmap.h>
#include <Locker.h>
#include <Looper.h>
#include <MediaRoster.h>
#include <MediaNode.h>
#include <MediaAddOn.h>
#include <TimeSource.h>
#include <BufferGroup.h>
#include <ObjectList.h>

#include "USBVideoParser.h"

class VideoConsumer;
class AudioConsumer;

// Video format information
struct VideoFormat {
	int32		width;
	int32		height;
	float		frameRate;
	char		colorSpace[32];

	VideoFormat()
		: width(0), height(0), frameRate(0.0f)
	{
		colorSpace[0] = '\0';
	}
};


class WebcamDevice {
public:
						WebcamDevice(const media_node& node,
							const dormant_node_info& info);
						WebcamDevice(const dormant_node_info& info,
							status_t instantiateError);
						~WebcamDevice();

	// Device identification
	const char*			Name() const { return fName.String(); }
	const char*			DevicePath() const { return fDevicePath.String(); }

	// USB information
	uint16				VendorID() const { return fVendorID; }
	uint16				ProductID() const { return fProductID; }
	const char*			VendorName() const { return fVendorName.String(); }
	const char*			ProductName() const { return fProductName.String(); }
	const char*			SerialNumber() const { return fSerialNumber.String(); }
	const char*			USBVersion() const { return fUSBVersion.String(); }
	uint8				DeviceClass() const { return fDeviceClass; }
	uint8				DeviceSubclass() const { return fDeviceSubclass; }
	uint8				DeviceProtocol() const { return fDeviceProtocol; }

	// Driver information
	const char*			DriverName() const { return fDriverName.String(); }
	const char*			DriverPath() const { return fDriverPath.String(); }
	const char*			DriverVersion() const { return fDriverVersion.String(); }

	// Video capabilities
	bool				SupportsVideo() const { return fSupportsVideo; }
	const BObjectList<VideoFormat>&	SupportedFormats() const
							{ return fSupportedFormats; }
	VideoFormat			CurrentFormat() const { return fCurrentFormat; }
	void				SetRequestedFormat(const VideoFormat& format)
							{ fRequestedFormat = format; fHasRequestedFormat = true; }
	void				ClearRequestedFormat() { fHasRequestedFormat = false; }

	// Audio capabilities
	bool				SupportsAudio() const { return fSupportsAudio; }
	float				AudioSampleRate() const { return fAudioSampleRate; }
	int32				AudioChannels() const { return fAudioChannels; }
	int32				AudioBitsPerSample() const { return fAudioBitsPerSample; }

	// Media Kit information
	bool				IsRegisteredWithMediaKit() const
							{ return fMediaNodeID >= 0; }
	int32				MediaNodeID() const { return fMediaNodeID; }
	const media_node&	MediaNode() const { return fMediaNode; }
	bool				IsNodeInstantiated() const { return fNodeInstantiated; }
	status_t			InstantiateError() const { return fInstantiateError; }
	const dormant_node_info& DormantInfo() const { return fDormantInfo; }
	void				MarkNodeReleased() { fNodeInstantiated = false; }

	// Capture control
	status_t			StartCapture(BLooper* target);
	void				StopCapture();
	bool				IsCapturing() const { return fIsCapturing; }

	// Frame access (for MCP server)
	BBitmap*			GetCurrentFrame() const;

	// Capture statistics
	uint32				FramesCaptured() const;
	uint32				FramesDropped() const;
	float				CurrentFPS() const;

	// Information gathering
	status_t			GatherDeviceInfo();

	// USB Video Class information (parsed from USB descriptors)
	const USBVideoInfo&	GetUSBVideoInfo() const { return fUSBVideoInfo; }
	status_t			ParseUSBDescriptors();

	// Driver diagnostics (for debugging driver issues)
	const char*			GetDriverWarnings() const { return fDriverWarnings.String(); }
	bool				HasDriverWarnings() const { return fDriverWarnings.Length() > 0; }
	const char*			GetFormatNegotiationLog() const { return fFormatNegotiationLog.String(); }

private:
	void				_AddDriverWarning(const char* warning);
	void				_LogFormatNegotiation(const char* message);
	void				_GatherUSBInfo();
	void				_GatherDriverInfo();
	void				_GatherVideoFormats();
	void				_GatherAudioInfo();

	status_t			_SetupVideoConnection();
	status_t			_SetupAudioConnection();
	void				_TeardownConnections();

	// Device identification
	BString				fName;
	BString				fDevicePath;

	// USB information
	uint16				fVendorID;
	uint16				fProductID;
	BString				fVendorName;
	BString				fProductName;
	BString				fSerialNumber;
	BString				fUSBVersion;
	uint8				fDeviceClass;
	uint8				fDeviceSubclass;
	uint8				fDeviceProtocol;

	// Driver information
	BString				fDriverName;
	BString				fDriverPath;
	BString				fDriverVersion;

	// Video capabilities
	bool				fSupportsVideo;
	BObjectList<VideoFormat> fSupportedFormats;
	VideoFormat			fCurrentFormat;
	VideoFormat			fRequestedFormat;
	bool				fHasRequestedFormat;

	// Audio capabilities
	bool				fSupportsAudio;
	float				fAudioSampleRate;
	int32				fAudioChannels;
	int32				fAudioBitsPerSample;

	// Media Kit - Producer
	media_node			fMediaNode;
	int32				fMediaNodeID;
	dormant_node_info	fDormantInfo;
	bool				fNodeInstantiated;
	status_t			fInstantiateError;

	// Media Kit - Consumers
	VideoConsumer*		fVideoConsumer;
	AudioConsumer*		fAudioConsumer;

	// Media Kit - Connections
	media_output		fVideoOutput;
	media_input			fVideoInput;
	media_output		fAudioOutput;
	media_input			fAudioInput;
	bool				fVideoConnected;
	bool				fAudioConnected;

	// Capture state
	bool				fIsCapturing;
	BLooper*			fTarget;
	bool				fUsedLiveNode;	// True if we used an existing live node
	mutable BLocker		fCaptureLock;	// Protects consumer pointers during capture/stop

	// USB Video Class descriptor info
	USBVideoInfo		fUSBVideoInfo;

	// Driver diagnostics
	BString				fDriverWarnings;
	BString				fFormatNegotiationLog;
};

#endif // WEBCAM_DEVICE_H
