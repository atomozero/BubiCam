/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * USB Video Class Descriptor Parser
 */

#include "USBVideoParser.h"

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
		fprintf(stderr, "USB DeviceAdded: VID=%04X PID=%04X Class=%02X '%s' '%s'\n",
			device->VendorID(), device->ProductID(), device->Class(),
			device->ManufacturerString(), device->ProductString());

		if (fFoundDevice)
			return B_OK;

		// Check if this is a video class device
		bool isVideoDevice = (device->Class() == USB_VIDEO_DEVICE_CLASS);
		fprintf(stderr, "  isVideoDevice (class check): %d (0x%02X == 0x%02X)\n",
			isVideoDevice, device->Class(), USB_VIDEO_DEVICE_CLASS);

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

		fprintf(stderr, "  isVideoDevice (after interface check): %d\n", isVideoDevice);

		if (!isVideoDevice)
			return B_OK;

		// If specific vendor/product requested, check it
		if (fTargetVendor != 0 && device->VendorID() != fTargetVendor)
			return B_OK;
		if (fTargetProduct != 0 && device->ProductID() != fTargetProduct)
			return B_OK;

		// Found a video device - parse it
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

		BString diag;
		diag << "USB Device: " << fInfo->vendorName << " " << fInfo->productName << "\n";
		diag << "VID:PID = " << BString().SetToFormat("0x%04X:0x%04X", fInfo->vendorID, fInfo->productID) << "\n";

		// Iterate through configurations and interfaces to find video descriptors
		for (uint32 c = 0; c < device->CountConfigurations(); c++) {
			const BUSBConfiguration* config = device->ConfigurationAt(c);
			if (config == NULL)
				continue;

			diag << "Configuration " << c << ": " << config->CountInterfaces() << " interfaces\n";

			for (uint32 i = 0; i < config->CountInterfaces(); i++) {
				const BUSBInterface* intf = config->InterfaceAt(i);
				if (intf == NULL)
					continue;

				diag << "    Interface " << i << ": class=0x" << BString().SetToFormat("%02x", intf->Class())
					<< ", subclass=0x" << BString().SetToFormat("%02x", intf->Subclass())
					<< ", #alts=" << intf->CountAlternates() << "\n";

				// Check all alternates
				for (uint32 a = 0; a < intf->CountAlternates() || a == 0; a++) {
					const BUSBInterface* alt = (a == 0) ? intf : intf->AlternateAt(a);
					if (alt == NULL) {
						diag << "      Alt " << a << ": NULL\n";
						continue;
					}

					diag << "      Alt " << a << ": class=0x" << BString().SetToFormat("%02x", alt->Class())
						<< ", subclass=0x" << BString().SetToFormat("%02x", alt->Subclass()) << "\n";

					if (alt->Class() == USB_VIDEO_DEVICE_CLASS) {
						if (alt->Subclass() == USB_VIDEO_INTERFACE_VC_SUBCLASS) {
							diag << "        -> Video Control\n";
							_ParseVideoControl(alt, diag);
						} else if (alt->Subclass() == USB_VIDEO_INTERFACE_VS_SUBCLASS) {
							diag << "        -> Video Streaming\n";
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
		uint8 buffer[1024];
		usb_descriptor* generic = (usb_descriptor*)buffer;

		for (uint32 k = 0; intf->OtherDescriptorAt(k, generic, sizeof(buffer)) == B_OK; k++) {
			uint8 descType = generic->generic.descriptor_type;
			uint8 descLen = generic->generic.length;

			// Class-specific interface descriptor
			if (descType == 0x24 && descLen >= 3) {
				uint8 subtype = buffer[2];
				switch (subtype) {
					case 0x01:	// VC_HEADER
						if (descLen >= 12) {
							fInfo->uvcVersion = buffer[3] | (buffer[4] << 8);
							fInfo->clockFrequency = buffer[7] | (buffer[8] << 8) |
								(buffer[9] << 16) | (buffer[10] << 24);
							diag << "    VC_HEADER: UVC " << (fInfo->uvcVersion >> 8) << "."
								<< (fInfo->uvcVersion & 0xFF) << ", Clock "
								<< (fInfo->clockFrequency / 1000000.0) << " MHz\n";
						}
						break;
					case 0x02:	// VC_INPUT_TERMINAL
						diag << "    VC_INPUT_TERMINAL\n";
						break;
					case 0x03:	// VC_OUTPUT_TERMINAL
						diag << "    VC_OUTPUT_TERMINAL\n";
						break;
					case 0x05:	// VC_PROCESSING_UNIT
						diag << "    VC_PROCESSING_UNIT\n";
						break;
				}
			}
		}
	}

	void _ParseVideoStreaming(const BUSBInterface* intf, BString& diag)
	{
		uint8 buffer[1024];
		usb_descriptor* generic = (usb_descriptor*)buffer;

		USBVideoFormat* currentFormat = NULL;

		diag << "      Checking OtherDescriptorAt (interface has " << intf->CountEndpoints() << " endpoints)...\n";

		// Try getting raw descriptor data
		const usb_interface_descriptor* rawDesc = intf->Descriptor();
		if (rawDesc != NULL) {
			diag << "      Interface descriptor: class=0x" << BString().SetToFormat("%02x", rawDesc->interface_class)
				<< ", subclass=0x" << BString().SetToFormat("%02x", rawDesc->interface_subclass)
				<< ", length=" << (int)rawDesc->length << "\n";
		} else {
			diag << "      Interface descriptor: NULL\n";
		}

		int descCount = 0;
		for (uint32 k = 0; intf->OtherDescriptorAt(k, generic, sizeof(buffer)) == B_OK; k++) {
			descCount++;
			diag << "      Descriptor " << k << ": type=0x" << BString().SetToFormat("%02x", generic->generic.descriptor_type)
				<< ", len=" << (int)generic->generic.length << "\n";
			uint8 descType = generic->generic.descriptor_type;
			uint8 descLen = generic->generic.length;

			// Class-specific interface descriptor (type 0x24 = CS_INTERFACE)
			if (descType == 0x24 && descLen >= 3) {
				uint8 subtype = buffer[2];

				switch (subtype) {
					case VS_INPUT_HEADER:
						if (descLen >= 13) {
							uint8 numFormats = buffer[3];
							diag << "    VS_INPUT_HEADER: " << (int)numFormats << " format(s)\n";
						}
						break;

					case VS_FORMAT_UNCOMPRESSED:
						if (descLen >= 27) {
							currentFormat = new USBVideoFormat();
							currentFormat->formatIndex = buffer[3];
							currentFormat->formatType = 0;  // Uncompressed
							currentFormat->numFrames = buffer[4];
							currentFormat->bitsPerPixel = buffer[21];

							// Decode GUID for format name
							if (memcmp(&buffer[5], kYUY2Guid, 16) == 0)
								currentFormat->formatName = "YUY2";
							else if (memcmp(&buffer[5], kNV12Guid, 16) == 0)
								currentFormat->formatName = "NV12";
							else
								currentFormat->formatName = "Uncompressed";

							fInfo->formats.AddItem((void*)currentFormat);
							diag << "    VS_FORMAT_UNCOMPRESSED: " << currentFormat->formatName
								<< ", " << (int)currentFormat->numFrames << " frame(s), "
								<< (int)currentFormat->bitsPerPixel << " bpp\n";
						}
						break;

					case VS_FORMAT_MJPEG:
						if (descLen >= 11) {
							currentFormat = new USBVideoFormat();
							currentFormat->formatIndex = buffer[3];
							currentFormat->formatType = 1;  // MJPEG
							currentFormat->formatName = "MJPEG";
							currentFormat->numFrames = buffer[4];
							currentFormat->bitsPerPixel = 0;

							fInfo->formats.AddItem((void*)currentFormat);
							diag << "    VS_FORMAT_MJPEG: " << (int)currentFormat->numFrames
								<< " frame(s)\n";
						}
						break;

					case VS_FRAME_UNCOMPRESSED:
					case VS_FRAME_MJPEG:
					{
						if (descLen >= 26 && currentFormat != NULL) {
							USBVideoFrame* frame = new USBVideoFrame();
							frame->formatType = (subtype == VS_FRAME_MJPEG) ? 1 : 0;
							frame->width = buffer[5] | (buffer[6] << 8);
							frame->height = buffer[7] | (buffer[8] << 8);
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
							if (intervalType > 0 && descLen >= 26 + intervalType * 4) {
								for (uint8 j = 0; j < intervalType; j++) {
									uint32 interval = buffer[26 + j*4] | (buffer[27 + j*4] << 8) |
										(buffer[28 + j*4] << 16) | (buffer[29 + j*4] << 24);
									if (interval > 0) {
										float* fps = new float;
										*fps = 10000000.0f / interval;
										frame->frameRates.AddItem((void*)fps);
									}
								}
							}

							currentFormat->frames.AddItem((void*)frame);

							const char* frameType = (subtype == VS_FRAME_MJPEG) ?
								"VS_FRAME_MJPEG" : "VS_FRAME_UNCOMPRESSED";
							diag << "    " << frameType << ": " << frame->width << "x"
								<< frame->height << " @ " << frame->defaultFrameRate << " fps\n";
						}
						break;
					}

					case VS_COLORFORMAT:
						diag << "    VS_COLORFORMAT\n";
						break;

					default:
						diag << "    Unknown VS descriptor subtype: " << (int)subtype << "\n";
						break;
				}
			}
		}
		diag << "      Total descriptors from OtherDescriptorAt: " << descCount << "\n";
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
	fprintf(stderr, "FindAndParseUSBVideoDevice: Starting USB enumeration...\n");
	bool result = ParseUSBVideoDevice(info, 0, 0);
	fprintf(stderr, "FindAndParseUSBVideoDevice: result=%d, found=%d, formats=%d\n",
		result, info ? info->found : -1,
		info ? (int)info->formats.CountItems() : -1);
	if (info && info->diagnosticInfo.Length() > 0) {
		fprintf(stderr, "USB Diagnostic:\n%s\n", info->diagnosticInfo.String());
	}
	return result;
}
