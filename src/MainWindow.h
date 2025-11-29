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

class VideoPreviewView;
class DriverInfoView;
class SyslogView;
class VUMeterView;
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
	MSG_DRIVER_INFO_UPDATE	= 'drvu'
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
	void				_SelectWebcam(int32 index);
	void				_StartPreview();
	void				_StopPreview();
	void				_UpdateDriverInfo();

	BMenuBar*			fMenuBar;
	BMenu*				fWebcamMenu;
	BMenu*				fControlMenu;

	VideoPreviewView*	fVideoPreview;
	DriverInfoView*		fDriverInfo;
	SyslogView*			fSyslogView;
	VUMeterView*		fVUMeter;
	BStringView*		fStatusBar;

	WebcamRoster*		fWebcamRoster;
	WebcamDevice*		fCurrentWebcam;
	int32				fCurrentWebcamIndex;
	bool				fIsPreviewActive;
};

#endif // MAIN_WINDOW_H
