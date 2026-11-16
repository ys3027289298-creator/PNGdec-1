//
// test_extbuf.cpp - external buffer mode test harness for PNGdec
//
// Builds against the unmodified library build (see run_tests.sh). Everything
// the decoder needs in external mode comes from caller supplied buffers;
// wrapped malloc/calloc/realloc count any heap traffic during decode.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "PNGdec.h"
#include "png_extbuf.h"

// ---- heap traffic instrumentation ---------------------------------------
extern "C" {
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static long g_heap_calls = 0;
void *__wrap_malloc(size_t n)   { g_heap_calls++; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { g_heap_calls++; return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { g_heap_calls++; return __real_realloc(p, n); }
void __wrap_free(void *p)       { __real_free(p); }
}

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; printf("  FAIL: %s\n", msg); } \
} while (0)

static std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    return d;
}

struct Spec {
    std::string name, file, ref;
    int width, height, bitdepth, ptype, interlaced, pitch, palette_size, image_size;
};

static std::vector<Spec> load_specs(const std::string &path)
{
    // parse the line-oriented specs.tsv emitted by gen_images.py
    std::ifstream f(path.c_str());
    std::vector<Spec> out;
    std::string line;
    std::getline(f, line); // header row
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::vector<std::string> col;
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            if (tab == std::string::npos) { col.push_back(line.substr(start)); break; }
            col.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (col.size() < 11) continue;
        Spec s;
        s.name = col[0]; s.file = col[1]; s.ref = col[2];
        s.width = atoi(col[3].c_str());
        s.height = atoi(col[4].c_str());
        s.bitdepth = atoi(col[5].c_str());
        s.ptype = atoi(col[6].c_str());
        s.interlaced = (col[7] == "1" || col[7] == "True") ? 1 : 0;
        s.pitch = atoi(col[8].c_str());
        s.palette_size = atoi(col[9].c_str());
        s.image_size = atoi(col[10].c_str());
        out.push_back(s);
    }
    return out;
}

static long g_lines_seen = 0;
static int draw_callback(PNGDRAW *pDraw)
{
    g_lines_seen++;
    // exercise RGB565 conversion (and alpha paths) using a stack buffer
    uint16_t line[64];
    int n = pDraw->iWidth > 64 ? 64 : pDraw->iWidth;
    PNG *png = (PNG *)pDraw->pUser;
    png->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    (void)n;
    return 1;
}

// decode one image in default mode (internal buffers); returns error code
static int decode_default(PNG &png, const std::vector<uint8_t> &data,
                          std::vector<uint8_t> *out, bool use_callback)
{
    int rc = png.openRAM((uint8_t *)data.data(), (int)data.size(),
                         use_callback ? draw_callback : NULL);
    if (rc != PNG_SUCCESS) return rc;
    if (!use_callback) {
        out->resize(png.getBufferSize());
        png.setBuffer(out->data());
    }
    g_lines_seen = 0;
    rc = png.decode(&png, 0);
    if (use_callback)
        CHECK(g_lines_seen == png.getHeight(), "callback got every line");
    png.close();
    return rc;
}

// decode using freshly allocated, exactly-sized caller buffers
static int decode_external_fresh(PNG &png, const std::vector<uint8_t> &data,
                                  std::vector<uint8_t> *imagebuf,
                                  bool use_callback)
{
    int rc = png.openRAM((uint8_t *)data.data(), (int)data.size(),
                         use_callback ? draw_callback : NULL);
    if (rc != PNG_SUCCESS) return rc;
    PNGEXTERNALBUFFERS ext;
    memset(&ext, 0, sizeof(ext));
    rc = PNG_getExternalBufferSizes(png, &ext);
    if (rc != PNG_SUCCESS) { png.close(); return rc; }
    static std::vector<uint8_t> linebuf, workspace;
    linebuf.assign(ext.iLineBufferNeeded, 0);
    workspace.assign(ext.iWorkspaceNeeded, 0);
    if (imagebuf) {
        imagebuf->assign(ext.iImageBufferNeeded, 0);
        png.setBuffer(imagebuf->data());
    }
    ext.pLineBuffer = linebuf.data();
    ext.iLineBufferSize = (int)linebuf.size();
    ext.pWorkspace = workspace.data();
    ext.iWorkspaceSize = (int)workspace.size();
    ext.pUser = &png;
    g_lines_seen = 0;
    rc = PNG_decodeExternal(png, &ext, 0);
    if (use_callback && rc == PNG_SUCCESS)
        CHECK(g_lines_seen == png.getHeight(), "ext callback got every line");
    png.close();
    return rc;
}

// decode using only caller supplied buffers; returns error code
static int decode_external(PNG &png, const std::vector<uint8_t> &data,
                           std::vector<uint8_t> &linebuf,
                           std::vector<uint8_t> &workspace,
                           std::vector<uint8_t> *imagebuf,
                           bool use_callback, PNGEXTERNALBUFFERS *pq_out)
{
    int rc = png.openRAM((uint8_t *)data.data(), (int)data.size(),
                         use_callback ? draw_callback : NULL);
    if (rc != PNG_SUCCESS) return rc;
    PNGEXTERNALBUFFERS ext;
    memset(&ext, 0, sizeof(ext));
    rc = PNG_getExternalBufferSizes(png, &ext);
    if (rc != PNG_SUCCESS) { png.close(); return rc; }
    if (pq_out) *pq_out = ext;
    if (imagebuf) {
        imagebuf->resize(ext.iImageBufferNeeded);
        png.setBuffer(imagebuf->data());
    }
    ext.pLineBuffer = linebuf.data();
    ext.iLineBufferSize = (int)linebuf.size();
    ext.pWorkspace = workspace.data();
    ext.iWorkspaceSize = (int)workspace.size();
    ext.pUser = &png;
    g_lines_seen = 0;
    rc = PNG_decodeExternal(png, &ext, 0);
    if (use_callback && rc == PNG_SUCCESS)
        CHECK(g_lines_seen == png.getHeight(), "ext callback got every line");
    png.close();
    return rc;
}

int main(int argc, char **argv)
{
    if (argc != 2) { printf("usage: %s data_dir\n", argv[0]); return 2; }
    std::string dir = argv[1];
    std::vector<Spec> specs = load_specs(dir + "/specs.tsv");
    CHECK(specs.size() == 17, "17 test images present");
    if (specs.empty()) return 2;

    // one workspace large enough for every image, allocated once
    PNG png_a, png_b;
    std::vector<uint8_t> workspace(40960);
    std::vector<uint8_t> linebuf(12288); // covers all small test pitches

    printf("=== per-image buffer requirements and decode results ===\n");
    printf("%-16s %5s %5s %3s %3s %8s %8s %8s  default/external\n",
           "name", "w", "h", "bd", "i7", "line", "work", "image");

    for (const Spec &s : specs) {
        std::vector<uint8_t> data = read_file(dir + "/" + s.file);
        std::vector<uint8_t> ref = read_file(dir + "/" + s.ref);
        CHECK(data.size() > 8 && memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) == 0,
              (s.name + " has PNG signature").c_str());

        PNG probe;
        int rc = probe.openRAM(data.data(), (int)data.size(), NULL);
        CHECK(rc == PNG_SUCCESS, (s.name + " opens").c_str());
        CHECK(probe.getWidth() == s.width && probe.getHeight() == s.height,
              (s.name + " dimensions").c_str());
        CHECK(probe.getBpp() == s.bitdepth, (s.name + " bitdepth").c_str());
        CHECK(probe.isInterlaced() == s.interlaced, (s.name + " interlace flag").c_str());

        PNGEXTERNALBUFFERS q;
        memset(&q, 0, sizeof(q));
        int qrc = PNG_getExternalBufferSizes(probe, &q);
        CHECK(qrc == PNG_SUCCESS, (s.name + " size query succeeds").c_str());
        CHECK(q.iLineBufferNeeded > 0 && q.iWorkspaceNeeded > 0,
              (s.name + " positive requirements").c_str());
        CHECK(q.iImageBufferNeeded == s.image_size,
              (s.name + " image buffer size matches pitch*h").c_str());
        CHECK(q.iLineBufferNeeded <= (int)linebuf.size() || s.width >= 1000,
              (s.name + " fits common line buffer").c_str());
        probe.close();

        // default mode
        std::vector<uint8_t> def_out;
        long heap_before = g_heap_calls;
        int rc_def = decode_default(png_a, data, &def_out, false);
        long heap_after = g_heap_calls;
        CHECK(heap_after == heap_before,
              (s.name + " default decode performs no heap alloc").c_str());

        // external mode, caller buffers only
        std::vector<uint8_t> ext_line(q.iLineBufferNeeded);
        std::vector<uint8_t> ext_ws(q.iWorkspaceNeeded);
        std::vector<uint8_t> ext_out;
        heap_before = g_heap_calls;
        int rc_ext;
        if (s.width >= 1000) {
            // big image needs a line buffer sized to its own pitch
            std::vector<uint8_t> big_line(q.iLineBufferNeeded);
            rc_ext = decode_external(png_b, data, big_line, ext_ws, &ext_out,
                                     false, NULL);
        } else {
            rc_ext = decode_external(png_b, data, ext_line, ext_ws, &ext_out,
                                     false, NULL);
        }
        heap_after = g_heap_calls;
        CHECK(heap_after == heap_before,
              (s.name + " external decode performs no heap alloc").c_str());

        const char *status;
        if (s.interlaced) {
            CHECK(rc_def == PNG_UNSUPPORTED_FEATURE,
                  (s.name + " default mode rejects interlace").c_str());
            CHECK(rc_ext == PNG_SUCCESS,
                  (s.name + " external mode decodes interlace").c_str());
            status = "UNSUP / OK";
        } else {
            if (s.width >= 1000) {
                CHECK(rc_def == PNG_TOO_BIG,
                      (s.name + " default mode rejects oversized image").c_str());
                status = "TOO_BIG / OK";
            } else {
                CHECK(rc_def == PNG_SUCCESS,
                      (s.name + " default mode decodes").c_str());
                status = "OK / OK";
            }
            CHECK(rc_ext == PNG_SUCCESS,
                  (s.name + " external mode decodes").c_str());
        }

        // reference comparison and (non-interlaced) pixel identity
        if (rc_ext == PNG_SUCCESS) {
            CHECK(ext_out.size() == ref.size(),
                  (s.name + " external output size").c_str());
            CHECK(memcmp(ext_out.data(), ref.data(), ref.size()) == 0,
                  (s.name + " external output matches reference pixels").c_str());
        }
        if (rc_def == PNG_SUCCESS && !s.interlaced) {
            CHECK(def_out.size() == ext_out.size(),
                  (s.name + " default/external output sizes equal").c_str());
            CHECK(memcmp(def_out.data(), ext_out.data(), ext_out.size()) == 0,
                  (s.name + " default vs external pixel-identical").c_str());
        }
        printf("%-16s %5d %5d %3d %3d %8d %8d %8d  %s\n",
               s.name.c_str(), s.width, s.height, s.bitdepth,
               s.interlaced ? 1 : 0, q.iLineBufferNeeded,
               q.iWorkspaceNeeded, q.iImageBufferNeeded, status);
    }

    printf("\n=== interlaced/non-interlaced twin identity ===\n");
    for (const Spec &s : specs) {
        if (s.interlaced) continue;
        Spec twin;
        bool found = false;
        for (const Spec &t : specs)
            if (t.name == s.name + "_i7") { twin = t; found = true; break; }
        if (!found) continue;
        std::vector<uint8_t> d1 = read_file(dir + "/" + s.file);
        std::vector<uint8_t> d2 = read_file(dir + "/" + twin.file);
        PNG p1, p2;
        std::vector<uint8_t> o1, o2;
        int r1 = decode_external_fresh(p1, d1, &o1, false);
        int r2 = decode_external_fresh(p2, d2, &o2, false);
        CHECK(r1 == PNG_SUCCESS && r2 == PNG_SUCCESS,
              (s.name + " twins decode").c_str());
        CHECK(o1.size() == o2.size() && memcmp(o1.data(), o2.data(), o1.size()) == 0,
              (s.name + " interlaced twin pixel-identical").c_str());
    }

    printf("\n=== callback (line-by-line) external mode, no image buffer ===\n");
    for (const Spec &s : specs) {
        if (s.interlaced || s.width >= 1000) continue; // callback needs full-image for i7
        std::vector<uint8_t> data = read_file(dir + "/" + s.file);
        PNG p;
        int rc = decode_external_fresh(p, data, NULL, true);
        CHECK(rc == PNG_SUCCESS, (s.name + " callback-mode external decode").c_str());
        CHECK(p.getLastError() == PNG_SUCCESS,
              (s.name + " callback mode no sticky error").c_str());
    }

    printf("\n=== undersized / missing buffer error reporting ===\n");
    {
        const Spec &s = specs[0]; // small_gray, non-interlaced
        std::vector<uint8_t> data = read_file(dir + "/" + s.file);
        PNG p;
        p.openRAM(data.data(), (int)data.size(), NULL);
        PNGEXTERNALBUFFERS q;
        memset(&q, 0, sizeof(q));
        PNG_getExternalBufferSizes(p, &q);
        p.close();

        // line buffer one byte short
        {
            std::vector<uint8_t> lb(q.iLineBufferNeeded - 1), ws(q.iWorkspaceNeeded);
            std::vector<uint8_t> img;
            PNG p2;
            int rc = decode_external(p2, data, lb, ws, &img, false, NULL);
            CHECK(rc == PNG_MEM_ERROR, "short line buffer -> PNG_MEM_ERROR");
            CHECK(p2.getLastError() == PNG_MEM_ERROR, "sticky error for short line buffer");
        }
        // workspace one byte short
        {
            std::vector<uint8_t> lb(q.iLineBufferNeeded), ws(q.iWorkspaceNeeded - 1);
            std::vector<uint8_t> img;
            PNG p2;
            int rc = decode_external(p2, data, lb, ws, &img, false, NULL);
            CHECK(rc == PNG_MEM_ERROR, "short workspace -> PNG_MEM_ERROR");
        }
        // NULL pointers
        {
            PNG p2;
            p2.openRAM(data.data(), (int)data.size(), NULL);
            PNGEXTERNALBUFFERS ext;
            memset(&ext, 0, sizeof(ext));
            ext.u32Magic = PNG_EXT_BUFFER_MAGIC;
            int rc = p2.decode(&ext, 0);
            CHECK(rc == PNG_MEM_ERROR, "NULL buffers -> PNG_MEM_ERROR");
            p2.close();
        }
        // interlaced image without a full-image buffer
        {
            const Spec *pi7 = NULL;
            for (const Spec &t : specs) if (t.interlaced) { pi7 = &t; break; }
            std::vector<uint8_t> d = read_file(dir + "/" + pi7->file);
            PNG p2;
            int rc = decode_external(p2, d, linebuf, workspace, NULL, true, NULL);
            CHECK(rc == PNG_NO_BUFFER,
                  "interlaced callback without image buffer -> PNG_NO_BUFFER");
        }
        // query on bogus input
        {
            PNGEXTERNALBUFFERS z;
            memset(&z, 0, sizeof(z));
            CHECK(PNG_getExternalBufferSizes(NULL, &z) == PNG_INVALID_PARAMETER,
                  "query with NULL object -> PNG_INVALID_PARAMETER");
        }
    }

    printf("\n=== fast-palette option still works in external mode ===\n");
    {
        for (const Spec &s : specs) {
            if (s.ptype != 3 || s.interlaced || s.width >= 1000) continue;
            std::vector<uint8_t> data = read_file(dir + "/" + s.file);
            PNG p;
            p.openRAM(data.data(), (int)data.size(), draw_callback);
            PNGEXTERNALBUFFERS ext;
            memset(&ext, 0, sizeof(ext));
            PNG_getExternalBufferSizes(p, &ext);
            std::vector<uint8_t> lb(ext.iLineBufferNeeded), ws(ext.iWorkspaceNeeded);
            ext.pLineBuffer = lb.data(); ext.iLineBufferSize = (int)lb.size();
            ext.pWorkspace = ws.data(); ext.iWorkspaceSize = (int)ws.size();
            ext.pUser = &p;
            int rc = PNG_decodeExternal(p, &ext, PNG_FAST_PALETTE);
            CHECK(rc == PNG_SUCCESS, (s.name + " FAST_PALETTE external decode").c_str());
            p.close();
        }
    }

    printf("\n========================================\n");
    printf("checks: %d, failures: %d\n", g_checks, g_failures);
    printf("%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_failures ? 1 : 0;
}
