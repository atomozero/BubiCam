/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "DriverInfoView.h"
#include "WebcamDevice.h"

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
				formatLine.SetToFormat("  %ldx%ld @ %.1f fps (%s)",
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
		currentStr.SetToFormat("%ldx%ld @ %.1f fps (%s)",
			currentFormat.width, currentFormat.height,
			currentFormat.frameRate, currentFormat.colorSpace);
		_AppendField("Current Format", currentStr.String());
	}
	_AppendNewLine();

	// Audio Capabilities Section
	_AppendSection("AUDIO CAPABILITIES");
	_AppendField("Audio Supported", device->SupportsAudio());
	if (device->SupportsAudio()) {
		_AppendField("Sample Rate", (int32)device->AudioSampleRate());
		_AppendField("Channels", (int32)device->AudioChannels());
		_AppendField("Bits per Sample", (int32)device->AudioBitsPerSample());
	}
	_AppendNewLine();

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
	str.SetToFormat("%ld", value);
	_AppendField(label, str.String());
}


void
DriverInfoView::_AppendField(const char* label, uint32 value, bool hex)
{
	BString str;
	if (hex)
		str.SetToFormat("0x%04X", value);
	else
		str.SetToFormat("%lu", value);
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
