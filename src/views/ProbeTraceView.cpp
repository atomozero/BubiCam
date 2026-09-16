/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Probe/Commit tracer implementation.
 * MIT License
 */

#include "ProbeTraceView.h"

#include <Autolock.h>
#include <Button.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <OS.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

#include <stdio.h>
#include <string.h>


const char* ProbeTraceView::kSyslogPath = "/var/log/syslog";


ProbeAttempt::ProbeAttempt()
	:
	width(0),
	height(0),
	frameIndex(-1),
	negInterval(0),
	maxFrameSize(0),
	maxPayload(0),
	required(0),
	singleMax(0),
	alt(-1),
	bandwidth(0),
	mult(1),
	highBandwidth(false),
	refused(false),
	stepDown(false),
	toWidth(0),
	toHeight(0),
	atMin(false)
{
}


ProbeTraceView::ProbeTraceView(const char* name)
	:
	BView(name, B_WILL_DRAW),
	fReport(NULL),
	fStatus(NULL),
	fMonitorThread(-1),
	fMonitoring(false),
	fLock("probe trace lock"),
	fLastPosition(0)
{

	BLayoutBuilder::Group<>(this, B_VERTICAL, 4)
		.Add(fStatus = new BStringView("probeStatus", "No negotiation traced yet"))
		.AddGroup(B_HORIZONTAL, 4)
			.Add(new BButton("probeRescan", "Rescan",
				new BMessage(MSG_RESCAN)))
			.Add(new BButton("probeClear", "Clear",
				new BMessage(MSG_CLEAR)))
			.AddGlue()
		.End()
		.Add(fReport = new BTextView("probeReport", B_WILL_DRAW))
		.SetInsets(4, 4, 4, 4);

	fReport->MakeEditable(false);
	fReport->MakeSelectable(true);
	fReport->SetWordWrap(false);
}


ProbeTraceView::~ProbeTraceView()
{
	StopMonitoring();
}


void
ProbeTraceView::AttachedToWindow()
{
	BView::AttachedToWindow();

	BFont font(be_fixed_font);
	font.SetSize(10);
	fReport->SetFontAndColor(&font);

	// Route button messages to this view
	for (int32 i = 0; i < CountChildren(); i++) {
		BButton* button = dynamic_cast<BButton*>(ChildAt(i));
		if (button != NULL)
			button->SetTarget(this);
	}
	// Buttons live inside an intermediate group view; cover that too
	for (int32 i = 0; i < CountChildren(); i++) {
		BView* child = ChildAt(i);
		for (int32 j = 0; j < child->CountChildren(); j++) {
			BButton* button = dynamic_cast<BButton*>(child->ChildAt(j));
			if (button != NULL)
				button->SetTarget(this);
		}
	}
}


void
ProbeTraceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_RESCAN:
			Rescan();
			break;
		case MSG_CLEAR:
			Clear();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ProbeTraceView::StartMonitoring()
{
	BAutolock lock(fLock);
	if (fMonitoring)
		return;
	fMonitoring = true;
	lock.Unlock();

	fMonitorThread = spawn_thread(_MonitorThread, "probe_trace_monitor",
		B_LOW_PRIORITY, this);
	if (fMonitorThread >= 0)
		resume_thread(fMonitorThread);
}


void
ProbeTraceView::StopMonitoring()
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


void
ProbeTraceView::Rescan()
{
	BAutolock lock(fLock);
	fAttempts.MakeEmpty();
	fLastPosition = 0;

	BFile file(kSyslogPath, B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		lock.Unlock();
		_Refresh();
		return;
	}

	char buffer[4096];
	BString pending;
	while (true) {
		ssize_t read = file.Read(buffer, sizeof(buffer) - 1);
		if (read <= 0)
			break;
		buffer[read] = '\0';
		pending << buffer;
		int32 pos;
		while ((pos = pending.FindFirst('\n')) >= 0) {
			BString line;
			pending.CopyInto(line, 0, pos);
			pending.Remove(0, pos + 1);
			_ParseLine(line.String());
		}
	}
	off_t size;
	file.GetSize(&size);
	fLastPosition = size;
	lock.Unlock();
	_Refresh();
}


void
ProbeTraceView::Clear()
{
	BAutolock lock(fLock);
	fAttempts.MakeEmpty();
	lock.Unlock();
	_Refresh();
}


int32
ProbeTraceView::_MonitorThread(void* data)
{
	ProbeTraceView* view = static_cast<ProbeTraceView*>(data);
	view->_MonitorLoop();
	return 0;
}


void
ProbeTraceView::_MonitorLoop()
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
			snooze(1000000);
			continue;
		}

		off_t currentSize;
		file.GetSize(&currentSize);

		fLock.Lock();
		if (currentSize < fLastPosition)
			fLastPosition = 0;  // rotated/truncated
		bool changed = false;
		if (currentSize > fLastPosition) {
			file.Seek(fLastPosition, SEEK_SET);
			BString chunk;
			while (true) {
				ssize_t read = file.Read(buffer, sizeof(buffer) - 1);
				if (read <= 0)
					break;
				buffer[read] = '\0';
				chunk << buffer;
			}
			fLastPosition = currentSize;
			// Parse line by line
			int32 pos;
			while ((pos = chunk.FindFirst('\n')) >= 0) {
				BString line;
				chunk.CopyInto(line, 0, pos);
				chunk.Remove(0, pos + 1);
				int32 before = fAttempts.CountItems();
				_ParseLine(line.String());
				if (fAttempts.CountItems() != before)
					changed = true;
				else {
					// Existing attempt may have been updated
					changed = true;
				}
			}
		}
		fLock.Unlock();

		if (changed)
			_Refresh();

		snooze(1500000);
	}
}


void
ProbeTraceView::_DeviceTag(const char* line, BString& out)
{
	out = "?";
	const char* open = strchr(line, '[');
	const char* close = open != NULL ? strchr(open, ']') : NULL;
	if (open != NULL && close != NULL && close > open + 1) {
		out.SetTo(open + 1, close - open - 1);
		// Keep only the cam tag (e.g. "cam1 0c45:6409")
		int32 space = out.FindFirst(' ');
		if (space > 0)
			out.Truncate(space);
	}
}


ProbeAttempt*
ProbeTraceView::_Current(const char* device, bool create)
{
	BString want(device);
	if (want == "?") {
		// Untagged line: attach to the latest attempt (single-camera
		// assumption; multi-cam interleaving is rare in practice).
		if (fAttempts.CountItems() > 0)
			return fAttempts.ItemAt(0);
	} else {
		for (int32 i = 0; i < fAttempts.CountItems(); i++) {
			ProbeAttempt* attempt = fAttempts.ItemAt(i);
			if (attempt->device == want) {
				// Adopt the tag for previously untagged attempts
				return attempt;
			}
		}
		// Adopt tag on the latest untagged attempt
		if (fAttempts.CountItems() > 0) {
			ProbeAttempt* latest = fAttempts.ItemAt(0);
			if (latest->device == "?") {
				latest->device = want;
				return latest;
			}
		}
	}
	if (!create)
		return NULL;
	ProbeAttempt* attempt = new ProbeAttempt();
	attempt->device = want;
	// Newest first
	fAttempts.AddItem(attempt, 0);
	while (fAttempts.CountItems() > kMaxAttempts)
		delete fAttempts.RemoveItemAt(fAttempts.CountItems() - 1);
	return fAttempts.ItemAt(0);
}


void
ProbeTraceView::_ParseLine(const char* line)
{
	if (strstr(line, "UVC Probe") == NULL
		&& strstr(line, "Required bandwidth") == NULL
		&& strstr(line, "Pass 1") == NULL
		&& strstr(line, "Using alternate") == NULL
		&& strstr(line, "usable alternate") == NULL
		&& strstr(line, "Reducing resolution") == NULL
		&& strstr(line, "Set resolution level") == NULL
		&& strstr(line, "Scanning") == NULL
		&& strstr(line, "alternate settings") == NULL)
		return;

	BString device;
	_DeviceTag(line, device);

	// New negotiation attempt: frame_index line. Reuse the latest attempt
	// unless it already ran a full cycle (alternate selected or refused),
	// so repeated "SuggestVideoFrame" logs for one negotiation stay together.
	const char* corresponds = strstr(line, "corresponds to");
	if (strstr(line, "frame_index") != NULL && corresponds != NULL) {
		int w = 0, h = 0, frame = -1;
		const char* fi = strstr(line, "frame_index=");
		if (fi != NULL)
			sscanf(fi + 12, "%d", &frame);
		if (sscanf(corresponds + 15, "%dx%d", &w, &h) == 2
			&& w > 0 && h > 0) {
			ProbeAttempt* attempt = NULL;
			if (fAttempts.CountItems() > 0) {
				ProbeAttempt* latest = fAttempts.ItemAt(0);
				if (latest->width > 0
					&& (latest->alt >= 0 || latest->refused)) {
					attempt = _Current(device.String(), true);
				} else {
					attempt = latest;
				}
			} else {
				attempt = _Current(device.String(), true);
			}
			attempt->frameIndex = frame;
			attempt->width = w;
			attempt->height = h;
		}
		return;
	}

	ProbeAttempt* attempt = _Current(device.String(), false);
	if (attempt == NULL) {
		// Attach to a device entry anyway so verdicts are not lost
		attempt = _Current(device.String(), true);
	}

	int interval = 0;
	unsigned int value = 0, value2 = 0;
	int alt = 0, mult = 1;
	const char* p = NULL;

	if ((p = strstr(line, "negotiated:")) != NULL
		&& (p = strstr(p, "interval=")) != NULL
		&& sscanf(p + 9, "%d", &interval) == 1) {
		attempt->negInterval = interval;
	} else if ((p = strstr(line, "maxVideoFrameSize=")) != NULL
		&& sscanf(p + 18, "%u", &value) == 1) {
		attempt->maxFrameSize = value;
		if ((p = strstr(line, "maxPayloadTransfer=")) != NULL
			&& sscanf(p + 19, "%u", &value2) == 1)
			attempt->maxPayload = value2;
	} else if ((p = strstr(line, "Required bandwidth from probe:")) != NULL
		&& sscanf(p + 30, "%u", &value) == 1) {
		attempt->required = value;
	} else if ((p = strstr(line, "single-transaction endpoint with")) != NULL
		&& sscanf(p + 32, "%u", &value) == 1) {
		attempt->singleMax = value;
	} else if ((p = strstr(line, "promoted to alt")) != NULL
		&& sscanf(p + 16, "%d, %u bytes/uframe (mult=%d)",
			&alt, &value, &mult) == 3) {
		attempt->alt = alt;
		attempt->bandwidth = value;
		attempt->mult = mult;
		attempt->highBandwidth = true;
	} else if ((p = strstr(line, "Using alternate")) != NULL
		&& sscanf(p + 15, "%d", &alt) == 1
		&& (p = strstr(p, "bandwidth")) != NULL
		&& sscanf(p + 9, "%u", &value) == 1) {
		// Keep promotion info if already set (has mult); else record plain
		if (!attempt->highBandwidth) {
			attempt->alt = alt;
			attempt->bandwidth = value;
			attempt->mult = 1;
		} else if (attempt->alt < 0) {
			attempt->alt = alt;
			attempt->bandwidth = value;
		}
	} else if (strstr(line, "usable alternate provides") != NULL
		|| strstr(line, "refusing doomed stream") != NULL) {
		attempt->refused = true;
	} else if (strstr(line, "stepping down resolution") != NULL) {
		attempt->stepDown = true;
	} else if ((p = strstr(line, "Reducing resolution from")) != NULL) {
		int fw = 0, fh = 0, tw = 0, th = 0;
		if (sscanf(p + 25, "%dx%d to %dx%d", &fw, &fh, &tw, &th) == 4) {
			attempt->stepDown = true;
			attempt->toWidth = tw;
			attempt->toHeight = th;
		}
	} else if (strstr(line, "minimum resolution") != NULL) {
		attempt->atMin = true;
	} else if ((p = strstr(line, "Set resolution level to")) != NULL) {
		int level = 0, w = 0, h = 0;
		const char* forRes = strstr(p, " for ");
		if (forRes != NULL && sscanf(forRes + 5, "%dx%d", &w, &h) == 2
			&& w > 0 && h > 0) {
			if (attempt->width <= 0) {
				attempt->width = w;
				attempt->height = h;
			}
			if (sscanf(p + 23, "%d", &level) == 1)
				attempt->frameIndex = attempt->frameIndex >= 0
					? attempt->frameIndex : level;
		}
	}
}


void
ProbeTraceView::_RenderAttempt(const ProbeAttempt* a, BString& out)
{
	BString line;
	line.SetToFormat("[%s] %dx%d", a->device.String(),
		(int)a->width, (int)a->height);
	if (a->frameIndex >= 0)
		line << BString().SetToFormat(" (frame %d)", (int)a->frameIndex);
	if (a->negInterval > 0)
		line << BString().SetToFormat(" @%.1ffps", 1000000.0f / a->negInterval);
	out << line << "\n";

	if (a->maxPayload > 0) {
		line.SetToFormat("  probe: payload %u B/uframe", a->maxPayload);
		if (a->maxFrameSize > 0)
			line << BString().SetToFormat(" (frame %u B)", a->maxFrameSize);
		out << line << "\n";
	}
	if (a->singleMax > 0) {
		line.SetToFormat("  single-transaction max: %u B/uframe",
			a->singleMax);
		out << line << "\n";
	}
	if (a->alt >= 0) {
		line.SetToFormat("  alternate %d: %u B/uframe (mult=%d)%s",
			(int)a->alt, a->bandwidth, (int)a->mult,
			a->highBandwidth ? " [HIGH-BANDWIDTH]" : "");
		out << line << "\n";
	}

	// Verdict
	if (a->refused && a->atMin) {
		out << "  verdict: XX REFUSED at minimum resolution\n";
		out << "           need high-bandwidth (WEBCAM_FORCE_HIGH_BANDWIDTH=1)\n";
	} else if (a->refused || a->stepDown) {
		out << "  verdict: >> STEP DOWN";
		if (a->toWidth > 0) {
			line.SetToFormat(" to %dx%d",
				(int)a->toWidth, (int)a->toHeight);
			out << line;
		}
		out << "\n";
	} else if (a->highBandwidth) {
		out << "  verdict: OK via high-bandwidth\n";
	} else if (a->alt >= 0 && a->required > 0 && a->bandwidth > 0) {
		if (a->required <= a->bandwidth)
			out << "  verdict: OK single-transaction\n";
		else {
			line.SetToFormat("  verdict: XX MISMATCH need %u > have %u\n",
				a->required, a->bandwidth);
			out << line;
		}
	} else if (a->alt >= 0) {
		out << "  verdict: OK (alternate selected)\n";
	} else {
		out << "  verdict: .. negotiating\n";
	}
	out << "\n";
}


void
ProbeTraceView::_Refresh()
{
	BAutolock lock(fLock);
	BString text;
	text << "UVC Probe/Commit trace (" << fAttempts.CountItems()
		<< " attempts)\n";
	text << "================================================\n\n";
	for (int32 i = 0; i < fAttempts.CountItems(); i++)
		_RenderAttempt(fAttempts.ItemAt(i), text);
	BString status;
	if (fAttempts.CountItems() > 0) {
		const ProbeAttempt* latest = fAttempts.ItemAt(0);
		status.SetToFormat("Latest: [%s] %dx%d",
			latest->device.String(),
			(int)latest->width, (int)latest->height);
		if (latest->refused)
			status << " - REFUSED";
		else if (latest->stepDown)
			status << " - STEP DOWN";
		else if (latest->alt >= 0)
			status << " - streaming candidate";
		else
			status << " - negotiating";
	} else {
		status = "No negotiation traced yet - start a preview, then Rescan";
	}
	BString statusCopy(status);
	BString textCopy(text);
	lock.Unlock();

	if (LockLooper()) {
		fReport->SetText(textCopy.String());
		fStatus->SetText(statusCopy.String());
		UnlockLooper();
	}
}
