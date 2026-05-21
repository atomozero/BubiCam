/*
 * Copyright 2024, BubiCam Team
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * USB Packet Viewer implementation
 */

#include "USBPacketView.h"
#include "WebcamDevice.h"
#include "WebcamRoster.h"

#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <StringView.h>
#include <Clipboard.h>
#include <File.h>
#include <Path.h>
#include <FindDirectory.h>
#include <Alert.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>


// #pragma mark - HexDumpView


HexDumpView::HexDumpView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fData(NULL),
	fDataLength(0),
	fCharWidth(8.0f),
	fLineHeight(14.0f),
	fBytesPerLine(16),
	fTotalLines(0)
{
	SetFont(be_fixed_font);
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
}


HexDumpView::~HexDumpView()
{
	delete[] fData;
}


void
HexDumpView::Draw(BRect updateRect)
{
	if (fData == NULL || fDataLength == 0) {
		DrawString("No data", BPoint(10, fLineHeight));
		return;
	}

	font_height fh;
	GetFontHeight(&fh);
	float baseline = fh.ascent;

	int32 firstLine = (int32)(updateRect.top / fLineHeight);
	int32 lastLine = (int32)(updateRect.bottom / fLineHeight) + 1;

	if (firstLine < 0)
		firstLine = 0;
	if (lastLine > fTotalLines)
		lastLine = fTotalLines;

	char line[128];
	char ascii[20];

	for (int32 i = firstLine; i < lastLine; i++) {
		size_t offset = i * fBytesPerLine;
		if (offset >= fDataLength)
			break;

		float y = i * fLineHeight + baseline;

		// Address
		sprintf(line, "%04X: ", (unsigned int)offset);
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
		DrawString(line, BPoint(5, y));

		// Hex bytes
		BString hexPart;
		memset(ascii, 0, sizeof(ascii));

		for (int j = 0; j < fBytesPerLine; j++) {
			if (offset + j < fDataLength) {
				uint8 byte = fData[offset + j];
				char hex[4];
				sprintf(hex, "%02X ", byte);
				hexPart += hex;
				ascii[j] = isprint(byte) ? byte : '.';
			} else {
				hexPart += "   ";
				ascii[j] = ' ';
			}

			// Add extra space in middle
			if (j == 7)
				hexPart += " ";
		}

		SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
		DrawString(hexPart.String(), BPoint(50, y));

		// ASCII representation
		SetHighColor(tint_color(ui_color(B_CONTROL_MARK_COLOR), B_NO_TINT));
		DrawString(ascii, BPoint(50 + fBytesPerLine * 3 * fCharWidth + fCharWidth, y));
	}
}


void
HexDumpView::FrameResized(float width, float height)
{
	_RecalculateSize();
	Invalidate();
}


void
HexDumpView::GetPreferredSize(float* width, float* height)
{
	if (width)
		*width = 50 + fBytesPerLine * 3 * fCharWidth + fCharWidth + fBytesPerLine * fCharWidth + 20;
	if (height)
		*height = fTotalLines * fLineHeight + 10;
}


void
HexDumpView::SetData(const uint8* data, size_t length)
{
	delete[] fData;
	fData = NULL;
	fDataLength = 0;

	if (data != NULL && length > 0) {
		fData = new uint8[length];
		memcpy(fData, data, length);
		fDataLength = length;
	}

	_RecalculateSize();
	Invalidate();
}


void
HexDumpView::Clear()
{
	SetData(NULL, 0);
}


BString
HexDumpView::GetHexString() const
{
	BString result;
	if (fData == NULL)
		return result;

	char line[128];
	char ascii[20];

	for (size_t i = 0; i < fDataLength; i += fBytesPerLine) {
		sprintf(line, "%04X: ", (unsigned int)i);
		result += line;

		memset(ascii, 0, sizeof(ascii));
		for (int j = 0; j < fBytesPerLine; j++) {
			if (i + j < fDataLength) {
				sprintf(line, "%02X ", fData[i + j]);
				result += line;
				ascii[j] = isprint(fData[i + j]) ? fData[i + j] : '.';
			} else {
				result += "   ";
			}
			if (j == 7)
				result += " ";
		}
		result += " ";
		result += ascii;
		result += "\n";
	}

	return result;
}


void
HexDumpView::_RecalculateSize()
{
	font_height fh;
	GetFontHeight(&fh);
	fLineHeight = fh.ascent + fh.descent + fh.leading + 2;
	fCharWidth = StringWidth("W");

	fTotalLines = (fDataLength + fBytesPerLine - 1) / fBytesPerLine;
	if (fTotalLines == 0)
		fTotalLines = 1;

	float prefWidth, prefHeight;
	GetPreferredSize(&prefWidth, &prefHeight);

	if (Parent() != NULL) {
		BScrollView* scrollView = dynamic_cast<BScrollView*>(Parent());
		if (scrollView != NULL) {
			BScrollBar* vBar = scrollView->ScrollBar(B_VERTICAL);
			if (vBar != NULL) {
				BRect bounds = scrollView->Bounds();
				float range = prefHeight - bounds.Height();
				if (range < 0)
					range = 0;
				vBar->SetRange(0, range);
				vBar->SetProportion(bounds.Height() / prefHeight);
			}
		}
	}

	ResizeTo(prefWidth, prefHeight);
}


// #pragma mark - DescriptorTreeView


DescriptorTreeView::DescriptorTreeView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fDescriptors(20),
	fSelectedIndex(-1),
	fLineHeight(18.0f)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	SetLowUIColor(B_LIST_BACKGROUND_COLOR);
	SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
}


DescriptorTreeView::~DescriptorTreeView()
{
	// NOTE: We do NOT delete the descriptors here.
	// USBPacketView owns all USBDescriptorInfo objects and is responsible
	// for deleting them. We just store non-owning pointers.
}


void
DescriptorTreeView::Draw(BRect updateRect)
{
	font_height fh;
	GetFontHeight(&fh);
	fLineHeight = fh.ascent + fh.descent + fh.leading + 4;

	BRect rect = Bounds();
	rect.bottom = rect.top + fLineHeight;

	for (int32 i = 0; i < fDescriptors.CountItems(); i++) {
		USBDescriptorInfo* info = fDescriptors.ItemAt(i);

		if (rect.Intersects(updateRect)) {
			// Selection background
			if (i == fSelectedIndex) {
				SetHighUIColor(B_LIST_SELECTED_BACKGROUND_COLOR);
				FillRect(rect);
				SetHighUIColor(B_LIST_SELECTED_ITEM_TEXT_COLOR);
			} else {
				SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
			}

			BPoint textPoint(rect.left + 5, rect.bottom - fh.descent - 2);

			// Type indicator
			char typeStr[8];
			sprintf(typeStr, "[%02X] ", info->type);
			SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
			DrawString(typeStr, textPoint);
			textPoint.x += StringWidth(typeStr);

			// Name
			if (i == fSelectedIndex)
				SetHighUIColor(B_LIST_SELECTED_ITEM_TEXT_COLOR);
			else
				SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
			DrawString(info->name.String(), textPoint);
		}

		rect.OffsetBy(0, fLineHeight);
	}
}


void
DescriptorTreeView::MouseDown(BPoint where)
{
	int32 index = (int32)(where.y / fLineHeight);
	if (index >= 0 && index < fDescriptors.CountItems()) {
		fSelectedIndex = index;
		Invalidate();

		// Notify parent to update hex view
		BMessage msg(MSG_USB_TAB_CHANGED);
		msg.AddInt32("index", index);
		if (Window() != NULL)
			Window()->PostMessage(&msg);
	}
}


void
DescriptorTreeView::GetPreferredSize(float* width, float* height)
{
	if (width)
		*width = 300;
	if (height)
		*height = fDescriptors.CountItems() * fLineHeight + 10;
}


void
DescriptorTreeView::AddDescriptor(USBDescriptorInfo* info)
{
	fDescriptors.AddItem(info);

	float prefWidth, prefHeight;
	GetPreferredSize(&prefWidth, &prefHeight);
	ResizeTo(prefWidth, prefHeight);

	Invalidate();
}


void
DescriptorTreeView::Clear()
{
	// NOTE: We do NOT delete descriptors here - USBPacketView owns them.
	// Just clear our pointer list.
	fDescriptors.MakeEmpty();
	fSelectedIndex = -1;
	Invalidate();
}


USBDescriptorInfo*
DescriptorTreeView::SelectedDescriptor() const
{
	if (fSelectedIndex >= 0 && fSelectedIndex < fDescriptors.CountItems())
		return fDescriptors.ItemAt(fSelectedIndex);
	return NULL;
}


// #pragma mark - USBPacketView


USBPacketView::USBPacketView(const char* name)
	:
	BView(name, B_WILL_DRAW),
	fDevice(NULL),
	fTabView(NULL),
	fSummaryView(NULL),
	fSummaryScroll(NULL),
	fTreeView(NULL),
	fTreeScroll(NULL),
	fHexView(NULL),
	fHexScroll(NULL),
	fRefreshButton(NULL),
	fCopyButton(NULL),
	fExportButton(NULL),
	fDescriptors(20),
	fRawDescriptorData(NULL),
	fRawDescriptorLength(0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	_BuildLayout();
}


USBPacketView::~USBPacketView()
{
	delete[] fRawDescriptorData;
	// Delete owned descriptors
	for (int32 i = 0; i < fDescriptors.CountItems(); i++) {
		delete fDescriptors.ItemAt(i);
	}
}


void
USBPacketView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fRefreshButton->SetTarget(this);
	fCopyButton->SetTarget(this);
	fExportButton->SetTarget(this);
}


void
USBPacketView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_USB_REFRESH:
			Refresh();
			break;

		case MSG_USB_COPY_HEX:
		{
			BString hexStr = fHexView->GetHexString();
			if (hexStr.Length() > 0) {
				if (be_clipboard->Lock()) {
					be_clipboard->Clear();
					BMessage* clip = be_clipboard->Data();
					clip->AddData("text/plain", B_MIME_TYPE, hexStr.String(),
						hexStr.Length());
					be_clipboard->Commit();
					be_clipboard->Unlock();
				}
			}
			break;
		}

		case MSG_USB_EXPORT:
			_ExportDescriptors();
			break;

		case MSG_USB_TAB_CHANGED:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				USBDescriptorInfo* info = NULL;
				if (index >= 0 && index < fDescriptors.CountItems())
					info = fDescriptors.ItemAt(index);

				if (info != NULL && info->rawData != NULL) {
					fHexView->SetData(info->rawData, info->rawLength);
				}
			}
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
USBPacketView::SetDevice(WebcamDevice* device)
{
	fDevice = device;
	Refresh();
}


void
USBPacketView::Refresh()
{
	// Clear old data
	fSummaryView->SetText("");
	fTreeView->Clear();
	fHexView->Clear();

	// Clear owned descriptors
	for (int32 i = 0; i < fDescriptors.CountItems(); i++) {
		delete fDescriptors.ItemAt(i);
	}
	fDescriptors.MakeEmpty();

	delete[] fRawDescriptorData;
	fRawDescriptorData = NULL;
	fRawDescriptorLength = 0;

	if (fDevice == NULL) {
		_AddToLog("No device selected.\n");
		_AddToLog("Select a webcam from the list to view USB descriptors.\n");
		return;
	}

	_AddToLog("USB Descriptor Analysis\n");
	_AddToLog("=======================\n\n");

	_ParseUSBDescriptors();
}


void
USBPacketView::_BuildLayout()
{
	// Create tab view - limit size to prevent layout overflow
	fTabView = new BTabView("usbTabs");
	fTabView->SetExplicitMinSize(BSize(200, 150));

	// Tab 1: Summary text view - limit size to prevent layout overflow
	fSummaryView = new BTextView("summary");
	fSummaryView->SetStylable(false);
	fSummaryView->MakeEditable(false);
	fSummaryView->SetFont(be_fixed_font);
	fSummaryView->SetExplicitMinSize(BSize(100, 100));
	fSummaryView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fSummaryScroll = new BScrollView("summaryScroll", fSummaryView,
		B_WILL_DRAW | B_SUPPORTS_LAYOUT, true, true);
	fTabView->AddTab(fSummaryScroll, new BTab());
	fTabView->TabAt(0)->SetLabel("Summary");

	// Tab 2: Descriptor tree - limit size to prevent layout overflow
	fTreeView = new DescriptorTreeView("tree");
	fTreeView->SetExplicitMinSize(BSize(100, 100));
	fTreeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fTreeScroll = new BScrollView("treeScroll", fTreeView,
		B_WILL_DRAW | B_SUPPORTS_LAYOUT, false, true);
	fTabView->AddTab(fTreeScroll, new BTab());
	fTabView->TabAt(1)->SetLabel("Descriptors");

	// Tab 3: Hex dump - limit size to prevent layout overflow
	fHexView = new HexDumpView("hex");
	fHexView->SetExplicitMinSize(BSize(100, 100));
	fHexView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fHexScroll = new BScrollView("hexScroll", fHexView,
		B_WILL_DRAW | B_SUPPORTS_LAYOUT, true, true);
	fTabView->AddTab(fHexScroll, new BTab());
	fTabView->TabAt(2)->SetLabel("Hex Dump");

	// Buttons
	fRefreshButton = new BButton("refresh", "Refresh", new BMessage(MSG_USB_REFRESH));
	fCopyButton = new BButton("copy", "Copy Hex", new BMessage(MSG_USB_COPY_HEX));
	fExportButton = new BButton("export", "Export...", new BMessage(MSG_USB_EXPORT));

	// Layout
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fTabView, 1.0)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fRefreshButton)
			.Add(fCopyButton)
			.Add(fExportButton)
			.AddGlue()
		.End()
	.End();
}


void
USBPacketView::_ParseUSBDescriptors()
{
	if (fDevice == NULL)
		return;

	// Get basic USB info from WebcamDevice
	const USBDeviceInfo& usbInfo = fDevice->GetUSBInfo();

	_AddToLog("Device: %s\n", fDevice->Name());
	_AddToLog("Vendor ID:  0x%04X (%s)\n", usbInfo.vendorID,
		usbInfo.vendorName.Length() > 0 ? usbInfo.vendorName.String() : "Unknown");
	_AddToLog("Product ID: 0x%04X (%s)\n", usbInfo.productID,
		usbInfo.productName.Length() > 0 ? usbInfo.productName.String() : "Unknown");
	if (usbInfo.serialNumber.Length() > 0)
		_AddToLog("Serial: %s\n", usbInfo.serialNumber.String());
	if (usbInfo.usbVersion.Length() > 0)
		_AddToLog("USB Version: %s\n", usbInfo.usbVersion.String());
	_AddToLog("\n");

	// USB Device Class
	_AddToLog("Device Class: 0x%02X", usbInfo.deviceClass);
	if (usbInfo.deviceClass == 0xEF)
		_AddToLog(" (Miscellaneous - IAD)");
	else if (usbInfo.deviceClass == 0x00)
		_AddToLog(" (Defined at Interface level)");
	else if (usbInfo.deviceClass == 0x0E)
		_AddToLog(" (Video)");
	_AddToLog("\n");

	_AddToLog("Device Subclass: 0x%02X\n", usbInfo.deviceSubclass);
	_AddToLog("Device Protocol: 0x%02X\n", usbInfo.deviceProtocol);
	_AddToLog("\n");

	// Create device descriptor entry for tree
	USBDescriptorInfo* devDesc = new USBDescriptorInfo();
	devDesc->type = USB_DESC_DEVICE;
	devDesc->name = "Device Descriptor";
	devDesc->description.SetToFormat("VID: 0x%04X, PID: 0x%04X",
		usbInfo.vendorID, usbInfo.productID);

	// Build raw device descriptor (18 bytes)
	devDesc->rawLength = 18;
	devDesc->rawData = new uint8[18];
	memset(devDesc->rawData, 0, 18);
	devDesc->rawData[0] = 18;  // bLength
	devDesc->rawData[1] = USB_DESC_DEVICE;  // bDescriptorType
	devDesc->rawData[2] = 0x00;  // bcdUSB low
	devDesc->rawData[3] = 0x02;  // bcdUSB high (USB 2.0)
	devDesc->rawData[4] = usbInfo.deviceClass;
	devDesc->rawData[5] = usbInfo.deviceSubclass;
	devDesc->rawData[6] = usbInfo.deviceProtocol;
	devDesc->rawData[8] = usbInfo.vendorID & 0xFF;
	devDesc->rawData[9] = (usbInfo.vendorID >> 8) & 0xFF;
	devDesc->rawData[10] = usbInfo.productID & 0xFF;
	devDesc->rawData[11] = (usbInfo.productID >> 8) & 0xFF;

	fDescriptors.AddItem(devDesc);
	fTreeView->AddDescriptor(devDesc);

	// Get UVC-specific info from USBVideoInfo
	const USBVideoInfo& uvcInfo = fDevice->GetUSBVideoInfo();

	_AddToLog("UVC Information:\n");
	_AddToLog("----------------\n");

	if (uvcInfo.found) {
		if (uvcInfo.uvcVersion > 0) {
			_AddToLog("UVC Version: %d.%02d\n", uvcInfo.uvcVersion >> 8,
				uvcInfo.uvcVersion & 0xFF);
		}

		if (uvcInfo.clockFrequency > 0) {
			_AddToLog("Clock Frequency: %u Hz\n", (unsigned)uvcInfo.clockFrequency);
		}

		// Show supported formats from UVC parsing
		_AddToLog("\nSupported Formats (from UVC descriptors):\n");

		for (int32 i = 0; i < uvcInfo.formats.CountItems(); i++) {
			USBVideoFormat* format = uvcInfo.formats.ItemAt(i);
			if (format != NULL) {
				_AddToLog("  Format %d: %s (%d bpp)\n",
					format->formatIndex,
					format->formatName.String(),
					format->bitsPerPixel);

				// Add format descriptor to tree
				USBDescriptorInfo* fmtDesc = new USBDescriptorInfo();
				fmtDesc->type = USB_DESC_CS_INTERFACE;
				fmtDesc->subtype = format->formatType == 0 ?
					UVC_VS_FORMAT_UNCOMPRESSED : UVC_VS_FORMAT_MJPEG;
				fmtDesc->name.SetToFormat("Format: %s", format->formatName.String());
				fmtDesc->rawLength = 8;
				fmtDesc->rawData = new uint8[8];
				memset(fmtDesc->rawData, 0, 8);
				fmtDesc->rawData[0] = 8;
				fmtDesc->rawData[1] = USB_DESC_CS_INTERFACE;
				fmtDesc->rawData[2] = fmtDesc->subtype;
				fmtDesc->rawData[3] = format->formatIndex;
				fmtDesc->rawData[4] = format->numFrames;
				fDescriptors.AddItem(fmtDesc);
				fTreeView->AddDescriptor(fmtDesc);

				// Show frame sizes
				for (int32 j = 0; j < format->frames.CountItems(); j++) {
					USBVideoFrame* frame = format->frames.ItemAt(j);
					if (frame != NULL) {
						_AddToLog("    - %dx%d @ %.1f fps",
							frame->width, frame->height, frame->defaultFrameRate);

						// Show all available frame rates
						if (frame->frameRates.CountItems() > 1) {
							_AddToLog(" (also: ");
							for (int32 k = 0; k < frame->frameRates.CountItems(); k++) {
								FrameRate* rate = frame->frameRates.ItemAt(k);
								if (rate != NULL && rate->value != frame->defaultFrameRate) {
									_AddToLog("%.1f", rate->value);
									if (k < frame->frameRates.CountItems() - 1)
										_AddToLog(", ");
								}
							}
							_AddToLog(")");
						}
						_AddToLog("\n");

						// Add frame descriptor to tree
						USBDescriptorInfo* frameDesc = new USBDescriptorInfo();
						frameDesc->type = USB_DESC_CS_INTERFACE;
						frameDesc->subtype = format->formatType == 0 ?
							UVC_VS_FRAME_UNCOMPRESSED : UVC_VS_FRAME_MJPEG;
						frameDesc->name.SetToFormat("  Frame: %dx%d", frame->width, frame->height);
						frameDesc->rawLength = 26;
						frameDesc->rawData = new uint8[26];
						memset(frameDesc->rawData, 0, 26);
						frameDesc->rawData[0] = 26;
						frameDesc->rawData[1] = USB_DESC_CS_INTERFACE;
						frameDesc->rawData[2] = frameDesc->subtype;
						frameDesc->rawData[3] = j + 1;  // frame index
						frameDesc->rawData[5] = frame->width & 0xFF;
						frameDesc->rawData[6] = (frame->width >> 8) & 0xFF;
						frameDesc->rawData[7] = frame->height & 0xFF;
						frameDesc->rawData[8] = (frame->height >> 8) & 0xFF;
						fDescriptors.AddItem(frameDesc);
						fTreeView->AddDescriptor(frameDesc);
					}
				}
			}
		}

		// Show diagnostic info if available
		if (uvcInfo.diagnosticInfo.Length() > 0) {
			_AddToLog("\nDiagnostic Info:\n");
			_AddToLog("%s\n", uvcInfo.diagnosticInfo.String());
		}
	} else {
		_AddToLog("UVC device info not available.\n");
		_AddToLog("The device may not be a UVC-compliant webcam,\n");
		_AddToLog("or USB descriptor parsing failed.\n");
	}

	// Driver info
	const DriverInfo& driverInfo = fDevice->GetDriverInfo();
	_AddToLog("\nDriver Information:\n");
	_AddToLog("-------------------\n");
	_AddToLog("Driver Name: %s\n", driverInfo.name.String());
	if (driverInfo.path.Length() > 0)
		_AddToLog("Driver Path: %s\n", driverInfo.path.String());
	if (driverInfo.version.Length() > 0)
		_AddToLog("Driver Version: %s\n", driverInfo.version.String());

	// Driver warnings
	if (fDevice->HasDriverWarnings()) {
		_AddToLog("\nDriver Warnings:\n");
		_AddToLog("%s\n", fDevice->GetDriverWarnings());
	}

	// Build combined raw data for hex view
	size_t totalSize = 0;
	for (int32 i = 0; i < fDescriptors.CountItems(); i++) {
		USBDescriptorInfo* info = fDescriptors.ItemAt(i);
		if (info->rawData != NULL)
			totalSize += info->rawLength;
	}

	if (totalSize > 0) {
		fRawDescriptorData = new uint8[totalSize];
		fRawDescriptorLength = totalSize;
		size_t offset = 0;
		for (int32 i = 0; i < fDescriptors.CountItems(); i++) {
			USBDescriptorInfo* info = fDescriptors.ItemAt(i);
			if (info->rawData != NULL) {
				memcpy(fRawDescriptorData + offset, info->rawData, info->rawLength);
				offset += info->rawLength;
			}
		}

		// Set initial hex view to all descriptors
		fHexView->SetData(fRawDescriptorData, fRawDescriptorLength);
	}

	_AddToLog("\n---\n");
	_AddToLog("Note: Some descriptor data is reconstructed from cached device info.\n");
	_AddToLog("For full raw USB descriptors, use 'listusb -v' from Terminal.\n");
}


BString
USBPacketView::_FormatGUID(const uint8* guid)
{
	BString result;
	result.SetToFormat(
		"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid[3], guid[2], guid[1], guid[0],
		guid[5], guid[4],
		guid[7], guid[6],
		guid[8], guid[9],
		guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
	return result;
}


BString
USBPacketView::_GetTerminalTypeName(uint16 type)
{
	switch (type) {
		case 0x0100: return "TT_VENDOR_SPECIFIC";
		case 0x0101: return "TT_STREAMING";
		case 0x0201: return "ITT_VENDOR_SPECIFIC";
		case 0x0202: return "ITT_CAMERA";
		case 0x0203: return "ITT_MEDIA_TRANSPORT_INPUT";
		case 0x0301: return "OTT_VENDOR_SPECIFIC";
		case 0x0302: return "OTT_DISPLAY";
		case 0x0303: return "OTT_MEDIA_TRANSPORT_OUTPUT";
		default:
		{
			BString unknown;
			unknown.SetToFormat("Unknown (0x%04X)", type);
			return unknown;
		}
	}
}


BString
USBPacketView::_GetProcessingUnitControls(uint32 controls)
{
	BString result;
	if (controls & (1 << 0)) result += "Brightness, ";
	if (controls & (1 << 1)) result += "Contrast, ";
	if (controls & (1 << 2)) result += "Hue, ";
	if (controls & (1 << 3)) result += "Saturation, ";
	if (controls & (1 << 4)) result += "Sharpness, ";
	if (controls & (1 << 5)) result += "Gamma, ";
	if (controls & (1 << 6)) result += "White Balance Temperature, ";
	if (controls & (1 << 7)) result += "White Balance Component, ";
	if (controls & (1 << 8)) result += "Backlight Compensation, ";
	if (controls & (1 << 9)) result += "Gain, ";
	if (controls & (1 << 10)) result += "Power Line Frequency, ";
	if (controls & (1 << 11)) result += "Hue Auto, ";
	if (controls & (1 << 12)) result += "White Balance Auto, ";
	if (controls & (1 << 13)) result += "White Balance Component Auto, ";
	if (controls & (1 << 14)) result += "Digital Multiplier, ";
	if (controls & (1 << 15)) result += "Digital Multiplier Limit, ";
	if (controls & (1 << 16)) result += "Analog Video Standard, ";
	if (controls & (1 << 17)) result += "Analog Video Lock Status, ";
	if (controls & (1 << 18)) result += "Contrast Auto, ";

	if (result.Length() > 2) {
		result.RemoveLast(", ");
	}

	return result;
}


void
USBPacketView::_AddToLog(const char* format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fSummaryView->Insert(fSummaryView->TextLength(), buffer, strlen(buffer));
}


void
USBPacketView::_UpdateHexView()
{
	USBDescriptorInfo* selected = fTreeView->SelectedDescriptor();
	if (selected != NULL && selected->rawData != NULL) {
		fHexView->SetData(selected->rawData, selected->rawLength);
	} else {
		fHexView->SetData(fRawDescriptorData, fRawDescriptorLength);
	}
}


void
USBPacketView::_ExportDescriptors()
{
	// Generate filename
	BPath path;
	find_directory(B_USER_DIRECTORY, &path);
	path.Append("usb_descriptors.txt");

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		BAlert* alert = new BAlert("Error", "Failed to create export file.",
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	// Write summary
	const char* summary = fSummaryView->Text();
	file.Write(summary, strlen(summary));

	file.Write("\n\n=== HEX DUMP ===\n\n", 20);

	// Write hex dump
	BString hexStr = fHexView->GetHexString();
	file.Write(hexStr.String(), hexStr.Length());

	BString message;
	message.SetToFormat("Descriptors exported to:\n%s", path.Path());
	BAlert* alert = new BAlert("Export Complete", message.String(),
		"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go();
}
