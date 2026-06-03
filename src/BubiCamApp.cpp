/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "BubiCamApp.h"
#include "MainWindow.h"

#include <Alert.h>
#include <Catalog.h>
#include <Mime.h>
#include <Resources.h>

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


static void
_RegisterMIMEType(const char* mimeType, const char* shortDesc,
	const char* longDesc, const char* extension, const char* preferredApp)
{
	BMimeType mime(mimeType);
	if (mime.IsInstalled())
		return;

	mime.Install();
	mime.SetShortDescription(shortDesc);
	mime.SetLongDescription(longDesc);
	mime.SetPreferredApp(preferredApp);

	if (extension != NULL) {
		BMessage extensions;
		extensions.AddString("extensions", extension);
		mime.SetFileExtensions(&extensions);
	}
}


void
BubiCamApp::ReadyToRun()
{
	// Register custom MIME types for BubiCam file formats
	_RegisterMIMEType(
		"application/x-vnd.BubiCam-preset",
		"BubiCam Preset",
		"BubiCam webcam control preset file",
		"bcpreset",
		kApplicationSignature);

	_RegisterMIMEType(
		"application/x-vnd.BubiCam-report",
		"BubiCam Report",
		"BubiCam diagnostic report file",
		"bcreport",
		kApplicationSignature);

	_RegisterMIMEType(
		"text/x-vnd.BubiCam-testresults-csv",
		"BubiCam Test CSV",
		"BubiCam test results in CSV format",
		"csv",
		NULL);  // Don't claim ownership of .csv

	_RegisterMIMEType(
		"application/x-vnd.BubiCam-testresults-json",
		"BubiCam Test JSON",
		"BubiCam test results in JSON format",
		NULL,
		NULL);

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
