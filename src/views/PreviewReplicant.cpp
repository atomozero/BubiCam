/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "PreviewReplicant.h"

#include <Bitmap.h>
#include <BitmapStream.h>
#include <Dragger.h>
#include <TranslatorRoster.h>
#include <TranslationUtils.h>
#include <Window.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kReplicantName = "BubiCam Preview";
static const char* kAppSignature = "application/x-vnd.BubiCam";
static const uint32 kMsgRefresh = 'rfsh';
static const bigtime_t kRefreshInterval = 200000;  // 200ms = ~5fps


PreviewReplicant::PreviewReplicant(BRect frame, const char* name)
	:
	BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
	fBitmap(NULL),
	fRefreshRunner(NULL),
	fStatus("Starting..."),
	fPort(9847)
{
	// Add dragger in bottom-right corner
	BRect draggerRect(frame.Width() - 7, frame.Height() - 7,
		frame.Width(), frame.Height());
	BDragger* dragger = new BDragger(draggerRect, this, B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	AddChild(dragger);
}


PreviewReplicant::PreviewReplicant(BMessage* archive)
	:
	BView(archive),
	fBitmap(NULL),
	fRefreshRunner(NULL),
	fStatus("Connecting..."),
	fPort(9847)
{
	archive->FindInt16("port", (int16*)&fPort);
}


PreviewReplicant::~PreviewReplicant()
{
	delete fRefreshRunner;
	delete fBitmap;
}


BArchivable*
PreviewReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "PreviewReplicant"))
		return NULL;
	return new PreviewReplicant(archive);
}


status_t
PreviewReplicant::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);
	if (status != B_OK)
		return status;

	data->AddString("class", "PreviewReplicant");
	data->AddString("add_on", kAppSignature);
	data->AddInt16("port", (int16)fPort);

	return B_OK;
}


void
PreviewReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(0, 0, 0);

	// Start periodic snapshot fetching
	BMessage refreshMsg(kMsgRefresh);
	fRefreshRunner = new BMessageRunner(BMessenger(this), &refreshMsg,
		kRefreshInterval);
}


void
PreviewReplicant::DetachedFromWindow()
{
	delete fRefreshRunner;
	fRefreshRunner = NULL;
	BView::DetachedFromWindow();
}


void
PreviewReplicant::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	if (fBitmap != NULL && fBitmap->IsValid()) {
		// Draw the webcam frame scaled to fill the view
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fBitmap, fBitmap->Bounds(), bounds);
	} else {
		// No frame - draw placeholder
		SetHighColor(32, 32, 32);
		FillRect(bounds);

		SetHighColor(180, 180, 180);
		SetDrawingMode(B_OP_OVER);
		BFont font(be_plain_font);
		font.SetSize(10);
		SetFont(&font);

		float strWidth = StringWidth(fStatus.String());
		DrawString(fStatus.String(),
			BPoint((bounds.Width() - strWidth) / 2, bounds.Height() / 2));
	}
}


void
PreviewReplicant::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefresh:
			_FetchSnapshot();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
PreviewReplicant::_FetchSnapshot()
{
	// Connect to BubiCam's HTTP snapshot endpoint
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fStatus = "Socket error";
		Invalidate();
		return;
	}

	// Set short timeout
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(fPort);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(sock);
		fStatus = "No stream (start MJPEG server)";
		Invalidate();
		return;
	}

	// Send HTTP request
	const char* request = "GET /snapshot HTTP/1.0\r\nHost: localhost\r\n\r\n";
	send(sock, request, strlen(request), 0);

	// Read response (up to 2MB)
	static const size_t kMaxSize = 2 * 1024 * 1024;
	uint8* buffer = (uint8*)malloc(kMaxSize);
	if (buffer == NULL) {
		close(sock);
		return;
	}

	size_t totalRead = 0;
	while (totalRead < kMaxSize) {
		ssize_t n = recv(sock, buffer + totalRead, kMaxSize - totalRead, 0);
		if (n <= 0)
			break;
		totalRead += n;
	}
	close(sock);

	if (totalRead == 0) {
		free(buffer);
		fStatus = "No data";
		Invalidate();
		return;
	}

	// Find JPEG data after HTTP headers (look for \r\n\r\n then JPEG SOI 0xFFD8)
	uint8* jpegStart = NULL;
	size_t jpegSize = 0;

	for (size_t i = 0; i + 3 < totalRead; i++) {
		if (buffer[i] == '\r' && buffer[i+1] == '\n' &&
			buffer[i+2] == '\r' && buffer[i+3] == '\n') {
			jpegStart = buffer + i + 4;
			jpegSize = totalRead - (i + 4);
			break;
		}
	}

	if (jpegStart != NULL && jpegSize > 2 &&
		jpegStart[0] == 0xFF && jpegStart[1] == 0xD8) {
		BBitmap* newBitmap = _DecodeJPEG(jpegStart, jpegSize);
		if (newBitmap != NULL) {
			delete fBitmap;
			fBitmap = newBitmap;
			fStatus = "";
		}
	} else {
		fStatus = "Invalid response";
	}

	free(buffer);
	Invalidate();
}


BBitmap*
PreviewReplicant::_DecodeJPEG(const void* data, size_t size)
{
	BMemoryIO memIO(data, size);
	BBitmap* bitmap = BTranslationUtils::GetBitmap(&memIO);
	return bitmap;
}
