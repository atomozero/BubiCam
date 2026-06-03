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
#include <Path.h>
#include <Button.h>
#include <Locker.h>
#include <NodeMonitor.h>
#include <ObjectList.h>
#include <SplitView.h>
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
class StreamServer;
class VideoRecorder;
class VideoFilterChain;

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
	MSG_EXPORT_RAW_FRAME	= 'exrf',
	MSG_CAPTURE_REFERENCE	= 'cprf',
	MSG_TOGGLE_COMPARE		= 'tgcm',
	MSG_CLEAR_REFERENCE		= 'clrf',
	MSG_TOGGLE_DESKBAR		= 'tgdb',
	MSG_CODEC_MJPEG			= 'cdmj',
	MSG_CODEC_RAW			= 'cdrw',
	MSG_TOGGLE_SYSLOG		= 'tgsy',
	MSG_TOGGLE_VUBAR		= 'tgvu',
	MSG_RESET_LAYOUT		= 'rlyt',
	MSG_TOGGLE_INSPECTOR	= 'tgin',
	MSG_EXPORT_DEBUG		= 'exdb',
	MSG_FILTER_TOGGLE		= 'fltg',
	MSG_TIMELAPSE_START		= 'tlst',
	MSG_TIMELAPSE_STOP		= 'tlsp',
	MSG_TIMELAPSE_TICK		= 'tltk',
	MSG_FLOATING_PREVIEW	= 'flpv',
	MSG_BUFFER_TOGGLE		= 'bftg',
	MSG_BUFFER_SAVE			= 'bfsv',
	MSG_TOGGLE_AUTO_PREVIEW	= 'tgap',
	MSG_RESTORE_DEVICE		= 'rstd',
	MSG_FACTORY_RESET		= 'frst',
	MSG_TOGGLE_GRID			= 'tggr',
	MSG_GRID_MODE			= 'grmd',
	MSG_FULLSCREEN			= 'fscr',
	MSG_AUDIO_SOURCE		= 'auds',
	MSG_AUDIO_NONE			= 'audn',
	MSG_STREAM_TOGGLE		= 'sttg'
};


class MainWindow : public BWindow {
public:
						MainWindow();
	virtual				~MainWindow();

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

	// Scripting support (hey BubiCam ...)
	virtual status_t	GetSupportedSuites(BMessage* data);
	virtual BHandler*	ResolveSpecifier(BMessage* message, int32 index,
						BMessage* specifier, int32 what,
						const char* property);

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
	void				_StartTimelapse();
	void				_StopTimelapse();
	void				_TimelapseTick();
	void				_ShowFloatingPreview();
	void				_ToggleCircularBuffer();
	void				_SaveCircularBuffer();
	void				_UpdateRecordingStatus();
	void				_SaveSettings();
	void				_LoadSettings();
	void				_ExportRawFrame();
	void				_PopulateAudioMenu();
	void				_ConnectAudioSource(int32 nodeID);
	void				_DisconnectAudio();

	void				_ExportDebugState();
	void				_EnterVideoFullscreen();
	void				_ExitVideoFullscreen();
	void				_StartDeviceWatching();
	void				_StopDeviceWatching();
	void				_HandleDeviceNodeMonitor(BMessage* message);

	bool				fIsFullscreen;
	BWindow*			fFullscreenWindow;
	VideoPreviewView*	fFullscreenPreview;
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

	// Split views (for layout persistence)
	BSplitView*			fMainSplit;
	BSplitView*			fLeftSplit;
	BSplitView*			fRightSplit;

	// Toolbar
	BToolBar*			fToolbar;

	// Video stats bar
	LEDView*			fCamLED;
	BStringView*		fStatsResolution;
	BStringView*		fStatsFormat;
	BStringView*		fStatsFPS;
	BStringView*		fStatsFrames;
	BStringView*		fStatsDropped;

	WebcamRoster*		fWebcamRoster;
	WebcamDevice*		fCurrentWebcam;
	mutable BLocker		fWebcamLock;	// Protects fCurrentWebcam access
	int32				fCurrentWebcamIndex;
	node_ref			fDevVideoNodeRef;	// /dev/video node for monitoring
	bool				fDevVideoWatching;
	int32				fSelectedAudioNodeID;  // -1 = auto, 0 = none, >0 = specific node
	bool				fIsPreviewActive;
	bool				fChangingResolution;

	BFilePanel*			fSavePanel;
	BBitmap*			fLastFrame;
	bool				fSavingJson;

	// MCP Server
	MCPServer*			fMCPServer;
	BMenuItem*			fMCPMenuItem;

	// Stream Server
	StreamServer*		fStreamServer;
	BMenuItem*			fStreamMenuItem;

	// Video recording
	VideoRecorder*		fRecorder;
	VideoFilterChain*	fFilterChain;

	// Time-lapse
	BMessageRunner*		fTimelapseRunner;
	BPath				fTimelapsePath;
	uint32				fTimelapseCount;
	bigtime_t			fTimelapseInterval;  // microseconds

	// Circular buffer (replay recording)
	struct BufferedFrame {
		uint8*		jpegData;
		uint32		jpegSize;
		bigtime_t	timestamp;

		BufferedFrame() : jpegData(NULL), jpegSize(0), timestamp(0) {}
		~BufferedFrame() { free(jpegData); }
	};
	bool				fCircularBufferActive;
	int32				fCircularBufferSeconds;
	BObjectList<BufferedFrame>	fCircularBuffer;
	BLocker				fBufferLock;

	// Floating preview
	BWindow*			fFloatingWindow;

	// Settings
	bool				fAutoStartPreview;

	// Driver crash protection
	bool				fDriverCrashed;
	bool				fWatchdogAlertShown;
	bool				fBandwidthAlertShown;
	bigtime_t			fLastFrameReceived;
	bigtime_t			fPreviewStartTime;
	uint32				fLastWatchdogFrameCount;
	uint32				fLastWatchdogDropCount;
	int32				fCatastrophicDropCount;
	BMessageRunner*		fWatchdogRunner;
};

#endif // MAIN_WINDOW_H
