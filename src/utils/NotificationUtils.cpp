/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "NotificationUtils.h"

#include <Notification.h>


static const char* kNotificationID = "application/x-vnd.BubiCam";


void
NotificationUtils::Info(const char* title, const char* message)
{
	_Send(B_INFORMATION_NOTIFICATION, title, message);
}


void
NotificationUtils::Warning(const char* title, const char* message)
{
	_Send(B_IMPORTANT_NOTIFICATION, title, message);
}


void
NotificationUtils::Error(const char* title, const char* message)
{
	_Send(B_ERROR_NOTIFICATION, title, message);
}


void
NotificationUtils::Progress(const char* title, const char* message,
	float progress)
{
	_Send(B_PROGRESS_NOTIFICATION, title, message, progress);
}


void
NotificationUtils::_Send(notification_type type, const char* title,
	const char* message, float progress)
{
	BNotification notification(type);
	notification.SetGroup("BubiCam");
	notification.SetTitle(title);
	notification.SetContent(message);
	notification.SetMessageID(kNotificationID);

	if (type == B_PROGRESS_NOTIFICATION && progress >= 0.0f)
		notification.SetProgress(progress);

	notification.Send();
}
