//
// reorder_test/harness.cpp
//
// Validation + measurement harness for the palette reorder feature.
// Self-contained: synthetic indexed PNGs are generated in memory (and
// dumped to samples/), no third-party libraries and no network access.
//
// The DEFLATE writers below (fixed-Huffman and dynamic-Huffman, RFC 1951)
// live in this file; CRC-32/Adler-32 and the inflate used to verify the
// streams come from the repository's own bundled zlib sources. Every
// compressed stream this harness produces is therefore checked by feeding
// it back through the PNG decoder under test.
//
// Build (from reorder_test/):
//   g++ -D__LINUX__ -Wall -O2 -fsanitize=address,undefined -I../src harness.cpp -o harness
//   ./harness
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

// Pull in the repository's inflate-only zlib and the library under test,
// exactly the way the bundled MacOS/PNG_Test suite does.
#include "../src/zutil.c"
#include "../src/inflate.c"
#include "../src/inffast.c"
#include "../src/inftrees.c"
#include "../src/crc32.c"
#undef DO1
#undef DO8
#include "../src/adler32.c"
// Test-only access: reach the PNGIMAGE state without changing any public
// library interface. All system headers above are already included, so this
// never touches standard-library declarations.
#define private public
#include "../src/PNGdec.cpp"
#undef private

extern "C" int PNG_reorderPalette(PNGIMAGE *pPNG, int iStrategy,
                                  int *pOldOrder, int *pNewMap, int iCount);
enum { REORDER_FREQ = 1, REORDER_SIM = 2 };

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("    [PASS] %s\n", msg); } \
    else { printf("    [FAIL] %s\n", msg); g_failures++; } \
} while (0)

//==========================================================================
// Bit writer
//==========================================================================
struct BitWriter {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int nbits = 0;
    void put(uint32_t value, int width) {
        if (width == 0) return;
        acc |= (value & (((uint64_t)1 << width) - 1)) << nbits;
        nbits += width;
        while (nbits >= 8) { out.push_back((uint8_t)(acc & 0xff)); acc >>= 8; nbits -= 8; }
    }
    void alignToByte() {
        if (nbits > 0) { out.push_back((uint8_t)(acc & 0xff)); acc = 0; nbits = 0; }
    }
};

static uint32_t reverseBits(uint32_t v, int width)
{
    uint32_t r = 0;
    for (int i = 0; i < width; i++) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

//==========================================================================
// LZ77 greedy tokenizer (window 32768, min match 3, max match 258)
//==========================================================================
static const int lenBase[29]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int lenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int distBase[30]  = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int distExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

struct Token { int lit; int len; int dist; }; // lit >= 0 means literal

static std::vector<Token> lz77(const uint8_t *src, size_t len)
{
    std::vector<Token> tok;
    size_t pos = 0;
    while (pos < len) {
        int bestLen = 0, bestDist = 0;
        size_t maxBack = pos > 32768 ? pos - 32768 : 0;
        int maxLen = (int)(len - pos); if (maxLen > 258) maxLen = 258;
        if (maxLen >= 3) {
            for (size_t back = pos; back > maxBack;) {
                back--;
                int l = 0;
                while (l < maxLen && src[back + l] == src[pos + l]) l++;
                if (l > bestLen) { bestLen = l; bestDist = (int)(pos - back); }
                if (bestLen == maxLen) break;
            }
        }
        if (bestLen >= 3) {
            tok.push_back({-1, bestLen, bestDist});
            pos += bestLen;
        } else {
            tok.push_back({src[pos], 0, 0});
            pos++;
        }
    }
    return tok;
}

static int lengthSymbol(int l)
{
    for (int k = 0; k < 29; k++)
        if (l <= lenBase[k] + (lenExtra[k] ? (1 << lenExtra[k]) - 1 : 0)) return k;
    return 28;
}
static int distanceSymbol(int d)
{
    for (int k = 0; k < 30; k++)
        if (d <= distBase[k] + (distExtra[k] ? (1 << distExtra[k]) - 1 : 0)) return k;
    return 29;
}

//==========================================================================
// Canonical Huffman code lengths with RFC 1951 bit-length clamping
//==========================================================================
static void huffmanLengths(const int *freq, int n, int maxBits, int *lengths)
{
    for (int i = 0; i < n; i++) lengths[i] = 0;
    std::vector<int> sym;
    for (int i = 0; i < n; i++) if (freq[i] > 0) sym.push_back(i);
    if (sym.empty()) return;
    if (sym.size() == 1) { lengths[sym[0]] = 1; return; }

    // Pair lowest-weight nodes until one tree remains; leaf depth = code length
    int used = (int)sym.size();
    std::vector<int64_t> weight(2 * used, 0);
    std::vector<int> parent(2 * used, -1);
    for (int i = 0; i < used; i++) weight[i] = freq[sym[i]];
    int next = used;
    while (next < 2 * used - 1) {
        int a = -1, b = -1;
        for (int i = 0; i < next; i++) {
            if (parent[i] != -1) continue;
            if (a < 0 || weight[i] < weight[a] ||
                (weight[i] == weight[a] && i < a)) { b = a; a = i; }
            else if (b < 0 || weight[i] < weight[b] ||
                     (weight[i] == weight[b] && i < b)) b = i;
        }
        parent[a] = parent[b] = next;
        weight[next] = weight[a] + weight[b];
        next++;
    }
    int blCount[300] = {0}; // natural depth can reach used-1 <= 285
    int maxDepth = 0;
    for (int i = 0; i < used; i++) {
        int depth = 0, node = i;
        while (parent[node] != -1) { node = parent[node]; depth++; }
        blCount[depth]++;
        if (depth > maxDepth) maxDepth = depth;
    }
    // Clamp to maxBits with the RFC 1951 section 3.2.2 / zlib shift algorithm
    while (maxDepth > maxBits) {
        int over = (blCount[maxBits] >> 1) + blCount[maxBits + 1];
        blCount[maxBits] -= over;
        blCount[maxBits] += over << 1;
        blCount[maxBits - 1] += over;
        int length = maxBits - 1;
        do {
            over = blCount[length] >> 1;
            blCount[length] -= over;
            blCount[length + 1] += over << 1;
            blCount[length - 1] += over;
            length--;
        } while (length > 0);
        maxDepth--;
    }
    // Reassign lengths to symbols, shortest lengths to most frequent symbols
    std::sort(sym.begin(), sym.end(), [&](int x, int y) {
        if (freq[x] != freq[y]) return freq[x] > freq[y];
        return x < y;
    });
    int idx = 0;
    for (int bits = 1; bits <= maxBits; bits++)
        for (int k = 0; k < blCount[bits] && idx < used; k++)
            lengths[sym[idx++]] = bits;
}

struct HuffCode { int len[288]; uint32_t code[288]; int n; };

static HuffCode makeCode(const int *lengths, int n)
{
    HuffCode h; h.n = n;
    memset(h.len, 0, sizeof(h.len)); memset(h.code, 0, sizeof(h.code));
    int blCount[16] = {0};
    for (int i = 0; i < n; i++) { blCount[lengths[i]]++; h.len[i] = lengths[i]; }
    uint32_t nextCode[16]; nextCode[0] = 0;
    for (int bits = 1; bits < 16; bits++)
        nextCode[bits] = (nextCode[bits-1] + blCount[bits-1]) << 1;
    for (int i = 0; i < n; i++)
        if (lengths[i]) h.code[i] = nextCode[lengths[i]]++;
    return h;
}

//==========================================================================
// Fixed-Huffman DEFLATE (single final block, stored-block fallback for <12B)
//==========================================================================
static std::vector<uint8_t> deflateFixed(const uint8_t *src, size_t len)
{
    BitWriter bw;
    if (len < 12) {
        uint16_t l = (uint16_t)len, nl = (uint16_t)~l;
        bw.put(1, 1); bw.put(0, 2); bw.alignToByte();
        bw.out.push_back(l & 0xff); bw.out.push_back(l >> 8);
        bw.out.push_back(nl & 0xff); bw.out.push_back(nl >> 8);
        bw.out.insert(bw.out.end(), src, src + len);
        return bw.out;
    }
    std::vector<Token> tok = lz77(src, len);
    bw.put(1, 1); bw.put(1, 2); // BFINAL=1, BTYPE=01
    for (const Token &t : tok) {
        if (t.lit >= 0) {
            uint8_t v = (uint8_t)t.lit;
            if (v < 144) bw.put(reverseBits(0x30 + v, 8), 8);
            else bw.put(reverseBits(0x190 + (v - 144), 9), 9);
        } else {
            int li = lengthSymbol(t.len);
            int sym = 257 + li;
            int cw = (sym <= 279) ? 7 : 8;
            int code = (sym <= 279) ? (sym - 256) : (0xc0 + (sym - 280));
            bw.put(reverseBits(code, cw), cw);
            if (lenExtra[li]) bw.put(t.len - lenBase[li], lenExtra[li]);
            int di = distanceSymbol(t.dist);
            bw.put(reverseBits(di, 5), 5);
            if (distExtra[di]) bw.put(t.dist - distBase[di], distExtra[di]);
        }
    }
    bw.put(reverseBits(0, 7), 7); // EOB symbol 256
    bw.alignToByte();
    return bw.out;
}

//==========================================================================
// Dynamic-Huffman DEFLATE (single final block; inputs here are < 1 KB)
//==========================================================================
static const int clOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

static std::vector<uint8_t> deflateDynamic(const uint8_t *src, size_t len)
{
    std::vector<Token> tok = lz77(src, len);
    int llFreq[286] = {0}, distFreq[30] = {0};
    llFreq[256] = 1; // end of block
    for (const Token &t : tok) {
        if (t.lit >= 0) llFreq[t.lit]++;
        else {
            llFreq[257 + lengthSymbol(t.len)]++;
            distFreq[distanceSymbol(t.dist)]++;
        }
    }
    int llLen[286], distLen[30];
    huffmanLengths(llFreq, 286, 15, llLen);
    huffmanLengths(distFreq, 30, 15, distLen);
    int hlit = 285; while (hlit >= 257 && llLen[hlit] == 0) hlit--;
    int hdist = 29;  while (hdist >= 1 && distLen[hdist] == 0) hdist--;
    int nLL = hlit + 1;                 // number of litlen length entries
    int nDist = hdist < 0 ? 0 : hdist + 1;

    // Concatenate the litlen and distance code lengths, then run-length
    // encode them per RFC 1951 section 3.2.7 (symbols 16/17/18).
    std::vector<int> flat;
    for (int i = 0; i < nLL; i++) flat.push_back(llLen[i]);
    for (int i = 0; i < (nDist > 0 ? nDist : 1); i++) flat.push_back(distLen[i]);
    std::vector<int> clSym, clExtra; // symbol plus its RFC extra value (0 if none)
    auto emitCL = [&](int sym, int extra) { clSym.push_back(sym); clExtra.push_back(extra); };
    for (size_t i = 0; i < flat.size();) {
        int v = flat[i];
        size_t run = 1;
        while (i + run < flat.size() && flat[i + run] == v) run++;
        size_t consumed = run;
        if (v == 0) {
            while (run >= 11) { int k = (int)(run > 138 ? 138 : run); emitCL(18, k - 11); run -= k; }
            if (run >= 3)  { int k = (int)(run > 10 ? 10 : run);  emitCL(17, k - 3);  run -= k; }
            while (run--) emitCL(0, 0);
        } else {
            emitCL(v, 0); run--; // first occurrence emitted literally
            while (run > 0) {
                if (run < 3) { emitCL(v, 0); run--; }
                else { int k = (int)(run > 6 ? 6 : run); emitCL(16, k - 3); run -= k; }
            }
        }
        i += consumed;
    }

    // Frequencies of the code-length symbols, then their Huffman code (max 7 bits)
    int clFreq[19] = {0};
    for (int sym : clSym) clFreq[sym]++;
    int clLen[19];
    huffmanLengths(clFreq, 19, 7, clLen);
    int hclen = 18;
    while (hclen >= 0 && clLen[clOrder[hclen]] == 0) hclen--;
    if (hclen < 3) hclen = 3; // RFC: HCLEN+4 must be at least 4

    BitWriter bw;
    bw.put(1, 1); bw.put(2, 2); // BFINAL=1, BTYPE=10 dynamic
    bw.put(nLL - 257, 5);
    bw.put((nDist > 0 ? nDist : 1) - 1, 5); // one zero-length code when no matches
    bw.put(hclen - 3, 4);
    for (int i = 0; i <= hclen; i++) bw.put(clLen[clOrder[i]], 3);

    HuffCode clCode = makeCode(clLen, 19);
    for (size_t i = 0; i < clSym.size(); i++) {
        int sym = clSym[i];
        bw.put(reverseBits(clCode.code[sym], clCode.len[sym]), clCode.len[sym]);
        if (sym == 16) bw.put(clExtra[i], 2);
        else if (sym == 17) bw.put(clExtra[i], 3);
        else if (sym == 18) bw.put(clExtra[i], 7);
    }

    HuffCode llCode = makeCode(llLen, nLL);
    HuffCode distCode = makeCode(distLen, nDist > 0 ? nDist : 1);
    for (const Token &t : tok) {
        if (t.lit >= 0) {
            bw.put(reverseBits(llCode.code[t.lit], llCode.len[t.lit]), llCode.len[t.lit]);
        } else {
            int li = lengthSymbol(t.len), di = distanceSymbol(t.dist);
            int s = 257 + li;
            bw.put(reverseBits(llCode.code[s], llCode.len[s]), llCode.len[s]);
            if (lenExtra[li]) bw.put(t.len - lenBase[li], lenExtra[li]);
            bw.put(reverseBits(distCode.code[di], distCode.len[di]), distCode.len[di]);
            if (distExtra[di]) bw.put(t.dist - distBase[di], distExtra[di]);
        }
    }
    bw.put(reverseBits(llCode.code[256], llCode.len[256]), llCode.len[256]); // EOB
    bw.alignToByte();
    return bw.out;
}

//==========================================================================
// zlib wrappers
//==========================================================================
static std::vector<uint8_t> zlibWrap(const std::vector<uint8_t> &def, const uint8_t *src, size_t len)
{
    std::vector<uint8_t> out;
    out.push_back(0x78); out.push_back(0x01);
    out.insert(out.end(), def.begin(), def.end());
    uLong ad = adler32(0L, Z_NULL, 0);
    ad = adler32(ad, (const Bytef *)src, (uInt)len);
    out.push_back((uint8_t)(ad >> 24)); out.push_back((uint8_t)(ad >> 16));
    out.push_back((uint8_t)(ad >> 8));  out.push_back((uint8_t)ad);
    return out;
}

//==========================================================================
// Minimal PNG builder for 1/2/4/8 bpp indexed images with adaptive
// NONE/SUB line filtering and selectable DEFLATE variant
//==========================================================================
static void putBE32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void writeChunk(std::vector<uint8_t> &png, const char type[4],
                       const uint8_t *data, size_t len)
{
    putBE32(png, (uint32_t)len);
    size_t crcStart = png.size();
    png.insert(png.end(), type, type + 4);
    if (len) png.insert(png.end(), data, data + len);
    uLong c = crc32(0L, Z_NULL, 0);
    c = crc32(c, (const Bytef *)&png[crcStart], (uInt)(4 + len));
    putBE32(png, (uint32_t)c);
}

// indices are one byte per pixel regardless of target bpp
static std::vector<uint8_t> buildPNG(int w, int h, int bpp,
                                     const uint8_t *palRGB, int nColors,
                                     const uint8_t *alpha, int nAlpha,
                                     const uint8_t *indices, bool dynamic)
{
    std::vector<uint8_t> png;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    png.insert(png.end(), sig, sig + 8);
    uint8_t ihdr[13];
    ihdr[0]=w>>24; ihdr[1]=w>>16; ihdr[2]=w>>8; ihdr[3]=w;
    ihdr[4]=h>>24; ihdr[5]=h>>16; ihdr[6]=h>>8; ihdr[7]=h;
    ihdr[8]=(uint8_t)bpp; ihdr[9]=3; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    writeChunk(png, "IHDR", ihdr, 13);
    writeChunk(png, "PLTE", palRGB, (size_t)nColors * 3);
    if (alpha && nAlpha > 0) writeChunk(png, "tRNS", alpha, (size_t)nAlpha);

    int pitch = (w * bpp + 7) / 8;
    std::vector<uint8_t> packed((size_t)h * pitch, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t idx = indices[y * w + x];
            uint8_t *line = &packed[(size_t)y * pitch];
            switch (bpp) {
                case 8: line[x] = idx; break;
                case 4: line[x >> 1] |= (x & 1) ? (idx & 0xf) : (idx << 4); break;
                case 2: line[x >> 2] |= (idx & 3) << (6 - 2 * (x & 3)); break;
                case 1: line[x >> 3] |= (idx & 1) << (7 - (x & 7)); break;
            }
        }

    // Adaptive per-line filtering: choose NONE or SUB by sum of |bytes|
    std::vector<uint8_t> raw((size_t)h * (pitch + 1));
    for (int y = 0; y < h; y++) {
        uint8_t *line = &packed[(size_t)y * pitch];
        uint8_t noneScore = 0, subScore = 0;
        for (int k = 0; k < pitch; k++) {
            noneScore += line[k];
            subScore += (uint8_t)(line[k] - (k ? line[k-1] : 0));
        }
        int filter = (subScore < noneScore) ? 1 : 0;
        raw[(size_t)y * (pitch + 1)] = (uint8_t)filter;
        for (int k = 0; k < pitch; k++)
            raw[(size_t)y * (pitch + 1) + 1 + k] =
                filter ? (uint8_t)(line[k] - (k ? line[k-1] : 0)) : line[k];
    }
    std::vector<uint8_t> def = dynamic ? deflateDynamic(raw.data(), raw.size())
                                       : deflateFixed(raw.data(), raw.size());
    std::vector<uint8_t> comp = zlibWrap(def, raw.data(), raw.size());
    writeChunk(png, "IDAT", comp.data(), comp.size());
    writeChunk(png, "IEND", NULL, 0);
    return png;
}

static size_t idatPayloadSize(const std::vector<uint8_t> &png)
{
    size_t total = 0, p = 8;
    while (p + 8 <= png.size()) {
        uint32_t len = (png[p] << 24) | (png[p+1] << 16) | (png[p+2] << 8) | png[p+3];
        uint32_t tag = (png[p+4] << 24) | (png[p+5] << 16) | (png[p+6] << 8) | png[p+7];
        if (tag == 0x49444154) total += len; // 'IDAT'
        p += 8 + len + 4;
    }
    return total;
}

//==========================================================================
// Decode helpers
//==========================================================================
struct Decoded {
    PNG png;
    uint8_t *buf = NULL;
    int w = 0, h = 0, bpp = 0, pitch = 0;
    Decoded() = default;
    Decoded(const Decoded &) = delete;
    Decoded &operator=(const Decoded &) = delete;
    Decoded(Decoded &&o) noexcept { *this = std::move(o); }
    Decoded &operator=(Decoded &&o) noexcept {
        if (this != &o) {
            free(buf);
            png = std::move(o.png);
            buf = o.buf; w = o.w; h = o.h; bpp = o.bpp; pitch = o.pitch;
            o.buf = NULL;
        }
        return *this;
    }
    ~Decoded() { free(buf); }
};

static Decoded decodeOne(const std::vector<uint8_t> &data)
{
    Decoded d;
    int rc = d.png.openRAM((uint8_t *)data.data(), (int)data.size(), NULL);
    if (rc != PNG_SUCCESS) { printf("openRAM failed: %d\n", rc); g_failures++; return d; }
    d.w = d.png.getWidth(); d.h = d.png.getHeight(); d.bpp = d.png.getBpp();
    d.pitch = (d.w * d.bpp + 7) / 8;
    d.buf = (uint8_t *)malloc(d.png.getBufferSize());
    d.png.setBuffer(d.buf);
    rc = d.png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) { printf("decode failed: %d\n", rc); g_failures++; }
    return d;
}

static std::vector<uint8_t> expandIndices(const Decoded &d)
{
    std::vector<uint8_t> v((size_t)d.w * d.h);
    for (int y = 0; y < d.h; y++) {
        uint8_t *s = &d.buf[(size_t)y * d.pitch];
        for (int x = 0; x < d.w; x++) {
            switch (d.bpp) {
                case 8: v[y*d.w+x] = s[x]; break;
                case 4: v[y*d.w+x] = (x & 1) ? (s[x>>1] & 0xf) : (s[x>>1] >> 4); break;
                case 2: v[y*d.w+x] = (s[x>>2] >> (6 - 2*(x&3))) & 3; break;
                case 1: v[y*d.w+x] = (s[x>>3] >> (7-(x&7))) & 1; break;
            }
        }
    }
    return v;
}

static long microsNow()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

//==========================================================================
// Synthetic test images
//==========================================================================
struct Sample {
    const char *name;
    int w, h, bpp, nColors, nAlpha;
    std::vector<uint8_t> palRGB;
    std::vector<uint8_t> alpha;
    std::vector<uint8_t> indices; // one byte per pixel
};

// Image 1: 64x48 8bpp, 256 PLTE entries, 10 used colors placed in far-apart
// high slots. A dominant background holds ~75% of pixels; 9 secondary colors
// form solid rectangular blobs with strictly decreasing, distinct areas.
// Frequent colors land in low indices after the frequency reorder, so SUB
// filter residuals stay small and sparse -> shorter Huffman codes.
static Sample makeImage1()
{
    Sample s;
    s.name = "img1_8bpp_skew"; s.w = 64; s.h = 48; s.bpp = 8;
    s.nColors = 256; s.nAlpha = 0;
    s.palRGB.assign(256 * 3, 0);
    s.alpha.assign(256, 255);
    // Dominant color in slot 240; secondaries in widely separated slots
    const uint8_t used[10][3] = {
        {235,235,235}, // 240 dominant background
        {200, 20, 20},{ 20,200, 20},{ 20, 20,200},{220,220, 10},{10,220,220},
        {220, 10,220},{120, 60,  0},{  0,120, 60},{ 60,  0,120}};
    const int slot[10] = {240, 17, 231, 58, 143, 79, 196, 102, 33, 167};
    for (int i = 0; i < 10; i++)
        memcpy(&s.palRGB[slot[i] * 3], used[i], 3);
    s.indices.resize((size_t)s.w * s.h, (uint8_t)slot[0]);
    // Nine solid blobs, areas strictly decreasing: 12x10, 10x9, 9x8, ...
    int blobs[9][4] = {
        {2,  2, 12, 10}, {18,  2, 10, 9}, {32,  2,  9, 8},
        {45, 2,  8,  7}, {2, 16,  9, 8}, {16, 16,  8, 7},
        {30,16,  7,  6}, {44,16,  6, 5}, {54,24,  6, 5}};
    for (int b = 0; b < 9; b++)
        for (int y = blobs[b][1]; y < blobs[b][1] + blobs[b][3]; y++)
            for (int x = blobs[b][0]; x < blobs[b][0] + blobs[b][2]; x++)
                if (x < s.w && y < s.h)
                    s.indices[y * s.w + x] = (uint8_t)slot[b + 1];
    return s;
}

// Image 2: 32x24 4bpp, all 16 colors used exactly 48 times (equal frequency,
// stable frequency sort cannot help) in a smooth snake of gradient colors.
// The palette slots are shuffled so original index deltas are large; the
// similarity nearest-neighbor walk restores gradient adjacency, turning the
// SUB residuals into mostly zeros and long match runs.
static Sample makeImage2()
{
    Sample s;
    s.name = "img2_4bpp_bands"; s.w = 32; s.h = 24; s.bpp = 4;
    s.nColors = 16;
    s.palRGB.assign(256 * 3, 0);
    s.alpha.assign(256, 255);
    for (int i = 0; i < 16; i++) {
        s.palRGB[i*3+0] = (uint8_t)(10 + i * 15);
        s.palRGB[i*3+1] = (uint8_t)(30 + i * 13);
        s.palRGB[i*3+2] = (uint8_t)(50 + i * 12);
    }
    const int perm[16] = {9,2,14,7,0,12,5,15,3,11,1,8,13,4,10,6};
    uint8_t shuffled[16 * 3];
    for (int k = 0; k < 16; k++) memcpy(&shuffled[perm[k]*3], &s.palRGB[k*3], 3);
    memcpy(s.palRGB.data(), shuffled, sizeof(shuffled));
    // Four distinct alpha values ride along to exercise tRNS remapping
    s.alpha[perm[0]] = 0; s.alpha[perm[1]] = 64;
    s.alpha[perm[2]] = 128; s.alpha[perm[3]] = 200;
    s.nAlpha = 16;
    s.indices.resize((size_t)s.w * s.h);
    // Each color occupies exactly 48 pixels: horizontal bands two rows high
    // (64 px each), alternating direction so color N is adjacent to N+1
    // everywhere, including the row-turn boundary.
    for (int y = 0; y < s.h; y++) {
        int band = y / 2;             // 0..11 -> logical colors in pairs below
        for (int x = 0; x < s.w; x++) {
            int pos = band * s.w + (band & 1 ? (s.w - 1 - x) : x);
            int logical = pos % 16;
            s.indices[y * s.w + x] = (uint8_t)perm[logical];
        }
    }
    return s;
}

//==========================================================================
// Per-image experiment
//==========================================================================
static void runSample(const Sample &samp, const char *outDir)
{
    printf("\n================ %s (%dx%d, %dbpp, %d PLTE entries) ================\n",
           samp.name, samp.w, samp.h, samp.bpp, samp.nColors);

    auto build = [&](const Sample &s, const uint8_t *pal, const uint8_t *alpha,
                     const uint8_t *idx, bool dyn) {
        return buildPNG(s.w, s.h, s.bpp, pal, 1 << s.bpp, alpha,
                        s.nAlpha ? (1 << s.bpp) : 0, idx, dyn);
    };

    std::vector<uint8_t> pngOrigD = build(samp, samp.palRGB.data(), samp.alpha.data(),
                                          samp.indices.data(), true);
    std::vector<uint8_t> pngOrigF = build(samp, samp.palRGB.data(), samp.alpha.data(),
                                          samp.indices.data(), false);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s_orig.png", outDir, samp.name);
    FILE *f = fopen(path, "wb"); fwrite(pngOrigD.data(), 1, pngOrigD.size(), f); fclose(f);

    int nColors = 1 << samp.bpp;
    int pitch = (samp.w * samp.bpp + 7) / 8;
    size_t rawStream = (size_t)samp.h * (pitch + 1);

    Decoded ref = decodeOne(pngOrigD);
    CHECK(ref.buf != NULL, "reference PNG decodes with the library");
    std::vector<uint8_t> refIdx = expandIndices(ref);
    uint8_t refPal[1024];
    memcpy(refPal, ref.png.getPalette(), sizeof(refPal));

    struct Variant {
        const char *tag = nullptr; int strategy = 0;
        Decoded dec; std::vector<uint8_t> idx; int order[256] = {}; int map[256] = {}; long reorderUs = 0;
        size_t idatD = 0, idatF = 0;
    };
    Variant variants[2];
    variants[0].tag = "frequency";  variants[0].strategy = REORDER_FREQ;
    variants[1].tag = "similarity"; variants[1].strategy = REORDER_SIM;

    for (auto &v : variants) {
        Decoded d = decodeOne(pngOrigD); // fresh decode per strategy
        long t0 = microsNow();
        int rc = PNG_reorderPalette(&d.png._png, v.strategy, v.order, v.map, nColors);
        v.reorderUs = microsNow() - t0;
        CHECK(rc == PNG_SUCCESS, "reorder returns PNG_SUCCESS");
        v.dec = std::move(d);
        v.idx = expandIndices(v.dec);

        // (2) correspondence: order/map bijection and per-pixel RGBA identity
        int seen[256]; memset(seen, 0, sizeof(seen));
        int bijection = 1;
        for (int i = 0; i < nColors; i++) {
            if (v.map[v.order[i]] != i) bijection = 0;
            if (v.order[i] < 0 || v.order[i] >= nColors || seen[v.order[i]]) bijection = 0;
            seen[v.order[i]] = 1;
        }
        CHECK(bijection, "order/map form a valid bijection of palette indices");

        int corr = 1;
        for (int p = 0; p < samp.w * samp.h; p++) {
            int oldI = refIdx[p], newI = v.idx[p];
            if (v.map[oldI] != newI) corr = 0;
            uint8_t *np = &v.dec.png.getPalette()[newI * 3];
            if (np[0] != refPal[oldI*3] || np[1] != refPal[oldI*3+1] || np[2] != refPal[oldI*3+2]) corr = 0;
            if (ref.png.hasAlpha() && v.dec.png.getPalette()[768 + newI] != refPal[768 + oldI]) corr = 0;
        }
        CHECK(corr, "every pixel keeps its exact RGBA color via the new palette");

        // Full RGB565 render path, line by line, must be byte-identical
        int identity = 1;
        for (int y = 0; y < samp.h; y++) {
            PNGDRAW da = {}, db = {};
            uint16_t a[64], b[64];
            da.iWidth = db.iWidth = samp.w; da.iBpp = db.iBpp = samp.bpp;
            da.iPixelType = db.iPixelType = PNG_PIXEL_INDEXED;
            da.iHasAlpha = ref.png.hasAlpha(); db.iHasAlpha = da.iHasAlpha;
            da.pPalette = refPal; db.pPalette = v.dec.png.getPalette();
            da.pPixels = &ref.buf[(size_t)y * pitch];
            db.pPixels = &v.dec.buf[(size_t)y * pitch];
            ref.png.getLineAsRGB565(&da, a, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
            v.dec.png.getLineAsRGB565(&db, b, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
            if (memcmp(a, b, (size_t)samp.w * 2) != 0) identity = 0;
        }
        CHECK(identity, "RGB565 decoded output is pixel-identical to the original");

        // Re-encode both DEFLATE variants and verify each decodes back exactly
        std::vector<uint8_t> packed(v.dec.buf, v.dec.buf + (size_t)samp.h * pitch);
        std::vector<uint8_t> pngD = build(samp, v.dec.png.getPalette(),
                                          &v.dec.png.getPalette()[768], v.idx.data(), true);
        std::vector<uint8_t> pngF = build(samp, v.dec.png.getPalette(),
                                          &v.dec.png.getPalette()[768], v.idx.data(), false);
        Decoded roundD = decodeOne(pngD), roundF = decodeOne(pngF);
        int roundtrip = (roundD.buf && roundF.buf &&
                         memcmp(roundD.buf, packed.data(), packed.size()) == 0 &&
                         memcmp(roundF.buf, packed.data(), packed.size()) == 0);
        CHECK(roundtrip, "re-encoded PNGs (dynamic+fixed DEFLATE) decode back to identical index bytes");
        v.idatD = idatPayloadSize(pngD);
        v.idatF = idatPayloadSize(pngF);
        snprintf(path, sizeof(path), "%s/%s_%s.png", outDir, samp.name, v.tag);
        f = fopen(path, "wb"); fwrite(pngD.data(), 1, pngD.size(), f); fclose(f);
    }

    size_t idatOrigD = idatPayloadSize(pngOrigD), idatOrigF = idatPayloadSize(pngOrigF);
    printf("\n  (1) Index encoding volume (filtered stream length fixed at %zu bytes):\n", rawStream);
    printf("      %-12s %16s %10s %9s | %16s %10s %9s\n",
           "variant", "IDAT dyn [B]", "vs orig", "gain", "IDAT fixed [B]", "vs orig", "gain");
    auto row = [&](const char *tag, size_t d, size_t f) {
        printf("      %-12s %16zu %+9ld %+8.2f%% | %16zu %+9ld %+8.2f%%\n", tag,
               d, (long)idatOrigD - (long)d, 100.0 * ((long)idatOrigD - (long)d) / idatOrigD,
               f, (long)idatOrigF - (long)f, 100.0 * ((long)idatOrigF - (long)f) / idatOrigF);
    };
    row("original", idatOrigD, idatOrigF);
    row(variants[0].tag, variants[0].idatD, variants[0].idatF);
    row(variants[1].tag, variants[1].idatD, variants[1].idatF);

    // (3) decode timing, median of 200 full decodes each (dynamic-Huffman files)
    auto timeDecode = [&](const std::vector<uint8_t> &png) {
        std::vector<long> samples;
        for (int it = 0; it < 200; it++) {
            PNG worker;
            worker.openRAM((uint8_t *)png.data(), (int)png.size(), NULL);
            uint8_t *b = (uint8_t *)malloc(worker.getBufferSize());
            worker.setBuffer(b);
            long t = microsNow();
            worker.decode(NULL, 0);
            samples.push_back(microsNow() - t);
            free(b);
        }
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    long tOrig = timeDecode(pngOrigD);
    long tF = timeDecode(build(samp, variants[0].dec.png.getPalette(),
                               &variants[0].dec.png.getPalette()[768], variants[0].idx.data(), true));
    long tS = timeDecode(build(samp, variants[1].dec.png.getPalette(),
                               &variants[1].dec.png.getPalette()[768], variants[1].idx.data(), true));
    printf("\n  (3) Decode time (median of 200 full decodes, microseconds):\n");
    printf("      original %ld us | frequency %ld us | similarity %ld us\n", tOrig, tF, tS);
    printf("      reorder function itself (one-shot): frequency %ld us, similarity %ld us\n",
           variants[0].reorderUs, variants[1].reorderUs);

    // (4) strategy comparison
    printf("\n  (4) Strategy comparison:\n");
    printf("      frequency: descending usage count, ties keep original order\n");
    printf("      similarity: greedy nearest-neighbor RGB walk seeded by the most-used color\n");
    const char *bestD = variants[0].idatD <= variants[1].idatD ? "frequency" : "similarity";
    printf("      dynamic IDAT: %s is smaller (%zu vs %zu bytes)\n",
           bestD, variants[0].idatD, variants[1].idatD);
}

//==========================================================================
// Sub-byte padding preservation (1bpp/2bpp odd widths)
//==========================================================================
static void checkSubBytePadding()
{
    printf("\n================ sub-byte padding preservation ================\n");
    struct Case { int w, h, bpp, n; };
    Case cases[2] = {{7, 4, 2, 4}, {13, 3, 1, 2}};
    for (const Case &cs : cases) {
        Sample s;
        s.w = cs.w; s.h = cs.h; s.bpp = cs.bpp; s.nColors = cs.n; s.nAlpha = 0;
        s.palRGB.assign(256 * 3, 0); s.alpha.assign(256, 255);
        for (int i = 0; i < cs.n; i++) { s.palRGB[i*3] = (uint8_t)(30 * i + 1); s.palRGB[i*3+1] = (uint8_t)(20 * i + 5); s.palRGB[i*3+2] = (uint8_t)(10 * i + 9); }
        s.indices.resize((size_t)cs.w * cs.h);
        for (int p = 0; p < cs.w * cs.h; p++) s.indices[p] = (uint8_t)((p * 7 + 3) % cs.n);
        auto png = buildPNG(s.w, s.h, s.bpp, s.palRGB.data(), cs.n, NULL, 0, s.indices.data(), true);
        Decoded d = decodeOne(png);
        int order[256], map[256];
        int rc = PNG_reorderPalette(&d.png._png, REORDER_SIM, order, map, cs.n);
        CHECK(rc == PNG_SUCCESS, "odd-width sub-byte image reorders successfully");
        auto idx = expandIndices(d);
        int ok = 1;
        for (int p = 0; p < cs.w * cs.h; p++)
            if (idx[p] != map[s.indices[p]]) ok = 0;
        CHECK(ok, "every sub-byte pixel follows the index permutation");
        // Unused padding bits live in the LOW bits of each line's LAST byte
        int pitch = (cs.w * cs.bpp + 7) / 8;
        int padBits = pitch * 8 - cs.w * cs.bpp;
        uint8_t mask = padBits ? (uint8_t)((1 << padBits) - 1) : 0;
        int padOk = 1;
        Decoded d0 = decodeOne(png);
        for (int y = 0; y < cs.h; y++) {
            int last = y * pitch + pitch - 1;
            if (mask && (d.buf[last] & mask) != (d0.buf[last] & mask)) padOk = 0;
        }
        CHECK(padOk, "unused padding bits in each line are preserved");
    }
}

//==========================================================================
// Error path tests
//==========================================================================
static void runErrorTests()
{
    printf("\n================ error path tests ================\n");
    Sample s = makeImage1();
    std::vector<uint8_t> png = buildPNG(s.w, s.h, s.bpp, s.palRGB.data(), s.nColors,
                                        s.alpha.data(), s.nAlpha, s.indices.data(), true);
    Decoded d = decodeOne(png);

    // Empty palette: all-zero RGB slots must be rejected, state left untouched
    PNGIMAGE empty;
    memset(&empty, 0, sizeof(empty));
    uint8_t dummyBuf[64] = {0};
    empty.pImage = dummyBuf;
    empty.ucPixelType = PNG_PIXEL_INDEXED;
    empty.iWidth = 8; empty.iHeight = 8; empty.ucBpp = 2; empty.iPitch = 2;
    int rc = PNG_reorderPalette(&empty, REORDER_FREQ, NULL, NULL, 4);
    CHECK(rc == PNG_INVALID_PARAMETER, "empty palette returns PNG_INVALID_PARAMETER");
    int untouched = 1;
    for (int i = 0; i < 12; i++) if (empty.ucPalette[i] != 0) untouched = 0;
    for (unsigned char i : dummyBuf) if (i != 0) untouched = 0;
    CHECK(untouched, "failed empty-palette call leaves palette and image untouched");

    rc = PNG_reorderPalette(NULL, REORDER_FREQ, NULL, NULL, 4);
    CHECK(rc == PNG_INVALID_PARAMETER, "NULL PNGIMAGE pointer rejected");

    PNGIMAGE nobuf;
    memset(&nobuf, 0, sizeof(nobuf));
    nobuf.ucPixelType = PNG_PIXEL_INDEXED; nobuf.ucBpp = 8;
    nobuf.iWidth = 8; nobuf.iHeight = 8;
    rc = PNG_reorderPalette(&nobuf, REORDER_FREQ, NULL, NULL, 256);
    CHECK(rc == PNG_INVALID_PARAMETER, "missing decoded index buffer rejected");

    d.png._png.ucPixelType = PNG_PIXEL_TRUECOLOR;
    rc = PNG_reorderPalette(&d.png._png, REORDER_FREQ, NULL, NULL, 256);
    CHECK(rc == PNG_INVALID_PARAMETER, "non-indexed image rejected");
    d.png._png.ucPixelType = PNG_PIXEL_INDEXED;

    CHECK(PNG_reorderPalette(&d.png._png, 99, NULL, NULL, 256) == PNG_INVALID_PARAMETER, "unknown strategy rejected");
    CHECK(PNG_reorderPalette(&d.png._png, REORDER_FREQ, NULL, NULL, 3) == PNG_INVALID_PARAMETER, "non-power-of-two count rejected");
    CHECK(PNG_reorderPalette(&d.png._png, REORDER_FREQ, NULL, NULL, 512) == PNG_INVALID_PARAMETER, "count above 256 rejected");
    CHECK(PNG_reorderPalette(&d.png._png, REORDER_FREQ, NULL, NULL, 1) == PNG_INVALID_PARAMETER, "count below 2 rejected");
}

int main()
{
    const char *outDir = "samples";
    mkdir(outDir, 0755);

    Sample a = makeImage1();
    Sample b = makeImage2();
    runSample(a, outDir);
    runSample(b, outDir);
    checkSubBytePadding();
    runErrorTests();

    printf("\n==== %s ====\n", g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_failures ? 1 : 0;
}
