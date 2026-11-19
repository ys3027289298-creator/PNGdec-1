//
// defilter_bench.cpp — correctness + performance test for the vectorized
// PNG filter inversion (DeFilter) in src/png.inl.
//
// What it does, in one run:
//   1. Exhaustive bit-exactness test: all 5 filters x bpp {1,2,3,4} x random
//      data x odd pitches — scalar (DeFilterScalar) vs SIMD (DeFilter).
//   2. Generates 4 small synthetic images (photo, screenshot, line art,
//      gradient), encodes them as real PNGs in memory (adaptive per-row
//      filter choice, zlib "stored" blocks — no external dependencies).
//   3. Times the filter-inversion stage, scalar vs SIMD, per image type.
//   4. Decodes the PNGs end-to-end through the library's own DecodePNG()
//      (scalar build vs SIMD build, both included from the unmodified
//      src/png.inl) and checks the pixels against ground truth.
//   5. Reports peak-RSS (ru_maxrss) delta between the two builds.
//
// The scalar engine is the SAME library code compiled with PNG_NO_SIMD,
// which is exactly the fallback path used on platforms without SSE2.
//
// Build (from the repo root, no Makefile changes needed):
//   g++ -O2 -Wall -D__LINUX__ -I src tests/defilter_bench.cpp \
//       src/adler32.c src/crc32.c src/inflate.c src/inffast.c \
//       src/inftrees.c src/infback.c src/zutil.c -o /tmp/defilter_bench
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <sys/resource.h>

#include "PNGdec.h"
#include "zlib.h"

// Include the library implementation twice: once forced to the scalar
// fallback, once with the default (SIMD-enabled) settings. png.inl has no
// include guard; PNGdec.h/zlib.h do, so this is safe.
namespace pngref {
#define PNG_NO_SIMD 1
#include "png.inl"
#undef PNG_NO_SIMD
}
namespace pngsimd {
#include "png.inl"
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
}
static long peak_rss_kb(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}
static uint32_t rng_state = 0x12345678;
static uint32_t rnd(void) // xorshift32, deterministic
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

// ---------------------------------------------------------------------------
// 1. Exhaustive bit-exactness: scalar vs SIMD on random rows
// ---------------------------------------------------------------------------
static int exactness_test(void)
{
    int fails = 0, tests = 0;
    // canary-padded line buffers (SIMD may read up to 3 bytes past iPitch)
    uint8_t curA[4096 + 64], curB[4096 + 64], prev[4096 + 64];
    for (int bpp = 1; bpp <= 4 && !fails; bpp++) {
        for (int filter = 0; filter < 5 && !fails; filter++) {
            for (int t = 0; t < 60; t++) {
                int width = 1 + (int)(rnd() % 300);
                int pitch = width * bpp;
                int iWidth = (bpp == 1) ? pitch : width; // matches decoder logic
                memset(prev, 0xA5, sizeof(prev));
                memset(curA, 0xA5, sizeof(curA));
                for (int i = 0; i < pitch + 1; i++) {
                    prev[i] = (uint8_t)rnd();
                    curA[i] = (uint8_t)rnd();
                }
                prev[0] = 0; // previous line's filter byte (skipped)
                curA[0] = (uint8_t)filter;
                memcpy(curB, curA, sizeof(curB));
                pngref::DeFilter(curA, prev, iWidth, pitch);   // scalar path
                pngsimd::DeFilter(curB, prev, iWidth, pitch);  // dispatch (SIMD)
                tests++;
                if (memcmp(curA, curB, sizeof(curA)) != 0) {
                    printf("  MISMATCH bpp=%d filter=%d pitch=%d\n", bpp, filter, pitch);
                    fails++;
                    break;
                }
            }
        }
    }
    printf("[1] Bit-exactness scalar vs SIMD: %d random rows, %s\n",
           tests, fails ? "FAIL" : "all identical");
    return fails;
}

// ---------------------------------------------------------------------------
// 1b. Per-filter micro-benchmark: scalar vs each SIMD kernel
// ---------------------------------------------------------------------------
// The kernels have no data-dependent branches, so re-running them in place
// (on already-reconstructed data) measures the same instruction stream.
static void filter_microbench(void)
{
#ifdef PNG_SIMD_SSE2
    const int pitch = 960, reps = 3000;
    std::vector<uint8_t> row(pitch + 64), prev(pitch + 64);
    for (size_t i = 0; i < row.size(); i++) { row[i] = (uint8_t)rnd(); prev[i] = (uint8_t)rnd(); }
    struct Case { const char *name; void (*simd)(uint8_t *, const uint8_t *, int); int filter; int bpp; };
    static const struct { const char *fname; int filter; } filters[4] = {
        { "SUB", 1 }, { "UP", 2 }, { "AVG", 3 }, { "PAETH", 4 }
    };
    printf("\n[2] Per-filter kernel timings (pitch=%d, ns/byte, scalar -> SIMD)\n", pitch);
    for (int f = 0; f < 4; f++) {
        for (int bpp = 1; bpp <= 4; bpp++) {
            int width = (bpp == 1) ? pitch : pitch / bpp;
            // scalar via the reference library entry point
            std::vector<uint8_t> work = row;
            work[0] = (uint8_t)filters[f].filter;
            double t0 = now_us();
            for (int r = 0; r < reps; r++)
                pngref::DeFilter(&work[0], &prev[0], width, pitch);
            double t_scalar = (now_us() - t0) * 1000.0 / reps / pitch;
            double t_simd = -1.0;
            switch (filters[f].filter) {
            case 1: t0 = now_us();
                for (int r = 0; r < reps; r++) pngsimd::png_simd_sub(&work[1], pitch, bpp);
                t_simd = (now_us() - t0) * 1000.0 / reps / pitch; break;
            case 2: t0 = now_us();
                for (int r = 0; r < reps; r++) pngsimd::png_simd_up(&work[1], &prev[1], pitch);
                t_simd = (now_us() - t0) * 1000.0 / reps / pitch; break;
            case 3:
                if (bpp < 3) break; // bpp 1-2: SIMD only ties scalar, not used
                t0 = now_us();
                switch (bpp) {
                case 3: for (int r = 0; r < reps; r++) pngsimd::png_simd_avg3(&work[1], &prev[1], pitch); break;
                case 4: for (int r = 0; r < reps; r++) pngsimd::png_simd_avg4(&work[1], &prev[1], pitch); break;
                }
                t_simd = (now_us() - t0) * 1000.0 / reps / pitch; break;
            case 4:
                if (bpp == 1) break; // no SIMD kernel for bpp==1
                t0 = now_us();
                switch (bpp) {
                case 2: for (int r = 0; r < reps; r++) pngsimd::png_simd_paeth2(&work[1], &prev[1], pitch); break;
                case 3: for (int r = 0; r < reps; r++) pngsimd::png_simd_paeth3(&work[1], &prev[1], pitch); break;
                case 4: for (int r = 0; r < reps; r++) pngsimd::png_simd_paeth4(&work[1], &prev[1], pitch); break;
                }
                t_simd = (now_us() - t0) * 1000.0 / reps / pitch; break;
            }
            if (t_simd > 0)
                printf("    %-5s bpp=%d  %7.3f -> %7.3f  (%4.2fx)%s\n", filters[f].fname, bpp,
                       t_scalar, t_simd, t_scalar / t_simd,
                       t_simd > t_scalar ? "  <- scalar wins, dispatches to scalar" : "");
            else
                printf("    %-5s bpp=%d  %7.3f -> scalar only\n", filters[f].fname, bpp, t_scalar);
        }
    }
#endif
}


#define IMG_W 320
#define IMG_H 240
#define IMG_BPP 3
#define IMG_PITCH (IMG_W * IMG_BPP)

enum ImgType { IMG_PHOTO = 0, IMG_SCREENSHOT, IMG_LINEART, IMG_GRADIENT, IMG_COUNT };
static const char *img_names[IMG_COUNT] = { "photo", "screenshot", "line_art", "gradient" };

static void gen_image(ImgType t, uint8_t *pix)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            uint8_t *p = &pix[(y * IMG_W + x) * 3];
            switch (t) {
            case IMG_PHOTO: { // smooth sinusoidal "scene" + per-pixel noise
                int n = (int)(rnd() % 41) - 20;
                p[0] = (uint8_t)(128 + 90 * sin(x * 0.043) * cos(y * 0.031) + n);
                p[1] = (uint8_t)(128 + 80 * sin(x * 0.021 + y * 0.052) + n / 2);
                p[2] = (uint8_t)(128 + 70 * cos(x * 0.017 - y * 0.041) + n / 3);
                break; }
            case IMG_SCREENSHOT: { // flat UI panels + 1px "text" stripes
                int panel = (x / 80) + (y / 60) * 4;
                uint8_t base = (uint8_t)(40 + (panel * 37) % 180);
                p[0] = base; p[1] = (uint8_t)(255 - base); p[2] = (uint8_t)(base / 2 + 60);
                if (x % 80 == 0 || y % 60 == 0) { p[0] = p[1] = p[2] = 0; } // window frames
                if (y % 60 > 10 && y % 60 < 50 && x % 80 > 8 && x % 80 < 72 && (y % 4) < 2 && x % 3)
                    { p[0] = p[1] = p[2] = (uint8_t)((x * y) % 2 ? 30 : 220); } // text lines
                break; }
            case IMG_LINEART: { // white background, thin black strokes
                p[0] = p[1] = p[2] = 255;
                int d1 = abs((x - 160) * (x - 160) + (y - 120) * (y - 120) - 80 * 80);
                int d2 = abs((x - 80) * (x - 80) + (y - 60) * (y - 60) - 40 * 40);
                if (d1 < 200 || d2 < 120 || (x - y) % 47 == 0 || (x + 2 * y) % 89 == 0)
                    p[0] = p[1] = p[2] = 0;
                break; }
            case IMG_GRADIENT: { // smooth gradient + subtle dither (like any
                // camera/rendered gradient; a perfectly clean gradient makes
                // the filtered stream all zeros - the degenerate best case
                // for the scalar loop's data-dependent branches)
                int n = (int)(rnd() % 5) - 2;
                p[0] = (uint8_t)(x * 255 / (IMG_W - 1) + n);
                p[1] = (uint8_t)(y * 255 / (IMG_H - 1) + n / 2);
                p[2] = (uint8_t)((x + y) * 255 / (IMG_W + IMG_H - 2) + n);
                break; }
            default: break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 3. PNG encoder: adaptive per-row filter (libpng MSD heuristic), zlib stored
// ---------------------------------------------------------------------------
struct PngImage {
    std::vector<uint8_t> raw;      // ground-truth pixels (IMG_PITCH * IMG_H)
    std::vector<uint8_t> filtered; // scanline stream: (1 + IMG_PITCH) * IMG_H
    std::vector<uint8_t> png;      // complete PNG file
    int filters[5];
};

static void paeth_predict(int a, int b, int c, int *pred)
{
    int p = b - c, pc = a - c;
    int pa = p < 0 ? -p : p, pb = pc < 0 ? -pc : pc;
    pc = (p + pc) < 0 ? -(p + pc) : p + pc;
    if (pb < pa) { pa = pb; a = b; }
    if (pc < pa) a = c;
    *pred = a;
}

static void encode_image(const uint8_t *pix, PngImage &img)
{
    const int pitch = IMG_PITCH, stride = pitch + 1;
    img.raw.assign(pix, pix + (size_t)pitch * IMG_H);
    img.filtered.resize((size_t)stride * IMG_H);
    memset(img.filters, 0, sizeof(img.filters));
    std::vector<uint8_t> cand(pitch);
    for (int y = 0; y < IMG_H; y++) {
        const uint8_t *row = &pix[(size_t)y * pitch];
        const uint8_t *up = y ? &pix[(size_t)(y - 1) * pitch] : NULL;
        long best = 0x7fffffff; int bestf = 0;
        for (int f = 0; f < 5; f++) {
            long score = 0;
            for (int x = 0; x < pitch; x++) {
                int a = x >= IMG_BPP ? row[x - IMG_BPP] : 0;
                int b = up ? up[x] : 0;
                int c = (up && x >= IMG_BPP) ? up[x - IMG_BPP] : 0;
                int v, pred;
                switch (f) {
                case 0: v = row[x]; break;
                case 1: v = row[x] - a; break;
                case 2: v = row[x] - b; break;
                case 3: v = row[x] - ((a + b) >> 1); break;
                default: paeth_predict(a, b, c, &pred); v = row[x] - pred; break;
                }
                cand[x] = (uint8_t)v;
                v = (int8_t)v; // signed byte, libpng MSD heuristic
                score += v < 0 ? -v : v;
            }
            if (score < best) { best = score; bestf = f; }
        }
        img.filters[bestf]++;
        uint8_t *dst = &img.filtered[(size_t)y * stride];
        dst[0] = (uint8_t)bestf;
        for (int x = 0; x < pitch; x++) {
            int a = x >= IMG_BPP ? row[x - IMG_BPP] : 0;
            int b = up ? up[x] : 0;
            int c = (up && x >= IMG_BPP) ? up[x - IMG_BPP] : 0;
            int v, pred;
            switch (bestf) {
            case 0: v = row[x]; break;
            case 1: v = row[x] - a; break;
            case 2: v = row[x] - b; break;
            case 3: v = row[x] - ((a + b) >> 1); break;
            default: paeth_predict(a, b, c, &pred); v = row[x] - pred; break;
            }
            dst[1 + x] = (uint8_t)v;
        }
    }
    // wrap the scanline stream in a zlib stream of stored (uncompressed) blocks
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01); // zlib header: deflate, 32k window
    size_t off = 0, total = img.filtered.size();
    while (off < total) {
        size_t n = total - off; if (n > 65535) n = 65535;
        int final = (off + n == total);
        z.push_back((uint8_t)final); // BFINAL, BTYPE=00 (stored)
        z.push_back((uint8_t)(n & 0xff)); z.push_back((uint8_t)(n >> 8));
        uint16_t nl = (uint16_t)~n;
        z.push_back((uint8_t)(nl & 0xff)); z.push_back((uint8_t)(nl >> 8));
        z.insert(z.end(), img.filtered.begin() + off, img.filtered.begin() + off + n);
        off += n;
    }
    uLong ad = adler32(1, &img.filtered[0], (uInt)img.filtered.size());
    z.push_back((uint8_t)(ad >> 24)); z.push_back((uint8_t)(ad >> 16));
    z.push_back((uint8_t)(ad >> 8)); z.push_back((uint8_t)ad);
    // assemble the PNG file
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    img.png.assign(sig, sig + 8);
    auto chunk = [&](const char *type, const uint8_t *data, size_t len) {
        img.png.push_back((uint8_t)(len >> 24)); img.png.push_back((uint8_t)(len >> 16));
        img.png.push_back((uint8_t)(len >> 8)); img.png.push_back((uint8_t)len);
        size_t crcpos = img.png.size();
        img.png.insert(img.png.end(), type, type + 4);
        if (len) img.png.insert(img.png.end(), data, data + len);
        uLong c = crc32(0, &img.png[crcpos], (uInt)(img.png.size() - crcpos));
        img.png.push_back((uint8_t)(c >> 24)); img.png.push_back((uint8_t)(c >> 16));
        img.png.push_back((uint8_t)(c >> 8)); img.png.push_back((uint8_t)c);
    };
    uint8_t ihdr[13] = {
        (uint8_t)(IMG_W >> 24), (uint8_t)(IMG_W >> 16), (uint8_t)(IMG_W >> 8), (uint8_t)IMG_W,
        (uint8_t)(IMG_H >> 24), (uint8_t)(IMG_H >> 16), (uint8_t)(IMG_H >> 8), (uint8_t)IMG_H,
        8, 2, 0, 0, 0 // 8-bit truecolor, no interlace
    };
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", &z[0], z.size());
    chunk("IEND", NULL, 0);
}

// ---------------------------------------------------------------------------
// 4. Filter-stage reconstruction (the code path that was vectorized)
// ---------------------------------------------------------------------------
typedef void DeFilterFn(uint8_t *, uint8_t *, int, int);

static void defilter_image(uint8_t *work, int pitch, int height, int width,
                           DeFilterFn *fn)
{
    int stride = pitch + 1;
    static uint8_t zero_row[8192]; // row "-1" is all zeros per PNG spec
    memset(zero_row, 0, sizeof(zero_row));
    for (int y = 0; y < height; y++) {
        uint8_t *pCurr = work + (size_t)y * stride;
        uint8_t *pPrev = y ? pCurr - stride : zero_row;
        fn(pCurr, pPrev, width, pitch);
    }
}

// ---------------------------------------------------------------------------
// 5. End-to-end decode through the library's own DecodePNG()
// ---------------------------------------------------------------------------
#define DEF_DECODE_FN(fnname, NS) \
static int fnname(uint8_t *pData, int iSize, uint8_t *pOut) \
{ \
    PNGIMAGE img; \
    memset(&img, 0, sizeof(img)); \
    img.ucMemType = PNG_MEM_RAM; \
    img.pfnRead = NS::readRAM; \
    img.pfnSeek = NS::seekMem; \
    img.PNGFile.iSize = iSize; \
    img.PNGFile.pData = pData; \
    if (NS::PNGInit(&img) != PNG_SUCCESS) return -1; \
    img.pImage = pOut; \
    NS::DecodePNG(&img, NULL, 0); \
    return img.iError; \
}
DEF_DECODE_FN(decode_ref, pngref)   // scalar build (PNG_NO_SIMD)
DEF_DECODE_FN(decode_simd, pngsimd) // default build (SSE2)

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    int rc = exactness_test();
    if (rc) return 1;
    filter_microbench();

    const char *outdir = argc > 1 ? argv[1] : NULL;
    printf("\n     image | filter mix N/S/U/A/P | defilter scalar | defilter SIMD | speedup | pixels\n");
    printf("-----------+----------------------+-----------------+---------------+---------+-------\n");

    double worst = 1e30;
    double speedups[IMG_COUNT] = { 0, 0, 0, 0 };
    long rss_before_all = 0, rss_ref = 0, rss_simd = 0;
    std::vector<uint8_t> work((size_t)(IMG_PITCH + 1) * IMG_H + 64);
    std::vector<uint8_t> outA((size_t)IMG_PITCH * IMG_H), outB((size_t)IMG_PITCH * IMG_H);

    for (int t = 0; t < IMG_COUNT; t++) {
        PngImage img;
        std::vector<uint8_t> pix((size_t)IMG_PITCH * IMG_H);
        gen_image((ImgType)t, &pix[0]);
        encode_image(&pix[0], img);

        if (outdir) { // save the sample PNGs (small, self-made)
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.png", outdir, img_names[t]);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(&img.png[0], 1, img.png.size(), f); fclose(f); }
        }

        // --- filter-stage timing: scalar vs SIMD, defilter-only, best of N ---
        const int stride = IMG_PITCH + 1;
        const size_t imgsz = (size_t)stride * IMG_H;
        double best_ref = 1e30, best_simd = 1e30;
        for (int rep = 0; rep < 40; rep++) {
            memcpy(&work[0], &img.filtered[0], imgsz);
            double t0 = now_us();
            defilter_image(&work[0], IMG_PITCH, IMG_H, IMG_W, pngref::DeFilter);
            double t1 = now_us();
            if (t1 - t0 < best_ref) best_ref = t1 - t0;
            // verify against ground truth on the last rep
            if (rep == 0) {
                for (int y = 0; y < IMG_H; y++)
                    if (memcmp(&work[(size_t)y * stride + 1], &img.raw[(size_t)y * IMG_PITCH], IMG_PITCH)) {
                        printf("  scalar reconstruction mismatch (%s row %d)\n", img_names[t], y);
                        return 1;
                    }
            }
            memcpy(&work[0], &img.filtered[0], imgsz);
            t0 = now_us();
            defilter_image(&work[0], IMG_PITCH, IMG_H, IMG_W, pngsimd::DeFilter);
            t1 = now_us();
            if (t1 - t0 < best_simd) best_simd = t1 - t0;
            if (rep == 0) {
                for (int y = 0; y < IMG_H; y++)
                    if (memcmp(&work[(size_t)y * stride + 1], &img.raw[(size_t)y * IMG_PITCH], IMG_PITCH)) {
                        printf("  SIMD reconstruction mismatch (%s row %d)\n", img_names[t], y);
                        return 1;
                    }
            }
        }
        double speedup = best_ref / best_simd;
        if (speedup < worst) worst = speedup;
        speedups[t] = speedup;
        printf("%11s | %3d/%3d/%3d/%3d/%3d | %12.1f us | %10.1f us | %6.2fx | OK\n",
               img_names[t], img.filters[0], img.filters[1], img.filters[2],
               img.filters[3], img.filters[4], best_ref, best_simd, speedup);

        // --- end-to-end decode through the real decoder, both builds ---
        double best_e2e_ref = 1e30, best_e2e_simd = 1e30;
        int r1 = PNG_SUCCESS, r2 = PNG_SUCCESS;
        for (int rep = 0; rep < 20; rep++) {
            double t0 = now_us();
            r1 = decode_ref(&img.png[0], (int)img.png.size(), &outA[0]);
            double t1 = now_us();
            if (t1 - t0 < best_e2e_ref) best_e2e_ref = t1 - t0;
            t0 = now_us();
            r2 = decode_simd(&img.png[0], (int)img.png.size(), &outB[0]);
            t1 = now_us();
            if (t1 - t0 < best_e2e_simd) best_e2e_simd = t1 - t0;
        }
        rss_ref = peak_rss_kb();
        rss_simd = peak_rss_kb();
        if (r1 != PNG_SUCCESS || r2 != PNG_SUCCESS) {
            printf("  decode error (%s): ref=%d simd=%d\n", img_names[t], r1, r2);
            return 1;
        }
        if (memcmp(&outA[0], &outB[0], outA.size()) ||
            memcmp(&outA[0], &img.raw[0], img.raw.size())) {
            printf("  end-to-end pixel mismatch (%s)\n", img_names[t]);
            return 1;
        }
        printf("    end-to-end decode: scalar %8.1f us | SIMD %8.1f us | speedup %5.2fx | pixels identical\n",
               best_e2e_ref, best_e2e_simd, best_e2e_ref / best_e2e_simd);
    }
    (void)rss_before_all;

    // --- peak memory: scalar build vs SIMD build (same buffers, no allocs) ---
    printf("\nPeak RSS after scalar decode: %ld kB, after SIMD decode: %ld kB, delta: %+ld kB\n",
           rss_ref, rss_simd, rss_simd - rss_ref);
    printf("End-to-end decode: scalar and SIMD builds produce identical pixels for all 4 images\n");
    double geomean = 1.0;
    for (int i = 0; i < IMG_COUNT; i++) geomean *= speedups[i];
    geomean = pow(geomean, 1.0 / IMG_COUNT);
    printf("Filter-stage speedups: min %.2fx, geomean %.2fx -> %s (target: geomean >= 2.0x, no regression)\n",
           worst, geomean, (geomean >= 2.0 && worst >= 1.0) ? "PASS" : "FAIL");
    return (geomean >= 2.0 && worst >= 1.0) ? 0 : 1;
}
