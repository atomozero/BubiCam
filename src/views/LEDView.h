/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#ifndef LED_VIEW_H
#define LED_VIEW_H

#include <View.h>
#include <MessageRunner.h>

enum led_state {
	LED_OFF = 0,
	LED_GREEN,
	LED_YELLOW,
	LED_RED
};


class LEDView : public BView {
public:
						LEDView(const char* name);
	virtual				~LEDView();

	virtual void		Draw(BRect updateRect);
	virtual void		AttachedToWindow();
	virtual void		MessageReceived(BMessage* message);

	void				SetState(led_state state);
	led_state			State() const { return fState; }

	void				SetBlinking(bool blink);
	bool				IsBlinking() const { return fBlinking; }

	void				SetLabel(const char* label);

private:
	led_state			fState;
	bool				fBlinking;
	bool				fBlinkOn;
	BMessageRunner*		fBlinkRunner;
	BString				fLabel;
};

#endif // LED_VIEW_H
