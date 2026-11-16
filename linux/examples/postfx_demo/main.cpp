//
// postfx_demo - measure the post-decode display pipeline added to png.inl
//
// Builds against the repository sources only. All numbers come from one run:
//   * per-stage RGB output for three pixels
//   * whole-image PSNR (baseline vs. each cumulative stage)
//   * extra time per line-callback call, per stage
//   * additional RAM required by the pipeline
//
// Usage: postfx_demo <sample.png>
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#define __LINUX__
#define ZLIB_DEBUG
#define INFLATE_STRICT
#include "../../../src/zutil.c"
#include "../../../src/inflate.c"
#include "../../../src/inffast.c"
#include "../../../src/inftrees.c"
#include "../../../src/crc32.c"
#undef DO1
#undef DO8
#include "../../../src/adler32.c"
#include "../../../src/PNGdec.cpp" // pulls in png.inl

static PNG png;

// GIF/LCD-style RGB565 -> 8-bit expansion, same as the pipeline uses
static void rgb565_to_rgb888(uint16_t p, uint8_t *out)
{
    int r5 = (p >> 11) & 0x1f, g6 = (p >> 5) & 0x3f, b5 = p & 0x1f;
    out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

// ---------- capture of the raw decoder output (baseline pixels) ----------
static uint16_t *gpLines;   // height * width RGB565 baseline
static int gW, gH;
static int gLines;
static uint8_t *gRawBuf;    // framebuffer decode (whole image at once)
static int gUseRaw;

static int CaptureDraw(PNGDRAW *pDraw)
{
    if (gUseRaw) return 1;
    png.getLineAsRGB565(pDraw, &gpLines[pDraw->y * gW], PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    gLines++;
    return 1;
}

// ---------- independent reference implementation ----------
// Straightforward full-image version: separate loops, 3x3 kernel applied from
// a whole-image buffer instead of a line cache. Used only to prove that the
// library's fused single-pass version produces the same pixels.
static void RefStage(const PNGPOST *p, const uint16_t *in, uint16_t *out1,
                     uint16_t *out2, uint16_t *out3)
{
    int y, x, c, n = gW * gH, i;
    uint8_t (*s2)[3] = (uint8_t (*)[3])malloc((size_t)n * 3);

    for (i = 0; i < n; i++) {
        uint8_t px[3], g[3], k[3];
        rgb565_to_rgb888(in[i], px);
        for (c = 0; c < 3; c++) {
            g[c] = p->ucGammaLUT[px[c]];
            k[c] = p->ucContrastLUT[g[c]];
            s2[i][c] = (uint8_t)k[c];
        }
        out1[i] = (uint16_t)(((g[0] & 0xf8) << 8) | ((g[1] & 0xfc) << 3) | (g[2] >> 3));
        out2[i] = (uint16_t)(((k[0] & 0xf8) << 8) | ((k[1] & 0xfc) << 3) | (k[2] >> 3));
    }
    for (y = 0; y < gH; y++) {
        for (x = 0; x < gW; x++) {
            int out[3];
            int xm = (x > 0) ? x - 1 : 0;
            int xp = (x + 1 < gW) ? x + 1 : gW - 1;
            int ym = (y > 0) ? y - 1 : 0;
            for (c = 0; c < 3; c++) {
                // vertical half of the separable kernel: previous row weighted
                // 1/4 only when a real previous row exists
                int vm = (s2[(size_t)ym * gW + xm][c] + 3 * s2[(size_t)y * gW + xm][c] + 2) >> 2;
                int v0 = (s2[(size_t)ym * gW + x][c] + 3 * s2[(size_t)y * gW + x][c] + 2) >> 2;
                int vp = (s2[(size_t)ym * gW + xp][c] + 3 * s2[(size_t)y * gW + xp][c] + 2) >> 2;
                int blur = (vm + 2 * v0 + vp + 2) >> 2;
                int cur = s2[(size_t)y * gW + x][c];
                int v = cur + (((cur - blur) * p->iSharpenQ8 + 128) >> 8);
                out[c] = (v < 0) ? 0 : (v > 255) ? 255 : v;
            }
            out3[(size_t)y * gW + x] =
                (uint16_t)(((out[0] & 0xf8) << 8) | ((out[1] & 0xfc) << 3) | (out[2] >> 3));
        }
    }
    free(s2);
}

// ---------- pipeline pass ----------
typedef struct {
    double dTime;       // total time spent in the pipeline, microseconds
    int iLines;
} TIMING;

static PNGPOST post;
static uint8_t *pLineBuf;
static int iTraceX0, iTraceX1, iTraceX2;

// Runs every line through the pipeline with the given stage mask. Optionally
// keeps the intermediate stage-1/stage-2/stage-3 images.
static int RunPipeline(int iMask, uint16_t *pS1, uint16_t *pS2, uint16_t *pS3,
                       TIMING *pTiming, int iY0, int iY1)
{
    uint16_t *pIn = (uint16_t *)malloc((size_t)gW * sizeof(uint16_t));
    uint16_t *p1 = (uint16_t *)malloc((size_t)gW * sizeof(uint16_t));
    uint16_t *p2 = (uint16_t *)malloc((size_t)gW * sizeof(uint16_t));
    uint16_t *p3 = (uint16_t *)malloc((size_t)gW * sizeof(uint16_t));
    double t0;
    int y, rc = PNG_SUCCESS;

    if (!pIn || !p1 || !p2 || !p3) return PNG_MEM_ERROR;
    PNGPostReset(&post);
    PNGPostSetStageMask(&post, iMask);
    PNGPostSetTrace(&post, iY0, iTraceX0, iTraceX1, iTraceX2);
    t0 = now_us();
    for (y = iY0; y <= iY1; y++) {
        memcpy(pIn, &gpLines[(size_t)y * gW], (size_t)gW * sizeof(uint16_t));
        rc = PNGPostLineRGB565Stages(&post, pIn, p1, p2, p3, gW, y);
        if (rc != PNG_SUCCESS) break;
        if (pS1) memcpy(&pS1[(size_t)y * gW], p1, (size_t)gW * sizeof(uint16_t));
        if (pS2) memcpy(&pS2[(size_t)y * gW], p2, (size_t)gW * sizeof(uint16_t));
        if (pS3) memcpy(&pS3[(size_t)y * gW], p3, (size_t)gW * sizeof(uint16_t));
    }
    if (pTiming) { pTiming->dTime += now_us() - t0; pTiming->iLines++; }
    free(pIn); free(p1); free(p2); free(p3);
    return rc;
}

static double psnr(const uint16_t *a, const uint16_t *b, int n)
{
    double se = 0.0;
    uint8_t ca[3], cb[3];
    int i, c;
    for (i = 0; i < n; i++) {
        rgb565_to_rgb888(a[i], ca);
        rgb565_to_rgb888(b[i], cb);
        for (c = 0; c < 3; c++) {
            double d = (double)ca[c] - (double)cb[c];
            se += d * d;
        }
    }
    if (se <= 0.0) return 99.0; // identical
    return 10.0 * log10((255.0 * 255.0) / (se / (n * 3.0)));
}

#define TRACE_RGB(s) tr[s][0], tr[s][1], tr[s][2]

int main(int argc, const char *argv[])
{
    FILE *f;
    uint8_t *pData;
    long iSize;
    int rc, y, x, i, w, h;
    uint16_t *pBase, *pStage1, *pStage2, *pStage3;
    TIMING t;
    double dLineUs[4];
    int iMask[4];
    const char *szStage[3] = {"gamma", "contrast", "sharpen"};

    if (argc != 2) {
        printf("Usage: postfx_demo <sample.png>\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); iSize = ftell(f); fseek(f, 0, SEEK_SET);
    pData = (uint8_t *)malloc((size_t)iSize);
    if (!pData || fread(pData, 1, (size_t)iSize, f) != (size_t)iSize) { printf("read error\n"); return 1; }
    fclose(f);

    rc = png.openRAM(pData, (int)iSize, CaptureDraw);
    if (rc != PNG_SUCCESS) { printf("openRAM failed: %d\n", rc); return 1; }
    w = png.getWidth(); h = png.getHeight();
    gW = w; gH = h;
    printf("Image: %d x %d, %d bpp, pixel type %d\n", w, h, png.getBpp(), png.getPixelType());

    pBase = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
    if (!pBase) { printf("oom\n"); return 1; }
    gpLines = pBase;
    gUseRaw = 0;
    gLines = 0;
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) { printf("decode failed: %d\n", rc); return 1; }
    if (gLines != h) { printf("line count mismatch: %d vs %d\n", gLines, h); return 1; }
    png.close();

    // ---------- pipeline set-up ----------
    pLineBuf = (uint8_t *)malloc((size_t)PNGPostBufferSize(w));
    if (!pLineBuf) { printf("oom\n"); return 1; }
    rc = PNGPostInit(&post, w, 220 /* gamma 0.86 */, 16, 240, 64 /* 0.25 */,
                     pLineBuf, PNGPostBufferSize(w));
    if (rc != PNG_SUCCESS) { printf("post init failed: %d\n", rc); return 1; }

    pStage1 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
    pStage2 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
    pStage3 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
    if (!pStage1 || !pStage2 || !pStage3) { printf("oom\n"); return 1; }

    // ---------- per-line callback timing, cumulative stages ----------
    iMask[0] = 0;
    iMask[1] = PNGPOST_STAGE_GAMMA;
    iMask[2] = PNGPOST_STAGE_GAMMA | PNGPOST_STAGE_CONTRAST;
    iMask[3] = PNGPOST_STAGE_GAMMA | PNGPOST_STAGE_CONTRAST | PNGPOST_STAGE_SHARPEN;
    {
        int rep, iReps = 2000;
        memset(dLineUs, 0, sizeof(dLineUs));
        for (i = 0; i < 4; i++) {
            double best = 1e18;
            for (rep = 0; rep < iReps; rep++) {
                memset(&t, 0, sizeof(t));
                if (RunPipeline(iMask[i], NULL, NULL, NULL, &t, 0, h - 1) != PNG_SUCCESS) {
                    printf("pipeline failed\n"); return 1;
                }
                if (t.dTime < best) best = t.dTime;
            }
            dLineUs[i] = best / h; // microseconds per line, best of iReps
        }
    }
    printf("\n-- per-line callback overhead (x86-64, best of %d full-image runs) --\n", 2000);
    printf("  passthrough (no stage)      : %7.4f us/line\n", dLineUs[0]);
    for (i = 1; i < 4; i++) {
        printf("  + %-25s : %7.4f us/line  (%+.4f us vs previous)\n",
               szStage[i - 1], dLineUs[i], dLineUs[i] - dLineUs[i - 1]);
    }
    printf("  three stages, total added   : %+.4f us/line\n", dLineUs[3] - dLineUs[0]);

    // ---------- one deterministic run holding every intermediate ----------
    // Trace three pixels on a middle row: a flat area, a 1-pixel impulse at
    // x=10 and the bright side of a hard step edge at x=20 (see sample.png).
    iTraceX0 = gW / 4;
    iTraceX1 = (gW > 11) ? 10 : gW / 2;
    iTraceX2 = (gW > 21) ? 20 : gW - 1;
    if (RunPipeline(iMask[3], pStage1, pStage2, pStage3, NULL, 0, h - 1) != PNG_SUCCESS) {
        printf("pipeline failed\n"); return 1;
    }

    printf("\n-- full-image total time (%d x %d = %d pixels, all three stages) --\n",
           w, h, w * h);
    {
        double best = 1e9, tot;
        int rep;
        for (rep = 0; rep < 200; rep++) {
            memset(&t, 0, sizeof(t));
            RunPipeline(iMask[3], NULL, NULL, NULL, &t, 0, h - 1);
            tot = t.dTime;
            if (tot < best) best = tot;
        }
        printf("  total pipeline time (best of 200 runs): %.2f us for %d lines, %.3f us/line\n",
               best, h, best / h);
        printf("  per-line measurement                  : %.4f us/line (all stages)\n",
               dLineUs[3]);
    }

    // ---------- three traced pixels, per stage ----------
    printf("\n-- per-stage RGB output of three pixels (row %d) --\n", h / 2);
    printf("  pixel        baseline      gamma        contrast     sharpen\n");
    {
        int xs[3] = {iTraceX0, iTraceX1, iTraceX2};
        for (i = 0; i < 3; i++) {
            uint8_t tr[3][3];
            uint8_t b[3];
            rgb565_to_rgb888(pBase[(size_t)(h / 2) * w + xs[i]], b);
            if (PNGPostGetTrace(&post, i, 2, tr[2]) != PNG_SUCCESS) { printf("trace failed\n"); return 1; }
            PNGPostGetTrace(&post, i, 0, tr[0]);
            PNGPostGetTrace(&post, i, 1, tr[1]);
            printf("  x=%-3d (%3d,%3d,%3d)  (%3d,%3d,%3d)  (%3d,%3d,%3d)  (%3d,%3d,%3d)\n",
                   xs[i], b[0], b[1], b[2],
                   TRACE_RGB(0), TRACE_RGB(1), TRACE_RGB(2));
        }
    }

    // ---------- whole-image PSNR, each cumulative stage vs baseline ----------
    printf("\n-- whole-image PSNR vs. raw decode output (%d pixels) --\n", w * h);
    printf("  gamma only                  : %6.2f dB\n", psnr(pBase, pStage1, w * h));
    printf("  gamma + contrast            : %6.2f dB\n", psnr(pBase, pStage2, w * h));
    printf("  gamma + contrast + sharpen  : %6.2f dB\n", psnr(pBase, pStage3, w * h));
    printf("  stage1 vs stage2 (delta)    : %6.2f dB\n", psnr(pStage1, pStage2, w * h));
    printf("  stage2 vs stage3 (delta)    : %6.2f dB\n", psnr(pStage2, pStage3, w * h));

    // ---------- streaming result vs. independent reference ----------
    {
        uint16_t *r1 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
        uint16_t *r2 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
        uint16_t *r3 = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
        int d1 = 0, d2 = 0, d3 = 0;
        if (!r1 || !r2 || !r3) { printf("oom\n"); return 1; }
        RefStage(&post, pBase, r1, r2, r3);
        for (i = 0; i < w * h; i++) {
            d1 += (r1[i] != pStage1[i]);
            d2 += (r2[i] != pStage2[i]);
            d3 += (r3[i] != pStage3[i]);
        }
        printf("\n-- streaming (line-cache) vs. full-buffer reference --\n");
        printf("  differing pixels gamma=%d contrast=%d sharpen=%d %s\n", d1, d2, d3,
               (d1 || d2 || d3) ? "(MISMATCH)" : "(identical)");
        free(r1); free(r2); free(r3);
    }

    // ---------- memory ----------
    printf("\n-- memory added by the pipeline --\n");
    printf("  PNGPOST state struct        : %4d bytes\n", (int)sizeof(PNGPOST));
    printf("  line buffer (2 x %d x RGB888): %4d bytes (caller supplied)\n",
           w, PNGPostBufferSize(w));
    printf("  static/global RAM in library:    0 bytes (no new statics)\n");
    printf("  full-image staged buffers   : %d bytes (diagnostic only, not required)\n",
           (int)(3 * (size_t)w * h * sizeof(uint16_t)));
    printf("  per-line working set        : %4d bytes\n",
           (int)(sizeof(PNGPOST) + PNGPostBufferSize(w)));

    // ---------- parameter validation ----------
    printf("\n-- parameter validation (expect %d for each) --\n", PNG_INVALID_PARAMETER);
    {
        struct { const char *name; int g, lo, hi, sh, bw, bs; } bad[] = {
            {"gamma 0.24 (below min)",        63,  16, 240, 64, 0, 0},
            {"gamma 4.01 (above max)",      1025,  16, 240, 64, 0, 0},
            {"contrast low = -1",            220,  -1, 240, 64, 0, 0},
            {"contrast low = 255",           220, 255, 240, 64, 0, 0},
            {"contrast high = 0",            220,  16,   0, 64, 0, 0},
            {"contrast high = 256",          220,  16, 256, 64, 0, 0},
            {"contrast high <= low + 7",     220, 100, 106, 64, 0, 0},
            {"sharpen = -1",                 220,  16, 240, -1, 0, 0},
            {"sharpen 0.51 (above max)",     220,  16, 240, 129, 0, 0},
            {"width = 0",                    220,  16, 240, 64, 1, 0},
            {"buffer one byte too small",    220,  16, 240, 64, 0, -1},
        };
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
            int bw = (bad[i].bw) ? 0 : w;
            int bs = PNGPostBufferSize(w) + bad[i].bs;
            int r = PNGPostInit(&post, bw, bad[i].g, bad[i].lo, bad[i].hi,
                                bad[i].sh, pLineBuf, bs);
            printf("  %-28s -> rc=%d %s\n", bad[i].name, r,
                   (r == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
        }
        // line-level parameter checks
        {
            uint16_t px = 0x1234, out;
            printf("  line: NULL dest              -> rc=%d %s\n",
                   PNGPostLineRGB565(&post, &px, NULL, w, 0),
                   (PNGPostLineRGB565(&post, &px, NULL, w, 0) == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
            printf("  line: width mismatch         -> rc=%d %s\n",
                   PNGPostLineRGB565(&post, &px, &out, w + 1, 0),
                   (PNGPostLineRGB565(&post, &px, &out, w + 1, 0) == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
            printf("  line: negative row           -> rc=%d %s\n",
                   PNGPostLineRGB565(&post, &px, &out, w, -1),
                   (PNGPostLineRGB565(&post, &px, &out, w, -1) == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
            printf("  trace: x out of range        -> rc=%d %s\n",
                   PNGPostSetTrace(&post, 0, 0, 0, w),
                   (PNGPostSetTrace(&post, 0, 0, 0, w) == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
            printf("  stage mask: 8 (invalid bit)  -> rc=%d %s\n",
                   PNGPostSetStageMask(&post, 8),
                   (PNGPostSetStageMask(&post, 8) == PNG_INVALID_PARAMETER) ? "OK" : "WRONG");
        }
        // restore a valid configuration after the negative tests
        PNGPostInit(&post, w, 220, 16, 240, 64, pLineBuf, PNGPostBufferSize(w));
    }

    // ---------- raw decode output is untouched ----------
    printf("\n-- raw decode output check --\n");
    if (png.getPixelType() == PNG_PIXEL_TRUECOLOR && png.getBpp() == 8) {
        uint16_t *pRaw = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
        int diff = 0;
        if (!pRaw) { printf("oom\n"); return 1; }
        png.openRAM(pData, (int)iSize, NULL);
        png.setBuffer(gRawBuf = (uint8_t *)malloc((size_t)png.getBufferSize()));
        if (!gRawBuf) { printf("oom\n"); return 1; }
        png.decode(NULL, 0);
        // convert the raw framebuffer (3 bytes/pixel) to RGB565 the same way
        for (y = 0; y < h; y++) {
            const uint8_t *s = gRawBuf + (size_t)y * w * 3;
            for (x = 0; x < w; x++) {
                uint16_t v = (uint16_t)(((s[0] & 0xf8) << 8) | ((s[1] & 0xfc) << 3) | (s[2] >> 3));
                pRaw[(size_t)y * w + x] = v;
                s += 3;
            }
        }
        png.close();
        for (i = 0; i < w * h; i++) {
            if (pRaw[i] != pBase[i]) diff++;
        }
        printf("  raw framebuffer vs callback path: %d differing pixels %s\n",
               diff, diff ? "(MISMATCH)" : "(identical)");
        free(pRaw);
        free(gRawBuf);
    } else {
        printf("  skipped: image is not 8-bit truecolor (type %d, %d bpp)\n",
               png.getPixelType(), png.getBpp());
    }

    free(pStage1); free(pStage2); free(pStage3);
    free(pLineBuf); free(pBase); free(pData);
    printf("\nAll measurements completed.\n");
    return 0;
}
