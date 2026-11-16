//
// c_smoke.c - exercise the plain-C build path + external buffer mode with
// static storage only (the realistic embedded usage pattern).
//
// The library's C build model includes png.inl directly in one translation
// unit (PNG_STATIC expands to empty). Some convenience getters are only
// implemented for the C++ class, so this uses the documented PNGIMAGE fields
// plus the C functions that do exist (PNG_openRAM/PNG_decode/PNG_close...).
//
#include <stdio.h>
#include <string.h>
#include "PNGdec.h"
#include "png.inl"
#include "png_extbuf.h"

static uint8_t s_workspace[44000];
static uint8_t s_linebuf[2048];
static uint8_t s_image[8192];

int main(int argc, char **argv)
{
    static uint8_t filedata[16384];
    FILE *f;
    long n;
    PNGIMAGE png;
    PNGEXTERNALBUFFERS ext;
    int rc;

    if (argc != 2) { printf("usage: %s file.png\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    n = (long)fread(filedata, 1, sizeof(filedata), f);
    fclose(f);

    memset(&png, 0, sizeof(png));
    rc = PNG_openRAM(&png, filedata, (int)n, NULL);
    if (rc != PNG_SUCCESS) { printf("PNG_openRAM failed: %d\n", rc); return 1; }
    printf("C API: %dx%d bpp=%d type=%d interlaced=%d alpha=%d\n",
           png.iWidth, png.iHeight, png.ucBpp, png.ucPixelType,
           png.iInterlaced, png.iHasAlpha);

    memset(&ext, 0, sizeof(ext));
    rc = PNG_getExternalBufferSizes(&png, &ext);
    if (rc != PNG_SUCCESS) { printf("size query failed: %d\n", rc); return 1; }
    printf("needs line=%d workspace=%d image=%d (pitch=%d)\n",
           ext.iLineBufferNeeded, ext.iWorkspaceNeeded,
           ext.iImageBufferNeeded, png.iPitch);
    if (ext.iLineBufferNeeded > (int)sizeof(s_linebuf) ||
        ext.iWorkspaceNeeded > (int)sizeof(s_workspace) ||
        ext.iImageBufferNeeded > (int)sizeof(s_image)) {
        printf("static buffers too small for this image\n");
        return 1;
    }
    PNG_setBuffer(&png, s_image);
    ext.pLineBuffer = s_linebuf;
    ext.iLineBufferSize = sizeof(s_linebuf);
    ext.pWorkspace = s_workspace;
    ext.iWorkspaceSize = sizeof(s_workspace);
    rc = PNG_decode(&png, &ext, 0);
    printf("PNG_decode returned %d (last error %d)\n", rc, PNG_getLastError(&png));
    PNG_close(&png);
    return rc == PNG_SUCCESS ? 0 : 1;
}
