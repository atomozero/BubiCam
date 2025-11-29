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
#include <TabView.h>
#include <StringView.h>
#include <FilePanel.h>
#include <Bitmap.h>

class VideoPreviewView;
class DriverInfoView;
class SyslogView;
class VUMeterView;
class WebcamControlsView;
class WebcamRoster;
class WebcamDevice;

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
	MSG_TOGGLE_CONTROLS		= 'tgct'
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

	BMenuBar*			fMenuBar;
	BMenu*				fWebcamMenu;
	BMenu*				fControlMenu;
	BMenu*				fFormatMenu;
	BMenu*				fToolsMenu;

	VideoPreviewView*	fVideoPreview;
	DriverInfoView*		fDriverInfo;
	SyslogView*			fSyslogView;
	VUMeterView*		fVUMeter;
	WebcamControlsView*	fWebcamControls;
	BStringView*		fStatusBar;
	BTabView*			fRightTabView;

	WebcamRoster*		fWebcamRoster;
	WebcamDevice*		fCurrentWebcam;
	int32				fCurrentWebcamIndex;
	bool				fIsPreviewActive;

	BFilePanel*			fSavePanel;
	BBitmap*			fLastFrame;
	bool				fSavingJson;
};

#endif // MAIN_WINDOW_H
