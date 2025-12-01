/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "SyslogView.h"

#include <Autolock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


const char* SyslogView::kSyslogPath = "/var/log/syslog";


// Noisy patterns to filter out
static const char* kNoisyPatterns[] = {
	"SubmitIsochronous",
	"Using ITD path",
	"Using siTD path",
	"EHCI: ######",
	"OHCI: ######",
	"UHCI: ######",
	"xHCI: ######",
	NULL
};


SyslogView::SyslogView(const char* name)
	:
	BTextView(name, B_WILL_DRAW),
	fMonitorThread(-1),
	fMonitoring(false),
	fLock("syslog lock"),
	fLastPosition(0),
	fMaxLines(kMaxLines),
	fNoiseFilterEnabled(true),
	fDuplicateCount(0)
{
	SetStylable(true);
	MakeEditable(false);
	MakeSelectable(true);
	SetWordWrap(false);

	// Default filter - show USB, video, media, and webcam related entries
	fFilter = "usb|video|media|webcam|uvc|camera|capture";

	// Colors for different log types
	fTimestampColor = make_color(100, 100, 100);   // Gray
	fUSBColor = make_color(0, 100, 0);             // Green
	fMediaColor = make_color(0, 0, 150);           // Blue
	fErrorColor = make_color(200, 0, 0);           // Red
	fDefaultColor = make_color(0, 0, 0);           // Black
}


SyslogView::~SyslogView()
{
	StopMonitoring();
}


void
SyslogView::AttachedToWindow()
{
	BTextView::AttachedToWindow();

	BFont font(be_fixed_font);
	font.SetSize(10);
	SetFontAndColor(&font);

	SetViewColor(make_color(250, 250, 250));
}


void
SyslogView::StartMonitoring()
{
	BAutolock lock(fLock);

	if (fMonitoring)
		return;

	// Start at end of file
	BFile file(kSyslogPath, B_READ_ONLY);
	if (file.InitCheck() == B_OK) {
		file.GetSize(&fLastPosition);
	}

	fMonitoring = true;
	fMonitorThread = spawn_thread(_MonitorThread, "syslog_monitor",
		B_LOW_PRIORITY, this);

	if (fMonitorThread >= 0)
		resume_thread(fMonitorThread);
}


void
SyslogView::StopMonitoring()
{
	fLock.Lock();
	fMonitoring = false;
	fLock.Unlock();

	if (fMonitorThread >= 0) {
		status_t result;
		wait_for_thread(fMonitorThread, &result);
		fMonitorThread = -1;
	}
}


int32
SyslogView::_MonitorThread(void* data)
{
	SyslogView* view = static_cast<SyslogView*>(data);
	view->_MonitorLoop();
	return 0;
}


void
SyslogView::_MonitorLoop()
{
	char buffer[4096];

	while (true) {
		fLock.Lock();
		bool running = fMonitoring;
		fLock.Unlock();

		if (!running)
			break;

		BFile file(kSyslogPath, B_READ_ONLY);
		if (file.InitCheck() != B_OK) {
			snooze(1000000);  // 1 second
			continue;
		}

		off_t currentSize;
		file.GetSize(&currentSize);

		// Check if file was truncated/rotated
		if (currentSize < fLastPosition)
			fLastPosition = 0;

		if (currentSize > fLastPosition) {
			file.Seek(fLastPosition, SEEK_SET);

			ssize_t bytesRead;
			BString lineBuffer;

			while ((bytesRead = file.Read(buffer, sizeof(buffer) - 1)) > 0) {
				buffer[bytesRead] = '\0';

				// Process buffer line by line
				char* line = buffer;
				char* newline;

				while ((newline = strchr(line, '\n')) != NULL) {
					*newline = '\0';

					// Combine with any previous partial line
					lineBuffer << line;

					if (_MatchesFilter(lineBuffer.String()))
						AddLine(lineBuffer.String());

					lineBuffer.SetTo("");
					line = newline + 1;
				}

				// Keep any remaining partial line
				if (*line != '\0')
					lineBuffer << line;
			}

			fLastPosition = currentSize;
		}

		snooze(250000);  // Check every 250ms
	}
}


bool
SyslogView::_MatchesFilter(const char* line)
{
	if (fFilter.Length() == 0)
		return true;

	BString lineLower(line);
	lineLower.ToLower();

	// Simple OR filter matching
	BString filter(fFilter);
	filter.ToLower();

	char* token = strtok(const_cast<char*>(filter.String()), "|");
	while (token != NULL) {
		if (lineLower.FindFirst(token) >= 0)
			return true;
		token = strtok(NULL, "|");
	}

	return false;
}


void
SyslogView::AddLine(const char* line)
{
	// Apply noise filter
	if (_IsNoisy(line))
		return;

	// Check for duplicates
	if (_IsDuplicate(line))
		return;

	if (!LockLooper())
		return;

	_AppendColoredLine(line);

	// Limit number of lines
	int32 lineCount = CountLines();
	if (lineCount > fMaxLines) {
		int32 linesToRemove = lineCount - fMaxLines;
		int32 offset = OffsetAt(linesToRemove);
		Delete(0, offset);
	}

	// Scroll to bottom
	ScrollToOffset(TextLength());

	UnlockLooper();
}


void
SyslogView::_AppendColoredLine(const char* line)
{
	BString lineStr(line);
	int32 start = TextLength();

	// Determine color based on content
	rgb_color color = fDefaultColor;

	BString lineLower(line);
	lineLower.ToLower();

	if (lineLower.FindFirst("error") >= 0 ||
		lineLower.FindFirst("fail") >= 0 ||
		lineLower.FindFirst("warn") >= 0) {
		color = fErrorColor;
	} else if (lineLower.FindFirst("usb") >= 0 ||
			   lineLower.FindFirst("uvc") >= 0) {
		color = fUSBColor;
	} else if (lineLower.FindFirst("media") >= 0 ||
			   lineLower.FindFirst("video") >= 0 ||
			   lineLower.FindFirst("camera") >= 0) {
		color = fMediaColor;
	}

	lineStr << "\n";
	Insert(TextLength(), lineStr.String(), lineStr.Length());

	int32 end = TextLength();
	SetFontAndColor(start, end, NULL, 0, &color);
}


void
SyslogView::Clear()
{
	if (LockLooper()) {
		SetText("");
		UnlockLooper();
	}
}


void
SyslogView::SetFilter(const char* filter)
{
	BAutolock lock(fLock);
	fFilter = filter;
}


void
SyslogView::SetNoiseFilterEnabled(bool enabled)
{
	BAutolock lock(fLock);
	fNoiseFilterEnabled = enabled;
	fLastLine.SetTo("");
	fDuplicateCount = 0;
}


bool
SyslogView::_IsNoisy(const char* line)
{
	if (!fNoiseFilterEnabled)
		return false;

	for (int i = 0; kNoisyPatterns[i] != NULL; i++) {
		if (strstr(line, kNoisyPatterns[i]) != NULL)
			return true;
	}
	return false;
}


bool
SyslogView::_IsDuplicate(const char* line)
{
	if (!fNoiseFilterEnabled)
		return false;

	if (fLastLine == line) {
		fDuplicateCount++;
		return true;
	}

	fLastLine.SetTo(line);
	fDuplicateCount = 0;
	return false;
}
