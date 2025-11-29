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
	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();
	virtual void		AboutRequested();

private:
	MainWindow*			fMainWindow;
};

#endif // BUBICAM_APP_H
