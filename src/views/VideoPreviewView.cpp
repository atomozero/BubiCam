/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoPreviewView.h"

#include <Autolock.h>
#include <BitmapStream.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <NodeInfo.h>
#include <Path.h>
#include <TranslatorRoster.h>

#include <stdio.h>
#include <string.h>


VideoPreviewView::VideoPreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fCurrentFrame(NULL),
	fReferenceFrame(NULL),
	fCompareMode(false),
	fInspectorMode(false),
	fInspectorPoint(-1, -1),
	fFrameLock("frame lock"),
	fCurrentFPS(0.0f),
	fFramesReceived(0),
	fFramesDropped(0),
	fVideoWidth(0),
	fVideoHeight(0),
	fShowStats(true),
	fShowHistogram(false),
	fShowGrid(false),
	fGridMode(0),
	fZoomLevel(1.0f),
	fPanOffset(0, 0),
	fLastMousePos(0, 0),
	fIsPanning(false),
	fFrozen(false),
	fDragInitiated(false),
	fClickPoint(0, 0),
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

		// Draw frozen indicator
		if (fFrozen) {
			SetDrawingMode(B_OP_ALPHA);
			// Border flash
			SetHighColor(255, 200, 0, 140);
			BRect b = Bounds();
			StrokeRect(b);
			StrokeRect(b.InsetByCopy(1, 1));

			// "PAUSED" label top-right
			BFont font(be_bold_font);
			font.SetSize(11);
			SetFont(&font);
			const char* label = "PAUSED - drag to save";
			float tw = StringWidth(label);
			BRect labelRect(b.right - tw - 14, b.top + 4,
				b.right - 4, b.top + 22);
			SetHighColor(0, 0, 0, 180);
			FillRoundRect(labelRect, 4, 4);
			SetHighColor(255, 200, 0, 240);
			DrawString(label, BPoint(labelRect.left + 5, labelRect.bottom - 4));
			SetDrawingMode(B_OP_COPY);
		}

		// Draw stats overlay
		if (fShowStats)
			_DrawStats();

		// Draw histogram overlay
		if (fShowHistogram)
			_DrawHistogram();

		// Draw grid overlay
		if (fShowGrid)
			_DrawGrid();

		// Draw inspector overlay
		if (fInspectorMode && fInspectorPoint.x >= 0 && fCurrentFrame != NULL) {
			// Map screen point to bitmap coordinates
			BRect vr = fVideoRect;
			float bmpX = (fInspectorPoint.x - vr.left) / vr.Width()
				* fCurrentFrame->Bounds().Width();
			float bmpY = (fInspectorPoint.y - vr.top) / vr.Height()
				* fCurrentFrame->Bounds().Height();

			int32 px = (int32)bmpX;
			int32 py = (int32)bmpY;
			int32 bw = (int32)(fCurrentFrame->Bounds().Width() + 1);
			int32 bh = (int32)(fCurrentFrame->Bounds().Height() + 1);

			if (px >= 0 && px < bw && py >= 0 && py < bh) {
				int32 bpr = fCurrentFrame->BytesPerRow();
				const uint8* bits = (const uint8*)fCurrentFrame->Bits();
				const uint8* pixel = bits + py * bpr + px * 4;
				uint8 b = pixel[0], g = pixel[1], r = pixel[2], a = pixel[3];

				fInspectorInfo.SetToFormat("(%d, %d)  R:%d G:%d B:%d A:%d  #%02X%02X%02X",
					px, py, r, g, b, a, r, g, b);

				// Draw crosshair
				SetHighColor(255, 255, 0);
				StrokeLine(BPoint(fInspectorPoint.x - 8, fInspectorPoint.y),
					BPoint(fInspectorPoint.x + 8, fInspectorPoint.y));
				StrokeLine(BPoint(fInspectorPoint.x, fInspectorPoint.y - 8),
					BPoint(fInspectorPoint.x, fInspectorPoint.y + 8));

				// Draw info box
				SetDrawingMode(B_OP_ALPHA);
				BRect infoRect(5, bounds.bottom - 25, 320, bounds.bottom - 5);
				SetHighColor(0, 0, 0, 180);
				FillRoundRect(infoRect, 3, 3);
				SetHighColor(255, 255, 255, 230);
				BFont font(be_fixed_font);
				font.SetSize(10);
				SetFont(&font);
				DrawString(fInspectorInfo.String(),
					BPoint(infoRect.left + 5, infoRect.bottom - 7));

				// Draw color swatch
				BRect swatch(infoRect.right - 18, infoRect.top + 3,
					infoRect.right - 3, infoRect.bottom - 3);
				SetHighColor(r, g, b);
				FillRect(swatch);
				SetHighColor(255, 255, 255);
				StrokeRect(swatch);

				SetDrawingMode(B_OP_COPY);
			}
		}
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

	if (buttons & B_PRIMARY_MOUSE_BUTTON) {
		if (fInspectorMode) {
			fInspectorPoint = where;
			Invalidate();
			return;
		}
		if (fZoomLevel > 1.0f) {
			// Pan mode when zoomed in
			fIsPanning = true;
			fLastMousePos = where;
			SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		} else if (fCurrentFrame != NULL) {
			// Freeze the frame on click
			fFrozen = true;
			fDragInitiated = false;
			fClickPoint = where;
			SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
			Invalidate();
		}
	} else if (buttons & B_SECONDARY_MOUSE_BUTTON) {
		_InitiateDrag(where);
	}
}


void
VideoPreviewView::MouseUp(BPoint where)
{
	if (fFrozen) {
		// Unfreeze after click release or after drag completes
		fFrozen = false;
		fDragInitiated = false;
		Invalidate();
	}
	fIsPanning = false;
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
		return;
	}

	if (fFrozen && !fDragInitiated) {
		// Check if mouse has moved enough to start a drag
		float dx = where.x - fClickPoint.x;
		float dy = where.y - fClickPoint.y;
		if (dx * dx + dy * dy > 16) {  // 4px threshold
			fDragInitiated = true;
			_InitiateDrag(where);
		}
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
VideoPreviewView::SetShowGrid(bool show)
{
	fShowGrid = show;
	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoPreviewView::SetGridMode(int32 mode)
{
	fGridMode = mode % 3;
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
VideoPreviewView::SetBackgroundColor(rgb_color color)
{
	fBackgroundColor = color;
	SetViewColor(color);
	SetLowColor(color);
}


void
VideoPreviewView::SetInspectorMode(bool enabled)
{
	fInspectorMode = enabled;
	if (!enabled)
		fInspectorPoint.Set(-1, -1);
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

	// Don't update the displayed frame while frozen (click-to-freeze)
	if (fFrozen)
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


void
VideoPreviewView::_InitiateDrag(BPoint where)
{
	BAutolock lock(fFrameLock);

	if (fCurrentFrame == NULL || !fCurrentFrame->IsValid())
		return;

	// Make a copy of the current frame for saving
	BBitmap* copy = new BBitmap(fCurrentFrame->Bounds(),
		fCurrentFrame->ColorSpace());
	if (copy == NULL || !copy->IsValid()) {
		delete copy;
		return;
	}
	memcpy(copy->Bits(), fCurrentFrame->Bits(), fCurrentFrame->BitsLength());

	lock.Unlock();

	// Save to a temp file as PNG
	BPath path;
	find_directory(B_SYSTEM_TEMP_DIRECTORY, &path);
	path.Append("BubiCam_drag.png");

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		delete copy;
		return;
	}

	BBitmapStream stream(copy);
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	status_t status = roster->Translate(&stream, NULL, NULL, &file, 'PNG ');

	BBitmap* detached;
	stream.DetachBitmap(&detached);

	if (status != B_OK) {
		delete detached;
		return;
	}

	// Set MIME type on the file
	BNodeInfo nodeInfo(&file);
	nodeInfo.SetType("image/png");
	file.Unset();

	// Create drag message with file ref
	entry_ref ref;
	BEntry entry(path.Path());
	if (entry.GetRef(&ref) != B_OK) {
		delete detached;
		return;
	}

	BMessage dragMsg(B_SIMPLE_DATA);
	dragMsg.AddRef("refs", &ref);

	// Create a small thumbnail for the drag icon
	BRect thumbRect(0, 0, 63, 47);
	BBitmap* thumb = new BBitmap(thumbRect, B_RGBA32);
	if (thumb != NULL && thumb->IsValid() && detached != NULL) {
		// Simple nearest-neighbor scale for thumbnail
		uint8* dstBits = (uint8*)thumb->Bits();
		int32 dstBPR = thumb->BytesPerRow();
		uint8* srcBits = (uint8*)detached->Bits();
		int32 srcBPR = detached->BytesPerRow();
		int32 srcW = (int32)(detached->Bounds().Width() + 1);
		int32 srcH = (int32)(detached->Bounds().Height() + 1);
		int32 dstW = 64, dstH = 48;

		for (int32 y = 0; y < dstH; y++) {
			int32 srcY = y * srcH / dstH;
			for (int32 x = 0; x < dstW; x++) {
				int32 srcX = x * srcW / dstW;
				uint8* sp = srcBits + srcY * srcBPR + srcX * 4;
				uint8* dp = dstBits + y * dstBPR + x * 4;
				dp[0] = sp[0];
				dp[1] = sp[1];
				dp[2] = sp[2];
				dp[3] = 200;  // semi-transparent
			}
		}

		DragMessage(&dragMsg, thumb, B_OP_ALPHA, BPoint(32, 24));
	} else {
		delete thumb;
		DragMessage(&dragMsg, BRect(0, 0, 63, 47));
	}

	delete detached;
}


void
VideoPreviewView::_DrawGrid()
{
	BRect vr = fVideoRect;
	if (!vr.IsValid())
		return;

	SetDrawingMode(B_OP_ALPHA);
	SetPenSize(1.0f);

	bool drawThirds = (fGridMode == 0 || fGridMode == 2);
	bool drawCenter = (fGridMode == 1 || fGridMode == 2);

	if (drawThirds) {
		// Rule of thirds - semi-transparent white lines
		SetHighColor(255, 255, 255, 120);

		float w = vr.Width();
		float h = vr.Height();

		// Vertical lines at 1/3 and 2/3
		float x1 = vr.left + w / 3;
		float x2 = vr.left + w * 2 / 3;
		StrokeLine(BPoint(x1, vr.top), BPoint(x1, vr.bottom));
		StrokeLine(BPoint(x2, vr.top), BPoint(x2, vr.bottom));

		// Horizontal lines at 1/3 and 2/3
		float y1 = vr.top + h / 3;
		float y2 = vr.top + h * 2 / 3;
		StrokeLine(BPoint(vr.left, y1), BPoint(vr.right, y1));
		StrokeLine(BPoint(vr.left, y2), BPoint(vr.right, y2));
	}

	if (drawCenter) {
		// Center crosshair - semi-transparent yellow
		SetHighColor(255, 255, 0, 140);

		float cx = (vr.left + vr.right) / 2;
		float cy = (vr.top + vr.bottom) / 2;
		float armLen = min_c(vr.Width(), vr.Height()) / 8;

		StrokeLine(BPoint(cx - armLen, cy), BPoint(cx + armLen, cy));
		StrokeLine(BPoint(cx, cy - armLen), BPoint(cx, cy + armLen));

		// Small circle at center
		StrokeEllipse(BPoint(cx, cy), 4, 4);
	}

	SetDrawingMode(B_OP_COPY);
}
