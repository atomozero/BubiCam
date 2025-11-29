/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoPreviewView.h"

#include <stdio.h>
#include <string.h>


VideoPreviewView::VideoPreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fCurrentFrame(NULL),
	fFrameLock("frame lock")
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

	if (fCurrentFrame != NULL && fCurrentFrame->IsValid()) {
		// Draw the video frame scaled to fit
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fCurrentFrame, fCurrentFrame->Bounds(), fVideoRect);

		// Draw black bars if needed
		BRect bounds = Bounds();
		SetHighColor(fBackgroundColor);

		if (fVideoRect.left > 0) {
			FillRect(BRect(0, 0, fVideoRect.left - 1, bounds.bottom));
			FillRect(BRect(fVideoRect.right + 1, 0, bounds.right, bounds.bottom));
		}
		if (fVideoRect.top > 0) {
			FillRect(BRect(0, 0, bounds.right, fVideoRect.top - 1));
			FillRect(BRect(0, fVideoRect.bottom + 1, bounds.right, bounds.bottom));
		}
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
		BRect bounds = Bounds();
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
	if (fCurrentFrame == NULL ||
		fCurrentFrame->Bounds() != bitmap->Bounds() ||
		fCurrentFrame->ColorSpace() != bitmap->ColorSpace()) {
		delete fCurrentFrame;
		fCurrentFrame = new BBitmap(bitmap->Bounds(), bitmap->ColorSpace());
		_CalculateVideoRect();
	}

	// Copy bitmap data
	memcpy(fCurrentFrame->Bits(), bitmap->Bits(), bitmap->BitsLength());

	lock.Unlock();

	if (LockLooper()) {
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
