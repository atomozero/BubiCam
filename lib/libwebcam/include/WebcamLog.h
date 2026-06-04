/*
 * WebcamKit - Webcam Library for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * WebcamLog.h - Logging utilities for the webcam library
 *
 * Usage:
 *   #define LOG_MODULE "MyClass"
 *   #include "WebcamLog.h"
 *
 *   LOG_DEBUG("Processing item %d", itemIndex);
 */

#ifndef WEBCAM_LOG_H
#define WEBCAM_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <GraphicsDefs.h>

// Log levels: 0 = All, 1 = Info+, 2 = Warning+, 3 = Error, 4 = None
#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

#ifndef LOG_MODULE
#define LOG_MODULE "WebcamKit"
#endif

#ifndef LOG_COLORS
#define LOG_COLORS_AUTO 1
#else
#define LOG_COLORS_AUTO 0
#endif

// ANSI colors
#define ANSI_RESET      "\033[0m"
#define ANSI_RED        "\033[0;31m"
#define ANSI_GREEN      "\033[0;32m"
#define ANSI_YELLOW     "\033[0;33m"
#define ANSI_CYAN       "\033[0;36m"
#define ANSI_BOLD_RED   "\033[1;31m"
#define ANSI_BOLD_CYAN  "\033[1;36m"
#define ANSI_DIM        "\033[2m"

#define LOG_COLOR_TRACE    ANSI_DIM
#define LOG_COLOR_DEBUG    ANSI_DIM
#define LOG_COLOR_INFO     ANSI_GREEN
#define LOG_COLOR_WARNING  ANSI_YELLOW
#define LOG_COLOR_ERROR    ANSI_BOLD_RED
#define LOG_COLOR_MODULE   ANSI_CYAN
#define LOG_COLOR_TIME     ANSI_DIM


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


inline void
_LogGetTimestamp(char* buffer, size_t size)
{
	time_t now = time(NULL);
	struct tm* tm_info = localtime(&now);
	strftime(buffer, size, "%H:%M:%S", tm_info);
}


inline void
_LogMessage(const char* level, const char* levelColor, const char* module,
	const char* fmt, ...)
{
	char timestamp[16];
	_LogGetTimestamp(timestamp, sizeof(timestamp));

	bool useColors = _LogColorsEnabled();

	if (useColors)
		fprintf(stderr, "%s[%s]%s ", LOG_COLOR_TIME, timestamp, ANSI_RESET);
	else
		fprintf(stderr, "[%s] ", timestamp);

	if (useColors)
		fprintf(stderr, "%s[%s]%s ", LOG_COLOR_MODULE, module, ANSI_RESET);
	else
		fprintf(stderr, "[%s] ", module);

	if (useColors)
		fprintf(stderr, "%s%s:%s ", levelColor, level, ANSI_RESET);
	else
		fprintf(stderr, "%s: ", level);

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}


#if LOG_LEVEL <= -1
#define LOG_TRACE(fmt, ...) \
	_LogMessage("TRACE", LOG_COLOR_TRACE, LOG_MODULE, fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(fmt, ...) ((void)0)
#endif

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


// Media Kit helper: compact colorspace name
inline const char*
ColorSpaceName(color_space cs)
{
	switch (cs) {
		case B_RGB32:     return "RGB32";
		case B_RGBA32:    return "RGBA32";
		case B_RGB24:     return "RGB24";
		case B_RGB16:     return "RGB16";
		case B_RGB15:     return "RGB15";
		case B_GRAY8:     return "GRAY8";
		case B_YCbCr422:  return "YUY2";
		case B_YUV422:    return "YUV422";
		case B_YCbCr420:  return "I420";
		case B_YUV420:    return "YUV420";
		case B_NO_COLOR_SPACE: return "NONE";
		default:          return "???";
	}
}


#endif // WEBCAM_LOG_H
