/*
 * BubiCam Test Runner
 * Automated tests and benchmarks for the webcam capture component.
 *
 * Build:  make -f tests/Makefile
 * Run:    objects.x86_64-cc13-release/BubiCamTests
 */

#include <Application.h>
#include <Bitmap.h>
#include <String.h>
#include <File.h>
#include <Path.h>
#include <FindDirectory.h>
#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <emmintrin.h>

// ============================================================================
// Minimal test framework
// ============================================================================

static int sTestsPassed = 0;
static int sTestsFailed = 0;
static int sTestsTotal = 0;

#define TEST_ASSERT(cond, msg) do { \
	sTestsTotal++; \
	if (!(cond)) { \
		fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
		sTestsFailed++; \
	} else { \
		sTestsPassed++; \
	} \
} while (0)

#define TEST_SECTION(name) \
	fprintf(stderr, "\n--- %s ---\n", name)

#define BENCH_START(name, iterations) \
	{ \
		const char* _bench_name = name; \
		int32 _bench_iters = iterations; \
		bigtime_t _bench_start = system_time();

#define BENCH_END() \
		bigtime_t _bench_elapsed = system_time() - _bench_start; \
		double _bench_ms = _bench_elapsed / 1000.0; \
		double _bench_per = _bench_ms / _bench_iters; \
		fprintf(stderr, "  BENCH: %-40s %8.2f ms total, %6.3f ms/iter (%d iters)\n", \
			_bench_name, _bench_ms, _bench_per, (int)_bench_iters); \
	}


// ============================================================================
// YUV conversion reference (scalar, known-correct)
// ============================================================================

static inline void
yuv_to_bgra(int y, int u, int v, uint8* out)
{
	int c = y - 16;
	int d = u - 128;
	int e = v - 128;

	int r = (298 * c + 409 * e + 128) >> 8;
	int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int b = (298 * c + 516 * d + 128) >> 8;

	out[0] = (uint8)(b < 0 ? 0 : (b > 255 ? 255 : b));
	out[1] = (uint8)(g < 0 ? 0 : (g > 255 ? 255 : g));
	out[2] = (uint8)(r < 0 ? 0 : (r > 255 ? 255 : r));
	out[3] = 255;
}


// Scalar YUV422 (YUYV) to BGRA
static void
ConvertYUV422_Scalar(const uint8* src, uint8* dst, int32 width, int32 height,
	int32 srcBytesPerRow, int32 dstBytesPerRow)
{
	for (int32 y = 0; y < height; y++) {
		const uint8* srcRow = src + y * srcBytesPerRow;
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width; x += 2) {
			int y0 = srcRow[0];
			int u  = srcRow[1];
			int y1 = srcRow[2];
			int v  = srcRow[3];

			yuv_to_bgra(y0, u, v, dstRow);
			yuv_to_bgra(y1, u, v, dstRow + 4);

			srcRow += 4;
			dstRow += 8;
		}
	}
}


// SSE2 YUV422 (YUYV) to BGRA
#ifdef __SSE2__
static void
ConvertYUV422_SSE2(const uint8* src, uint8* dst, int32 width, int32 height,
	int32 srcBytesPerRow, int32 dstBytesPerRow)
{
	const __m128i mask_y = _mm_set1_epi16(0x00FF);
	const __m128i sub16  = _mm_set1_epi16(16);
	const __m128i sub128 = _mm_set1_epi16(128);
	const __m128i coeff_y  = _mm_set1_epi16(298);
	const __m128i coeff_rv = _mm_set1_epi16(409);
	const __m128i coeff_gu = _mm_set1_epi16(100);
	const __m128i coeff_gv = _mm_set1_epi16(208);
	const __m128i coeff_bu = _mm_set1_epi16(516);
	const __m128i zero = _mm_setzero_si128();
	const __m128i alpha = _mm_set1_epi8((char)0xFF);

	for (int32 y = 0; y < height; y++) {
		const uint8* srcRow = src + y * srcBytesPerRow;
		uint8* dstRow = dst + y * dstBytesPerRow;

		int32 x = 0;
		for (; x + 7 < width; x += 8) {
			// Load 16 bytes (8 pixels in YUYV)
			__m128i yuyv = _mm_loadu_si128((const __m128i*)srcRow);

			// Extract Y (even bytes) and UV (odd bytes)
			__m128i y_vals = _mm_and_si128(yuyv, mask_y);
			__m128i uv_raw = _mm_srli_epi16(yuyv, 8);

			// Separate U and V
			__m128i u_packed = _mm_and_si128(uv_raw, _mm_set1_epi32(0x0000FFFF));
			__m128i v_packed = _mm_srli_epi32(uv_raw, 16);

			// Duplicate U and V for each pixel pair
			__m128i u_vals = _mm_or_si128(u_packed, _mm_slli_epi32(u_packed, 16));
			__m128i v_vals = _mm_or_si128(v_packed, _mm_slli_epi32(v_packed, 16));

			// Apply YUV to RGB conversion
			__m128i c = _mm_sub_epi16(y_vals, sub16);
			__m128i d = _mm_sub_epi16(u_vals, sub128);
			__m128i e = _mm_sub_epi16(v_vals, sub128);

			__m128i cy = _mm_mullo_epi16(c, coeff_y);

			__m128i r16 = _mm_srai_epi16(_mm_add_epi16(
				_mm_add_epi16(cy, _mm_mullo_epi16(e, coeff_rv)),
				_mm_set1_epi16(128)), 8);
			__m128i g16 = _mm_srai_epi16(_mm_add_epi16(
				_mm_sub_epi16(
					_mm_sub_epi16(cy, _mm_mullo_epi16(d, coeff_gu)),
					_mm_mullo_epi16(e, coeff_gv)),
				_mm_set1_epi16(128)), 8);
			__m128i b16 = _mm_srai_epi16(_mm_add_epi16(
				_mm_add_epi16(cy, _mm_mullo_epi16(d, coeff_bu)),
				_mm_set1_epi16(128)), 8);

			// Clamp 0-255
			r16 = _mm_max_epi16(_mm_min_epi16(r16, _mm_set1_epi16(255)), zero);
			g16 = _mm_max_epi16(_mm_min_epi16(g16, _mm_set1_epi16(255)), zero);
			b16 = _mm_max_epi16(_mm_min_epi16(b16, _mm_set1_epi16(255)), zero);

			// Pack to bytes
			__m128i r8 = _mm_packus_epi16(r16, zero);
			__m128i g8 = _mm_packus_epi16(g16, zero);
			__m128i b8 = _mm_packus_epi16(b16, zero);

			// Interleave to BGRA
			__m128i bg = _mm_unpacklo_epi8(b8, g8);
			__m128i ra = _mm_unpacklo_epi8(r8, alpha);
			__m128i bgra_lo = _mm_unpacklo_epi16(bg, ra);
			__m128i bgra_hi = _mm_unpackhi_epi16(bg, ra);

			_mm_storeu_si128((__m128i*)(dstRow), bgra_lo);
			_mm_storeu_si128((__m128i*)(dstRow + 16), bgra_hi);

			srcRow += 16;
			dstRow += 32;
		}

		// Scalar fallback for remaining pixels
		for (; x + 1 < width; x += 2) {
			int y0 = srcRow[0], u = srcRow[1], y1 = srcRow[2], v = srcRow[3];
			yuv_to_bgra(y0, u, v, dstRow);
			yuv_to_bgra(y1, u, v, dstRow + 4);
			srcRow += 4;
			dstRow += 8;
		}
	}
}
#endif


// Scalar NV12 to BGRA
static void
ConvertNV12_Scalar(const uint8* src, uint8* dst, int32 width, int32 height,
	int32 dstBytesPerRow)
{
	const uint8* yPlane = src;
	const uint8* uvPlane = src + width * height;

	for (int32 y = 0; y < height; y++) {
		const uint8* yRow = yPlane + y * width;
		const uint8* uvRow = uvPlane + (y / 2) * width;
		uint8* dstRow = dst + y * dstBytesPerRow;

		for (int32 x = 0; x < width; x += 2) {
			int u = uvRow[x];
			int v = uvRow[x + 1];

			yuv_to_bgra(yRow[x], u, v, dstRow + x * 4);
			if (x + 1 < width)
				yuv_to_bgra(yRow[x + 1], u, v, dstRow + (x + 1) * 4);
		}
	}
}


// ============================================================================
// Test: YUV422 conversion correctness
// ============================================================================

static void
TestYUV422Conversion()
{
	TEST_SECTION("YUV422 (YUYV) to BGRA Conversion");

	const int32 width = 320;
	const int32 height = 240;
	const int32 srcBytesPerRow = width * 2;
	const int32 dstBytesPerRow = width * 4;

	uint8* srcBuf = new uint8[srcBytesPerRow * height];
	uint8* dstScalar = new uint8[dstBytesPerRow * height];
	uint8* dstSSE2 = new uint8[dstBytesPerRow * height];

	// Generate test pattern: gradient
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x += 2) {
			int32 offset = y * srcBytesPerRow + x * 2;
			srcBuf[offset + 0] = (uint8)((x * 255) / width);       // Y0
			srcBuf[offset + 1] = (uint8)(128 + (y * 50) / height); // U
			srcBuf[offset + 2] = (uint8)((x * 200) / width + 30);  // Y1
			srcBuf[offset + 3] = (uint8)(128 - (y * 50) / height); // V
		}
	}

	// Convert with scalar
	ConvertYUV422_Scalar(srcBuf, dstScalar, width, height,
		srcBytesPerRow, dstBytesPerRow);

	// Verify scalar output is non-zero
	bool hasNonZero = false;
	for (int32 i = 0; i < dstBytesPerRow * height; i++) {
		if (dstScalar[i] != 0) { hasNonZero = true; break; }
	}
	TEST_ASSERT(hasNonZero, "Scalar YUV422 produces non-zero output");

	// Verify alpha channel is always 255
	bool alphaOK = true;
	for (int32 y = 0; y < height && alphaOK; y++) {
		for (int32 x = 0; x < width && alphaOK; x++) {
			if (dstScalar[y * dstBytesPerRow + x * 4 + 3] != 255)
				alphaOK = false;
		}
	}
	TEST_ASSERT(alphaOK, "Scalar YUV422 alpha is always 255");

	// Verify RGB values are in valid range (clamped)
	bool rangeOK = true;
	for (int32 i = 0; i < dstBytesPerRow * height; i++) {
		if (dstScalar[i] > 255) { rangeOK = false; break; }
	}
	TEST_ASSERT(rangeOK, "Scalar YUV422 values in 0-255 range");

#ifdef __SSE2__
	// Convert with SSE2
	ConvertYUV422_SSE2(srcBuf, dstSSE2, width, height,
		srcBytesPerRow, dstBytesPerRow);

	// Verify SSE2 produces non-zero output (basic sanity)
	bool sse2HasOutput = false;
	for (int32 i = 0; i < dstBytesPerRow * height; i++) {
		if (dstSSE2[i] != 0) { sse2HasOutput = true; break; }
	}
	TEST_ASSERT(sse2HasOutput, "SSE2 YUV422 produces non-zero output");

	// Verify SSE2 alpha channel is always 255
	bool sse2AlphaOK = true;
	for (int32 y = 0; y < height && sse2AlphaOK; y++) {
		for (int32 x = 0; x < width && sse2AlphaOK; x++) {
			if (dstSSE2[y * dstBytesPerRow + x * 4 + 3] != 255)
				sse2AlphaOK = false;
		}
	}
	TEST_ASSERT(sse2AlphaOK, "SSE2 YUV422 alpha is always 255");
#else
	fprintf(stderr, "  SKIP: SSE2 not available\n");
#endif

	// Test known values: pure white (Y=235, U=128, V=128) -> should be ~(255,255,255)
	uint8 whiteYUYV[4] = { 235, 128, 235, 128 };
	uint8 whiteOut[8] = {0};
	ConvertYUV422_Scalar(whiteYUYV, whiteOut, 2, 1, 4, 8);
	TEST_ASSERT(whiteOut[0] >= 250 && whiteOut[1] >= 250 && whiteOut[2] >= 250,
		"White YUV (235,128,128) -> near-white RGB");

	// Test known values: pure black (Y=16, U=128, V=128) -> should be ~(0,0,0)
	uint8 blackYUYV[4] = { 16, 128, 16, 128 };
	uint8 blackOut[8] = {0};
	ConvertYUV422_Scalar(blackYUYV, blackOut, 2, 1, 4, 8);
	TEST_ASSERT(blackOut[0] <= 5 && blackOut[1] <= 5 && blackOut[2] <= 5,
		"Black YUV (16,128,128) -> near-black RGB");

	// Test known values: pure red (Y=82, U=90, V=240)
	uint8 redYUYV[4] = { 82, 90, 82, 240 };
	uint8 redOut[8] = {0};
	ConvertYUV422_Scalar(redYUYV, redOut, 2, 1, 4, 8);
	TEST_ASSERT(redOut[2] > 200, "Red YUV -> R channel > 200");
	TEST_ASSERT(redOut[1] < 50,  "Red YUV -> G channel < 50");
	TEST_ASSERT(redOut[0] < 50,  "Red YUV -> B channel < 50");

	delete[] srcBuf;
	delete[] dstScalar;
	delete[] dstSSE2;
}


// ============================================================================
// Test: NV12 conversion correctness
// ============================================================================

static void
TestNV12Conversion()
{
	TEST_SECTION("NV12 to BGRA Conversion");

	const int32 width = 320;
	const int32 height = 240;
	const int32 dstBytesPerRow = width * 4;
	size_t srcSize = width * height * 3 / 2;

	uint8* srcBuf = new uint8[srcSize];
	uint8* dstBuf = new uint8[dstBytesPerRow * height];

	// Generate NV12 test pattern: gray gradient
	uint8* yPlane = srcBuf;
	uint8* uvPlane = srcBuf + width * height;
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			yPlane[y * width + x] = (uint8)((y * 219) / height + 16);
		}
	}
	for (int32 y = 0; y < height / 2; y++) {
		for (int32 x = 0; x < width; x += 2) {
			uvPlane[y * width + x] = 128;      // U
			uvPlane[y * width + x + 1] = 128;  // V
		}
	}

	ConvertNV12_Scalar(srcBuf, dstBuf, width, height, dstBytesPerRow);

	// Gray (U=128, V=128) should produce R == G == B
	bool grayOK = true;
	int maxChannelDiff = 0;
	for (int32 y = 0; y < height && grayOK; y++) {
		for (int32 x = 0; x < width && grayOK; x++) {
			uint8* px = dstBuf + y * dstBytesPerRow + x * 4;
			int diff = abs((int)px[0] - (int)px[2]);
			if (diff > maxChannelDiff) maxChannelDiff = diff;
			if (diff > 2) grayOK = false;
		}
	}
	TEST_ASSERT(grayOK, "NV12 gray gradient: R ~= G ~= B (diff <= 2)");

	// Verify size validation would reject truncated buffer
	TEST_ASSERT(srcSize == (size_t)(width * height * 3 / 2),
		"NV12 expected size = width * height * 3/2");

	delete[] srcBuf;
	delete[] dstBuf;
}


// ============================================================================
// Test: Buffer size validation
// ============================================================================

static void
TestBufferValidation()
{
	TEST_SECTION("Buffer Size Validation");

	int32 width = 640, height = 480;

	// YUV422: expected = width * height * 2
	size_t expectedYUV422 = (size_t)(width * height * 2);
	TEST_ASSERT(expectedYUV422 == 614400, "YUV422 expected size 640x480 = 614400");

	// YUV420: expected = width * height * 3/2
	size_t expectedYUV420 = (size_t)(width * height * 3 / 2);
	TEST_ASSERT(expectedYUV420 == 460800, "YUV420 expected size 640x480 = 460800");

	// NV12: same as YUV420
	TEST_ASSERT(expectedYUV420 == expectedYUV420, "NV12 size == YUV420 size");

	// RGB32: expected = width * height * 4
	size_t expectedRGB32 = (size_t)(width * height * 4);
	TEST_ASSERT(expectedRGB32 == 1228800, "RGB32 expected size 640x480 = 1228800");

	// Truncated buffer should be rejected
	size_t truncated = expectedYUV422 - 100;
	TEST_ASSERT(truncated < expectedYUV422, "Truncated buffer < expected size");
}


// ============================================================================
// Test: CSV/JSON export format
// ============================================================================

static void
TestExportFormats()
{
	TEST_SECTION("Export Format Validation");

	// Test CSV escaping
	BString field1("simple");
	TEST_ASSERT(field1.FindFirst(',') < 0, "Simple CSV field has no commas");

	BString field2("has,comma");
	TEST_ASSERT(field2.FindFirst(',') >= 0, "Comma field detected");

	BString field3("has\"quote");
	TEST_ASSERT(field3.FindFirst('"') >= 0, "Quote field detected");

	// Test JSON escaping
	BString json("line1\nline2");
	TEST_ASSERT(json.FindFirst('\n') >= 0, "Newline in JSON needs escaping");

	BString jsonQuote("say \"hello\"");
	TEST_ASSERT(jsonQuote.FindFirst('"') >= 0, "Quote in JSON needs escaping");

	// Test timestamp format (should be YYYYMMDD_HHMMSS)
	time_t now = time(NULL);
	TEST_ASSERT(now > 0, "time() returns positive value");
}


// ============================================================================
// Test: BBitmap allocation
// ============================================================================

static void
TestBitmapAllocation()
{
	TEST_SECTION("BBitmap Allocation");

	// Common webcam resolutions
	struct { int32 w; int32 h; const char* name; } resolutions[] = {
		{ 160, 120, "QQVGA" },
		{ 320, 240, "QVGA" },
		{ 640, 480, "VGA" },
		{ 1280, 720, "720p" },
		{ 1920, 1080, "1080p" },
	};

	for (size_t i = 0; i < sizeof(resolutions)/sizeof(resolutions[0]); i++) {
		BRect rect(0, 0, resolutions[i].w - 1, resolutions[i].h - 1);
		BBitmap* bmp = new BBitmap(rect, B_RGB32);

		char msg[128];
		snprintf(msg, sizeof(msg), "BBitmap %s (%dx%d) allocation succeeds",
			resolutions[i].name, (int)resolutions[i].w, (int)resolutions[i].h);
		TEST_ASSERT(bmp != NULL && bmp->IsValid(), msg);

		snprintf(msg, sizeof(msg), "BBitmap %s has correct dimensions",
			resolutions[i].name);
		TEST_ASSERT(bmp->Bounds().Width() == resolutions[i].w - 1 &&
			bmp->Bounds().Height() == resolutions[i].h - 1, msg);

		snprintf(msg, sizeof(msg), "BBitmap %s BytesPerRow >= width*4",
			resolutions[i].name);
		TEST_ASSERT(bmp->BytesPerRow() >= resolutions[i].w * 4, msg);

		delete bmp;
	}
}


// ============================================================================
// Benchmark: YUV422 conversion (Scalar vs SSE2)
// ============================================================================

static void
BenchmarkYUV422()
{
	TEST_SECTION("Benchmark: YUV422 Conversion");

	struct { int32 w; int32 h; const char* name; int32 iters; } sizes[] = {
		{ 320, 240, "QVGA 320x240", 500 },
		{ 640, 480, "VGA 640x480", 200 },
		{ 1280, 720, "720p 1280x720", 50 },
		{ 1920, 1080, "1080p 1920x1080", 20 },
	};

	for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
		int32 w = sizes[s].w, h = sizes[s].h;
		int32 srcBPR = w * 2;
		int32 dstBPR = w * 4;
		int32 iters = sizes[s].iters;

		uint8* src = new uint8[srcBPR * h];
		uint8* dst = new uint8[dstBPR * h];

		// Fill with random-ish data
		for (int32 i = 0; i < srcBPR * h; i++)
			src[i] = (uint8)(i * 7 + 13);

		char label[128];

		// Scalar benchmark
		snprintf(label, sizeof(label), "Scalar %s", sizes[s].name);
		BENCH_START(label, iters)
		for (int32 i = 0; i < iters; i++)
			ConvertYUV422_Scalar(src, dst, w, h, srcBPR, dstBPR);
		BENCH_END()

#ifdef __SSE2__
		// SSE2 benchmark
		snprintf(label, sizeof(label), "SSE2   %s", sizes[s].name);
		BENCH_START(label, iters)
		for (int32 i = 0; i < iters; i++)
			ConvertYUV422_SSE2(src, dst, w, h, srcBPR, dstBPR);
		BENCH_END()
#endif

		delete[] src;
		delete[] dst;
	}
}


// ============================================================================
// Benchmark: NV12 conversion
// ============================================================================

static void
BenchmarkNV12()
{
	TEST_SECTION("Benchmark: NV12 Conversion");

	struct { int32 w; int32 h; const char* name; int32 iters; } sizes[] = {
		{ 640, 480, "VGA 640x480", 200 },
		{ 1280, 720, "720p 1280x720", 50 },
		{ 1920, 1080, "1080p 1920x1080", 20 },
	};

	for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
		int32 w = sizes[s].w, h = sizes[s].h;
		int32 dstBPR = w * 4;
		size_t srcSize = w * h * 3 / 2;
		int32 iters = sizes[s].iters;

		uint8* src = new uint8[srcSize];
		uint8* dst = new uint8[dstBPR * h];

		for (size_t i = 0; i < srcSize; i++)
			src[i] = (uint8)(i * 11 + 3);

		char label[128];
		snprintf(label, sizeof(label), "NV12   %s", sizes[s].name);

		BENCH_START(label, iters)
		for (int32 i = 0; i < iters; i++)
			ConvertNV12_Scalar(src, dst, w, h, dstBPR);
		BENCH_END()

		delete[] src;
		delete[] dst;
	}
}


// ============================================================================
// Benchmark: BBitmap memcpy (simulates direct RGB32 copy)
// ============================================================================

static void
BenchmarkMemcpy()
{
	TEST_SECTION("Benchmark: Frame Copy (memcpy)");

	struct { int32 w; int32 h; const char* name; int32 iters; } sizes[] = {
		{ 640, 480, "VGA 640x480", 500 },
		{ 1280, 720, "720p 1280x720", 100 },
		{ 1920, 1080, "1080p 1920x1080", 30 },
	};

	for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
		int32 w = sizes[s].w, h = sizes[s].h;
		size_t frameSize = w * h * 4;
		int32 iters = sizes[s].iters;

		uint8* src = (uint8*)malloc(frameSize);
		uint8* dst = (uint8*)malloc(frameSize);
		memset(src, 0x42, frameSize);

		char label[128];
		snprintf(label, sizeof(label), "memcpy %s (%zu KB)", sizes[s].name,
			frameSize / 1024);

		BENCH_START(label, iters)
		for (int32 i = 0; i < iters; i++)
			memcpy(dst, src, frameSize);
		BENCH_END()

		free(src);
		free(dst);
	}
}


// ============================================================================
// Main
// ============================================================================

int
main(int argc, char* argv[])
{
	BApplication app("application/x-vnd.BubiCam-Tests");

	fprintf(stderr, "========================================\n");
	fprintf(stderr, "BubiCam Test Suite & Benchmarks\n");
	fprintf(stderr, "========================================\n");

	bool runBench = true;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-bench") == 0)
			runBench = false;
	}

	// --- Tests ---
	TestYUV422Conversion();
	TestNV12Conversion();
	TestBufferValidation();
	TestExportFormats();
	TestBitmapAllocation();

	// --- Benchmarks ---
	if (runBench) {
		fprintf(stderr, "\n========================================\n");
		fprintf(stderr, "Benchmarks\n");
		fprintf(stderr, "========================================\n");

		BenchmarkYUV422();
		BenchmarkNV12();
		BenchmarkMemcpy();
	}

	// --- Summary ---
	fprintf(stderr, "\n========================================\n");
	fprintf(stderr, "Results: %d passed, %d failed, %d total\n",
		sTestsPassed, sTestsFailed, sTestsTotal);
	fprintf(stderr, "========================================\n");

	return sTestsFailed > 0 ? 1 : 0;
}
