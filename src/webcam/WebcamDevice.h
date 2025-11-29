/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef WEBCAM_DEVICE_H
#define WEBCAM_DEVICE_H

#include <String.h>
#include <Bitmap.h>
#include <Looper.h>
#include <MediaRoster.h>
#include <MediaNode.h>
#include <MediaAddOn.h>
#include <TimeSource.h>
#include <BufferGroup.h>
#include <ObjectList.h>

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

	// Capture control
	status_t			StartCapture(BLooper* target);
	void				StopCapture();
	bool				IsCapturing() const { return fIsCapturing; }

	// Capture statistics
	uint32				FramesCaptured() const { return fFramesCaptured; }
	uint32				FramesDropped() const { return fFramesDropped; }
	float				CurrentFPS() const { return fCurrentFPS; }

	// Information gathering
	status_t			GatherDeviceInfo();

private:
	void				_GatherUSBInfo();
	void				_GatherDriverInfo();
	void				_GatherVideoFormats();
	void				_GatherAudioInfo();
	static int32		_CaptureThread(void* data);
	void				_CaptureLoop();

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

	// Audio capabilities
	bool				fSupportsAudio;
	float				fAudioSampleRate;
	int32				fAudioChannels;
	int32				fAudioBitsPerSample;

	// Media Kit
	media_node			fMediaNode;
	int32				fMediaNodeID;
	dormant_node_info	fDormantInfo;

	// Capture state
	bool				fIsCapturing;
	thread_id			fCaptureThread;
	BLooper*			fTarget;

	// Statistics
	uint32				fFramesCaptured;
	uint32				fFramesDropped;
	float				fCurrentFPS;
	bigtime_t			fLastFrameTime;
};

#endif // WEBCAM_DEVICE_H
