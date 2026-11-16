//
// crc_fault_test - chunk-level CRC fault-tolerance test for PNGdec
//
// Generates its own PNG samples in memory (no external files, no network,
// no third-party libs - only the repo's own sources are linked).
//
// Four sample categories are exercised:
//   1) normal file
//   2) single chunk with a CRC error (one IDAT)
//   3) multiple chunks with CRC errors (tEXt + one IDAT)
//   4) truncated IDAT (file cut short mid-chunk)
//
// For each category it reports decode success rate, number of filled
// (synthesized) pixels and average decode time, with the fault-tolerance
// switch off (current behavior) and on (both fill strategies).
//
// Build (from repo root):
//   g++ -D__LINUX__ -DPNGDEC_HAS_TOLERANCE -Wall -O2 -g -fsanitize=address,undefined \
//       -I src linux/crc_fault_test/main.cpp src/PNGdec.cpp src/adler32.c src/crc32.c \
//       src/infback.c src/inffast.c src/inflate.c src/inftrees.c src/zutil.c -o /tmp/crc_fault_test
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "PNGdec.h"

// Fault-tolerance option bits (defined in png.inl; values fixed there)
#define PNG_TOLERATE_CRC 4 // verify chunk CRCs, skip bad chunks, fill missing lines
#define PNG_FILL_ZERO    8 // fill with zeros instead of replicating the previous line

#ifdef PNGDEC_HAS_TOLERANCE
extern uint32_t g_pngFilledPixels;  // stats exported by png.inl
extern uint32_t g_pngSkippedChunks;
#endif

PNG png;

// ---------------- self-made sample PNG ----------------
#define W 64
#define H 48
#define RAWLINE (1 + W)          // filter byte + 64 gray pixels
#define RAWBYTES (H * RAWLINE)   // 3120
#define ZBYTES (2 + 5 + RAWBYTES + 4) // zlib hdr + stored-block hdr + data + adler = 3131
#define IDAT1_LEN (2 + 5 + 20 * RAWLINE) // first 20 scanlines worth of the zlib stream
#define IDAT2_LEN (ZBYTES - IDAT1_LEN)

static uint8_t pngbuf[4096];
static int pnglen;
static int offTextData, offIdat2Data, offIdat1Data;
#define TEXT_LEN 22

static void put32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

static int addChunk(uint8_t *dst, const char *type, const uint8_t *data, int len)
{
    put32(dst, (uint32_t)len);
    memcpy(dst+4, type, 4);
    if (len) memcpy(dst+8, data, len);
    uint32_t c = (uint32_t)crc32(0, dst+4, 4+len); // repo's own crc32
    put32(dst+8+len, c);
    return 12 + len;
}

static void buildPNG(void)
{
    uint8_t raw[RAWBYTES];
    uint8_t z[ZBYTES];
    for (int y = 0; y < H; y++) {
        raw[y*RAWLINE] = 0; // filter: none
        memset(&raw[y*RAWLINE+1], y*4, W); // line y filled with value y*4
    }
    z[0] = 0x78; z[1] = 0x01;       // zlib header (deflate, 32k window)
    z[2] = 0x01;                    // final stored block
    z[3] = RAWBYTES & 0xff; z[4] = RAWBYTES >> 8;
    z[5] = (uint8_t)~z[3]; z[6] = (uint8_t)~z[4];
    memcpy(z+7, raw, RAWBYTES);
    uint32_t a = (uint32_t)adler32(adler32(0, NULL, 0), raw, RAWBYTES);
    put32(z + 2 + 5 + RAWBYTES, a); // adler32, big-endian

    int n = 0;
    memcpy(pngbuf, "\x89PNG\r\n\x1a\n", 8); n = 8;
    uint8_t ihdr[13];
    put32(ihdr, W); put32(ihdr+4, H);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; // 8bpp grayscale
    n += addChunk(pngbuf+n, "IHDR", ihdr, 13);
    const char txt[TEXT_LEN+1] = "Comment\0crc fault test"; // 21 chars + 2 NULs
    offTextData = n + 8;
    n += addChunk(pngbuf+n, "tEXt", (const uint8_t *)txt, TEXT_LEN);
    offIdat1Data = n + 8;
    n += addChunk(pngbuf+n, "IDAT", z, IDAT1_LEN);
    offIdat2Data = n + 8;
    n += addChunk(pngbuf+n, "IDAT", z + IDAT1_LEN, IDAT2_LEN);
    n += addChunk(pngbuf+n, "IEND", NULL, 0);
    pnglen = n;
}

// ---------------- decode helper ----------------
typedef struct { int rc, err; long filled, skipped; double us; } Result;

static double nowUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static Result decode(const uint8_t *data, int len, int options, uint8_t *outBuf)
{
    Result r;
    r.filled = r.skipped = -1;
    r.rc = png.openRAM((uint8_t *)data, len, NULL);
    if (r.rc != PNG_SUCCESS) { r.err = png.getLastError(); r.us = 0; return r; }
    png.setBuffer(outBuf);
    double t0 = nowUs();
    r.rc = png.decode(NULL, options);
    r.us = nowUs() - t0;
    r.err = png.getLastError();
#ifdef PNGDEC_HAS_TOLERANCE
    r.filled = (long)g_pngFilledPixels;
    r.skipped = (long)g_pngSkippedChunks;
#endif
    png.close();
    return r;
}

// ---------------- report machinery ----------------
#define RUNS 25
typedef struct {
    const char *name;
    int opts;
    long ok, runs, filledSum, skippedSum;
    double timeSum;
} Row;

static void runRow(Row *row, uint8_t *sample, int len, uint8_t *baseline, uint8_t *scratch)
{
    row->ok = row->runs = row->filledSum = row->skippedSum = 0;
    row->timeSum = 0;
    for (int k = 0; k < RUNS; k++) {
        Result r = decode(sample, len, row->opts, scratch);
        row->runs++;
        if (r.rc == PNG_SUCCESS) row->ok++;
        if (r.filled > 0) row->filledSum += r.filled;
        if (r.skipped > 0) row->skippedSum += r.skipped;
        row->timeSum += r.us;
    }
}

static void printRow(const Row *row)
{
    printf("  %-26s ok %2ld/%2ld  filled %6ld  skipped %2ld  %8.1f us\n",
           row->name, row->ok, row->runs,
           row->ok ? row->filledSum / row->ok : 0,
           row->ok ? row->skippedSum / row->ok : 0,
           row->timeSum / row->runs);
}

int main(void)
{
    uint8_t *baseline = (uint8_t *)malloc(W*H);
    uint8_t *scratch  = (uint8_t *)malloc(W*H);
    uint8_t *sample   = (uint8_t *)malloc(sizeof(pngbuf));
    int iFail = 0;
    Row row;

    buildPNG();
    printf("sample: %d bytes, %dx%d 8bpp grayscale, chunks: IHDR tEXt IDAT(%d) IDAT(%d) IEND\n",
           pnglen, W, H, IDAT1_LEN, IDAT2_LEN);
    printf("expect: lines 0..19 come from IDAT1, lines 20..47 are lost when IDAT2 is bad\n\n");

    // ---------- category 1: normal file ----------
    printf("[1] normal file\n");
    Result rBase = decode(pngbuf, pnglen, 0, baseline);
    if (rBase.rc != PNG_SUCCESS) { printf("  FATAL: cannot decode clean sample\n"); return 1; }

    row = (Row){"current (options=0)", 0};
    runRow(&row, pngbuf, pnglen, baseline, scratch); printRow(&row);
    row = (Row){"tolerate + prev-line", PNG_TOLERATE_CRC};
    runRow(&row, pngbuf, pnglen, baseline, scratch); printRow(&row);
    if (row.ok != RUNS) { printf("  FAIL: normal file must decode with tolerance on\n"); iFail++; }
    row = (Row){"tolerate + zero-fill", PNG_TOLERATE_CRC|PNG_FILL_ZERO};
    runRow(&row, pngbuf, pnglen, baseline, scratch); printRow(&row);

    // pixel-identical requirement: tolerance on must not change a normal file
    Result rTol = decode(pngbuf, pnglen, PNG_TOLERATE_CRC, scratch);
    if (memcmp(baseline, scratch, W*H) != 0 || rTol.filled != 0) {
        printf("  FAIL: normal file not pixel-identical with tolerance on (filled=%ld)\n", rTol.filled);
        iFail++;
    } else {
        printf("  pixel-identical vs current decode: YES (filled=0)\n");
    }

    // ---------- category 2: single chunk CRC error (IDAT2) ----------
    printf("\n[2] single chunk CRC error (1 byte flipped in IDAT2, %d variants)\n", RUNS);
    srand(1234);
    long okCur = 0, okCrc = 0, okTol = 0, okTolZ = 0, fillSum = 0, skipSum = 0;
    double tTol = 0;
    int fillContentOK = 1, zeroContentOK = 1, headOK = 1;
    for (int k = 0; k < RUNS; k++) {
        memcpy(sample, pngbuf, pnglen);
        sample[offIdat2Data + rand() % IDAT2_LEN] ^= 0x5A; // corrupt IDAT2 data
        if (decode(sample, pnglen, 0, scratch).rc == PNG_SUCCESS) okCur++;
        if (decode(sample, pnglen, PNG_CHECK_CRC, scratch).rc == PNG_SUCCESS) okCrc++;
        Result r = decode(sample, pnglen, PNG_TOLERATE_CRC, scratch);
        if (r.rc == PNG_SUCCESS) { okTol++; fillSum += r.filled; skipSum += r.skipped; tTol += r.us; }
        // filled lines 20..47 must replicate line 19 (value 19*4=76); lines 0..19 intact
        for (int y = 0; y < 20 && headOK; y++)
            for (int x = 0; x < W; x++) if (scratch[y*W+x] != baseline[y*W+x]) headOK = 0;
        for (int y = 20; y < H; y++)
            for (int x = 0; x < W; x++) if (scratch[y*W+x] != 76) fillContentOK = 0;
        r = decode(sample, pnglen, PNG_TOLERATE_CRC|PNG_FILL_ZERO, scratch);
        if (r.rc == PNG_SUCCESS) okTolZ++;
        for (int y = 20; y < H; y++)
            for (int x = 0; x < W; x++) if (scratch[y*W+x] != 0) zeroContentOK = 0;
    }
    printf("  current (options=0)       ok %2ld/%d  (decodes garbage silently)\n", okCur, RUNS);
    printf("  current (PNG_CHECK_CRC)   ok %2ld/%d  (whole image rejected)\n", okCrc, RUNS);
    printf("  tolerate + prev-line      ok %2ld/%d  filled %ld  skipped %ld  %.1f us\n",
           okTol, RUNS, okTol ? fillSum/okTol : 0, okTol ? skipSum/okTol : 0, tTol/RUNS);
    printf("  tolerate + zero-fill      ok %2ld/%d\n", okTolZ, RUNS);
    printf("  fill content: prev-line %s, zero %s, intact head %s\n",
           fillContentOK ? "OK" : "BAD", zeroContentOK ? "OK" : "BAD", headOK ? "OK" : "BAD");
    if (okTol != RUNS || okTolZ != RUNS || !fillContentOK || !zeroContentOK || !headOK) {
        printf("  FAIL: single-CRC-error category\n"); iFail++;
    }

    // ---------- category 3: multiple chunk CRC errors (tEXt + IDAT2) ----------
    printf("\n[3] multiple chunk CRC errors (tEXt + IDAT2, %d variants)\n", RUNS);
    srand(4321);
    okCur = okCrc = okTol = skipSum = fillSum = 0; tTol = 0;
    for (int k = 0; k < RUNS; k++) {
        memcpy(sample, pngbuf, pnglen);
        sample[offTextData + rand() % TEXT_LEN] ^= 0xA5;   // corrupt tEXt
        sample[offIdat2Data + rand() % IDAT2_LEN] ^= 0x5A; // corrupt IDAT2
        if (decode(sample, pnglen, 0, scratch).rc == PNG_SUCCESS) okCur++;
        if (decode(sample, pnglen, PNG_CHECK_CRC, scratch).rc == PNG_SUCCESS) okCrc++;
        Result r = decode(sample, pnglen, PNG_TOLERATE_CRC, scratch);
        if (r.rc == PNG_SUCCESS) { okTol++; fillSum += r.filled; skipSum += r.skipped; tTol += r.us; }
    }
    printf("  current (options=0)       ok %2ld/%d\n", okCur, RUNS);
    printf("  current (PNG_CHECK_CRC)   ok %2ld/%d\n", okCrc, RUNS);
    printf("  tolerate + prev-line      ok %2ld/%d  filled %ld  skipped %ld  %.1f us\n",
           okTol, RUNS, okTol ? fillSum/okTol : 0, okTol ? skipSum/okTol : 0, tTol/RUNS);
    if (okTol != RUNS || (okTol && skipSum/okTol != 2)) {
        printf("  FAIL: multi-CRC-error category\n"); iFail++;
    }

    // ---------- category 4: truncated IDAT ----------
    printf("\n[4] truncated IDAT (file cut 400 bytes into IDAT2 data)\n");
    int truncLen = offIdat2Data + 400;
    Result r0 = decode(pngbuf, truncLen, 0, scratch);
    Result rC = decode(pngbuf, truncLen, PNG_CHECK_CRC, scratch);
    Result rT = decode(pngbuf, truncLen, PNG_TOLERATE_CRC, scratch);
    printf("  current (options=0)       rc=%d err=%d\n", r0.rc, r0.err);
    printf("  current (PNG_CHECK_CRC)   rc=%d err=%d\n", rC.rc, rC.err);
    printf("  tolerate + prev-line      rc=%d filled=%ld skipped=%ld  %.1f us\n",
           rT.rc, rT.filled, rT.skipped, rT.us);
    int truncFillOK = 1;
    for (int y = 20; y < H; y++)
        for (int x = 0; x < W; x++) if (scratch[y*W+x] != 76) truncFillOK = 0;
    if (rT.rc != PNG_SUCCESS || rT.filled != 28*W || !truncFillOK) {
        printf("  FAIL: truncated-IDAT category\n"); iFail++;
    }

    printf("\n%s\n", iFail ? "CHECKS FAILED" : "ALL CHECKS PASSED");
    free(baseline); free(scratch); free(sample);
    return iFail ? 1 : 0;
}
