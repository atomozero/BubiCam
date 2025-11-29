/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "BubiCamApp.h"
#include "MainWindow.h"

#include <Alert.h>
#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BubiCamApp"

const char* kApplicationSignature = "application/x-vnd.BubiCam";


BubiCamApp::BubiCamApp()
	:
	BApplication(kApplicationSignature),
	fMainWindow(NULL)
{
}


BubiCamApp::~BubiCamApp()
{
}


void
BubiCamApp::ReadyToRun()
{
	fMainWindow = new MainWindow();
	fMainWindow->Show();
}


void
BubiCamApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool
BubiCamApp::QuitRequested()
{
	return true;
}


void
BubiCamApp::AboutRequested()
{
	BAlert* alert = new BAlert("About BubiCam",
		"BubiCam - Webcam Driver Tester\n\n"
		"A tool for testing USB webcam drivers on Haiku OS.\n\n"
		"Features:\n"
		"• Live video preview\n"
		"• Driver information display\n"
		"• Real-time syslog monitoring\n"
		"• Audio VU meter for microphone\n"
		"• Multiple webcam support\n\n"
		"Copyright © 2024 BubiCam Contributors\n"
		"MIT License",
		"OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


int
main()
{
	BubiCamApp app;
	app.Run();
	return 0;
}
