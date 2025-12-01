/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoPreviewView.h"

#include <Autolock.h>
#include <stdio.h>
#include <string.h>


VideoPreviewView::VideoPreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fCurrentFrame(NULL),
	fFrameLock("frame lock"),
	fCurrentFPS(0.0f),
	fFramesReceived(0),
	fFramesDropped(0),
	fVideoWidth(0),
	fVideoHeight(0),
	fShowStats(true)
{
	fBackgroundColor = make_color(40, 40, 40);
	SetViewColor(fBackgroundColor);
	SetLowColor(fBackgroundColor);
}


VideoPreviewView::~VideoPreviewView()
{
	BAutolock lock(fFrameLock);
	delete fCurrentFrame;
}


void
VideoPreviewView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_CalculateVideoRect();
}


void
VideoPreviewView::Draw(BRect updateRect)
{
	BAutolock lock(fFrameLock);

	BRect bounds = Bounds();

	if (fCurrentFrame != NULL && fCurrentFrame->IsValid()) {
		// Draw the video frame scaled to fit
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fCurrentFrame, fCurrentFrame->Bounds(), fVideoRect);

		// Draw black bars if needed
		SetHighColor(fBackgroundColor);

		if (fVideoRect.left > 0) {
			FillRect(BRect(0, 0, fVideoRect.left - 1, bounds.bottom));
			FillRect(BRect(fVideoRect.right + 1, 0, bounds.right, bounds.bottom));
		}
		if (fVideoRect.top > 0) {
			FillRect(BRect(0, 0, bounds.right, fVideoRect.top - 1));
			FillRect(BRect(0, fVideoRect.bottom + 1, bounds.right, bounds.bottom));
		}

		// Draw stats overlay
		if (fShowStats)
			_DrawStats();
	} else {
		// No frame - draw placeholder
		SetHighColor(fBackgroundColor);
		FillRect(updateRect);

		SetHighColor(128, 128, 128);
		SetDrawingMode(B_OP_OVER);

		BFont font;
		GetFont(&font);
		font.SetSize(14);
		SetFont(&font);

		const char* text = "No video signal";
		float textWidth = StringWidth(text);
		BPoint textPos(
			(bounds.Width() - textWidth) / 2,
			(bounds.Height() + font.Size()) / 2
		);
		DrawString(text, textPos);

		// Draw camera icon placeholder
		BRect iconRect(
			bounds.Width() / 2 - 30,
			bounds.Height() / 2 - 50,
			bounds.Width() / 2 + 30,
			bounds.Height() / 2 - 10
		);
		SetHighColor(80, 80, 80);
		StrokeRoundRect(iconRect, 5, 5);

		// Lens
		BPoint center(bounds.Width() / 2, bounds.Height() / 2 - 30);
		StrokeEllipse(center, 15, 15);
	}
}


void
VideoPreviewView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	_CalculateVideoRect();
	Invalidate();
}


void
VideoPreviewView::SetFrame(BBitmap* bitmap)
{
	if (bitmap == NULL)
		return;

	BAutolock lock(fFrameLock);

	// Clone the bitmap if dimensions differ or no current frame
	bool sizeChanged = false;
	if (fCurrentFrame == NULL ||
		fCurrentFrame->Bounds() != bitmap->Bounds() ||
		fCurrentFrame->ColorSpace() != bitmap->ColorSpace()) {
		delete fCurrentFrame;
		fCurrentFrame = new BBitmap(bitmap->Bounds(), bitmap->ColorSpace());
		sizeChanged = true;
	}

	// Copy bitmap data
	memcpy(fCurrentFrame->Bits(), bitmap->Bits(), bitmap->BitsLength());

	// Get video dimensions for sizing
	float videoWidth = bitmap->Bounds().Width() + 1;
	float videoHeight = bitmap->Bounds().Height() + 1;

	lock.Unlock();

	if (LockLooper()) {
		// Don't change layout when video arrives - keep window stable
		_CalculateVideoRect();
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::ClearFrame()
{
	BAutolock lock(fFrameLock);
	delete fCurrentFrame;
	fCurrentFrame = NULL;

	lock.Unlock();

	if (LockLooper()) {
		// Just redraw, don't change layout
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::_CalculateVideoRect()
{
	BRect bounds = Bounds();
	float viewWidth = bounds.Width();
	float viewHeight = bounds.Height();

	float videoWidth = 640;  // Default
	float videoHeight = 480;

	BAutolock lock(fFrameLock);
	if (fCurrentFrame != NULL) {
		videoWidth = fCurrentFrame->Bounds().Width() + 1;
		videoHeight = fCurrentFrame->Bounds().Height() + 1;
	}
	lock.Unlock();

	// Calculate aspect ratio preserving rect
	float aspectRatio = videoWidth / videoHeight;
	float viewAspect = viewWidth / viewHeight;

	float destWidth, destHeight;
	if (viewAspect > aspectRatio) {
		// View is wider - fit to height
		destHeight = viewHeight;
		destWidth = viewHeight * aspectRatio;
	} else {
		// View is taller - fit to width
		destWidth = viewWidth;
		destHeight = viewWidth / aspectRatio;
	}

	float x = (viewWidth - destWidth) / 2;
	float y = (viewHeight - destHeight) / 2;

	fVideoRect = BRect(x, y, x + destWidth - 1, y + destHeight - 1);
}


void
VideoPreviewView::UpdateStats(float fps, uint32 received, uint32 dropped)
{
	fCurrentFPS = fps;
	fFramesReceived = received;
	fFramesDropped = dropped;
}


void
VideoPreviewView::SetResolution(int32 width, int32 height)
{
	fVideoWidth = width;
	fVideoHeight = height;
}


void
VideoPreviewView::SetShowStats(bool show)
{
	fShowStats = show;
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::_DrawStats()
{
	// Draw semi-transparent background for stats
	BRect bounds = Bounds();
	BRect statsRect(bounds.left + 5, bounds.top + 5,
		bounds.left + 180, bounds.top + 75);

	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(0, 0, 0, 160);
	FillRoundRect(statsRect, 5, 5);

	// Draw stats text
	SetHighColor(255, 255, 255);
	BFont font(be_plain_font);
	font.SetSize(11);
	SetFont(&font);

	float y = statsRect.top + 15;
	float x = statsRect.left + 8;

	// Resolution
	BString resStr;
	if (fVideoWidth > 0 && fVideoHeight > 0)
		resStr.SetToFormat("%dx%d", (int)fVideoWidth, (int)fVideoHeight);
	else
		resStr = "N/A";
	DrawString("Resolution:", BPoint(x, y));
	DrawString(resStr.String(), BPoint(x + 75, y));

	// FPS
	y += 15;
	BString fpsStr;
	fpsStr.SetToFormat("%.1f", fCurrentFPS);
	DrawString("FPS:", BPoint(x, y));
	// Color code FPS
	if (fCurrentFPS >= 25.0f)
		SetHighColor(0, 255, 0);  // Green
	else if (fCurrentFPS >= 15.0f)
		SetHighColor(255, 255, 0);  // Yellow
	else
		SetHighColor(255, 100, 100);  // Red
	DrawString(fpsStr.String(), BPoint(x + 75, y));

	// Frames received
	SetHighColor(255, 255, 255);
	y += 15;
	BString recvStr;
	recvStr.SetToFormat("%u", (unsigned)fFramesReceived);
	DrawString("Received:", BPoint(x, y));
	DrawString(recvStr.String(), BPoint(x + 75, y));

	// Frames dropped
	y += 15;
	BString dropStr;
	dropStr.SetToFormat("%u", (unsigned)fFramesDropped);
	DrawString("Dropped:", BPoint(x, y));
	if (fFramesDropped > 0)
		SetHighColor(255, 100, 100);  // Red for drops
	DrawString(dropStr.String(), BPoint(x + 75, y));

	SetDrawingMode(B_OP_COPY);
}
