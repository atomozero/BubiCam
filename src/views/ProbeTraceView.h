/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Probe/Commit tracer: parses UVC negotiation from the syslog into a
 * per-resolution table with a fit/step-down/refused verdict.
 * MIT License
 */

#ifndef PROBE_TRACE_VIEW_H
#define PROBE_TRACE_VIEW_H

#include <View.h>
#include <Locker.h>
#include <ObjectList.h>
#include <String.h>

struct ProbeAttempt {
	BString		device;
	int32		width;
	int32		height;
	int32		frameIndex;
	int32		negInterval;
	uint32		maxFrameSize;
	uint32		maxPayload;
	uint32		required;
	uint32		singleMax;
	int32		alt;
	uint32		bandwidth;
	int32		mult;
	bool		highBandwidth;
	bool		refused;
	bool		stepDown;
	int32		toWidth;
	int32		toHeight;
	bool		atMin;

	ProbeAttempt();
};

class BTextView;
class BStringView;

class ProbeTraceView : public BView {
public:
						ProbeTraceView(const char* name);
	virtual				~ProbeTraceView();

	virtual void		AttachedToWindow();
	virtual void		MessageReceived(BMessage* message);

	void				StartMonitoring();
	void				StopMonitoring();
	void				Rescan();
	void				Clear();

private:
	static int32		_MonitorThread(void* data);
	void				_MonitorLoop();
	void				_ParseLine(const char* line);
	ProbeAttempt*		_Current(const char* device, bool create);
	void				_Refresh();
	void				_RenderAttempt(const ProbeAttempt* a, BString& out);
	static void			_DeviceTag(const char* line, BString& out);

	enum {
		MSG_RESCAN		= 'pbtr',
		MSG_CLEAR		= 'pbtc'
	};

	static const char*	kSyslogPath;
	static const int32	kMaxAttempts = 60;

	BTextView*			fReport;
	BStringView*		fStatus;
	thread_id			fMonitorThread;
	bool				fMonitoring;
	BLocker				fLock;
	BObjectList<ProbeAttempt, true> fAttempts;
	off_t				fLastPosition;
};

#endif // PROBE_TRACE_VIEW_H
