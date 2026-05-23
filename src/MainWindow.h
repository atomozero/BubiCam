/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <Window.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <TabView.h>
#include <StringView.h>
#include <FilePanel.h>
#include <Bitmap.h>
#include <Button.h>
#include <Locker.h>
#include <ToolBar.h>

class LEDView;
class VideoPreviewView;
class DriverInfoView;
class DriverTestView;
class USBPacketView;
class SyslogView;
class VUMeterView;
class WebcamControlsView;
class WebcamRoster;
class WebcamDevice;
class MCPServer;
class VideoRecorder;

// Message constants
enum {
	MSG_WEBCAM_SELECTED		= 'wcsl',
	MSG_WEBCAM_START		= 'wcst',
	MSG_WEBCAM_STOP			= 'wcsp',
	MSG_REFRESH_DEVICES		= 'rfrd',
	MSG_ABOUT				= 'abot',
	MSG_FRAME_RECEIVED		= 'frcv',
	MSG_AUDIO_LEVEL			= 'audl',
	MSG_AUDIO_BUFFER		= 'audb',
	MSG_SYSLOG_UPDATE		= 'sysu',
	MSG_DRIVER_INFO_UPDATE	= 'drvu',
	MSG_SCREENSHOT			= 'scsh',
	MSG_SCREENSHOT_SAVED	= 'scsv',
	MSG_EXPORT_INFO			= 'expi',
	MSG_EXPORT_INFO_JSON	= 'expj',
	MSG_EXPORT_SAVED		= 'exsv',
	MSG_FORMAT_SELECTED		= 'fmsl',
	MSG_CLEAR_SYSLOG		= 'clsl',
	MSG_TOGGLE_CONTROLS		= 'tgct',
	MSG_RESTART_MEDIA		= 'rmed',
	MSG_TOGGLE_NOISE_FILTER	= 'tgnf',
	MSG_MCP_TOGGLE			= 'mcpt',
	MSG_MCP_STATUS			= 'mcpu',
	MSG_MCP_LOG				= 'mcpl',
	MSG_RESTART_PREVIEW		= 'rsrt',
	MSG_WATCHDOG_CHECK		= 'wdck',
	MSG_FORCE_STOP			= 'fstp',
	MSG_SHOW_DRIVER_TESTS	= 'sdtv',
	MSG_SHOW_USB_VIEWER		= 'susb',
	MSG_RECORD_START		= 'rcst',
	MSG_RECORD_STOP			= 'rcsp',
	MSG_TOGGLE_HISTOGRAM	= 'tghi',
	MSG_RESET_ZOOM			= 'rszm',
	MSG_CAPTURE_REFERENCE	= 'cprf',
	MSG_TOGGLE_COMPARE		= 'tgcm',
	MSG_CLEAR_REFERENCE		= 'clrf',
	MSG_TOGGLE_AUTO_PREVIEW	= 'tgap',
	MSG_RESTORE_DEVICE		= 'rstd',
	MSG_FACTORY_RESET		= 'frst',
	MSG_TOGGLE_GRID			= 'tggr',
	MSG_GRID_MODE			= 'grmd',
	MSG_FULLSCREEN			= 'fscr',
	MSG_EXPORT_RAW_FRAME	= 'exrf',
	MSG_AUDIO_SOURCE		= 'auds',
	MSG_AUDIO_NONE			= 'audn'
};


class MainWindow : public BWindow {
public:
						MainWindow();
	virtual				~MainWindow();

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

private:
	void				_BuildMenu();
	void				_BuildLayout();
	void				_PopulateWebcamMenu();
	void				_PopulateFormatMenu();
	void				_SelectWebcam(int32 index);
	void				_SelectFormat(int32 index);
	void				_StartPreview();
	void				_StopPreview();
	void				_UpdateDriverInfo();
	void				_TakeScreenshot();
	void				_ExportDriverInfo(bool asJson);
	void				_SaveScreenshot(const char* path);
	void				_SaveExport(const char* path, bool asJson);
	void				_BuildToolbar();
	BView*				_BuildStatsBar();
	BTabView*			_BuildTabView();
	void				_UpdateStatsBar();
	void				_UpdateToolbarState();
	void				_HandleFrameReceived(BMessage* message);
	void				_CheckWatchdog();
	void				_ForceStop();
	void				_FactoryResetControls();
	void				_RestartMediaServices();
	void				_DoRestartMediaServices(bool askConfirmation);
	void				_StartRecording();
	void				_StopRecording();
	void				_UpdateRecordingStatus();
	void				_SaveSettings();
	void				_LoadSettings();
	void				_ExportRawFrame();
	void				_PopulateAudioMenu();
	void				_ConnectAudioSource(int32 nodeID);
	void				_DisconnectAudio();

	bool				fIsFullscreen;
	BRect				fSavedFrame;
	window_look			fSavedLook;
	uint32				fSavedFlags;

	BMenuBar*			fMenuBar;
	BMenu*				fWebcamMenu;
	BMenu*				fControlMenu;
	BMenu*				fFormatMenu;
	BMenu*				fToolsMenu;
	BMenu*				fAudioMenu;

	VideoPreviewView*	fVideoPreview;
	DriverInfoView*		fDriverInfo;
	DriverTestView*		fDriverTestView;
	USBPacketView*		fUSBPacketView;
	SyslogView*			fSyslogView;
	VUMeterView*		fVUMeter;
	WebcamControlsView*	fWebcamControls;
	BStringView*		fStatusBar;
	BTabView*			fRightTabView;

	// Toolbar
	BToolBar*			fToolbar;

	// Video stats bar
	LEDView*			fCamLED;
	BStringView*		fStatsResolution;
	BStringView*		fStatsFPS;
	BStringView*		fStatsFrames;
	BStringView*		fStatsDropped;

	WebcamRoster*		fWebcamRoster;
	WebcamDevice*		fCurrentWebcam;
	mutable BLocker		fWebcamLock;	// Protects fCurrentWebcam access
	int32				fCurrentWebcamIndex;
	int32				fSelectedAudioNodeID;  // -1 = auto, 0 = none, >0 = specific node
	bool				fIsPreviewActive;

	BFilePanel*			fSavePanel;
	BBitmap*			fLastFrame;
	bool				fSavingJson;

	// MCP Server
	MCPServer*			fMCPServer;
	BMenuItem*			fMCPMenuItem;

	// Video recording
	VideoRecorder*		fRecorder;

	// Settings
	bool				fAutoStartPreview;

	// Driver crash protection
	bool				fDriverCrashed;
	bool				fWatchdogAlertShown;
	bigtime_t			fLastFrameReceived;
	BMessageRunner*		fWatchdogRunner;
};

#endif // MAIN_WINDOW_H
