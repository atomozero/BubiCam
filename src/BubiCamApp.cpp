/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "BubiCamApp.h"
#include "MainWindow.h"
#include "WebcamRoster.h"
#include "WebcamDevice.h"

#include <Alert.h>
#include <Catalog.h>
#include <Looper.h>
#include <Mime.h>
#include <Resources.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BubiCamApp"

const char* kApplicationSignature = "application/x-vnd.BubiCam";


BubiCamApp::BubiCamApp()
	:
	BApplication(kApplicationSignature),
	fMainWindow(NULL),
	fHeadless(false),
	fHeadlessDuration(0)
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

	if (fHeadless) {
		_RunHeadless();
		return;
	}

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


void
BubiCamApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--headless") == 0) {
			fHeadless = true;
		} else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
			fHeadlessDuration = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			_PrintUsage();
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
	}
}


void
BubiCamApp::_PrintUsage()
{
	fprintf(stderr,
		"BubiCam - Webcam Driver Tester for Haiku OS\n"
		"\n"
		"Usage: BubiCam [options]\n"
		"\n"
		"Options:\n"
		"  --headless         Run without GUI (streaming server only)\n"
		"  --duration <sec>   Headless mode duration (0 = run until killed)\n"
		"  --help, -h         Show this help message\n"
		"\n"
		"Headless mode starts the MJPEG streaming server on port 8080\n"
		"and the MCP server on port 9847. Access the live stream at:\n"
		"  http://localhost:8080/stream\n"
		"  http://localhost:8080/snapshot\n"
		"\n"
		"Scripting (with GUI):\n"
		"  hey BubiCam get Status\n"
		"  hey BubiCam get FPS\n"
		"  hey BubiCam set Streaming to true\n"
		"  hey BubiCam do Screenshot\n"
	);
}


void
BubiCamApp::_RunHeadless()
{
	fprintf(stderr, "BubiCam: headless mode\n");

	// Enumerate webcams
	WebcamRoster roster;
	roster.EnumerateDevices();

	if (roster.CountDevices() == 0) {
		fprintf(stderr, "BubiCam: no webcams found, exiting\n");
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	WebcamDevice* device = roster.DeviceAt(0);
	fprintf(stderr, "BubiCam: using '%s'\n", device->Name());

	// Create a simple looper to receive frames (frame counting only)
	BLooper* looper = new BLooper("headless_capture");
	looper->Run();

	status_t err = device->StartCapture(looper);
	if (err != B_OK) {
		fprintf(stderr, "BubiCam: failed to start capture: %s\n", strerror(err));
		looper->Lock();
		looper->Quit();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fprintf(stderr, "BubiCam: capture started, streaming on port 8080\n");

	if (fHeadlessDuration > 0) {
		// Run for specified duration
		snooze((bigtime_t)fHeadlessDuration * 1000000);
		fprintf(stderr, "BubiCam: duration expired (%d sec), stopping\n",
			(int)fHeadlessDuration);
		device->StopCapture();
		looper->Lock();
		looper->Quit();
		PostMessage(B_QUIT_REQUESTED);
	}
	// If duration == 0, run indefinitely until quit signal
}


int
main()
{
	BubiCamApp app;
	app.Run();
	return 0;
}
