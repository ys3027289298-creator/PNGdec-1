// Test harness for the 16-bit output path added to PNGdec.
// Self-contained: only the library under test and the C/C++ standard
// library are used. Run with the data directory and dump directory:
//   test_16bit <data_dir> <dump_dir>
// Exits non-zero if any check fails.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "../src/PNGdec.h"

// New API implemented in png.inl (no public header change allowed).
extern "C" int PNG_getLineAsU16(PNGDRAW *pDraw, uint16_t *pPixels,
                                int iOutBpp, int iScale);
#ifndef PNG_SCALE_TRUNCATE
#define PNG_SCALE_TRUNCATE 0
#define PNG_SCALE_ROUND 1
#endif

static int g_failures = 0;
// "modified" expects 16-bit support to work; "baseline" expects the old
// library to reject 16-bit files at openRAM().
static bool g_modified = true;

static void check(bool cond, const char *msg) {
    if (!cond) {
        printf("    FAIL: %s\n", msg);
        g_failures++;
    }
}

static std::vector<uint8_t> read_file(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        printf("    FAIL: cannot open %s\n", path.c_str());
        g_failures++;
        return {};
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(n);
    if (fread(buf.data(), 1, n, f) != (size_t)n) {
        printf("    FAIL: short read %s\n", path.c_str());
        g_failures++;
    }
    fclose(f);
    return buf;
}

struct Sidecar {
    int w, h, channels, bpp;
    std::vector<uint32_t> samples; // native sample values
};

static Sidecar load_sidecar(const std::string &path) {
    Sidecar sc{0, 0, 0, 0, {}};
    std::vector<uint8_t> d = read_file(path);
    if (d.size() < 12) return sc;
    sc.w = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
    sc.h = (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7];
    sc.channels = (d[8] << 8) | d[9];
    sc.bpp = (d[10] << 8) | d[11];
    int bytes = sc.bpp == 16 ? 2 : 1;
    size_t off = 12;
    for (int i = 0; i < sc.w * sc.h * sc.channels; i++) {
        uint32_t v = d[off++];
        if (bytes == 2) v = (v << 8) | d[off++];
        sc.samples.push_back(v);
    }
    return sc;
}

struct Ctx {
    int outBpp;
    int scale;
    std::vector<uint16_t> out;
};

static int draw_cb(PNGDRAW *pDraw) {
    Ctx *c = (Ctx *)pDraw->pUser;
    size_t old = c->out.size();
    int maxSamples = pDraw->iWidth *
                     (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR ? 3 : 1);
    c->out.resize(old + maxSamples);
    int n = PNG_getLineAsU16(pDraw, &c->out[old], c->outBpp, c->scale);
    if (n < 0) return 0;
    c->out.resize(old + n);
    return 1;
}

struct Metrics {
    uint64_t sumErr = 0;
    uint32_t maxErr = 0;
};

// Decode into a caller-owned image buffer (no callback).
static std::vector<uint8_t> decode_buffered(PNG &png,
        const std::vector<uint8_t> &file, int &rc) {
    std::vector<uint8_t> out;
    rc = png.openRAM((uint8_t *)file.data(), (int)file.size(), NULL);
    if (rc != PNG_SUCCESS) return out;
    int size = png.getBufferSize();
    check(size > 0, "getBufferSize > 0");
    out.resize(size);
    png.setBuffer(out.data());
    rc = png.decode(NULL, 0);
    return out;
}

// Decode line-by-line through PNG_getLineAsU16.
static std::vector<uint16_t> decode_callback(PNG &png,
        const std::vector<uint8_t> &file, int outBpp, int scale, int &rc) {
    Ctx ctx;
    ctx.outBpp = outBpp;
    ctx.scale = scale;
    rc = png.openRAM((uint8_t *)file.data(), (int)file.size(), draw_cb);
    if (rc != PNG_SUCCESS) return {};
    rc = png.decode(&ctx, 0);
    return ctx.out;
}

static double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Repeated decode timing (microseconds per decode).
static double time_decode(const std::vector<uint8_t> &file, bool useCallback,
                          int outBpp, int scale) {
    const int N = 200;
    double t0 = now_sec();
    for (int i = 0; i < N; i++) {
        PNG tpng;
        int rc;
        if (useCallback) {
            decode_callback(tpng, file, outBpp, scale, rc);
        } else {
            decode_buffered(tpng, file, rc);
        }
    }
    return (now_sec() - t0) * 1e6 / N;
}

static Metrics compare16(const std::vector<uint16_t> &got,
                         const Sidecar &sc) {
    Metrics m;
    check(got.size() == sc.samples.size(), "full 16-bit sample count");
    size_t n = got.size() < sc.samples.size() ? got.size()
                                              : sc.samples.size();
    for (size_t i = 0; i < n; i++) {
        uint32_t e = (uint32_t)abs((int)got[i] - (int)sc.samples[i]);
        m.sumErr += e;
        if (e > m.maxErr) m.maxErr = e;
    }
    return m;
}

// Compare 8-bit readback against the original 16-bit sample by scaling the
// readback back into the 16-bit range.
static Metrics compare8(const std::vector<uint16_t> &got,
                        const Sidecar &sc, int scale) {
    Metrics m;
    check(got.size() == sc.samples.size(), "8-bit sample count");
    size_t n = got.size() < sc.samples.size() ? got.size()
                                              : sc.samples.size();
    for (size_t i = 0; i < n; i++) {
        uint32_t expected8;
        if (scale == PNG_SCALE_ROUND) {
            expected8 = (sc.samples[i] + 128) >> 8;
            if (expected8 > 255) expected8 = 255; // matches library clamp
        } else {
            expected8 = sc.samples[i] >> 8;
        }
        check(got[i] == expected8, "8-bit value matches expected scaling");
        uint32_t reconstructed = (uint32_t)got[i] << 8;
        uint32_t e = (uint32_t)abs((int)reconstructed
                                   - (int)sc.samples[i]);
        m.sumErr += e;
        if (e > m.maxErr) m.maxErr = e;
    }
    return m;
}

static void report(const char *tag, const Metrics &m, size_t count) {
    printf("    %-28s maxErr=%-6u meanErr=%.2f (16-bit units)\n", tag,
           m.maxErr, count ? (double)m.sumErr / count : 0.0);
}

static int test_16bit(const char *name, const std::string &data,
                      const std::string &pngName, int expectPixelType) {
    printf("[16-bit] %s\n", name);
    auto file = read_file(data + "/" + pngName);
    Sidecar sc = load_sidecar(data + "/" + pngName.substr(0,
                                   pngName.size() - 4) + ".raw");
    PNG png;
    int rc;
    auto buf = decode_buffered(png, file, rc);
    if (!g_modified) {
        check(rc == PNG_UNSUPPORTED_FEATURE,
              "baseline rejects 16-bit with PNG_UNSUPPORTED_FEATURE");
        printf("    (16-bit path absent in baseline; remaining 16-bit "
               "checks skipped)\n");
        return 0;
    }
    check(rc == PNG_SUCCESS, "buffered decode succeeds");
    check(png.getBpp() == 16, "getBpp() == 16");
    check(png.getPixelType() == expectPixelType, "pixel type preserved");
    check((int)buf.size() == sc.w * sc.h * sc.channels * 2,
          "buffer size matches 16-bit samples");
    bool exact = buf.size() == sc.samples.size() * 2;
    for (size_t i = 0; exact && i < sc.samples.size(); i++) {
        uint16_t v = (uint16_t)((buf[i * 2] << 8) | buf[i * 2 + 1]);
        if (v != sc.samples[i]) { exact = false; break; }
    }
    check(exact, "buffered 16-bit bytes match source exactly (no 8-bit hop)");
    png.close();

    PNG p2;
    auto full = decode_callback(p2, file, 16, PNG_SCALE_TRUNCATE, rc);
    check(rc == PNG_SUCCESS, "callback 16-bit decode succeeds");
    Metrics mf = compare16(full, sc);
    report("full 16-bit via getLineAsU16", mf, sc.samples.size());
    p2.close();

    PNG p3;
    auto trunc = decode_callback(p3, file, 8, PNG_SCALE_TRUNCATE, rc);
    check(rc == PNG_SUCCESS, "truncate decode succeeds");
    Metrics mt = compare8(trunc, sc, PNG_SCALE_TRUNCATE);
    report("truncated to 8-bit", mt, sc.samples.size());
    p3.close();

    PNG p4;
    auto roundv = decode_callback(p4, file, 8, PNG_SCALE_ROUND, rc);
    check(rc == PNG_SUCCESS, "round decode succeeds");
    Metrics mr = compare8(roundv, sc, PNG_SCALE_ROUND);
    report("rounded to 8-bit", mr, sc.samples.size());
    p4.close();

    size_t diffs = 0;
    size_t n = trunc.size() < roundv.size() ? trunc.size() : roundv.size();
    for (size_t i = 0; i < n; i++)
        if (trunc[i] != roundv[i]) diffs++;
    printf("    %-28s %zu of %zu samples differ\n",
           "truncate vs round diffs", diffs, n);

    double usBuf = time_decode(file, false, 16, 0);
    double usCb16 = time_decode(file, true, 16, 0);
    double usCbT = time_decode(file, true, 8, PNG_SCALE_TRUNCATE);
    double usCbR = time_decode(file, true, 8, PNG_SCALE_ROUND);
    printf("    %-28s %8.1f us\n", "decode time (buffered)", usBuf);
    printf("    %-28s %8.1f us\n", "decode time (callback 16)", usCb16);
    printf("    %-28s %8.1f us\n", "decode time (truncate)", usCbT);
    printf("    %-28s %8.1f us\n", "decode time (round)", usCbR);
    return 0;
}

static int test_8bit(const char *name, const std::string &data,
                     const std::string &dump, const std::string &pngName,
                     int expectPixelType) {
    printf("[8-bit regression] %s\n", name);
    auto file = read_file(data + "/" + pngName);
    Sidecar sc = load_sidecar(data + "/" + pngName.substr(0,
                                   pngName.size() - 4) + ".raw");
    PNG png;
    int rc;
    auto buf = decode_buffered(png, file, rc);
    check(rc == PNG_SUCCESS, "8-bit buffered decode succeeds");
    check(png.getBpp() == 8, "getBpp() == 8");
    check(png.getPixelType() == expectPixelType, "pixel type preserved");
    bool exact = buf.size() == sc.samples.size();
    for (size_t i = 0; exact && i < sc.samples.size(); i++)
        if (buf[i] != (uint8_t)sc.samples[i]) exact = false;
    check(exact, "8-bit buffered bytes match source exactly");

    // Dump for byte-for-byte comparison against the unmodified library.
    std::string dumpPath = dump + "/" + pngName + ".buf";
    FILE *df = fopen(dumpPath.c_str(), "wb");
    check(df != NULL, "dump file opened");
    if (df) {
        fwrite(buf.data(), 1, buf.size(), df);
        fclose(df);
    }
    png.close();

    PNG p2;
    auto ext = decode_callback(p2, file, 16, 0, rc);
    check(rc == PNG_SUCCESS, "8-bit callback decode succeeds");
    bool zext = ext.size() == sc.samples.size();
    for (size_t i = 0; zext && i < sc.samples.size(); i++)
        if (ext[i] != sc.samples[i] || ext[i] > 255) zext = false;
    check(zext, "8-bit samples zero-extended into unsigned short unchanged");
    p2.close();

    double usBuf = time_decode(file, false, 16, 0);
    double usCb = time_decode(file, true, 16, 0);
    printf("    %-28s %8.1f us\n", "decode time (buffered)", usBuf);
    printf("    %-28s %8.1f us\n", "decode time (callback)", usCb);
    return 0;
}

static void test_unsupported(const std::string &data,
                             const std::string &pngName) {
    auto file = read_file(data + "/" + pngName);
    PNG png;
    int rc = png.openRAM((uint8_t *)file.data(), (int)file.size(), NULL);
    printf("[unsupported] %-22s openRAM -> %d (expect %d) %s\n",
           pngName.c_str(), rc, PNG_UNSUPPORTED_FEATURE,
           rc == PNG_UNSUPPORTED_FEATURE ? "OK" : "FAIL");
    if (rc != PNG_UNSUPPORTED_FEATURE) g_failures++;
}

static void test_scaling_boundaries() {
    // Deterministic in-memory lines; big-endian 16-bit samples as PNG stores.
    const uint16_t in16[4] = {0x0000, 0x0080, 0xFF80, 0xFFFF};
    uint8_t raw[8];
    for (int i = 0; i < 4; i++) {
        raw[i * 2] = in16[i] >> 8;
        raw[i * 2 + 1] = in16[i] & 0xFF;
    }
    PNGDRAW d;
    memset(&d, 0, sizeof(d));
    d.iWidth = 4;
    d.iPixelType = PNG_PIXEL_GRAYSCALE;
    d.iBpp = 16;
    d.pPixels = raw;
    uint16_t out[4];

    printf("[scaling] 16-bit gray boundary samples {0,128,65408,65535}\n");
    int n = PNG_getLineAsU16(&d, out, 16, 0);
    check(n == 4, "full 16-bit writes 4 samples");
    bool ok = out[0] == 0x0000 && out[1] == 0x0080 &&
              out[2] == 0xFF80 && out[3] == 0xFFFF;
    check(ok, "full 16-bit identity (no 8-bit hop)");

    n = PNG_getLineAsU16(&d, out, 8, PNG_SCALE_TRUNCATE);
    check(n == 4, "truncate writes 4 samples");
    ok = out[0] == 0 && out[1] == 0 && out[2] == 255 && out[3] == 255;
    check(ok, "truncate keeps high byte {0,0,255,255}");

    n = PNG_getLineAsU16(&d, out, 8, PNG_SCALE_ROUND);
    check(n == 4, "round writes 4 samples");
    // 0x0080 rounds up to 1; 0xFFFF/0xFF80 clamp to 255 instead of 256.
    ok = out[0] == 0 && out[1] == 1 && out[2] == 255 && out[3] == 255;
    check(ok, "round nearest with 255 clamp {0,1,255,255}");
}

static void test_api_guards() {
    printf("[api guards] invalid arguments must return -1\n");
    uint16_t dst[16];
    PNGDRAW d;
    memset(&d, 0, sizeof(d));
    d.iWidth = 4;
    d.iPixelType = PNG_PIXEL_GRAYSCALE;
    d.iBpp = 16;
    static uint8_t pixels[16] = {0};
    d.pPixels = pixels;
    check(PNG_getLineAsU16(NULL, dst, 16, 0) == -1, "NULL PNGDRAW rejected");
    check(PNG_getLineAsU16(&d, NULL, 16, 0) == -1, "NULL output rejected");
    check(PNG_getLineAsU16(&d, dst, 12, 0) == -1,
          "unsupported output depth rejected");
    d.iBpp = 8;
    check(PNG_getLineAsU16(&d, dst, 8, 0) == -1,
          "8-bit source cannot be asked for 8-bit u16 output");
    d.iPixelType = PNG_PIXEL_INDEXED;
    d.iBpp = 16;
    check(PNG_getLineAsU16(&d, dst, 16, 0) == -1,
          "non gray/truecolor pixel type rejected");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("usage: %s <data_dir> <dump_dir> <baseline|modified>\n",
               argv[0]);
        return 2;
    }
    std::string data = argv[1];
    std::string dump = argv[2];
    g_modified = (strcmp(argv[3], "modified") == 0);

    test_16bit("16-bit grayscale (gray16.png)", data, "gray16.png",
               PNG_PIXEL_GRAYSCALE);
    test_16bit("16-bit truecolor (rgb16.png)", data, "rgb16.png",
               PNG_PIXEL_TRUECOLOR);
    // Downscaling comparison is reported inside each 16-bit run above
    // (truncate vs round errors and diff counts).
    test_8bit("8-bit grayscale (gray8.png)", data, dump, "gray8.png",
              PNG_PIXEL_GRAYSCALE);
    test_8bit("8-bit truecolor (rgb8.png)", data, dump, "rgb8.png",
              PNG_PIXEL_TRUECOLOR);

    test_unsupported(data, "bad_grayalpha16.png");
    test_unsupported(data, "bad_interlace16.png");
    test_unsupported(data, "bad_bpp32.png");
    if (g_modified) {
        test_scaling_boundaries();
        test_api_guards();
    }

    printf("\n==== %s (%d failure%s) ====\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
