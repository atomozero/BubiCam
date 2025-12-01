/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "IconUtils.h"
#include <string.h>
#include <math.h>


void
IconUtils::_SetPixel(BBitmap* bitmap, int x, int y, uint8 r, uint8 g, uint8 b, uint8 a)
{
	if (x < 0 || y < 0 || x >= bitmap->Bounds().Width() + 1 || y >= bitmap->Bounds().Height() + 1)
		return;

	uint8* bits = (uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	uint8* pixel = bits + y * bpr + x * 4;
	pixel[0] = b;
	pixel[1] = g;
	pixel[2] = r;
	pixel[3] = a;
}


void
IconUtils::_FillRect(BBitmap* bitmap, int x1, int y1, int x2, int y2, uint8 r, uint8 g, uint8 b)
{
	for (int y = y1; y <= y2; y++) {
		for (int x = x1; x <= x2; x++) {
			_SetPixel(bitmap, x, y, r, g, b);
		}
	}
}


void
IconUtils::_DrawLine(BBitmap* bitmap, int x1, int y1, int x2, int y2, uint8 r, uint8 g, uint8 b)
{
	int dx = abs(x2 - x1);
	int dy = abs(y2 - y1);
	int sx = x1 < x2 ? 1 : -1;
	int sy = y1 < y2 ? 1 : -1;
	int err = dx - dy;

	while (true) {
		_SetPixel(bitmap, x1, y1, r, g, b);
		if (x1 == x2 && y1 == y2) break;
		int e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x1 += sx; }
		if (e2 < dx) { err += dx; y1 += sy; }
	}
}


void
IconUtils::_DrawCircle(BBitmap* bitmap, int cx, int cy, int radius, uint8 r, uint8 g, uint8 b)
{
	int x = radius;
	int y = 0;
	int err = 0;

	while (x >= y) {
		_SetPixel(bitmap, cx + x, cy + y, r, g, b);
		_SetPixel(bitmap, cx + y, cy + x, r, g, b);
		_SetPixel(bitmap, cx - y, cy + x, r, g, b);
		_SetPixel(bitmap, cx - x, cy + y, r, g, b);
		_SetPixel(bitmap, cx - x, cy - y, r, g, b);
		_SetPixel(bitmap, cx - y, cy - x, r, g, b);
		_SetPixel(bitmap, cx + y, cy - x, r, g, b);
		_SetPixel(bitmap, cx + x, cy - y, r, g, b);

		y++;
		if (err <= 0) {
			err += 2 * y + 1;
		}
		if (err > 0) {
			x--;
			err -= 2 * x + 1;
		}
	}
}


void
IconUtils::_FillCircle(BBitmap* bitmap, int cx, int cy, int radius, uint8 r, uint8 g, uint8 b)
{
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			if (x * x + y * y <= radius * radius) {
				_SetPixel(bitmap, cx + x, cy + y, r, g, b);
			}
		}
	}
}


void
IconUtils::_FillTriangle(BBitmap* bitmap, int x1, int y1, int x2, int y2, int x3, int y3, uint8 r, uint8 g, uint8 b)
{
	// Simple scanline fill
	int minY = fmin(y1, fmin(y2, y3));
	int maxY = fmax(y1, fmax(y2, y3));

	for (int y = minY; y <= maxY; y++) {
		int minX = 9999, maxX = -9999;

		// Check each edge
		auto checkEdge = [&](int ax, int ay, int bx, int by) {
			if ((ay <= y && by > y) || (by <= y && ay > y)) {
				int x = ax + (y - ay) * (bx - ax) / (by - ay);
				minX = fmin(minX, x);
				maxX = fmax(maxX, x);
			}
		};

		checkEdge(x1, y1, x2, y2);
		checkEdge(x2, y2, x3, y3);
		checkEdge(x3, y3, x1, y1);

		for (int x = minX; x <= maxX; x++) {
			_SetPixel(bitmap, x, y, r, g, b);
		}
	}
}


BBitmap*
IconUtils::CreateRefreshIcon(int32 size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	int cx = size / 2;
	int cy = size / 2;
	int r = size / 2 - 3;

	// Draw circular arrow (green)
	// Outer arc
	for (int i = 0; i < 270; i++) {
		float angle = i * M_PI / 180.0f;
		int x = cx + (int)(r * cos(angle));
		int y = cy + (int)(r * sin(angle));
		_SetPixel(bitmap, x, y, 0, 160, 0);
		// Make it thicker
		x = cx + (int)((r - 1) * cos(angle));
		y = cy + (int)((r - 1) * sin(angle));
		_SetPixel(bitmap, x, y, 0, 160, 0);
	}

	// Arrow head at end of arc
	int ax = cx + r;
	int ay = cy;
	_FillTriangle(bitmap, ax, ay - 4, ax, ay + 4, ax + 5, ay, 0, 160, 0);

	return bitmap;
}


BBitmap*
IconUtils::CreateStartIcon(int32 size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	// Play triangle (green)
	int margin = 4;
	int x1 = margin + 2;
	int y1 = margin;
	int x2 = margin + 2;
	int y2 = size - margin - 1;
	int x3 = size - margin - 1;
	int y3 = size / 2;

	_FillTriangle(bitmap, x1, y1, x2, y2, x3, y3, 0, 180, 0);

	return bitmap;
}


BBitmap*
IconUtils::CreateStopIcon(int32 size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	// Stop square (red)
	int margin = 5;
	_FillRect(bitmap, margin, margin, size - margin - 1, size - margin - 1, 200, 60, 60);

	return bitmap;
}


BBitmap*
IconUtils::CreateScreenshotIcon(int32 size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	// Camera body (dark gray)
	int margin = 3;
	_FillRect(bitmap, margin, margin + 4, size - margin - 1, size - margin - 1, 80, 80, 80);

	// Camera top bump
	_FillRect(bitmap, size/2 - 4, margin, size/2 + 4, margin + 4, 80, 80, 80);

	// Lens (blue circle)
	int cx = size / 2;
	int cy = size / 2 + 2;
	_FillCircle(bitmap, cx, cy, 5, 100, 150, 220);
	_FillCircle(bitmap, cx, cy, 3, 60, 100, 180);

	return bitmap;
}
