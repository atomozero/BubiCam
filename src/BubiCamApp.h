/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef BUBICAM_APP_H
#define BUBICAM_APP_H

#include <Application.h>

class MainWindow;

class BubiCamApp : public BApplication {
public:
						BubiCamApp();
	virtual				~BubiCamApp();

	virtual void		ReadyToRun();
	virtual void		ArgvReceived(int32 argc, char** argv);
	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();
	virtual void		AboutRequested();

private:
	void				_PrintUsage();
	void				_RunHeadless();

	MainWindow*			fMainWindow;
	bool				fHeadless;
	int32				fHeadlessDuration;	// seconds, 0 = until quit
};

#endif // BUBICAM_APP_H
