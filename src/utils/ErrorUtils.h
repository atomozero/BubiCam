/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * ErrorUtils - Centralized error handling and logging utilities
 *
 * This module standardizes error handling across the codebase by providing:
 * - Severity-based logging macros with ANSI colors (LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR)
 * - Timestamps for each log entry
 * - Consistent message formatting with module name and severity prefix
 * - Helper function for user-facing error alerts
 *
 * Usage:
 *   #define LOG_MODULE "MyClass"
 *   #include "ErrorUtils.h"
 *
 *   LOG_DEBUG("Processing item %d", itemIndex);
 *   LOG_ERROR("Failed to open file: %s", strerror(status));
 *   ShowErrorAlert("Cannot save file", "The disk may be full.");
 *
 * Output format (with colors):
 *   [12:34:56] [MyClass] DEBUG: Processing item 5
 *   [12:34:56] [MyClass] ERROR: Failed to open file: No such file
 */

#ifndef ERROR_UTILS_H
#define ERROR_UTILS_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <Alert.h>
#include <String.h>

// Log levels for filtering (can be set at compile time)
// 0 = All (DEBUG+), 1 = Info+, 2 = Warning+, 3 = Error only, 4 = None
#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

// Default module name if not defined before including this header
#ifndef LOG_MODULE
#define LOG_MODULE "BubiCam"
#endif

// Enable/disable colors (auto-detect TTY by default, or force with LOG_COLORS)
#ifndef LOG_COLORS
#define LOG_COLORS_AUTO 1
#else
#define LOG_COLORS_AUTO 0
#endif


// ============================================================================
// ANSI Color Codes
// ============================================================================

// Reset
#define ANSI_RESET      "\033[0m"

// Regular colors
#define ANSI_BLACK      "\033[0;30m"
#define ANSI_RED        "\033[0;31m"
#define ANSI_GREEN      "\033[0;32m"
#define ANSI_YELLOW     "\033[0;33m"
#define ANSI_BLUE       "\033[0;34m"
#define ANSI_MAGENTA    "\033[0;35m"
#define ANSI_CYAN       "\033[0;36m"
#define ANSI_WHITE      "\033[0;37m"

// Bold/bright colors
#define ANSI_BOLD_BLACK   "\033[1;30m"
#define ANSI_BOLD_RED     "\033[1;31m"
#define ANSI_BOLD_GREEN   "\033[1;32m"
#define ANSI_BOLD_YELLOW  "\033[1;33m"
#define ANSI_BOLD_BLUE    "\033[1;34m"
#define ANSI_BOLD_MAGENTA "\033[1;35m"
#define ANSI_BOLD_CYAN    "\033[1;36m"
#define ANSI_BOLD_WHITE   "\033[1;37m"

// Dim (for less important info)
#define ANSI_DIM        "\033[2m"

// Log level colors
#define LOG_COLOR_DEBUG    ANSI_DIM        // Gray/dim for debug
#define LOG_COLOR_INFO     ANSI_GREEN      // Green for info
#define LOG_COLOR_WARNING  ANSI_YELLOW     // Yellow for warnings
#define LOG_COLOR_ERROR    ANSI_BOLD_RED   // Bold red for errors
#define LOG_COLOR_MODULE   ANSI_CYAN       // Cyan for module name
#define LOG_COLOR_TIME     ANSI_DIM        // Dim for timestamp


// ============================================================================
// Helper Functions
// ============================================================================

// Check if stderr is a TTY (for color auto-detection)
inline bool
_LogColorsEnabled()
{
#if LOG_COLORS_AUTO
	static int cached = -1;
	if (cached == -1)
		cached = isatty(fileno(stderr)) ? 1 : 0;
	return cached == 1;
#else
	return LOG_COLORS;
#endif
}


// Get current time as HH:MM:SS string
inline void
_LogGetTimestamp(char* buffer, size_t size)
{
	time_t now = time(NULL);
	struct tm* tm_info = localtime(&now);
	strftime(buffer, size, "%H:%M:%S", tm_info);
}


// Core logging function with colors and timestamp
inline void
_LogMessage(const char* level, const char* levelColor, const char* module,
	const char* fmt, ...)
{
	char timestamp[16];
	_LogGetTimestamp(timestamp, sizeof(timestamp));

	bool useColors = _LogColorsEnabled();

	// Print timestamp
	if (useColors)
		fprintf(stderr, "%s[%s]%s ", LOG_COLOR_TIME, timestamp, ANSI_RESET);
	else
		fprintf(stderr, "[%s] ", timestamp);

	// Print module name
	if (useColors)
		fprintf(stderr, "%s[%s]%s ", LOG_COLOR_MODULE, module, ANSI_RESET);
	else
		fprintf(stderr, "[%s] ", module);

	// Print level
	if (useColors)
		fprintf(stderr, "%s%s:%s ", levelColor, level, ANSI_RESET);
	else
		fprintf(stderr, "%s: ", level);

	// Print message
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}


// ============================================================================
// Logging Macros
// ============================================================================

#if LOG_LEVEL <= 0
#define LOG_DEBUG(fmt, ...) \
	_LogMessage("DEBUG", LOG_COLOR_DEBUG, LOG_MODULE, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 1
#define LOG_INFO(fmt, ...) \
	_LogMessage("INFO", LOG_COLOR_INFO, LOG_MODULE, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 2
#define LOG_WARNING(fmt, ...) \
	_LogMessage("WARNING", LOG_COLOR_WARNING, LOG_MODULE, fmt, ##__VA_ARGS__)
#else
#define LOG_WARNING(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 3
#define LOG_ERROR(fmt, ...) \
	_LogMessage("ERROR", LOG_COLOR_ERROR, LOG_MODULE, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) ((void)0)
#endif


// ============================================================================
// Indented/Detail Logging (for hierarchical output)
// ============================================================================

// Log a detail line (indented, no timestamp/module prefix)
// Use for multi-line output or sub-items
inline void
_LogDetail(const char* fmt, ...)
{
	bool useColors = _LogColorsEnabled();

	// Indent to align with message after "[HH:MM:SS] [Module] LEVEL: "
	fprintf(stderr, "                              ");

	if (useColors)
		fprintf(stderr, "%s", ANSI_DIM);

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (useColors)
		fprintf(stderr, "%s", ANSI_RESET);

	fprintf(stderr, "\n");
}

#define LOG_DETAIL(fmt, ...) _LogDetail(fmt, ##__VA_ARGS__)


// ============================================================================
// User-Facing Alert Helpers
// ============================================================================

// Show user-facing error alert
inline void
ShowErrorAlert(const char* title, const char* message)
{
	BAlert* alert = new BAlert(title, message, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


// Show error alert with status_t code
inline void
ShowErrorAlertWithStatus(const char* title, const char* message, status_t status)
{
	BString fullMessage;
	fullMessage.SetToFormat("%s\n\nError: %s (0x%08x)",
		message, strerror(status), (unsigned int)status);
	ShowErrorAlert(title, fullMessage.String());
}


// Show warning alert (less severe than errors)
inline void
ShowWarningAlert(const char* title, const char* message)
{
	BAlert* alert = new BAlert(title, message, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


// Show confirmation dialog
// Returns true if user clicked the confirm button
inline bool
ShowConfirmationAlert(const char* title, const char* message,
	const char* cancelLabel = "Cancel", const char* confirmLabel = "OK")
{
	BAlert* alert = new BAlert(title, message, cancelLabel, confirmLabel, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


// ============================================================================
// Combined Logging + Alert Macros
// ============================================================================

// Log and show error to user in one call
#define LOG_AND_SHOW_ERROR(title, fmt, ...) \
	do { \
		LOG_ERROR(fmt, ##__VA_ARGS__); \
		BString _msg; \
		_msg.SetToFormat(fmt, ##__VA_ARGS__); \
		ShowErrorAlert(title, _msg.String()); \
	} while (0)


// Check status and log error if not B_OK, then return
#define CHECK_STATUS(status, fmt, ...) \
	do { \
		if ((status) != B_OK) { \
			LOG_ERROR(fmt ": %s (0x%x)", ##__VA_ARGS__, strerror(status), status); \
			return (status); \
		} \
	} while (0)


// Check status and log error, but continue execution
#define CHECK_STATUS_LOG(status, fmt, ...) \
	do { \
		if ((status) != B_OK) { \
			LOG_ERROR(fmt ": %s (0x%x)", ##__VA_ARGS__, strerror(status), status); \
		} \
	} while (0)


// ============================================================================
// Section Headers (for organizing output)
// ============================================================================

// Print a section header for visual separation
inline void
LogSection(const char* title)
{
	bool useColors = _LogColorsEnabled();

	if (useColors)
		fprintf(stderr, "\n%s=== %s ===%s\n", ANSI_BOLD_CYAN, title, ANSI_RESET);
	else
		fprintf(stderr, "\n=== %s ===\n", title);
}

#define LOG_SECTION(title) LogSection(title)


#endif // ERROR_UTILS_H
