/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * DeskbarReplicant - Deskbar tray icon showing webcam status.
 */

#ifndef DESKBAR_REPLICANT_H
#define DESKBAR_REPLICANT_H

#include <View.h>
#include <Bitmap.h>
#include <String.h>
#include <MessageRunner.h>

// Status the replicant can display
enum webcam_status_t {
	WEBCAM_STATUS_IDLE		= 0,
	WEBCAM_STATUS_STREAMING	= 1,
	WEBCAM_STATUS_RECORDING	= 2,
	WEBCAM_STATUS_ERROR		= 3
};

// Messages between app and replicant
enum {
	MSG_REPLICANT_UPDATE	= 'rpup',
	MSG_REPLICANT_PULSE		= 'rpls'
};


class DeskbarReplicant : public BView {
public:
						DeskbarReplicant(BRect frame, const char* name);
						DeskbarReplicant(BMessage* archive);
	virtual				~DeskbarReplicant();

	// BArchivable
	static BArchivable*	Instantiate(BMessage* archive);
	virtual status_t	Archive(BMessage* data, bool deep = true) const;

	// BView
	virtual void		AttachedToWindow();
	virtual void		DetachedFromWindow();
	virtual void		Draw(BRect updateRect);
	virtual void		MessageReceived(BMessage* message);
	virtual void		MouseDown(BPoint where);

	// Status control (called from app via message)
	void				SetStatus(webcam_status_t status);
	void				SetFPS(float fps);
	void				SetDeviceName(const char* name);

	// Install/remove from Deskbar
	static status_t		InstallInDeskbar();
	static status_t		RemoveFromDeskbar();
	static bool			IsInstalledInDeskbar();

private:
	void				_DrawIcon();
	rgb_color			_StatusColor() const;

	webcam_status_t		fStatus;
	float				fFPS;
	BString				fDeviceName;
	BMessageRunner*		fPulseRunner;
	bool				fBlinkOn;
};

#endif // DESKBAR_REPLICANT_H
