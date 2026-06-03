/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * PreviewReplicant - Draggable live webcam preview replicant.
 *
 * Can be embedded on the Desktop or in any Shelf-aware app.
 * Fetches JPEG snapshots from BubiCam's HTTP streaming server
 * (localhost:8080/snapshot) and displays them as a live preview.
 */

#ifndef PREVIEW_REPLICANT_H
#define PREVIEW_REPLICANT_H

#include <View.h>
#include <Bitmap.h>
#include <Dragger.h>
#include <MessageRunner.h>
#include <String.h>

class PreviewReplicant : public BView {
public:
						PreviewReplicant(BRect frame, const char* name);
						PreviewReplicant(BMessage* archive);
	virtual				~PreviewReplicant();

	// BArchivable
	static BArchivable*	Instantiate(BMessage* archive);
	virtual status_t	Archive(BMessage* data, bool deep = true) const;

	// BView
	virtual void		AttachedToWindow();
	virtual void		DetachedFromWindow();
	virtual void		Draw(BRect updateRect);
	virtual void		MessageReceived(BMessage* message);

private:
	void				_FetchSnapshot();
	BBitmap*			_DecodeJPEG(const void* data, size_t size);

	BBitmap*			fBitmap;
	BMessageRunner*		fRefreshRunner;
	BString				fStatus;
	uint16				fPort;
};

#endif // PREVIEW_REPLICANT_H
