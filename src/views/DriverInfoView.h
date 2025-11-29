/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef DRIVER_INFO_VIEW_H
#define DRIVER_INFO_VIEW_H

#include <View.h>
#include <TextView.h>
#include <String.h>

class WebcamDevice;

class DriverInfoView : public BTextView {
public:
						DriverInfoView(const char* name);
	virtual				~DriverInfoView();

	virtual void		AttachedToWindow();

	void				SetDevice(WebcamDevice* device, bool isCapturing);
	void				Clear();

private:
	void				_AppendSection(const char* title);
	void				_AppendField(const char* label, const char* value);
	void				_AppendField(const char* label, int32 value);
	void				_AppendField(const char* label, uint32 value, bool hex = false);
	void				_AppendField(const char* label, float value);
	void				_AppendField(const char* label, bool value);
	void				_AppendNewLine();

	BFont				fBoldFont;
	BFont				fNormalFont;
	rgb_color			fSectionColor;
	rgb_color			fLabelColor;
	rgb_color			fValueColor;
};

#endif // DRIVER_INFO_VIEW_H
