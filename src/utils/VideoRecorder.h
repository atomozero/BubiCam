/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VideoRecorder - Records video frames to AVI (Motion JPEG) files
 */

#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

#include <File.h>
#include <Bitmap.h>
#include <Locker.h>
#include <ObjectList.h>
#include <SupportDefs.h>

#include <stdio.h>
#include <jpeglib.h>


struct AVIIndexEntry {
	off_t		offset;
	uint32		size;
};


class VideoRecorder {
public:
						VideoRecorder();
						~VideoRecorder();

	status_t			Start(const char* path, int32 width, int32 height,
							float fps = 30.0f, int jpegQuality = 85);
	status_t			AddFrame(BBitmap* bitmap);
	status_t			Stop();

	bool				IsRecording() const { return fRecording; }
	uint32				FramesRecorded() const { return fFrameCount; }
	bigtime_t			Duration() const;
	off_t				FileSize() const;

private:
	status_t			_WriteAVIHeaders();
	status_t			_FinalizeAVI();
	status_t			_CompressFrameToJPEG(BBitmap* bitmap,
							uint8** outBuffer, unsigned long* outSize);
	void				_WriteFourCC(const char* fourcc);
	void				_WriteUInt32(uint32 value);
	void				_WriteUInt16(uint16 value);

	BFile				fFile;
	BLocker				fLock;
	bool				fRecording;

	int32				fWidth;
	int32				fHeight;
	float				fFPS;
	int					fJPEGQuality;
	uint32				fFrameCount;
	bigtime_t			fStartTime;

	// AVI structure tracking
	off_t				fMoviListStart;
	off_t				fMoviDataStart;
	BObjectList<AVIIndexEntry>	fIndex;
};

#endif // VIDEO_RECORDER_H
