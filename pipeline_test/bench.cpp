//
// Row-pipeline benchmark & correctness harness for PNGdec.
// Decodes the 4 generated image types, verifies pixel-exact output against
// ground-truth .raw files (buffer mode and callback mode), and times decodes.
// Config comes from the same compile-time macros the library uses.
//
#include "../src/PNGdec.h"
#include <time.h>

#ifndef PNG_ROW_PIPELINE
#define PNG_ROW_PIPELINE 1
#endif
#ifndef PNG_PIPELINE_ROWS
#define PNG_PIPELINE_ROWS 8
#endif

#ifndef ITERS
#define ITERS 30
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint64_t fnv1a(uint64_t h, const uint8_t *p, long n) {
    for (long i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
#define FNV_SEED 1469598103934665603ULL

static uint8_t *slurp(const char *fname, long *pSize) {
    FILE *f = fopen(fname, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", fname); exit(1); }
    fseek(f, 0, SEEK_END); *pSize = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(*pSize);
    if (fread(buf, 1, *pSize, f) != (size_t)*pSize) { fprintf(stderr, "read error\n"); exit(1); }
    fclose(f);
    return buf;
}

// callback-mode state
static uint64_t g_cbHash;
static int g_cbRows;
static int DrawCB(PNGDRAW *pDraw) {
    g_cbHash = fnv1a(g_cbHash, pDraw->pPixels, pDraw->iPitch); // chained, row order matters
    g_cbRows++;
    return 1;
}

static PNG png;
static int g_fail = 0;

static void run_one(const char *name) {
    char path[256];
    long pngSize, rawSize;
    snprintf(path, sizeof(path), "%s.png", name);
    uint8_t *pPng = slurp(path, &pngSize);
    snprintf(path, sizeof(path), "%s.raw", name);
    uint8_t *pRaw = slurp(path, &rawSize);

    int rc = png.openRAM(pPng, (int)pngSize, NULL);
    if (rc != PNG_SUCCESS) { printf("%-12s openRAM FAILED rc=%d\n", name, rc); g_fail = 1; return; }
    int w = png.getWidth(), h = png.getHeight();
    int pitch = png.getBufferSize() / h;
    int iRowBytes = pitch + 1;
    // replicate the library's bank sizing for reporting
    int iMaxRows = (int)((PNG_MAX_BUFFERED_PIXELS - 512 - 32) / iRowBytes);
    int R = PNG_PIPELINE_ROWS;
    if (R > iMaxRows / 2) R = iMaxRows / 2;
    int pipelined = (PNG_ROW_PIPELINE && R >= 2);
    if (!pipelined) R = 1;
    long memDelta = pipelined ? (long)(2 * R - 2) * iRowBytes : 0;

    if ((long)png.getBufferSize() != rawSize) {
        printf("%-12s buffer size mismatch: lib=%d raw=%ld\n", name, png.getBufferSize(), rawSize);
        g_fail = 1; return;
    }
    uint8_t *pImg = (uint8_t *)malloc(rawSize);
    png.setBuffer(pImg);

    // correctness: buffer mode vs ground truth
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) { printf("%-12s decode FAILED rc=%d\n", name, rc); g_fail = 1; return; }
    int match = (memcmp(pImg, pRaw, rawSize) == 0);

    // timing: repeated decodes (supported without re-open)
    double t0 = now_ms();
    for (int i = 0; i < ITERS; i++) png.decode(NULL, 0);
    double dt = (now_ms() - t0) / ITERS;

    // correctness: callback mode vs ground truth (hash of all rows in order)
    PNG png2;
    g_cbHash = FNV_SEED; g_cbRows = 0;
    rc = png2.openRAM(pPng, (int)pngSize, DrawCB);
    if (rc == PNG_SUCCESS) rc = png2.decode(NULL, 0);
    uint64_t rawHash = FNV_SEED;
    for (int yy = 0; yy < h; yy++) rawHash = fnv1a(rawHash, pRaw + (long)yy * pitch, pitch);
    int cbMatch = (rc == PNG_SUCCESS && g_cbRows == h && g_cbHash == rawHash);

    printf("%-12s %4dx%-4d pitch=%4d  %-8s R=%d  +%6ld B  %8.3f ms/decode  buf=%s cb=%s\n",
           name, w, h, pitch, pipelined ? "PIPELINE" : "serial", pipelined ? R : 0,
           memDelta, dt, match ? "MATCH" : "MISMATCH", cbMatch ? "MATCH" : "MISMATCH");
    if (!match || !cbMatch) g_fail = 1;
    free(pImg); free(pPng); free(pRaw);
}

int main(void) {
    printf("config: PNG_ROW_PIPELINE=%d PNG_PIPELINE_ROWS=%d PNG_MAX_BUFFERED_PIXELS=%d sizeof(PNGIMAGE)=%zu\n",
           PNG_ROW_PIPELINE, PNG_PIPELINE_ROWS, PNG_MAX_BUFFERED_PIXELS, sizeof(PNGIMAGE));
    printf("%-12s %9s  %-8s %-4s %-11s %15s  %s\n",
           "image", "size", "mode", "R", "mem-delta", "time", "pixel-check");
    run_one("gray8");
    run_one("truecolor8");
    run_one("indexed8");
    run_one("rgba8");
    printf("%s\n", g_fail ? "RESULT: FAIL" : "RESULT: ALL MATCH");
    return g_fail;
}
