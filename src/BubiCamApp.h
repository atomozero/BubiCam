/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef BUBICAM_APP_H
#define BUBICAM_APP_H

#include <Application.h>

#include <atomic>

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

	// Called by MainWindow to keep emergency exit watchdog at bay
	void				PingAlive() { fLastPing = system_time(); }

private:
	void				_PrintUsage();
	void				_RunHeadless();
	static int32		_EmergencyExitWatchdog(void* data);

	MainWindow*			fMainWindow;
	bool				fHeadless;
	int32				fHeadlessDuration;	// seconds, 0 = until quit

	// Emergency exit watchdog: if MainWindow stops calling PingAlive()
	// for more than kEmergencyExitTimeoutUs while quit is requested,
	// the watchdog thread calls _exit(1) to break out of any kernel-level
	// semaphore deadlock that signals can't reach.
	std::atomic<bigtime_t>	fLastPing;
	std::atomic<bool>		fQuitTriggered;
	thread_id				fWatchdogThread;
};

#endif // BUBICAM_APP_H
