// Test-only shim for the BASELINE (unmodified) library, which has no
// PNG_getLineAsU16 symbol. 16-bit files are rejected by the baseline at
// openRAM(), so this stub only ever sees the 8-bit path that the old
// library supports; it zero-extends native 8-bit samples. It is linked
// into the baseline harness only and is not part of the library.
#include <cstdint>
#include "../src/PNGdec.h"

#ifndef PNG_SCALE_TRUNCATE
#define PNG_SCALE_TRUNCATE 0
#define PNG_SCALE_ROUND 1
#endif

extern "C" int PNG_getLineAsU16(PNGDRAW *pDraw, uint16_t *pPixels,
                                int iOutBpp, int /*iScale*/) {
    if (!pDraw || !pPixels || iOutBpp != 16 || pDraw->iBpp != 8)
        return -1;
    int channels =
        (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR) ? 3 : 1;
    uint8_t *s = pDraw->pPixels;
    uint16_t *d = pPixels;
    int count = pDraw->iWidth * channels;
    for (int i = 0; i < count; i++)
        *d++ = (uint16_t)(*s++);
    return count;
}
