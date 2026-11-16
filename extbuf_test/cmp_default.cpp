//
// cmp_default.cpp - prove default-mode (internal buffers) decoding is
// pixel-identical between the original library and the modified one.
// Built twice: once against each png.inl; outputs raw decoded images to
// files that are then byte-compared.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include "PNGdec.h"

static std::vector<uint8_t> read_file(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

int main(int argc, char **argv)
{
    if (argc != 3) { printf("usage: %s in.png out.raw\n", argv[0]); return 2; }
    std::vector<uint8_t> data = read_file(argv[1]);
    PNG png;
    int rc = png.openRAM(data.data(), (int)data.size(), NULL);
    if (rc != PNG_SUCCESS) {
        // encode non-success as a marker file naming convention via stderr
        fprintf(stderr, "open rc=%d err=%d i7=%d\n", rc, png.getLastError(),
                png.isInterlaced());
        // still write an empty marker so caller knows
        std::ofstream(argv[2], std::ios::binary).put((char)rc);
        return 0;
    }
    std::vector<uint8_t> out(png.getBufferSize());
    png.setBuffer(out.data());
    rc = png.decode(NULL, 0);
    if (rc != PNG_SUCCESS) {
        fprintf(stderr, "decode rc=%d err=%d\n", rc, png.getLastError());
        std::ofstream(argv[2], std::ios::binary).put((char)rc);
        return 0;
    }
    std::ofstream f(argv[2], std::ios::binary);
    f.write((const char *)out.data(), out.size());
    return 0;
}
