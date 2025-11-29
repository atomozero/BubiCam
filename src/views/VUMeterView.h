/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef VU_METER_VIEW_H
#define VU_METER_VIEW_H

#include <View.h>
#include <Locker.h>

class VUMeterView : public BView {
public:
						VUMeterView(const char* name);
	virtual				~VUMeterView();

	virtual void		Draw(BRect updateRect);
	virtual void		AttachedToWindow();
	virtual void		FrameResized(float newWidth, float newHeight);

	void				SetLevel(float left, float right);
	void				SetPeakHold(bool enabled) { fPeakHold = enabled; }

private:
	void				_DrawMeter(BRect rect, float level, float peak,
							const char* label);
	void				_DrawScale(BRect rect);
	rgb_color			_ColorForLevel(float level);

	float				fLeftLevel;
	float				fRightLevel;
	float				fLeftPeak;
	float				fRightPeak;
	bigtime_t			fLeftPeakTime;
	bigtime_t			fRightPeakTime;
	bool				fPeakHold;
	BLocker				fLock;

	static const bigtime_t kPeakHoldTime = 2000000;  // 2 seconds
};

#endif // VU_METER_VIEW_H
