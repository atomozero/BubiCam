/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VideoFilter - Abstract interface for video frame filters.
 *
 * Filters process B_RGB32 bitmaps in-place. They are applied in the
 * video preview pipeline after format conversion and before display.
 *
 * Built-in filters: grayscale, invert, mirror, sepia.
 * The interface is designed so external add-ons can provide additional
 * filters in the future.
 */

#ifndef VIDEO_FILTER_H
#define VIDEO_FILTER_H

#include <Bitmap.h>
#include <String.h>
#include <ObjectList.h>

class VideoFilter {
public:
	virtual				~VideoFilter() {}

	virtual const char*	Name() const = 0;
	virtual void		Apply(BBitmap* bitmap) = 0;
	virtual bool		IsEnabled() const { return fEnabled; }
	virtual void		SetEnabled(bool enabled) { fEnabled = enabled; }

protected:
	bool				fEnabled = false;
};


// Built-in filters

class GrayscaleFilter : public VideoFilter {
public:
	const char*			Name() const { return "Grayscale"; }
	void				Apply(BBitmap* bitmap);
};

class InvertFilter : public VideoFilter {
public:
	const char*			Name() const { return "Invert Colors"; }
	void				Apply(BBitmap* bitmap);
};

class MirrorFilter : public VideoFilter {
public:
	const char*			Name() const { return "Mirror Horizontal"; }
	void				Apply(BBitmap* bitmap);
};

class SepiaFilter : public VideoFilter {
public:
	const char*			Name() const { return "Sepia Tone"; }
	void				Apply(BBitmap* bitmap);
};


// Filter chain manager
class VideoFilterChain {
public:
						VideoFilterChain();
						~VideoFilterChain();

	void				AddFilter(VideoFilter* filter);
	VideoFilter*		FilterAt(int32 index) const;
	int32				CountFilters() const;
	void				ApplyAll(BBitmap* bitmap);

private:
	BObjectList<VideoFilter>	fFilters;
};

#endif // VIDEO_FILTER_H
