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
#include "WebcamControlsView.h"
#include "WebcamRoster.h"
#include "WebcamDevice.h"
#include "ExportUtils.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <SplitView.h>
#include <ScrollView.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <Box.h>
#include <TabView.h>
#include <Catalog.h>
#include <Alert.h>
#include <Path.h>
#include <Entry.h>

#include <stdio.h>
#include <string.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 1200, 800), "BubiCam - Webcam Driver Tester",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fMenuBar(NULL),
	fWebcamMenu(NULL),
	fControlMenu(NULL),
	fFormatMenu(NULL),
	fToolsMenu(NULL),
	fVideoPreview(NULL),
	fDriverInfo(NULL),
	fSyslogView(NULL),
	fVUMeter(NULL),
	fWebcamControls(NULL),
	fStatusBar(NULL),
	fRightTabView(NULL),
	fWebcamRoster(NULL),
	fCurrentWebcam(NULL),
	fCurrentWebcamIndex(-1),
	fIsPreviewActive(false),
	fSavePanel(NULL),
	fLastFrame(NULL),
	fSavingJson(false)
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
	delete fSavePanel;
	delete fLastFrame;
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
	fileMenu->AddItem(new BMenuItem("Take Screenshot",
		new BMessage(MSG_SCREENSHOT), 'P'));
	fileMenu->AddItem(new BMenuItem("Export Info as Text" B_UTF8_ELLIPSIS,
		new BMessage(MSG_EXPORT_INFO), 'E'));
	fileMenu->AddItem(new BMenuItem("Export Info as JSON" B_UTF8_ELLIPSIS,
		new BMessage(MSG_EXPORT_INFO_JSON), 'E', B_SHIFT_KEY));
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

	// Format menu
	fFormatMenu = new BMenu("Format");
	fFormatMenu->SetEnabled(false);
	fMenuBar->AddItem(fFormatMenu);

	// Control menu
	fControlMenu = new BMenu("Control");
	fControlMenu->AddItem(new BMenuItem("Start Preview",
		new BMessage(MSG_WEBCAM_START), 'S'));
	fControlMenu->AddItem(new BMenuItem("Stop Preview",
		new BMessage(MSG_WEBCAM_STOP), 'T'));
	fControlMenu->AddSeparatorItem();
	fControlMenu->AddItem(new BMenuItem("Show Controls Panel",
		new BMessage(MSG_TOGGLE_CONTROLS), 'K'));
	fMenuBar->AddItem(fControlMenu);

	// Tools menu
	fToolsMenu = new BMenu("Tools");
	fToolsMenu->AddItem(new BMenuItem("Clear Syslog",
		new BMessage(MSG_CLEAR_SYSLOG), 'L'));
	fMenuBar->AddItem(fToolsMenu);
}


void
MainWindow::_BuildLayout()
{
	// Create views
	fVideoPreview = new VideoPreviewView("videoPreview");
	fDriverInfo = new DriverInfoView("driverInfo");
	fSyslogView = new SyslogView("syslogView");
	fVUMeter = new VUMeterView("vuMeter");
	fWebcamControls = new WebcamControlsView("webcamControls");

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

	// VU Meter box
	BBox* vuBox = new BBox("vuBox");
	vuBox->SetLabel("Microphone Level");
	vuBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(fVUMeter)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	// Left side: video + VU meter
	BSplitView* leftSplit = new BSplitView(B_VERTICAL);
	leftSplit->AddChild(videoBox);
	leftSplit->AddChild(vuBox);
	leftSplit->SetItemWeight(0, 0.85f, true);
	leftSplit->SetItemWeight(1, 0.15f, true);

	// Create tab view for right panel
	fRightTabView = new BTabView("rightTabs");

	// Driver Info tab
	BScrollView* infoScroll = new BScrollView("infoScroll", fDriverInfo,
		0, false, true);
	fRightTabView->AddTab(infoScroll, new BTab());
	fRightTabView->TabAt(0)->SetLabel("Driver Info");

	// Syslog tab
	BScrollView* syslogScroll = new BScrollView("syslogScroll", fSyslogView,
		0, false, true);
	fRightTabView->AddTab(syslogScroll, new BTab());
	fRightTabView->TabAt(1)->SetLabel("Syslog");

	// Controls tab
	BScrollView* controlsScroll = new BScrollView("controlsScroll",
		fWebcamControls, 0, false, true);
	fRightTabView->AddTab(controlsScroll, new BTab());
	fRightTabView->TabAt(2)->SetLabel("Controls");

	// Main horizontal split: left side on left, tabs on right
	BSplitView* mainSplit = new BSplitView(B_HORIZONTAL);
	mainSplit->AddChild(leftSplit);
	mainSplit->AddChild(fRightTabView);
	mainSplit->SetItemWeight(0, 0.55f, true);
	mainSplit->SetItemWeight(1, 0.45f, true);

	// Main layout
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(mainSplit)
		.Add(fStatusBar)
		.SetInsets(0, 0, 0, 0);

	// Set minimum sizes
	videoBox->SetExplicitMinSize(BSize(400, 300));
	vuBox->SetExplicitMinSize(BSize(400, 80));
	fRightTabView->SetExplicitMinSize(BSize(350, 400));
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
MainWindow::_PopulateFormatMenu()
{
	// Clear existing items
	while (fFormatMenu->CountItems() > 0)
		delete fFormatMenu->RemoveItem((int32)0);

	if (fCurrentWebcam == NULL) {
		fFormatMenu->SetEnabled(false);
		return;
	}

	const BObjectList<VideoFormat>& formats = fCurrentWebcam->SupportedFormats();
	if (formats.CountItems() == 0) {
		BMenuItem* noFormat = new BMenuItem("No formats available", NULL);
		noFormat->SetEnabled(false);
		fFormatMenu->AddItem(noFormat);
	} else {
		for (int32 i = 0; i < formats.CountItems(); i++) {
			VideoFormat* format = formats.ItemAt(i);
			if (format != NULL) {
				BString label;
				label.SetToFormat("%ldx%ld @ %.1f fps (%s)",
					format->width, format->height,
					format->frameRate, format->colorSpace);

				BMessage* msg = new BMessage(MSG_FORMAT_SELECTED);
				msg->AddInt32("index", i);
				BMenuItem* item = new BMenuItem(label.String(), msg);
				fFormatMenu->AddItem(item);

				// Mark current format
				VideoFormat current = fCurrentWebcam->CurrentFormat();
				if (format->width == current.width &&
					format->height == current.height) {
					item->SetMarked(true);
				}
			}
		}
	}

	fFormatMenu->SetEnabled(true);
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
	_PopulateFormatMenu();

	// Update controls panel
	fWebcamControls->SetDevice(device);

	BString status;
	status.SetToFormat("Selected: %s", device->Name());
	fStatusBar->SetText(status.String());
}


void
MainWindow::_SelectFormat(int32 index)
{
	if (fCurrentWebcam == NULL)
		return;

	const BObjectList<VideoFormat>& formats = fCurrentWebcam->SupportedFormats();
	if (index < 0 || index >= formats.CountItems())
		return;

	// Update menu checkmarks
	for (int32 i = 0; i < fFormatMenu->CountItems(); i++) {
		BMenuItem* item = fFormatMenu->ItemAt(i);
		if (item != NULL)
			item->SetMarked(i == index);
	}

	// Note: Actual format change would require restarting capture
	// with new parameters
	BString status;
	status.SetToFormat("Format selected (restart preview to apply)");
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
MainWindow::_TakeScreenshot()
{
	if (fLastFrame == NULL) {
		BAlert* alert = new BAlert("Error",
			"No video frame available.\n"
			"Start the preview first.", "OK");
		alert->Go();
		return;
	}

	// Generate default filename
	BPath path = ExportUtils::GetScreenshotDirectory();
	path.Append(ExportUtils::GenerateScreenshotFilename().String());

	// Create save panel if needed
	if (fSavePanel == NULL) {
		BMessenger messenger(this);
		fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger,
			NULL, 0, false, new BMessage(MSG_SCREENSHOT_SAVED));
	}

	fSavePanel->SetSaveText(path.Leaf());
	fSavePanel->Show();
}


void
MainWindow::_ExportDriverInfo(bool asJson)
{
	if (fCurrentWebcam == NULL) {
		BAlert* alert = new BAlert("Error",
			"No webcam selected.\n"
			"Select a webcam first.", "OK");
		alert->Go();
		return;
	}

	fSavingJson = asJson;

	// Generate default filename
	BString filename("BubiCam_DriverInfo_");
	filename << ExportUtils::GetTimestamp();
	filename << (asJson ? ".json" : ".txt");

	BPath path = ExportUtils::GetScreenshotDirectory();
	path.Append(filename.String());

	// Create save panel if needed
	if (fSavePanel == NULL) {
		BMessenger messenger(this);
		fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger,
			NULL, 0, false, new BMessage(MSG_EXPORT_SAVED));
	}

	fSavePanel->SetMessage(new BMessage(MSG_EXPORT_SAVED));
	fSavePanel->SetSaveText(path.Leaf());
	fSavePanel->Show();
}


void
MainWindow::_SaveScreenshot(const char* path)
{
	if (fLastFrame == NULL)
		return;

	status_t status = ExportUtils::SaveScreenshot(fLastFrame, path);
	if (status == B_OK) {
		BString msg;
		msg.SetToFormat("Screenshot saved:\n%s", path);
		fStatusBar->SetText("Screenshot saved");
	} else {
		BString error;
		error.SetToFormat("Failed to save screenshot:\n%s", strerror(status));
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
	}
}


void
MainWindow::_SaveExport(const char* path, bool asJson)
{
	if (fCurrentWebcam == NULL)
		return;

	status_t status;
	if (asJson)
		status = ExportUtils::ExportDriverInfoAsJSON(fCurrentWebcam, path);
	else
		status = ExportUtils::ExportDriverInfoAsText(fCurrentWebcam, path);

	if (status == B_OK) {
		fStatusBar->SetText("Driver info exported");
	} else {
		BString error;
		error.SetToFormat("Failed to export:\n%s", strerror(status));
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
	}
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

		case MSG_FORMAT_SELECTED:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK)
				_SelectFormat(index);
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

				// Keep a copy for screenshots
				if (fLastFrame == NULL ||
					fLastFrame->Bounds() != bitmap->Bounds()) {
					delete fLastFrame;
					fLastFrame = new BBitmap(bitmap->Bounds(),
						bitmap->ColorSpace());
				}
				memcpy(fLastFrame->Bits(), bitmap->Bits(),
					bitmap->BitsLength());
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

		case MSG_SCREENSHOT:
			_TakeScreenshot();
			break;

		case MSG_SCREENSHOT_SAVED:
		{
			entry_ref ref;
			BString name;
			if (message->FindRef("directory", &ref) == B_OK &&
				message->FindString("name", &name) == B_OK) {
				BPath path(&ref);
				path.Append(name.String());
				_SaveScreenshot(path.Path());
			}
			break;
		}

		case MSG_EXPORT_INFO:
			_ExportDriverInfo(false);
			break;

		case MSG_EXPORT_INFO_JSON:
			_ExportDriverInfo(true);
			break;

		case MSG_EXPORT_SAVED:
		{
			entry_ref ref;
			BString name;
			if (message->FindRef("directory", &ref) == B_OK &&
				message->FindString("name", &name) == B_OK) {
				BPath path(&ref);
				path.Append(name.String());
				_SaveExport(path.Path(), fSavingJson);
			}
			break;
		}

		case MSG_CLEAR_SYSLOG:
			fSyslogView->Clear();
			break;

		case MSG_TOGGLE_CONTROLS:
			// Switch to controls tab
			fRightTabView->Select(2);
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
