/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * ErrorUtils - Centralized error handling and logging utilities
 *
 * This module standardizes error handling across the codebase by providing:
 * - Severity-based logging macros (LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR)
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
 */

#ifndef ERROR_UTILS_H
#define ERROR_UTILS_H

#include <stdio.h>
#include <Alert.h>
#include <String.h>

// Log levels for filtering (can be set at compile time)
// 0 = All, 1 = Info+, 2 = Warning+, 3 = Error only, 4 = None
#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

// Default module name if not defined before including this header
#ifndef LOG_MODULE
#define LOG_MODULE "BubiCam"
#endif


// Logging macros with severity levels
// Format: [MODULE] LEVEL: message

#if LOG_LEVEL <= 0
#define LOG_DEBUG(fmt, ...) \
	fprintf(stderr, "[%s] DEBUG: " fmt "\n", LOG_MODULE, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 1
#define LOG_INFO(fmt, ...) \
	fprintf(stderr, "[%s] INFO: " fmt "\n", LOG_MODULE, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 2
#define LOG_WARNING(fmt, ...) \
	fprintf(stderr, "[%s] WARNING: " fmt "\n", LOG_MODULE, ##__VA_ARGS__)
#else
#define LOG_WARNING(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= 3
#define LOG_ERROR(fmt, ...) \
	fprintf(stderr, "[%s] ERROR: " fmt "\n", LOG_MODULE, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) ((void)0)
#endif


// Helper function to show user-facing error alerts
// Use this for errors that the user needs to know about
inline void
ShowErrorAlert(const char* title, const char* message)
{
	BAlert* alert = new BAlert(title, message, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


// Helper function to show error alert with status_t code
// Formats the error message with the status code and description
inline void
ShowErrorAlertWithStatus(const char* title, const char* message, status_t status)
{
	BString fullMessage;
	fullMessage.SetToFormat("%s\n\nError: %s (0x%08x)",
		message, strerror(status), (unsigned int)status);
	ShowErrorAlert(title, fullMessage.String());
}


// Helper function to show warning alerts (less severe than errors)
inline void
ShowWarningAlert(const char* title, const char* message)
{
	BAlert* alert = new BAlert(title, message, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


// Helper function to show confirmation dialog
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


// Macro to log and show error to user in one call
// Use for errors that need both logging and user notification
#define LOG_AND_SHOW_ERROR(title, fmt, ...) \
	do { \
		LOG_ERROR(fmt, ##__VA_ARGS__); \
		BString _msg; \
		_msg.SetToFormat(fmt, ##__VA_ARGS__); \
		ShowErrorAlert(title, _msg.String()); \
	} while (0)


// Macro to check status and log error if not B_OK
// Returns from the current function with the status if error
#define CHECK_STATUS(status, fmt, ...) \
	do { \
		if ((status) != B_OK) { \
			LOG_ERROR(fmt ": %s (0x%x)", ##__VA_ARGS__, strerror(status), status); \
			return (status); \
		} \
	} while (0)


// Macro to check status and log error, but continue execution
// Use when you want to log but not return on error
#define CHECK_STATUS_LOG(status, fmt, ...) \
	do { \
		if ((status) != B_OK) { \
			LOG_ERROR(fmt ": %s (0x%x)", ##__VA_ARGS__, strerror(status), status); \
		} \
	} while (0)


#endif // ERROR_UTILS_H
