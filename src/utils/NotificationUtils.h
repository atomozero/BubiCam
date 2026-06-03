/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * NotificationUtils - System notification helpers.
 */

#ifndef NOTIFICATION_UTILS_H
#define NOTIFICATION_UTILS_H

#include <Notification.h>
#include <String.h>

class NotificationUtils {
public:
	static void		Info(const char* title, const char* message);
	static void		Warning(const char* title, const char* message);
	static void		Error(const char* title, const char* message);
	static void		Progress(const char* title, const char* message,
						float progress);

private:
	static void		_Send(notification_type type, const char* title,
						const char* message, float progress = -1.0f);
};

#endif // NOTIFICATION_UTILS_H
