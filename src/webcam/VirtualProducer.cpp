/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 */

#include "VirtualProducer.h"

#include <Autolock.h>
#include <Buffer.h>
#include <MediaRoster.h>
#include <TimeSource.h>

#include <stdio.h>
#include <string.h>


VirtualProducer::VirtualProducer(const char* name)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferProducer(B_MEDIA_RAW_VIDEO),
	fConnected(false),
	fEnabled(false),
	fBufferGroup(NULL),
	fWidth(640),
	fHeight(480),
	fFPS(30.0f)
{
	fOutput.destination = media_destination::null;
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.node = Node();
	strcpy(fOutput.name, "BubiCam Virtual Output");

	fOutput.format.type = B_MEDIA_RAW_VIDEO;
	fOutput.format.u.raw_video.display.line_width = fWidth;
	fOutput.format.u.raw_video.display.line_count = fHeight;
	fOutput.format.u.raw_video.display.format = B_RGB32;
	fOutput.format.u.raw_video.field_rate = fFPS;
}


VirtualProducer::~VirtualProducer()
{
	delete fBufferGroup;
}


void
VirtualProducer::SetFormat(int32 width, int32 height, float fps)
{
	BAutolock lock(fLock);
	fWidth = width;
	fHeight = height;
	fFPS = fps;
}


status_t
VirtualProducer::PushFrame(BBitmap* bitmap)
{
	BAutolock lock(fLock);

	if (!fConnected || !fEnabled || bitmap == NULL)
		return B_NOT_ALLOWED;

	if (fBufferGroup == NULL)
		return B_NO_INIT;

	BBuffer* buffer = fBufferGroup->RequestBuffer(
		fWidth * fHeight * 4, 10000);
	if (buffer == NULL)
		return B_WOULD_BLOCK;

	memcpy(buffer->Data(), bitmap->Bits(),
		min_c((size_t)buffer->SizeAvailable(), (size_t)bitmap->BitsLength()));

	media_header* header = buffer->Header();
	header->type = B_MEDIA_RAW_VIDEO;
	header->size_used = fWidth * fHeight * 4;
	header->time_source = TimeSource()->ID();
	header->start_time = TimeSource()->Now();

	status_t err = SendBuffer(buffer, fOutput.source, fOutput.destination);
	if (err != B_OK)
		buffer->Recycle();

	return err;
}


BMediaAddOn*
VirtualProducer::AddOn(int32* internalId) const
{
	if (internalId != NULL)
		*internalId = 0;
	return NULL;
}


void
VirtualProducer::NodeRegistered()
{
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.node = Node();

	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


void
VirtualProducer::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	// No periodic events needed - frames are pushed externally
}


status_t
VirtualProducer::FormatSuggestionRequested(media_type type,
	int32 quality, media_format* format)
{
	if (type != B_MEDIA_RAW_VIDEO && type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;

	*format = fOutput.format;
	return B_OK;
}


status_t
VirtualProducer::FormatProposal(const media_source& output,
	media_format* format)
{
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	format->type = B_MEDIA_RAW_VIDEO;
	format->u.raw_video.display.format = B_RGB32;
	format->u.raw_video.display.line_width = fWidth;
	format->u.raw_video.display.line_count = fHeight;
	format->u.raw_video.field_rate = fFPS;

	return B_OK;
}


status_t
VirtualProducer::FormatChangeRequested(const media_source& source,
	const media_destination& destination, media_format* ioFormat,
	int32* _deprecated_)
{
	return B_ERROR;
}


status_t
VirtualProducer::GetNextOutput(int32* cookie, media_output* outOutput)
{
	if (*cookie != 0)
		return B_BAD_INDEX;

	*outOutput = fOutput;
	(*cookie)++;
	return B_OK;
}


status_t
VirtualProducer::DisposeOutputCookie(int32 cookie)
{
	return B_OK;
}


status_t
VirtualProducer::SetBufferGroup(const media_source& forSource,
	BBufferGroup* group)
{
	if (forSource != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	BAutolock lock(fLock);
	delete fBufferGroup;
	fBufferGroup = group;
	return B_OK;
}


status_t
VirtualProducer::PrepareToConnect(const media_source& what,
	const media_destination& where, media_format* format,
	media_source* outSource, char* outName)
{
	if (what != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	if (fConnected)
		return B_MEDIA_ALREADY_CONNECTED;

	*format = fOutput.format;
	*outSource = fOutput.source;
	strcpy(outName, "BubiCam Virtual");

	return B_OK;
}


void
VirtualProducer::Connect(status_t error, const media_source& source,
	const media_destination& destination, const media_format& format,
	char* ioName)
{
	if (error != B_OK) {
		fOutput.destination = media_destination::null;
		return;
	}

	BAutolock lock(fLock);
	fOutput.destination = destination;
	fOutput.format = format;
	fConnected = true;

	// Create buffer group
	int32 frameSize = fWidth * fHeight * 4;
	delete fBufferGroup;
	fBufferGroup = new BBufferGroup(frameSize, 3);

	strcpy(ioName, "BubiCam Virtual");
}


void
VirtualProducer::Disconnect(const media_source& what,
	const media_destination& where)
{
	if (what != fOutput.source || where != fOutput.destination)
		return;

	BAutolock lock(fLock);
	fOutput.destination = media_destination::null;
	fConnected = false;
	fEnabled = false;

	delete fBufferGroup;
	fBufferGroup = NULL;
}


void
VirtualProducer::EnableOutput(const media_source& what,
	bool enabled, int32* _deprecated_)
{
	if (what == fOutput.source)
		fEnabled = enabled;
}


status_t
VirtualProducer::GetLatency(bigtime_t* outLatency)
{
	*outLatency = 10000;  // 10ms
	return B_OK;
}


void
VirtualProducer::LatencyChanged(const media_source& source,
	const media_destination& destination, bigtime_t newLatency,
	uint32 flags)
{
	// Nothing to adjust
}
