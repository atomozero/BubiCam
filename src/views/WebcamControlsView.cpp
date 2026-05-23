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
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Alert.h>

#include <stdio.h>


WebcamControlsView::WebcamControlsView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fDevice(NULL),
	fTarget(NULL),
	fControlsContainer(NULL),
	fNoControlsLabel(NULL),
	fButtonBar(NULL),
	fLoadBar(NULL)
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

	// "Load Controls" bar - shown when device selected but controls not loaded
	fLoadBar = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(new BButton("refreshButton", "Load Controls",
			new BMessage(MSG_CONTROL_REFRESH)))
		.View();

	// Action button bar - shown only when controls are loaded
	fButtonBar = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(new BButton("lockAE", "Lock AE",
				new BMessage(MSG_LOCK_AE)))
			.Add(new BButton("lockAWB", "Lock AWB",
				new BMessage(MSG_LOCK_AWB)))
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(new BButton("resetButton", "Reset",
				new BMessage(MSG_CONTROL_RESET)))
			.Add(new BButton("refreshButton2", "Reload",
				new BMessage(MSG_CONTROL_REFRESH)))
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(new BButton("savePreset", "Save Preset",
				new BMessage(MSG_PRESET_SAVE)))
			.Add(new BButton("loadPreset", "Load Preset",
				new BMessage(MSG_PRESET_LOAD)))
		.End()
		.View();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fNoControlsLabel)
		.Add(fControlsContainer)
		.AddGlue()
		.Add(fLoadBar)
		.Add(fButtonBar);

	// Initial state: nothing visible except the label
	fControlsContainer->Hide();
	fButtonBar->Hide();
	fLoadBar->Hide();
}


WebcamControlsView::~WebcamControlsView()
{
	_ClearControls();
}


static void
_SetTargetsRecursive(BView* root, BHandler* target)
{
	for (int32 i = 0; i < root->CountChildren(); i++) {
		BView* child = root->ChildAt(i);
		BButton* btn = dynamic_cast<BButton*>(child);
		if (btn != NULL)
			btn->SetTarget(target);
		else
			_SetTargetsRecursive(child, target);
	}
}


void
WebcamControlsView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_SetTargetsRecursive(this, this);
}


void
WebcamControlsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_CONTROL_CHANGED:
		{
			int32 paramId;
			float value;
			int32 index;
			if (message->FindInt32("param_id", &paramId) == B_OK) {
				// Menu items use "index", sliders use "be:value", checkboxes need special handling
				if (message->FindInt32("index", &index) == B_OK) {
					// Menu item selected
					_ApplyControlValue(paramId, (float)index);
				} else if (message->FindFloat("value", &value) == B_OK) {
					// Slider value
					_ApplyControlValue(paramId, value);
				} else if (message->FindInt32("be:value", &index) == B_OK) {
					// BSlider sends value as int32 in "be:value"
					_ApplyControlValue(paramId, (float)index);
				} else {
					// Checkbox - get value from control (it's in fControlsContainer)
					const char* paramName;
					if (message->FindString("param_name", &paramName) == B_OK) {
						BCheckBox* cb = dynamic_cast<BCheckBox*>(
							fControlsContainer->FindView(paramName));
						if (cb != NULL) {
							_ApplyControlValue(paramId, cb->Value() ? 1.0f : 0.0f);
						}
					}
				}
			}
			break;
		}

		case MSG_CONTROL_RESET:
			_ResetToDefaults();
			break;

		case MSG_CONTROL_REFRESH:
			_BuildControls();
			break;

		case MSG_PRESET_SAVE:
		{
			BPath dir = _PresetsDirectory();
			BMessenger messenger(this);
			BFilePanel* panel = new BFilePanel(B_SAVE_PANEL, &messenger,
				NULL, 0, false, new BMessage(MSG_PRESET_SAVED));
			panel->SetPanelDirectory(dir.Path());
			panel->SetSaveText("controls.bcpreset");
			panel->Show();
			break;
		}

		case MSG_PRESET_LOAD:
		{
			BPath dir = _PresetsDirectory();
			BMessenger messenger(this);
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL, &messenger,
				NULL, 0, false, new BMessage(MSG_PRESET_LOADED));
			panel->SetPanelDirectory(dir.Path());
			panel->Show();
			break;
		}

		case MSG_LOCK_AE:
			_ToggleAutoParam("Auto Exposure");
			_ToggleAutoParam("auto_exposure");
			break;

		case MSG_LOCK_AWB:
			_ToggleAutoParam("Auto White Balance");
			_ToggleAutoParam("auto_wb");
			break;

		case MSG_PRESET_SAVED:
		{
			entry_ref ref;
			const char* name;
			if (message->FindRef("directory", &ref) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BPath path(&ref);
				path.Append(name);
				status_t status = SavePreset(path.Path());
				if (status == B_OK) {
					BAlert* alert = new BAlert("Preset",
						"Controls preset saved.", "OK");
					alert->Go(NULL);
				}
			}
			break;
		}

		case MSG_PRESET_LOADED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				status_t status = LoadPreset(path.Path());
				if (status == B_OK) {
					BAlert* alert = new BAlert("Preset",
						"Controls preset loaded.", "OK");
					alert->Go(NULL);
				}
			}
			break;
		}

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
	} else {
		fNoControlsLabel->SetText(
			"No controls available.\n"
			"Select a webcam to see its controls.");
	}
	_UpdateButtonVisibility();
}


void
WebcamControlsView::Clear()
{
	fDevice = NULL;
	_ClearControls();
	fNoControlsLabel->SetText(
		"No controls available.\n"
		"Select a webcam to see its controls.");
	_UpdateButtonVisibility();
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
		_UpdateButtonVisibility();
		return;
	}

	// Check if the node is instantiated - required for GetParameterWebFor
	if (!fDevice->IsNodeInstantiated()) {
		fNoControlsLabel->SetText("Webcam not active.\nStart preview to see controls.");
		_UpdateButtonVisibility();
		return;
	}

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fNoControlsLabel->SetText("Media Kit not available.");
		_UpdateButtonVisibility();
		return;
	}

	// Get parameter web from device
	BParameterWeb* web = NULL;
	status_t status = roster->GetParameterWebFor(fDevice->MediaNode(), &web);

	fprintf(stderr, "WebcamControlsView: GetParameterWebFor returned %ld, web=%p\n",
		(long)status, web);
	if (web != NULL) {
		fprintf(stderr, "WebcamControlsView: Parameter web has %ld parameters\n",
			(long)web->CountParameters());
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* p = web->ParameterAt(i);
			if (p != NULL) {
				fprintf(stderr, "  Param %d: name='%s', type=%d, id=%ld\n",
					(int)i, p->Name(), (int)p->Type(), (long)p->ID());
				if (p->Type() == BParameter::B_DISCRETE_PARAMETER) {
					BDiscreteParameter* disc = dynamic_cast<BDiscreteParameter*>(p);
					if (disc != NULL) {
						fprintf(stderr, "    -> Discrete param with %ld items\n",
							(long)disc->CountItems());
					}
				}
			}
		}
	}

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

	_UpdateButtonVisibility();
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
	info->defaultValue = current;
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
	info->defaultValue = current ? 1.0f : 0.0f;
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
	info->defaultValue = (float)current;
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

	// GetParameterWebFor only works on instantiated (non-dormant) nodes
	if (!fDevice->IsNodeInstantiated())
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


void
WebcamControlsView::_UpdateButtonVisibility()
{
	bool hasControls = fControls.CountItems() > 0;
	bool hasDevice = fDevice != NULL;

	// Controls container: show only when controls are loaded
	if (hasControls) {
		if (fControlsContainer->IsHidden())
			fControlsContainer->Show();
	} else {
		if (!fControlsContainer->IsHidden())
			fControlsContainer->Hide();
	}

	// Label: show when no controls loaded
	if (hasControls) {
		if (!fNoControlsLabel->IsHidden())
			fNoControlsLabel->Hide();
	} else {
		if (fNoControlsLabel->IsHidden())
			fNoControlsLabel->Show();
	}

	// Load bar: show only when device present but no controls loaded
	if (hasDevice && !hasControls) {
		if (fLoadBar->IsHidden())
			fLoadBar->Show();
	} else {
		if (!fLoadBar->IsHidden())
			fLoadBar->Hide();
	}

	// Button bar (reset, lock, presets): show only when controls loaded
	if (hasControls) {
		if (fButtonBar->IsHidden())
			fButtonBar->Show();
	} else {
		if (!fButtonBar->IsHidden())
			fButtonBar->Hide();
	}
}


void
WebcamControlsView::_ResetToDefaults()
{
	for (int32 i = 0; i < fControls.CountItems(); i++) {
		ControlInfo* info = fControls.ItemAt(i);
		if (info == NULL)
			continue;

		float defVal = info->defaultValue;

		// Update UI
		switch (info->type) {
			case CONTROL_SLIDER:
			{
				BSlider* slider = dynamic_cast<BSlider*>(info->view);
				if (slider != NULL)
					slider->SetValue((int32)defVal);
				break;
			}
			case CONTROL_CHECKBOX:
			{
				BCheckBox* cb = dynamic_cast<BCheckBox*>(info->view);
				if (cb != NULL)
					cb->SetValue(defVal != 0 ? B_CONTROL_ON : B_CONTROL_OFF);
				break;
			}
			case CONTROL_MENU:
			{
				BMenuField* field = dynamic_cast<BMenuField*>(info->view);
				if (field != NULL && field->Menu() != NULL) {
					BMenuItem* item = field->Menu()->ItemAt((int32)defVal);
					if (item != NULL)
						item->SetMarked(true);
				}
				break;
			}
		}

		// Apply to driver
		_ApplyControlValue(info->parameterId, defVal);
		info->currentValue = defVal;
	}
}


status_t
WebcamControlsView::SavePreset(const char* path)
{
	BMessage preset;
	preset.AddString("preset_type", "BubiCam_controls");
	preset.AddInt32("version", 1);

	for (int32 i = 0; i < fControls.CountItems(); i++) {
		ControlInfo* info = fControls.ItemAt(i);
		if (info == NULL)
			continue;

		BMessage control;
		control.AddString("name", info->name);
		control.AddString("param", info->parameterName);
		control.AddInt32("type", info->type);
		control.AddInt32("param_id", info->parameterId);
		control.AddFloat("value", info->currentValue);
		control.AddFloat("min", info->minValue);
		control.AddFloat("max", info->maxValue);

		preset.AddMessage("control", &control);
	}

	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	return preset.Flatten(&file);
}


status_t
WebcamControlsView::LoadPreset(const char* path)
{
	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMessage preset;
	status_t status = preset.Unflatten(&file);
	if (status != B_OK)
		return status;

	// Verify it's a valid preset
	const char* type;
	if (preset.FindString("preset_type", &type) != B_OK
		|| strcmp(type, "BubiCam_controls") != 0)
		return B_BAD_DATA;

	// Apply each control value
	BMessage control;
	for (int32 i = 0; preset.FindMessage("control", i, &control) == B_OK; i++) {
		const char* paramName;
		float value;
		if (control.FindString("param", &paramName) != B_OK
			|| control.FindFloat("value", &value) != B_OK)
			continue;

		// Find matching control by parameter name
		for (int32 j = 0; j < fControls.CountItems(); j++) {
			ControlInfo* info = fControls.ItemAt(j);
			if (info == NULL || info->parameterName != paramName)
				continue;

			// Update UI
			switch (info->type) {
				case CONTROL_SLIDER:
				{
					BSlider* slider = dynamic_cast<BSlider*>(info->view);
					if (slider != NULL)
						slider->SetValue((int32)value);
					break;
				}
				case CONTROL_CHECKBOX:
				{
					BCheckBox* cb = dynamic_cast<BCheckBox*>(info->view);
					if (cb != NULL)
						cb->SetValue(value != 0 ? B_CONTROL_ON : B_CONTROL_OFF);
					break;
				}
				case CONTROL_MENU:
				{
					BMenuField* field = dynamic_cast<BMenuField*>(info->view);
					if (field != NULL && field->Menu() != NULL) {
						BMenuItem* item = field->Menu()->ItemAt((int32)value);
						if (item != NULL)
							item->SetMarked(true);
					}
					break;
				}
			}

			// Apply to driver
			_ApplyControlValue(info->parameterId, value);
			info->currentValue = value;
			break;
		}
	}

	return B_OK;
}


void
WebcamControlsView::_ToggleAutoParam(const char* paramName)
{
	for (int32 i = 0; i < fControls.CountItems(); i++) {
		ControlInfo* info = fControls.ItemAt(i);
		if (info == NULL || info->type != CONTROL_CHECKBOX)
			continue;

		if (info->parameterName.ICompare(paramName) != 0
			&& info->name.ICompare(paramName) != 0)
			continue;

		// Toggle the value
		float newValue = (info->currentValue != 0) ? 0.0f : 1.0f;

		BCheckBox* cb = dynamic_cast<BCheckBox*>(info->view);
		if (cb != NULL)
			cb->SetValue(newValue != 0 ? B_CONTROL_ON : B_CONTROL_OFF);

		_ApplyControlValue(info->parameterId, newValue);
		info->currentValue = newValue;
		return;
	}
}


BPath
WebcamControlsView::_PresetsDirectory()
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("BubiCam/presets");

	// Create directory if it doesn't exist
	BDirectory dir;
	if (dir.SetTo(path.Path()) != B_OK)
		create_directory(path.Path(), 0755);

	return path;
}
