/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "LEDView.h"

#include <String.h>
#include <Font.h>
#include <LayoutUtils.h>

static const uint32 kMsgBlink = '_blk';


LEDView::LEDView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fState(LED_OFF),
	fBlinking(false),
	fBlinkOn(true),
	fBlinkRunner(NULL),
	fLabel("CAM")
{
	SetExplicitMinSize(BSize(42, 16));
	SetExplicitMaxSize(BSize(42, 16));
}


LEDView::~LEDView()
{
	delete fBlinkRunner;
}


void
LEDView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
LEDView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	// LED circle (left side)
	float ledRadius = 5;
	BPoint ledCenter(ledRadius + 2, bounds.Height() / 2);

	// Determine color
	rgb_color ledColor;
	bool showLit = (!fBlinking || fBlinkOn);

	if (!showLit || fState == LED_OFF) {
		// Dark/off state
		ledColor = (rgb_color){60, 60, 60, 255};
	} else {
		switch (fState) {
			case LED_GREEN:
				ledColor = (rgb_color){0, 220, 0, 255};
				break;
			case LED_YELLOW:
				ledColor = (rgb_color){240, 200, 0, 255};
				break;
			case LED_RED:
				ledColor = (rgb_color){220, 0, 0, 255};
				break;
			default:
				ledColor = (rgb_color){60, 60, 60, 255};
				break;
		}
	}

	// Draw glow effect when lit
	if (showLit && fState != LED_OFF) {
		SetDrawingMode(B_OP_ALPHA);
		rgb_color glow = ledColor;
		glow.alpha = 60;
		SetHighColor(glow);
		FillEllipse(ledCenter, ledRadius + 2, ledRadius + 2);
		SetDrawingMode(B_OP_COPY);
	}

	// Draw LED circle
	SetHighColor(ledColor);
	FillEllipse(ledCenter, ledRadius, ledRadius);

	// Border
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
	StrokeEllipse(ledCenter, ledRadius, ledRadius);

	// Highlight
	if (showLit && fState != LED_OFF) {
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(255, 255, 255, 80);
		FillEllipse(BPoint(ledCenter.x - 1, ledCenter.y - 2), 2, 2);
		SetDrawingMode(B_OP_COPY);
	}

	// Label text (right of LED)
	BFont font(be_plain_font);
	font.SetSize(9);
	SetFont(&font);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	DrawString(fLabel.String(), BPoint(ledRadius * 2 + 6, bounds.Height() / 2 + 3));
}


void
LEDView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBlink:
			fBlinkOn = !fBlinkOn;
			Invalidate();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
LEDView::SetState(led_state state)
{
	if (fState == state)
		return;

	fState = state;

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
LEDView::SetBlinking(bool blink)
{
	if (fBlinking == blink)
		return;

	fBlinking = blink;
	fBlinkOn = true;

	delete fBlinkRunner;
	fBlinkRunner = NULL;

	if (blink) {
		BMessage msg(kMsgBlink);
		fBlinkRunner = new BMessageRunner(BMessenger(this), &msg, 500000);
	}

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
LEDView::SetLabel(const char* label)
{
	fLabel = label;
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}
