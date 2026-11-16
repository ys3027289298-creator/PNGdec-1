//
// PNG Decoder
//
// written by Larry Bank
// bitbank@pobox.com
// Arduino port started 5/3/2021
// Original PNG code written 20+ years ago :)
// The goal of this code is to decode PNG images on embedded systems
//
// Copyright 2021 BitBank Software, Inc. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//===========================================================================
//
#include "PNGdec.h"

// forward references
PNG_STATIC int PNGInit(PNGIMAGE *pPNG);
PNG_STATIC int DecodePNG(PNGIMAGE *pImage, void *pUser, int iOptions);
PNG_STATIC uint8_t PNGMakeMask(PNGDRAW *pDraw, uint8_t *pMask, uint8_t ucThreshold);
// Include the C code which does the actual work
#include "png.inl"

//
// Memory initialization
//
int PNG::openRAM(uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw)
{
    memset(&_png, 0, sizeof(PNGIMAGE));
    _png.ucMemType = PNG_MEM_RAM;
    _png.pfnRead = readRAM;
    _png.pfnSeek = seekMem;
    _png.pfnDraw = pfnDraw;
    _png.pfnOpen = NULL;
    _png.pfnClose = NULL;
    _png.PNGFile.iSize = iDataSize;
    _png.PNGFile.pData = pData;
    return PNGInit(&_png);
} /* openRAM() */
//
// It's necessary to separate out a FLASH version on Harvard architecture machines
//
int PNG::openFLASH(uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw)
{
    memset(&_png, 0, sizeof(PNGIMAGE));
    _png.ucMemType = PNG_MEM_FLASH;
    _png.pfnRead = readFLASH;
    _png.pfnSeek = seekMem;
    _png.pfnDraw = pfnDraw;
    _png.pfnOpen = NULL;
    _png.pfnClose = NULL;
    _png.PNGFile.iSize = iDataSize;
    _png.PNGFile.pData = pData;
    return PNGInit(&_png);
} /* openRAM() */

//
// File (SD/MMC) based initialization
//
int PNG::open(const char *szFilename, PNG_OPEN_CALLBACK *pfnOpen, PNG_CLOSE_CALLBACK *pfnClose, PNG_READ_CALLBACK *pfnRead, PNG_SEEK_CALLBACK *pfnSeek, PNG_DRAW_CALLBACK *pfnDraw)
{
    memset(&_png, 0, sizeof(PNGIMAGE));
    _png.pfnRead = pfnRead;
    _png.pfnSeek = pfnSeek;
    _png.pfnDraw = pfnDraw;
    _png.pfnOpen = pfnOpen;
    _png.pfnClose = pfnClose;
    _png.PNGFile.fHandle = (*pfnOpen)(szFilename, &_png.PNGFile.iSize);
    if (_png.PNGFile.fHandle == NULL)
       return 0;
    return PNGInit(&_png);

} /* open() */
//
// Return a pointer to the comment string or NULL if not present
//
char * PNG::getComment(void)
{
    if (_png.szComment[0]) return &_png.szComment[0];
    return NULL;
} /* getComment() */

//
// return the last error (if any)
//
int PNG::getLastError()
{
    return _png.iError;
} /* getLastError() */
//
// Get the width of the image in pixels
// can be called after opening the file (before decoding)
//
int PNG::getWidth()
{
    return _png.iWidth;
} /* getWidth() */
//
// Get the height of the image in pixels
// can be called after opening the file (before decoding)
//
int PNG::getHeight()
{
    return _png.iHeight;
} /* getHeight() */
//
// For truecolor and palette images, it's possible to have a single
// transparent color defined. This call will return it if defined
//
uint32_t PNG::getTransparentColor()
{
    return _png.iTransparent;
} /* getTransparentColor() */
//
// Alpha information can be per pixel, per color or a single color
// depending on the PNG pixel type of the image
// This call simply tells you if there is alpha for the current pixel type
//
int PNG::hasAlpha()
{
    return _png.iHasAlpha;
} /* hasAlpha() */
//
// Returns true or false for the use of Adam7 interlacing
// This option is not supported by the decoder, but after opening the image
// you can determine if it's set
//
int PNG::isInterlaced()
{
    return _png.iInterlaced;
} /* isInterlaced() */

//
// Returns the number of bits per color stimulus
// values of 1,2,4, and 8 are supported
//
int PNG::getBpp()
{
    return (int)_png.ucBpp;
} /* getBpp() */
//
// Returns the PNG pixel type (see enum in PNGdec.h)
//
int PNG::getPixelType()
{
    return (int)_png.ucPixelType;
} /* getPixelType() */
//
// Set the image buffer to memory managed by the caller
// If set, decode() will not use the PNGDRAW callback function
// and instead write the image into this buffer in one shot
//
void PNG::setBuffer(uint8_t *pBuffer)
{
    _png.pImage = pBuffer;
} /* setBuffer() */
//
// Returns the previously set image buffer or NULL if there is none
//
uint8_t * PNG::getBuffer()
{
    return _png.pImage;
} /* getBuffer() */
//
// Returns the size in bytes of the buffer needed to hold the uncompressed image
//
int PNG::getBufferSize()
{
    return _png.iHeight * _png.iPitch;
} /* getBufferSize() */
//
// Returns a pointer to the palette
// If there is alpha info for the palette, it starts at pPalette[768]
//
uint8_t * PNG::getPalette()
{
    return _png.ucPalette;
} /* getPalette() */
//
// Close the file - not needed when decoding from memory
//
void PNG::close()
{
    if (_png.pfnClose)
        (*_png.pfnClose)(_png.PNGFile.fHandle);
} /* close() */

//
// Decode the image
// returns:
// 0 = PNG_SUCCESS
// non 0 = PNG enumerated error code
//
int PNG::decode(void *pUser, int iOptions)
{
    return DecodePNG(&_png, pUser, iOptions);
} /* decode() */
//
// Convert a line of native pixels (all supported formats) into RGB565
// can optionally mix in a background color - set to -1 to disable
// Background color is in the form of a uint32_t -> 00BBGGRR (MSB on left)
//
void PNG::getLineAsRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndianness, uint32_t u32Bkgd)
{
    PNGRGB565(pDraw, pPixels, iEndianness, u32Bkgd, hasAlpha());
} /* getLineAsRGB565() */

uint8_t PNG::getAlphaMask(PNGDRAW *pDraw, uint8_t *pMask, uint8_t ucThreshold)
{
    return PNGMakeMask(pDraw, pMask, ucThreshold);
} /* getAlphaMask() */

//
// Palette reordering for indexed images
//
// The order of palette entries determines which index values the packed
// pixel data uses. Reordering the palette and remapping the indices lets
// an encoder produce better compression without changing a single decoded
// pixel: the same RGB(A) color always ends up at every position it was
// used before the reorder.
//
// Two deterministic strategies are provided:
//   PNG_PALETTE_REORDER_FREQUENCY (1): most frequently used colors first
//   PNG_PALETTE_REORDER_SIMILARITY (2): greedy nearest-neighbor walk over
//                                       RGB color space, seeded with the
//                                       most frequently used color
//
#define PNG_PALETTE_REORDER_FREQUENCY 1
#define PNG_PALETTE_REORDER_SIMILARITY 2

//
// Count how often each palette index occurs in the packed index image
// Supports 1, 2, 4 and 8 bits per pixel (the PNG indexed formats)
//
PNG_STATIC void PNGIndexHistogram(PNGIMAGE *pPNG, int *pCount)
{
    int x, y, i;
    int iBpp = pPNG->ucBpp;
    int iColors = 1 << iBpp;

    for (i = 0; i < iColors; i++) pCount[i] = 0;
    for (y = 0; y < pPNG->iHeight; y++) {
        uint8_t *s = &pPNG->pImage[y * pPNG->iPitch];
        uint8_t ucPixel = 0;
        switch (iBpp) {
            case 8:
                for (x = 0; x < pPNG->iWidth; x++)
                    pCount[s[x]]++;
                break;
            case 4:
                for (x = 0; x < pPNG->iWidth; x += 2) {
                    ucPixel = s[x >> 1];
                    pCount[ucPixel >> 4]++;
                    if (x + 1 < pPNG->iWidth) // last nibble of an odd-width line is padding
                        pCount[ucPixel & 0xf]++;
                }
                break;
            case 2:
                for (x = 0; x < pPNG->iWidth; x++) {
                    if ((x & 3) == 0)
                        ucPixel = *s++;
                    pCount[ucPixel >> 6]++;
                    ucPixel <<= 2;
                }
                break;
            case 1:
                for (x = 0; x < pPNG->iWidth; x++) {
                    if ((x & 7) == 0)
                        ucPixel = *s++;
                    pCount[ucPixel >> 7]++;
                    ucPixel <<= 1;
                }
                break;
        } // switch on bpp
    } // for each line
} /* PNGIndexHistogram() */

//
// Squared RGB distance between two palette entries
//
PNG_STATIC int PNGPaletteDistance(uint8_t *pPalette, int i, int j)
{
    int dr = (int)pPalette[i*3+0] - (int)pPalette[j*3+0];
    int dg = (int)pPalette[i*3+1] - (int)pPalette[j*3+1];
    int db = (int)pPalette[i*3+2] - (int)pPalette[j*3+2];
    return dr*dr + dg*dg + db*db;
} /* PNGPaletteDistance() */

//
// Remap every packed index in the image through the given permutation.
// Sub-byte formats are rewritten in place one byte at a time and the
// unused padding bits of the final byte of each line are preserved.
//
PNG_STATIC void PNGRemapPixels(PNGIMAGE *pPNG, int *pMap)
{
    int x, y, k, b;
    int iBpp = pPNG->ucBpp;

    for (y = 0; y < pPNG->iHeight; y++) {
        uint8_t *s = &pPNG->pImage[y * pPNG->iPitch];
        switch (iBpp) {
            case 8:
                for (x = 0; x < pPNG->iWidth; x++)
                    s[x] = (uint8_t)pMap[s[x]];
                break;
            case 4:
                for (k = 0; k < pPNG->iPitch; k++) {
                    uint8_t v = s[k];
                    uint8_t out = (uint8_t)(pMap[v >> 4] << 4);
                    if (2*k + 1 < pPNG->iWidth)
                        out |= (uint8_t)pMap[v & 0xf];
                    else // keep the unused low nibble untouched
                        out |= (v & 0xf);
                    s[k] = out;
                }
                break;
            case 2:
                for (k = 0; k < pPNG->iPitch; k++) {
                    int iValid = pPNG->iWidth - 4*k;
                    uint8_t v = s[k], out = 0;
                    if (iValid > 4) iValid = 4;
                    for (b = 0; b < iValid; b++)
                        out |= (uint8_t)(pMap[(v >> (6 - 2*b)) & 3] << (6 - 2*b));
                    if (iValid < 4) // preserve unused padding bits
                        out |= v & (uint8_t)((1 << (8 - iValid*2)) - 1);
                    s[k] = out;
                }
                break;
            case 1:
                for (k = 0; k < pPNG->iPitch; k++) {
                    int iValid = pPNG->iWidth - 8*k;
                    uint8_t v = s[k], out = 0;
                    if (iValid > 8) iValid = 8;
                    for (b = 0; b < iValid; b++) {
                        if (pMap[(v >> (7-b)) & 1])
                            out |= (uint8_t)(1 << (7-b));
                    }
                    if (iValid < 8) // preserve unused padding bits
                        out |= v & (uint8_t)(0xff >> iValid);
                    s[k] = out;
                }
                break;
        } // switch on bpp
    } // for each line
} /* PNGRemapPixels() */

//
// Reorder the palette of a decoded indexed image and regenerate its
// packed index image so the decoded pixel output is unchanged.
//
// Parameters:
//   pPNG       - PNGIMAGE previously decoded into a caller-provided buffer
//   iStrategy  - PNG_PALETTE_REORDER_FREQUENCY or PNG_PALETTE_REORDER_SIMILARITY
//   pOldOrder  - optional: receives iCount old palette indices in new order
//   pNewMap    - optional: receives the old-index -> new-index permutation
//   iCount     - number of palette entries to reorder (power of 2, 2..256)
//
// Returns PNG_SUCCESS or an enumerated error code.
//
extern "C" int PNG_reorderPalette(PNGIMAGE *pPNG, int iStrategy,
                                  int *pOldOrder, int *pNewMap, int iCount)
{
    int i, j;
    int iHist[256], iOrder[256], iMap[256];
    uint8_t *pPal;
    int bEmpty;

    if (pPNG == NULL)
        return PNG_INVALID_PARAMETER;
    if (pPNG->pImage == NULL ||
        (iStrategy != PNG_PALETTE_REORDER_FREQUENCY &&
         iStrategy != PNG_PALETTE_REORDER_SIMILARITY)) {
        pPNG->iError = PNG_INVALID_PARAMETER;
        return pPNG->iError;
    }
    if (pPNG->ucPixelType != PNG_PIXEL_INDEXED ||
        pPNG->iWidth <= 0 || pPNG->iHeight <= 0 || pPNG->ucBpp == 0) {
        pPNG->iError = PNG_INVALID_PARAMETER; // not a decoded indexed image
        return pPNG->iError;
    }
    if (iCount < 2 || iCount > 256 || (iCount & (iCount - 1)) != 0 ||
        iCount > (1 << pPNG->ucBpp)) {
        pPNG->iError = PNG_INVALID_PARAMETER;
        return pPNG->iError;
    }
    pPal = pPNG->ucPalette;
    // An empty palette (no PLTE loaded) has every RGB slot at zero.
    bEmpty = 1;
    for (i = 0; i < iCount * 3; i++) {
        if (pPal[i] != 0) { bEmpty = 0; break; }
    }
    if (bEmpty) {
        pPNG->iError = PNG_INVALID_PARAMETER;
        return pPNG->iError;
    }

    PNGIndexHistogram(pPNG, iHist);

    if (iStrategy == PNG_PALETTE_REORDER_FREQUENCY) {
        // Descending usage count; equal counts keep their original relative
        // order (strict comparison makes the selection sort deterministic).
        for (i = 0; i < iCount; i++) iOrder[i] = i;
        for (i = 0; i < iCount - 1; i++) {
            int iBest = i;
            for (j = i + 1; j < iCount; j++) {
                if (iHist[iOrder[j]] > iHist[iOrder[iBest]])
                    iBest = j;
            }
            if (iBest != i) {
                int iTemp = iOrder[i];
                iOrder[i] = iOrder[iBest];
                iOrder[iBest] = iTemp;
            }
        }
    } else {
        // Greedy nearest-neighbor walk in RGB color space.
        // Start at the most frequently used color, then repeatedly append
        // the closest unused entry (lowest index breaks a tie).
        uint8_t ucUsed[256];
        int iCurrent, iUsed = 0;
        memset(ucUsed, 0, sizeof(ucUsed));
        iCurrent = 0;
        for (i = 1; i < iCount; i++) {
            if (iHist[i] > iHist[iCurrent])
                iCurrent = i;
        }
        iOrder[iUsed++] = iCurrent;
        ucUsed[iCurrent] = 1;
        while (iUsed < iCount) {
            int iBest = -1, iBestDist = 0;
            for (j = 0; j < iCount; j++) {
                int iDist;
                if (ucUsed[j]) continue;
                iDist = PNGPaletteDistance(pPal, iCurrent, j);
                if (iBest < 0 || iDist < iBestDist) {
                    iBest = j;
                    iBestDist = iDist;
                }
            }
            iCurrent = iBest;
            ucUsed[iCurrent] = 1;
            iOrder[iUsed++] = iCurrent;
        }
    }

    // Build the old-index -> new-index permutation and report it if asked
    for (i = 0; i < iCount; i++)
        iMap[iOrder[i]] = i;
    if (pOldOrder) {
        for (i = 0; i < iCount; i++)
            pOldOrder[i] = iOrder[i];
    }
    if (pNewMap) {
        for (i = 0; i < iCount; i++)
            pNewMap[i] = iMap[i];
    }

    // Regenerate the packed index image through the permutation
    PNGRemapPixels(pPNG, iMap);

    // Verify the remap while iOrder is still intact: the color now living in
    // new slot i must show the exact usage count the old entry had.
    {
        int iHist2[256];
        PNGIndexHistogram(pPNG, iHist2);
        for (i = 0; i < iCount; i++) {
            if (iHist2[i] != iHist[iOrder[i]]) {
                pPNG->iError = PNG_DECODE_ERROR;
                return pPNG->iError;
            }
        }
    }

    // Rotate the palette into the new order in place (one cycle at a time),
    // moving each RGB triple and its alpha byte together. iOrder is consumed
    // as the visited marker (-1), so no extra palette-sized buffer is needed.
    for (i = 0; i < iCount; i++) {
        int j, k;
        uint8_t ucR, ucG, ucB, ucA;
        if (iOrder[i] < 0)
            continue; // this position was already reached by another cycle
        ucR = pPal[i * 3 + 0];
        ucG = pPal[i * 3 + 1];
        ucB = pPal[i * 3 + 2];
        ucA = pPal[768 + i];
        j = i;
        for (;;) {
            k = iOrder[j];
            iOrder[j] = -1;
            if (k == i) { // cycle closed: drop in the saved starting entry
                pPal[j * 3 + 0] = ucR;
                pPal[j * 3 + 1] = ucG;
                pPal[j * 3 + 2] = ucB;
                pPal[768 + j] = ucA;
                break;
            }
            pPal[j * 3 + 0] = pPal[k * 3 + 0];
            pPal[j * 3 + 1] = pPal[k * 3 + 1];
            pPal[j * 3 + 2] = pPal[k * 3 + 2];
            pPal[768 + j] = pPal[768 + k];
            j = k;
        }
    }

    pPNG->iError = PNG_SUCCESS;
    return PNG_SUCCESS;
} /* PNG_reorderPalette() */
