/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "WebcamControlsView.h"
#include "WebcamDevice.h"

#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <SpaceLayoutItem.h>
#include <SeparatorView.h>
#include <ScrollView.h>
#include <MediaRoster.h>

#include <stdio.h>


WebcamControlsView::WebcamControlsView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fDevice(NULL),
	fTarget(NULL),
	fControlsContainer(NULL),
	fNoControlsLabel(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
	SetLayout(layout);

	fControlsContainer = new BView("controlsContainer", B_WILL_DRAW);
	fControlsContainer->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fControlsContainer->SetLayout(new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING));

	fNoControlsLabel = new BStringView("noControls",
		"No controls available.\nSelect a webcam to see its controls.");
	fNoControlsLabel->SetAlignment(B_ALIGN_CENTER);

	// Control buttons
	BButton* resetButton = new BButton("resetButton", "Reset All",
		new BMessage(MSG_CONTROL_RESET));
	BButton* refreshButton = new BButton("refreshButton", "Load Controls",
		new BMessage(MSG_CONTROL_REFRESH));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fNoControlsLabel)
		.Add(fControlsContainer)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.Add(refreshButton)
			.Add(resetButton)
		.End();

	fControlsContainer->Hide();
}


WebcamControlsView::~WebcamControlsView()
{
	_ClearControls();
}


void
WebcamControlsView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Set button targets
	BButton* resetButton = dynamic_cast<BButton*>(FindView("resetButton"));
	if (resetButton != NULL)
		resetButton->SetTarget(this);

	BButton* refreshButton = dynamic_cast<BButton*>(FindView("refreshButton"));
	if (refreshButton != NULL)
		refreshButton->SetTarget(this);
}


void
WebcamControlsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_CONTROL_CHANGED:
		{
			int32 paramId;
			float value;
			if (message->FindInt32("param_id", &paramId) == B_OK &&
				message->FindFloat("value", &value) == B_OK) {
				_ApplyControlValue(paramId, value);
			}
			break;
		}

		case MSG_CONTROL_RESET:
			RefreshControls();
			break;

		case MSG_CONTROL_REFRESH:
			_BuildControls();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
WebcamControlsView::SetDevice(WebcamDevice* device)
{
	fDevice = device;
	// Don't automatically read controls - this can crash buggy drivers
	// User must click "Load Controls" button manually
	_ClearControls();
	if (fDevice != NULL) {
		fNoControlsLabel->SetText(
			"Controls not loaded.\n"
			"Click 'Load Controls' to read driver parameters.\n\n"
			"WARNING: Some drivers may crash when reading parameters.");
		fNoControlsLabel->Show();
		fControlsContainer->Hide();
	}
}


void
WebcamControlsView::Clear()
{
	fDevice = NULL;
	_ClearControls();
	fNoControlsLabel->Show();
	fControlsContainer->Hide();
}


void
WebcamControlsView::RefreshControls()
{
	_BuildControls();
}


void
WebcamControlsView::_BuildControls()
{
	_ClearControls();

	if (fDevice == NULL) {
		fNoControlsLabel->SetText("No webcam selected.");
		fNoControlsLabel->Show();
		fControlsContainer->Hide();
		return;
	}

	// Check if the node is instantiated - required for GetParameterWebFor
	if (!fDevice->IsNodeInstantiated()) {
		fNoControlsLabel->SetText("Webcam not active.\nStart preview to see controls.");
		fNoControlsLabel->Show();
		fControlsContainer->Hide();
		return;
	}

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fNoControlsLabel->SetText("Media Kit not available.");
		fNoControlsLabel->Show();
		fControlsContainer->Hide();
		return;
	}

	// Get parameter web from device
	BParameterWeb* web = NULL;
	status_t status = roster->GetParameterWebFor(fDevice->MediaNode(), &web);

	if (status != B_OK || web == NULL) {
		// Add default controls that most webcams support
		_AddSliderControl("Brightness", "brightness", 0, 255, 128, 1);
		_AddSliderControl("Contrast", "contrast", 0, 255, 128, 2);
		_AddSliderControl("Saturation", "saturation", 0, 255, 128, 3);
		_AddSliderControl("Hue", "hue", -180, 180, 0, 4);
		_AddSliderControl("Gamma", "gamma", 1, 500, 100, 5);
		_AddSliderControl("Sharpness", "sharpness", 0, 255, 128, 6);
		_AddCheckboxControl("Auto White Balance", "auto_wb", true, 7);
		_AddCheckboxControl("Auto Exposure", "auto_exposure", true, 8);
		_AddSliderControl("Exposure", "exposure", 1, 10000, 500, 9);
		_AddSliderControl("Gain", "gain", 0, 255, 64, 10);

		fNoControlsLabel->SetText(
			"Using default controls.\n"
			"Actual driver parameters not available.");
		fNoControlsLabel->Show();
	} else {
		// Parse parameter web and create controls
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* param = web->ParameterAt(i);
			if (param == NULL)
				continue;

			// Skip groups
			if (param->Type() == BParameter::B_NULL_PARAMETER)
				continue;

			const char* name = param->Name();
			int32 paramId = param->ID();

			if (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
				BContinuousParameter* cont =
					dynamic_cast<BContinuousParameter*>(param);
				if (cont != NULL) {
					float min = cont->MinValue();
					float max = cont->MaxValue();
					float current = (min + max) / 2;

					// Try to get current value
					bigtime_t lastChange;
					size_t size = sizeof(float);
					cont->GetValue(&current, &size, &lastChange);

					_AddSliderControl(name, name, min, max, current, paramId);
				}
			} else if (param->Type() == BParameter::B_DISCRETE_PARAMETER) {
				BDiscreteParameter* disc =
					dynamic_cast<BDiscreteParameter*>(param);
				if (disc != NULL) {
					int32 count = disc->CountItems();
					if (count == 2) {
						// Likely a boolean
						int32 current = 0;
						bigtime_t lastChange;
						size_t size = sizeof(int32);
						disc->GetValue(&current, &size, &lastChange);
						_AddCheckboxControl(name, name, current != 0, paramId);
					} else if (count > 2) {
						// Menu
						BObjectList<BString> options;
						for (int32 j = 0; j < count; j++) {
							options.AddItem(new BString(disc->ItemNameAt(j)));
						}
						int32 current = 0;
						bigtime_t lastChange;
						size_t size = sizeof(int32);
						disc->GetValue(&current, &size, &lastChange);
						_AddMenuControl(name, name, options, current, paramId);

						// Clean up
						for (int32 j = 0; j < options.CountItems(); j++)
							delete options.ItemAt(j);
					}
				}
			}
		}

		delete web;

		if (fControls.CountItems() == 0) {
			fNoControlsLabel->SetText("No adjustable parameters found.");
			fNoControlsLabel->Show();
		} else {
			fNoControlsLabel->Hide();
		}
	}

	if (fControls.CountItems() > 0)
		fControlsContainer->Show();
	else
		fControlsContainer->Hide();

	InvalidateLayout();
	Invalidate();
}


void
WebcamControlsView::_AddSliderControl(const char* label, const char* param,
	float min, float max, float current, int32 paramId)
{
	BMessage* message = new BMessage(MSG_CONTROL_CHANGED);
	message->AddInt32("param_id", paramId);
	message->AddString("param_name", param);

	BSlider* slider = new BSlider(param, label, message,
		(int32)min, (int32)max, B_HORIZONTAL);
	slider->SetValue((int32)current);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels(BString().SetToFormat("%.0f", min).String(),
		BString().SetToFormat("%.0f", max).String());
	slider->SetModificationMessage(new BMessage(*message));
	slider->SetTarget(this);

	fControlsContainer->AddChild(slider);

	ControlInfo* info = new ControlInfo();
	info->name = label;
	info->parameterName = param;
	info->type = CONTROL_SLIDER;
	info->minValue = min;
	info->maxValue = max;
	info->currentValue = current;
	info->view = slider;
	info->parameterId = paramId;
	fControls.AddItem(info);
}


void
WebcamControlsView::_AddCheckboxControl(const char* label, const char* param,
	bool current, int32 paramId)
{
	BMessage* message = new BMessage(MSG_CONTROL_CHANGED);
	message->AddInt32("param_id", paramId);
	message->AddString("param_name", param);

	BCheckBox* checkbox = new BCheckBox(param, label, message);
	checkbox->SetValue(current ? B_CONTROL_ON : B_CONTROL_OFF);
	checkbox->SetTarget(this);

	fControlsContainer->AddChild(checkbox);

	ControlInfo* info = new ControlInfo();
	info->name = label;
	info->parameterName = param;
	info->type = CONTROL_CHECKBOX;
	info->currentValue = current ? 1.0f : 0.0f;
	info->view = checkbox;
	info->parameterId = paramId;
	fControls.AddItem(info);
}


void
WebcamControlsView::_AddMenuControl(const char* label, const char* param,
	const BObjectList<BString>& options, int32 current, int32 paramId)
{
	BPopUpMenu* menu = new BPopUpMenu(label);

	for (int32 i = 0; i < options.CountItems(); i++) {
		BString* option = options.ItemAt(i);
		if (option != NULL) {
			BMessage* itemMsg = new BMessage(MSG_CONTROL_CHANGED);
			itemMsg->AddInt32("param_id", paramId);
			itemMsg->AddString("param_name", param);
			itemMsg->AddInt32("index", i);

			BMenuItem* item = new BMenuItem(option->String(), itemMsg);
			if (i == current)
				item->SetMarked(true);
			menu->AddItem(item);
		}
	}

	BMenuField* field = new BMenuField(param, label, menu);
	menu->SetTargetForItems(this);

	fControlsContainer->AddChild(field);

	ControlInfo* info = new ControlInfo();
	info->name = label;
	info->parameterName = param;
	info->type = CONTROL_MENU;
	info->currentValue = (float)current;
	info->view = field;
	info->parameterId = paramId;
	fControls.AddItem(info);
}


void
WebcamControlsView::_ClearControls()
{
	// Remove all control views
	while (fControlsContainer->CountChildren() > 0) {
		BView* child = fControlsContainer->ChildAt(0);
		fControlsContainer->RemoveChild(child);
		delete child;
	}

	// Clear control info list
	for (int32 i = 0; i < fControls.CountItems(); i++)
		delete fControls.ItemAt(i);
	fControls.MakeEmpty();
}


void
WebcamControlsView::_ApplyControlValue(int32 paramId, float value)
{
	if (fDevice == NULL)
		return;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	BParameterWeb* web = NULL;
	status_t status = roster->GetParameterWebFor(fDevice->MediaNode(), &web);
	if (status != B_OK || web == NULL)
		return;

	// Find parameter and set value
	for (int32 i = 0; i < web->CountParameters(); i++) {
		BParameter* param = web->ParameterAt(i);
		if (param != NULL && param->ID() == paramId) {
			if (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
				BContinuousParameter* cont =
					dynamic_cast<BContinuousParameter*>(param);
				if (cont != NULL) {
					cont->SetValue(&value, sizeof(float), system_time());
				}
			} else if (param->Type() == BParameter::B_DISCRETE_PARAMETER) {
				BDiscreteParameter* disc =
					dynamic_cast<BDiscreteParameter*>(param);
				if (disc != NULL) {
					int32 intValue = (int32)value;
					disc->SetValue(&intValue, sizeof(int32), system_time());
				}
			}
			break;
		}
	}

	delete web;

	// Notify target
	if (fTarget != NULL) {
		BMessage msg(MSG_CONTROL_CHANGED);
		msg.AddInt32("param_id", paramId);
		msg.AddFloat("value", value);
		BMessenger(fTarget).SendMessage(&msg);
	}
}
