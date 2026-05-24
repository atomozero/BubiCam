/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef ICON_UTILS_H
#define ICON_UTILS_H

#include <Bitmap.h>

// Resource IDs for toolbar icons
enum {
	kIconReload		= 201,
	kIconPlay		= 202,
	kIconStop		= 203,
	kIconScreenshot	= 204,
	kIconRecord		= 205
};

class IconUtils {
public:
	static BBitmap*	CreateRefreshIcon(int32 size = 24);
	static BBitmap*	CreateStartIcon(int32 size = 24);
	static BBitmap*	CreateStopIcon(int32 size = 24);
	static BBitmap*	CreateScreenshotIcon(int32 size = 24);
	static BBitmap*	CreateRecordIcon(int32 size = 24);
	static BBitmap*	CreateRecordStopIcon(int32 size = 24);

private:
	static BBitmap*	_LoadHVIFIcon(int32 resourceID, int32 size);
};

#endif // ICON_UTILS_H
