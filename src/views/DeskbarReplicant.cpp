/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "DeskbarReplicant.h"

#include <Application.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>

static const char* kReplicantName = "BubiCam";
static const char* kAppSignature = "application/x-vnd.BubiCam";

// Deskbar tray icon size
static const float kIconSize = 16.0f;


DeskbarReplicant::DeskbarReplicant(BRect frame, const char* name)
	:
	BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW),
	fStatus(WEBCAM_STATUS_IDLE),
	fFPS(0.0f),
	fDeviceName(""),
	fPulseRunner(NULL),
	fBlinkOn(true)
{
}


DeskbarReplicant::DeskbarReplicant(BMessage* archive)
	:
	BView(archive),
	fStatus(WEBCAM_STATUS_IDLE),
	fFPS(0.0f),
	fDeviceName(""),
	fPulseRunner(NULL),
	fBlinkOn(true)
{
	int32 status;
	if (archive->FindInt32("webcam_status", &status) == B_OK)
		fStatus = (webcam_status_t)status;
	archive->FindFloat("webcam_fps", &fFPS);
	const char* name;
	if (archive->FindString("webcam_device", &name) == B_OK)
		fDeviceName = name;
}


DeskbarReplicant::~DeskbarReplicant()
{
	delete fPulseRunner;
}


BArchivable*
DeskbarReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DeskbarReplicant"))
		return NULL;
	return new DeskbarReplicant(archive);
}


status_t
DeskbarReplicant::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);
	if (status != B_OK)
		return status;

	data->AddString("class", "DeskbarReplicant");
	data->AddString("add_on", kAppSignature);
	data->AddInt32("webcam_status", (int32)fStatus);
	data->AddFloat("webcam_fps", fFPS);
	data->AddString("webcam_device", fDeviceName.String());

	return B_OK;
}


void
DeskbarReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();

	AdoptParentColors();

	// Pulse timer for blinking during recording
	BMessage pulseMsg(MSG_REPLICANT_PULSE);
	fPulseRunner = new BMessageRunner(BMessenger(this), &pulseMsg,
		500000);  // 500ms
}


void
DeskbarReplicant::DetachedFromWindow()
{
	delete fPulseRunner;
	fPulseRunner = NULL;
	BView::DetachedFromWindow();
}


void
DeskbarReplicant::Draw(BRect updateRect)
{
	_DrawIcon();
}


void
DeskbarReplicant::_DrawIcon()
{
	BRect bounds = Bounds();
	rgb_color bgColor;
	if (Parent() != NULL)
		bgColor = Parent()->ViewColor();
	else
		bgColor = ui_color(B_PANEL_BACKGROUND_COLOR);

	// Clear background
	SetHighColor(bgColor);
	FillRect(bounds);

	// Draw camera body (rounded rect)
	float cx = bounds.Width() / 2;
	float cy = bounds.Height() / 2;

	BRect body(cx - 6, cy - 4, cx + 6, cy + 4);
	SetHighColor(_StatusColor());
	FillRoundRect(body, 2, 2);

	// Draw lens circle
	SetHighColor(bgColor);
	FillEllipse(BPoint(cx, cy), 2.5f, 2.5f);

	// Draw lens inner
	SetHighColor(_StatusColor());
	FillEllipse(BPoint(cx, cy), 1.5f, 1.5f);

	// Draw flash/viewfinder bump
	BRect bump(cx - 2, cy - 6, cx + 2, cy - 4);
	SetHighColor(_StatusColor());
	FillRect(bump);

	// Recording indicator (red dot, blinking)
	if (fStatus == WEBCAM_STATUS_RECORDING && fBlinkOn) {
		SetHighColor(make_color(255, 0, 0));
		FillEllipse(BPoint(bounds.right - 3, bounds.top + 3), 2, 2);
	}

	// Streaming indicator (green dot)
	if (fStatus == WEBCAM_STATUS_STREAMING) {
		SetHighColor(make_color(0, 200, 0));
		FillEllipse(BPoint(bounds.right - 3, bounds.top + 3), 2, 2);
	}
}


rgb_color
DeskbarReplicant::_StatusColor() const
{
	switch (fStatus) {
		case WEBCAM_STATUS_STREAMING:
			return make_color(60, 140, 60);		// Green
		case WEBCAM_STATUS_RECORDING:
			return make_color(180, 40, 40);		// Red
		case WEBCAM_STATUS_ERROR:
			return make_color(180, 120, 0);		// Orange
		case WEBCAM_STATUS_IDLE:
		default:
			return make_color(120, 120, 120);	// Gray
	}
}


void
DeskbarReplicant::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_REPLICANT_UPDATE:
		{
			int32 status;
			if (message->FindInt32("status", &status) == B_OK)
				fStatus = (webcam_status_t)status;
			float fps;
			if (message->FindFloat("fps", &fps) == B_OK)
				fFPS = fps;
			const char* name;
			if (message->FindString("device", &name) == B_OK)
				fDeviceName = name;
			Invalidate();
			break;
		}

		case MSG_REPLICANT_PULSE:
			if (fStatus == WEBCAM_STATUS_RECORDING) {
				fBlinkOn = !fBlinkOn;
				Invalidate();
			}
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
DeskbarReplicant::MouseDown(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("BubiCam", false, false);

	// Status info
	BString statusStr;
	switch (fStatus) {
		case WEBCAM_STATUS_IDLE:
			statusStr = "Idle";
			break;
		case WEBCAM_STATUS_STREAMING:
			statusStr.SetToFormat("Streaming (%.1f fps)", fFPS);
			break;
		case WEBCAM_STATUS_RECORDING:
			statusStr.SetToFormat("Recording (%.1f fps)", fFPS);
			break;
		case WEBCAM_STATUS_ERROR:
			statusStr = "Error";
			break;
	}

	BMenuItem* statusItem = new BMenuItem(statusStr.String(), NULL);
	statusItem->SetEnabled(false);
	menu->AddItem(statusItem);

	if (fDeviceName.Length() > 0) {
		BMenuItem* devItem = new BMenuItem(fDeviceName.String(), NULL);
		devItem->SetEnabled(false);
		menu->AddItem(devItem);
	}

	menu->AddSeparatorItem();

	// Launch/activate BubiCam
	BMenuItem* openItem = new BMenuItem("Open BubiCam",
		new BMessage('open'));
	openItem->SetTarget(this);
	menu->AddItem(openItem);

	// Remove from Deskbar
	BMenuItem* removeItem = new BMenuItem("Remove from Deskbar",
		new BMessage('rmdb'));
	removeItem->SetTarget(this);
	menu->AddItem(removeItem);

	ConvertToScreen(&where);
	BMenuItem* selected = menu->Go(where, false, true);

	if (selected != NULL) {
		if (selected->Message()->what == 'open') {
			// Launch or activate BubiCam
			be_roster->Launch(kAppSignature);
		} else if (selected->Message()->what == 'rmdb') {
			RemoveFromDeskbar();
		}
	}

	delete menu;
}


void
DeskbarReplicant::SetStatus(webcam_status_t status)
{
	fStatus = status;
	Invalidate();
}


void
DeskbarReplicant::SetFPS(float fps)
{
	fFPS = fps;
}


void
DeskbarReplicant::SetDeviceName(const char* name)
{
	fDeviceName = name;
}


// Static methods for Deskbar integration

status_t
DeskbarReplicant::InstallInDeskbar()
{
	// Remove existing first
	RemoveFromDeskbar();

	BDeskbar deskbar;

	// BDeskbar::AddItem(BView*, int32*) installs a replicant from a view
	BRect frame(0, 0, kIconSize - 1, kIconSize - 1);
	DeskbarReplicant* view = new DeskbarReplicant(frame, kReplicantName);

	int32 id = -1;
	status_t status = deskbar.AddItem(view, &id);
	delete view;

	return status;
}


status_t
DeskbarReplicant::RemoveFromDeskbar()
{
	BDeskbar deskbar;
	return deskbar.RemoveItem(kReplicantName);
}


bool
DeskbarReplicant::IsInstalledInDeskbar()
{
	BDeskbar deskbar;
	return deskbar.HasItem(kReplicantName);
}
