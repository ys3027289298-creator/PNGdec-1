//
// roi_test.cpp
//
// Functional + pixel-consistency + timing tests for the ROI decode
// extension added in src/png.inl. Built exactly like the repository's
// own MacOS/PNG_Test harness: one translation unit including the library
// sources directly, no build-system changes anywhere in the repo.
//
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>

#define __LINUX__
#include "../src/zutil.c"
#include "../src/inflate.c"
#include "../src/inffast.c"
#include "../src/inftrees.c"
#include "../src/crc32.c"
#undef DO1
#undef DO8
#include "../src/adler32.c"
#include "../src/PNGdec.cpp"

// The ROI API (PNG_setROI/PNG_getROI/PNG_getROIDrawGeometry/PNG_resetROI
// and PNG_ROI_RECT) is defined in src/png.inl, which PNGdec.cpp includes.

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); } \
    else { g_fail++; printf("  [FAIL] %s\n", name); } } while (0)

static long Micros(void)
{
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return (long)res.tv_sec * 1000000L + res.tv_nsec / 1000;
}

static uint8_t *read_file(const char *path, int *pSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    int n = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = (uint8_t *)malloc(n);
    if (fread(p, 1, n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(2); }
    fclose(f);
    *pSize = n;
    return p;
}

struct Image {
    const char *name;
    const char *path;
    uint8_t *data;
    int size, w, h, bpp, ptype;
    int bytesPerPixel; // 1 for sub-byte formats too (we compare packed bytes)
};

// ---- full image decode: every line captured verbatim ----
struct FullCtx { uint8_t *buf; int pitch; int lines; };
static int draw_full(PNGDRAW *d)
{
    FullCtx *c = (FullCtx *)d->pUser;
    memcpy(c->buf + d->y * c->pitch, d->pPixels, d->iPitch);
    c->lines++;
    return 1;
}

// ---- ROI decode: lines land at (y - firstY) * drawPitch ----
struct RoiCtx { uint8_t *buf; int firstY, drawX, drawWidth, drawPitch; int lines; int pixels; };
static int draw_roi(PNGDRAW *d)
{
    RoiCtx *c = (RoiCtx *)d->pUser;
    int idx = c->lines; // callbacks arrive in strictly increasing y order
    memcpy(c->buf + idx * c->drawPitch, d->pPixels, d->iPitch);
    if (d->iWidth != c->drawWidth) {
        printf("    line %d: callback width %d != geometry width %d\n", d->y, d->iWidth, c->drawWidth);
    }
    c->pixels += d->iWidth;
    c->lines++;
    return 1;
}

static void open_image(PNG &png, Image &im)
{
    if (im.data == NULL) // each file is read exactly once
        im.data = read_file(im.path, &im.size);
    int rc = png.openRAM(im.data, im.size, NULL);
    if (rc != PNG_SUCCESS) { fprintf(stderr, "open failed %s rc=%d\n", im.path, rc); exit(2); }
    im.w = png.getWidth(); im.h = png.getHeight();
    im.bpp = png.getBpp(); im.ptype = png.getPixelType();
}

static PNGIMAGE *inner(PNG &png) // PNG is standard-layout with one data member
{
    return reinterpret_cast<PNGIMAGE *>(&png);
}

static int bpp_bits(Image &im)
{
    int ch = (im.ptype == PNG_PIXEL_TRUECOLOR) ? 3
           : (im.ptype == PNG_PIXEL_GRAY_ALPHA) ? 2
           : (im.ptype == PNG_PIXEL_TRUECOLOR_ALPHA) ? 4 : 1;
    return im.bpp * ch;
}

//
// Decode the full image (reference) and an ROI, then verify every
// delivered ROI byte matches the corresponding full-decode region.
//
static void consistency_case(Image &im, const char *label, int rx, int ry, int rw, int rh)
{
    PNG png;
    png.openRAM(im.data, im.size, draw_full);
    int pitch = png.getBufferSize() / im.h; // iPitch
    uint8_t *full = (uint8_t *)malloc(pitch * im.h);
    FullCtx fc = { full, pitch, 0 };
    int rc = png.decode(&fc, 0);
    CHECK(rc == PNG_SUCCESS && fc.lines == im.h, "full reference decode succeeds");

    PNG png2;
    png2.openRAM(im.data, im.size, draw_roi);
    PNGIMAGE *p = inner(png2);
    rc = PNG_setROI(p, rx, ry, rw, rh);
    CHECK(rc == PNG_SUCCESS, "PNG_setROI accepts the rectangle");
    PNG_ROI_RECT req, eff;
    PNG_getROI(p, &req, &eff);
    int gx, gw, gp;
    PNG_getROIDrawGeometry(p, &gx, &gw, &gp);
    RoiCtx rc2;
    rc2.buf = (uint8_t *)malloc(gp * (eff.h + 1));
    rc2.firstY = eff.y; rc2.drawX = gx; rc2.drawWidth = gw; rc2.drawPitch = gp;
    rc2.lines = 0; rc2.pixels = 0;
    int rc3 = png2.decode(&rc2, 0);
    CHECK(rc3 == PNG_SUCCESS, "ROI decode succeeds");
    CHECK(rc2.lines == eff.h, "ROI delivers exactly the clamped row count");

    // byte-for-byte comparison of the delivered spans vs. the full decode
    int bits = bpp_bits(im);
    int mismatches = 0;
    int totalBytes = 0;
    for (int ly = 0; ly < eff.h; ly++) {
        int srcY = eff.y + ly;
        int startByte = (gx * bits) / 8;
        for (int b = 0; b < gp; b++) {
            totalBytes++;
            uint8_t a = full[srcY * pitch + startByte + b];
            uint8_t c = rc2.buf[ly * gp + b];
            if (a != c) {
                if (mismatches < 5)
                    printf("    mismatch y=%d byte=%d: full=0x%02x roi=0x%02x\n", srcY, b, a, c);
                mismatches++;
            }
        }
    }
    printf("  %s / %s: req=(%d,%d,%d,%d) clamped=(%d,%d,%d,%d) "
           "drawGeom x=%d w=%d pitch=%d deliveredPixels=%d bytes=%d mismatches=%d\n",
           im.name, label, rx, ry, rw, rh,
           eff.x, eff.y, eff.w, eff.h, gx, gw, gp, rc2.pixels, totalBytes, mismatches);
    CHECK(mismatches == 0, "ROI pixels identical to full-decode region");

    free(full); free(rc2.buf);
    png.close(); png2.close();
}

//
// Timing comparison: full decode vs. ROI decode.
// Returns delivered pixel counts and average times.
//
static void timing_case(Image &im, const char *label, int rx, int ry, int rw, int rh,
                        int iters, long *tFull, long *tRoi, int *pxFull, int *pxRoi, int *rowsRoi)
{
    PNG png;
    png.openRAM(im.data, im.size, draw_full);
    int pitch = png.getBufferSize() / im.h;
    uint8_t *full = (uint8_t *)malloc(pitch * im.h);
    FullCtx fc = { full, pitch, 0 };
    long t0 = Micros();
    for (int i = 0; i < iters; i++) {
        fc.lines = 0;
        png.decode(&fc, 0);
    }
    *tFull = (Micros() - t0) / iters;
    *pxFull = im.w * im.h;
    png.close();

    PNG png2;
    png2.openRAM(im.data, im.size, draw_roi);
    PNGIMAGE *p = inner(png2);
    PNG_setROI(p, rx, ry, rw, rh);
    PNG_ROI_RECT req, eff;
    PNG_getROI(p, &req, &eff);
    int gx, gw, gp;
    PNG_getROIDrawGeometry(p, &gx, &gw, &gp);
    RoiCtx rc2;
    rc2.buf = (uint8_t *)malloc(gp * (eff.h + 1));
    rc2.firstY = eff.y; rc2.drawX = gx; rc2.drawWidth = gw; rc2.drawPitch = gp;
    long t1 = Micros();
    for (int i = 0; i < iters; i++) {
        rc2.lines = 0; rc2.pixels = 0;
        png2.decode(&rc2, 0);
    }
    *tRoi = (Micros() - t1) / iters;
    *pxRoi = rc2.pixels;
    *rowsRoi = rc2.lines;
    free(full); free(rc2.buf);
    png2.close();
    printf("  %-12s %-10s full: %6d px %5ld us/dec | ROI: %6d px %3d rows %5ld us/dec | speedup %.2fx\n",
           im.name, label, *pxFull, *tFull, *pxRoi, *rowsRoi, *tRoi,
           *tRoi > 0 ? (double)*tFull / *tRoi : 0.0);
}

static Image IMGS[4];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: roi_test <png_dir>\n"); return 2; }
    char path[512];
    const char *names[4] = { "gray8_64x48.png", "rgb24_64x48.png", "rgba32_64x48.png", "indexed4_64x48.png" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/%s", argv[1], names[i]);
        IMGS[i].name = names[i]; IMGS[i].path = strdup(path);
        PNG probe;
        open_image(probe, IMGS[i]); // read file and learn dimensions/format
        probe.close();
    }

    // ---------- 1. full-image ROI must behave like the full decode ----------
    printf("== functional: full-image ROI (all 4 pixel types) ==\n");
    for (int i = 0; i < 4; i++)
        consistency_case(IMGS[i], "full", 0, 0, IMGS[i].w, IMGS[i].h);

    // ---------- 2. top-left corner ----------
    printf("== functional: top-left ROI (0,0,64,48) ==\n");
    for (int i = 0; i < 4; i++)
        consistency_case(IMGS[i], "topleft", 0, 0, 64, 48);

    // ---------- 3. bottom-right corner ----------
    printf("== functional: bottom-right ROI (160,120,96,72) ==\n");
    for (int i = 0; i < 4; i++)
        consistency_case(IMGS[i], "botright", 160, 120, 96, 72);

    // ---------- 4. ROI straddling the image boundary ----------
    printf("== functional: boundary-straddling ROI (200,150,120,90) ==\n");
    for (int i = 0; i < 4; i++)
        consistency_case(IMGS[i], "cross", 200, 150, 120, 90);

    // ---------- 5. clamping behavior ----------
    printf("== functional: out-of-bounds clamping ==\n");
    {
        PNG png; open_image(png, IMGS[1]);
        PNGIMAGE *p = inner(png);
        PNG_ROI_RECT req, eff;
        // fully outside: negative origin
        PNG_setROI(p, -100, -100, 10, 10);
        PNG_getROI(p, &req, &eff);
        CHECK(eff.x == 0 && eff.y == 0 && eff.w == 0 && eff.h == 0,
              "negative-origin rectangle clamps to empty");
        // far outside, positive
        PNG_setROI(p, 300, 300, 10, 10);
        PNG_getROI(p, &req, &eff);
        CHECK(eff.w == 0 && eff.h == 0 && eff.x == 256 && eff.y == 192,
              "past-edge rectangle clamps to empty at image size");
        // oversized rectangle clamps to whole image
        PNG_setROI(p, -5, -7, 500, 500);
        PNG_getROI(p, &req, &eff);
        CHECK(eff.x == 0 && eff.y == 0 && eff.w == 256 && eff.h == 192,
              "oversized rectangle clamps to full image");
        // empty size
        int rc = PNG_setROI(p, 10, 10, 0, 5);
        CHECK(rc == PNG_SUCCESS, "zero-width ROI accepted as empty");
        PNG_getROI(p, NULL, &eff);
        CHECK(eff.w == 0 && eff.h == 0, "zero-width ROI delivers nothing");
        // geometry query reports inactive after reset
        PNG_resetROI(p);
        CHECK(PNG_getROI(p, NULL, NULL) == 0, "PNG_getROI inactive after reset");
        int gx = -1, gw = -1, gp = -1;
        CHECK(PNG_getROIDrawGeometry(p, &gx, &gw, &gp) == 0,
              "geometry query inactive after reset");
        png.close();
    }

    // ---------- 6. sub-byte geometry expansion (4bpp indexed) ----------
    printf("== functional: sub-byte (4bpp) line-buffer geometry ==\n");
    {
        PNG png; open_image(png, IMGS[3]);
        PNGIMAGE *p = inner(png);
        PNG_setROI(p, 3, 5, 7, 9); // odd start -> byte alignment expands the span
        PNG_ROI_RECT req, eff; PNG_getROI(p, &req, &eff);
        int gx, gw, gp; PNG_getROIDrawGeometry(p, &gx, &gw, &gp);
        printf("  indexed4 req=(3,5,7,9) eff=(%d,%d,%d,%d) drawGeom x=%d w=%d pitch=%d (image w=256, pitch=128)\n",
               eff.x, eff.y, eff.w, eff.h, gx, gw, gp);
        CHECK(gx == 2 && gw == 8 && gp == 4, "4bpp span rounded out to whole bytes");
        png.close();
    }

    // ---------- 7. reset restores full-image decode ----------
    printf("== functional: reset restores full decode ==\n");
    {
        PNG png; open_image(png, IMGS[0]);
        PNGIMAGE *p = inner(png);
        int pitch = png.getBufferSize() / IMGS[0].h;
        uint8_t *buf = (uint8_t *)malloc(pitch * IMGS[0].h);
        FullCtx fc = { buf, pitch, 0 };
        png.close();
        png.openRAM(IMGS[0].data, IMGS[0].size, draw_full);
        PNG_setROI(p, 0, 0, 8, 8);
        PNG_resetROI(p);
        int rc = png.decode(&fc, 0);
        CHECK(rc == PNG_SUCCESS && fc.lines == IMGS[0].h, "full line count after reset");
        free(buf); png.close();
    }

    // ---------- 8. timing: four picture classes ----------
    printf("== timing: average over 500 decodes each (microseconds/dec) ==\n");
    const int ITERS = 500;
    long tf, tr; int pf, pr, rows;
    for (int i = 0; i < 4; i++) {
        Image &im = IMGS[i];
        timing_case(im, "full", 0, 0, im.w, im.h, ITERS, &tf, &tr, &pf, &pr, &rows);
        timing_case(im, "topleft", 0, 0, 64, 48, ITERS, &tf, &tr, &pf, &pr, &rows);
        timing_case(im, "botright", 160, 120, 96, 72, ITERS, &tf, &tr, &pf, &pr, &rows);
        timing_case(im, "cross", 200, 150, 120, 90, ITERS, &tf, &tr, &pf, &pr, &rows);
    }

    for (int i = 0; i < 4; i++)
        free(IMGS[i].data);

    printf("\nTOTAL: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
