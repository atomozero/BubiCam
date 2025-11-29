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

class VideoPreviewView : public BView {
public:
						VideoPreviewView(const char* name);
	virtual				~VideoPreviewView();

	virtual void		Draw(BRect updateRect);
	virtual void		FrameResized(float newWidth, float newHeight);
	virtual void		AttachedToWindow();

	void				SetFrame(BBitmap* bitmap);
	void				ClearFrame();

	BRect				VideoRect() const { return fVideoRect; }

private:
	void				_CalculateVideoRect();

	BBitmap*			fCurrentFrame;
	BLocker				fFrameLock;
	BRect				fVideoRect;
	rgb_color			fBackgroundColor;
};

#endif // VIDEO_PREVIEW_VIEW_H
