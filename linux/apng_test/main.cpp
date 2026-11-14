//
// APNG test harness for the PNGdec library
//
// Covers:
//  - static PNG decode regression (pixel hash must match the pre-APNG build)
//  - 5-frame APNG decode: per-frame composited pixels + delays
//  - corrupt frame data: must return an error without crashing (run with ASan)
//  - decode timing of a static PNG (before/after comparison)
//  - memory usage figures
//
// Build with -DSTATIC_ONLY to compile against the pre-APNG library.
//
#include <PNGdec.h>
#include <time.h>

static PNG png;
static uint64_t u64Hash; // FNV-1a hash accumulator for the draw callback
static int iLineCount;

static void hash_bytes(const uint8_t *p, int len)
{
    for (int i = 0; i < len; i++) {
        u64Hash ^= p[i];
        u64Hash *= 1099511628211ULL;
    }
}

static uint8_t *load_file(const char *szPath, int *pSize)
{
    FILE *f = fopen(szPath, "rb");
    uint8_t *pData;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *pSize = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    pData = (uint8_t *)malloc(*pSize);
    if (fread(pData, 1, *pSize, f) != (size_t)*pSize) {
        fclose(f);
        free(pData);
        return NULL;
    }
    fclose(f);
    return pData;
}

static int PNGDrawCB(PNGDRAW *pDraw)
{
    hash_bytes(pDraw->pPixels, pDraw->iPitch);
    iLineCount++;
    return 1;
}

// Decode a static PNG in buffer mode and print a hash of the pixels
static int test_static(const char *szPath, int iOptions)
{
    int iSize, rc;
    uint8_t *pData = load_file(szPath, &iSize);
    if (!pData) { printf("STATIC %s : cannot open\n", szPath); return 1; }
    rc = png.openRAM(pData, iSize, NULL);
    if (rc != PNG_SUCCESS) {
        printf("STATIC %s : open failed err=%d\n", szPath, png.getLastError());
        free(pData);
        return 1;
    }
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    rc = png.decode(NULL, iOptions);
    u64Hash = 14695981039346656037ULL;
    if (rc == PNG_SUCCESS)
        hash_bytes(png.getBuffer(), png.getBufferSize());
    printf("STATIC %s rc=%d err=%d %dx%d type=%d bpp=%d hash=%016llx\n", szPath,
           rc, png.getLastError(), png.getWidth(), png.getHeight(),
           png.getPixelType(), png.getBpp(), (unsigned long long)u64Hash);
    free(png.getBuffer());
    png.close();
    free(pData);
    return 0;
}

// Decode a static PNG in callback mode and print a hash of the lines
static int test_static_cb(const char *szPath)
{
    int iSize, rc;
    uint8_t *pData = load_file(szPath, &iSize);
    if (!pData) { printf("STATIC_CB %s : cannot open\n", szPath); return 1; }
    rc = png.openRAM(pData, iSize, PNGDrawCB);
    if (rc != PNG_SUCCESS) {
        printf("STATIC_CB %s : open failed err=%d\n", szPath, png.getLastError());
        free(pData);
        return 1;
    }
    u64Hash = 14695981039346656037ULL;
    iLineCount = 0;
    rc = png.decode(NULL, 0);
    printf("STATIC_CB %s rc=%d err=%d lines=%d hash=%016llx\n", szPath,
           rc, png.getLastError(), iLineCount, (unsigned long long)u64Hash);
    png.close();
    free(pData);
    return 0;
}

// Time repeated decodes of a static PNG
static int test_perf(const char *szPath, int iLoops)
{
    int iSize, rc, i;
    struct timespec ts0, ts1;
    uint8_t *pData = load_file(szPath, &iSize);
    if (!pData) { printf("PERF %s : cannot open\n", szPath); return 1; }
    rc = png.openRAM(pData, iSize, NULL);
    if (rc != PNG_SUCCESS) { free(pData); return 1; }
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    for (i = 0; i < iLoops; i++)
        rc = png.decode(NULL, 0);
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    double dMs = (ts1.tv_sec - ts0.tv_sec) * 1000.0 + (ts1.tv_nsec - ts0.tv_nsec) / 1e6;
    printf("PERF %s rc=%d loops=%d total_ms=%.1f per_decode_us=%.1f\n", szPath,
           rc, iLoops, dMs, dMs * 1000.0 / iLoops);
    free(png.getBuffer());
    png.close();
    free(pData);
    return 0;
}

#ifndef STATIC_ONLY
// Decode all frames of an APNG and dump the composited canvas of each frame
static int test_apng(const char *szPath, const char *szOutFile)
{
    int iSize, rc, i, x, y;
    uint8_t *pFrame0;
    uint8_t *pData = load_file(szPath, &iSize);
    FILE *fOut;
    if (!pData) { printf("APNG %s : cannot open\n", szPath); return 1; }
    rc = png.openRAM(pData, iSize, NULL);
    if (rc != PNG_SUCCESS) {
        printf("APNG %s : open failed err=%d\n", szPath, png.getLastError());
        free(pData);
        return 1;
    }
    printf("APNG %s frames=%d %dx%d type=%d\n", szPath, png.getFrameCount(),
           png.getWidth(), png.getHeight(), png.getPixelType());
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    pFrame0 = (uint8_t *)malloc(png.getBufferSize());
    fOut = fopen(szOutFile, "w");
    for (i = 0; i < png.getFrameCount(); i++) {
        rc = png.decode(NULL, 0);
        printf("APNG frame %d rc=%d err=%d delay=%d ms\n", i, rc, png.getLastError(), png.getFrameDelay());
        if (rc != PNG_SUCCESS) break;
        if (i == 0) memcpy(pFrame0, png.getBuffer(), png.getBufferSize());
        fprintf(fOut, "frame %d delay %d\n", i, png.getFrameDelay());
        for (y = 0; y < png.getHeight(); y++) {
            uint8_t *pRow = &png.getBuffer()[y * png.getWidth() * 4];
            for (x = 0; x < png.getWidth(); x++)
                fprintf(fOut, "%s%02x%02x%02x%02x", (x) ? " " : "",
                        pRow[x*4], pRow[x*4+1], pRow[x*4+2], pRow[x*4+3]);
            fprintf(fOut, "\n");
        }
    }
    fclose(fOut);
    // after the last frame, decode() must restart from frame 0
    rc = png.decode(NULL, 0);
    printf("APNG restart rc=%d delay=%d pixels_match_frame0=%s\n", rc, png.getFrameDelay(),
           (rc == PNG_SUCCESS && memcmp(pFrame0, png.getBuffer(), png.getBufferSize()) == 0) ? "yes" : "NO");
    free(pFrame0);
    free(png.getBuffer());
    png.close();
    free(pData);
    return 0;
}

// A corrupt APNG must fail with an error (and not crash / not go out of bounds)
static int test_corrupt(const char *szPath)
{
    int iSize, rc = 0, i;
    uint8_t *pData = load_file(szPath, &iSize);
    if (!pData) { printf("CORRUPT %s : cannot open\n", szPath); return 1; }
    rc = png.openRAM(pData, iSize, NULL);
    if (rc != PNG_SUCCESS) {
        printf("CORRUPT %s : rejected at open, err=%d -> PASS\n", szPath, png.getLastError());
        free(pData);
        return 0;
    }
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    for (i = 0; i < 12; i++) { // try to decode all frames until one fails
        rc = png.decode(NULL, 0);
        if (rc != PNG_SUCCESS || png.getLastError() != PNG_SUCCESS) break;
    }
    printf("CORRUPT %s : failed_at_frame=%d rc=%d err=%d -> %s\n", szPath, i, rc,
           png.getLastError(), (rc != PNG_SUCCESS || png.getLastError() != PNG_SUCCESS) ? "PASS" : "FAIL");
    free(png.getBuffer());
    png.close();
    free(pData);
    return 0;
}

// Cross-check: frame 0 of the APNG (SOURCE, full canvas) must equal the
// identical static RGBA image decoded through the classic code path
static int test_crosscheck(const char *szApng, const char *szStatic)
{
    int iSizeA, iSizeB;
    uint8_t *pA = load_file(szApng, &iSizeA);
    uint8_t *pB = load_file(szStatic, &iSizeB);
    uint8_t *pBufA, *pBufB;
    int iMatch = 0;
    if (!pA || !pB) return 1;
    png.openRAM(pA, iSizeA, NULL);
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    png.decode(NULL, 0);
    pBufA = (uint8_t *)malloc(png.getBufferSize());
    memcpy(pBufA, png.getBuffer(), png.getBufferSize());
    int iSizeA2 = png.getBufferSize();
    free(png.getBuffer());
    png.close();
    png.openRAM(pB, iSizeB, NULL);
    png.setBuffer((uint8_t *)malloc(png.getBufferSize()));
    png.decode(NULL, 0);
    pBufB = (uint8_t *)malloc(png.getBufferSize());
    memcpy(pBufB, png.getBuffer(), png.getBufferSize());
    iMatch = (iSizeA2 == png.getBufferSize() && memcmp(pBufA, pBufB, iSizeA2) == 0);
    printf("CROSSCHECK apng_frame0_vs_static_rgba pixels_match=%s\n", iMatch ? "yes" : "NO");
    free(pBufA); free(pBufB);
    free(png.getBuffer());
    png.close();
    free(pA); free(pB);
    return 0;
}
#endif // !STATIC_ONLY

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered so hangs/crashes are visible
    const char *szSamples = (argc > 1) ? argv[1] : "samples";
    const char *szRepo = (argc > 2) ? argv[2] : "../..";
    char szPath[512];

    printf("sizeof(PNGIMAGE)=%zu bytes\n", sizeof(PNGIMAGE));

    snprintf(szPath, sizeof(szPath), "%s/static_rgba.png", szSamples); test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/static_rgb.png", szSamples);  test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/static_gray.png", szSamples); test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/static_idx.png", szSamples);  test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/static_idx.png", szSamples);  test_static(szPath, PNG_CHECK_CRC);
    snprintf(szPath, sizeof(szPath), "%s/test_images/homer_2bpp.png", szRepo); test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/test_images/homer_2bpp.png", szRepo); test_static_cb(szPath);
    snprintf(szPath, sizeof(szPath), "%s/perf_small.png", szRepo);     test_static(szPath, 0);
    snprintf(szPath, sizeof(szPath), "%s/perf_small.png", szRepo);     test_perf(szPath, 100);

#ifndef STATIC_ONLY
    snprintf(szPath, sizeof(szPath), "%s/apng5.png", szSamples);
    test_apng(szPath, "apng_out.txt");
    snprintf(szPath, sizeof(szPath), "%s/apng_multifdat.png", szSamples);
    test_apng(szPath, "apng_out_multifdat.txt");
    snprintf(szPath, sizeof(szPath), "%s/apng_nodefault.png", szSamples);
    test_apng(szPath, "apng_out_nodefault.txt");
    snprintf(szPath, sizeof(szPath), "%s/apng5.png", szSamples);
    char szStatic[512];
    snprintf(szStatic, sizeof(szStatic), "%s/static_rgba.png", szSamples);
    test_crosscheck(szPath, szStatic);
    const char *szBad[] = {"apng_trunc.png", "apng_bitflip.png", "apng_badrect.png",
                           "apng_baddispose.png", "apng_zerow.png", "apng_extralines.png"};
    for (int i = 0; i < 6; i++) {
        snprintf(szPath, sizeof(szPath), "%s/%s", szSamples, szBad[i]);
        test_corrupt(szPath);
    }
#endif
    return 0;
}
