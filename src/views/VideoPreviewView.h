/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef VIDEO_PREVIEW_VIEW_H
#define VIDEO_PREVIEW_VIEW_H

#include <View.h>
#include <Bitmap.h>
#include <Locker.h>
#include <String.h>

class VideoPreviewView : public BView {
public:
						VideoPreviewView(const char* name);
	virtual				~VideoPreviewView();

	virtual void		Draw(BRect updateRect);
	virtual void		FrameResized(float newWidth, float newHeight);
	virtual void		AttachedToWindow();
	virtual void		MouseWheelChanged(BMessage* message);
	virtual void		MouseDown(BPoint where);
	virtual void		MouseUp(BPoint where);
	virtual void		MouseMoved(BPoint where, uint32 transit, const BMessage* msg);

	void				SetFrame(BBitmap* bitmap);
	void				ClearFrame();

	// Statistics
	void				UpdateStats(float fps, uint32 received, uint32 dropped);
	void				SetResolution(int32 width, int32 height);
	void				SetShowStats(bool show);
	void				SetShowHistogram(bool show);
	void				SetShowGrid(bool show);
	bool				ShowGrid() const { return fShowGrid; }
	void				SetGridMode(int32 mode);
	int32				GridMode() const { return fGridMode; }
	void				ResetZoom();
	bool				IsFrozen() const { return fFrozen; }
	void				SetBackgroundColor(rgb_color color);
	void				SetInspectorMode(bool enabled);
	bool				InspectorMode() const { return fInspectorMode; }

	void				CaptureReference();
	void				ClearReference();
	void				SetCompareMode(bool enabled);
	bool				CompareMode() const { return fCompareMode; }
	bool				HasReference() const { return fReferenceFrame != NULL; }

	// Getters for external stats bar
	float				CurrentFPS() const { return fCurrentFPS; }
	uint32				FramesReceived() const { return fFramesReceived; }
	uint32				FramesDropped() const { return fFramesDropped; }
	int32				VideoWidth() const { return fVideoWidth; }
	int32				VideoHeight() const { return fVideoHeight; }

	BRect				VideoRect() const { return fVideoRect; }

private:
	void				_CalculateVideoRect();
	void				_DrawStats();
	void				_DrawHistogram();
	void				_ComputeHistogram();
	void				_DrawGrid();
	void				_DrawCompareMode();
	void				_InitiateDrag(BPoint where);

	BBitmap*			fCurrentFrame;
	BBitmap*			fReferenceFrame;
	bool				fCompareMode;
	bool				fInspectorMode;
	BPoint				fInspectorPoint;
	BString				fInspectorInfo;
	BLocker				fFrameLock;
	BRect				fVideoRect;
	rgb_color			fBackgroundColor;

	// Statistics
	float				fCurrentFPS;
	uint32				fFramesReceived;
	uint32				fFramesDropped;
	int32				fVideoWidth;
	int32				fVideoHeight;
	bool				fShowStats;
	bool				fShowHistogram;
	bool				fShowGrid;
	int32				fGridMode;		// 0=thirds, 1=center cross, 2=both

	// Zoom and pan
	float				fZoomLevel;
	BPoint				fPanOffset;
	BPoint				fLastMousePos;
	bool				fIsPanning;

	// Click-to-freeze and drag-to-save
	bool				fFrozen;
	bool				fDragInitiated;
	BPoint				fClickPoint;

	// Histogram data (256 bins per channel)
	uint32				fHistR[256];
	uint32				fHistG[256];
	uint32				fHistB[256];
	bool				fHistogramDirty;
};

#endif // VIDEO_PREVIEW_VIEW_H
