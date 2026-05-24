/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "IconUtils.h"

#include <Application.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Roster.h>

#include <stdio.h>


BBitmap*
IconUtils::_LoadHVIFIcon(int32 resourceID, int32 size)
{
	BResources* resources = BApplication::AppResources();
	if (resources == NULL)
		return NULL;

	size_t dataSize;
	const void* data = resources->LoadResource('VICN', resourceID, &dataSize);
	if (data == NULL || dataSize == 0)
		return NULL;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (bitmap == NULL || !bitmap->IsValid()) {
		delete bitmap;
		return NULL;
	}

	status_t status = BIconUtils::GetVectorIcon(
		static_cast<const uint8*>(data), dataSize, bitmap);
	if (status != B_OK) {
		delete bitmap;
		return NULL;
	}

	return bitmap;
}


BBitmap*
IconUtils::CreateRefreshIcon(int32 size)
{
	return _LoadHVIFIcon(kIconReload, size);
}


BBitmap*
IconUtils::CreateStartIcon(int32 size)
{
	return _LoadHVIFIcon(kIconPlay, size);
}


BBitmap*
IconUtils::CreateStopIcon(int32 size)
{
	return _LoadHVIFIcon(kIconStop, size);
}


BBitmap*
IconUtils::CreateScreenshotIcon(int32 size)
{
	return _LoadHVIFIcon(kIconScreenshot, size);
}


BBitmap*
IconUtils::CreateRecordIcon(int32 size)
{
	return _LoadHVIFIcon(kIconRecord, size);
}


BBitmap*
IconUtils::CreateRecordStopIcon(int32 size)
{
	// Use stop icon for record-stop as well
	return _LoadHVIFIcon(kIconStop, size);
}
