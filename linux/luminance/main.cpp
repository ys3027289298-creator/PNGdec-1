//
// Luminance (Y) extraction demo + verification for the PNGdec library
//
// Demonstrates both usages of the new API:
//   1) png.decodeLuminance()  - whole-image luminance buffer
//   2) png.getLineAsLuminance() - per-line, from a PNGDRAW callback
//
// Reports:
//   - error of the luminance map vs. hand-computed weighted Y
//   - decode time of luminance-only vs. full-color decode (ratio)
//   - luminance consistency with and without alpha retention
//   - error returned for unsupported bit depths
//   - confirmation that the original decode output is unchanged
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/PNGdec.h"
#include "rgb_sample.h"
#include "rgba_sample.h"

PNG png; // static instance of the decoder

static double now_ms(void)
{
struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

// Per-line usage: called by the decoder for each decoded line
static int PNGDrawLum(PNGDRAW *pDraw)
{
uint8_t *pY = (uint8_t *)pDraw->pUser;
    if (png.getLineAsLuminance(pDraw, &pY[pDraw->y * pDraw->iWidth], NULL) != PNG_SUCCESS)
        return 0; // stop on unsupported bit depth
    return 1; // continue decoding
}

int main(int argc, const char *argv[])
{
const char *szLowBpp = (argc > 1) ? argv[1] : "test_images/homer_2bpp.png";
int i, rc, W, H, iFailures = 0;
uint8_t *pColor, *pColor2, *pY, *pY2, *pA;
double t0, tColor, tLum;

    printf("=== PNGdec luminance extraction test ===\n");

    /* ---------- RGB image: error vs hand-computed Y + timing ratio ---------- */
    if (png.openRAM((uint8_t *)rgb_sample_png, rgb_sample_png_len, NULL) != PNG_SUCCESS) {
        printf("FAIL: cannot open RGB sample\n");
        return 1;
    }
    W = png.getWidth(); H = png.getHeight();
    printf("RGB sample: %d x %d, %d bpp, pixel type %d\n", W, H, png.getBpp(), png.getPixelType());
    pColor = (uint8_t *)malloc(png.getBufferSize());
    pColor2 = (uint8_t *)malloc(png.getBufferSize());
    pY = (uint8_t *)malloc(W * H);
    pY2 = (uint8_t *)malloc(W * H);
    pA = (uint8_t *)malloc(W * H);

    // reference full-color decode
    png.setBuffer(pColor);
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) { printf("FAIL: color decode rc=%d\n", rc); return 1; }

    // whole-image luminance, no alpha
    rc = png.decodeLuminance(pY, NULL);
    if (rc != PNG_SUCCESS) { printf("FAIL: decodeLuminance rc=%d\n", rc); return 1; }

    // (1) error vs hand-computed weighted luminance (BT.601, float reference)
    {
        double dMax = 0, dSum = 0;
        for (i = 0; i < W * H; i++) {
            double yref = 0.299 * pColor[i*3] + 0.587 * pColor[i*3+1] + 0.114 * pColor[i*3+2];
            double d = yref - pY[i];
            if (d < 0) d = -d;
            if (d > dMax) dMax = d;
            dSum += d;
        }
        printf("[RGB] Y error vs hand-computed 0.299/0.587/0.114: max=%.3f mean=%.4f (gray levels)\n",
               dMax, dSum / (W * H));
        if (dMax > 1.5) { printf("FAIL: luminance error too large\n"); iFailures++; }
    }

    // (2)+(3) timing: luminance-only vs full-color decode
    // interleaved rounds, best-of to reduce CPU frequency scaling noise
    {
        const int iLoops = 200, iRounds = 10;
        int round;
        double tBest;
        tColor = tLum = 1e9;
        for (round = 0; round < iRounds; round++) {
            t0 = now_ms();
            for (i = 0; i < iLoops; i++) {
                png.setBuffer(pColor2);
                png.decode(NULL, 0);
            }
            tBest = (now_ms() - t0) / iLoops;
            if (tBest < tColor) tColor = tBest;
            t0 = now_ms();
            for (i = 0; i < iLoops; i++)
                png.decodeLuminance(pY2, NULL);
            tBest = (now_ms() - t0) / iLoops;
            if (tBest < tLum) tLum = tBest;
        }
        printf("[RGB] decode time: luminance-only=%.3f ms, full-color=%.3f ms, ratio=%.3f\n",
               tLum, tColor, tLum / tColor);
        printf("[RGB] note: PNG inflate+defilter must process every byte, so luminance\n");
        printf("      decode time ~= full-color time; the win is memory: %d bytes vs %d\n",
               W * H, W * H * 3);
    }

    // per-line callback usage must match whole-image usage exactly
    {
        PNG png2;
        memset(pY2, 0, W * H);
        if (png2.openRAM((uint8_t *)rgb_sample_png, rgb_sample_png_len, PNGDrawLum) != PNG_SUCCESS) {
            printf("FAIL: cannot reopen RGB sample\n"); return 1;
        }
        rc = png2.decode(pY2, 0);
        if (rc != PNG_SUCCESS || memcmp(pY, pY2, W * H) != 0) {
            printf("FAIL: per-line luminance differs from whole-image\n"); iFailures++;
        } else {
            printf("[RGB] per-line callback vs whole-image luminance: identical\n");
        }
    }

    // (4a) alpha retention consistency on RGB (no alpha channel -> 255)
    rc = png.decodeLuminance(pY2, pA);
    if (rc != PNG_SUCCESS || memcmp(pY, pY2, W * H) != 0) {
        printf("FAIL: Y changed when alpha retained (RGB)\n"); iFailures++;
    } else {
        for (i = 0; i < W * H && pA[i] == 255; i++);
        printf("[RGB] Y identical with/without alpha retention; alpha all 255: %s\n",
               (i == W * H) ? "yes" : "NO");
        if (i != W * H) iFailures++;
    }

    // original decode output must be pixel-for-pixel unchanged
    png.setBuffer(pColor2);
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS || memcmp(pColor, pColor2, png.getBufferSize()) != 0) {
        printf("FAIL: original decode output changed\n"); iFailures++;
    } else {
        printf("[RGB] original decode output after luminance use: pixel-identical\n");
    }
    png.close();
    free(pColor); free(pColor2); free(pY); free(pY2); free(pA);

    /* ---------- RGBA image: alpha retention before/after consistency ---------- */
    if (png.openRAM((uint8_t *)rgba_sample_png, rgba_sample_png_len, NULL) != PNG_SUCCESS) {
        printf("FAIL: cannot open RGBA sample\n");
        return 1;
    }
    W = png.getWidth(); H = png.getHeight();
    printf("RGBA sample: %d x %d, %d bpp, pixel type %d, hasAlpha=%d\n",
           W, H, png.getBpp(), png.getPixelType(), png.hasAlpha());
    pColor = (uint8_t *)malloc(png.getBufferSize());
    pY = (uint8_t *)malloc(W * H);
    pY2 = (uint8_t *)malloc(W * H);
    pA = (uint8_t *)malloc(W * H);
    png.setBuffer(pColor);
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) { printf("FAIL: RGBA color decode rc=%d\n", rc); return 1; }
    png.decodeLuminance(pY, NULL);      // without alpha
    png.decodeLuminance(pY2, pA);       // with alpha retained
    if (memcmp(pY, pY2, W * H) != 0) {
        printf("FAIL: Y changed when alpha retained (RGBA)\n"); iFailures++;
    } else {
        printf("[RGBA] Y identical with/without alpha retention\n");
    }
    {   // retained alpha must match the alpha channel of the full-color decode
        int iBad = 0;
        double dMax = 0;
        for (i = 0; i < W * H; i++) {
            if (pA[i] != pColor[i*4+3]) iBad++;
            double yref = 0.299 * pColor[i*4] + 0.587 * pColor[i*4+1] + 0.114 * pColor[i*4+2];
            double d = yref - pY[i];
            if (d < 0) d = -d;
            if (d > dMax) dMax = d;
        }
        printf("[RGBA] retained alpha vs color decode alpha: %d mismatches; Y max err=%.3f\n", iBad, dMax);
        if (iBad || dMax > 1.5) iFailures++;
    }
    png.close();
    free(pColor); free(pY); free(pY2); free(pA);

    /* ---------- unsupported bit depth must report an error ---------- */
    {
        FILE *f = fopen(szLowBpp, "rb");
        uint8_t *pData;
        long lSize;
        if (!f) { printf("FAIL: cannot open %s\n", szLowBpp); return 1; }
        fseek(f, 0, SEEK_END); lSize = ftell(f); fseek(f, 0, SEEK_SET);
        pData = (uint8_t *)malloc(lSize);
        fread(pData, 1, lSize, f);
        fclose(f);
        if (png.openRAM(pData, (int)lSize, NULL) != PNG_SUCCESS) {
            printf("FAIL: cannot open %s\n", szLowBpp); return 1;
        }
        printf("%s: %d x %d, %d bpp\n", szLowBpp, png.getWidth(), png.getHeight(), png.getBpp());
        pY = (uint8_t *)malloc(png.getWidth() * png.getHeight());
        rc = png.decodeLuminance(pY, NULL);
        printf("[2-bit] decodeLuminance returned %d (PNG_UNSUPPORTED_FEATURE=%d): %s\n",
               rc, PNG_UNSUPPORTED_FEATURE,
               (rc == PNG_UNSUPPORTED_FEATURE && png.getLastError() == PNG_UNSUPPORTED_FEATURE) ? "PASS" : "FAIL");
        if (rc != PNG_UNSUPPORTED_FEATURE) iFailures++;
        png.close();
        free(pY); free(pData);
    }

    printf("=== %s ===\n", (iFailures == 0) ? "ALL CHECKS PASSED" : "FAILURES PRESENT");
    return (iFailures == 0) ? 0 : 1;
}
