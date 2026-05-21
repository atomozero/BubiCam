/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VUMeterView.h"

#include <Autolock.h>
#include <stdio.h>
#include <math.h>


VUMeterView::VUMeterView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fLeftLevel(0.0f),
	fRightLevel(0.0f),
	fLeftPeak(0.0f),
	fRightPeak(0.0f),
	fLeftPeakTime(0),
	fRightPeakTime(0),
	fPeakHold(true),
	fLock("vu lock")
{
}


VUMeterView::~VUMeterView()
{
}


void
VUMeterView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
VUMeterView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	Invalidate();
}


void
VUMeterView::SetLevel(float left, float right)
{
	BAutolock lock(fLock);

	// Clamp values
	fLeftLevel = fmax(0.0f, fmin(1.0f, left));
	fRightLevel = fmax(0.0f, fmin(1.0f, right));

	bigtime_t now = system_time();

	// Update peak hold
	if (fLeftLevel >= fLeftPeak) {
		fLeftPeak = fLeftLevel;
		fLeftPeakTime = now;
	} else if (now - fLeftPeakTime > kPeakHoldTime) {
		fLeftPeak = fLeftLevel;
	}

	if (fRightLevel >= fRightPeak) {
		fRightPeak = fRightLevel;
		fRightPeakTime = now;
	} else if (now - fRightPeakTime > kPeakHoldTime) {
		fRightPeak = fRightLevel;
	}

	lock.Unlock();

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VUMeterView::Draw(BRect updateRect)
{
	BAutolock lock(fLock);

	BRect bounds = Bounds();
	float meterHeight = (bounds.Height() - 25) / 2;  // Space for labels and gap

	// Background
	SetHighColor(ViewColor());
	FillRect(bounds);

	// Draw scale at top
	BRect scaleRect(30, 0, bounds.right - 5, 15);
	_DrawScale(scaleRect);

	// Left channel
	BRect leftRect(30, 18, bounds.right - 5, 18 + meterHeight - 5);
	_DrawMeter(leftRect, fLeftLevel, fLeftPeak, "L");

	// Right channel
	BRect rightRect(30, 18 + meterHeight, bounds.right - 5,
		18 + meterHeight * 2 - 5);
	_DrawMeter(rightRect, fRightLevel, fRightPeak, "R");
}


void
VUMeterView::_DrawMeter(BRect rect, float level, float peak, const char* label)
{
	// Draw label
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	SetLowColor(ViewColor());
	BFont font(be_bold_font);
	font.SetSize(10);
	SetFont(&font);
	DrawString(label, BPoint(5, rect.top + rect.Height() / 2 + 4));

	// Background
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(tint_color(panel, B_DARKEN_MAX_TINT));
	FillRoundRect(rect, 2, 2);

	// Border
	SetHighColor(tint_color(panel, B_DARKEN_3_TINT));
	StrokeRoundRect(rect, 2, 2);

	// Calculate meter width
	float meterWidth = rect.Width() - 4;
	float levelWidth = meterWidth * level;

	if (levelWidth > 0) {
		// Draw gradient meter
		BRect meterRect(rect.left + 2, rect.top + 2,
			rect.left + 2 + levelWidth, rect.bottom - 2);

		// Draw segments
		float segmentWidth = meterWidth / 20;
		float x = rect.left + 2;

		for (int i = 0; i < 20 && x < meterRect.right; i++) {
			float segEnd = fmin(x + segmentWidth - 1, meterRect.right);
			float segLevel = (float)(i + 1) / 20.0f;

			rgb_color color = _ColorForLevel(segLevel);
			SetHighColor(color);
			FillRect(BRect(x, rect.top + 2, segEnd, rect.bottom - 2));

			x += segmentWidth;
		}
	}

	// Draw peak indicator
	if (fPeakHold && peak > 0.02f) {
		float peakX = rect.left + 2 + meterWidth * peak;
		SetHighColor(255, 255, 255);
		StrokeLine(BPoint(peakX, rect.top + 2), BPoint(peakX, rect.bottom - 2));
	}
}


void
VUMeterView::_DrawScale(BRect rect)
{
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
	BFont font(be_plain_font);
	font.SetSize(9);
	SetFont(&font);

	float meterWidth = rect.Width();

	// dB scale markers
	struct {
		float position;  // 0.0 to 1.0
		const char* label;
	} markers[] = {
		{0.0f, "-∞"},
		{0.3f, "-20"},
		{0.5f, "-12"},
		{0.7f, "-6"},
		{0.85f, "-3"},
		{1.0f, "0"}
	};

	for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
		float x = rect.left + meterWidth * markers[i].position;
		DrawString(markers[i].label, BPoint(x - StringWidth(markers[i].label) / 2,
			rect.bottom));
	}
}


rgb_color
VUMeterView::_ColorForLevel(float level)
{
	if (level < 0.7f) {
		// Green zone (safe)
		int green = 180 + (int)(75 * level / 0.7f);
		return make_color(0, green, 0);
	} else if (level < 0.85f) {
		// Yellow zone (caution)
		float t = (level - 0.7f) / 0.15f;
		return make_color((int)(255 * t), 220, 0);
	} else {
		// Red zone (clipping)
		float t = (level - 0.85f) / 0.15f;
		return make_color(255, (int)(200 * (1 - t)), 0);
	}
}
