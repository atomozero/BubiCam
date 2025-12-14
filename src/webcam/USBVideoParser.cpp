/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * USB Video Class Descriptor Parser
 */

#include "USBVideoParser.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "USBParser"
#include "ErrorUtils.h"

#include <OS.h>
#include <USBKit.h>
#include <stdio.h>
#include <string.h>


// GUID for YUY2 format
static const uint8 kYUY2Guid[] = {
	0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

// GUID for NV12 format
static const uint8 kNV12Guid[] = {
	0x4e, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};


const char*
GetFormatTypeName(uint8 formatType)
{
	switch (formatType) {
		case 0: return "Uncompressed";
		case 1: return "MJPEG";
		default: return "Unknown";
	}
}


// Helper class to enumerate USB devices
class USBVideoRoster : public BUSBRoster {
public:
	USBVideoRoster(USBVideoInfo* info, uint16 targetVendor = 0, uint16 targetProduct = 0)
		: fInfo(info), fTargetVendor(targetVendor), fTargetProduct(targetProduct),
		  fFoundDevice(false) {}

	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		// Skip if we already found a video device (avoid duplicate enumeration)
		if (fFoundDevice)
			return B_OK;

		// Check if this is a video class device
		bool isVideoDevice = (device->Class() == USB_VIDEO_DEVICE_CLASS);

		// Also check interfaces for video class
		if (!isVideoDevice) {
			for (uint32 c = 0; c < device->CountConfigurations(); c++) {
				const BUSBConfiguration* config = device->ConfigurationAt(c);
				if (config == NULL)
					continue;
				for (uint32 i = 0; i < config->CountInterfaces(); i++) {
					const BUSBInterface* intf = config->InterfaceAt(i);
					if (intf != NULL && intf->Class() == USB_VIDEO_DEVICE_CLASS) {
						isVideoDevice = true;
						break;
					}
				}
				if (isVideoDevice)
					break;
			}
		}

		if (!isVideoDevice)
			return B_OK;

		// If specific vendor/product requested, check it
		if (fTargetVendor != 0 && device->VendorID() != fTargetVendor)
			return B_OK;
		if (fTargetProduct != 0 && device->ProductID() != fTargetProduct)
			return B_OK;

		// Found a video device - parse it
		LOG_DEBUG("Found USB video device: %04X:%04X %s",
			device->VendorID(), device->ProductID(), device->ProductString());
		_ParseDevice(device);
		fFoundDevice = true;

		return B_OK;
	}

	virtual void DeviceRemoved(BUSBDevice* device) {}

	bool FoundDevice() const { return fFoundDevice; }

private:
	void _ParseDevice(BUSBDevice* device)
	{
		fInfo->found = true;
		fInfo->vendorID = device->VendorID();
		fInfo->productID = device->ProductID();
		fInfo->vendorName = device->ManufacturerString();
		fInfo->productName = device->ProductString();
		fInfo->serialNumber = device->SerialNumberString();

		// Build concise diagnostic info (stored but not printed here)
		BString diag;
		diag << "USB: " << fInfo->vendorName << " " << fInfo->productName;
		diag << " (" << BString().SetToFormat("%04X:%04X", fInfo->vendorID, fInfo->productID) << ")";

		// Iterate through configurations and interfaces to find video descriptors
		for (uint32 c = 0; c < device->CountConfigurations(); c++) {
			const BUSBConfiguration* config = device->ConfigurationAt(c);
			if (config == NULL)
				continue;

			for (uint32 i = 0; i < config->CountInterfaces(); i++) {
				const BUSBInterface* intf = config->InterfaceAt(i);
				if (intf == NULL)
					continue;

				// Check all alternates for video class interfaces
				for (uint32 a = 0; a < intf->CountAlternates() || a == 0; a++) {
					const BUSBInterface* alt = (a == 0) ? intf : intf->AlternateAt(a);
					if (alt == NULL)
						continue;

					if (alt->Class() == USB_VIDEO_DEVICE_CLASS) {
						if (alt->Subclass() == USB_VIDEO_INTERFACE_VC_SUBCLASS) {
							_ParseVideoControl(alt, diag);
						} else if (alt->Subclass() == USB_VIDEO_INTERFACE_VS_SUBCLASS) {
							_ParseVideoStreaming(alt, diag);
						}
					}
				}
			}
		}

		fInfo->diagnosticInfo = diag;
	}

	void _ParseVideoControl(const BUSBInterface* intf, BString& diag)
	{
		static const size_t kBufferSize = 1024;
		uint8 buffer[kBufferSize];
		usb_descriptor* generic = (usb_descriptor*)buffer;

		for (uint32 k = 0; intf->OtherDescriptorAt(k, generic, sizeof(buffer)) == B_OK; k++) {
			uint8 descType = generic->generic.descriptor_type;
			uint8 descLen = generic->generic.length;

			// Validate descriptor length
			if (descLen > kBufferSize || descLen < 2)
				continue;

			// Class-specific interface descriptor - VC_HEADER
			if (descType == 0x24 && descLen >= 12 && buffer[2] == 0x01) {
				fInfo->uvcVersion = buffer[3] | (buffer[4] << 8);
				fInfo->clockFrequency = buffer[7] | (buffer[8] << 8) |
					(buffer[9] << 16) | (buffer[10] << 24);
				diag << ", UVC " << (fInfo->uvcVersion >> 8) << "."
					<< (fInfo->uvcVersion & 0xFF);
			}
		}
	}

	void _ParseVideoStreaming(const BUSBInterface* intf, BString& diag)
	{
		static const size_t kBufferSize = 1024;
		uint8 buffer[kBufferSize];
		usb_descriptor* generic = (usb_descriptor*)buffer;

		USBVideoFormat* currentFormat = NULL;

		for (uint32 k = 0; intf->OtherDescriptorAt(k, generic, sizeof(buffer)) == B_OK; k++) {
			uint8 descType = generic->generic.descriptor_type;
			uint8 descLen = generic->generic.length;

			// Validate descriptor length
			if (descLen > kBufferSize || descLen < 2)
				continue;

			// Class-specific interface descriptor (type 0x24 = CS_INTERFACE)
			if (descType == 0x24 && descLen >= 3) {
				uint8 subtype = buffer[2];

				switch (subtype) {
					case VS_FORMAT_UNCOMPRESSED:
						if (descLen >= 27) {
							currentFormat = new USBVideoFormat();
							currentFormat->formatIndex = buffer[3];
							currentFormat->formatType = 0;
							currentFormat->numFrames = buffer[4];
							currentFormat->bitsPerPixel = buffer[21];

							// Decode GUID for format name
							if (memcmp(&buffer[5], kYUY2Guid, 16) == 0)
								currentFormat->formatName = "YUY2";
							else if (memcmp(&buffer[5], kNV12Guid, 16) == 0)
								currentFormat->formatName = "NV12";
							else
								currentFormat->formatName = "Uncompressed";

							fInfo->formats.AddItem(currentFormat);
						}
						break;

					case VS_FORMAT_MJPEG:
						if (descLen >= 11) {
							currentFormat = new USBVideoFormat();
							currentFormat->formatIndex = buffer[3];
							currentFormat->formatType = 1;
							currentFormat->formatName = "MJPEG";
							currentFormat->numFrames = buffer[4];
							currentFormat->bitsPerPixel = 0;
							fInfo->formats.AddItem(currentFormat);
						}
						break;

					case VS_FRAME_UNCOMPRESSED:
					case VS_FRAME_MJPEG:
					{
						if (descLen >= 26 && currentFormat != NULL) {
							USBVideoFrame* frame = new USBVideoFrame();
							if (frame == NULL)
								break;

							frame->formatType = (subtype == VS_FRAME_MJPEG) ? 1 : 0;
							frame->width = buffer[5] | (buffer[6] << 8);
							frame->height = buffer[7] | (buffer[8] << 8);

							// Sanity check dimensions
							if (frame->width == 0 || frame->height == 0 ||
								frame->width > 8192 || frame->height > 8192) {
								delete frame;
								break;
							}

							frame->minBitRate = buffer[9] | (buffer[10] << 8) |
								(buffer[11] << 16) | (buffer[12] << 24);
							frame->maxBitRate = buffer[13] | (buffer[14] << 8) |
								(buffer[15] << 16) | (buffer[16] << 24);
							frame->frameBufferSize = buffer[17] | (buffer[18] << 8) |
								(buffer[19] << 16) | (buffer[20] << 24);

							// Default frame interval (in 100ns units)
							uint32 defaultInterval = buffer[21] | (buffer[22] << 8) |
								(buffer[23] << 16) | (buffer[24] << 24);
							if (defaultInterval > 0)
								frame->defaultFrameRate = 10000000.0f / defaultInterval;

							// Parse discrete frame intervals
							uint8 intervalType = buffer[25];
							size_t requiredLen = 26 + (size_t)intervalType * 4;
							if (intervalType > 0 && intervalType <= 30 &&
								descLen >= requiredLen && requiredLen <= kBufferSize) {
								for (uint8 j = 0; j < intervalType; j++) {
									size_t offset = 26 + (size_t)j * 4;
									if (offset + 3 >= kBufferSize)
										break;
									uint32 interval = buffer[offset] | (buffer[offset + 1] << 8) |
										(buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
									if (interval > 0) {
										float fpsValue = 10000000.0f / interval;
										frame->frameRates.AddItem(new FrameRate(fpsValue));
									}
								}
							}

							currentFormat->frames.AddItem(frame);
						}
						break;
					}

					default:
						break;
				}
			}
		}
	}

	USBVideoInfo*	fInfo;
	uint16			fTargetVendor;
	uint16			fTargetProduct;
	bool			fFoundDevice;
};


bool
ParseUSBVideoDevice(USBVideoInfo* info, uint16 vendorID, uint16 productID)
{
	if (info == NULL)
		return false;

	USBVideoRoster roster(info, vendorID, productID);
	roster.Start();

	// Give it some time to enumerate
	snooze(100000);	// 100ms

	roster.Stop();

	return info->found;
}


bool
FindAndParseUSBVideoDevice(USBVideoInfo* info)
{
	LOG_DEBUG("Starting USB device enumeration...");
	bool result = ParseUSBVideoDevice(info, 0, 0);

	if (info && info->found) {
		LOG_INFO("USB device: %s %s (%04X:%04X)",
			info->vendorName.String(), info->productName.String(),
			info->vendorID, info->productID);

		if (info->formats.CountItems() > 0) {
			LOG_INFO("Supported formats: %d", (int)info->formats.CountItems());
			for (int32 f = 0; f < info->formats.CountItems(); f++) {
				USBVideoFormat* fmt = info->formats.ItemAt(f);
				if (fmt == NULL) continue;
				BString resolutions;
				for (int32 fr = 0; fr < fmt->frames.CountItems(); fr++) {
					USBVideoFrame* frame = fmt->frames.ItemAt(fr);
					if (frame == NULL) continue;
					if (resolutions.Length() > 0) resolutions << ", ";
					resolutions << frame->width << "x" << frame->height;
				}
				LOG_DETAIL("  %s: %s", fmt->formatName.String(), resolutions.String());
			}
		}
	} else {
		LOG_DEBUG("No USB video device found");
	}

	return result;
}
