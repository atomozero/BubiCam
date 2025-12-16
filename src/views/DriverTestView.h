/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef DRIVER_TEST_VIEW_H
#define DRIVER_TEST_VIEW_H

#include <View.h>
#include <Button.h>
#include <TextView.h>
#include <StatusBar.h>
#include <CheckBox.h>
#include <StringView.h>
#include <Locker.h>
#include <MessageRunner.h>
#include <ObjectList.h>

class WebcamDevice;

// Test result structure
struct TestResult {
	BString		testName;
	bool		passed;
	BString		details;
	bigtime_t	duration;		// microseconds
	int32		iterations;
	int32		failures;

	TestResult()
		: passed(false), duration(0), iterations(0), failures(0)
	{}
};

// Frame timing data for latency/drop analysis
struct FrameTiming {
	bigtime_t	captureTime;	// When frame was captured by driver
	bigtime_t	receiveTime;	// When we received the buffer
	bigtime_t	displayTime;	// When frame was displayed
	uint32		frameNumber;
	bool		dropped;

	FrameTiming()
		: captureTime(0), receiveTime(0), displayTime(0),
		  frameNumber(0), dropped(false)
	{}
};

// Message constants for DriverTestView
enum {
	MSG_TEST_START_STRESS		= 'tsts',
	MSG_TEST_START_LATENCY		= 'tstl',
	MSG_TEST_START_FORMAT		= 'tstf',
	MSG_TEST_START_MEMORY		= 'tstm',
	MSG_TEST_EXPORT_REPORT		= 'texp',
	MSG_TEST_STOP				= 'tstp',
	MSG_TEST_PROGRESS			= 'tprg',
	MSG_TEST_COMPLETE			= 'tcmp',
	MSG_TEST_FRAME_TIMING		= 'tftm'
};


class DropFrameGraphView : public BView {
public:
						DropFrameGraphView(const char* name);
	virtual				~DropFrameGraphView();

	virtual void		Draw(BRect updateRect);
	virtual void		FrameResized(float width, float height);

	void				AddDataPoint(bool dropped, float fps);
	void				Clear();
	void				SetMaxPoints(int32 max) { fMaxPoints = max; }

private:
	struct DataPoint {
		bool	dropped;
		float	fps;
	};

	BObjectList<DataPoint>	fDataPoints;
	int32					fMaxPoints;
	BLocker					fLock;
};


class DriverTestView : public BView {
public:
						DriverTestView(const char* name);
	virtual				~DriverTestView();

	virtual void		AttachedToWindow();
	virtual void		MessageReceived(BMessage* message);

	void				SetDevice(WebcamDevice* device);
	void				SetTarget(BLooper* target) { fTarget = target; }

	// Frame timing (called from VideoConsumer via MainWindow)
	void				RecordFrameTiming(bigtime_t captureTime,
							bigtime_t receiveTime, bool dropped);

	// Test control
	void				StopCurrentTest();
	bool				IsTestRunning() const { return fTestRunning; }

	// Results
	const BObjectList<TestResult>&	GetResults() const { return fResults; }
	BString				GenerateDiagnosticReport();

private:
	void				_BuildLayout();
	void				_UpdateButtonStates();
	void				_AppendLog(const char* text, rgb_color color);
	void				_AppendLog(const char* text);
	void				_ClearLog();

	// Test implementations
	void				_RunStressTest();
	void				_RunLatencyTest();
	void				_RunFormatBenchmark();
	void				_RunMemoryTest();

	// Test thread entry points
	static int32		_StressTestThread(void* data);
	static int32		_LatencyTestThread(void* data);
	static int32		_FormatTestThread(void* data);
	static int32		_MemoryTestThread(void* data);

	void				_TestComplete(TestResult* result);
	void				_UpdateProgress(float percent, const char* status);

	// UI elements
	BButton*			fStressTestButton;
	BButton*			fLatencyTestButton;
	BButton*			fFormatTestButton;
	BButton*			fMemoryTestButton;
	BButton*			fExportReportButton;
	BButton*			fStopButton;
	BTextView*			fLogView;
	BStatusBar*			fProgressBar;
	BStringView*		fStatusLabel;
	DropFrameGraphView*	fDropGraph;

	// Test options
	BCheckBox*			fStressIterationsCheck;
	BCheckBox*			fIncludeResolutionChange;

	// State
	WebcamDevice*		fDevice;
	BLooper*			fTarget;
	bool				fTestRunning;
	thread_id			fTestThread;
	volatile bool		fStopRequested;

	// Results and timing data
	BObjectList<TestResult>		fResults;
	BObjectList<FrameTiming>	fFrameTimings;
	BLocker						fTimingLock;

	// Latency statistics
	bigtime_t			fMinLatency;
	bigtime_t			fMaxLatency;
	bigtime_t			fAvgLatency;
	uint32				fLatencySamples;

	// Colors
	rgb_color			fSuccessColor;
	rgb_color			fErrorColor;
	rgb_color			fWarningColor;
	rgb_color			fInfoColor;
};

#endif // DRIVER_TEST_VIEW_H
