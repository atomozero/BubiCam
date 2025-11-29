/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef WEBCAM_CONTROLS_VIEW_H
#define WEBCAM_CONTROLS_VIEW_H

#include <View.h>
#include <Slider.h>
#include <CheckBox.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <Button.h>
#include <StringView.h>
#include <ObjectList.h>
#include <ParameterWeb.h>

class WebcamDevice;

// Control types
enum ControlType {
	CONTROL_SLIDER,
	CONTROL_CHECKBOX,
	CONTROL_MENU
};

// Generic control info
struct ControlInfo {
	BString			name;
	BString			parameterName;
	ControlType		type;
	float			minValue;
	float			maxValue;
	float			currentValue;
	BView*			view;
	int32			parameterId;

	ControlInfo()
		: type(CONTROL_SLIDER), minValue(0), maxValue(100),
		  currentValue(50), view(NULL), parameterId(-1) {}
};


class WebcamControlsView : public BView {
public:
						WebcamControlsView(const char* name);
	virtual				~WebcamControlsView();

	virtual void		AttachedToWindow();
	virtual void		MessageReceived(BMessage* message);

	void				SetDevice(WebcamDevice* device);
	void				Clear();
	void				RefreshControls();

	// Control change callback
	void				SetTarget(BHandler* target) { fTarget = target; }

private:
	void				_BuildControls();
	void				_AddSliderControl(const char* label, const char* param,
							float min, float max, float current, int32 paramId);
	void				_AddCheckboxControl(const char* label, const char* param,
							bool current, int32 paramId);
	void				_AddMenuControl(const char* label, const char* param,
							const BObjectList<BString>& options, int32 current,
							int32 paramId);
	void				_ClearControls();
	void				_ApplyControlValue(int32 paramId, float value);

	WebcamDevice*		fDevice;
	BHandler*			fTarget;
	BObjectList<ControlInfo> fControls;
	BView*				fControlsContainer;
	BStringView*		fNoControlsLabel;
};

// Message constants for controls
enum {
	MSG_CONTROL_CHANGED		= 'ctch',
	MSG_CONTROL_RESET		= 'ctrs',
	MSG_CONTROL_REFRESH		= 'ctrf'
};

#endif // WEBCAM_CONTROLS_VIEW_H
