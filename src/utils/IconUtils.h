/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef ICON_UTILS_H
#define ICON_UTILS_H

#include <Bitmap.h>

class IconUtils {
public:
	static BBitmap*	CreateRefreshIcon(int32 size = 24);
	static BBitmap*	CreateStartIcon(int32 size = 24);
	static BBitmap*	CreateStopIcon(int32 size = 24);
	static BBitmap*	CreateScreenshotIcon(int32 size = 24);
	static BBitmap*	CreateRecordIcon(int32 size = 24);
	static BBitmap*	CreateRecordStopIcon(int32 size = 24);

private:
	static void		_SetPixel(BBitmap* bitmap, int x, int y, uint8 r, uint8 g, uint8 b, uint8 a = 255);
	static void		_DrawCircle(BBitmap* bitmap, int cx, int cy, int r, uint8 red, uint8 green, uint8 blue);
	static void		_FillCircle(BBitmap* bitmap, int cx, int cy, int r, uint8 red, uint8 green, uint8 blue);
	static void		_DrawLine(BBitmap* bitmap, int x1, int y1, int x2, int y2, uint8 r, uint8 g, uint8 b);
	static void		_FillRect(BBitmap* bitmap, int x1, int y1, int x2, int y2, uint8 r, uint8 g, uint8 b);
	static void		_FillTriangle(BBitmap* bitmap, int x1, int y1, int x2, int y2, int x3, int y3, uint8 r, uint8 g, uint8 b);
};

#endif // ICON_UTILS_H
