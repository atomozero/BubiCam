/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VideoFilter.h"

#include <string.h>
#include <algorithm>


// ============================================================================
// Grayscale
// ============================================================================

void
GrayscaleFilter::Apply(BBitmap* bitmap)
{
	if (bitmap == NULL || !fEnabled)
		return;

	uint8* bits = (uint8*)bitmap->Bits();
	int32 length = bitmap->BitsLength();

	for (int32 i = 0; i < length; i += 4) {
		uint8 gray = (uint8)((bits[i + 2] * 77 + bits[i + 1] * 150 + bits[i] * 29) >> 8);
		bits[i] = gray;
		bits[i + 1] = gray;
		bits[i + 2] = gray;
		// alpha unchanged
	}
}


// ============================================================================
// Invert
// ============================================================================

void
InvertFilter::Apply(BBitmap* bitmap)
{
	if (bitmap == NULL || !fEnabled)
		return;

	uint8* bits = (uint8*)bitmap->Bits();
	int32 length = bitmap->BitsLength();

	for (int32 i = 0; i < length; i += 4) {
		bits[i] = 255 - bits[i];
		bits[i + 1] = 255 - bits[i + 1];
		bits[i + 2] = 255 - bits[i + 2];
	}
}


// ============================================================================
// Mirror
// ============================================================================

void
MirrorFilter::Apply(BBitmap* bitmap)
{
	if (bitmap == NULL || !fEnabled)
		return;

	int32 width = (int32)(bitmap->Bounds().Width() + 1);
	int32 height = (int32)(bitmap->Bounds().Height() + 1);
	int32 bpr = bitmap->BytesPerRow();
	uint8* bits = (uint8*)bitmap->Bits();

	for (int32 y = 0; y < height; y++) {
		uint32* row = (uint32*)(bits + y * bpr);
		for (int32 x = 0; x < width / 2; x++) {
			uint32 tmp = row[x];
			row[x] = row[width - 1 - x];
			row[width - 1 - x] = tmp;
		}
	}
}


// ============================================================================
// Sepia
// ============================================================================

void
SepiaFilter::Apply(BBitmap* bitmap)
{
	if (bitmap == NULL || !fEnabled)
		return;

	uint8* bits = (uint8*)bitmap->Bits();
	int32 length = bitmap->BitsLength();

	for (int32 i = 0; i < length; i += 4) {
		uint8 b = bits[i], g = bits[i + 1], r = bits[i + 2];
		int32 gray = (r * 77 + g * 150 + b * 29) >> 8;

		int32 sr = gray + 40;
		int32 sg = gray + 20;
		int32 sb = gray - 10;

		bits[i] = (uint8)(sb < 0 ? 0 : (sb > 255 ? 255 : sb));
		bits[i + 1] = (uint8)(sg > 255 ? 255 : sg);
		bits[i + 2] = (uint8)(sr > 255 ? 255 : sr);
	}
}


// ============================================================================
// Filter Chain
// ============================================================================

VideoFilterChain::VideoFilterChain()
	:
	fFilters(10)
{
}


VideoFilterChain::~VideoFilterChain()
{
}


void
VideoFilterChain::AddFilter(VideoFilter* filter)
{
	fFilters.AddItem(filter);
}


VideoFilter*
VideoFilterChain::FilterAt(int32 index) const
{
	return fFilters.ItemAt(index);
}


int32
VideoFilterChain::CountFilters() const
{
	return fFilters.CountItems();
}


void
VideoFilterChain::ApplyAll(BBitmap* bitmap)
{
	for (int32 i = 0; i < fFilters.CountItems(); i++) {
		VideoFilter* filter = fFilters.ItemAt(i);
		if (filter != NULL && filter->IsEnabled())
			filter->Apply(bitmap);
	}
}
