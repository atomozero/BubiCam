/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "MainWindow.h"
#include "VideoConsumer.h"
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
#include "StreamServer.h"
#include "DeskbarReplicant.h"
#include "NotificationUtils.h"
#include "VideoFilter.h"
#include "ExportUtils.h"
#include "IconUtils.h"
#include "VideoRecorder.h"
#include "AudioConsumer.h"

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
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <Roster.h>

#include <Autolock.h>
#include <Directory.h>
#include <PropertyInfo.h>
#include <File.h>
#include <FindDirectory.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

// Fullscreen video window - shows only the video stream, Escape to exit
class FullscreenVideoWindow : public BWindow {
public:
	FullscreenVideoWindow(BRect frame, BMessenger parent)
		: BWindow(frame, "Video", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE),
		  fParent(parent)
	{
		SetPulseRate(0);
	}

	void DispatchMessage(BMessage* message, BHandler* handler)
	{
		if (message->what == B_KEY_DOWN) {
			const char* bytes;
			if (message->FindString("bytes", &bytes) == B_OK
				&& bytes[0] == B_ESCAPE) {
				fParent.SendMessage(new BMessage(MSG_FULLSCREEN));
				return;
			}
		}
		BWindow::DispatchMessage(message, handler);
	}

private:
	BMessenger	fParent;
};


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow()
	:
	BWindow(BRect(50, 50, 1300, 800), "BubiCam - Webcam Driver Tester",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS),
	// Order must match declaration order in MainWindow.h
	fIsFullscreen(false),
	fFullscreenWindow(NULL),
	fFullscreenPreview(NULL),
	fSavedLook(B_TITLED_WINDOW_LOOK),
	fSavedFlags(0),
	fMenuBar(NULL),
	fWebcamMenu(NULL),
	fControlMenu(NULL),
	fFormatMenu(NULL),
	fToolsMenu(NULL),
	fAudioMenu(NULL),
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
	fDevVideoWatching(false),
	fSelectedAudioNodeID(-1),
	fIsPreviewActive(false),
	fSavePanel(NULL),
	fLastFrame(NULL),
	fSavingJson(false),
	fMCPServer(NULL),
	fMCPMenuItem(NULL),
	fStreamServer(NULL),
	fStreamMenuItem(NULL),
	fRecorder(NULL),
	fFilterChain(NULL),
	fTimelapseRunner(NULL),
	fTimelapseCount(0),
	fTimelapseInterval(5000000),
	fCircularBufferActive(false),
	fCircularBufferSeconds(10),
	fBufferLock("circular buffer"),
	fFloatingWindow(NULL),
	fAutoStartPreview(true),
	fDriverCrashed(false),
	fWatchdogAlertShown(false),
	fBandwidthAlertShown(false),
	fLastFrameReceived(0),
	fPreviewStartTime(0),
	fLastWatchdogFrameCount(0),
	fLastWatchdogDropCount(0),
	fCatastrophicDropCount(0),
	fWatchdogRunner(NULL)
{
	fWebcamRoster = new WebcamRoster();
	AddHandler(fWebcamRoster);

	// Create MCP server
	fMCPServer = new MCPServer(BMessenger(this));

	// Create stream server
	fStreamServer = new StreamServer(BMessenger(this));

	// Create video filter chain with built-in filters
	fFilterChain = new VideoFilterChain();
	fFilterChain->AddFilter(new GrayscaleFilter());
	fFilterChain->AddFilter(new InvertFilter());
	fFilterChain->AddFilter(new MirrorFilter());
	fFilterChain->AddFilter(new SepiaFilter());

	_BuildMenu();
	_BuildLayout();
	_PopulateWebcamMenu();

	// Start watching for media node changes (hot-plug detection)
	fWebcamRoster->StartWatching();

	// Watch /dev/video for USB device addition/removal
	// This fires BEFORE the USB stack destroys the device, giving us
	// time to release isochronous pipes and prevent kernel panic
	_StartDeviceWatching();

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

	_StopDeviceWatching();

	// Stop node watching (non-blocking)
	if (fWebcamRoster != NULL) {
		fWebcamRoster->StopWatching();
		RemoveHandler(fWebcamRoster);
		// Don't delete fWebcamRoster here - QuitRequested already cleared it,
		// and the destructor of WebcamDevice would re-call StopCapture
		// which can hang. Let the OS reclaim the memory on exit.
	}

	// These are already cleaned up in QuitRequested, but delete handles NULL
	delete fTimelapseRunner;
	fTimelapseRunner = NULL;
	delete fSavePanel;
	delete fLastFrame;
	delete fRecorder;
	delete fFilterChain;

	// Stop stream server
	if (fStreamServer != NULL) {
		fStreamServer->Stop();
		delete fStreamServer;
		fStreamServer = NULL;
	}

	// Stop MCP server - QuitRequested already stopped it and cleared device,
	// so Lock()/Quit() should return quickly
	if (fMCPServer != NULL) {
		if (fMCPServer->IsRunning())
			fMCPServer->Stop();
		if (fMCPServer->LockWithTimeout(1000000) == B_OK)
			fMCPServer->Quit();
		// If LockWithTimeout fails, the server is stuck - let OS clean up
	}
}


void
MainWindow::_BuildMenu()
{
	fMenuBar = new BMenuBar("menubar");

	// File menu
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("About BubiCam" B_UTF8_ELLIPSIS),
		new BMessage(MSG_ABOUT)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Take Screenshot"),
		new BMessage(MSG_SCREENSHOT), 'P'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Start Recording"),
		new BMessage(MSG_RECORD_START), 'G'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Stop Recording"),
		new BMessage(MSG_RECORD_STOP), 'G', B_SHIFT_KEY));
	{
		BMenu* codecMenu = new BMenu(B_TRANSLATE("Recording Codec"));
		BMenuItem* mjpegItem = new BMenuItem("Motion JPEG",
			new BMessage(MSG_CODEC_MJPEG));
		mjpegItem->SetMarked(true);
		codecMenu->AddItem(mjpegItem);
		codecMenu->AddItem(new BMenuItem("Uncompressed RGB32",
			new BMessage(MSG_CODEC_RAW)));
		codecMenu->SetRadioMode(true);
		fileMenu->AddItem(codecMenu);
	}
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Export Info as Text" B_UTF8_ELLIPSIS),
		new BMessage(MSG_EXPORT_INFO), 'E'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Export Info as JSON" B_UTF8_ELLIPSIS),
		new BMessage(MSG_EXPORT_INFO_JSON), 'E', B_SHIFT_KEY));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"), new BMessage(B_QUIT_REQUESTED),
		'Q'));
	fMenuBar->AddItem(fileMenu);

	// Webcam menu
	fWebcamMenu = new BMenu(B_TRANSLATE("Webcam"));
	fWebcamMenu->AddItem(new BMenuItem(B_TRANSLATE("Refresh Devices"),
		new BMessage(MSG_REFRESH_DEVICES), 'R'));
	fWebcamMenu->AddSeparatorItem();
	fMenuBar->AddItem(fWebcamMenu);

	// Format menu
	fFormatMenu = new BMenu(B_TRANSLATE("Format"));
	fFormatMenu->SetEnabled(false);
	fMenuBar->AddItem(fFormatMenu);

	// Control menu
	fControlMenu = new BMenu(B_TRANSLATE("Control"));
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Start Preview"),
		new BMessage(MSG_WEBCAM_START), 'S'));
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Stop Preview"),
		new BMessage(MSG_WEBCAM_STOP), 'T'));
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Force Stop (Driver Frozen)"),
		new BMessage(MSG_FORCE_STOP)));
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Reconnect"),
		new BMessage(MSG_RESTART_PREVIEW), 'R'));
	fControlMenu->AddSeparatorItem();
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Show Controls Panel"),
		new BMessage(MSG_TOGGLE_CONTROLS), 'K'));
	fControlMenu->AddItem(new BMenuItem(B_TRANSLATE("Restore Factory Defaults"),
		new BMessage(MSG_FACTORY_RESET)));
	fControlMenu->AddSeparatorItem();
	{
		BMenuItem* autoPreviewItem = new BMenuItem(B_TRANSLATE("Auto-start Preview on Launch"),
			new BMessage(MSG_TOGGLE_AUTO_PREVIEW));
		autoPreviewItem->SetMarked(fAutoStartPreview);
		fControlMenu->AddItem(autoPreviewItem);
	}
	fControlMenu->AddSeparatorItem();
	fControlMenu->AddItem(new BMenuItem("Toggle Syslog Panel",
		new BMessage(MSG_TOGGLE_SYSLOG)));
	fControlMenu->AddItem(new BMenuItem("Toggle VU Meter",
		new BMessage(MSG_TOGGLE_VUBAR)));
	fControlMenu->AddItem(new BMenuItem("Reset Layout",
		new BMessage(MSG_RESET_LAYOUT)));
	fMenuBar->AddItem(fControlMenu);

	// Audio menu
	fAudioMenu = new BMenu(B_TRANSLATE("Audio"));
	_PopulateAudioMenu();
	fMenuBar->AddItem(fAudioMenu);

	// Filters menu
	BMenu* filterMenu = new BMenu("Filters");
	for (int32 i = 0; i < fFilterChain->CountFilters(); i++) {
		VideoFilter* filter = fFilterChain->FilterAt(i);
		if (filter != NULL) {
			BMessage* msg = new BMessage(MSG_FILTER_TOGGLE);
			msg->AddInt32("index", i);
			BMenuItem* item = new BMenuItem(filter->Name(), msg);
			filterMenu->AddItem(item);
		}
	}
	fMenuBar->AddItem(filterMenu);

	fToolsMenu = new BMenu(B_TRANSLATE("Tools"));
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
	fToolsMenu->AddItem(new BMenuItem("Export Raw Frame",
		new BMessage(MSG_EXPORT_RAW_FRAME)));
	fToolsMenu->AddItem(new BMenuItem("Capture Reference Frame",
		new BMessage(MSG_CAPTURE_REFERENCE), 'B'));
	fToolsMenu->AddItem(new BMenuItem("A/B Compare Mode",
		new BMessage(MSG_TOGGLE_COMPARE), 'B', B_SHIFT_KEY));
	fToolsMenu->AddItem(new BMenuItem("Clear Reference",
		new BMessage(MSG_CLEAR_REFERENCE)));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Start Time-Lapse" B_UTF8_ELLIPSIS,
		new BMessage(MSG_TIMELAPSE_START), 'T'));
	fToolsMenu->AddItem(new BMenuItem("Floating Preview",
		new BMessage(MSG_FLOATING_PREVIEW), 'F', B_SHIFT_KEY));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Circular Buffer (10 sec)",
		new BMessage(MSG_BUFFER_TOGGLE)));
	fToolsMenu->AddItem(new BMenuItem("Save Buffer Now",
		new BMessage(MSG_BUFFER_SAVE), 'S', B_SHIFT_KEY));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Toggle Grid Overlay",
		new BMessage(MSG_TOGGLE_GRID), 'G'));
	BMenu* gridModeMenu = new BMenu("Grid Style");
	gridModeMenu->AddItem(new BMenuItem("Rule of Thirds",
		new BMessage(MSG_GRID_MODE)));
	gridModeMenu->AddItem(new BMenuItem("Center Crosshair",
		new BMessage(MSG_GRID_MODE)));
	gridModeMenu->AddItem(new BMenuItem("Both",
		new BMessage(MSG_GRID_MODE)));
	gridModeMenu->ItemAt(0)->SetMarked(true);
	fToolsMenu->AddItem(gridModeMenu);
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Fullscreen",
		new BMessage(MSG_FULLSCREEN), B_RETURN));
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Restart Media Services" B_UTF8_ELLIPSIS,
		new BMessage(MSG_RESTART_MEDIA), 'M', B_SHIFT_KEY));
	fToolsMenu->AddSeparatorItem();
	fMCPMenuItem = new BMenuItem("Enable MCP Server (Port 9847)",
		new BMessage(MSG_MCP_TOGGLE));
	fToolsMenu->AddItem(fMCPMenuItem);
	fStreamMenuItem = new BMenuItem("Start MJPEG Stream (Port 8080)",
		new BMessage(MSG_STREAM_TOGGLE));
	fToolsMenu->AddItem(fStreamMenuItem);
	fToolsMenu->AddSeparatorItem();
	fToolsMenu->AddItem(new BMenuItem("Show in Deskbar",
		new BMessage(MSG_TOGGLE_DESKBAR)));
	fToolsMenu->AddItem(new BMenuItem("Pixel Inspector",
		new BMessage(MSG_TOGGLE_INSPECTOR), 'I'));
	fToolsMenu->AddItem(new BMenuItem("Export Debug State" B_UTF8_ELLIPSIS,
		new BMessage(MSG_EXPORT_DEBUG)));
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
	fLeftSplit = new BSplitView(B_VERTICAL);
	BSplitView* leftSplit = fLeftSplit;
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
	fRightSplit = new BSplitView(B_VERTICAL);
	BSplitView* rightSplit = fRightSplit;
	rightSplit->AddChild(fRightTabView);
	rightSplit->AddChild(syslogBox);
	rightSplit->SetItemWeight(0, 0.55f, true);
	rightSplit->SetItemWeight(1, 0.45f, true);

	// Main horizontal split
	fMainSplit = new BSplitView(B_HORIZONTAL);
	BSplitView* mainSplit = fMainSplit;
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
		BMenuItem* noDevice = new BMenuItem(B_TRANSLATE("No webcams found"), NULL);
		noDevice->SetEnabled(false);
		fWebcamMenu->AddItem(noDevice);
		fStatusBar->SetText(B_TRANSLATE("No webcams detected"));
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
		fStatusBar->SetText(B_TRANSLATE("Select a webcam from the menu"));
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
				const char* cs = format->colorSpace[0] != '\0'
					? format->colorSpace : "auto";
				label.SetToFormat("%dx%d @ %.1f fps (%s)",
					format->width, format->height,
					format->frameRate, cs);

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
MainWindow::_PopulateAudioMenu()
{
	// Clear existing items
	while (fAudioMenu->CountItems() > 0)
		delete fAudioMenu->RemoveItem((int32)0);

	// "None" option - disable audio entirely
	BMessage* noneMsg = new BMessage(MSG_AUDIO_NONE);
	BMenuItem* noneItem = new BMenuItem("Disabled", noneMsg);
	noneItem->SetMarked(fSelectedAudioNodeID == 0);
	fAudioMenu->AddItem(noneItem);

	// "Auto" option - uses system default
	BMessage* autoMsg = new BMessage(MSG_AUDIO_SOURCE);
	autoMsg->AddInt32("node_id", -1);
	BMenuItem* autoItem = new BMenuItem("Auto (System Default)" B_UTF8_ELLIPSIS, autoMsg);
	autoItem->SetMarked(fSelectedAudioNodeID == -1);
	fAudioMenu->AddItem(autoItem);

	fAudioMenu->AddSeparatorItem();

	// Enumerate audio input nodes
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	// Get all live audio input nodes
	live_node_info liveNodes[32];
	int32 liveCount = 32;
	status_t status = roster->GetLiveNodes(liveNodes, &liveCount, NULL, NULL,
		NULL, B_BUFFER_PRODUCER);

	if (status == B_OK) {
		for (int32 i = 0; i < liveCount; i++) {
			// Check if this node has audio outputs
			media_output outputs[4];
			int32 outputCount = 0;
			if (roster->GetFreeOutputsFor(liveNodes[i].node, outputs, 4,
				&outputCount, B_MEDIA_RAW_AUDIO) == B_OK && outputCount > 0) {

				BMessage* msg = new BMessage(MSG_AUDIO_SOURCE);
				msg->AddInt32("node_id", liveNodes[i].node.node);
				BMenuItem* item = new BMenuItem(liveNodes[i].name, msg);
				if (fSelectedAudioNodeID == liveNodes[i].node.node)
					item->SetMarked(true);
				fAudioMenu->AddItem(item);
			}
		}
	}

	// Also list dormant audio producer nodes
	const int32 kMaxDormant = 32;
	dormant_node_info dormantNodes[kMaxDormant];
	int32 dormantCount = kMaxDormant;
	status = roster->GetDormantNodes(dormantNodes, &dormantCount,
		NULL, NULL, NULL, B_BUFFER_PRODUCER, 0);

	if (status == B_OK && dormantCount > 0) {
		bool addedSeparator = false;
		for (int32 i = 0; i < dormantCount; i++) {
			// Check the dormant node's flavor for audio output
			dormant_flavor_info flavorInfo;
			if (roster->GetDormantFlavorInfoFor(dormantNodes[i], &flavorInfo) == B_OK) {
				bool hasAudio = false;
				for (int32 j = 0; j < flavorInfo.out_format_count; j++) {
					if (flavorInfo.out_formats[j].type == B_MEDIA_RAW_AUDIO) {
						hasAudio = true;
						break;
					}
				}
				if (hasAudio) {
					if (!addedSeparator) {
						fAudioMenu->AddSeparatorItem();
						addedSeparator = true;
					}
					// Use negative addon+flavor as unique ID for dormant nodes
					int32 dormantID = -(dormantNodes[i].addon * 100
						+ dormantNodes[i].flavor_id + 1);
					BMessage* msg = new BMessage(MSG_AUDIO_SOURCE);
					msg->AddInt32("node_id", dormantID);
					msg->AddInt32("addon", dormantNodes[i].addon);
					msg->AddInt32("flavor_id", dormantNodes[i].flavor_id);
					BString label(dormantNodes[i].name);
					label << " (dormant)";
					BMenuItem* item = new BMenuItem(label.String(), msg);
					if (fSelectedAudioNodeID == dormantID)
						item->SetMarked(true);
					fAudioMenu->AddItem(item);
				}
			}
		}
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

	// Pass audio selection to device before starting
	device->SetAudioNodeID(fSelectedAudioNodeID);

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
		BString statusMsg;
		statusMsg.SetToFormat("Changing to %dx%d...",
			(int)selectedFormat->width, (int)selectedFormat->height);
		fStatusBar->SetText(statusMsg.String());
		UpdateIfNeeded();

		// Suppress watchdog alerts during resolution change
		fBandwidthAlertShown = true;
		fWatchdogAlertShown = true;

		_StopPreview();

		// Brief pause to let the driver fully release USB resources
		// before re-acquiring them at a different resolution
		snooze(500000);  // 500ms

		status_t err = fCurrentWebcam->StartCapture(this);
		if (err != B_OK) {
			// Resolution change failed - try to recover with previous format
			// (no notification here - only notify if recovery also fails)
			fCurrentWebcam->ClearRequestedFormat();
			fStatusBar->SetText("Retrying previous format...");
			UpdateIfNeeded();

			snooze(500000);

			err = fCurrentWebcam->StartCapture(this);
			if (err == B_OK) {
				fIsPreviewActive = true;
				fLastFrameReceived = system_time();
				fPreviewStartTime = system_time();
				fLastWatchdogFrameCount = 0;
				fLastWatchdogDropCount = 0;
				fCatastrophicDropCount = 0;
				fBandwidthAlertShown = false;
				fDriverCrashed = false;

				delete fWatchdogRunner;
				fWatchdogRunner = new BMessageRunner(BMessenger(this),
					new BMessage(MSG_WATCHDOG_CHECK), 2000000);

				fStatusBar->SetText("Recovered with previous resolution");
				_UpdateToolbarState();
			} else {
				fStatusBar->SetText("Resolution change failed. Select webcam again.");
				_UpdateToolbarState();
			}
		} else {
			// StartCapture succeeded, set up the preview UI state
			fIsPreviewActive = true;
			fLastFrameReceived = system_time();
			fPreviewStartTime = system_time();
			fLastWatchdogFrameCount = 0;
			fLastWatchdogDropCount = 0;
			fCatastrophicDropCount = 0;
			fBandwidthAlertShown = false;
			fDriverCrashed = false;

			fCamLED->SetBlinking(false);
			fCamLED->SetState(LED_GREEN);

			delete fWatchdogRunner;
			fWatchdogRunner = new BMessageRunner(BMessenger(this),
				new BMessage(MSG_WATCHDOG_CHECK), 2000000);

			BString okMsg;
			okMsg.SetToFormat("Resolution changed to %dx%d",
				(int)selectedFormat->width, (int)selectedFormat->height);
			fStatusBar->SetText(okMsg.String());

			_PopulateFormatMenu();
			_UpdateToolbarState();
			_UpdateDriverInfo();
		}
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
	fPreviewStartTime = system_time();
	fLastWatchdogFrameCount = 0;
	fLastWatchdogDropCount = 0;
	fCatastrophicDropCount = 0;
	fBandwidthAlertShown = false;
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
	// Exit fullscreen video if active
	if (fIsFullscreen)
		_ExitVideoFullscreen();

	// Stop watchdog timer
	delete fWatchdogRunner;
	fWatchdogRunner = NULL;

	// Reset watchdog alert state for next session
	fWatchdogAlertShown = false;
	fBandwidthAlertShown = false;

	// Stop recording if active
	if (fRecorder != NULL && fRecorder->IsRecording())
		_StopRecording();

	if (fCurrentWebcam != NULL && fIsPreviewActive) {
		fStatusBar->SetText("Stopping capture (waiting for USB settle)...");
		UpdateIfNeeded();
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

	// Format (MJPEG/YUY2/etc.)
	if (fCurrentWebcam != NULL) {
		VideoFormat currentFmt = fCurrentWebcam->CurrentFormat();
		fStatsFormat->SetText(
			currentFmt.colorSpace[0] != '\0' ? currentFmt.colorSpace : "---");
		if (strcasecmp(currentFmt.colorSpace, "MJPEG") == 0
			|| strcasecmp(currentFmt.colorSpace, "JPEG") == 0)
			fStatsFormat->SetHighColor(0, 150, 0);
		else
			fStatsFormat->SetHighUIColor(B_PANEL_TEXT_COLOR);
	} else {
		fStatsFormat->SetText("---");
	}

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
		NotificationUtils::Info("Screenshot", path);
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

		case B_NODE_MONITOR:
			_HandleDeviceNodeMonitor(message);
			break;

		case MSG_DEVICES_CHANGED:
		{
			// Auto-refresh triggered by media node watcher (hot-plug)
			if (fIsPreviewActive && fCurrentWebcam != NULL) {
				// Check if our current device's node is still valid
				BMediaRoster* roster = BMediaRoster::Roster();
				if (roster != NULL) {
					media_node node = fCurrentWebcam->MediaNode();
					live_node_info liveInfo;
					// If the node is gone, the device was disconnected
					if (roster->GetLiveNodeInfo(node, &liveInfo) != B_OK) {
						// Device disconnected! Stop preview and refresh
						BString deviceName(fCurrentWebcam->Name());
						_StopPreview();
						{
							BAutolock lock(fWebcamLock);
							fCurrentWebcam = NULL;
							fCurrentWebcamIndex = -1;
						}
						if (fMCPServer != NULL)
							fMCPServer->SetWebcamDevice(NULL);
						fDriverTestView->SetDevice(NULL);

						_PopulateWebcamMenu();

						BString status;
						status.SetToFormat("Disconnected: %s", deviceName.String());
						fStatusBar->SetText(status.String());

						// Try to auto-reconnect if the device reappears
						BMessage* restoreMsg = new BMessage(MSG_RESTORE_DEVICE);
						restoreMsg->AddString("device_name", deviceName.String());
						BMessageRunner::StartSending(BMessenger(this),
							restoreMsg, 2000000, 1);  // retry in 2s
						break;
					}
				}
				// Our node is still alive - a different device changed
				// Don't disrupt the active preview
				fStatusBar->SetText("New device detected (preview still active)");
				break;
			}

			fStatusBar->SetText("Device change detected, refreshing...");
			_PopulateWebcamMenu();

			// Auto-select if only one webcam is available and none selected
			int32 count = fWebcamRoster->CountDevices();
			if (count == 1 && fCurrentWebcam == NULL) {
				_SelectWebcam(0);
				BString status;
				status.SetToFormat("Auto-connected: %s",
					fWebcamRoster->DeviceAt(0)->Name());
				fStatusBar->SetText(status.String());
			} else {
				BString status;
				status.SetToFormat("Hot-plug: %d webcam%s found",
					(int)count, count != 1 ? "s" : "");
				fStatusBar->SetText(status.String());
			}
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
			// Suppress audio levels only if the driver has actually crashed.
			// Audio may work independently from video (e.g., separate audio node),
			// so don't suppress during bandwidth issues.
			if (fDriverCrashed || !fIsPreviewActive) {
				fVUMeter->SetLevel(0.0f, 0.0f);
				break;
			}
			float left = 0.0f, right = 0.0f;
			message->FindFloat("left", &left);
			message->FindFloat("right", &right);
			fVUMeter->SetLevel(left, right);
			break;
		}

		case MSG_AUDIO_BUFFER:
		{
			if (fRecorder != NULL && fRecorder->IsRecording()
				&& fRecorder->HasAudio()) {
				const void* data;
				ssize_t dataSize;
				if (message->FindData("audio_data", B_RAW_TYPE,
						&data, &dataSize) == B_OK) {
					static int32 sAudioLogCount = 0;
					if (++sAudioLogCount <= 3)
						LOG_DEBUG("Recording: AddAudioBuffer %zd bytes", (size_t)dataSize);
					fRecorder->AddAudioBuffer(data, (size_t)dataSize);
				}
			}
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

		case MSG_STREAM_TOGGLE:
		{
			if (fStreamServer->IsRunning()) {
				fStreamServer->Stop();
				fStreamMenuItem->SetLabel("Start MJPEG Stream (Port 8080)");
				fStreamMenuItem->SetMarked(false);
				fStatusBar->SetText("MJPEG stream stopped");
			} else {
				if (fStreamServer->Start(8080) == B_OK) {
					fStreamMenuItem->SetLabel("Stop MJPEG Stream (Port 8080)");
					fStreamMenuItem->SetMarked(true);
					fStatusBar->SetText("MJPEG stream at http://localhost:8080/");
				} else {
					fStatusBar->SetText("Failed to start stream server");
				}
			}
			break;
		}

		case MSG_STREAM_STARTED:
		{
			fStreamMenuItem->SetLabel("Stop MJPEG Stream (Port 8080)");
			fStreamMenuItem->SetMarked(true);
			break;
		}

		case MSG_STREAM_STOPPED:
		{
			fStreamMenuItem->SetLabel("Start MJPEG Stream (Port 8080)");
			fStreamMenuItem->SetMarked(false);
			break;
		}

		case MSG_STREAM_CLIENT:
		{
			int32 count;
			if (message->FindInt32("count", &count) == B_OK) {
				BString status;
				status.SetToFormat("MJPEG stream: %d client%s connected",
					(int)count, count == 1 ? "" : "s");
				fStatusBar->SetText(status.String());
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

		case MSG_FILTER_TOGGLE:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				VideoFilter* filter = fFilterChain->FilterAt(index);
				if (filter != NULL) {
					filter->SetEnabled(!filter->IsEnabled());
					BString msg;
					msg.SetToFormat("Filter '%s': %s", filter->Name(),
						filter->IsEnabled() ? "enabled" : "disabled");
					fStatusBar->SetText(msg.String());
				}
			}
			break;
		}

		case MSG_EXPORT_DEBUG:
			_ExportDebugState();
			break;

		case MSG_TOGGLE_INSPECTOR:
		{
			bool inspect = !fVideoPreview->InspectorMode();
			fVideoPreview->SetInspectorMode(inspect);
			BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_INSPECTOR);
			if (item != NULL)
				item->SetMarked(inspect);
			fStatusBar->SetText(inspect
				? "Pixel Inspector: click on preview to inspect"
				: "Pixel Inspector disabled");
			break;
		}

		case MSG_TOGGLE_SYSLOG:
		{
			// Toggle syslog panel visibility via collapsing split
			bool visible = fRightSplit->IsItemCollapsed(1);
			fRightSplit->SetItemCollapsed(1, !visible);
			break;
		}

		case MSG_TOGGLE_VUBAR:
		{
			bool visible = fLeftSplit->IsItemCollapsed(1);
			fLeftSplit->SetItemCollapsed(1, !visible);
			break;
		}

		case MSG_RESET_LAYOUT:
			fMainSplit->SetItemWeight(0, 0.25f, true);
			fMainSplit->SetItemWeight(1, 0.75f, true);
			fLeftSplit->SetItemWeight(0, 0.78f, true);
			fLeftSplit->SetItemWeight(1, 0.22f, true);
			fLeftSplit->SetItemCollapsed(1, false);
			fRightSplit->SetItemWeight(0, 0.55f, true);
			fRightSplit->SetItemWeight(1, 0.45f, true);
			fRightSplit->SetItemCollapsed(1, false);
			fStatusBar->SetText("Layout reset to defaults");
			break;

		case MSG_CODEC_MJPEG:
			if (fRecorder != NULL)
				fRecorder->SetCodec(VIDEO_CODEC_MJPEG);
			fStatusBar->SetText("Recording codec: Motion JPEG");
			break;

		case MSG_CODEC_RAW:
			if (fRecorder != NULL)
				fRecorder->SetCodec(VIDEO_CODEC_RAW);
			fStatusBar->SetText("Recording codec: Uncompressed RGB32");
			break;

		case MSG_TOGGLE_DESKBAR:
		{
			if (DeskbarReplicant::IsInstalledInDeskbar()) {
				DeskbarReplicant::RemoveFromDeskbar();
				fStatusBar->SetText("Removed from Deskbar");
			} else {
				status_t err = DeskbarReplicant::InstallInDeskbar();
				if (err == B_OK)
					fStatusBar->SetText("Installed in Deskbar");
				else
					fStatusBar->SetText("Failed to install in Deskbar");
			}
			// Update menu checkmark
			BMenuItem* item = fToolsMenu->FindItem(MSG_TOGGLE_DESKBAR);
			if (item != NULL)
				item->SetMarked(DeskbarReplicant::IsInstalledInDeskbar());
			break;
		}

		case MSG_RECORD_START:
			_StartRecording();
			break;

		case MSG_RECORD_STOP:
			_StopRecording();
			break;

		case MSG_TIMELAPSE_START:
			if (fTimelapseRunner != NULL)
				_StopTimelapse();
			else
				_StartTimelapse();
			break;

		case MSG_TIMELAPSE_STOP:
			_StopTimelapse();
			break;

		case MSG_TIMELAPSE_TICK:
			_TimelapseTick();
			break;

		case MSG_FLOATING_PREVIEW:
			_ShowFloatingPreview();
			break;

		case MSG_BUFFER_TOGGLE:
			_ToggleCircularBuffer();
			break;

		case MSG_BUFFER_SAVE:
			_SaveCircularBuffer();
			break;

		case MSG_RESTART_PREVIEW:
			// Reconnect: stop and restart the stream
			if (fIsPreviewActive)
				_StopPreview();
			_StartPreview();
			break;

		case MSG_WATCHDOG_CHECK:
			_CheckWatchdog();
			break;

		case MSG_FORCE_STOP:
			_ForceStop();
			break;

		case MSG_FACTORY_RESET:
			_FactoryResetControls();
			break;

		case MSG_TOGGLE_GRID:
		{
			bool show = !fVideoPreview->ShowGrid();
			fVideoPreview->SetShowGrid(show);
			fStatusBar->SetText(show ? "Grid overlay on" : "Grid overlay off");
			break;
		}

		case MSG_GRID_MODE:
		{
			// Find which submenu item was clicked
			BMenuItem* item = NULL;
			if (message->FindPointer("source", (void**)&item) == B_OK && item != NULL) {
				BMenu* menu = item->Menu();
				if (menu != NULL) {
					int32 index = menu->IndexOf(item);
					fVideoPreview->SetGridMode(index);
					fVideoPreview->SetShowGrid(true);
					// Update marks
					for (int32 i = 0; i < menu->CountItems(); i++)
						menu->ItemAt(i)->SetMarked(i == index);
				}
			}
			break;
		}

		case MSG_FULLSCREEN:
		{
			if (!fIsFullscreen)
				_EnterVideoFullscreen();
			else
				_ExitVideoFullscreen();
			break;
		}

		case MSG_EXPORT_RAW_FRAME:
			_ExportRawFrame();
			break;

		case MSG_AUDIO_SOURCE:
		{
			int32 nodeID;
			if (message->FindInt32("node_id", &nodeID) == B_OK) {
				// Auto (-1) uses the safe system audio input, no warning needed.
				// Specific nodes may use webcam driver audio which can crash.
				if (nodeID > 0) {
					BAlert* alert = new BAlert("Audio Warning",
						"WARNING: Audio input connection may crash the "
						"media_addon_server due to bugs in some Haiku audio "
						"drivers (divide-by-zero in Connect()).\n\n"
						"This can kill all audio on the system until reboot.\n\n"
						"Continue at your own risk?",
						"Cancel", "Enable Audio", NULL,
						B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->SetShortcut(0, B_ESCAPE);
					if (alert->Go() != 1)
						break;
				}

				fSelectedAudioNodeID = nodeID;
				// Update menu marks
				for (int32 i = 0; i < fAudioMenu->CountItems(); i++) {
					BMenuItem* item = fAudioMenu->ItemAt(i);
					if (item != NULL)
						item->SetMarked(false);
				}
				BMenuItem* srcItem = fAudioMenu->FindItem(message->what);
				if (srcItem != NULL) {
					// Find the item that sent this message
					for (int32 i = 0; i < fAudioMenu->CountItems(); i++) {
						BMenuItem* item = fAudioMenu->ItemAt(i);
						if (item != NULL && item->Message() != NULL
							&& item->Message()->what == MSG_AUDIO_SOURCE) {
							int32 itemNodeID;
							if (item->Message()->FindInt32("node_id", &itemNodeID) == B_OK
								&& itemNodeID == nodeID) {
								item->SetMarked(true);
								break;
							}
						}
					}
				}
				// Apply: restart preview to reconnect audio
				if (fCurrentWebcam != NULL) {
					fCurrentWebcam->SetAudioNodeID(nodeID);
					if (fIsPreviewActive) {
						_StopPreview();
						_StartPreview();
						fStatusBar->SetText("Audio source changed, preview restarted");
					}
				}
			}
			break;
		}

		case MSG_AUDIO_NONE:
		{
			fSelectedAudioNodeID = 0;
			for (int32 i = 0; i < fAudioMenu->CountItems(); i++) {
				BMenuItem* item = fAudioMenu->ItemAt(i);
				if (item != NULL)
					item->SetMarked(item->Message() != NULL
						&& item->Message()->what == MSG_AUDIO_NONE);
			}
			if (fCurrentWebcam != NULL) {
				fCurrentWebcam->SetAudioNodeID(0);
				if (fIsPreviewActive) {
					_StopPreview();
					_StartPreview();
					fStatusBar->SetText("Audio disabled, preview restarted");
				}
			}
			fVUMeter->SetLevel(0.0f, 0.0f);
			break;
		}

		// Scripting (hey BubiCam get/set Property)
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		case B_EXECUTE_PROPERTY:
		{
			BMessage specifier;
			int32 index;
			int32 what;
			const char* property;
			if (message->GetCurrentSpecifier(&index, &specifier, &what,
					&property) != B_OK) {
				BWindow::MessageReceived(message);
				break;
			}

			BMessage reply(B_REPLY);

			if (strcmp(property, "Status") == 0 &&
				message->what == B_GET_PROPERTY) {
				const char* status = "idle";
				if (fRecorder != NULL && fRecorder->IsRecording())
					status = "recording";
				else if (fIsPreviewActive)
					status = "streaming";
				reply.AddString("result", status);
			} else if (strcmp(property, "FPS") == 0 &&
				message->what == B_GET_PROPERTY) {
				WebcamDevice* webcam = NULL;
				{
					BAutolock lock(fWebcamLock);
					webcam = fCurrentWebcam;
				}
				reply.AddFloat("result",
					webcam != NULL ? webcam->CurrentFPS() : 0.0f);
			} else if (strcmp(property, "Device") == 0 &&
				message->what == B_GET_PROPERTY) {
				WebcamDevice* webcam = NULL;
				{
					BAutolock lock(fWebcamLock);
					webcam = fCurrentWebcam;
				}
				reply.AddString("result",
					webcam != NULL ? webcam->Name() : "none");
			} else if (strcmp(property, "Streaming") == 0) {
				if (message->what == B_GET_PROPERTY) {
					reply.AddBool("result", fIsPreviewActive);
				} else {
					bool value;
					if (specifier.FindBool("data", &value) == B_OK ||
						message->FindBool("data", &value) == B_OK) {
						if (value && !fIsPreviewActive && fCurrentWebcam != NULL)
							_StartPreview();
						else if (!value && fIsPreviewActive)
							_StopPreview();
						reply.AddInt32("error", B_OK);
					}
				}
			} else if (strcmp(property, "Recording") == 0) {
				if (message->what == B_GET_PROPERTY) {
					reply.AddBool("result",
						fRecorder != NULL && fRecorder->IsRecording());
				} else {
					bool value;
					if (specifier.FindBool("data", &value) == B_OK ||
						message->FindBool("data", &value) == B_OK) {
						if (value)
							_StartRecording();
						else
							_StopRecording();
						reply.AddInt32("error", B_OK);
					}
				}
			} else if (strcmp(property, "Screenshot") == 0 &&
				message->what == B_EXECUTE_PROPERTY) {
				_TakeScreenshot();
				reply.AddInt32("error", B_OK);
			} else if (strcmp(property, "FramesCaptured") == 0 &&
				message->what == B_GET_PROPERTY) {
				WebcamDevice* webcam = NULL;
				{
					BAutolock lock(fWebcamLock);
					webcam = fCurrentWebcam;
				}
				reply.AddInt32("result",
					webcam != NULL ? (int32)webcam->FramesCaptured() : 0);
			} else if (strcmp(property, "FramesDropped") == 0 &&
				message->what == B_GET_PROPERTY) {
				WebcamDevice* webcam = NULL;
				{
					BAutolock lock(fWebcamLock);
					webcam = fCurrentWebcam;
				}
				reply.AddInt32("result",
					webcam != NULL ? (int32)webcam->FramesDropped() : 0);
			} else {
				BWindow::MessageReceived(message);
				break;
			}

			reply.AddInt32("error", B_OK);
			message->SendReply(&reply);
			break;
		}

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
	fStatsFormat = new BStringView("statsFormat", "---");
	fStatsFormat->SetAlignment(B_ALIGN_CENTER);
	fStatsFPS = new BStringView("statsFPS", "--- fps");
	fStatsFPS->SetAlignment(B_ALIGN_CENTER);
	fStatsFrames = new BStringView("statsFrames", "0 frames");
	fStatsFrames->SetAlignment(B_ALIGN_CENTER);
	fStatsDropped = new BStringView("statsDropped", "0 dropped");
	fStatsDropped->SetAlignment(B_ALIGN_CENTER);

	BFont smallFont(be_plain_font);
	smallFont.SetSize(10);
	fStatsResolution->SetFont(&smallFont);
	fStatsFormat->SetFont(&smallFont);
	fStatsFPS->SetFont(&smallFont);
	fStatsFrames->SetFont(&smallFont);
	fStatsDropped->SetFont(&smallFont);

	fCamLED = new LEDView("camLED");

	BLayoutBuilder::Group<>(statsBar, B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
		.Add(fCamLED)
		.Add(fStatsResolution)
		.Add(fStatsFormat)
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

	if (bitmap == NULL || !bitmap->IsValid())
		return;

	// Sanity check: reject frames with unreasonable dimensions
	BRect bounds = bitmap->Bounds();
	int32 w = (int32)(bounds.Width() + 1);
	int32 h = (int32)(bounds.Height() + 1);
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
		return;

	// Apply video filters before display
	fFilterChain->ApplyAll(bitmap);

	fVideoPreview->SetFrame(bitmap);

	// Forward frame to fullscreen preview if active
	if (fIsFullscreen && fFullscreenPreview != NULL)
		fFullscreenPreview->SetFrame(bitmap);

	// Update resolution info from actual frame dimensions
	BRect bitmapBounds = bitmap->Bounds();
	int32 frameWidth = (int32)(bitmapBounds.Width() + 1);
	int32 frameHeight = (int32)(bitmapBounds.Height() + 1);
	fVideoPreview->SetResolution(frameWidth, frameHeight);

	// Sync device's current format with actual frame resolution
	{
		BAutolock lock(fWebcamLock);
		if (fCurrentWebcam != NULL) {
			fCurrentWebcam->UpdateActualResolution(frameWidth, frameHeight);
		}
	}

	// Refresh Driver Info once after first frames arrive, so it shows
	// the actual resolution instead of the negotiated one
	if (fVideoPreview->FramesReceived() == 5) {
		BAutolock lock(fWebcamLock);
		if (fCurrentWebcam != NULL)
			fDriverInfo->SetDevice(fCurrentWebcam, fIsPreviewActive);
	}

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

	// Feed frame to stream server
	if (fStreamServer != NULL && fStreamServer->IsRunning())
		fStreamServer->FeedFrame(bitmap);

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

	// Add to circular buffer if active
	if (fCircularBufferActive && fLastFrame != NULL) {
		BAutolock bufLock(fBufferLock);

		// Compress to JPEG for memory efficiency
		uint8* jpegData = NULL;
		unsigned long jpegSize = 0;

		struct jpeg_compress_struct cinfo;
		struct jpeg_error_mgr jerr;
		cinfo.err = jpeg_std_error(&jerr);
		jpeg_create_compress(&cinfo);

		jpeg_mem_dest(&cinfo, &jpegData, &jpegSize);

		int32 w = (int32)(fLastFrame->Bounds().Width() + 1);
		int32 h = (int32)(fLastFrame->Bounds().Height() + 1);
		cinfo.image_width = w;
		cinfo.image_height = h;
		cinfo.input_components = 3;
		cinfo.in_color_space = JCS_RGB;
		jpeg_set_defaults(&cinfo);
		jpeg_set_quality(&cinfo, 80, TRUE);
		jpeg_start_compress(&cinfo, TRUE);

		uint8* bits = (uint8*)fLastFrame->Bits();
		int32 bpr = fLastFrame->BytesPerRow();
		uint8* rowBuf = new uint8[w * 3];

		while (cinfo.next_scanline < cinfo.image_height) {
			uint8* src = bits + cinfo.next_scanline * bpr;
			for (int32 x = 0; x < w; x++) {
				rowBuf[x * 3 + 0] = src[x * 4 + 2]; // R
				rowBuf[x * 3 + 1] = src[x * 4 + 1]; // G
				rowBuf[x * 3 + 2] = src[x * 4 + 0]; // B
			}
			JSAMPROW row = rowBuf;
			jpeg_write_scanlines(&cinfo, &row, 1);
		}

		jpeg_finish_compress(&cinfo);
		jpeg_destroy_compress(&cinfo);
		delete[] rowBuf;

		if (jpegData != NULL && jpegSize > 0) {
			BufferedFrame* bf = new BufferedFrame();
			bf->jpegData = jpegData;
			bf->jpegSize = (uint32)jpegSize;
			bf->timestamp = system_time();
			fCircularBuffer.AddItem(bf);

			// Trim old frames beyond the buffer window
			bigtime_t cutoff = bf->timestamp
				- (bigtime_t)fCircularBufferSeconds * 1000000;
			while (fCircularBuffer.CountItems() > 1) {
				BufferedFrame* oldest = fCircularBuffer.ItemAt(0);
				if (oldest->timestamp < cutoff)
					delete fCircularBuffer.RemoveItemAt(0);
				else
					break;
			}
		}
	}

	// Update floating preview window if open
	if (fFloatingWindow != NULL && fFloatingWindow->LockLooperWithTimeout(1000) == B_OK) {
		VideoPreviewView* floatView = dynamic_cast<VideoPreviewView*>(
			fFloatingWindow->ChildAt(0));
		if (floatView != NULL)
			floatView->SetFrame(bitmap);
		fFloatingWindow->UnlockLooper();
	}
}


void
MainWindow::_CheckWatchdog()
{
	if (!fIsPreviewActive || fLastFrameReceived == 0)
		return;

	// Check for bandwidth/timeout issues in early capture phase
	// The driver may be receiving USB isochronous data too slowly
	// (insufficient alternate setting bandwidth), causing frame timeouts
	if (!fBandwidthAlertShown && fPreviewStartTime > 0) {
		bigtime_t timeSinceStart = system_time() - fPreviewStartTime;
		uint32 currentFrames = 0;
		{
			BAutolock lock(fWebcamLock);
			if (fCurrentWebcam != NULL)
				currentFrames = fCurrentWebcam->FramesCaptured();
		}

		// After 4 seconds with zero frames: likely bandwidth issue
		if (timeSinceStart > 4000000 && currentFrames == 0) {
			fBandwidthAlertShown = true;

			fCamLED->SetState(LED_YELLOW);
			fCamLED->SetBlinking(true);

			fStatusBar->SetText(
				"No frames received - possible USB bandwidth issue. "
				"Try a lower resolution.");
			fStatusBar->SetHighColor(200, 100, 0);

			BAlert* alert = new BAlert("No Video Signal",
				"No video frames have been received from the webcam.\n\n"
				"This is likely caused by insufficient USB bandwidth.\n"
				"The driver may have selected a transfer mode that is\n"
				"too slow for the current resolution.\n\n"
				"Check the syslog for 'WaitFrame TIMEOUT' messages.\n\n"
				"Suggested fixes:\n"
				"  \xe2\x80\xa2 Try a lower resolution (320x240 or 640x480)\n"
				"  \xe2\x80\xa2 Disconnect other USB devices\n"
				"  \xe2\x80\xa2 Use a different USB port",
				"OK", "Try Lower Resolution", NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			int32 choice = alert->Go();
			if (choice == 1) {
				// Try to switch to lowest available resolution
				BAutolock lock(fWebcamLock);
				if (fCurrentWebcam != NULL) {
					const BObjectList<VideoFormat>& formats =
						fCurrentWebcam->SupportedFormats();
					if (formats.CountItems() > 0) {
						// Find smallest resolution
						VideoFormat* smallest = formats.ItemAt(0);
						for (int32 i = 1; i < formats.CountItems(); i++) {
							VideoFormat* f = formats.ItemAt(i);
							if (f->width * f->height
								< smallest->width * smallest->height)
								smallest = f;
						}
						fCurrentWebcam->SetRequestedFormat(*smallest);
						lock.Unlock();

						// Restart preview with new format
						_StopPreview();
						_StartPreview();

						BString msg;
						msg.SetToFormat("Switched to %dx%d - retrying...",
							(int)smallest->width, (int)smallest->height);
						fStatusBar->SetText(msg.String());
					}
				}
			}
			return;
		}

		// After 6 seconds, if FPS is very low compared to expected
		if (timeSinceStart > 6000000 && currentFrames > 0
			&& !fBandwidthAlertShown) {
			float elapsed = timeSinceStart / 1000000.0f;
			float actualFPS = currentFrames / elapsed;

			if (actualFPS < 2.0f) {
				fBandwidthAlertShown = true;

				BString warning;
				warning.SetToFormat(
					"Low frame rate: %.1f fps (expected 15-30). "
					"USB bandwidth may be limited - try a lower resolution.",
					actualFPS);
				fStatusBar->SetText(warning.String());
				fStatusBar->SetHighColor(200, 100, 0);
			}
		}
	}

	// Check for catastrophic drop rate (XHCI failure scenario)
	// If drops keep growing but captured frames don't, the USB controller
	// is losing almost all isochronous transfers
	{
		uint32 currentFrames = 0;
		uint32 currentDrops = 0;
		BAutolock lock(fWebcamLock);
		if (fCurrentWebcam != NULL) {
			currentFrames = fCurrentWebcam->FramesCaptured();
			currentDrops = fCurrentWebcam->FramesDropped();
		}
		lock.Unlock();

		// Calculate drops since last watchdog check (every 2 seconds)
		uint32 newFrames = currentFrames - fLastWatchdogFrameCount;
		uint32 newDrops = currentDrops - fLastWatchdogDropCount;
		fLastWatchdogFrameCount = currentFrames;
		fLastWatchdogDropCount = currentDrops;

		// If we have significant activity but >80% drops, it's catastrophic
		uint32 totalNew = newFrames + newDrops;
		if (totalNew > 5) {
			float dropRate = (float)newDrops / totalNew;
			if (dropRate > 0.80f) {
				fCatastrophicDropCount++;
			} else {
				fCatastrophicDropCount = 0;
			}

			// 3 consecutive watchdog cycles (6 seconds) with >80% drops
			// means USB controller is failing - auto-stop to prevent hang
			if (fCatastrophicDropCount >= 3) {
				fprintf(stderr, "BubiCam: Catastrophic drop rate (>80%%) for "
					"6+ seconds - auto-stopping to prevent hang\n");

				fStatusBar->SetText(
					"Auto-stopped: USB transfer failures (>80% packet loss). "
					"Try a lower resolution or different USB port.");
				fStatusBar->SetHighColor(200, 0, 0);

				fCamLED->SetState(LED_RED);
				fCamLED->SetBlinking(true);

				_ForceStop();
				return;
			}
		}
	}

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


void
MainWindow::_FactoryResetControls()
{
	if (fCurrentWebcam == NULL || !fCurrentWebcam->IsNodeInstantiated()) {
		BAlert* alert = new BAlert("Error",
			"No active webcam.\nStart the preview first.", "OK");
		alert->Go();
		return;
	}

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return;

	BParameterWeb* web = NULL;
	status_t status = roster->GetParameterWebFor(
		fCurrentWebcam->MediaNode(), &web);
	if (status != B_OK || web == NULL)
		return;

	int32 resetCount = 0;
	for (int32 i = 0; i < web->CountParameters(); i++) {
		BParameter* param = web->ParameterAt(i);
		if (param == NULL || param->Type() == BParameter::B_NULL_PARAMETER)
			continue;

		if (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
			BContinuousParameter* cont =
				dynamic_cast<BContinuousParameter*>(param);
			if (cont != NULL) {
				float midpoint = (cont->MinValue() + cont->MaxValue()) / 2.0f;
				cont->SetValue(&midpoint, sizeof(float), system_time());
				resetCount++;
			}
		} else if (param->Type() == BParameter::B_DISCRETE_PARAMETER) {
			BDiscreteParameter* disc =
				dynamic_cast<BDiscreteParameter*>(param);
			if (disc != NULL && disc->CountItems() > 0) {
				int32 defaultVal = 0;
				disc->SetValue(&defaultVal, sizeof(int32), system_time());
				resetCount++;
			}
		}
	}

	delete web;

	// Reload controls panel to reflect new values
	fWebcamControls->RefreshControls();

	BString msg;
	msg.SetToFormat("Reset %d parameters to factory defaults.", (int)resetCount);
	fStatusBar->SetText(msg.String());
}


void
MainWindow::_EnterVideoFullscreen()
{
	if (fIsFullscreen)
		return;

	BScreen screen(this);
	BRect screenFrame = screen.Frame();

	fFullscreenPreview = new VideoPreviewView("fullscreenPreview");
	// Set background to black for fullscreen before adding to window
	rgb_color black = {0, 0, 0, 255};
	fFullscreenPreview->SetBackgroundColor(black);

	fFullscreenWindow = new FullscreenVideoWindow(screenFrame,
		BMessenger(this));

	// Use a layout so the preview fills the entire window
	fFullscreenWindow->SetLayout(new BGroupLayout(B_VERTICAL, 0));
	fFullscreenWindow->AddChild(fFullscreenPreview);

	// Also explicitly resize the view to match the window bounds
	// in case the layout hasn't taken effect yet
	BRect windowBounds = fFullscreenWindow->Bounds();
	fFullscreenPreview->ResizeTo(windowBounds.Width(), windowBounds.Height());
	fFullscreenPreview->MoveTo(0, 0);

	fFullscreenWindow->Show();

	fIsFullscreen = true;

	// Copy grid/histogram state from main preview
	fFullscreenPreview->SetShowGrid(fVideoPreview->ShowGrid());
	fFullscreenPreview->SetGridMode(fVideoPreview->GridMode());

	fStatusBar->SetText("Fullscreen video - press Escape to exit");
}


void
MainWindow::_ExitVideoFullscreen()
{
	if (!fIsFullscreen)
		return;

	if (fFullscreenWindow != NULL) {
		fFullscreenWindow->Lock();
		fFullscreenWindow->Quit();
		fFullscreenWindow = NULL;
		fFullscreenPreview = NULL;  // deleted by window
	}

	fIsFullscreen = false;
	fStatusBar->SetText("Fullscreen exited");
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

	// Only send system notification if not already shown recently
	static bigtime_t sLastFrozenNotification = 0;
	bigtime_t now = system_time();
	if (now - sLastFrozenNotification > 30000000) {  // 30 second debounce
		sLastFrozenNotification = now;
		NotificationUtils::Error("Driver Frozen",
			"The webcam driver stopped responding. Preview was force-stopped.");
	}
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

	status_t status;

	// Start with audio if the webcam supports it
	WebcamDevice* webcam = NULL;
	{
		BAutolock lock(fWebcamLock);
		webcam = fCurrentWebcam;
	}
	if (webcam != NULL && webcam->SupportsAudio()) {
		LOG_INFO("Recording: StartWithAudio %.0f Hz, %d ch, %d bit",
			webcam->AudioSampleRate(), (int)webcam->AudioChannels(),
			(int)webcam->AudioBitsPerSample());
		status = fRecorder->StartWithAudio(filePath.Path(), width, height, fps,
			webcam->AudioSampleRate(), webcam->AudioChannels(),
			webcam->AudioBitsPerSample());
	} else {
		LOG_INFO("Recording: Start (no audio, supportsAudio=%d, webcam=%p)",
			webcam ? webcam->SupportsAudio() : -1, webcam);
		status = fRecorder->Start(filePath.Path(), width, height, fps);
	}

	if (status != B_OK) {
		BString error;
		error.SetToFormat("Failed to start recording:\n%s", strerror(status));
		BAlert* alert = new BAlert("Error", error.String(), "OK");
		alert->Go();
		return;
	}

	// Connect recorder directly to audio consumer for zero-loss recording
	bool hasAudio = fRecorder->HasAudio();
	if (hasAudio && webcam != NULL) {
		webcam->SetAudioSink(fRecorder);
		LOG_DEBUG("Recording: routed audio to recorder sink %p", fRecorder);
	}

	BString statusMsg;
	statusMsg.SetToFormat("Recording%s to %s",
		hasAudio ? " (with audio)" : "", filename.String());
	fStatusBar->SetText(statusMsg.String());

	_UpdateToolbarState();
}


void
MainWindow::_StopRecording()
{
	if (fRecorder == NULL || !fRecorder->IsRecording())
		return;

	// Disconnect recorder from audio consumer BEFORE stopping
	// to prevent writes to a stopped recorder
	WebcamDevice* webcam = NULL;
	{
		BAutolock lock(fWebcamLock);
		webcam = fCurrentWebcam;
	}
	if (webcam != NULL)
		webcam->ClearAudioSink();

	uint32 frames = fRecorder->FramesRecorded();
	bigtime_t duration = fRecorder->Duration();

	fRecorder->Stop();

	BString statusMsg;
	statusMsg.SetToFormat("Recording saved: %u frames, %.1f seconds",
		(unsigned)frames, duration / 1000000.0);
	fStatusBar->SetText(statusMsg.String());
	NotificationUtils::Info("Recording Saved", statusMsg.String());

	_UpdateToolbarState();
}


// ============================================================================
// Time-Lapse
// ============================================================================

void
MainWindow::_StartTimelapse()
{
	if (!fIsPreviewActive || fLastFrame == NULL) {
		BAlert* alert = new BAlert("Error",
			"Start the video preview first.", "OK");
		alert->Go();
		return;
	}

	// Ask user for interval
	BAlert* alert = new BAlert("Time-Lapse Interval",
		"Choose capture interval:",
		"1 sec", "5 sec", "30 sec",
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	int32 choice = alert->Go();
	if (choice < 0)
		return;

	bigtime_t intervals[] = { 1000000, 5000000, 30000000 };
	fTimelapseInterval = intervals[choice];

	// Create output directory
	BPath dirPath = ExportUtils::GetScreenshotDirectory();
	BString folderName("BubiCam_Timelapse_");
	folderName << ExportUtils::GetTimestamp();
	fTimelapsePath.SetTo(dirPath.Path());
	fTimelapsePath.Append(folderName.String());

	create_directory(fTimelapsePath.Path(), 0755);

	fTimelapseCount = 0;

	// Start the timer
	BMessage tickMsg(MSG_TIMELAPSE_TICK);
	fTimelapseRunner = new BMessageRunner(BMessenger(this),
		&tickMsg, fTimelapseInterval);

	// Capture first frame immediately
	_TimelapseTick();

	// Update menu item
	BMenuItem* item = fToolsMenu->FindItem(MSG_TIMELAPSE_START);
	if (item != NULL) {
		item->SetLabel("Stop Time-Lapse");
		item->SetMarked(true);
	}

	BString statusMsg;
	statusMsg.SetToFormat("Time-lapse started (every %d sec) to %s",
		(int)(fTimelapseInterval / 1000000), folderName.String());
	fStatusBar->SetText(statusMsg.String());
}


void
MainWindow::_StopTimelapse()
{
	delete fTimelapseRunner;
	fTimelapseRunner = NULL;

	BMenuItem* item = fToolsMenu->FindItem(MSG_TIMELAPSE_START);
	if (item != NULL) {
		item->SetLabel("Start Time-Lapse" B_UTF8_ELLIPSIS);
		item->SetMarked(false);
	}

	BString statusMsg;
	statusMsg.SetToFormat("Time-lapse stopped: %u frames captured",
		(unsigned)fTimelapseCount);
	fStatusBar->SetText(statusMsg.String());
}


void
MainWindow::_TimelapseTick()
{
	if (fLastFrame == NULL || !fIsPreviewActive) {
		_StopTimelapse();
		return;
	}

	// Save current frame as JPEG
	BString filename;
	filename.SetToFormat("frame_%06u.jpg", (unsigned)fTimelapseCount);

	BPath filePath(fTimelapsePath);
	filePath.Append(filename.String());

	ExportUtils::SaveScreenshot(fLastFrame, filePath.Path(), 'JPEG');
	fTimelapseCount++;

	BString statusMsg;
	statusMsg.SetToFormat("Time-lapse: %u frames (every %d sec)",
		(unsigned)fTimelapseCount, (int)(fTimelapseInterval / 1000000));
	fStatusBar->SetText(statusMsg.String());
}


// ============================================================================
// Circular Buffer
// ============================================================================

void
MainWindow::_ToggleCircularBuffer()
{
	fCircularBufferActive = !fCircularBufferActive;

	BMenuItem* item = fToolsMenu->FindItem(MSG_BUFFER_TOGGLE);
	if (item != NULL)
		item->SetMarked(fCircularBufferActive);

	if (fCircularBufferActive) {
		BString statusMsg;
		statusMsg.SetToFormat("Circular buffer active (keeping last %d sec)",
			(int)fCircularBufferSeconds);
		fStatusBar->SetText(statusMsg.String());
	} else {
		BAutolock lock(fBufferLock);
		for (int32 i = 0; i < fCircularBuffer.CountItems(); i++)
			delete fCircularBuffer.ItemAt(i);
		fCircularBuffer.MakeEmpty();
		fStatusBar->SetText("Circular buffer disabled");
	}
}


void
MainWindow::_SaveCircularBuffer()
{
	BAutolock lock(fBufferLock);

	int32 count = fCircularBuffer.CountItems();
	if (count == 0) {
		fStatusBar->SetText("Buffer empty - enable buffer and capture first");
		return;
	}

	// Save buffered frames as AVI using VideoRecorder
	BPath dirPath = ExportUtils::GetScreenshotDirectory();
	BString filename("BubiCam_Buffer_");
	filename << ExportUtils::GetTimestamp();
	filename << ".avi";

	BPath filePath(dirPath);
	filePath.Append(filename.String());

	// Calculate actual FPS from buffered frames
	BufferedFrame* first = fCircularBuffer.ItemAt(0);
	BufferedFrame* last = fCircularBuffer.ItemAt(count - 1);
	bigtime_t span = last->timestamp - first->timestamp;
	float fps = span > 0 ? (count * 1000000.0f / span) : 30.0f;

	// Get frame dimensions from fLastFrame
	int32 width = 640, height = 480;
	if (fLastFrame != NULL) {
		width = (int32)(fLastFrame->Bounds().Width() + 1);
		height = (int32)(fLastFrame->Bounds().Height() + 1);
	}

	VideoRecorder recorder;
	status_t status = recorder.Start(filePath.Path(), width, height, fps);
	if (status != B_OK) {
		fStatusBar->SetText("Failed to save buffer");
		return;
	}

	// Decompress each buffered JPEG and add to recorder
	for (int32 i = 0; i < count; i++) {
		BufferedFrame* bf = fCircularBuffer.ItemAt(i);

		// Decompress JPEG to bitmap
		struct jpeg_decompress_struct dinfo;
		struct jpeg_error_mgr jerr;
		dinfo.err = jpeg_std_error(&jerr);
		jpeg_create_decompress(&dinfo);
		jpeg_mem_src(&dinfo, bf->jpegData, bf->jpegSize);
		jpeg_read_header(&dinfo, TRUE);
		dinfo.out_color_space = JCS_RGB;
		jpeg_start_decompress(&dinfo);

		BBitmap frameBitmap(BRect(0, 0, dinfo.output_width - 1,
			dinfo.output_height - 1), B_RGB32);

		uint8* dst = (uint8*)frameBitmap.Bits();
		int32 dstBpr = frameBitmap.BytesPerRow();
		uint8* rowBuf = new uint8[dinfo.output_width * 3];

		while (dinfo.output_scanline < dinfo.output_height) {
			JSAMPROW row = rowBuf;
			jpeg_read_scanlines(&dinfo, &row, 1);
			uint8* dstRow = dst + (dinfo.output_scanline - 1) * dstBpr;
			for (uint32 x = 0; x < dinfo.output_width; x++) {
				dstRow[x * 4 + 0] = rowBuf[x * 3 + 2]; // B
				dstRow[x * 4 + 1] = rowBuf[x * 3 + 1]; // G
				dstRow[x * 4 + 2] = rowBuf[x * 3 + 0]; // R
				dstRow[x * 4 + 3] = 255;
			}
		}

		delete[] rowBuf;
		jpeg_finish_decompress(&dinfo);
		jpeg_destroy_decompress(&dinfo);

		recorder.AddFrame(&frameBitmap);
	}

	recorder.Stop();

	BString statusMsg;
	statusMsg.SetToFormat("Buffer saved: %d frames (%.1f sec) to %s",
		(int)count, span / 1000000.0, filename.String());
	fStatusBar->SetText(statusMsg.String());
}


// ============================================================================
// Floating Preview Window
// ============================================================================

void
MainWindow::_ShowFloatingPreview()
{
	if (fFloatingWindow != NULL && fFloatingWindow->LockLooper()) {
		// Already exists - bring to front or close
		if (fFloatingWindow->IsHidden()) {
			fFloatingWindow->Show();
		} else {
			fFloatingWindow->Hide();
		}
		fFloatingWindow->UnlockLooper();
		return;
	}

	// Create floating window
	BRect frame(100, 100, 420, 340);
	fFloatingWindow = new BWindow(frame, "BubiCam Preview",
		B_FLOATING_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS);

	VideoPreviewView* floatPreview = new VideoPreviewView("floatPreview");
	fFloatingWindow->AddChild(floatPreview);

	// Share current frame
	if (fLastFrame != NULL) {
		BBitmap* copy = new BBitmap(fLastFrame->Bounds(),
			fLastFrame->ColorSpace());
		if (copy != NULL && copy->IsValid())
			memcpy(copy->Bits(), fLastFrame->Bits(), fLastFrame->BitsLength());
		floatPreview->SetFrame(copy);
	}

	fFloatingWindow->Show();
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


static int32
_ShutdownWatchdogThread(void* data)
{
	// Allow 10 seconds for graceful shutdown.
	// StopNodeWithTimeout uses 3s per node, and we may have up to 3 nodes.
	snooze(10000000);
	fprintf(stderr, "BubiCam: Shutdown watchdog triggered after 10s - forcing exit\n");
	_exit(1);
	return 0;
}


// ============================================================================
// Scripting support: hey BubiCam get/set Property
// ============================================================================

static const char* kScriptingSuite = "suite/x-vnd.BubiCam";

static property_info sScriptingProperties[] = {
	{
		"Status",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Current status (idle, streaming, recording)",
		0, { B_STRING_TYPE }
	},
	{
		"FPS",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Current frames per second",
		0, { B_FLOAT_TYPE }
	},
	{
		"Device",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Current webcam device name",
		0, { B_STRING_TYPE }
	},
	{
		"Streaming",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Start/stop video preview (bool)",
		0, { B_BOOL_TYPE }
	},
	{
		"Recording",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Start/stop video recording (bool)",
		0, { B_BOOL_TYPE }
	},
	{
		"Screenshot",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Take a screenshot",
		0, {}
	},
	{
		"FramesCaptured",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Total frames captured",
		0, { B_INT32_TYPE }
	},
	{
		"FramesDropped",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Total frames dropped",
		0, { B_INT32_TYPE }
	},
	{ 0 }
};


status_t
MainWindow::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	data->AddString("suites", kScriptingSuite);

	BPropertyInfo propertyInfo(sScriptingProperties);
	data->AddFlat("messages", &propertyInfo);

	return BWindow::GetSupportedSuites(data);
}


BHandler*
MainWindow::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo propertyInfo(sScriptingProperties);
	if (propertyInfo.FindMatch(message, index, specifier, what, property) >= 0)
		return this;

	return BWindow::ResolveSpecifier(message, index, specifier, what, property);
}


bool
MainWindow::QuitRequested()
{
	// IMPORTANT: Clean up Media Kit resources BEFORE application shutdown begins.
	// This is critical because the BMediaRoster singleton gets destroyed during
	// process exit, and some webcam drivers (like aukey_webcam) don't handle
	// their node cleanup properly - they try to access already-destroyed objects
	// in their BUSBRoster when the media add-on is unloaded.

	// Save settings before shutdown - do this first, always
	_SaveSettings();

	fprintf(stderr, "MainWindow::QuitRequested() - Starting shutdown...\n");

	// Spawn watchdog FIRST - guarantees we never hang indefinitely.
	// Uses _exit() to skip atexit handlers which could also hang.
	thread_id exitWatchdog = spawn_thread(_ShutdownWatchdogThread,
		"exit_watchdog", B_LOW_PRIORITY, NULL);
	if (exitWatchdog >= 0)
		resume_thread(exitWatchdog);

	// Stop MCP server early to prevent new requests during shutdown
	if (fMCPServer != NULL) {
		fMCPServer->SetWebcamDevice(NULL);
		if (fMCPServer->IsRunning())
			fMCPServer->Stop();
	}

	// Stop any running driver test
	if (fDriverTestView != NULL && fDriverTestView->IsTestRunning())
		fDriverTestView->StopCurrentTest();

	// Stop recording if active
	if (fRecorder != NULL && fRecorder->IsRecording())
		_StopRecording();

	// Stop timelapse
	delete fTimelapseRunner;
	fTimelapseRunner = NULL;

	// Check if driver appears to be crashed/frozen
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
		fprintf(stderr, "  Driver appears frozen/crashed - skipping Media Kit cleanup\n");

		// Just mark as not capturing and let the OS clean up
		{
			BAutolock lock(fWebcamLock);
			fIsPreviewActive = false;
			fCurrentWebcam = NULL;
		}

		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	// Normal shutdown path: stop preview (uses StopNodeWithTimeout internally)
	fprintf(stderr, "  Stopping preview...\n");
	_StopPreview();

	// Clear the current device pointer before clearing the roster
	{
		BAutolock lock(fWebcamLock);
		fCurrentWebcam = NULL;
		fCurrentWebcamIndex = -1;
	}

	// Release all webcam devices
	if (fWebcamRoster != NULL) {
		fprintf(stderr, "  Clearing webcam roster...\n");
		fWebcamRoster->Clear();
	}

	fprintf(stderr, "MainWindow::QuitRequested() - Shutdown complete\n");
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


void
MainWindow::_ExportDebugState()
{
	BPath path;
	find_directory(B_USER_DIRECTORY, &path);
	BString filename("BubiCam_DebugState_");
	filename << ExportUtils::GetTimestamp() << ".txt";
	path.Append(filename.String());

	BString report;
	report << "=== BubiCam Debug State Export ===\n";
	report << "Timestamp: " << ExportUtils::GetTimestamp() << "\n\n";

	// Application state
	report << "--- Application State ---\n";
	report << "Preview active: " << (fIsPreviewActive ? "yes" : "no") << "\n";
	report << "Recording: " << (fRecorder != NULL && fRecorder->IsRecording() ? "yes" : "no") << "\n";
	report << "Driver crashed: " << (fDriverCrashed ? "yes" : "no") << "\n";
	report << "Fullscreen: " << (fIsFullscreen ? "yes" : "no") << "\n\n";

	// Device state
	WebcamDevice* webcam = NULL;
	{
		BAutolock lock(fWebcamLock);
		webcam = fCurrentWebcam;
	}
	report << "--- Webcam Device ---\n";
	if (webcam != NULL) {
		report << "Name: " << webcam->Name() << "\n";
		report << "Driver: " << webcam->DriverName() << "\n";
		report << "VID:PID: " << BString().SetToFormat("0x%04X:0x%04X",
			webcam->VendorID(), webcam->ProductID()) << "\n";
		report << "Capturing: " << (webcam->IsCapturing() ? "yes" : "no") << "\n";
		report << "Frames captured: " << webcam->FramesCaptured() << "\n";
		report << "Frames dropped: " << webcam->FramesDropped() << "\n";
		report << "FPS: " << BString().SetToFormat("%.1f", webcam->CurrentFPS()) << "\n";
		VideoFormat fmt = webcam->CurrentFormat();
		report << "Format: " << BString().SetToFormat("%dx%d @ %.1f fps (%s)",
			fmt.width, fmt.height, fmt.frameRate, fmt.colorSpace) << "\n";
		report << "Audio: " << (webcam->SupportsAudio() ? "yes" : "no") << "\n";
		if (webcam->HasDriverWarnings()) {
			report << "Warnings: " << webcam->GetDriverWarnings() << "\n";
		}
	} else {
		report << "No device selected\n";
	}
	report << "\n";

	// Server state
	report << "--- Server State ---\n";
	if (fMCPServer != NULL)
		report << "MCP server: " << (fMCPServer->IsRunning() ? "running" : "stopped") << "\n";
	if (fStreamServer != NULL)
		report << "Stream server: " << (fStreamServer->IsRunning() ? "running" : "stopped") << "\n";
	report << "\n";

	// System info
	report << "--- System Info ---\n";
	system_info sysInfo;
	if (get_system_info(&sysInfo) == B_OK) {
		report << "CPU count: " << sysInfo.cpu_count << "\n";
		report << "Max pages: " << sysInfo.max_pages << "\n";
		report << "Used pages: " << sysInfo.used_pages << "\n";
	}
	report << "\n=== End Debug State ===\n";

	// Write to file
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		file.Write(report.String(), report.Length());
		BString msg;
		msg.SetToFormat("Debug state exported to:\n%s", path.Path());
		fStatusBar->SetText("Debug state exported");
		NotificationUtils::Info("Debug Export", path.Path());
	} else {
		fStatusBar->SetText("Failed to export debug state");
	}
}


void
MainWindow::_ExportRawFrame()
{
	if (fCurrentWebcam == NULL || !fIsPreviewActive) {
		fStatusBar->SetText("No active capture to export raw frame from");
		return;
	}

	// Get the raw (pre-conversion) buffer from the video consumer
	VideoConsumer* consumer = NULL;
	{
		BAutolock lock(fWebcamLock);
		if (fCurrentWebcam != NULL)
			consumer = fCurrentWebcam->GetVideoConsumer();
	}

	void* rawData = NULL;
	size_t rawSize = 0;
	color_space rawFormat = B_NO_COLOR_SPACE;
	int32 rawWidth = 0, rawHeight = 0;

	bool gotRaw = false;
	if (consumer != NULL) {
		gotRaw = (consumer->CaptureRawFrame(&rawData, &rawSize,
			&rawFormat, &rawWidth, &rawHeight) == B_OK);
	}

	// Fall back to the converted frame if raw capture failed
	if (!gotRaw && fLastFrame != NULL) {
		BRect bounds = fLastFrame->Bounds();
		rawWidth = (int32)(bounds.Width() + 1);
		rawHeight = (int32)(bounds.Height() + 1);
		rawFormat = fLastFrame->ColorSpace();
		rawSize = fLastFrame->BitsLength();
		rawData = malloc(rawSize);
		if (rawData != NULL)
			memcpy(rawData, fLastFrame->Bits(), rawSize);
		gotRaw = (rawData != NULL);
	}

	if (!gotRaw || rawData == NULL) {
		fStatusBar->SetText("No frame available to export");
		return;
	}

	BPath path;
	find_directory(B_USER_DIRECTORY, &path);

	BString filename("BubiCam_RawFrame_");
	filename << ExportUtils::GetTimestamp();

	// Save raw buffer data
	BString rawFile(filename);
	rawFile << "_" << rawWidth << "x" << rawHeight << ".raw";
	BPath rawPath(path);
	rawPath.Append(rawFile.String());

	BFile file(rawPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		file.Write(rawData, rawSize);

		// Write companion info file with format metadata
		BString infoFile(filename);
		infoFile << ".info";
		BPath infoPath(path);
		infoPath.Append(infoFile.String());

		BFile info(infoPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (info.InitCheck() == B_OK) {
			// Determine format name
			const char* formatName = "unknown";
			switch (rawFormat) {
				case B_RGB32: formatName = "RGB32"; break;
				case B_RGBA32: formatName = "RGBA32"; break;
				case B_RGB24: formatName = "RGB24"; break;
				case B_YCbCr422: formatName = "YCbCr422 (YUYV)"; break;
				case B_YUV422: formatName = "YUV422"; break;
				case B_YCbCr420: formatName = "YCbCr420 (I420)"; break;
				case B_YUV420: formatName = "YUV420"; break;
				case B_YUV12: formatName = "YUV12 (NV12)"; break;
				case B_YUV9: formatName = "YUV9 (NV21)"; break;
				default: break;
			}
			if ((int)rawFormat == 0x2000)
				formatName = "UYVY";

			BString infoContent;
			infoContent.SetToFormat(
				"BubiCam Raw Frame Export\n"
				"========================\n"
				"Source: pre-conversion driver buffer\n"
				"Width: %d\n"
				"Height: %d\n"
				"Color space: 0x%04x (%s)\n"
				"Total bytes: %zu\n"
				"Device: %s\n"
				"\n"
				"To view with ffmpeg:\n"
				"  ffplay -f rawvideo -pix_fmt %s -s %dx%d %s\n",
				(int)rawWidth, (int)rawHeight,
				(int)rawFormat, formatName,
				rawSize,
				fCurrentWebcam ? fCurrentWebcam->Name() : "unknown",
				(rawFormat == B_YCbCr422 || rawFormat == B_YUV422)
					? "yuyv422" : "nv12",
				(int)rawWidth, (int)rawHeight,
				rawPath.Leaf());
			info.Write(infoContent.String(), infoContent.Length());
		}

		BString msg;
		msg.SetToFormat("Raw frame exported: %s", rawPath.Path());
		fStatusBar->SetText(msg.String());
	} else {
		fStatusBar->SetText("Failed to export raw frame");
	}

	free(rawData);
}


void
MainWindow::_StartDeviceWatching()
{
	// Watch /dev/video directory for device file creation/removal.
	// B_ENTRY_REMOVED fires when the USB device file is deleted, which
	// happens BEFORE the USB stack destroys the device and its pipes.
	// This gives us a window to call StopCapture() and release the
	// isochronous endpoints, preventing the "USB object did not become idle"
	// kernel panic.
	BDirectory dir("/dev/video");
	if (dir.InitCheck() != B_OK) {
		// /dev/video might not exist yet (no webcam driver loaded)
		// Try /dev instead
		dir.SetTo("/dev");
		if (dir.InitCheck() != B_OK)
			return;
	}

	node_ref nref;
	if (dir.GetNodeRef(&nref) == B_OK) {
		fDevVideoNodeRef = nref;
		if (watch_node(&fDevVideoNodeRef, B_WATCH_DIRECTORY, this) == B_OK) {
			fDevVideoWatching = true;
		}
	}
}


void
MainWindow::_StopDeviceWatching()
{
	if (fDevVideoWatching) {
		watch_node(&fDevVideoNodeRef, B_STOP_WATCHING, this);
		fDevVideoWatching = false;
	}
}


void
MainWindow::_HandleDeviceNodeMonitor(BMessage* message)
{
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK)
		return;

	switch (opcode) {
		case B_ENTRY_REMOVED:
		{
			// A device file was removed from /dev/video - the webcam
			// was physically disconnected. We MUST stop capture immediately
			// to release isochronous USB pipes before the USB stack
			// tries to destroy the device (which would cause a kernel panic).
			if (fIsPreviewActive && fCurrentWebcam != NULL) {
				fprintf(stderr, "BubiCam: USB device removed, "
					"emergency stop to release USB pipes\n");

				BString deviceName(fCurrentWebcam->Name());

				// Stop capture IMMEDIATELY - this is time-critical
				_StopPreview();
				{
					BAutolock lock(fWebcamLock);
					fCurrentWebcam = NULL;
					fCurrentWebcamIndex = -1;
				}
				if (fMCPServer != NULL)
					fMCPServer->SetWebcamDevice(NULL);
				fDriverTestView->SetDevice(NULL);

				fStatusBar->SetText("Webcam disconnected");

				// Schedule a device list refresh after the USB stack
				// finishes cleanup
				BMessage refreshMsg(MSG_REFRESH_DEVICES);
				BMessageRunner::StartSending(BMessenger(this),
					&refreshMsg, 1500000, 1);  // 1.5s delay
			}
			break;
		}

		case B_ENTRY_CREATED:
		{
			// A new device file appeared - a webcam was plugged in.
			// Wait a moment for the driver to fully initialize, then
			// refresh the device list.
			if (!fIsPreviewActive) {
				BMessage refreshMsg(MSG_REFRESH_DEVICES);
				BMessageRunner::StartSending(BMessenger(this),
					&refreshMsg, 2000000, 1);  // 2s delay for driver init
			}
			break;
		}
	}
}

