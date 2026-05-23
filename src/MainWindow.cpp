/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "MainWindow.h"
#include "VideoPreviewView.h"
#include "DriverInfoView.h"
#include "DriverTestView.h"
#include "USBPacketView.h"
#include "SyslogView.h"
#include "VUMeterView.h"
#include "WebcamControlsView.h"
#include "LEDView.h"
#include "WebcamRoster.h"
#include "WebcamDevice.h"
#include "MCPServer.h"
#include "ExportUtils.h"
#include "IconUtils.h"
#include "VideoRecorder.h"

// Logging macros using centralized ErrorUtils
#define LOG_MODULE "MainWindow"
#include "ErrorUtils.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <SplitView.h>
#include <ToolBar.h>
#include <ScrollView.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <Box.h>
#include <TabView.h>
#include <Catalog.h>
#include <Alert.h>
#include <Path.h>
#include <Entry.h>
#include <Screen.h>
#include <MediaRoster.h>
#include <Roster.h>

#include <Autolock.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow()
	:
	BWindow(BRect(50, 50, 1300, 800), "BubiCam - Webcam Driver Tester",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS),
	fMenuBar(NULL),
	fWebcamMenu(NULL),
	fControlMenu(NULL),
	fFormatMenu(NULL),
	fToolsMenu(NULL),
	fVideoPreview(NULL),
	fDriverInfo(NULL),
	fDriverTestView(NULL),
	fUSBPacketView(NULL),
	fSyslogView(NULL),
	fVUMeter(NULL),
	fWebcamControls(NULL),
	fStatusBar(NULL),
	fRightTabView(NULL),
	fToolbar(NULL),
	fStatsResolution(NULL),
	fStatsFPS(NULL),
	fStatsFrames(NULL),
	fStatsDropped(NULL),
	fWebcamRoster(NULL),
	fCurrentWebcam(NULL),
	fCurrentWebcamIndex(-1),
	fIsPreviewActive(false),
	fSavePanel(NULL),
	fLastFrame(NULL),
	fSavingJson(false),
	fMCPServer(NULL),
	fMCPMenuItem(NULL),
	fRecorder(NULL),
	fAutoStartPreview(true),
	fDriverCrashed(false),
	fWatchdogAlertShown(false),
	fLastFrameReceived(0),
	fWatchdogRunner(NULL)
{
	fWebcamRoster = new WebcamRoster();
	AddHandler(fWebcamRoster);

	// Create MCP server
	fMCPServer = new MCPServer(BMessenger(this));

	_BuildMenu();
	_BuildLayout();
	_PopulateWebcamMenu();

	// Start watching for media node changes (hot-plug detection)
	fWebcamRoster->StartWatching();

	// Allow window to be resized freely
	BScreen screen(this);
	BRect screenFrame = screen.Frame();
	SetSizeLimits(800, screenFrame.Width(), 620, screenFrame.Height());

	// Start syslog monitoring
	fSyslogView->StartMonitoring();

	// Restore saved settings (window position, last device, etc.)
	_LoadSettings();
}


MainWindow::~MainWindow()
{
	// Note: Primary Media Kit cleanup is done in QuitRequested().
	// The destructor only handles non-blocking cleanup.

	delete fWatchdogRunner;
	fWatchdogRunner = NULL;

	if (fSyslogView != NULL)
		fSyslogView->StopMonitoring();

	// Stop node watching (non-blocking)
	if (fWebcamRoster != NULL) {
		fWebcamRoster->StopWatching();
		RemoveHandler(fWebcamRoster);
		// Don't delete fWebcamRoster here - QuitRequested already cleared it,
		// and the destructor of WebcamDevice would re-call StopCapture
		// which can hang. Let the OS reclaim the memory on exit.
	}

	delete fSavePanel;
	delete fLastFrame;
	delete fRecorder;

	// Stop MCP server (non-blocking)
	if (fMCPServer != NULL) {
		fMCPServer->Stop();
		fMCPServer->Lock();
		fMCPServer->Quit();
	}
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
	fileMenu->AddItem(new BMenuItem("Start Recording",
		new BMessage(MSG_RECORD_START), 'G'));
	fileMenu->AddItem(new BMenuItem("Stop Recording",
		new BMessage(MSG_RECORD_STOP), 'G', B_SHIFT_KEY));
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
	fControlMenu->AddItem(new BMenuItem("Force Stop (Driver Frozen)",
		new BMessage(MSG_FORCE_STOP)));
	fControlMenu->AddSeparatorItem();
	fControlMenu->AddItem(new BMenuItem("Show Controls Panel",
		new BMessage(MSG_TOGGLE_CONTROLS), 'K'));
	fControlMenu->AddSeparatorItem();
	{
		BMenuItem* autoPreviewItem = new BMenuItem("Auto-start Preview on Launch",
			new BMessage(MSG_TOGGLE_AUTO_PREVIEW));
		autoPreviewItem->SetMarked(fAutoStartPreview);
		fControlMenu->AddItem(autoPreviewItem);
	}
	fMenuBar->AddItem(fControlMenu);

	// Tools menu
	fToolsMenu = new BMenu("Tools");
	fToolsMenu->AddItem(new BMenuItem("Driver Tests" B_UTF8_ELLIPSIS,
		new BMessage(MSG_SHOW_DRIVER_TESTS), 'D'));
	fToolsMenu->AddItem(new BMenuItem("USB Descriptors" B_UTF8_ELLIPSIS,
		new BMessage(MSG_SHOW_USB_VIEWER), 'U'));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Clear Syslog",
		new BMessage(MSG_CLEAR_SYSLOG), 'L'));
	BMenuItem* noiseFilterItem = new BMenuItem("Filter Syslog Noise",
		new BMessage(MSG_TOGGLE_NOISE_FILTER));
	noiseFilterItem->SetMarked(true);
	fToolsMenu->AddItem(noiseFilterItem);
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Toggle Histogram",
		new BMessage(MSG_TOGGLE_HISTOGRAM), 'H'));
	fToolsMenu->AddItem(new BMenuItem("Reset Zoom",
		new BMessage(MSG_RESET_ZOOM), '0'));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Capture Reference Frame",
		new BMessage(MSG_CAPTURE_REFERENCE), 'B'));
	fToolsMenu->AddItem(new BMenuItem("A/B Compare Mode",
		new BMessage(MSG_TOGGLE_COMPARE), 'B', B_SHIFT_KEY));
	fToolsMenu->AddItem(new BMenuItem("Clear Reference",
		new BMessage(MSG_CLEAR_REFERENCE)));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Restart Media Services" B_UTF8_ELLIPSIS,
		new BMessage(MSG_RESTART_MEDIA), 'M', B_SHIFT_KEY));
	fToolsMenu->AddSeparatorItem();
	fMCPMenuItem = new BMenuItem("Enable MCP Server (Port 9847)",
		new BMessage(MSG_MCP_TOGGLE));
	fToolsMenu->AddItem(fMCPMenuItem);
	fMenuBar->AddItem(fToolsMenu);
}


void
MainWindow::_BuildToolbar()
{
	fToolbar = new BToolBar();

	// Create icons
	BBitmap* refreshIcon = IconUtils::CreateRefreshIcon(24);
	BBitmap* startIcon = IconUtils::CreateStartIcon(24);
	BBitmap* stopIcon = IconUtils::CreateStopIcon(24);
	BBitmap* screenshotIcon = IconUtils::CreateScreenshotIcon(24);

	// Add actions with icons
	fToolbar->AddAction(MSG_REFRESH_DEVICES, this, refreshIcon,
		"Refresh webcam list (Cmd+R)", "Refresh");
	fToolbar->AddSeparator();
	fToolbar->AddAction(MSG_WEBCAM_START, this, startIcon,
		"Start video preview (Cmd+S)", "Start");
	fToolbar->AddAction(MSG_WEBCAM_STOP, this, stopIcon,
		"Stop video preview (Cmd+T)", "Stop");
	fToolbar->AddSeparator();
	fToolbar->AddAction(MSG_SCREENSHOT, this, screenshotIcon,
		"Take screenshot (Cmd+P)", "Screenshot");

	BBitmap* recordIcon = IconUtils::CreateRecordIcon(24);
	BBitmap* recordStopIcon = IconUtils::CreateRecordStopIcon(24);
	fToolbar->AddSeparator();
	fToolbar->AddAction(MSG_RECORD_START, this, recordIcon,
		"Start recording (Cmd+G)", "Record");
	fToolbar->AddAction(MSG_RECORD_STOP, this, recordStopIcon,
		"Stop recording (Cmd+Shift+G)", "Stop Rec");
	fToolbar->AddGlue();

	// Initial state
	fToolbar->SetActionEnabled(MSG_WEBCAM_STOP, false);
	fToolbar->SetActionEnabled(MSG_SCREENSHOT, false);
	fToolbar->SetActionEnabled(MSG_RECORD_START, false);
	fToolbar->SetActionEnabled(MSG_RECORD_STOP, false);

	// Clean up icons (BToolBar makes copies)
	delete refreshIcon;
	delete startIcon;
	delete stopIcon;
	delete screenshotIcon;
	delete recordIcon;
	delete recordStopIcon;
}


void
MainWindow::_BuildLayout()
{
	// Build toolbar first
	_BuildToolbar();

	// Create views
	fVideoPreview = new VideoPreviewView("videoPreview");
	fVideoPreview->SetExplicitMinSize(BSize(280, 210));
	fVideoPreview->SetExplicitPreferredSize(BSize(360, 270));
	fVideoPreview->SetShowStats(false);  // Disable overlay, use stats bar instead

	fDriverInfo = new DriverInfoView("driverInfo");
	fSyslogView = new SyslogView("syslogView");
	fVUMeter = new VUMeterView("vuMeter");
	fVUMeter->SetExplicitMinSize(BSize(B_SIZE_UNSET, 72));
	fWebcamControls = new WebcamControlsView("webcamControls");

	// Status bar (bottom)
	fStatusBar = new BStringView("statusBar", "No webcam selected");
	fStatusBar->SetExplicitMinSize(BSize(B_SIZE_UNSET, 20));

	BView* statsBar = _BuildStatsBar();

	// Create video box with toolbar, video and stats bar inside
	BBox* videoBox = new BBox("videoBox");
	videoBox->SetLabel("Video Preview");
	videoBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 2)
		.Add(fToolbar)
		.Add(fVideoPreview)
		.Add(statsBar)
		.SetInsets(6, 6, 6, 6)
		.View());

	// VU Meter box (more compact)
	BBox* vuBox = new BBox("vuBox");
	vuBox->SetLabel("Mic Level");
	vuBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(fVUMeter)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	// Left side: video + VU meter (toolbar is inside video box)
	BSplitView* leftSplit = new BSplitView(B_VERTICAL);
	leftSplit->AddChild(videoBox);
	leftSplit->AddChild(vuBox);
	leftSplit->SetItemWeight(0, 0.78f, true);
	leftSplit->SetItemWeight(1, 0.22f, true);
	leftSplit->SetExplicitMaxSize(BSize(430, B_SIZE_UNLIMITED));

	fRightTabView = _BuildTabView();

	// Syslog box
	BBox* syslogBox = new BBox("syslogBox");
	syslogBox->SetLabel("Syslog");
	BScrollView* syslogScroll = new BScrollView("syslogScroll", fSyslogView,
		B_SUPPORTS_LAYOUT, false, true);
	syslogBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.Add(syslogScroll)
		.SetInsets(B_USE_SMALL_INSETS)
		.View());

	// Right side: Tab view on top, Syslog on bottom
	BSplitView* rightSplit = new BSplitView(B_VERTICAL);
	rightSplit->AddChild(fRightTabView);
	rightSplit->AddChild(syslogBox);
	rightSplit->SetItemWeight(0, 0.55f, true);
	rightSplit->SetItemWeight(1, 0.45f, true);

	// Main horizontal split
	BSplitView* mainSplit = new BSplitView(B_HORIZONTAL);
	mainSplit->AddChild(leftSplit);
	mainSplit->AddChild(rightSplit);
	mainSplit->SetItemWeight(0, 0.25f, true);
	mainSplit->SetItemWeight(1, 0.75f, true);

	// Main layout with top margin below menu
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddStrut(8)
		.Add(mainSplit)
		.Add(fStatusBar)
		.SetInsets(0, 0, 0, 0);
}


void
MainWindow::_PopulateWebcamMenu()
{
	// Stop preview and clear current device before re-enumerating,
	// since EnumerateDevices() deletes all existing device objects
	_StopPreview();
	{
		BAutolock lock(fWebcamLock);
		fCurrentWebcam = NULL;
		fCurrentWebcamIndex = -1;
	}
	if (fMCPServer != NULL)
		fMCPServer->SetWebcamDevice(NULL);
	fDriverTestView->SetDevice(NULL);

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
				label.SetToFormat("%dx%d @ %.1f fps (%s)",
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

	{
		BAutolock lock(fWebcamLock);
		fCurrentWebcam = device;
		fCurrentWebcamIndex = index;
	}

	// Update menu checkmarks
	for (int32 i = 2; i < fWebcamMenu->CountItems(); i++) {
		BMenuItem* item = fWebcamMenu->ItemAt(i);
		if (item != NULL)
			item->SetMarked(i - 2 == index);
	}

	// Start preview FIRST - this instantiates the media node
	// which is required before we can access parameters
	_StartPreview();

	// Now that the node is instantiated, update UI
	_UpdateDriverInfo();
	_PopulateFormatMenu();

	// Update controls panel (requires instantiated node for GetParameterWebFor)
	fWebcamControls->SetDevice(device);

	// Update test panel
	if (fDriverTestView != NULL)
		fDriverTestView->SetDevice(device);

	// Update USB panel
	if (fUSBPacketView != NULL)
		fUSBPacketView->SetDevice(device);

	// Update MCP server with new device
	if (fMCPServer != NULL)
		fMCPServer->SetWebcamDevice(device);

	// Save settings immediately so they survive a force-kill
	_SaveSettings();
}


void
MainWindow::_SelectFormat(int32 index)
{
	if (fCurrentWebcam == NULL)
		return;

	const BObjectList<VideoFormat>& formats = fCurrentWebcam->SupportedFormats();
	if (index < 0 || index >= formats.CountItems())
		return;

	VideoFormat* selectedFormat = formats.ItemAt(index);
	if (selectedFormat == NULL)
		return;

	// Update menu checkmarks
	for (int32 i = 0; i < fFormatMenu->CountItems(); i++) {
		BMenuItem* item = fFormatMenu->ItemAt(i);
		if (item != NULL)
			item->SetMarked(i == index);
	}

	// Set the requested format
	fCurrentWebcam->SetRequestedFormat(*selectedFormat);

	// Restart preview to apply the new format
	bool wasActive = fIsPreviewActive;
	if (wasActive) {
		fStatusBar->SetText("Changing resolution...");
		UpdateIfNeeded();

		_StopPreview();
		_StartPreview();
	}
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
		// Check if this is a "node already in use" error
		if (status == B_NAME_NOT_FOUND) {
			BAlert* alert = new BAlert("Webcam Blocked",
				"The webcam appears to be blocked by a previous session.\n\n"
				"This usually happens when an application didn't release "
				"the webcam properly.\n\n"
				"Would you like to restart the Media Services to fix this?",
				"Cancel", "Restart Media Services", NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);

			if (alert->Go() == 1) {
				// Save the webcam name before restart (device will be destroyed)
				BString webcamName;
				if (fCurrentWebcam != NULL)
					webcamName = fCurrentWebcam->Name();

				_DoRestartMediaServices(false);  // No confirmation needed, user already confirmed

				// After restart, find the webcam by name (index may have changed)
				if (webcamName.Length() > 0) {
					for (int32 i = 0; i < fWebcamRoster->CountDevices(); i++) {
						WebcamDevice* device = fWebcamRoster->DeviceAt(i);
						if (device != NULL && webcamName == device->Name()) {
							_SelectWebcam(i);
							break;
						}
					}
				}
			}
			return;
		}

		// Other errors
		BString error;
		error.SetToFormat("Failed to start capture: %s (0x%08x)",
			strerror(status), status);
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
		return;
	}

	fIsPreviewActive = true;
	fLastFrameReceived = system_time();
	fDriverCrashed = false;
	fStatusBar->SetText("Preview active");

	// LED on (green, steady)
	fCamLED->SetBlinking(false);
	fCamLED->SetState(LED_GREEN);

	// Start watchdog timer (check every 2 seconds)
	delete fWatchdogRunner;
	fWatchdogRunner = new BMessageRunner(BMessenger(this),
		new BMessage(MSG_WATCHDOG_CHECK), 2000000);  // 2 seconds

	// Refresh Format menu now that ParameterWeb is available
	_PopulateFormatMenu();

	// Update toolbar and driver info
	_UpdateToolbarState();
	_UpdateDriverInfo();
}


void
MainWindow::_StopPreview()
{
	// Stop watchdog timer
	delete fWatchdogRunner;
	fWatchdogRunner = NULL;

	// Reset watchdog alert state for next session
	fWatchdogAlertShown = false;

	// Stop recording if active
	if (fRecorder != NULL && fRecorder->IsRecording())
		_StopRecording();

	if (fCurrentWebcam != NULL && fIsPreviewActive) {
		fCurrentWebcam->StopCapture();
		fIsPreviewActive = false;
		fVideoPreview->ClearFrame();
		fVUMeter->SetLevel(0.0f, 0.0f);

		// LED off (red)
		fCamLED->SetBlinking(false);
		fCamLED->SetState(LED_RED);

		// Reset stats bar
		fStatsResolution->SetText("---");
		fStatsFPS->SetText("--- fps");
		fStatsFrames->SetText("0 frames");
		fStatsDropped->SetText("0 dropped");
		fStatsDropped->SetHighUIColor(B_PANEL_TEXT_COLOR);

		// Update toolbar
		_UpdateToolbarState();

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
MainWindow::_UpdateStatsBar()
{
	if (fVideoPreview == NULL)
		return;

	// Resolution
	int32 width = fVideoPreview->VideoWidth();
	int32 height = fVideoPreview->VideoHeight();
	BString resStr;
	if (width > 0 && height > 0)
		resStr.SetToFormat("%dx%d", (int)width, (int)height);
	else
		resStr = "---";
	fStatsResolution->SetText(resStr.String());

	// FPS with color coding
	float fps = fVideoPreview->CurrentFPS();
	BString fpsStr;
	fpsStr.SetToFormat("%.1f fps", fps);
	fStatsFPS->SetText(fpsStr.String());

	if (fps >= 25.0f)
		fStatsFPS->SetHighColor(0, 180, 0);  // Green
	else if (fps >= 15.0f)
		fStatsFPS->SetHighColor(180, 180, 0);  // Yellow
	else if (fps > 0)
		fStatsFPS->SetHighColor(200, 80, 80);  // Red
	else
		fStatsFPS->SetHighUIColor(B_PANEL_TEXT_COLOR);

	// Frames received
	uint32 received = fVideoPreview->FramesReceived();
	BString framesStr;
	framesStr.SetToFormat("%u frames", (unsigned)received);
	fStatsFrames->SetText(framesStr.String());

	// Frames dropped with color coding
	uint32 dropped = fVideoPreview->FramesDropped();
	BString droppedStr;
	droppedStr.SetToFormat("%u dropped", (unsigned)dropped);
	fStatsDropped->SetText(droppedStr.String());

	if (dropped > 0)
		fStatsDropped->SetHighColor(200, 80, 80);  // Red
	else
		fStatsDropped->SetHighUIColor(B_PANEL_TEXT_COLOR);
}


void
MainWindow::_UpdateToolbarState()
{
	bool hasWebcam = (fCurrentWebcam != NULL);
	bool isActive = fIsPreviewActive;

	bool isRecording = (fRecorder != NULL && fRecorder->IsRecording());

	fToolbar->SetActionEnabled(MSG_WEBCAM_START, hasWebcam && !isActive);
	fToolbar->SetActionEnabled(MSG_WEBCAM_STOP, hasWebcam && isActive);
	fToolbar->SetActionEnabled(MSG_SCREENSHOT, isActive && fLastFrame != NULL);
	fToolbar->SetActionEnabled(MSG_RECORD_START, isActive && !isRecording);
	fToolbar->SetActionEnabled(MSG_RECORD_STOP, isRecording);
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

		case MSG_RESTORE_DEVICE:
		{
			const char* deviceName;
			if (message->FindString("device_name", &deviceName) != B_OK)
				break;

			for (int32 i = 0; i < fWebcamRoster->CountDevices(); i++) {
				WebcamDevice* device = fWebcamRoster->DeviceAt(i);
				if (device != NULL && strcmp(device->Name(), deviceName) == 0) {
					if (fAutoStartPreview) {
						_SelectWebcam(i);
					} else {
						BAutolock lock(fWebcamLock);
						fCurrentWebcam = device;
						fCurrentWebcamIndex = i;
						lock.Unlock();

						for (int32 j = 2; j < fWebcamMenu->CountItems(); j++) {
							BMenuItem* menuItem = fWebcamMenu->ItemAt(j);
							if (menuItem != NULL)
								menuItem->SetMarked(j - 2 == i);
						}
						BString status;
						status.SetToFormat("Selected: %s (preview disabled)",
							device->Name());
						fStatusBar->SetText(status.String());
					}
					break;
				}
			}
			break;
		}

		case MSG_TOGGLE_AUTO_PREVIEW:
		{
			fAutoStartPreview = !fAutoStartPreview;
			BMenuItem* item = fControlMenu->FindItem(MSG_TOGGLE_AUTO_PREVIEW);
			if (item != NULL)
				item->SetMarked(fAutoStartPreview);
			fStatusBar->SetText(fAutoStartPreview
				? "Auto-start preview enabled"
				: "Auto-start preview disabled");
			_SaveSettings();
			break;
		}

		case MSG_DEVICES_CHANGED:
		{
			// Auto-refresh triggered by media node watcher (hot-plug)
			// If preview is active, don't refresh - our own consumer nodes
			// being created/destroyed would trigger this and kill the preview
			if (fIsPreviewActive) {
				// Just note it for next manual refresh
				fStatusBar->SetText("Device change detected (refresh when idle)");
				break;
			}

			fStatusBar->SetText("Device change detected, refreshing...");
			_PopulateWebcamMenu();
			int32 count = fWebcamRoster->CountDevices();
			BString status;
			status.SetToFormat("Hot-plug: %d webcam%s found",
				(int)count, count != 1 ? "s" : "");
			fStatusBar->SetText(status.String());
			break;
		}

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
			_HandleFrameReceived(message);
			break;

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

		case MSG_TOGGLE_NOISE_FILTER:
		{
			bool enabled = !fSyslogView->NoiseFilterEnabled();
			fSyslogView->SetNoiseFilterEnabled(enabled);
			BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_NOISE_FILTER);
			if (item != NULL)
				item->SetMarked(enabled);
			break;
		}

		case MSG_TOGGLE_CONTROLS:
			// Switch to controls tab
			fRightTabView->Select(1);
			break;

		case MSG_SHOW_DRIVER_TESTS:
			// Switch to testing tab
			fRightTabView->Select(2);
			break;

		case MSG_SHOW_USB_VIEWER:
			// Switch to USB tab
			fRightTabView->Select(3);
			break;

		case MSG_RESTART_MEDIA:
			_RestartMediaServices();
			break;

		case MSG_MCP_TOGGLE:
		{
			if (fMCPServer->IsRunning()) {
				fMCPServer->Stop();
				fMCPMenuItem->SetLabel("Enable MCP Server (Port 9847)");
				fMCPMenuItem->SetMarked(false);
				fStatusBar->SetText("MCP Server stopped");
			} else {
				fMCPServer->SetWebcamDevice(fCurrentWebcam);
				if (fMCPServer->Start(9847) == B_OK) {
					fMCPMenuItem->SetLabel("Disable MCP Server (Port 9847)");
					fMCPMenuItem->SetMarked(true);
					fStatusBar->SetText("MCP Server running on port 9847");
				} else {
					fStatusBar->SetText("Failed to start MCP Server");
				}
			}
			break;
		}

		case MSG_MCP_STATUS:
		{
			bool running;
			if (message->FindBool("running", &running) == B_OK) {
				fMCPMenuItem->SetMarked(running);
				if (running) {
					int16 port;
					message->FindInt16("port", &port);
					BString status;
					status.SetToFormat("MCP Server running on port %d", port);
					fStatusBar->SetText(status.String());
				}
			}
			break;
		}

		case MSG_MCP_LOG:
		{
			const char* logMessage;
			if (message->FindString("message", &logMessage) == B_OK) {
				BString syslogLine;
				syslogLine.SetToFormat("[MCP] %s", logMessage);
				fSyslogView->AddLine(syslogLine.String());
			}
			break;
		}

		case MSG_TOGGLE_HISTOGRAM:
		{
			// Toggle histogram overlay on the preview
			static bool histogramShown = false;
			histogramShown = !histogramShown;
			fVideoPreview->SetShowHistogram(histogramShown);
			BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_HISTOGRAM);
			if (item != NULL)
				item->SetMarked(histogramShown);
			break;
		}

		case MSG_RESET_ZOOM:
			fVideoPreview->ResetZoom();
			break;

		case MSG_CAPTURE_REFERENCE:
			fVideoPreview->CaptureReference();
			fStatusBar->SetText("Reference frame captured for A/B comparison");
			break;

		case MSG_TOGGLE_COMPARE:
		{
			if (!fVideoPreview->HasReference()) {
				// Auto-capture if no reference exists
				fVideoPreview->CaptureReference();
			}
			bool compare = !fVideoPreview->CompareMode();
			fVideoPreview->SetCompareMode(compare);
			BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_COMPARE);
			if (item != NULL)
				item->SetMarked(compare);
			fStatusBar->SetText(compare
				? "A/B Compare: Reference (left) vs Live (right)"
				: "Compare mode off");
			break;
		}

		case MSG_CLEAR_REFERENCE:
			fVideoPreview->ClearReference();
			{
				BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_COMPARE);
				if (item != NULL)
					item->SetMarked(false);
			}
			fStatusBar->SetText("Reference frame cleared");
			break;

		case MSG_RECORD_START:
			_StartRecording();
			break;

		case MSG_RECORD_STOP:
			_StopRecording();
			break;

		case MSG_RESTART_PREVIEW:
			// Called from MCP server when resolution is changed
			if (fIsPreviewActive) {
				_StopPreview();
				_StartPreview();
			}
			break;

		case MSG_WATCHDOG_CHECK:
			_CheckWatchdog();
			break;

		case MSG_FORCE_STOP:
			_ForceStop();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


BView*
MainWindow::_BuildStatsBar()
{
	BView* statsBar = new BView("statsBar", B_WILL_DRAW);
	statsBar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	statsBar->SetExplicitMinSize(BSize(B_SIZE_UNSET, 22));
	statsBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 22));

	fStatsResolution = new BStringView("statsRes", "---");
	fStatsResolution->SetAlignment(B_ALIGN_CENTER);
	fStatsFPS = new BStringView("statsFPS", "--- fps");
	fStatsFPS->SetAlignment(B_ALIGN_CENTER);
	fStatsFrames = new BStringView("statsFrames", "0 frames");
	fStatsFrames->SetAlignment(B_ALIGN_CENTER);
	fStatsDropped = new BStringView("statsDropped", "0 dropped");
	fStatsDropped->SetAlignment(B_ALIGN_CENTER);

	BFont smallFont(be_plain_font);
	smallFont.SetSize(10);
	fStatsResolution->SetFont(&smallFont);
	fStatsFPS->SetFont(&smallFont);
	fStatsFrames->SetFont(&smallFont);
	fStatsDropped->SetFont(&smallFont);

	fCamLED = new LEDView("camLED");

	BLayoutBuilder::Group<>(statsBar, B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
		.Add(fCamLED)
		.Add(fStatsResolution)
		.Add(fStatsFPS)
		.Add(fStatsFrames)
		.Add(fStatsDropped)
		.End();

	return statsBar;
}


BTabView*
MainWindow::_BuildTabView()
{
	BTabView* tabView = new BTabView("rightTabs");
	tabView->SetExplicitMinSize(BSize(200, 200));

	// Driver Info tab
	fDriverInfo->SetExplicitMinSize(BSize(200, 100));
	fDriverInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	BScrollView* infoScroll = new BScrollView("infoScroll", fDriverInfo,
		B_SUPPORTS_LAYOUT, false, true);
	tabView->AddTab(infoScroll, new BTab());
	tabView->TabAt(0)->SetLabel("Driver Info");

	// Controls tab
	fWebcamControls->SetExplicitMinSize(BSize(200, 100));
	fWebcamControls->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	BScrollView* controlsScroll = new BScrollView("controlsScroll",
		fWebcamControls, B_SUPPORTS_LAYOUT, false, true);
	tabView->AddTab(controlsScroll, new BTab());
	tabView->TabAt(1)->SetLabel("Controls");

	// Testing tab
	fDriverTestView = new DriverTestView("driverTestView");
	fDriverTestView->SetTarget(this);
	fDriverTestView->SetExplicitMinSize(BSize(200, 100));
	fDriverTestView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	BScrollView* testScroll = new BScrollView("testScroll",
		fDriverTestView, B_SUPPORTS_LAYOUT, false, true);
	tabView->AddTab(testScroll, new BTab());
	tabView->TabAt(2)->SetLabel("Testing");

	// USB tab
	fUSBPacketView = new USBPacketView("usbPacketView");
	fUSBPacketView->SetExplicitMinSize(BSize(200, 100));
	fUSBPacketView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	tabView->AddTab(fUSBPacketView, new BTab());
	tabView->TabAt(3)->SetLabel("USB");

	return tabView;
}


void
MainWindow::_HandleFrameReceived(BMessage* message)
{
	fLastFrameReceived = system_time();
	fDriverCrashed = false;

	BBitmap* bitmap = NULL;
	if (message->FindPointer("bitmap", (void**)&bitmap) != B_OK)
		return;

	fVideoPreview->SetFrame(bitmap);

	// Update resolution info
	BRect bitmapBounds = bitmap->Bounds();
	fVideoPreview->SetResolution(
		(int32)(bitmapBounds.Width() + 1),
		(int32)(bitmapBounds.Height() + 1));

	// CRITICAL FIX: Copy webcam pointer under lock, then release lock
	// BEFORE calling methods that may acquire other locks. This prevents
	// deadlock with StopCapture which holds fCaptureLock and waits for
	// fTargetLock.
	WebcamDevice* webcam = NULL;
	{
		BAutolock lock(fWebcamLock);
		webcam = fCurrentWebcam;
	}
	if (webcam != NULL) {
		fVideoPreview->UpdateStats(
			webcam->CurrentFPS(),
			webcam->FramesCaptured(),
			webcam->FramesDropped());
	}

	_UpdateStatsBar();

	// Update recording status periodically
	if (fRecorder != NULL && fRecorder->IsRecording()
		&& (fRecorder->FramesRecorded() % 30) == 0)
		_UpdateRecordingStatus();

	// Record frame if recording is active
	if (fRecorder != NULL && fRecorder->IsRecording())
		fRecorder->AddFrame(bitmap);

	// Keep a copy for screenshots
	if (fLastFrame == NULL ||
		fLastFrame->Bounds() != bitmap->Bounds() ||
		fLastFrame->ColorSpace() != bitmap->ColorSpace()) {
		delete fLastFrame;
		fLastFrame = new BBitmap(bitmap->Bounds(), bitmap->ColorSpace());
		if (fLastFrame == NULL || !fLastFrame->IsValid()) {
			delete fLastFrame;
			fLastFrame = NULL;
			fprintf(stderr, "MainWindow: Failed to allocate screenshot bitmap\n");
		} else {
			_UpdateToolbarState();
		}
	}
	if (fLastFrame != NULL && fLastFrame->IsValid() &&
		fLastFrame->BitsLength() >= bitmap->BitsLength()) {
		memcpy(fLastFrame->Bits(), bitmap->Bits(), bitmap->BitsLength());
	}
}


void
MainWindow::_CheckWatchdog()
{
	if (!fIsPreviewActive || fLastFrameReceived == 0)
		return;

	bigtime_t timeSinceLastFrame = system_time() - fLastFrameReceived;
	if (timeSinceLastFrame <= 5000000)
		return;

	fDriverCrashed = true;

	// LED yellow blinking = driver frozen
	fCamLED->SetState(LED_YELLOW);
	fCamLED->SetBlinking(true);

	BString warning;
	warning.SetToFormat("WARNING: No frames for %.0f seconds - driver may be frozen!",
		timeSinceLastFrame / 1000000.0);
	fStatusBar->SetText(warning.String());
	fStatusBar->SetHighColor(200, 0, 0);

	if (!fWatchdogAlertShown) {
		fWatchdogAlertShown = true;
		BAlert* alert = new BAlert("Driver Frozen",
			"The webcam driver appears to be frozen.\n\n"
			"No video frames have been received for several seconds.\n\n"
			"Use Control \xe2\x86\x92 Force Stop to safely stop the preview,\n"
			"or close the application (it will exit cleanly).",
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(NULL);
	}
}


struct ForceStopData {
	WebcamDevice*	device;
};

static int32
_ForceStopThread(void* data)
{
	ForceStopData* fsd = static_cast<ForceStopData*>(data);
	if (fsd->device != NULL)
		fsd->device->StopCapture();
	delete fsd;
	return 0;
}


void
MainWindow::_ForceStop()
{
	fprintf(stderr, "MainWindow: Force stop requested\n");

	delete fWatchdogRunner;
	fWatchdogRunner = NULL;

	fDriverCrashed = true;
	fIsPreviewActive = false;

	// LED off
	fCamLED->SetBlinking(false);
	fCamLED->SetState(LED_OFF);

	// Attempt to stop capture in a background thread so the UI
	// remains responsive even if StopCapture blocks on a frozen driver
	WebcamDevice* deviceToStop = NULL;
	{
		BAutolock lock(fWebcamLock);
		deviceToStop = fCurrentWebcam;
		// Do NOT clear fCurrentWebcam here - _SelectWebcam needs to see it
		// is not NULL to know the device exists. The background thread only
		// calls StopCapture which is safe even if called multiple times.
	}
	if (deviceToStop != NULL) {
		ForceStopData* fsd = new ForceStopData();
		fsd->device = deviceToStop;
		thread_id tid = spawn_thread(_ForceStopThread, "force_stop",
			B_LOW_PRIORITY, fsd);
		if (tid >= 0)
			resume_thread(tid);
		else
			delete fsd;
	}

	fVideoPreview->ClearFrame();
	fVUMeter->SetLevel(0.0f, 0.0f);
	fStatsResolution->SetText("---");
	fStatsFPS->SetText("--- fps");
	fStatsFrames->SetText("0 frames");
	fStatsDropped->SetText("0 dropped");
	fStatsDropped->SetHighUIColor(B_PANEL_TEXT_COLOR);

	_UpdateToolbarState();

	fStatusBar->SetText("Preview force-stopped (driver was frozen)");
	fStatusBar->SetHighUIColor(B_PANEL_TEXT_COLOR);
}


void
MainWindow::_RestartMediaServices()
{
	_DoRestartMediaServices(true);  // With confirmation
}


void
MainWindow::_DoRestartMediaServices(bool askConfirmation)
{
	if (askConfirmation) {
		// Confirm with user
		BAlert* alert = new BAlert("Restart Media Services",
			"This will restart the media_server and media_addon_server.\n\n"
			"Use this if you get 'Name not found' errors when selecting a webcam.\n\n"
			"The application will refresh after restart.",
			"Cancel", "Restart", NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);

		if (alert->Go() != 1)
			return;
	}

	// Stop preview first if active
	_StopPreview();

	fStatusBar->SetText("Restarting media services...");
	UpdateIfNeeded();

	// First, try the clean Haiku way
	status_t status = shutdown_media_server(5000000);  // 5 second timeout

	if (status != B_OK) {
		// Clean shutdown failed, try force killing
		// Find and kill media_server and media_addon_server by reading /proc-like info
		fprintf(stderr, "Clean shutdown failed, force killing media services...\n");


		// Fallback: use system() with a more reliable command
		system("hey media_addon_server quit 2>/dev/null");
		system("hey media_server quit 2>/dev/null");
		snooze(500000);  // Wait 500ms

		// If still running, force kill
		system("kill -9 `pidof media_addon_server` 2>/dev/null");
		system("kill -9 `pidof media_server` 2>/dev/null");
		snooze(500000);
	}

	// Wait for media_server to restart automatically
	fStatusBar->SetText("Waiting for media services...");
	UpdateIfNeeded();
	snooze(2000000);  // 2 seconds

	// Refresh device list
	_PopulateWebcamMenu();

	fStatusBar->SetText("Media services restarted - select a webcam");
}


void
MainWindow::_StartRecording()
{
	if (!fIsPreviewActive || fLastFrame == NULL) {
		BAlert* alert = new BAlert("Error",
			"Start the video preview first.", "OK");
		alert->Go();
		return;
	}

	if (fRecorder != NULL && fRecorder->IsRecording())
		return;

	// Generate filename
	BPath dirPath = ExportUtils::GetScreenshotDirectory();

	BString filename("BubiCam_");
	filename << ExportUtils::GetTimestamp();
	filename << ".avi";

	BPath filePath(dirPath);
	filePath.Append(filename.String());

	// Get resolution from current frame
	int32 width = (int32)(fLastFrame->Bounds().Width() + 1);
	int32 height = (int32)(fLastFrame->Bounds().Height() + 1);

	// Get FPS from preview
	float fps = fVideoPreview->CurrentFPS();
	if (fps < 1.0f)
		fps = 30.0f;  // Default if not yet available

	if (fRecorder == NULL)
		fRecorder = new VideoRecorder();

	status_t status = fRecorder->Start(filePath.Path(), width, height, fps);
	if (status != B_OK) {
		BString error;
		error.SetToFormat("Failed to start recording:\n%s", strerror(status));
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
		return;
	}

	BString statusMsg;
	statusMsg.SetToFormat("Recording to %s", filename.String());
	fStatusBar->SetText(statusMsg.String());

	_UpdateToolbarState();
}


void
MainWindow::_StopRecording()
{
	if (fRecorder == NULL || !fRecorder->IsRecording())
		return;

	uint32 frames = fRecorder->FramesRecorded();
	bigtime_t duration = fRecorder->Duration();

	fRecorder->Stop();

	BString statusMsg;
	statusMsg.SetToFormat("Recording saved: %u frames, %.1f seconds",
		(unsigned)frames, duration / 1000000.0);
	fStatusBar->SetText(statusMsg.String());

	_UpdateToolbarState();
}


void
MainWindow::_UpdateRecordingStatus()
{
	if (fRecorder == NULL || !fRecorder->IsRecording())
		return;

	BString statusMsg;
	statusMsg.SetToFormat("REC: %u frames (%.1f s, %.1f MB)",
		(unsigned)fRecorder->FramesRecorded(),
		fRecorder->Duration() / 1000000.0,
		fRecorder->FileSize() / (1024.0 * 1024.0));
	fStatusBar->SetText(statusMsg.String());
}


bool
MainWindow::QuitRequested()
{
	// IMPORTANT: Clean up Media Kit resources BEFORE application shutdown begins.
	// This is critical because the BMediaRoster singleton gets destroyed during
	// process exit, and some webcam drivers (like aukey_webcam) don't handle
	// their node cleanup properly - they try to access already-destroyed objects
	// in their BUSBRoster when the media add-on is unloaded.

	// Save settings before shutdown
	_SaveSettings();

	fprintf(stderr, "MainWindow::QuitRequested() - Starting shutdown...\n");

	// Check if driver appears to be crashed/frozen
	// If we haven't received a frame in 3+ seconds while capturing, driver is likely stuck
	bool driverFrozen = false;
	if (fIsPreviewActive && fLastFrameReceived > 0) {
		bigtime_t timeSinceLastFrame = system_time() - fLastFrameReceived;
		if (timeSinceLastFrame > 3000000) {  // 3 seconds
			fprintf(stderr, "  WARNING: No frames received for %.1f seconds - driver may be frozen\n",
				timeSinceLastFrame / 1000000.0);
			driverFrozen = true;
		}
	}

	if (driverFrozen || fDriverCrashed) {
		// Driver is frozen/crashed - skip normal cleanup to avoid hanging
		fprintf(stderr, "  Driver appears frozen/crashed - forcing immediate shutdown\n");
		fprintf(stderr, "  Skipping Media Kit cleanup to avoid hang\n");

		// Just mark as not capturing and let the OS clean up
		{
			BAutolock lock(fWebcamLock);
			fIsPreviewActive = false;
			fCurrentWebcam = NULL;
		}

		// Don't try to clear the roster - it would call into the frozen driver
		// The OS will clean up when the process exits

		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	// Normal shutdown path - run cleanup in a thread with a global timeout
	// to prevent the process from hanging indefinitely
	{
		struct ShutdownData {
			MainWindow*		window;
			WebcamRoster*	roster;
			volatile bool	done;
		};

		ShutdownData* sd = new ShutdownData();
		sd->window = this;
		sd->roster = fWebcamRoster;
		sd->done = false;

		thread_id shutdownThread = spawn_thread([](void* data) -> int32 {
			ShutdownData* sd = static_cast<ShutdownData*>(data);

			// Stop preview (calls StopCapture with timeouts)
			if (sd->window->LockLooper()) {
				sd->window->_StopPreview();
				sd->window->UnlockLooper();
			}

			// Release all webcam devices
			if (sd->roster != NULL) {
				fprintf(stderr, "  Clearing webcam roster...\n");
				sd->roster->Clear();
			}

			sd->done = true;
			return 0;
		}, "shutdown_cleanup", B_NORMAL_PRIORITY, sd);

		if (shutdownThread >= 0) {
			resume_thread(shutdownThread);

			// Wait up to 5 seconds for clean shutdown
			bigtime_t deadline = system_time() + 5000000;
			while (!sd->done && system_time() < deadline)
				snooze(100000);

			if (sd->done) {
				status_t exitValue;
				wait_for_thread(shutdownThread, &exitValue);
				fprintf(stderr, "MainWindow::QuitRequested() - Clean shutdown complete\n");
			} else {
				fprintf(stderr, "MainWindow::QuitRequested() - Shutdown timed out after 5s, forcing exit\n");
				// Don't wait for the thread - it's stuck in Media Kit IPC.
				// The process will exit and the OS will clean up.
			}
		} else {
			// Fallback: direct cleanup (may hang)
			_StopPreview();
		}

		delete sd;
	}

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::_SaveSettings()
{
	BMessage settings;

	// Window frame
	settings.AddRect("window_frame", Frame());

	// Selected tab
	if (fRightTabView != NULL)
		settings.AddInt32("active_tab", fRightTabView->Selection());

	// Last selected webcam
	if (fCurrentWebcam != NULL)
		settings.AddString("last_device", fCurrentWebcam->Name());

	// Auto-start preview
	settings.AddBool("auto_start_preview", fAutoStartPreview);

	// Write to settings file
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("BubiCam_settings");

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


void
MainWindow::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("BubiCam_settings");

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	// Restore window frame
	BRect frame;
	if (settings.FindRect("window_frame", &frame) == B_OK) {
		// Validate that the frame is still on-screen
		BScreen screen(this);
		BRect screenFrame = screen.Frame();
		if (screenFrame.Contains(BPoint(frame.left, frame.top))) {
			MoveTo(frame.LeftTop());
			ResizeTo(frame.Width(), frame.Height());
		}
	}

	// Restore active tab
	int32 activeTab;
	if (settings.FindInt32("active_tab", &activeTab) == B_OK) {
		if (fRightTabView != NULL && activeTab >= 0
			&& activeTab < fRightTabView->CountTabs()) {
			fRightTabView->Select(activeTab);
		}
	}

	// Restore auto-start preview setting
	bool autoPreview;
	if (settings.FindBool("auto_start_preview", &autoPreview) == B_OK) {
		fAutoStartPreview = autoPreview;
		BMenuItem* item = fControlMenu->FindItem(MSG_TOGGLE_AUTO_PREVIEW);
		if (item != NULL)
			item->SetMarked(fAutoStartPreview);
	}

	// Restore last selected webcam - defer to after Show() so the
	// message loop is running when StartCapture sends frames
	const char* lastDevice;
	if (settings.FindString("last_device", &lastDevice) == B_OK) {
		BMessage restoreMsg(MSG_RESTORE_DEVICE);
		restoreMsg.AddString("device_name", lastDevice);
		PostMessage(&restoreMsg);
	}
}

