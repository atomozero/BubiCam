/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "DriverTestView.h"
#include "WebcamDevice.h"
#include "MainWindow.h"

#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <GroupView.h>
#include <SeparatorView.h>
#include <Box.h>
#include <Autolock.h>
#include <OS.h>
#include <Path.h>
#include <FindDirectory.h>
#include <File.h>
#include <NodeInfo.h>
#include <Roster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Logging macros
#define LOG_MODULE "DriverTestView"
#include "ErrorUtils.h"


// ============================================================================
// DropFrameGraphView - Visual graph of frame drops over time
// ============================================================================

DropFrameGraphView::DropFrameGraphView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fDataPoints(true),	// owns items
	fMaxPoints(300),
	fLock("graph lock")
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


DropFrameGraphView::~DropFrameGraphView()
{
}


void
DropFrameGraphView::Draw(BRect updateRect)
{
	BAutolock lock(fLock);

	BRect bounds = Bounds();
	float width = bounds.Width();
	float height = bounds.Height();

	// Background
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(tint_color(panel, B_DARKEN_MAX_TINT));
	FillRect(bounds);

	// Border
	SetHighColor(tint_color(panel, B_DARKEN_3_TINT));
	StrokeRect(bounds);

	int32 count = fDataPoints.CountItems();
	if (count < 2)
		return;

	// Draw grid lines
	SetHighColor(tint_color(panel, B_DARKEN_4_TINT));
	for (int i = 1; i < 4; i++) {
		float y = height * i / 4;
		StrokeLine(BPoint(0, y), BPoint(width, y));
	}

	// Calculate x step
	float xStep = width / (float)(fMaxPoints - 1);
	int32 startIdx = count > fMaxPoints ? count - fMaxPoints : 0;

	// Draw FPS line (green)
	SetHighColor(80, 200, 80);
	SetPenSize(1.5);

	BPoint prevPoint;
	bool first = true;
	for (int32 i = startIdx; i < count; i++) {
		DataPoint* dp = fDataPoints.ItemAt(i);
		float x = (i - startIdx) * xStep;
		// Normalize FPS to 0-60 range
		float normalizedFps = dp->fps / 60.0f;
		if (normalizedFps > 1.0f) normalizedFps = 1.0f;
		float y = height - (normalizedFps * height * 0.9f) - 5;

		BPoint currentPoint(x, y);
		if (!first) {
			StrokeLine(prevPoint, currentPoint);
		}
		prevPoint = currentPoint;
		first = false;
	}

	// Draw drop markers (red vertical lines)
	SetHighColor(255, 80, 80);
	SetPenSize(2.0);
	for (int32 i = startIdx; i < count; i++) {
		DataPoint* dp = fDataPoints.ItemAt(i);
		if (dp->dropped) {
			float x = (i - startIdx) * xStep;
			StrokeLine(BPoint(x, 5), BPoint(x, height - 5));
		}
	}

	SetPenSize(1.0);

	// Draw labels
	rgb_color labelColor = tint_color(panel, B_LIGHTEN_2_TINT);
	SetHighColor(labelColor);
	BFont font(be_plain_font);
	font.SetSize(9);
	SetFont(&font);

	DrawString("60fps", BPoint(5, 15));
	DrawString("30fps", BPoint(5, height / 2));
	DrawString("0fps", BPoint(5, height - 5));

	// Legend
	float legendX = width - 80;
	SetHighColor(80, 200, 80);
	FillRect(BRect(legendX, 5, legendX + 10, 10));
	SetHighColor(labelColor);
	DrawString("FPS", BPoint(legendX + 15, 12));

	SetHighColor(255, 80, 80);
	FillRect(BRect(legendX, 17, legendX + 10, 22));
	SetHighColor(labelColor);
	DrawString("Drop", BPoint(legendX + 15, 24));
}


void
DropFrameGraphView::FrameResized(float width, float height)
{
	Invalidate();
}


void
DropFrameGraphView::AddDataPoint(bool dropped, float fps)
{
	BAutolock lock(fLock);

	DataPoint* dp = new DataPoint();
	dp->dropped = dropped;
	dp->fps = fps;
	fDataPoints.AddItem(dp);

	// Remove old points
	while (fDataPoints.CountItems() > fMaxPoints * 2) {
		fDataPoints.RemoveItemAt(0);
	}

	// Redraw (from window thread)
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
DropFrameGraphView::Clear()
{
	BAutolock lock(fLock);
	fDataPoints.MakeEmpty();

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


// ============================================================================
// DriverTestView - Main test control panel
// ============================================================================

DriverTestView::DriverTestView(const char* name)
	:
	BView(name, B_WILL_DRAW),
	fStressTestButton(NULL),
	fLatencyTestButton(NULL),
	fFormatTestButton(NULL),
	fMemoryTestButton(NULL),
	fExportReportButton(NULL),
	fStopButton(NULL),
	fLogView(NULL),
	fProgressBar(NULL),
	fStatusLabel(NULL),
	fDropGraph(NULL),
	fStressIterationsCheck(NULL),
	fIncludeResolutionChange(NULL),
	fDevice(NULL),
	fTarget(NULL),
	fTestRunning(false),
	fTestThread(-1),
	fStopRequested(false),
	fResults(true),		// owns items
	fFrameTimings(true),
	fTimingLock("timing lock"),
	fMinLatency(LLONG_MAX),
	fMaxLatency(0),
	fAvgLatency(0),
	fLatencySamples(0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Colors
	fSuccessColor = (rgb_color){80, 200, 80, 255};
	fErrorColor = (rgb_color){255, 80, 80, 255};
	fWarningColor = (rgb_color){255, 200, 80, 255};
	fInfoColor = (rgb_color){80, 160, 255, 255};
}


DriverTestView::~DriverTestView()
{
	StopCurrentTest();
}


void
DriverTestView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_BuildLayout();
	_UpdateButtonStates();
}


void
DriverTestView::_BuildLayout()
{
	// Test buttons
	fStressTestButton = new BButton("stressTest", "Stress Test",
		new BMessage(MSG_TEST_START_STRESS));
	fStressTestButton->SetToolTip("Repeated start/stop cycles to test driver stability");

	fLatencyTestButton = new BButton("latencyTest", "Latency Test",
		new BMessage(MSG_TEST_START_LATENCY));
	fLatencyTestButton->SetToolTip("Measure capture-to-display latency");

	fFormatTestButton = new BButton("formatTest", "Format Benchmark",
		new BMessage(MSG_TEST_START_FORMAT));
	fFormatTestButton->SetToolTip("Compare performance across all formats");

	fMemoryTestButton = new BButton("memoryTest", "Memory Test",
		new BMessage(MSG_TEST_START_MEMORY));
	fMemoryTestButton->SetToolTip("Check for memory leaks during extended capture");

	fExportReportButton = new BButton("exportReport", "Export Report",
		new BMessage(MSG_TEST_EXPORT_REPORT));
	fExportReportButton->SetToolTip("Generate diagnostic report for bug reports");

	fStopButton = new BButton("stop", "Stop Test",
		new BMessage(MSG_TEST_STOP));
	fStopButton->SetEnabled(false);

	// Options
	fStressIterationsCheck = new BCheckBox("iterations",
		"Extended (100 cycles)", NULL);
	fIncludeResolutionChange = new BCheckBox("resChange",
		"Include resolution changes", NULL);
	fIncludeResolutionChange->SetValue(B_CONTROL_ON);

	// Progress
	fProgressBar = new BStatusBar("progress");
	fProgressBar->SetMaxValue(100.0f);
	fProgressBar->SetBarHeight(12.0f);

	fStatusLabel = new BStringView("status", "Ready");

	// Drop frame graph
	fDropGraph = new DropFrameGraphView("dropGraph");
	fDropGraph->SetExplicitMinSize(BSize(B_SIZE_UNSET, 80));
	fDropGraph->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 120));

	// Log view
	fLogView = new BTextView("log");
	fLogView->MakeEditable(false);
	fLogView->SetStylable(true);
	fLogView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	BFont monoFont(be_fixed_font);
	monoFont.SetSize(10);
	fLogView->SetFontAndColor(&monoFont);

	BScrollView* logScroll = new BScrollView("logScroll", fLogView,
		B_FRAME_EVENTS, false, true);

	// Set targets
	fStressTestButton->SetTarget(this);
	fLatencyTestButton->SetTarget(this);
	fFormatTestButton->SetTarget(this);
	fMemoryTestButton->SetTarget(this);
	fExportReportButton->SetTarget(this);
	fStopButton->SetTarget(this);

	// Buttons box
	BBox* testBox = new BBox("testBox");
	testBox->SetLabel("Driver Tests");
	testBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.AddGrid(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)
			.Add(fStressTestButton, 0, 0)
			.Add(fLatencyTestButton, 1, 0)
			.Add(fFormatTestButton, 0, 1)
			.Add(fMemoryTestButton, 1, 1)
			.End()
		.AddStrut(B_USE_SMALL_SPACING)
		.Add(fStressIterationsCheck)
		.Add(fIncludeResolutionChange)
		.AddStrut(B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fExportReportButton)
			.AddGlue()
			.Add(fStopButton)
			.End()
		.View());

	// Graph box
	BBox* graphBox = new BBox("graphBox");
	graphBox->SetLabel("Frame Rate / Drops");
	graphBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, 0)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fDropGraph)
		.View());

	// Progress box
	BBox* progressBox = new BBox("progressBox");
	progressBox->SetLabel("Progress");
	progressBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fProgressBar)
		.Add(fStatusLabel)
		.View());

	// Main layout
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(testBox)
		.Add(graphBox)
		.Add(progressBox)
		.Add(logScroll, 1.0f)
		.End();
}


void
DriverTestView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_TEST_START_STRESS:
			_RunStressTest();
			break;

		case MSG_TEST_START_LATENCY:
			_RunLatencyTest();
			break;

		case MSG_TEST_START_FORMAT:
			_RunFormatBenchmark();
			break;

		case MSG_TEST_START_MEMORY:
			_RunMemoryTest();
			break;

		case MSG_TEST_EXPORT_REPORT:
		{
			BString report = GenerateDiagnosticReport();
			// Save to file
			BPath path;
			find_directory(B_USER_DIRECTORY, &path);
			path.Append("BubiCam_DiagReport.txt");

			BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
			if (file.InitCheck() == B_OK) {
				file.Write(report.String(), report.Length());

				BString msg;
				msg.SetToFormat("Report saved to:\n%s", path.Path());
				_AppendLog(msg.String(), fSuccessColor);
			} else {
				_AppendLog("Failed to save report!", fErrorColor);
			}
			break;
		}

		case MSG_TEST_STOP:
			StopCurrentTest();
			break;

		case MSG_TEST_PROGRESS:
		{
			float percent;
			const char* status;
			if (message->FindFloat("percent", &percent) == B_OK) {
				fProgressBar->SetTo(percent);
			}
			if (message->FindString("status", &status) == B_OK) {
				fStatusLabel->SetText(status);
			}
			break;
		}

		case MSG_TEST_COMPLETE:
		{
			TestResult* result = NULL;
			if (message->FindPointer("result", (void**)&result) == B_OK && result != NULL) {
				_TestComplete(result);
			}
			break;
		}

		case MSG_TEST_FRAME_TIMING:
		{
			bool dropped;
			float fps;
			if (message->FindBool("dropped", &dropped) == B_OK &&
				message->FindFloat("fps", &fps) == B_OK) {
				fDropGraph->AddDataPoint(dropped, fps);
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
DriverTestView::SetDevice(WebcamDevice* device)
{
	fDevice = device;
	_UpdateButtonStates();

	if (device != NULL) {
		BString msg;
		msg.SetToFormat("Device set: %s", device->Name());
		_AppendLog(msg.String(), fInfoColor);
	}
}


void
DriverTestView::RecordFrameTiming(bigtime_t captureTime,
	bigtime_t receiveTime, bool dropped)
{
	if (!fTestRunning)
		return;

	BAutolock lock(fTimingLock);

	FrameTiming* timing = new FrameTiming();
	timing->captureTime = captureTime;
	timing->receiveTime = receiveTime;
	timing->displayTime = system_time();
	timing->frameNumber = fFrameTimings.CountItems();
	timing->dropped = dropped;
	fFrameTimings.AddItem(timing);

	// Update latency stats
	if (!dropped && captureTime > 0) {
		bigtime_t latency = receiveTime - captureTime;
		if (latency > 0 && latency < 1000000) {  // Sanity check: < 1 second
			if (latency < fMinLatency) fMinLatency = latency;
			if (latency > fMaxLatency) fMaxLatency = latency;
			fAvgLatency = (fAvgLatency * fLatencySamples + latency) / (fLatencySamples + 1);
			fLatencySamples++;
		}
	}

	// Limit stored timings
	while (fFrameTimings.CountItems() > 1000) {
		fFrameTimings.RemoveItemAt(0);
	}
}


void
DriverTestView::StopCurrentTest()
{
	if (!fTestRunning)
		return;

	_AppendLog("Stopping test...", fWarningColor);
	fStopRequested = true;

	// Wait for thread with timeout
	if (fTestThread >= 0) {
		status_t exitValue;
		wait_for_thread(fTestThread, &exitValue);
		fTestThread = -1;
	}

	fTestRunning = false;
	fStopRequested = false;
	_UpdateButtonStates();
	fStatusLabel->SetText("Test stopped");
}


void
DriverTestView::_UpdateButtonStates()
{
	bool hasDevice = (fDevice != NULL);
	bool canRun = hasDevice && !fTestRunning;

	fStressTestButton->SetEnabled(canRun);
	fLatencyTestButton->SetEnabled(canRun);
	fFormatTestButton->SetEnabled(canRun);
	fMemoryTestButton->SetEnabled(canRun);
	fExportReportButton->SetEnabled(hasDevice);
	fStopButton->SetEnabled(fTestRunning);
}


void
DriverTestView::_AppendLog(const char* text, rgb_color color)
{
	if (fLogView == NULL)
		return;

	// Add timestamp
	bigtime_t now = system_time();
	int32 secs = (now / 1000000) % 3600;
	int32 ms = (now / 1000) % 1000;

	BString line;
	line.SetToFormat("[%02d:%03d] %s\n", (int)secs, (int)ms, text);

	// Append with color
	int32 textLen = fLogView->TextLength();
	fLogView->Insert(textLen, line.String(), line.Length());
	fLogView->SetFontAndColor(textLen, textLen + line.Length(),
		NULL, 0, &color);

	// Scroll to bottom
	fLogView->ScrollToOffset(fLogView->TextLength());
}


void
DriverTestView::_AppendLog(const char* text)
{
	rgb_color defaultColor = ui_color(B_PANEL_TEXT_COLOR);
	_AppendLog(text, defaultColor);
}


void
DriverTestView::_ClearLog()
{
	if (fLogView != NULL)
		fLogView->SetText("");
}


void
DriverTestView::_UpdateProgress(float percent, const char* status)
{
	BMessage msg(MSG_TEST_PROGRESS);
	msg.AddFloat("percent", percent);
	msg.AddString("status", status);
	BMessenger(this).SendMessage(&msg);
}


void
DriverTestView::_TestComplete(TestResult* result)
{
	fResults.AddItem(result);

	BString msg;
	msg.SetToFormat("=== %s: %s ===",
		result->testName.String(),
		result->passed ? "PASSED" : "FAILED");
	_AppendLog(msg.String(), result->passed ? fSuccessColor : fErrorColor);

	if (result->details.Length() > 0) {
		_AppendLog(result->details.String());
	}

	msg.SetToFormat("Duration: %.2f seconds, Iterations: %d, Failures: %d",
		result->duration / 1000000.0,
		(int)result->iterations,
		(int)result->failures);
	_AppendLog(msg.String(), fInfoColor);

	fTestRunning = false;
	fTestThread = -1;
	_UpdateButtonStates();
	fProgressBar->SetTo(100.0f);
	fStatusLabel->SetText(result->passed ? "Test passed" : "Test failed");
}


// ============================================================================
// Stress Test - Repeated start/stop cycles
// ============================================================================

void
DriverTestView::_RunStressTest()
{
	if (fDevice == NULL || fTestRunning)
		return;

	_ClearLog();
	_AppendLog("Starting Stress Test...", fInfoColor);
	_AppendLog("This test repeatedly starts and stops capture to check driver stability.");

	fDropGraph->Clear();
	fTestRunning = true;
	fStopRequested = false;
	_UpdateButtonStates();

	fTestThread = spawn_thread(_StressTestThread, "stress_test",
		B_NORMAL_PRIORITY, this);
	if (fTestThread >= 0) {
		resume_thread(fTestThread);
	} else {
		_AppendLog("Failed to start test thread!", fErrorColor);
		fTestRunning = false;
		_UpdateButtonStates();
	}
}


int32
DriverTestView::_StressTestThread(void* data)
{
	DriverTestView* view = static_cast<DriverTestView*>(data);
	WebcamDevice* device = view->fDevice;
	BLooper* target = view->fTarget;

	if (device == NULL || target == NULL)
		return -1;

	TestResult* result = new TestResult();
	result->testName = "Stress Test";

	bool extended = view->fStressIterationsCheck->Value() == B_CONTROL_ON;
	bool changeRes = view->fIncludeResolutionChange->Value() == B_CONTROL_ON;

	int32 iterations = extended ? 100 : 20;
	int32 failures = 0;
	bigtime_t startTime = system_time();

	BString status;

	for (int32 i = 0; i < iterations && !view->fStopRequested; i++) {
		status.SetToFormat("Cycle %d/%d", (int)(i + 1), (int)iterations);
		view->_UpdateProgress((i * 100.0f) / iterations, status.String());

		// Start capture
		status_t err = device->StartCapture(target);
		if (err != B_OK) {
			BString msg;
			msg.SetToFormat("Cycle %d: StartCapture failed: %s",
				(int)(i + 1), strerror(err));
			BMessage logMsg('_log');
			logMsg.AddString("text", msg.String());
			logMsg.AddBool("error", true);
			BMessenger(view).SendMessage(&logMsg);
			failures++;
		} else {
			// Let it run for a bit
			snooze(500000);  // 500ms

			// Check for frames
			uint32 frames = device->FramesCaptured();
			if (frames == 0) {
				failures++;
			}

			// Stop capture
			device->StopCapture();
		}

		// Brief pause between cycles
		snooze(200000);  // 200ms

		// Resolution change test
		if (changeRes && (i % 5 == 4) && !view->fStopRequested) {
			// Try different formats if available
			const BObjectList<VideoFormat>& formats = device->SupportedFormats();
			if (formats.CountItems() > 1) {
				int32 formatIdx = (i / 5) % formats.CountItems();
				VideoFormat* format = formats.ItemAt(formatIdx);
				if (format != NULL) {
					device->SetRequestedFormat(*format);
				}
			}
		}
	}

	result->duration = system_time() - startTime;
	result->iterations = iterations;
	result->failures = failures;
	result->passed = (failures == 0);

	if (failures > 0) {
		result->details.SetToFormat("%d failures out of %d cycles (%.1f%% success rate)",
			(int)failures, (int)iterations,
			100.0f * (iterations - failures) / iterations);
	} else {
		result->details = "All cycles completed successfully";
	}

	// Send completion message
	BMessage msg(MSG_TEST_COMPLETE);
	msg.AddPointer("result", result);
	BMessenger(view).SendMessage(&msg);

	return 0;
}


// ============================================================================
// Latency Test - Measure capture-to-display latency
// ============================================================================

void
DriverTestView::_RunLatencyTest()
{
	if (fDevice == NULL || fTestRunning)
		return;

	_ClearLog();
	_AppendLog("Starting Latency Test...", fInfoColor);
	_AppendLog("Measuring time from capture to display.");

	// Reset timing stats
	{
		BAutolock lock(fTimingLock);
		fFrameTimings.MakeEmpty();
		fMinLatency = LLONG_MAX;
		fMaxLatency = 0;
		fAvgLatency = 0;
		fLatencySamples = 0;
	}

	fDropGraph->Clear();
	fTestRunning = true;
	fStopRequested = false;
	_UpdateButtonStates();

	fTestThread = spawn_thread(_LatencyTestThread, "latency_test",
		B_NORMAL_PRIORITY, this);
	if (fTestThread >= 0) {
		resume_thread(fTestThread);
	} else {
		fTestRunning = false;
		_UpdateButtonStates();
	}
}


int32
DriverTestView::_LatencyTestThread(void* data)
{
	DriverTestView* view = static_cast<DriverTestView*>(data);
	WebcamDevice* device = view->fDevice;
	BLooper* target = view->fTarget;

	if (device == NULL || target == NULL)
		return -1;

	TestResult* result = new TestResult();
	result->testName = "Latency Test";

	bigtime_t startTime = system_time();
	bigtime_t testDuration = 10000000;  // 10 seconds

	// Start capture
	status_t err = device->StartCapture(target);
	if (err != B_OK) {
		result->passed = false;
		result->details.SetToFormat("Failed to start capture: %s", strerror(err));

		BMessage msg(MSG_TEST_COMPLETE);
		msg.AddPointer("result", result);
		BMessenger(view).SendMessage(&msg);
		return -1;
	}

	// Collect data for duration
	while (!view->fStopRequested) {
		bigtime_t elapsed = system_time() - startTime;
		if (elapsed >= testDuration)
			break;

		float progress = (elapsed * 100.0f) / testDuration;
		BString status;
		status.SetToFormat("Collecting data: %.0f%%", progress);
		view->_UpdateProgress(progress, status.String());

		// Update graph with current FPS
		float fps = device->CurrentFPS();
		uint32 dropped = device->FramesDropped();

		BMessage frameMsg(MSG_TEST_FRAME_TIMING);
		frameMsg.AddBool("dropped", dropped > 0);
		frameMsg.AddFloat("fps", fps);
		BMessenger(view).SendMessage(&frameMsg);

		snooze(100000);  // 100ms
	}

	device->StopCapture();

	result->duration = system_time() - startTime;
	result->iterations = view->fLatencySamples;

	// Calculate results
	BAutolock lock(view->fTimingLock);

	if (view->fLatencySamples > 0) {
		result->passed = true;
		result->details.SetToFormat(
			"Samples: %u\n"
			"Min latency: %.2f ms\n"
			"Max latency: %.2f ms\n"
			"Avg latency: %.2f ms",
			(unsigned)view->fLatencySamples,
			view->fMinLatency / 1000.0,
			view->fMaxLatency / 1000.0,
			view->fAvgLatency / 1000.0);
	} else {
		result->passed = false;
		result->details = "No timing data collected - driver may not provide timestamps";
	}

	BMessage msg(MSG_TEST_COMPLETE);
	msg.AddPointer("result", result);
	BMessenger(view).SendMessage(&msg);

	return 0;
}


// ============================================================================
// Format Benchmark - Compare all supported formats
// ============================================================================

void
DriverTestView::_RunFormatBenchmark()
{
	if (fDevice == NULL || fTestRunning)
		return;

	_ClearLog();
	_AppendLog("Starting Format Benchmark...", fInfoColor);
	_AppendLog("Testing each supported format for performance.");

	fDropGraph->Clear();
	fTestRunning = true;
	fStopRequested = false;
	_UpdateButtonStates();

	fTestThread = spawn_thread(_FormatTestThread, "format_test",
		B_NORMAL_PRIORITY, this);
	if (fTestThread >= 0) {
		resume_thread(fTestThread);
	} else {
		fTestRunning = false;
		_UpdateButtonStates();
	}
}


int32
DriverTestView::_FormatTestThread(void* data)
{
	DriverTestView* view = static_cast<DriverTestView*>(data);
	WebcamDevice* device = view->fDevice;
	BLooper* target = view->fTarget;

	if (device == NULL || target == NULL)
		return -1;

	TestResult* result = new TestResult();
	result->testName = "Format Benchmark";

	const BObjectList<VideoFormat>& formats = device->SupportedFormats();
	int32 formatCount = formats.CountItems();

	if (formatCount == 0) {
		result->passed = false;
		result->details = "No formats available to test";

		BMessage msg(MSG_TEST_COMPLETE);
		msg.AddPointer("result", result);
		BMessenger(view).SendMessage(&msg);
		return -1;
	}

	bigtime_t startTime = system_time();
	BString benchmarkResults;
	benchmarkResults = "Format Benchmark Results:\n";
	benchmarkResults << "========================\n";

	int32 successCount = 0;

	for (int32 i = 0; i < formatCount && !view->fStopRequested; i++) {
		VideoFormat* format = formats.ItemAt(i);
		if (format == NULL)
			continue;

		BString status;
		status.SetToFormat("Testing %dx%d %s",
			format->width, format->height, format->colorSpace);
		view->_UpdateProgress((i * 100.0f) / formatCount, status.String());

		// Set format
		device->SetRequestedFormat(*format);

		// Start capture
		status_t err = device->StartCapture(target);
		if (err != B_OK) {
			benchmarkResults << status << ": FAILED (";
			benchmarkResults << strerror(err) << ")\n";
			continue;
		}

		// Let it run
		snooze(3000000);  // 3 seconds

		// Collect stats
		uint32 frames = device->FramesCaptured();
		uint32 dropped = device->FramesDropped();
		float fps = device->CurrentFPS();

		device->StopCapture();

		benchmarkResults << status;
		benchmarkResults.Append(": ");
		BString stats;
		stats.SetToFormat("%.1f fps, %u frames, %u dropped\n",
			fps, (unsigned)frames, (unsigned)dropped);
		benchmarkResults << stats;

		if (frames > 0)
			successCount++;

		snooze(500000);  // Brief pause
	}

	result->duration = system_time() - startTime;
	result->iterations = formatCount;
	result->failures = formatCount - successCount;
	result->passed = (successCount > 0);
	result->details = benchmarkResults;

	// Clear requested format
	device->ClearRequestedFormat();

	BMessage msg(MSG_TEST_COMPLETE);
	msg.AddPointer("result", result);
	BMessenger(view).SendMessage(&msg);

	return 0;
}


// ============================================================================
// Memory Test - Extended capture to detect leaks
// ============================================================================

void
DriverTestView::_RunMemoryTest()
{
	if (fDevice == NULL || fTestRunning)
		return;

	_ClearLog();
	_AppendLog("Starting Memory Test...", fInfoColor);
	_AppendLog("Running extended capture to check for memory leaks.");
	_AppendLog("Monitor system memory usage during this test.");

	fDropGraph->Clear();
	fTestRunning = true;
	fStopRequested = false;
	_UpdateButtonStates();

	fTestThread = spawn_thread(_MemoryTestThread, "memory_test",
		B_NORMAL_PRIORITY, this);
	if (fTestThread >= 0) {
		resume_thread(fTestThread);
	} else {
		fTestRunning = false;
		_UpdateButtonStates();
	}
}


int32
DriverTestView::_MemoryTestThread(void* data)
{
	DriverTestView* view = static_cast<DriverTestView*>(data);
	WebcamDevice* device = view->fDevice;
	BLooper* target = view->fTarget;

	if (device == NULL || target == NULL)
		return -1;

	TestResult* result = new TestResult();
	result->testName = "Memory Test";

	bigtime_t testDuration = 60000000;  // 60 seconds
	bigtime_t startTime = system_time();

	// Get initial memory info
	system_info sysInfoStart;
	get_system_info(&sysInfoStart);
	uint64 usedPagesStart = sysInfoStart.used_pages;

	// Start capture
	status_t err = device->StartCapture(target);
	if (err != B_OK) {
		result->passed = false;
		result->details.SetToFormat("Failed to start capture: %s", strerror(err));

		BMessage msg(MSG_TEST_COMPLETE);
		msg.AddPointer("result", result);
		BMessenger(view).SendMessage(&msg);
		return -1;
	}

	// Run for duration
	uint32 checkpoints = 0;
	uint64 maxUsedPages = usedPagesStart;

	while (!view->fStopRequested) {
		bigtime_t elapsed = system_time() - startTime;
		if (elapsed >= testDuration)
			break;

		float progress = (elapsed * 100.0f) / testDuration;

		// Check memory every 5 seconds
		if ((elapsed / 5000000) > checkpoints) {
			checkpoints++;

			system_info sysInfo;
			get_system_info(&sysInfo);

			if (sysInfo.used_pages > maxUsedPages)
				maxUsedPages = sysInfo.used_pages;

			BString status;
			status.SetToFormat("Running: %.0f%% - Memory: %llu pages",
				progress, (unsigned long long)sysInfo.used_pages);
			view->_UpdateProgress(progress, status.String());

			// Update graph
			float fps = device->CurrentFPS();
			BMessage frameMsg(MSG_TEST_FRAME_TIMING);
			frameMsg.AddBool("dropped", device->FramesDropped() > 0);
			frameMsg.AddFloat("fps", fps);
			BMessenger(view).SendMessage(&frameMsg);
		}

		snooze(100000);
	}

	device->StopCapture();

	// Get final memory info
	system_info sysInfoEnd;
	get_system_info(&sysInfoEnd);

	result->duration = system_time() - startTime;
	result->iterations = checkpoints;

	int64 memoryDelta = (int64)sysInfoEnd.used_pages - (int64)usedPagesStart;
	int64 memoryDeltaKB = memoryDelta * B_PAGE_SIZE / 1024;

	// Consider it a pass if memory growth is < 10MB
	result->passed = (memoryDeltaKB < 10240);

	result->details.SetToFormat(
		"Test duration: %.1f seconds\n"
		"Frames captured: %u\n"
		"Memory at start: %llu KB\n"
		"Memory at end: %llu KB\n"
		"Memory delta: %+lld KB\n"
		"Peak memory: %llu KB\n"
		"%s",
		result->duration / 1000000.0,
		(unsigned)device->FramesCaptured(),
		(unsigned long long)(usedPagesStart * B_PAGE_SIZE / 1024),
		(unsigned long long)(sysInfoEnd.used_pages * B_PAGE_SIZE / 1024),
		(long long)memoryDeltaKB,
		(unsigned long long)(maxUsedPages * B_PAGE_SIZE / 1024),
		result->passed ? "No significant memory leak detected" :
			"WARNING: Possible memory leak detected!");

	BMessage msg(MSG_TEST_COMPLETE);
	msg.AddPointer("result", result);
	BMessenger(view).SendMessage(&msg);

	return 0;
}


// ============================================================================
// Diagnostic Report Generation
// ============================================================================

BString
DriverTestView::GenerateDiagnosticReport()
{
	BString report;

	// Header
	report << "========================================\n";
	report << "BubiCam Diagnostic Report\n";
	report << "========================================\n\n";

	// Timestamp
	time_t now = time(NULL);
	report << "Generated: " << ctime(&now);
	report << "\n";

	// System Info
	report << "--- System Information ---\n";
	system_info sysInfo;
	get_system_info(&sysInfo);

	report << "Haiku version: " << sysInfo.kernel_build_date << "\n";
	report << "CPU count: " << sysInfo.cpu_count << "\n";
	report << "Memory: " << (sysInfo.max_pages * B_PAGE_SIZE / 1024 / 1024) << " MB\n";
	report << "\n";

	// Device Info
	if (fDevice != NULL) {
		report << "--- Webcam Device ---\n";
		report << "Name: " << fDevice->Name() << "\n";
		report << "Vendor: " << fDevice->VendorName() << "\n";
		report << "Product: " << fDevice->ProductName() << "\n";
		report << "VID:PID: ";
		BString vidpid;
		vidpid.SetToFormat("%04x:%04x", fDevice->VendorID(), fDevice->ProductID());
		report << vidpid << "\n";
		report << "Driver: " << fDevice->DriverName() << "\n";
		report << "USB Version: " << fDevice->USBVersion() << "\n";
		report << "Node ID: " << fDevice->MediaNodeID() << "\n";
		report << "Node Instantiated: " << (fDevice->IsNodeInstantiated() ? "Yes" : "No") << "\n";
		report << "\n";

		// Supported formats
		report << "--- Supported Formats ---\n";
		const BObjectList<VideoFormat>& formats = fDevice->SupportedFormats();
		for (int32 i = 0; i < formats.CountItems(); i++) {
			VideoFormat* f = formats.ItemAt(i);
			if (f != NULL) {
				BString line;
				line.SetToFormat("  %dx%d @ %.1f fps (%s)\n",
					f->width, f->height, f->frameRate, f->colorSpace);
				report << line;
			}
		}
		report << "\n";

		// Driver warnings
		if (fDevice->HasDriverWarnings()) {
			report << "--- Driver Warnings ---\n";
			report << fDevice->GetDriverWarnings() << "\n\n";
		}
	} else {
		report << "--- No device selected ---\n\n";
	}

	// Test Results
	if (fResults.CountItems() > 0) {
		report << "--- Test Results ---\n";
		for (int32 i = 0; i < fResults.CountItems(); i++) {
			TestResult* r = fResults.ItemAt(i);
			if (r != NULL) {
				report << "\n[" << r->testName << "] ";
				report << (r->passed ? "PASSED" : "FAILED") << "\n";
				report << "Duration: " << (r->duration / 1000000.0) << " seconds\n";
				report << "Iterations: " << r->iterations << "\n";
				report << "Failures: " << r->failures << "\n";
				if (r->details.Length() > 0) {
					report << "Details:\n" << r->details << "\n";
				}
			}
		}
		report << "\n";
	}

	// Latency Stats
	{
		BAutolock lock(fTimingLock);
		if (fLatencySamples > 0) {
			report << "--- Latency Statistics ---\n";
			BString stats;
			stats.SetToFormat(
				"Samples: %u\n"
				"Min: %.2f ms\n"
				"Max: %.2f ms\n"
				"Avg: %.2f ms\n",
				(unsigned)fLatencySamples,
				fMinLatency / 1000.0,
				fMaxLatency / 1000.0,
				fAvgLatency / 1000.0);
			report << stats << "\n";
		}
	}

	// USB device list
	report << "--- USB Devices (listusb) ---\n";
	FILE* pipe = popen("listusb 2>&1", "r");
	if (pipe != NULL) {
		char buffer[256];
		while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
			report << buffer;
		}
		pclose(pipe);
	}
	report << "\n";

	// Recent syslog entries
	report << "--- Recent Syslog (USB/Media) ---\n";
	pipe = popen("tail -100 /var/log/syslog 2>&1 | grep -iE 'usb|video|media|webcam|uvc' | tail -30", "r");
	if (pipe != NULL) {
		char buffer[512];
		while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
			report << buffer;
		}
		pclose(pipe);
	}
	report << "\n";

	report << "========================================\n";
	report << "End of Report\n";
	report << "========================================\n";

	return report;
}
