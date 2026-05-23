/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "DriverInfoView.h"
#include "WebcamDevice.h"
#include "USBVideoParser.h"

#include <stdio.h>


DriverInfoView::DriverInfoView(const char* name)
	:
	BTextView(name, B_WILL_DRAW)
{
	SetStylable(true);
	MakeEditable(false);
	MakeSelectable(true);
	SetWordWrap(true);

	fSectionColor = make_color(70, 130, 180);  // Steel blue
	fLabelColor = make_color(100, 100, 100);   // Gray
	fValueColor = make_color(0, 0, 0);         // Black
	fWarningColor = make_color(204, 102, 0);   // Orange
	fErrorColor = make_color(180, 0, 0);       // Red
}


DriverInfoView::~DriverInfoView()
{
}


void
DriverInfoView::AttachedToWindow()
{
	BTextView::AttachedToWindow();

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	// Set up fonts
	fNormalFont = *be_plain_font;
	fNormalFont.SetSize(11);

	fBoldFont = *be_bold_font;
	fBoldFont.SetSize(12);
}


void
DriverInfoView::SetDevice(WebcamDevice* device, bool isCapturing)
{
	SetText("");

	if (device == NULL) {
		SetText("No device selected");
		return;
	}

	// Device Information Section
	_AppendSection("DEVICE INFORMATION");
	_AppendField("Name", device->Name());
	_AppendField("Device Path", device->DevicePath());
	_AppendNewLine();

	// Media Kit Status Section
	_AppendSection("MEDIA KIT STATUS");
	_AppendField("Registered", device->IsRegisteredWithMediaKit() ? "Yes" : "No");

	const dormant_node_info& dormant = device->DormantInfo();
	BString addonInfo;
	addonInfo.SetToFormat("%d (flavor: %d)", dormant.addon, dormant.flavor_id);
	_AppendField("Add-on ID", addonInfo.String());

	if (device->IsNodeInstantiated()) {
		_AppendField("Node Status", "Instantiated successfully");
		_AppendField("Node ID", device->MediaNodeID());
	} else {
		status_t err = device->InstantiateError();
		BString errStr;
		switch (err) {
			case B_TIMED_OUT:
				errStr = "TIMEOUT - Driver not responding";
				break;
			case B_BUSY:
				errStr = "BUSY - Device in use";
				break;
			case B_NO_MEMORY:
				errStr = "NO MEMORY";
				break;
			case B_MEDIA_SYSTEM_FAILURE:
				errStr = "MEDIA SYSTEM FAILURE";
				break;
			default:
				errStr.SetToFormat("Error 0x%08x (%d)", err, err);
		}
		_AppendField("Node Status", "FAILED TO INSTANTIATE");
		_AppendField("Error", errStr.String());
	}
	_AppendNewLine();

	// USB Information Section
	_AppendSection("USB INFORMATION");
	_AppendField("Vendor ID", device->VendorID(), true);
	_AppendField("Product ID", device->ProductID(), true);
	_AppendField("Vendor Name", device->VendorName());
	_AppendField("Product Name", device->ProductName());
	_AppendField("Serial Number", device->SerialNumber());
	_AppendField("USB Version", device->USBVersion());
	_AppendField("Device Class", device->DeviceClass());
	_AppendField("Device Subclass", device->DeviceSubclass());
	_AppendField("Device Protocol", device->DeviceProtocol());
	_AppendNewLine();

	// Driver Information Section
	_AppendSection("DRIVER INFORMATION");
	_AppendField("Driver Name", device->DriverName());
	_AppendField("Driver Path", device->DriverPath());
	_AppendField("Driver Version", device->DriverVersion());

	// Check if this is a USB webcam driver that may have shutdown issues
	BString driverName(device->DriverName());
	driverName.ToLower();
	if (driverName.FindFirst("uvc") >= 0 || driverName.FindFirst("webcam") >= 0 ||
		driverName.FindFirst("usb") >= 0) {
		_AppendNewLine();
		_AppendWarning("Note: This driver may not properly release its BUSBRoster");
		_AppendWarning("during shutdown. BubiCam handles this gracefully, but if");
		_AppendWarning("you experience crashes on exit, please report the issue");
		_AppendWarning("to dev.haiku-os.org with VID:PID of your webcam.");
	}
	_AppendNewLine();

	// Video Capabilities Section
	_AppendSection("VIDEO CAPABILITIES");
	_AppendField("Video Supported", device->SupportsVideo());

	const BObjectList<VideoFormat>& formats = device->SupportedFormats();
	if (formats.CountItems() > 0) {
		BString formatsStr;
		for (int32 i = 0; i < formats.CountItems(); i++) {
			VideoFormat* format = formats.ItemAt(i);
			if (format != NULL) {
				BString formatLine;
				formatLine.SetToFormat("  %dx%d @ %.1f fps (%s)",
					format->width, format->height, format->frameRate,
					format->colorSpace);
				if (i > 0)
					formatsStr << "\n";
				formatsStr << formatLine;
			}
		}
		_AppendField("Supported Formats", formatsStr.String());
	} else {
		_AppendField("Supported Formats", "Unknown");
	}

	VideoFormat currentFormat = device->CurrentFormat();
	if (currentFormat.width > 0) {
		BString currentStr;
		currentStr.SetToFormat("%dx%d @ %.1f fps (%s)",
			currentFormat.width, currentFormat.height,
			currentFormat.frameRate, currentFormat.colorSpace);
		_AppendField("Current Format", currentStr.String());

		// Detailed format info
		int32 bpp = 0;
		if (strcmp(currentFormat.colorSpace, "YUY2") == 0 ||
			strcmp(currentFormat.colorSpace, "UYVY") == 0)
			bpp = 16;
		else if (strcmp(currentFormat.colorSpace, "MJPEG") == 0)
			bpp = 12;  // typical compressed
		else if (strcmp(currentFormat.colorSpace, "RGB32") == 0 ||
				 strcmp(currentFormat.colorSpace, "BGRA") == 0)
			bpp = 32;
		else if (strcmp(currentFormat.colorSpace, "RGB24") == 0)
			bpp = 24;
		else if (strcmp(currentFormat.colorSpace, "I420") == 0 ||
				 strcmp(currentFormat.colorSpace, "NV12") == 0 ||
				 strcmp(currentFormat.colorSpace, "NV21") == 0)
			bpp = 12;

		if (bpp > 0) {
			float rawBandwidth = currentFormat.width * currentFormat.height
				* bpp * currentFormat.frameRate / 8.0f / 1024.0f / 1024.0f;
			BString detailStr;
			detailStr.SetToFormat("%d bits/pixel, %.1f MB/s raw bandwidth",
				(int)bpp, rawBandwidth);
			_AppendField("Format Details", detailStr.String());
		}

		BString pixelStr;
		pixelStr.SetToFormat("%d total pixels, %.2f megapixels",
			(int)(currentFormat.width * currentFormat.height),
			currentFormat.width * currentFormat.height / 1000000.0f);
		_AppendField("Resolution Details", pixelStr.String());
	} else {
		_AppendField("Current Format", "Not available (0x0)");
	}
	_AppendNewLine();

	// Driver Diagnostics Section - show issues detected during format negotiation
	if (device->HasDriverWarnings() || (currentFormat.width == 0 && currentFormat.height == 0)) {
		_AppendSection("DRIVER DIAGNOSTICS");

		// Show warnings collected during format negotiation
		if (device->HasDriverWarnings()) {
			_AppendError("Driver Issues Detected:");
			_AppendNewLine();

			// Split warnings by newline and show each
			BString warnings(device->GetDriverWarnings());
			int32 pos = 0;
			while (pos < warnings.Length()) {
				int32 nextLine = warnings.FindFirst('\n', pos);
				if (nextLine < 0)
					nextLine = warnings.Length();

				BString line;
				warnings.CopyInto(line, pos, nextLine - pos);
				if (line.Length() > 0)
					_AppendWarning(line.String());
				pos = nextLine + 1;
			}
			_AppendNewLine();
		}

		// Add context if format is 0x0
		if (currentFormat.width == 0 && currentFormat.height == 0) {
			_AppendError("Video format not available from driver");
			_AppendWarning("The driver's FormatProposal() rejected all format attempts.");
			_AppendNewLine();

			_AppendField("Root Cause Analysis",
				"The UVC driver reports 0x0 dimensions, indicating it failed\n"
				"  to parse USB Video Class frame descriptors. Without valid\n"
				"  frame descriptors, the driver cannot accept any format proposal.");
			_AppendNewLine();

			_AppendField("Technical Details",
				"1. Driver's GetFreeOutputsFor() returns format with 0x0 dimensions\n"
				"  2. BMediaRoster::Connect() fails at FormatProposal stage\n"
				"  3. Both wildcard and specific formats are rejected\n"
				"  4. CodyCam exhibits the same failure pattern");
			_AppendNewLine();

			_AppendField("Driver Code to Check",
				"src/add-ons/media/media-add-ons/usb_webcam/\n"
				"  - UVCCamDevice.cpp: _ParseVideoStreamingDescriptors()\n"
				"  - UVCCamDevice.cpp: AcceptVideoFrame()\n"
				"  - UVCCamDevice.cpp: FormatProposal()\n"
				"  Look for fUncompressedFrames list initialization");
			_AppendNewLine();

			_AppendField("Suggested Actions",
				"1. Run: tail -f /var/log/syslog | grep -i uvc\n"
				"  2. Look for 'Found X descriptors' messages\n"
				"  3. Report bug at dev.haiku-os.org with device VID:PID\n"
				"  4. Include the USB descriptor info shown above");
			_AppendNewLine();
		}
	}

	// Format Negotiation Log Section - useful for driver debugging
	const char* formatLog = device->GetFormatNegotiationLog();
	if (formatLog != NULL && strlen(formatLog) > 0) {
		_AppendSection("FORMAT NEGOTIATION LOG");
		_AppendField("Details", formatLog);
		_AppendNewLine();
	}

	// Audio Capabilities Section
	_AppendSection("AUDIO CAPABILITIES");
	if (device->SupportsAudio()) {
		_AppendField("Audio Connected", true);
		_AppendField("Sample Rate", (int32)device->AudioSampleRate());
		_AppendField("Channels", (int32)device->AudioChannels());
		_AppendField("Bits per Sample", (int32)device->AudioBitsPerSample());
	} else if (device->AudioNodeID() == 0) {
		_AppendField("Audio Status", "Disabled (select source in Audio menu)");
	} else {
		_AppendField("Audio Status", "Not available from this driver");
		_AppendField("Note",
			"The driver may expose audio as a separate node. "
			"Check Audio menu or Media preferences.");
	}
	_AppendNewLine();

	// USB Video Class Descriptors Section
	const USBVideoInfo& usbInfo = device->GetUSBVideoInfo();
	if (usbInfo.found) {
		_AppendSection("USB VIDEO CLASS DESCRIPTORS");

		BString uvcVer;
		uvcVer.SetToFormat("%d.%d", usbInfo.uvcVersion >> 8, usbInfo.uvcVersion & 0xFF);
		_AppendField("UVC Version", uvcVer.String());

		BString clockFreq;
		clockFreq.SetToFormat("%.2f MHz", usbInfo.clockFrequency / 1000000.0);
		_AppendField("Clock Frequency", clockFreq.String());

		_AppendField("Formats Found", (int32)usbInfo.formats.CountItems());

		// Display each format and its frames
		for (int32 f = 0; f < usbInfo.formats.CountItems(); f++) {
			USBVideoFormat* format = usbInfo.formats.ItemAt(f);
			if (format == NULL)
				continue;

			_AppendNewLine();
			BString formatTitle;
			formatTitle.SetToFormat("FORMAT %d: %s", format->formatIndex,
				format->formatName.String());
			_AppendSection(formatTitle.String());

			if (format->bitsPerPixel > 0)
				_AppendField("Bits per Pixel", (int32)format->bitsPerPixel);

			for (int32 fr = 0; fr < format->frames.CountItems(); fr++) {
				USBVideoFrame* frame = format->frames.ItemAt(fr);
				if (frame == NULL)
					continue;

				BString frameInfo;
				frameInfo.SetToFormat("%dx%d", frame->width, frame->height);
				BString fpsInfo;
				if (frame->frameRates.CountItems() > 0) {
					fpsInfo << " @ ";
					for (int32 r = 0; r < frame->frameRates.CountItems(); r++) {
						FrameRate* fps = frame->frameRates.ItemAt(r);
						if (fps != NULL) {
							if (r > 0)
								fpsInfo << ", ";
							fpsInfo << BString().SetToFormat("%.1f", fps->value);
						}
					}
					fpsInfo << " fps";
				} else if (frame->defaultFrameRate > 0) {
					fpsInfo << " @ " << BString().SetToFormat("%.1f fps", frame->defaultFrameRate);
				}
				frameInfo << fpsInfo;

				BString label;
				label.SetToFormat("  Resolution %d", fr + 1);
				_AppendField(label.String(), frameInfo.String());
			}
		}

		// Show diagnostic info if available
		if (usbInfo.diagnosticInfo.Length() > 0) {
			_AppendNewLine();
			_AppendSection("USB DESCRIPTOR DETAILS");
			_AppendField("Raw Details", usbInfo.diagnosticInfo.String());
		}

		_AppendNewLine();
	}

	// Media Kit Information Section
	_AppendSection("MEDIA KIT STATUS");
	_AppendField("Registered with Media Kit", device->IsRegisteredWithMediaKit());
	_AppendField("Media Node ID", (int32)device->MediaNodeID());
	_AppendNewLine();

	// Capture Status Section
	_AppendSection("CAPTURE STATUS");
	_AppendField("Currently Capturing", isCapturing);
	if (isCapturing) {
		_AppendField("Frames Captured", (int32)device->FramesCaptured());
		_AppendField("Frames Dropped", (int32)device->FramesDropped());
		_AppendField("Capture FPS", device->CurrentFPS());
	}
}


void
DriverInfoView::Clear()
{
	SetText("");
}


void
DriverInfoView::_AppendSection(const char* title)
{
	int32 start = TextLength();
	Insert(TextLength(), title, strlen(title));
	Insert(TextLength(), "\n", 1);
	int32 end = TextLength() - 1;

	SetFontAndColor(start, end, &fBoldFont, B_FONT_ALL, &fSectionColor);
}


void
DriverInfoView::_AppendField(const char* label, const char* value)
{
	int32 labelStart = TextLength();
	Insert(TextLength(), label, strlen(label));
	Insert(TextLength(), ": ", 2);
	int32 labelEnd = TextLength();

	const char* displayValue = (value != NULL && strlen(value) > 0) ? value : "N/A";
	int32 valueStart = TextLength();
	Insert(TextLength(), displayValue, strlen(displayValue));
	Insert(TextLength(), "\n", 1);
	int32 valueEnd = TextLength() - 1;

	SetFontAndColor(labelStart, labelEnd, &fNormalFont, B_FONT_ALL, &fLabelColor);
	SetFontAndColor(valueStart, valueEnd, &fNormalFont, B_FONT_ALL, &fValueColor);
}


void
DriverInfoView::_AppendField(const char* label, int32 value)
{
	BString str;
	str.SetToFormat("%d", value);
	_AppendField(label, str.String());
}


void
DriverInfoView::_AppendField(const char* label, uint32 value, bool hex)
{
	BString str;
	if (hex)
		str.SetToFormat("0x%04X", value);
	else
		str.SetToFormat("%u", value);
	_AppendField(label, str.String());
}


void
DriverInfoView::_AppendField(const char* label, float value)
{
	BString str;
	str.SetToFormat("%.2f", value);
	_AppendField(label, str.String());
}


void
DriverInfoView::_AppendField(const char* label, bool value)
{
	_AppendField(label, value ? "Yes" : "No");
}


void
DriverInfoView::_AppendNewLine()
{
	Insert(TextLength(), "\n", 1);
}


void
DriverInfoView::_AppendWarning(const char* text)
{
	int32 start = TextLength();
	Insert(TextLength(), text, strlen(text));
	Insert(TextLength(), "\n", 1);
	int32 end = TextLength() - 1;

	SetFontAndColor(start, end, &fNormalFont, B_FONT_ALL, &fWarningColor);
}


void
DriverInfoView::_AppendError(const char* text)
{
	int32 start = TextLength();
	Insert(TextLength(), text, strlen(text));
	Insert(TextLength(), "\n", 1);
	int32 end = TextLength() - 1;

	SetFontAndColor(start, end, &fBoldFont, B_FONT_ALL, &fErrorColor);
}
