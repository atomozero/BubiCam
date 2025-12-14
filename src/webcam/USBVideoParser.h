/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * USB Video Class Descriptor Parser
 */

#ifndef USB_VIDEO_PARSER_H
#define USB_VIDEO_PARSER_H

#include <SupportDefs.h>
#include <String.h>
#include <ObjectList.h>

// USB Video Class constants
#define USB_VIDEO_DEVICE_CLASS			0x0E
#define USB_VIDEO_INTERFACE_VC_SUBCLASS	0x01
#define USB_VIDEO_INTERFACE_VS_SUBCLASS	0x02

// Video Streaming Interface descriptor subtypes
#define VS_UNDEFINED			0x00
#define VS_INPUT_HEADER			0x01
#define VS_OUTPUT_HEADER		0x02
#define VS_STILL_IMAGE_FRAME	0x03
#define VS_FORMAT_UNCOMPRESSED	0x04
#define VS_FRAME_UNCOMPRESSED	0x05
#define VS_FORMAT_MJPEG			0x06
#define VS_FRAME_MJPEG			0x07
#define VS_FORMAT_DV			0x0C
#define VS_COLORFORMAT			0x0D

// Simple wrapper for frame rate values (for use with BObjectList)
struct FrameRate {
	float value;
	FrameRate(float v = 0.0f) : value(v) {}
};

// Parsed USB video frame descriptor
struct USBVideoFrame {
	uint8		formatType;			// 0=Uncompressed, 1=MJPEG, 2=Other
	uint16		width;
	uint16		height;
	uint32		minBitRate;
	uint32		maxBitRate;
	uint32		frameBufferSize;
	float		defaultFrameRate;
	BObjectList<FrameRate, true> frameRates;	// Supported frame rates (owned)

	USBVideoFrame()
		: formatType(0), width(0), height(0),
		  minBitRate(0), maxBitRate(0), frameBufferSize(0),
		  defaultFrameRate(0), frameRates(10) {}
};

// Parsed USB video format descriptor
struct USBVideoFormat {
	uint8		formatIndex;
	uint8		formatType;			// 0=Uncompressed (YUY2, etc), 1=MJPEG
	BString		formatName;			// "YUY2", "MJPEG", etc
	uint8		bitsPerPixel;
	uint8		numFrames;
	BObjectList<USBVideoFrame, true> frames;	// Owned USBVideoFrame objects

	USBVideoFormat()
		: formatIndex(0), formatType(0), bitsPerPixel(0), numFrames(0),
		  frames(10) {}
};

// Complete parsed USB video device info
struct USBVideoInfo {
	bool		found;
	uint16		vendorID;
	uint16		productID;
	BString		vendorName;
	BString		productName;
	BString		serialNumber;
	uint16		uvcVersion;
	uint32		clockFrequency;

	BObjectList<USBVideoFormat, true> formats;	// Owned USBVideoFormat objects

	// Diagnostic info
	BString		diagnosticInfo;		// Additional debug info

	USBVideoInfo()
		: found(false), vendorID(0), productID(0), uvcVersion(0),
		  clockFrequency(0), formats(5) {}
};

// Parse USB video descriptors for a device matching vendor/product ID
// Fills the provided USBVideoInfo structure, returns true if device found
bool ParseUSBVideoDevice(USBVideoInfo* info, uint16 vendorID = 0, uint16 productID = 0);

// Find any USB video class device and parse its descriptors
// Fills the provided USBVideoInfo structure, returns true if device found
bool FindAndParseUSBVideoDevice(USBVideoInfo* info);

// Get format type name
const char* GetFormatTypeName(uint8 formatType);

#endif // USB_VIDEO_PARSER_H
