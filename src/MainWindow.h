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
	MSG_RECORD_STOP			= 'rcsp'
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
	void				_UpdateStatsBar();
	void				_UpdateToolbarState();
	void				_RestartMediaServices();
	void				_DoRestartMediaServices(bool askConfirmation);
	void				_StartRecording();
	void				_StopRecording();
	void				_UpdateRecordingStatus();

	BMenuBar*			fMenuBar;
	BMenu*				fWebcamMenu;
	BMenu*				fControlMenu;
	BMenu*				fFormatMenu;
	BMenu*				fToolsMenu;

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
	BStringView*		fStatsResolution;
	BStringView*		fStatsFPS;
	BStringView*		fStatsFrames;
	BStringView*		fStatsDropped;

	WebcamRoster*		fWebcamRoster;
	WebcamDevice*		fCurrentWebcam;
	mutable BLocker		fWebcamLock;	// Protects fCurrentWebcam access
	int32				fCurrentWebcamIndex;
	bool				fIsPreviewActive;

	BFilePanel*			fSavePanel;
	BBitmap*			fLastFrame;
	bool				fSavingJson;

	// MCP Server
	MCPServer*			fMCPServer;
	BMenuItem*			fMCPMenuItem;

	// Video recording
	VideoRecorder*		fRecorder;

	// Driver crash protection
	bool				fDriverCrashed;
	bool				fWatchdogAlertShown;
	bigtime_t			fLastFrameReceived;
	BMessageRunner*		fWatchdogRunner;
};

#endif // MAIN_WINDOW_H
