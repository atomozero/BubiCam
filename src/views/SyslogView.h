/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef SYSLOG_VIEW_H
#define SYSLOG_VIEW_H

#include <View.h>
#include <TextView.h>
#include <String.h>
#include <OS.h>
#include <File.h>
#include <Locker.h>
#include <ObjectList.h>

class SyslogView : public BTextView {
public:
						SyslogView(const char* name);
	virtual				~SyslogView();

	virtual void		AttachedToWindow();

	void				StartMonitoring();
	void				StopMonitoring();
	void				AddLine(const char* line);
	void				Clear();

	void				SetFilter(const char* filter);
	const char*			Filter() const { return fFilter.String(); }

	void				SetNoiseFilterEnabled(bool enabled);
	bool				NoiseFilterEnabled() const { return fNoiseFilterEnabled; }

	// Level filter: bit flags
	enum {
		LEVEL_ERROR		= 0x01,
		LEVEL_WARNING	= 0x02,
		LEVEL_INFO		= 0x04,
		LEVEL_ALL		= 0x07
	};
	void				SetLevelFilter(uint32 levels);
	uint32				LevelFilter() const { return fLevelFilter; }

private:
	bool				_IsNoisy(const char* line);
	bool				_IsDuplicate(const char* line);
	static int32		_MonitorThread(void* data);
	void				_MonitorLoop();
	bool				_MatchesFilter(const char* line);
	void				_AppendColoredLine(const char* line);

	thread_id			fMonitorThread;
	bool				fMonitoring;
	BLocker				fLock;
	BString				fFilter;
	off_t				fLastPosition;
	int32				fMaxLines;

	bool				fNoiseFilterEnabled;
	uint32				fLevelFilter;
	static uint32		_ClassifyLevel(const char* line);
	BString				fLastLine;
	int32				fDuplicateCount;

	rgb_color			fTimestampColor;
	rgb_color			fUSBColor;
	rgb_color			fMediaColor;
	rgb_color			fErrorColor;
	rgb_color			fDefaultColor;

	static const int32	kMaxLines = 500;
	static const char*	kSyslogPath;
};

#endif // SYSLOG_VIEW_H
