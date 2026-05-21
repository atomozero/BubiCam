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
	fMoviListStart(0),
	fMoviDataStart(0),
	fIndex(20)
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
	fIndex.MakeEmpty();

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
	fIndex.AddItem(entry);

	fFrameCount++;
	free(jpegData);

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

	// RIFF header
	_WriteFourCC("RIFF");
	_WriteUInt32(0);  // File size - 8, filled in later
	_WriteFourCC("AVI ");

	// LIST 'hdrl'
	_WriteFourCC("LIST");
	_WriteUInt32(4 + 64 + 4 + 12 + 56 + 40);  // hdrl size
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
	_WriteUInt32(1);                    // dwStreams
	_WriteUInt32(fWidth * fHeight * 3); // dwSuggestedBufferSize
	_WriteUInt32(fWidth);               // dwWidth
	_WriteUInt32(fHeight);              // dwHeight
	_WriteUInt32(0);                    // dwReserved[0]
	_WriteUInt32(0);                    // dwReserved[1]
	_WriteUInt32(0);                    // dwReserved[2]
	_WriteUInt32(0);                    // dwReserved[3]

	// LIST 'strl'
	_WriteFourCC("LIST");
	_WriteUInt32(4 + 8 + 56 + 8 + 40);  // strl size
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

	// LIST 'movi'
	_WriteFourCC("LIST");
	fMoviListStart = 0;
	fMoviListStart = fFile.Position();
	_WriteUInt32(0);  // movi list size (filled later)
	_WriteFourCC("movi");

	fMoviDataStart = 0;
	fMoviDataStart = fFile.Position();

	return B_OK;
}


status_t
VideoRecorder::_FinalizeAVI()
{
	// End of movi data
	off_t moviEnd = 0;
	moviEnd = fFile.Position();

	// Fix movi LIST size
	uint32 moviSize = (uint32)(moviEnd - fMoviListStart - 4);
	fFile.Seek(fMoviListStart, SEEK_SET);
	_WriteUInt32(moviSize);

	// Seek to end to write index
	fFile.Seek(moviEnd, SEEK_SET);

	// Write 'idx1' index
	_WriteFourCC("idx1");
	_WriteUInt32(fIndex.CountItems() * 16);

	for (int32 i = 0; i < fIndex.CountItems(); i++) {
		AVIIndexEntry* entry = fIndex.ItemAt(i);
		_WriteFourCC("00dc");               // ckid
		_WriteUInt32(0x10);                 // dwFlags: AVIIF_KEYFRAME
		_WriteUInt32((uint32)entry->offset); // dwOffset (from movi start)
		_WriteUInt32(entry->size);          // dwSize
	}

	// Fix RIFF size
	off_t fileEnd = 0;
	fileEnd = fFile.Position();
	uint32 riffSize = (uint32)(fileEnd - 8);
	fFile.Seek(4, SEEK_SET);
	_WriteUInt32(riffSize);

	// Fix total frames in avih
	fFile.Seek(48, SEEK_SET);
	_WriteUInt32(fFrameCount);

	// Fix stream length in strh
	fFile.Seek(140, SEEK_SET);
	_WriteUInt32(fFrameCount);

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
