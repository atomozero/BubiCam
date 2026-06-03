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

#include "AudioSink.h"


// Video codec selection for recording
enum video_codec_t {
	VIDEO_CODEC_MJPEG	= 0,	// Motion JPEG (default, good compression)
	VIDEO_CODEC_RAW		= 1		// Uncompressed RGB32 (lossless, large files)
};


struct AVIIndexEntry {
	off_t		offset;
	uint32		size;
};


class VideoRecorder : public AudioSink {
public:
						VideoRecorder();
	virtual				~VideoRecorder();

	void				SetCodec(video_codec_t codec) { fCodec = codec; }
	video_codec_t		Codec() const { return fCodec; }

	status_t			Start(const char* path, int32 width, int32 height,
							float fps = 30.0f, int jpegQuality = 85);
	status_t			StartWithAudio(const char* path, int32 width,
							int32 height, float fps,
							float sampleRate, int32 channels,
							int32 bitsPerSample, int jpegQuality = 85);
	status_t			AddFrame(BBitmap* bitmap);
	status_t			AddAudioBuffer(const void* data, size_t size);
	status_t			Stop();

	// AudioSink: accepts raw PCM from the capture library and converts it
	// (float -> int16 when needed) before storing it in the AVI stream.
	virtual void		WriteAudio(const void* data, size_t size,
							const media_raw_audio_format& format);

	bool				IsRecording() const { return fRecording; }
	bool				HasAudio() const { return fHasAudio; }
	uint32				FramesRecorded() const { return fFrameCount; }
	uint32				AudioChunksRecorded() const { return fAudioChunkCount; }
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
	video_codec_t		fCodec;
	uint32				fFrameCount;
	bigtime_t			fStartTime;

	// Audio parameters
	bool				fHasAudio;
	float				fSampleRate;
	int32				fChannels;
	int32				fBitsPerSample;
	uint32				fAudioChunkCount;
	uint32				fTotalAudioBytes;

	// Reusable float->int16 conversion scratch. Avoids a heap allocation on
	// every audio buffer: WriteAudio() runs on the real-time audio thread.
	int16*				fAudioScratch;
	size_t				fAudioScratchSamples;

	// AVI structure tracking
	off_t				fMoviListStart;
	off_t				fMoviDataStart;
	BObjectList<AVIIndexEntry>	fVideoIndex;
	BObjectList<AVIIndexEntry>	fAudioIndex;
};

#endif // VIDEO_RECORDER_H
