//
// test_rgba8888.cpp
//
// Tests for the new RGBA8888 decode path in PNGdec.h:
//   stage 1: decode any of the 5 PNG color types to unified RGBA8888
//   stage 2: optional premultiplied alpha        (PNG_RGBA8888_PREMULTIPLY)
//   stage 3: optional output of a row range only (yStart/yCount)
//
// Reference decoder: lodepng (vendored in examples/png_comparison).
// Sample PNGs are generated in memory (stored-deflate, hand-built chunks);
// no network, no third-party code beyond what the repo already contains.
//
// Usage:
//   test_rgba8888                  -> run all functional/timing/memory tests
//   test_rgba8888 mem <ct> <mode>  -> single memory measurement (VmHWM)
//                                     ct:   0,2,3,4,6 (PNG color type)
//                                     mode: stream | full | crop | native
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include "../src/PNGdec.h"
#include "../examples/png_comparison/lodepng.h"

// ---------------------------------------------------------------------------
// malloc wrapping: audits that the library performs zero heap allocations
// ---------------------------------------------------------------------------
static size_t g_liveBytes = 0, g_peakBytes = 0;
static unsigned long g_allocCount = 0;
#define ALLOC_MAGIC 0x504e474445433132ULL // "PNGdec12"
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t n) {
    void *p = __real_malloc(n + 16);
    if (!p) return p;
    ((size_t *)p)[0] = n;
    ((uint64_t *)p)[1] = ALLOC_MAGIC;
    g_liveBytes += n; g_allocCount++;
    if (g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
    return (char *)p + 16;
}
extern "C" void *__wrap_calloc(size_t nm, size_t sz) {
    size_t n = nm * sz;
    void *p = __real_malloc(n + 16);
    if (!p) return p;
    memset(p, 0, n + 16);
    ((size_t *)p)[0] = n;
    ((uint64_t *)p)[1] = ALLOC_MAGIC;
    g_liveBytes += n; g_allocCount++;
    if (g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
    return (char *)p + 16;
}
extern "C" void *__wrap_realloc(void *p, size_t n) {
    if (!p) return __wrap_malloc(n);
    void *base = (char *)p - 16;
    if (((uint64_t *)base)[1] != ALLOC_MAGIC) return __real_realloc(p, n);
    size_t old = ((size_t *)base)[0];
    void *np = __real_realloc(base, n + 16);
    if (!np) return np;
    ((size_t *)np)[0] = n;
    ((uint64_t *)np)[1] = ALLOC_MAGIC;
    g_liveBytes += n - old;
    if (g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
    return (char *)np + 16;
}
extern "C" void __wrap_free(void *p) {
    if (!p) return;
    void *base = (char *)p - 16;
    if (((uint64_t *)base)[1] != ALLOC_MAGIC) { __real_free(p); return; }
    g_liveBytes -= ((size_t *)base)[0];
    __real_free(base);
}
static void allocReset() { g_liveBytes = 0; g_peakBytes = 0; g_allocCount = 0; }

// ---------------------------------------------------------------------------
// Minimal PNG writer (stored deflate blocks; crc32/adler32 from repo zlib)
// ---------------------------------------------------------------------------
static void put32be(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}
static void putChunk(std::vector<uint8_t> &v, const char *type, const uint8_t *data, uint32_t len) {
    put32be(v, len);
    size_t start = v.size();
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)type[i]);
    for (uint32_t i = 0; i < len; i++) v.push_back(data[i]);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)&v[start], 4 + len);
    put32be(v, (uint32_t)crc);
}
static std::vector<uint8_t> zlibStore(const uint8_t *raw, size_t n) {
    std::vector<uint8_t> v;
    v.push_back(0x78); v.push_back(0x01); // zlib header: deflate, 32k window
    size_t off = 0;
    do {
        size_t blk = n - off;
        if (blk > 65535) blk = 65535;
        int last = (off + blk == n);
        v.push_back((uint8_t)(last ? 1 : 0)); // BFINAL, BTYPE=00 (stored)
        v.push_back((uint8_t)(blk & 0xff)); v.push_back((uint8_t)(blk >> 8));
        uint16_t nlen = (uint16_t)~(uint16_t)blk;
        v.push_back((uint8_t)(nlen & 0xff)); v.push_back((uint8_t)(nlen >> 8));
        for (size_t i = 0; i < blk; i++) v.push_back(raw[off + i]);
        off += blk;
    } while (off < n);
    uLong a = adler32(0L, Z_NULL, 0);
    a = adler32(a, (const Bytef *)raw, (uInt)n);
    put32be(v, (uint32_t)a); // zlib stores adler32 big-endian
    return v;
}

// ---------------------------------------------------------------------------
// Sample generation
// ---------------------------------------------------------------------------
struct Sample {
    std::string name;
    int w, h, bitdepth, colortype;
    bool useTrns;
    int interlace;
    int channels, pitch;
    std::vector<uint8_t> raw;     // unfiltered pixel bytes, pitch*h
    std::vector<uint8_t> palette; // 3 bytes per entry (palette images)
    std::vector<uint8_t> trns;    // tRNS chunk payload
    std::vector<uint8_t> png;     // finished PNG file
};

static uint32_t g_rng;
static uint8_t rnd8() { g_rng = g_rng * 1103515245u + 12345u; return (uint8_t)(g_rng >> 16); }

static int paethPred(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static Sample makeSample(const char *name, int w, int h, int bitdepth, int colortype,
                         bool useTrns, int interlace) {
    Sample s;
    s.name = name; s.w = w; s.h = h; s.bitdepth = bitdepth;
    s.colortype = colortype; s.useTrns = useTrns; s.interlace = interlace;
    s.channels = (colortype == 2) ? 3 : (colortype == 6) ? 4 : (colortype == 4) ? 2 : 1;
    s.pitch = (w * bitdepth * s.channels + 7) / 8;
    s.raw.resize((size_t)s.pitch * h);
    g_rng = 0xC0FFEEu + (uint32_t)(w * 131 + h * 17 + bitdepth * 7 + colortype * 3 + useTrns);

    const uint8_t trnsGray = 0x80;
    const uint8_t trnsRGB[3] = {0x12, 0x34, 0x56};
    int mask = (1 << bitdepth) - 1;

    // palette
    int nPal = (colortype == 3) ? (1 << bitdepth) : 0;
    if (nPal) {
        s.palette.resize(nPal * 3);
        for (int i = 0; i < nPal * 3; i++) s.palette[i] = rnd8();
    }

    // pixels
    for (int y = 0; y < h; y++) {
        uint8_t *row = &s.raw[(size_t)y * s.pitch];
        for (int x = 0; x < w; x++) {
            switch (colortype) {
                case 0: { // grayscale, packed
                    int v;
                    if (x == 0) v = 0;
                    else if (x == 1) v = mask;
                    else if (useTrns && bitdepth == 8 && x % 7 == 0) v = trnsGray;
                    else v = rnd8() & mask;
                    if (bitdepth == 8) row[x] = (uint8_t)v;
                    else {
                        int bit = x * bitdepth;
                        row[bit >> 3] |= (uint8_t)(v << (8 - bitdepth - (bit & 7)));
                    }
                    break;
                }
                case 4: { // grayscale + alpha
                    uint8_t g = rnd8(), a;
                    switch (x % 16) {
                        case 0: a = 0; break;   case 1: a = 255; break;
                        case 2: a = 1; break;   case 3: a = 127; break;
                        case 4: a = 128; break; case 5: a = 254; break;
                        default: a = rnd8();
                    }
                    row[x * 2] = g; row[x * 2 + 1] = a;
                    break;
                }
                case 3: { // indexed, packed
                    int v = rnd8() & mask;
                    if (bitdepth == 8) row[x] = (uint8_t)v;
                    else {
                        int bit = x * bitdepth;
                        row[bit >> 3] |= (uint8_t)(v << (8 - bitdepth - (bit & 7)));
                    }
                    break;
                }
                case 2: { // truecolor
                    uint8_t r = rnd8(), g = rnd8(), b = rnd8();
                    if (useTrns && x % 9 == 0) { r = trnsRGB[0]; g = trnsRGB[1]; b = trnsRGB[2]; }
                    row[x * 3] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
                    break;
                }
                case 6: { // truecolor + alpha
                    uint8_t r = rnd8(), g = rnd8(), b = rnd8(), a;
                    if (x % 8 == 6) { r = g = b = 0; }
                    if (x % 8 == 7) { r = g = b = 255; }
                    switch (x % 16) {
                        case 0: a = 0; break;   case 1: a = 255; break;
                        case 2: a = 1; break;   case 3: a = 127; break;
                        case 4: a = 128; break; case 5: a = 254; break;
                        default: a = rnd8();
                    }
                    row[x * 4] = r; row[x * 4 + 1] = g; row[x * 4 + 2] = b; row[x * 4 + 3] = a;
                    break;
                }
            }
        }
    }

    // tRNS payload
    if (useTrns) {
        if (colortype == 3) {
            s.trns.resize(nPal);
            for (int i = 0; i < nPal; i++) {
                switch (i % 16) {
                    case 0: s.trns[i] = 0; break;
                    case 1: s.trns[i] = 128; break;
                    case 2: s.trns[i] = 64; break;
                    case 3: s.trns[i] = 1; break;
                    case 4: s.trns[i] = 254; break;
                    default: s.trns[i] = 255;
                }
            }
        } else if (colortype == 0) {
            s.trns = {0x00, trnsGray};
        } else if (colortype == 2) {
            s.trns = {0x00, trnsRGB[0], 0x00, trnsRGB[1], 0x00, trnsRGB[2]};
        }
    }

    // filter the scanlines (cycle through all 5 filter types)
    int bppF = (bitdepth * s.channels + 7) / 8;
    std::vector<uint8_t> filtered;
    filtered.reserve((size_t)(s.pitch + 1) * h);
    for (int y = 0; y < h; y++) {
        int f = y % 5;
        const uint8_t *cur = &s.raw[(size_t)y * s.pitch];
        const uint8_t *up = (y > 0) ? &s.raw[(size_t)(y - 1) * s.pitch] : nullptr;
        filtered.push_back((uint8_t)f);
        for (int i = 0; i < s.pitch; i++) {
            int left = (i >= bppF) ? cur[i - bppF] : 0;
            int u = up ? up[i] : 0;
            int ul = (up && i >= bppF) ? up[i - bppF] : 0;
            int v = cur[i];
            switch (f) {
                case 0: break;
                case 1: v -= left; break;
                case 2: v -= u; break;
                case 3: v -= (left + u) / 2; break;
                case 4: v -= paethPred(left, u, ul); break;
            }
            filtered.push_back((uint8_t)v);
        }
    }

    // assemble the PNG file
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    s.png.assign(sig, sig + 8);
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = (uint8_t)bitdepth;  ihdr[9] = (uint8_t)colortype;
    ihdr[10] = 0; ihdr[11] = 0;   ihdr[12] = (uint8_t)interlace;
    putChunk(s.png, "IHDR", ihdr, 13);
    if (nPal) putChunk(s.png, "PLTE", s.palette.data(), (uint32_t)s.palette.size());
    if (!s.trns.empty()) putChunk(s.png, "tRNS", s.trns.data(), (uint32_t)s.trns.size());
    std::vector<uint8_t> idat = zlibStore(filtered.data(), filtered.size());
    putChunk(s.png, "IDAT", idat.data(), (uint32_t)idat.size());
    putChunk(s.png, "IEND", nullptr, 0);
    return s;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, label) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", label); } \
    else { g_fail++; printf("  FAIL  %s\n", label); } \
} while (0)

static bool refDecode(const Sample &s, std::vector<uint8_t> &rgba) {
    unsigned char *out = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&out, &w, &h, s.png.data(), s.png.size());
    if (err != 0 || w != (unsigned)s.w || h != (unsigned)s.h) {
        if (out) free(out);
        return false;
    }
    rgba.assign(out, out + (size_t)w * h * 4);
    free(out);
    return true;
}

static void premultiplyRef(std::vector<uint8_t> &rgba) {
    for (size_t i = 0; i < rgba.size(); i += 4) {
        uint32_t a = rgba[i + 3];
        if (a != 255) {
            rgba[i]     = (uint8_t)((rgba[i]     * a + 127) / 255);
            rgba[i + 1] = (uint8_t)((rgba[i + 1] * a + 127) / 255);
            rgba[i + 2] = (uint8_t)((rgba[i + 2] * a + 127) / 255);
        }
    }
}

static int countDiff(const uint8_t *a, const uint8_t *b, size_t n, size_t *first) {
    int diff = 0;
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) { if (diff == 0) *first = i; diff++; }
    return diff;
}

static double nowSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

static long readVmHWMkb() {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmHWM: %ld kB", &kb) == 1) break;
    }
    fclose(f);
    return kb;
}

// ---------------------------------------------------------------------------
// Per-sample consistency + native regression
// ---------------------------------------------------------------------------
static void testConsistency(const Sample &s) {
    std::vector<uint8_t> ref;
    if (!refDecode(s, ref)) {
        printf("  FAIL  %-22s reference decoder rejected the sample\n", s.name.c_str());
        g_fail++;
        return;
    }
    const int W = s.w, H = s.h;
    char label[128];

    // stage 1: full decode to RGBA8888
    std::vector<uint32_t> out((size_t)W * H, 0xdeadbeef);
    PNG png;
    int rc = png.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
    snprintf(label, sizeof(label), "%-22s open", s.name.c_str());
    CHECK(rc == PNG_SUCCESS, label);
    rc = png.decodeRGBA8888(out.data(), 0, H, 0);
    size_t first = 0;
    int diff = countDiff((uint8_t *)out.data(), ref.data(), (size_t)W * H * 4, &first);
    snprintf(label, sizeof(label), "%-22s RGBA8888 == reference (%d px%s)", s.name.c_str(),
             diff / 4, diff ? " DIFFER" : "");
    CHECK(rc == PNG_SUCCESS && diff == 0, label);

    // stage 1+2: premultiplied alpha
    std::vector<uint8_t> refPre = ref;
    premultiplyRef(refPre);
    std::vector<uint32_t> outPre((size_t)W * H, 0xdeadbeef);
    rc = png.decodeRGBA8888(outPre.data(), 0, H, PNG_RGBA8888_PREMULTIPLY);
    diff = countDiff((uint8_t *)outPre.data(), refPre.data(), (size_t)W * H * 4, &first);
    snprintf(label, sizeof(label), "%-22s premultiply == reference", s.name.c_str());
    CHECK(rc == PNG_SUCCESS && diff == 0, label);

    // stage 1+3: row range [H/4, H/4+H/2)
    int yStart = H / 4, yCount = H / 2;
    std::vector<uint32_t> outCrop((size_t)W * yCount, 0xdeadbeef);
    rc = png.decodeRGBA8888(outCrop.data(), yStart, yCount, 0);
    diff = countDiff((uint8_t *)outCrop.data(), ref.data() + (size_t)yStart * W * 4,
                     (size_t)W * yCount * 4, &first);
    snprintf(label, sizeof(label), "%-22s rows [%d,%d) == reference", s.name.c_str(),
             yStart, yStart + yCount);
    CHECK(rc == PNG_SUCCESS && diff == 0, label);

    // stage 1+2+3: row range + premultiply together
    std::vector<uint32_t> outCropPre((size_t)W * yCount, 0xdeadbeef);
    rc = png.decodeRGBA8888(outCropPre.data(), yStart, yCount, PNG_RGBA8888_PREMULTIPLY);
    diff = countDiff((uint8_t *)outCropPre.data(), refPre.data() + (size_t)yStart * W * 4,
                     (size_t)W * yCount * 4, &first);
    snprintf(label, sizeof(label), "%-22s rows+premult == reference", s.name.c_str());
    CHECK(rc == PNG_SUCCESS && diff == 0, label);

    // row range clamped to image bounds
    std::vector<uint32_t> outClamp((size_t)W * 2, 0xdeadbeef);
    rc = png.decodeRGBA8888(outClamp.data(), H - 2, 1000, 0);
    diff = countDiff((uint8_t *)outClamp.data(), ref.data() + (size_t)(H - 2) * W * 4,
                     (size_t)W * 2 * 4, &first);
    snprintf(label, sizeof(label), "%-22s clamped range == reference", s.name.c_str());
    CHECK(rc == PNG_SUCCESS && diff == 0, label);
    png.close();

    // regression: legacy native decode must reproduce the original pixels
    PNG png2;
    rc = png2.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
    std::vector<uint8_t> native(png2.getBufferSize());
    png2.setBuffer(native.data());
    rc = png2.decode(NULL, 0);
    diff = countDiff(native.data(), s.raw.data(), (size_t)s.pitch * H, &first);
    snprintf(label, sizeof(label), "%-22s native output unchanged", s.name.c_str());
    CHECK(rc == PNG_SUCCESS && diff == 0, label);
    png2.close();
}

// ---------------------------------------------------------------------------
// Error handling for unsupported combinations
// ---------------------------------------------------------------------------
static void testErrors(const Sample &valid) {
    printf("== error handling ==\n");
    char label[128];
    // 16-bit and interlaced files are rejected at open()
    Sample s16 = makeSample("gray16", 33, 17, 16, 0, false, 0);
    PNG p1;
    int rc = p1.openRAM(s16.png.data(), (int)s16.png.size(), NULL);
    snprintf(label, sizeof(label), "16-bit depth -> PNG_UNSUPPORTED_FEATURE (%d)", p1.getLastError());
    CHECK(rc != PNG_SUCCESS && p1.getLastError() == PNG_UNSUPPORTED_FEATURE, label);

    Sample sIL = makeSample("gray8-interlaced", 33, 17, 8, 0, false, 1);
    PNG p2;
    rc = p2.openRAM(sIL.png.data(), (int)sIL.png.size(), NULL);
    snprintf(label, sizeof(label), "interlaced -> PNG_UNSUPPORTED_FEATURE (%d)", p2.getLastError());
    CHECK(rc != PNG_SUCCESS && p2.getLastError() == PNG_UNSUPPORTED_FEATURE, label);

    // invalid decodeRGBA8888 parameters
    PNG p3;
    rc = p3.openRAM((uint8_t *)valid.png.data(), (int)valid.png.size(), NULL);
    std::vector<uint32_t> buf((size_t)valid.w * valid.h);
    CHECK(p3.decodeRGBA8888(NULL, 0, 1, 0) == PNG_INVALID_PARAMETER &&
          p3.getLastError() == PNG_INVALID_PARAMETER, "NULL output -> PNG_INVALID_PARAMETER");
    CHECK(p3.decodeRGBA8888(buf.data(), -1, 1, 0) == PNG_INVALID_PARAMETER,
          "negative yStart -> PNG_INVALID_PARAMETER");
    CHECK(p3.decodeRGBA8888(buf.data(), 0, 0, 0) == PNG_INVALID_PARAMETER,
          "zero yCount -> PNG_INVALID_PARAMETER");
    CHECK(p3.decodeRGBA8888(buf.data(), valid.h, 1, 0) == PNG_INVALID_PARAMETER,
          "yStart >= height -> PNG_INVALID_PARAMETER");
    CHECK(p3.decodeRGBA8888(buf.data(), 0, 1, 0x8000) == PNG_INVALID_PARAMETER,
          "unknown flag bits -> PNG_INVALID_PARAMETER");

    // setBuffer() whole-image mode is an unsupported combination
    PNG p4;
    p4.openRAM((uint8_t *)valid.png.data(), (int)valid.png.size(), NULL);
    std::vector<uint8_t> tmp(p4.getBufferSize());
    p4.setBuffer(tmp.data());
    CHECK(p4.decodeRGBA8888(buf.data(), 0, 1, 0) == PNG_INVALID_PARAMETER &&
          p4.getLastError() == PNG_INVALID_PARAMETER,
          "setBuffer()+decodeRGBA8888 -> PNG_INVALID_PARAMETER");

    // unknown pixel type in the line converter
    PNGDRAW d;
    memset(&d, 0, sizeof(d));
    d.iPixelType = 99;
    d.iWidth = 1;
    uint8_t one = 0;
    uint32_t px;
    d.pPixels = &one;
    CHECK(p3.getLineAsRGBA8888(&d, &px, 0) == PNG_UNSUPPORTED_FEATURE,
          "unknown pixel type -> PNG_UNSUPPORTED_FEATURE");
    CHECK(p3.getLineAsRGBA8888(&d, NULL, 0) == PNG_INVALID_PARAMETER,
          "NULL line buffer -> PNG_INVALID_PARAMETER");
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static double timeIt(int iters, void (*fn)(void *), void *arg) {
    fn(arg); // warmup
    double best = 1e30;
    for (int i = 0; i < iters; i++) { // min-of-N: robust against scheduling noise
        double t0 = nowSec();
        fn(arg);
        double dt = nowSec() - t0;
        if (dt < best) best = dt;
    }
    return best * 1000.0; // ms
}

struct TimedCtx {
    const Sample *s;
    PNG *png;
    uint32_t *out;
    uint8_t *native;
    int yStart, yCount, flags;
};
static void runFullRGBA(void *a) {
    TimedCtx *c = (TimedCtx *)a;
    c->png->decodeRGBA8888(c->out, 0, c->s->h, c->flags);
}
static void runCropRGBA(void *a) {
    TimedCtx *c = (TimedCtx *)a;
    c->png->decodeRGBA8888(c->out, c->yStart, c->yCount, c->flags);
}
static void runNative(void *a) {
    TimedCtx *c = (TimedCtx *)a;
    c->png->decode(NULL, 0);
}
static void runLodepng(void *a) {
    TimedCtx *c = (TimedCtx *)a;
    unsigned char *out = nullptr;
    unsigned w, h;
    lodepng_decode32(&out, &w, &h, c->s->png.data(), c->s->png.size());
    free(out);
}

static void testTiming(const std::vector<Sample> &big, int iters) {
    printf("== timing (min of %d iterations, 512x512, ms/decode) ==\n", iters);
    printf("%-14s %10s %10s %10s %12s %10s | %8s %8s\n", "type", "native", "RGBA-full",
           "RGBA-crop", "crop+premul", "lodepng", "saved-ms", "saved-%");
    for (const Sample &s : big) {
        PNG png;
        png.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
        std::vector<uint32_t> out((size_t)s.w * s.h);
        std::vector<uint8_t> nativeBuf(png.getBufferSize());
        TimedCtx c{&s, &png, out.data(), nativeBuf.data(), s.h / 4, s.h / 2, 0};

        double tNative, tFull, tCrop, tCropPre, tLode;
        png.setBuffer(nativeBuf.data());
        tNative = timeIt(iters, runNative, &c);
        png.setBuffer(NULL);
        tFull = timeIt(iters, runFullRGBA, &c);
        tCrop = timeIt(iters, runCropRGBA, &c);
        c.flags = PNG_RGBA8888_PREMULTIPLY;
        tCropPre = timeIt(iters, runCropRGBA, &c);
        c.flags = 0;
        tLode = timeIt(iters, runLodepng, &c);
        png.close();
        printf("%-14s %10.3f %10.3f %10.3f %12.3f %10.3f | %8.3f %7.1f%%\n", s.name.c_str(),
               tNative, tFull, tCrop, tCropPre, tLode, tFull - tCrop,
               100.0 * (tFull - tCrop) / tFull);
    }
}

// ---------------------------------------------------------------------------
// Memory (in-process): library heap usage + line working set
// ---------------------------------------------------------------------------
static void testMemory(const std::vector<Sample> &big) {
    printf("== memory (in-process measurements) ==\n");
    printf("  sizeof(PNGIMAGE) = %zu bytes (fixed, height-independent; holds zlib state + 2 line buffers)\n",
           sizeof(PNGIMAGE));
    printf("  PNG_MAX_BUFFERED_PIXELS = %d\n", PNG_MAX_BUFFERED_PIXELS);
    printf("%-14s %12s %16s %16s %16s\n", "type", "2-line bytes", "allocs(RGBA8888)",
           "allocs(native)", "out buf bytes");
    for (const Sample &s : big) {
        PNG png;
        png.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
        int lineBytes = ((s.pitch + 1 + 15) & ~15) * 2; // curr+prev, 16-byte aligned
        std::vector<uint32_t> out((size_t)s.w * s.h);
        allocReset();
        png.decodeRGBA8888(out.data(), 0, s.h, 0);
        unsigned long allocsRGBA = g_allocCount;
        size_t peakRGBA = g_peakBytes;
        std::vector<uint8_t> nativeBuf(png.getBufferSize());
        png.setBuffer(nativeBuf.data());
        allocReset();
        png.decode(NULL, 0);
        unsigned long allocsNative = g_allocCount;
        png.close();
        printf("%-14s %12d %16lu %16lu %16zu\n", s.name.c_str(), lineBytes, allocsRGBA,
               allocsNative, (size_t)s.w * s.h * 4);
        if (allocsRGBA != 0 || peakRGBA != 0 || allocsNative != 0) {
            printf("  FAIL  library allocated heap memory during decode\n");
            g_fail++;
        } else {
            g_pass++;
        }
    }
    printf("  (decode-side heap is zero: the library keeps only the current line + one\n");
    printf("   previous line inside PNGIMAGE; the output buffer is caller-owned)\n");
}

// ---------------------------------------------------------------------------
// Memory (per-process): VmHWM for streaming vs full-buffer decode
// ---------------------------------------------------------------------------
static PNG *g_streamPNG;
static uint32_t g_streamLine[4096];
static uint64_t g_streamSum;
static int streamCallback(PNGDRAW *pDraw) {
    g_streamPNG->getLineAsRGBA8888(pDraw, g_streamLine, 0);
    for (int i = 0; i < pDraw->iWidth; i++) g_streamSum += g_streamLine[i];
    return 1;
}

static int memMode(int colortype, const char *mode) {
    Sample s = makeSample("mem", 512, 512, 8, colortype, colortype == 3, 0);
    long base = readVmHWMkb();
    PNG png2;
    size_t outBytes = 0;
    if (strcmp(mode, "stream") == 0) {
        png2.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), streamCallback);
        g_streamPNG = &png2;
        g_streamSum = 0;
        png2.decode(NULL, 0);
        outBytes = sizeof(g_streamLine); // one reusable RGBA8888 line
        printf("MODE=stream CT=%d BASE_KB=%ld VMHWM_KB=%ld OUT_BYTES=%zu SUM=%llu\n",
               colortype, base, readVmHWMkb(), outBytes, (unsigned long long)g_streamSum);
    } else if (strcmp(mode, "full") == 0) {
        png2.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
        std::vector<uint32_t> out((size_t)s.w * s.h);
        png2.decodeRGBA8888(out.data(), 0, s.h, 0);
        outBytes = (size_t)s.w * s.h * 4;
        uint64_t sum = 0;
        for (size_t i = 0; i < (size_t)s.w * s.h; i++) sum += out[i];
        printf("MODE=full CT=%d BASE_KB=%ld VMHWM_KB=%ld OUT_BYTES=%zu SUM=%llu\n",
               colortype, base, readVmHWMkb(), outBytes, (unsigned long long)sum);
    } else if (strcmp(mode, "crop") == 0) {
        png2.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
        int yCount = s.h / 2;
        std::vector<uint32_t> out((size_t)s.w * yCount);
        png2.decodeRGBA8888(out.data(), s.h / 4, yCount, 0);
        outBytes = (size_t)s.w * yCount * 4;
        uint64_t sum = 0;
        for (size_t i = 0; i < (size_t)s.w * yCount; i++) sum += out[i];
        printf("MODE=crop CT=%d BASE_KB=%ld VMHWM_KB=%ld OUT_BYTES=%zu SUM=%llu\n",
               colortype, base, readVmHWMkb(), outBytes, (unsigned long long)sum);
    } else if (strcmp(mode, "native") == 0) {
        png2.openRAM((uint8_t *)s.png.data(), (int)s.png.size(), NULL);
        std::vector<uint8_t> buf(png2.getBufferSize());
        png2.setBuffer(buf.data());
        png2.decode(NULL, 0);
        outBytes = buf.size();
        uint64_t sum = 0;
        for (uint8_t b : buf) sum += b;
        printf("MODE=native CT=%d BASE_KB=%ld VMHWM_KB=%ld OUT_BYTES=%zu SUM=%llu\n",
               colortype, base, readVmHWMkb(), outBytes, (unsigned long long)sum);
    } else {
        fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "mem") == 0)
        return memMode(atoi(argv[2]), argv[3]);

    printf("== RGBA8888 consistency vs reference decoder (lodepng, vendored) ==\n");
    // small odd-size samples: all 5 color types + bit depths + tRNS variants
    std::vector<Sample> small;
    small.push_back(makeSample("gray1", 67, 41, 1, 0, false, 0));
    small.push_back(makeSample("gray2", 67, 41, 2, 0, false, 0));
    small.push_back(makeSample("gray4", 67, 41, 4, 0, false, 0));
    small.push_back(makeSample("gray8", 67, 41, 8, 0, false, 0));
    small.push_back(makeSample("gray8+trns", 67, 41, 8, 0, true, 0));
    small.push_back(makeSample("grayalpha8", 67, 41, 8, 4, false, 0));
    small.push_back(makeSample("pal1", 67, 41, 1, 3, false, 0));
    small.push_back(makeSample("pal2", 67, 41, 2, 3, false, 0));
    small.push_back(makeSample("pal4+trns", 67, 41, 4, 3, true, 0));
    small.push_back(makeSample("pal8", 67, 41, 8, 3, false, 0));
    small.push_back(makeSample("pal8+trns", 67, 41, 8, 3, true, 0));
    small.push_back(makeSample("truecolor8", 67, 41, 8, 2, false, 0));
    small.push_back(makeSample("truecolor8+trns", 67, 41, 8, 2, true, 0));
    small.push_back(makeSample("truecoloralpha8", 67, 41, 8, 6, false, 0));
    for (const Sample &s : small) testConsistency(s);

    // larger samples for the 5 color types (consistency + timing + memory)
    std::vector<Sample> big;
    big.push_back(makeSample("gray8-512", 512, 512, 8, 0, false, 0));
    big.push_back(makeSample("grayalpha8-512", 512, 512, 8, 4, false, 0));
    big.push_back(makeSample("pal8+trns-512", 512, 512, 8, 3, true, 0));
    big.push_back(makeSample("truecolor8-512", 512, 512, 8, 2, false, 0));
    big.push_back(makeSample("truecoloralpha8-512", 512, 512, 8, 6, false, 0));
    for (const Sample &s : big) testConsistency(s);

    testErrors(small[3]); // gray8 as the valid sample

    testTiming(big, 50);
    testMemory(big);

    printf("== summary: %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
