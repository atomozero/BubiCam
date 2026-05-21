/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoPreviewView.h"

#include <Autolock.h>
#include <InterfaceDefs.h>
#include <stdio.h>
#include <string.h>


VideoPreviewView::VideoPreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fCurrentFrame(NULL),
	fReferenceFrame(NULL),
	fCompareMode(false),
	fFrameLock("frame lock"),
	fCurrentFPS(0.0f),
	fFramesReceived(0),
	fFramesDropped(0),
	fVideoWidth(0),
	fVideoHeight(0),
	fShowStats(true),
	fShowHistogram(false),
	fZoomLevel(1.0f),
	fPanOffset(0, 0),
	fLastMousePos(0, 0),
	fIsPanning(false),
	fHistogramDirty(true)
{
	memset(fHistR, 0, sizeof(fHistR));
	memset(fHistG, 0, sizeof(fHistG));
	memset(fHistB, 0, sizeof(fHistB));
	// Use a slightly darker shade of the panel background for the video area
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	fBackgroundColor = tint_color(panel, B_DARKEN_4_TINT);
	SetViewColor(fBackgroundColor);
	SetLowColor(fBackgroundColor);
}


VideoPreviewView::~VideoPreviewView()
{
	BAutolock lock(fFrameLock);
	delete fCurrentFrame;
	delete fReferenceFrame;
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
		if (fCompareMode && fReferenceFrame != NULL) {
			_DrawCompareMode();
		} else {
			// Apply zoom: scale the video rect around its center
			BRect destRect = fVideoRect;
			if (fZoomLevel > 1.0f) {
				float cx = (destRect.left + destRect.right) / 2 + fPanOffset.x;
				float cy = (destRect.top + destRect.bottom) / 2 + fPanOffset.y;
				float hw = destRect.Width() * fZoomLevel / 2;
				float hh = destRect.Height() * fZoomLevel / 2;
				destRect.Set(cx - hw, cy - hh, cx + hw, cy + hh);
			}

			SetDrawingMode(B_OP_COPY);
			DrawBitmap(fCurrentFrame, fCurrentFrame->Bounds(), destRect);

			// Draw background around the frame
			SetHighColor(fBackgroundColor);
			if (destRect.left > 0)
				FillRect(BRect(0, 0, destRect.left - 1, bounds.bottom));
			if (destRect.right < bounds.right)
				FillRect(BRect(destRect.right + 1, 0, bounds.right, bounds.bottom));
			if (destRect.top > 0)
				FillRect(BRect(destRect.left, 0, destRect.right, destRect.top - 1));
			if (destRect.bottom < bounds.bottom)
				FillRect(BRect(destRect.left, destRect.bottom + 1, destRect.right, bounds.bottom));
		}

		// Draw stats overlay
		if (fShowStats)
			_DrawStats();

		// Draw histogram overlay
		if (fShowHistogram)
			_DrawHistogram();
	} else {
		// No frame - draw placeholder
		SetHighColor(fBackgroundColor);
		FillRect(updateRect);

		SetHighColor(tint_color(fBackgroundColor, B_LIGHTEN_2_TINT));
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
		SetHighColor(tint_color(fBackgroundColor, B_LIGHTEN_1_TINT));
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
VideoPreviewView::MouseWheelChanged(BMessage* message)
{
	float deltaY = 0;
	if (message->FindFloat("be:wheel_delta_y", &deltaY) != B_OK)
		return;

	float oldZoom = fZoomLevel;
	fZoomLevel -= deltaY * 0.25f;
	if (fZoomLevel < 1.0f)
		fZoomLevel = 1.0f;
	if (fZoomLevel > 8.0f)
		fZoomLevel = 8.0f;

	// Reset pan when zooming back to 1x
	if (fZoomLevel == 1.0f)
		fPanOffset.Set(0, 0);

	if (fZoomLevel != oldZoom)
		Invalidate();
}


void
VideoPreviewView::MouseDown(BPoint where)
{
	uint32 buttons;
	GetMouse(&where, &buttons);

	if (buttons & B_PRIMARY_MOUSE_BUTTON && fZoomLevel > 1.0f) {
		fIsPanning = true;
		fLastMousePos = where;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}
}


void
VideoPreviewView::MouseMoved(BPoint where, uint32 transit, const BMessage* msg)
{
	if (fIsPanning) {
		if (transit == B_EXITED_VIEW || !(modifiers() & B_PRIMARY_MOUSE_BUTTON)) {
			fIsPanning = false;
			return;
		}

		fPanOffset.x += where.x - fLastMousePos.x;
		fPanOffset.y += where.y - fLastMousePos.y;
		fLastMousePos = where;
		Invalidate();
	}
}


void
VideoPreviewView::SetShowHistogram(bool show)
{
	fShowHistogram = show;
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::ResetZoom()
{
	fZoomLevel = 1.0f;
	fPanOffset.Set(0, 0);
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::CaptureReference()
{
	BAutolock lock(fFrameLock);

	if (fCurrentFrame == NULL || !fCurrentFrame->IsValid())
		return;

	delete fReferenceFrame;
	fReferenceFrame = new BBitmap(fCurrentFrame->Bounds(),
		fCurrentFrame->ColorSpace());
	if (fReferenceFrame != NULL && fReferenceFrame->IsValid() &&
		fReferenceFrame->BitsLength() >= fCurrentFrame->BitsLength()) {
		memcpy(fReferenceFrame->Bits(), fCurrentFrame->Bits(),
			fCurrentFrame->BitsLength());
	} else {
		delete fReferenceFrame;
		fReferenceFrame = NULL;
	}
}


void
VideoPreviewView::ClearReference()
{
	BAutolock lock(fFrameLock);
	delete fReferenceFrame;
	fReferenceFrame = NULL;
	fCompareMode = false;

	lock.Unlock();

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::SetCompareMode(bool enabled)
{
	fCompareMode = enabled;
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
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
		if (fCurrentFrame == NULL || !fCurrentFrame->IsValid()) {
			delete fCurrentFrame;
			fCurrentFrame = NULL;
			fprintf(stderr, "VideoPreviewView: Failed to allocate frame bitmap\n");
			return;
		}
	}

	// Copy bitmap data - verify buffer sizes match before copying
	if (fCurrentFrame != NULL && fCurrentFrame->IsValid() &&
		fCurrentFrame->BitsLength() >= bitmap->BitsLength()) {
		memcpy(fCurrentFrame->Bits(), bitmap->Bits(), bitmap->BitsLength());
		fHistogramDirty = true;
	}

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


void
VideoPreviewView::_ComputeHistogram()
{
	if (fCurrentFrame == NULL || !fCurrentFrame->IsValid())
		return;

	memset(fHistR, 0, sizeof(fHistR));
	memset(fHistG, 0, sizeof(fHistG));
	memset(fHistB, 0, sizeof(fHistB));

	const uint8* bits = static_cast<const uint8*>(fCurrentFrame->Bits());
	int32 width = (int32)(fCurrentFrame->Bounds().Width() + 1);
	int32 height = (int32)(fCurrentFrame->Bounds().Height() + 1);
	int32 bpr = fCurrentFrame->BytesPerRow();

	// Sample every 2nd pixel for speed
	for (int32 y = 0; y < height; y += 2) {
		const uint8* row = bits + y * bpr;
		for (int32 x = 0; x < width; x += 2) {
			int32 off = x * 4;
			fHistB[row[off + 0]]++;
			fHistG[row[off + 1]]++;
			fHistR[row[off + 2]]++;
		}
	}

	fHistogramDirty = false;
}


void
VideoPreviewView::_DrawHistogram()
{
	if (fCurrentFrame == NULL)
		return;

	if (fHistogramDirty)
		_ComputeHistogram();

	BRect bounds = Bounds();
	float histW = 258;
	float histH = 100;
	BRect histRect(
		bounds.right - histW - 5, bounds.bottom - histH - 5,
		bounds.right - 5, bounds.bottom - 5);

	// Semi-transparent background
	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(0, 0, 0, 180);
	FillRoundRect(histRect, 4, 4);

	// Find max value for normalization
	uint32 maxVal = 1;
	for (int i = 0; i < 256; i++) {
		if (fHistR[i] > maxVal) maxVal = fHistR[i];
		if (fHistG[i] > maxVal) maxVal = fHistG[i];
		if (fHistB[i] > maxVal) maxVal = fHistB[i];
	}

	float baseY = histRect.bottom - 2;
	float scaleY = (histH - 4) / (float)maxVal;
	float baseX = histRect.left + 1;

	// Draw R, G, B channels with additive blending
	SetDrawingMode(B_OP_ALPHA);
	for (int i = 0; i < 256; i++) {
		float x = baseX + i;

		float rH = fHistR[i] * scaleY;
		float gH = fHistG[i] * scaleY;
		float bH = fHistB[i] * scaleY;

		if (rH > 1) {
			SetHighColor(220, 40, 40, 100);
			StrokeLine(BPoint(x, baseY), BPoint(x, baseY - rH));
		}
		if (gH > 1) {
			SetHighColor(40, 200, 40, 100);
			StrokeLine(BPoint(x, baseY), BPoint(x, baseY - gH));
		}
		if (bH > 1) {
			SetHighColor(60, 60, 220, 100);
			StrokeLine(BPoint(x, baseY), BPoint(x, baseY - bH));
		}
	}

	// Label
	SetHighColor(255, 255, 255, 200);
	BFont font(be_plain_font);
	font.SetSize(9);
	SetFont(&font);
	DrawString("Histogram", BPoint(histRect.left + 5, histRect.top + 11));

	// Zoom indicator
	if (fZoomLevel > 1.0f) {
		BString zoomStr;
		zoomStr.SetToFormat("%.1fx", fZoomLevel);
		DrawString(zoomStr.String(), BPoint(histRect.right - 30, histRect.top + 11));
	}

	SetDrawingMode(B_OP_COPY);
}


void
VideoPreviewView::_DrawCompareMode()
{
	BRect bounds = Bounds();
	float midX = bounds.Width() / 2;

	SetDrawingMode(B_OP_COPY);
	SetHighColor(fBackgroundColor);
	FillRect(bounds);

	// Left half: reference frame
	if (fReferenceFrame != NULL && fReferenceFrame->IsValid()) {
		BRect leftDest(bounds.left, bounds.top, midX - 1, bounds.bottom);
		BRect srcBounds = fReferenceFrame->Bounds();
		float srcAspect = (srcBounds.Width() + 1) / (srcBounds.Height() + 1);
		float destW = leftDest.Width();
		float destH = leftDest.Height();
		float destAspect = destW / destH;

		BRect scaledDest;
		if (destAspect > srcAspect) {
			float w = destH * srcAspect;
			float offset = (destW - w) / 2;
			scaledDest.Set(leftDest.left + offset, leftDest.top,
				leftDest.left + offset + w, leftDest.bottom);
		} else {
			float h = destW / srcAspect;
			float offset = (destH - h) / 2;
			scaledDest.Set(leftDest.left, leftDest.top + offset,
				leftDest.right, leftDest.top + offset + h);
		}
		DrawBitmap(fReferenceFrame, srcBounds, scaledDest);
	}

	// Right half: live frame
	if (fCurrentFrame != NULL && fCurrentFrame->IsValid()) {
		BRect rightDest(midX + 1, bounds.top, bounds.right, bounds.bottom);
		BRect srcBounds = fCurrentFrame->Bounds();
		float srcAspect = (srcBounds.Width() + 1) / (srcBounds.Height() + 1);
		float destW = rightDest.Width();
		float destH = rightDest.Height();
		float destAspect = destW / destH;

		BRect scaledDest;
		if (destAspect > srcAspect) {
			float w = destH * srcAspect;
			float offset = (destW - w) / 2;
			scaledDest.Set(rightDest.left + offset, rightDest.top,
				rightDest.left + offset + w, rightDest.bottom);
		} else {
			float h = destW / srcAspect;
			float offset = (destH - h) / 2;
			scaledDest.Set(rightDest.left, rightDest.top + offset,
				rightDest.right, rightDest.top + offset + h);
		}
		DrawBitmap(fCurrentFrame, srcBounds, scaledDest);
	}

	// Draw divider line
	SetHighColor(255, 255, 0);
	StrokeLine(BPoint(midX, bounds.top), BPoint(midX, bounds.bottom));

	// Labels
	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(0, 0, 0, 160);
	FillRoundRect(BRect(5, 5, 85, 22), 3, 3);
	FillRoundRect(BRect(midX + 5, 5, midX + 65, 22), 3, 3);

	SetHighColor(255, 255, 255, 220);
	BFont font(be_bold_font);
	font.SetSize(10);
	SetFont(&font);
	DrawString("Reference", BPoint(10, 18));
	DrawString("Live", BPoint(midX + 10, 18));

	SetDrawingMode(B_OP_COPY);
}
