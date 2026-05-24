/*
 * BubiCam - Webcam Driver Tester for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * VideoRecorder - Records video frames to AVI (Motion JPEG) files
 *
 * AVI container format (simplified for MJPEG):
 *   RIFF 'AVI '
 *     LIST 'hdrl'
 *       'avih' - Main AVI header
 *       LIST 'strl'
 *         'strh' - Stream header (video)
 *         'strf' - Stream format (BITMAPINFOHEADER)
 *     LIST 'movi'
 *       '00dc' - Compressed video frames (JPEG data)
 *       ...
 *     'idx1' - Index of all frames
 */

#include "VideoRecorder.h"

#include <Autolock.h>
#include <string.h>
#include <setjmp.h>

#define LOG_MODULE "VideoRecorder"
#include "ErrorUtils.h"


VideoRecorder::VideoRecorder()
	:
	fLock("recorder lock"),
	fRecording(false),
	fWidth(0),
	fHeight(0),
	fFPS(30.0f),
	fJPEGQuality(85),
	fFrameCount(0),
	fStartTime(0),
	fHasAudio(false),
	fSampleRate(0),
	fChannels(0),
	fBitsPerSample(0),
	fAudioChunkCount(0),
	fTotalAudioBytes(0),
	fMoviListStart(0),
	fMoviDataStart(0),
	fVideoIndex(20),
	fAudioIndex(20)
{
}


VideoRecorder::~VideoRecorder()
{
	if (fRecording)
		Stop();
}


status_t
VideoRecorder::Start(const char* path, int32 width, int32 height,
	float fps, int jpegQuality)
{
	BAutolock lock(fLock);

	if (fRecording)
		return B_BUSY;

	status_t status = fFile.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK) {
		LOG_ERROR("Failed to create file: %s", strerror(status));
		return status;
	}

	fWidth = width;
	fHeight = height;
	fFPS = fps > 0 ? fps : 30.0f;
	fJPEGQuality = jpegQuality;
	fFrameCount = 0;
	// Reset audio state for video-only recording.
	// StartWithAudio() sets these BEFORE calling Start().
	fHasAudio = false;
	fAudioChunkCount = 0;
	fTotalAudioBytes = 0;
	fVideoIndex.MakeEmpty();
	fAudioIndex.MakeEmpty();

	status = _WriteAVIHeaders();
	if (status != B_OK) {
		fFile.Unset();
		return status;
	}

	fRecording = true;
	fStartTime = system_time();

	LOG_INFO("Recording started: %s (%dx%d @ %.1f fps, quality %d)",
		path, (int)width, (int)height, fps, jpegQuality);

	return B_OK;
}


status_t
VideoRecorder::StartWithAudio(const char* path, int32 width, int32 height,
	float fps, float sampleRate, int32 channels, int32 bitsPerSample,
	int jpegQuality)
{
	BAutolock lock(fLock);

	// Validate audio parameters
	if (sampleRate <= 0 || channels <= 0 || bitsPerSample <= 0) {
		LOG_ERROR("Invalid audio params: %.0f Hz, %d ch, %d bit",
			sampleRate, (int)channels, (int)bitsPerSample);
		lock.Unlock();
		return Start(path, width, height, fps, jpegQuality);
	}

	if (fRecording)
		return B_BUSY;

	status_t status = fFile.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK) {
		LOG_ERROR("Failed to create file: %s", strerror(status));
		return status;
	}

	fWidth = width;
	fHeight = height;
	fFPS = fps > 0 ? fps : 30.0f;
	fJPEGQuality = jpegQuality;
	fFrameCount = 0;
	fHasAudio = true;
	fSampleRate = sampleRate;
	fChannels = channels;
	fBitsPerSample = bitsPerSample;
	fAudioChunkCount = 0;
	fTotalAudioBytes = 0;
	fVideoIndex.MakeEmpty();
	fAudioIndex.MakeEmpty();

	status = _WriteAVIHeaders();
	if (status != B_OK) {
		fFile.Unset();
		return status;
	}

	fRecording = true;
	fStartTime = system_time();

	LOG_INFO("Recording started with audio: %s (%dx%d @ %.1f fps, %.0f Hz %d ch %d bit)",
		path, (int)width, (int)height, fps, sampleRate, (int)channels,
		(int)bitsPerSample);

	return B_OK;
}


status_t
VideoRecorder::AddFrame(BBitmap* bitmap)
{
	BAutolock lock(fLock);

	if (!fRecording || bitmap == NULL)
		return B_NOT_ALLOWED;

	// Compress bitmap to JPEG
	uint8* jpegData = NULL;
	unsigned long jpegSize = 0;

	status_t status = _CompressFrameToJPEG(bitmap, &jpegData, &jpegSize);
	if (status != B_OK || jpegData == NULL)
		return status;

	// Write AVI chunk: '00dc' + size + data (+ padding)
	off_t chunkStart = 0;
	chunkStart = fFile.Position();

	off_t frameOffset = chunkStart - fMoviDataStart;

	_WriteFourCC("00dc");
	_WriteUInt32((uint32)jpegSize);
	fFile.Write(jpegData, jpegSize);

	// Pad to 2-byte boundary
	if (jpegSize & 1) {
		uint8 pad = 0;
		fFile.Write(&pad, 1);
	}

	// Record index entry
	AVIIndexEntry* entry = new AVIIndexEntry();
	entry->offset = frameOffset;
	entry->size = (uint32)jpegSize;
	fVideoIndex.AddItem(entry);

	fFrameCount++;
	free(jpegData);

	return B_OK;
}


status_t
VideoRecorder::AddAudioBuffer(const void* data, size_t size)
{
	BAutolock lock(fLock);

	if (!fRecording || !fHasAudio || data == NULL || size == 0)
		return B_NOT_ALLOWED;

	off_t chunkStart = fFile.Position();
	off_t chunkOffset = chunkStart - fMoviDataStart;

	// Write AVI audio chunk: '01wb' + size + data
	_WriteFourCC("01wb");
	_WriteUInt32((uint32)size);
	fFile.Write(data, size);

	// Pad to 2-byte boundary
	if (size & 1) {
		uint8 pad = 0;
		fFile.Write(&pad, 1);
	}

	AVIIndexEntry* entry = new AVIIndexEntry();
	entry->offset = chunkOffset;
	entry->size = (uint32)size;
	fAudioIndex.AddItem(entry);

	fAudioChunkCount++;
	fTotalAudioBytes += (uint32)size;

	return B_OK;
}


status_t
VideoRecorder::Stop()
{
	BAutolock lock(fLock);

	if (!fRecording)
		return B_NOT_ALLOWED;

	fRecording = false;

	status_t status = _FinalizeAVI();

	bigtime_t duration = system_time() - fStartTime;
	LOG_INFO("Recording stopped: %u frames, %.1f seconds",
		(unsigned)fFrameCount, duration / 1000000.0);

	fFile.Unset();
	return status;
}


bigtime_t
VideoRecorder::Duration() const
{
	if (fStartTime == 0)
		return 0;
	return system_time() - fStartTime;
}


off_t
VideoRecorder::FileSize() const
{
	if (!fRecording)
		return 0;
	off_t size = 0;
	fFile.GetSize(&size);
	return size;
}


status_t
VideoRecorder::_WriteAVIHeaders()
{
	// We write placeholder headers now and fix them up in _FinalizeAVI()

	uint32 numStreams = fHasAudio ? 2 : 1;

	// Calculate header list size
	// strl content: "strl"(4) + strh(8+56) + strf(8+40) = 116 (video) / 94 (audio)
	// strl on disk: "LIST"(4) + size(4) + content = 8 + content
	uint32 videoStrlSize = 4 + 8 + 56 + 8 + 40;  // content of video strl LIST
	uint32 audioStrlSize = fHasAudio ? (4 + 8 + 56 + 8 + 18) : 0;
	uint32 hdrlSize = 4 + 64 + (8 + videoStrlSize)
		+ (fHasAudio ? (8 + audioStrlSize) : 0);

	// RIFF header
	_WriteFourCC("RIFF");
	_WriteUInt32(0);  // File size - 8, filled in later
	_WriteFourCC("AVI ");

	// LIST 'hdrl'
	_WriteFourCC("LIST");
	_WriteUInt32(hdrlSize);
	_WriteFourCC("hdrl");

	// 'avih' - Main AVI header (56 bytes)
	_WriteFourCC("avih");
	_WriteUInt32(56);
	uint32 usPerFrame = (uint32)(1000000.0f / fFPS);
	_WriteUInt32(usPerFrame);           // dwMicroSecPerFrame
	_WriteUInt32(0);                    // dwMaxBytesPerSec (filled later)
	_WriteUInt32(0);                    // dwPaddingGranularity
	_WriteUInt32(0x0110);               // dwFlags: AVIF_HASINDEX | AVIF_ISINTERLEAVED
	_WriteUInt32(0);                    // dwTotalFrames (filled later)
	_WriteUInt32(0);                    // dwInitialFrames
	_WriteUInt32(numStreams);            // dwStreams
	_WriteUInt32(fWidth * fHeight * 3); // dwSuggestedBufferSize
	_WriteUInt32(fWidth);               // dwWidth
	_WriteUInt32(fHeight);              // dwHeight
	_WriteUInt32(0);                    // dwReserved[0]
	_WriteUInt32(0);                    // dwReserved[1]
	_WriteUInt32(0);                    // dwReserved[2]
	_WriteUInt32(0);                    // dwReserved[3]

	// --- Video Stream (stream 0) ---
	// LIST 'strl'
	_WriteFourCC("LIST");
	_WriteUInt32(videoStrlSize);
	_WriteFourCC("strl");

	// 'strh' - Stream header (56 bytes)
	_WriteFourCC("strh");
	_WriteUInt32(56);
	_WriteFourCC("vids");               // fccType
	_WriteFourCC("MJPG");               // fccHandler
	_WriteUInt32(0);                    // dwFlags
	_WriteUInt16(0);                    // wPriority
	_WriteUInt16(0);                    // wLanguage
	_WriteUInt32(0);                    // dwInitialFrames
	_WriteUInt32(1000);                 // dwScale
	_WriteUInt32((uint32)(fFPS * 1000)); // dwRate
	_WriteUInt32(0);                    // dwStart
	_WriteUInt32(0);                    // dwLength (filled later)
	_WriteUInt32(fWidth * fHeight * 3); // dwSuggestedBufferSize
	_WriteUInt32(0xFFFFFFFF);           // dwQuality
	_WriteUInt32(0);                    // dwSampleSize
	_WriteUInt16(0);                    // rcFrame left
	_WriteUInt16(0);                    // rcFrame top
	_WriteUInt16((uint16)fWidth);       // rcFrame right
	_WriteUInt16((uint16)fHeight);      // rcFrame bottom

	// 'strf' - Stream format (BITMAPINFOHEADER, 40 bytes)
	_WriteFourCC("strf");
	_WriteUInt32(40);
	_WriteUInt32(40);                   // biSize
	_WriteUInt32(fWidth);               // biWidth
	_WriteUInt32(fHeight);              // biHeight
	_WriteUInt16(1);                    // biPlanes
	_WriteUInt16(24);                   // biBitCount
	_WriteFourCC("MJPG");               // biCompression
	_WriteUInt32(fWidth * fHeight * 3); // biSizeImage
	_WriteUInt32(0);                    // biXPelsPerMeter
	_WriteUInt32(0);                    // biYPelsPerMeter
	_WriteUInt32(0);                    // biClrUsed
	_WriteUInt32(0);                    // biClrImportant

	// --- Audio Stream (stream 1, optional) ---
	if (fHasAudio) {
		uint32 blockAlign = fChannels * (fBitsPerSample / 8);
		uint32 avgBytesPerSec = (uint32)(fSampleRate * blockAlign);

		_WriteFourCC("LIST");
		_WriteUInt32(audioStrlSize);
		_WriteFourCC("strl");

		// 'strh' - Audio stream header (56 bytes)
		_WriteFourCC("strh");
		_WriteUInt32(56);
		_WriteFourCC("auds");               // fccType
		_WriteUInt32(0);                    // fccHandler (0 for PCM audio)
		_WriteUInt32(0);                    // dwFlags
		_WriteUInt16(0);                    // wPriority
		_WriteUInt16(0);                    // wLanguage
		_WriteUInt32(0);                    // dwInitialFrames
		_WriteUInt32(blockAlign);            // dwScale
		_WriteUInt32(avgBytesPerSec);        // dwRate
		_WriteUInt32(0);                    // dwStart
		_WriteUInt32(0);                    // dwLength (filled later)
		_WriteUInt32(avgBytesPerSec);        // dwSuggestedBufferSize
		_WriteUInt32(0xFFFFFFFF);           // dwQuality
		_WriteUInt32(blockAlign);            // dwSampleSize
		_WriteUInt16(0);                    // rcFrame (unused for audio)
		_WriteUInt16(0);
		_WriteUInt16(0);
		_WriteUInt16(0);

		// 'strf' - Audio stream format (WAVEFORMATEX, 18 bytes)
		_WriteFourCC("strf");
		_WriteUInt32(18);
		_WriteUInt16(1);                    // wFormatTag (WAVE_FORMAT_PCM)
		_WriteUInt16((uint16)fChannels);     // nChannels
		_WriteUInt32((uint32)fSampleRate);   // nSamplesPerSec
		_WriteUInt32(avgBytesPerSec);        // nAvgBytesPerSec
		_WriteUInt16((uint16)blockAlign);     // nBlockAlign
		_WriteUInt16((uint16)fBitsPerSample); // wBitsPerSample
		_WriteUInt16(0);                    // cbSize (no extra data)
	}

	// LIST 'movi'
	_WriteFourCC("LIST");
	fMoviListStart = fFile.Position();
	_WriteUInt32(0);  // movi list size (filled later)
	_WriteFourCC("movi");

	fMoviDataStart = fFile.Position();

	return B_OK;
}


status_t
VideoRecorder::_FinalizeAVI()
{
	// End of movi data
	off_t moviEnd = fFile.Position();

	// Fix movi LIST size
	uint32 moviSize = (uint32)(moviEnd - fMoviListStart - 4);
	fFile.Seek(fMoviListStart, SEEK_SET);
	_WriteUInt32(moviSize);

	// Seek to end to write index
	fFile.Seek(moviEnd, SEEK_SET);

	// Write 'idx1' index - must reflect actual chunk order in the file
	// Build a merged index sorted by file offset
	struct IndexItem {
		off_t		offset;
		uint32		size;
		bool		isVideo;
	};

	int32 totalEntries = fVideoIndex.CountItems() + fAudioIndex.CountItems();
	BObjectList<IndexItem> merged(totalEntries);

	for (int32 i = 0; i < fVideoIndex.CountItems(); i++) {
		AVIIndexEntry* e = fVideoIndex.ItemAt(i);
		IndexItem* item = new IndexItem();
		item->offset = e->offset;
		item->size = e->size;
		item->isVideo = true;
		merged.AddItem(item);
	}

	for (int32 i = 0; i < fAudioIndex.CountItems(); i++) {
		AVIIndexEntry* e = fAudioIndex.ItemAt(i);
		IndexItem* item = new IndexItem();
		item->offset = e->offset;
		item->size = e->size;
		item->isVideo = false;
		merged.AddItem(item);
	}

	// Sort by file offset to match actual chunk order
	merged.SortItems([](const IndexItem* a, const IndexItem* b) -> int {
		if (a->offset < b->offset) return -1;
		if (a->offset > b->offset) return 1;
		return 0;
	});

	_WriteFourCC("idx1");
	_WriteUInt32(totalEntries * 16);

	for (int32 i = 0; i < merged.CountItems(); i++) {
		IndexItem* item = merged.ItemAt(i);
		_WriteFourCC(item->isVideo ? "00dc" : "01wb");
		_WriteUInt32(item->isVideo ? 0x10 : 0x00);  // KEYFRAME for video
		_WriteUInt32((uint32)item->offset);
		_WriteUInt32(item->size);
	}

	// Fix RIFF size
	off_t fileEnd = fFile.Position();
	uint32 riffSize = (uint32)(fileEnd - 8);
	fFile.Seek(4, SEEK_SET);
	_WriteUInt32(riffSize);

	// Fix total frames in avih
	fFile.Seek(48, SEEK_SET);
	_WriteUInt32(fFrameCount);

	// Fix video stream length in strh (offset 140 = 12+8+64+12+8+36)
	fFile.Seek(140, SEEK_SET);
	_WriteUInt32(fFrameCount);

	// Fix audio stream length in strh if present
	if (fHasAudio) {
		uint32 blockAlign = fChannels * (fBitsPerSample / 8);
		uint32 audioSamples = blockAlign > 0 ? fTotalAudioBytes / blockAlign : 0;
		// Audio strh dwLength is at: avih end + video strl size + audio strh offset
		// Video strl: LIST(8) + 4 + strh(8+56) + strf(8+40) = 124
		// Audio strh dwLength is at fixed offset 264:
		// RIFF(8) + "AVI "(4) + LIST(8) + "hdrl"(4) + avih(64)
		// + video_strl(LIST(8)+"strl"(4)+strh(64)+strf(48)=124)
		// + audio LIST(8) + "strl"(4) + "strh"(8) + 32 bytes into strh data
		// = 12 + 12 + 64 + 124 + 12 + 8 + 32 = 264
		fFile.Seek(264, SEEK_SET);
		_WriteUInt32(audioSamples);
	}

	return B_OK;
}


status_t
VideoRecorder::_CompressFrameToJPEG(BBitmap* bitmap,
	uint8** outBuffer, unsigned long* outSize)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return B_BAD_VALUE;

	*outBuffer = NULL;
	*outSize = 0;

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);

	jmp_buf jumpBuffer;
	cinfo.client_data = &jumpBuffer;
	jerr.error_exit = [](j_common_ptr cinfo) {
		jmp_buf* jb = static_cast<jmp_buf*>(cinfo->client_data);
		longjmp(*jb, 1);
	};

	if (setjmp(jumpBuffer)) {
		jpeg_destroy_compress(&cinfo);
		if (*outBuffer != NULL) {
			free(*outBuffer);
			*outBuffer = NULL;
		}
		return B_ERROR;
	}

	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, outBuffer, outSize);

	cinfo.image_width = bitmap->Bounds().IntegerWidth() + 1;
	cinfo.image_height = bitmap->Bounds().IntegerHeight() + 1;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, fJPEGQuality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	// Convert BGRA (Haiku B_RGB32) to RGB row by row
	int32 width = cinfo.image_width;
	int32 srcBytesPerRow = bitmap->BytesPerRow();
	const uint8* srcBits = static_cast<const uint8*>(bitmap->Bits());

	uint8* rowBuffer = new uint8[width * 3];

	while (cinfo.next_scanline < cinfo.image_height) {
		const uint8* srcRow = srcBits + cinfo.next_scanline * srcBytesPerRow;
		for (int32 x = 0; x < width; x++) {
			rowBuffer[x * 3 + 0] = srcRow[x * 4 + 2];  // R
			rowBuffer[x * 3 + 1] = srcRow[x * 4 + 1];  // G
			rowBuffer[x * 3 + 2] = srcRow[x * 4 + 0];  // B
		}
		JSAMPROW row = rowBuffer;
		jpeg_write_scanlines(&cinfo, &row, 1);
	}

	delete[] rowBuffer;

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	return B_OK;
}


void
VideoRecorder::_WriteFourCC(const char* fourcc)
{
	fFile.Write(fourcc, 4);
}


void
VideoRecorder::_WriteUInt32(uint32 value)
{
	// AVI is little-endian
	uint8 buf[4];
	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	buf[2] = (value >> 16) & 0xFF;
	buf[3] = (value >> 24) & 0xFF;
	fFile.Write(buf, 4);
}


void
VideoRecorder::_WriteUInt16(uint16 value)
{
	uint8 buf[2];
	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	fFile.Write(buf, 2);
}
