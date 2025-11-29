/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "MainWindow.h"
#include "VideoPreviewView.h"
#include "DriverInfoView.h"
#include "SyslogView.h"
#include "VUMeterView.h"
#include "WebcamRoster.h"
#include "WebcamDevice.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <SplitView.h>
#include <ScrollView.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <Box.h>
#include <Catalog.h>
#include <Alert.h>

#include <stdio.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 1100, 750), "BubiCam - Webcam Driver Tester",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fMenuBar(NULL),
	fWebcamMenu(NULL),
	fControlMenu(NULL),
	fVideoPreview(NULL),
	fDriverInfo(NULL),
	fSyslogView(NULL),
	fVUMeter(NULL),
	fStatusBar(NULL),
	fWebcamRoster(NULL),
	fCurrentWebcam(NULL),
	fCurrentWebcamIndex(-1),
	fIsPreviewActive(false)
{
	fWebcamRoster = new WebcamRoster();

	_BuildMenu();
	_BuildLayout();
	_PopulateWebcamMenu();

	// Start syslog monitoring
	fSyslogView->StartMonitoring();
}


MainWindow::~MainWindow()
{
	_StopPreview();

	if (fSyslogView != NULL)
		fSyslogView->StopMonitoring();

	delete fWebcamRoster;
}


void
MainWindow::_BuildMenu()
{
	fMenuBar = new BMenuBar("menubar");

	// File menu
	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("About BubiCam" B_UTF8_ELLIPSIS,
		new BMessage(MSG_ABOUT)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED),
		'Q'));
	fMenuBar->AddItem(fileMenu);

	// Webcam menu
	fWebcamMenu = new BMenu("Webcam");
	fWebcamMenu->AddItem(new BMenuItem("Refresh Devices",
		new BMessage(MSG_REFRESH_DEVICES), 'R'));
	fWebcamMenu->AddSeparatorItem();
	fMenuBar->AddItem(fWebcamMenu);

	// Control menu
	fControlMenu = new BMenu("Control");
	fControlMenu->AddItem(new BMenuItem("Start Preview",
		new BMessage(MSG_WEBCAM_START), 'S'));
	fControlMenu->AddItem(new BMenuItem("Stop Preview",
		new BMessage(MSG_WEBCAM_STOP), 'T'));
	fMenuBar->AddItem(fControlMenu);
}


void
MainWindow::_BuildLayout()
{
	// Create views
	fVideoPreview = new VideoPreviewView("videoPreview");
	fDriverInfo = new DriverInfoView("driverInfo");
	fSyslogView = new SyslogView("syslogView");
	fVUMeter = new VUMeterView("vuMeter");

	// Status bar
	fStatusBar = new BStringView("statusBar", "No webcam selected");
	fStatusBar->SetExplicitMinSize(BSize(B_SIZE_UNSET, 20));

	// Create boxes for organization
	BBox* videoBox = new BBox("videoBox");
	videoBox->SetLabel("Video Preview");
	videoBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(fVideoPreview)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	BBox* infoBox = new BBox("infoBox");
	infoBox->SetLabel("Driver Information");
	BScrollView* infoScroll = new BScrollView("infoScroll", fDriverInfo,
		0, false, true);
	infoBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(infoScroll)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	BBox* syslogBox = new BBox("syslogBox");
	syslogBox->SetLabel("Syslog Monitor (USB/Webcam)");
	BScrollView* syslogScroll = new BScrollView("syslogScroll", fSyslogView,
		0, false, true);
	syslogBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(syslogScroll)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	BBox* vuBox = new BBox("vuBox");
	vuBox->SetLabel("Microphone Level");
	vuBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(fVUMeter)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	// Right panel with info, syslog and VU meter
	BSplitView* rightSplit = new BSplitView(B_VERTICAL);
	rightSplit->AddChild(infoBox);
	rightSplit->AddChild(syslogBox);
	rightSplit->AddChild(vuBox);
	rightSplit->SetItemWeight(0, 0.35f, true);
	rightSplit->SetItemWeight(1, 0.50f, true);
	rightSplit->SetItemWeight(2, 0.15f, true);

	// Main horizontal split: video on left, info panel on right
	BSplitView* mainSplit = new BSplitView(B_HORIZONTAL);
	mainSplit->AddChild(videoBox);
	mainSplit->AddChild(rightSplit);
	mainSplit->SetItemWeight(0, 0.55f, true);
	mainSplit->SetItemWeight(1, 0.45f, true);

	// Main layout
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(mainSplit)
		.Add(fStatusBar)
		.SetInsets(0, 0, 0, 0);

	// Set minimum sizes
	videoBox->SetExplicitMinSize(BSize(320, 240));
	infoBox->SetExplicitMinSize(BSize(300, 150));
	syslogBox->SetExplicitMinSize(BSize(300, 150));
	vuBox->SetExplicitMinSize(BSize(300, 60));
}


void
MainWindow::_PopulateWebcamMenu()
{
	// Remove old webcam items (keep "Refresh Devices" and separator)
	while (fWebcamMenu->CountItems() > 2)
		delete fWebcamMenu->RemoveItem(2);

	// Refresh device list
	fWebcamRoster->EnumerateDevices();

	int32 count = fWebcamRoster->CountDevices();
	if (count == 0) {
		BMenuItem* noDevice = new BMenuItem("No webcams found", NULL);
		noDevice->SetEnabled(false);
		fWebcamMenu->AddItem(noDevice);
		fStatusBar->SetText("No webcams detected");
	} else {
		for (int32 i = 0; i < count; i++) {
			WebcamDevice* device = fWebcamRoster->DeviceAt(i);
			if (device != NULL) {
				BMessage* msg = new BMessage(MSG_WEBCAM_SELECTED);
				msg->AddInt32("index", i);
				BMenuItem* item = new BMenuItem(device->Name(), msg);
				fWebcamMenu->AddItem(item);
			}
		}
		fStatusBar->SetText("Select a webcam from the menu");
	}
}


void
MainWindow::_SelectWebcam(int32 index)
{
	_StopPreview();

	WebcamDevice* device = fWebcamRoster->DeviceAt(index);
	if (device == NULL) {
		fStatusBar->SetText("Error: Invalid webcam selection");
		return;
	}

	fCurrentWebcam = device;
	fCurrentWebcamIndex = index;

	// Update menu checkmarks
	for (int32 i = 2; i < fWebcamMenu->CountItems(); i++) {
		BMenuItem* item = fWebcamMenu->ItemAt(i);
		if (item != NULL)
			item->SetMarked(i - 2 == index);
	}

	_UpdateDriverInfo();

	BString status;
	status.SetToFormat("Selected: %s", device->Name());
	fStatusBar->SetText(status.String());
}


void
MainWindow::_StartPreview()
{
	if (fCurrentWebcam == NULL) {
		BAlert* alert = new BAlert("Error",
			"Please select a webcam first.", "OK");
		alert->Go();
		return;
	}

	if (fIsPreviewActive)
		return;

	status_t status = fCurrentWebcam->StartCapture(this);
	if (status != B_OK) {
		BString error;
		error.SetToFormat("Failed to start capture: %s", strerror(status));
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
		return;
	}

	fIsPreviewActive = true;
	fStatusBar->SetText("Preview active");

	// Update driver info with capture state
	_UpdateDriverInfo();
}


void
MainWindow::_StopPreview()
{
	if (fCurrentWebcam != NULL && fIsPreviewActive) {
		fCurrentWebcam->StopCapture();
		fIsPreviewActive = false;
		fVideoPreview->ClearFrame();
		fVUMeter->SetLevel(0.0f, 0.0f);

		if (fCurrentWebcam != NULL) {
			BString status;
			status.SetToFormat("Selected: %s (stopped)", fCurrentWebcam->Name());
			fStatusBar->SetText(status.String());
		}
	}
}


void
MainWindow::_UpdateDriverInfo()
{
	if (fCurrentWebcam == NULL)
		return;

	fDriverInfo->SetDevice(fCurrentWebcam, fIsPreviewActive);
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_ABOUT:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;

		case MSG_REFRESH_DEVICES:
			_PopulateWebcamMenu();
			break;

		case MSG_WEBCAM_SELECTED:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK)
				_SelectWebcam(index);
			break;
		}

		case MSG_WEBCAM_START:
			_StartPreview();
			break;

		case MSG_WEBCAM_STOP:
			_StopPreview();
			break;

		case MSG_FRAME_RECEIVED:
		{
			BBitmap* bitmap = NULL;
			if (message->FindPointer("bitmap", (void**)&bitmap) == B_OK) {
				fVideoPreview->SetFrame(bitmap);
			}
			break;
		}

		case MSG_AUDIO_LEVEL:
		{
			float left = 0.0f, right = 0.0f;
			message->FindFloat("left", &left);
			message->FindFloat("right", &right);
			fVUMeter->SetLevel(left, right);
			break;
		}

		case MSG_SYSLOG_UPDATE:
		{
			const char* text;
			if (message->FindString("text", &text) == B_OK)
				fSyslogView->AddLine(text);
			break;
		}

		case MSG_DRIVER_INFO_UPDATE:
			_UpdateDriverInfo();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
MainWindow::QuitRequested()
{
	_StopPreview();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
