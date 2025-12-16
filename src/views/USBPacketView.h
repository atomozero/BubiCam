/*
 * Copyright 2024, BubiCam Team
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * USB Packet Viewer - View USB descriptors and traffic for webcam debugging
 */
#ifndef USB_PACKET_VIEW_H
#define USB_PACKET_VIEW_H

#include <View.h>
#include <TextView.h>
#include <TabView.h>
#include <Button.h>
#include <ScrollView.h>
#include <String.h>
#include <ObjectList.h>

class WebcamDevice;

// USB descriptor types
enum {
	USB_DESC_DEVICE = 0x01,
	USB_DESC_CONFIGURATION = 0x02,
	USB_DESC_STRING = 0x03,
	USB_DESC_INTERFACE = 0x04,
	USB_DESC_ENDPOINT = 0x05,
	USB_DESC_INTERFACE_ASSOCIATION = 0x0B,
	// UVC specific
	USB_DESC_CS_INTERFACE = 0x24,
	USB_DESC_CS_ENDPOINT = 0x25
};

// UVC interface subtypes
enum {
	UVC_VC_HEADER = 0x01,
	UVC_VC_INPUT_TERMINAL = 0x02,
	UVC_VC_OUTPUT_TERMINAL = 0x03,
	UVC_VC_SELECTOR_UNIT = 0x04,
	UVC_VC_PROCESSING_UNIT = 0x05,
	UVC_VC_EXTENSION_UNIT = 0x06,
	UVC_VS_INPUT_HEADER = 0x01,
	UVC_VS_OUTPUT_HEADER = 0x02,
	UVC_VS_FORMAT_UNCOMPRESSED = 0x04,
	UVC_VS_FRAME_UNCOMPRESSED = 0x05,
	UVC_VS_FORMAT_MJPEG = 0x06,
	UVC_VS_FRAME_MJPEG = 0x07,
	UVC_VS_FORMAT_FRAME_BASED = 0x10,
	UVC_VS_FRAME_FRAME_BASED = 0x11,
	UVC_VS_COLOR_FORMAT = 0x0D
};

// Parsed USB descriptor info
struct USBDescriptorInfo {
	uint8		type;
	uint8		subtype;
	BString		name;
	BString		description;
	uint8*		rawData;
	size_t		rawLength;

	USBDescriptorInfo()
		: type(0), subtype(0), rawData(NULL), rawLength(0) {}
	~USBDescriptorInfo() { delete[] rawData; }
};

// Messages
enum {
	MSG_USB_REFRESH = 'uref',
	MSG_USB_COPY_HEX = 'ucph',
	MSG_USB_EXPORT = 'uexp',
	MSG_USB_TAB_CHANGED = 'utab'
};


class HexDumpView : public BView {
public:
							HexDumpView(const char* name);
	virtual					~HexDumpView();

	virtual void			Draw(BRect updateRect);
	virtual void			FrameResized(float width, float height);
	virtual void			GetPreferredSize(float* width, float* height);

			void			SetData(const uint8* data, size_t length);
			void			Clear();
			BString			GetHexString() const;

private:
			void			_RecalculateSize();

			uint8*			fData;
			size_t			fDataLength;
			float			fCharWidth;
			float			fLineHeight;
			int32			fBytesPerLine;
			int32			fTotalLines;
};


class DescriptorTreeView : public BView {
public:
							DescriptorTreeView(const char* name);
	virtual					~DescriptorTreeView();

	virtual void			Draw(BRect updateRect);
	virtual void			MouseDown(BPoint where);
	virtual void			GetPreferredSize(float* width, float* height);

			void			AddDescriptor(USBDescriptorInfo* info);
			void			Clear();
			USBDescriptorInfo* SelectedDescriptor() const;

private:
			void			_DrawDescriptor(USBDescriptorInfo* info,
								BRect& rect, int32 indent);

			BObjectList<USBDescriptorInfo>	fDescriptors;
			int32			fSelectedIndex;
			float			fLineHeight;
};


class USBPacketView : public BView {
public:
							USBPacketView(const char* name);
	virtual					~USBPacketView();

	virtual void			AttachedToWindow();
	virtual void			MessageReceived(BMessage* message);

			void			SetDevice(WebcamDevice* device);
			void			Refresh();

private:
			void			_BuildLayout();
			void			_ParseUSBDescriptors();
			void			_ParseDeviceDescriptor(const uint8* data, size_t length);
			void			_ParseConfigDescriptor(const uint8* data, size_t length);
			void			_ParseInterfaceDescriptor(const uint8* data, size_t length,
								int interfaceNum);
			void			_ParseEndpointDescriptor(const uint8* data, size_t length);
			void			_ParseUVCDescriptor(const uint8* data, size_t length,
								bool isVideoControl);
			BString			_FormatGUID(const uint8* guid);
			BString			_GetTerminalTypeName(uint16 type);
			BString			_GetProcessingUnitControls(uint32 controls);
			void			_AddToLog(const char* format, ...);
			void			_UpdateHexView();
			void			_ExportDescriptors();

			WebcamDevice*	fDevice;

			// UI elements
			BTabView*		fTabView;
			BTextView*		fSummaryView;
			BScrollView*	fSummaryScroll;
			DescriptorTreeView*	fTreeView;
			BScrollView*	fTreeScroll;
			HexDumpView*	fHexView;
			BScrollView*	fHexScroll;
			BButton*		fRefreshButton;
			BButton*		fCopyButton;
			BButton*		fExportButton;

			// Parsed data
			BObjectList<USBDescriptorInfo>	fDescriptors;
			uint8*			fRawDescriptorData;
			size_t			fRawDescriptorLength;
};

#endif // USB_PACKET_VIEW_H
