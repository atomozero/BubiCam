/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef EXPORT_UTILS_H
#define EXPORT_UTILS_H

#include <Bitmap.h>
#include <String.h>
#include <Path.h>
#include <ObjectList.h>

class WebcamDevice;
struct TestResult;

class ExportUtils {
public:
	// Screenshot functions
	static status_t		SaveScreenshot(BBitmap* bitmap, const char* path,
							uint32 format = 'PNG ');
	static BString		GenerateScreenshotFilename();
	static BPath		GetScreenshotDirectory();

	// Driver info export
	static status_t		ExportDriverInfo(WebcamDevice* device,
							const char* path);
	static status_t		ExportDriverInfoAsJSON(WebcamDevice* device,
							const char* path);
	static status_t		ExportDriverInfoAsText(WebcamDevice* device,
							const char* path);

	// Test results export
	static status_t		ExportTestResultsAsCSV(
								const BObjectList<TestResult>& results,
								const char* deviceName, const char* path);
	static status_t		ExportTestResultsAsJSON(
								const BObjectList<TestResult>& results,
								const char* deviceName, const char* path);

	// Utility
	static BString		GetTimestamp();

private:
	static status_t		_WriteToFile(const char* path, const BString& content);
};

#endif // EXPORT_UTILS_H
