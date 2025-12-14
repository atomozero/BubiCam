/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "ExportUtils.h"
#include "WebcamDevice.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "ExportUtils"
#include "ErrorUtils.h"

#include <File.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <TranslatorRoster.h>
#include <BitmapStream.h>
#include <TranslationUtils.h>
#include <NodeInfo.h>

#include <stdio.h>
#include <time.h>


status_t
ExportUtils::SaveScreenshot(BBitmap* bitmap, const char* path, uint32 format)
{
	if (bitmap == NULL || path == NULL)
		return B_BAD_VALUE;

	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	// Use translation kit to save
	BBitmapStream stream(bitmap);

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL)
		return B_ERROR;

	status_t status = roster->Translate(&stream, NULL, NULL, &file, format);

	// Detach bitmap so it's not deleted
	BBitmap* detached;
	stream.DetachBitmap(&detached);

	if (status == B_OK) {
		// Set file type
		BNodeInfo nodeInfo(&file);
		if (format == 'PNG ')
			nodeInfo.SetType("image/png");
		else if (format == 'JPEG')
			nodeInfo.SetType("image/jpeg");
		else if (format == 'BMP ')
			nodeInfo.SetType("image/bmp");
	}

	return status;
}


BString
ExportUtils::GenerateScreenshotFilename()
{
	BString filename("BubiCam_");
	filename << GetTimestamp();
	filename << ".png";
	return filename;
}


BPath
ExportUtils::GetScreenshotDirectory()
{
	BPath path;

	// Try Desktop first
	if (find_directory(B_DESKTOP_DIRECTORY, &path) == B_OK)
		return path;

	// Fall back to home
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK)
		return path;

	// Last resort
	path.SetTo("/boot/home");
	return path;
}


status_t
ExportUtils::ExportDriverInfo(WebcamDevice* device, const char* path)
{
	BString extension(path);
	int32 dotPos = extension.FindLast('.');

	if (dotPos >= 0) {
		extension.Remove(0, dotPos + 1);
		extension.ToLower();

		if (extension == "json")
			return ExportDriverInfoAsJSON(device, path);
	}

	return ExportDriverInfoAsText(device, path);
}


status_t
ExportUtils::ExportDriverInfoAsJSON(WebcamDevice* device, const char* path)
{
	if (device == NULL)
		return B_BAD_VALUE;

	BString json;
	json << "{\n";

	// Device info
	json << "  \"device\": {\n";
	json << "    \"name\": \"" << device->Name() << "\",\n";
	json << "    \"path\": \"" << device->DevicePath() << "\"\n";
	json << "  },\n";

	// USB info
	json << "  \"usb\": {\n";
	json << "    \"vendor_id\": \"0x" << BString().SetToFormat("%04X", device->VendorID()) << "\",\n";
	json << "    \"product_id\": \"0x" << BString().SetToFormat("%04X", device->ProductID()) << "\",\n";
	json << "    \"vendor_name\": \"" << device->VendorName() << "\",\n";
	json << "    \"product_name\": \"" << device->ProductName() << "\",\n";
	json << "    \"serial_number\": \"" << device->SerialNumber() << "\",\n";
	json << "    \"usb_version\": \"" << device->USBVersion() << "\",\n";
	json << "    \"device_class\": " << (int)device->DeviceClass() << ",\n";
	json << "    \"device_subclass\": " << (int)device->DeviceSubclass() << ",\n";
	json << "    \"device_protocol\": " << (int)device->DeviceProtocol() << "\n";
	json << "  },\n";

	// Driver info
	json << "  \"driver\": {\n";
	json << "    \"name\": \"" << device->DriverName() << "\",\n";
	json << "    \"path\": \"" << device->DriverPath() << "\",\n";
	json << "    \"version\": \"" << device->DriverVersion() << "\"\n";
	json << "  },\n";

	// Video capabilities
	json << "  \"video\": {\n";
	json << "    \"supported\": " << (device->SupportsVideo() ? "true" : "false") << ",\n";
	json << "    \"formats\": [\n";

	const BObjectList<VideoFormat>& formats = device->SupportedFormats();
	for (int32 i = 0; i < formats.CountItems(); i++) {
		VideoFormat* fmt = formats.ItemAt(i);
		if (fmt != NULL) {
			json << "      {\n";
			json << "        \"width\": " << fmt->width << ",\n";
			json << "        \"height\": " << fmt->height << ",\n";
			json << "        \"frame_rate\": " << fmt->frameRate << ",\n";
			json << "        \"color_space\": \"" << fmt->colorSpace << "\"\n";
			json << "      }";
			if (i < formats.CountItems() - 1)
				json << ",";
			json << "\n";
		}
	}
	json << "    ]\n";
	json << "  },\n";

	// Audio capabilities
	json << "  \"audio\": {\n";
	json << "    \"supported\": " << (device->SupportsAudio() ? "true" : "false");
	if (device->SupportsAudio()) {
		json << ",\n";
		json << "    \"sample_rate\": " << device->AudioSampleRate() << ",\n";
		json << "    \"channels\": " << device->AudioChannels() << ",\n";
		json << "    \"bits_per_sample\": " << device->AudioBitsPerSample() << "\n";
	} else {
		json << "\n";
	}
	json << "  },\n";

	// Media Kit info
	json << "  \"media_kit\": {\n";
	json << "    \"registered\": " << (device->IsRegisteredWithMediaKit() ? "true" : "false") << ",\n";
	json << "    \"node_id\": " << device->MediaNodeID() << "\n";
	json << "  },\n";

	// Export metadata
	json << "  \"export\": {\n";
	json << "    \"timestamp\": \"" << GetTimestamp() << "\",\n";
	json << "    \"application\": \"BubiCam\",\n";
	json << "    \"version\": \"1.0.0\"\n";
	json << "  }\n";

	json << "}\n";

	return _WriteToFile(path, json);
}


status_t
ExportUtils::ExportDriverInfoAsText(WebcamDevice* device, const char* path)
{
	if (device == NULL)
		return B_BAD_VALUE;

	BString text;
	text << "=== BubiCam Driver Information Export ===\n";
	text << "Export Date: " << GetTimestamp() << "\n\n";

	text << "--- DEVICE INFORMATION ---\n";
	text << "Name: " << device->Name() << "\n";
	text << "Device Path: " << device->DevicePath() << "\n\n";

	text << "--- USB INFORMATION ---\n";
	text << "Vendor ID: 0x" << BString().SetToFormat("%04X", device->VendorID()) << "\n";
	text << "Product ID: 0x" << BString().SetToFormat("%04X", device->ProductID()) << "\n";
	text << "Vendor Name: " << device->VendorName() << "\n";
	text << "Product Name: " << device->ProductName() << "\n";
	text << "Serial Number: " << device->SerialNumber() << "\n";
	text << "USB Version: " << device->USBVersion() << "\n";
	text << "Device Class: " << (int)device->DeviceClass() << "\n";
	text << "Device Subclass: " << (int)device->DeviceSubclass() << "\n";
	text << "Device Protocol: " << (int)device->DeviceProtocol() << "\n\n";

	text << "--- DRIVER INFORMATION ---\n";
	text << "Driver Name: " << device->DriverName() << "\n";
	text << "Driver Path: " << device->DriverPath() << "\n";
	text << "Driver Version: " << device->DriverVersion() << "\n\n";

	text << "--- VIDEO CAPABILITIES ---\n";
	text << "Video Supported: " << (device->SupportsVideo() ? "Yes" : "No") << "\n";

	const BObjectList<VideoFormat>& formats = device->SupportedFormats();
	if (formats.CountItems() > 0) {
		text << "Supported Formats:\n";
		for (int32 i = 0; i < formats.CountItems(); i++) {
			VideoFormat* fmt = formats.ItemAt(i);
			if (fmt != NULL) {
				text << "  - " << fmt->width << "x" << fmt->height;
				text << " @ " << fmt->frameRate << " fps";
				text << " (" << fmt->colorSpace << ")\n";
			}
		}
	}
	text << "\n";

	text << "--- AUDIO CAPABILITIES ---\n";
	text << "Audio Supported: " << (device->SupportsAudio() ? "Yes" : "No") << "\n";
	if (device->SupportsAudio()) {
		text << "Sample Rate: " << device->AudioSampleRate() << " Hz\n";
		text << "Channels: " << device->AudioChannels() << "\n";
		text << "Bits per Sample: " << device->AudioBitsPerSample() << "\n";
	}
	text << "\n";

	text << "--- MEDIA KIT STATUS ---\n";
	text << "Registered: " << (device->IsRegisteredWithMediaKit() ? "Yes" : "No") << "\n";
	text << "Node ID: " << device->MediaNodeID() << "\n\n";

	text << "=== End of Report ===\n";

	return _WriteToFile(path, text);
}


BString
ExportUtils::GetTimestamp()
{
	time_t now = time(NULL);
	struct tm* tm = localtime(&now);

	BString timestamp;
	timestamp.SetToFormat("%04d-%02d-%02d_%02d-%02d-%02d",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return timestamp;
}


status_t
ExportUtils::_WriteToFile(const char* path, const BString& content)
{
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	ssize_t written = file.Write(content.String(), content.Length());
	if (written < 0)
		return (status_t)written;

	if (written != content.Length())
		return B_IO_ERROR;

	return B_OK;
}
