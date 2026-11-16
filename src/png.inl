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
#include "zlib.h"
//
// Convert 8-bit grayscale into RGB565
//
static const uint16_t usGrayTo565[] = {0x0000,0x0000,0x0000,0x0000,0x0020,0x0020,0x0020,0x0020, // 0
    0x0841,0x0841,0x0841,0x0841,0x0861,0x0861,0x0861,0x0861,
    0x1082,0x1082,0x1082,0x1082,0x10a2,0x10a2,0x10a2,0x10a2,
    0x18c3,0x18c3,0x18c3,0x18c3,0x18e3,0x18e3,0x18e3,0x18e3,
    0x2104,0x2104,0x2104,0x2104,0x2124,0x2124,0x2124,0x2124,
    0x2945,0x2945,0x2945,0x2945,0x2965,0x2965,0x2965,0x2965,
    0x3186,0x3186,0x3186,0x3186,0x31a6,0x31a6,0x31a6,0x31a6,
    0x39c7,0x39c7,0x39c7,0x39c7,0x39e7,0x39e7,0x39e7,0x39e7,
    0x4208,0x4208,0x4208,0x4208,0x4228,0x4228,0x4228,0x4228,
    0x4a49,0x4a49,0x4a49,0x4a49,0x4a69,0x4a69,0x4a69,0x4a69,
    0x528a,0x528a,0x528a,0x528a,0x52aa,0x52aa,0x52aa,0x52aa,
    0x5acb,0x5acb,0x5acb,0x5acb,0x5aeb,0x5aeb,0x5aeb,0x5aeb,
    0x630c,0x630c,0x630c,0x630c,0x632c,0x632c,0x632c,0x632c,
    0x6b4d,0x6b4d,0x6b4d,0x6b4d,0x6b6d,0x6b6d,0x6b6d,0x6b6d,
    0x738e,0x738e,0x738e,0x738e,0x73ae,0x73ae,0x73ae,0x73ae,
    0x7bcf,0x7bcf,0x7bcf,0x7bcf,0x7bef,0x7bef,0x7bef,0x7bef,
    0x8410,0x8410,0x8410,0x8410,0x8430,0x8430,0x8430,0x8430,
    0x8c51,0x8c51,0x8c51,0x8c51,0x8c71,0x8c71,0x8c71,0x8c71,
    0x9492,0x9492,0x9492,0x9492,0x94b2,0x94b2,0x94b2,0x94b2,
    0x9cd3,0x9cd3,0x9cd3,0x9cd3,0x9cf3,0x9cf3,0x9cf3,0x9cf3,
    0xa514,0xa514,0xa514,0xa514,0xa534,0xa534,0xa534,0xa534,
    0xad55,0xad55,0xad55,0xad55,0xad75,0xad75,0xad75,0xad75,
    0xb596,0xb596,0xb596,0xb596,0xb5b6,0xb5b6,0xb5b6,0xb5b6,
    0xbdd7,0xbdd7,0xbdd7,0xbdd7,0xbdf7,0xbdf7,0xbdf7,0xbdf7,
    0xc618,0xc618,0xc618,0xc618,0xc638,0xc638,0xc638,0xc638,
    0xce59,0xce59,0xce59,0xce59,0xce79,0xce79,0xce79,0xce79,
    0xd69a,0xd69a,0xd69a,0xd69a,0xd6ba,0xd6ba,0xd6ba,0xd6ba,
    0xdedb,0xdedb,0xdedb,0xdedb,0xdefb,0xdefb,0xdefb,0xdefb,
    0xe71c,0xe71c,0xe71c,0xe71c,0xe73c,0xe73c,0xe73c,0xe73c,
    0xef5d,0xef5d,0xef5d,0xef5d,0xef7d,0xef7d,0xef7d,0xef7d,
    0xf79e,0xf79e,0xf79e,0xf79e,0xf7be,0xf7be,0xf7be,0xf7be,
    0xffdf,0xffdf,0xffdf,0xffdf,0xffff,0xffff,0xffff,0xffff};

//
// C interface
//
#ifndef __cplusplus
// Forward references
PNG_STATIC int32_t readFLASH(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
PNG_STATIC int32_t seekMem(PNGFILE *pFile, int32_t iPosition);
PNG_STATIC int PNGInit(PNGIMAGE *pPNG);

// C API
int PNG_openRAM(PNGIMAGE *pPNG, uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw)
{
    pPNG->iError = PNG_SUCCESS;
    pPNG->pfnRead = readFLASH;
    pPNG->pfnSeek = seekMem;
    pPNG->pfnDraw = pfnDraw;
    pPNG->pfnOpen = NULL;
    pPNG->pfnClose = NULL;
    pPNG->PNGFile.iSize = iDataSize;
    pPNG->PNGFile.pData = pData;
    pPNG->PNGFile.iPos = 0;
    return PNGInit(pPNG);
} /* PNG_openRAM() */

#ifdef __LINUX__
int32_t readFile(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!handle->fHandle) return 0;
  return (int32_t)fread(buffer, 1, length, (FILE *)handle->fHandle);
}
int32_t seekFile(PNGFILE *handle, int32_t position) {
  if (!handle->fHandle) return 0;
  return fseek((FILE *)handle->fHandle, position, SEEK_SET);
}
void closeFile(void *handle) {
    if (handle) {
        FILE *f = (FILE *)handle;
        fclose(f);
    }
}
int PNG_openFile(PNGIMAGE *pPNG, const char *szFilename, PNG_DRAW_CALLBACK *pfnDraw)
{
    pPNG->iError = PNG_SUCCESS;
    pPNG->pfnRead = readFile;
    pPNG->pfnSeek = seekFile;
    pPNG->pfnDraw = pfnDraw;
    pPNG->pfnOpen = NULL;
    pPNG->pfnClose = closeFile;
    pPNG->PNGFile.fHandle = fopen(szFilename, "r+b");
    if (pPNG->PNGFile.fHandle == NULL)
       return 0;
    fseek((FILE *)pPNG->PNGFile.fHandle, 0, SEEK_END);
    pPNG->PNGFile.iSize = (int)ftell((FILE *)pPNG->PNGFile.fHandle);
    fseek((FILE *)pPNG->PNGFile.fHandle, 0, SEEK_SET);
    return PNGInit(pPNG);
} /* PNG_openFile() */
#endif // __LINUX__
void PNG_close(PNGIMAGE *pPNG)
{
    if (pPNG->pfnClose)
        (*pPNG->pfnClose)(pPNG->PNGFile.fHandle);
} /* PNG_close() */

int PNG_getWidth(PNGIMAGE *pPNG)
{
    return pPNG->iWidth;
} /* PNG_getsWidth() */

int PNG_getHeight(PNGIMAGE *pPNG)
{
    return pPNG->iHeight;
} /* PNG_getHeight() */

int PNG_getLastError(PNGIMAGE *pPNG)
{
    return pPNG->iError;
} /* PNG_getLastError() */

uint8_t *PNG_getPalette(PNGIMAGE *pPNG)
{
    return pPNG->ucPalette;
} /* PNG_getPalette() */

int PNG_getBufferSize(PNGIMAGE *pPNG)
{
    return pPNG->iHeight * pPNG->iPitch;
} /* PNG_getBufferSize() */

uint8_t * PNG_getBuffer(PNGIMAGE *pPNG)
{
    return pPNG->pImage;
} /* PNG_getBuffer() */

#endif // !__cplusplus
PNG_STATIC uint8_t PNGMakeMask(PNGDRAW *pDraw, uint8_t *pMask, uint8_t ucThreshold)
{
    uint8_t alpha, c, *s, *d, *pPal;
    uint8_t cHasOpaque = 0;
    int i, x;
    
    switch (pDraw->iPixelType) {
        case PNG_PIXEL_TRUECOLOR_ALPHA: // truecolor + alpha
            s = pDraw->pPixels;
            d = pMask;
            for (x=0; x<pDraw->iWidth; x+=8) { // groups of 8 pixels in each byte of mask
                c = 0;
                for (i=0; i<8; i++) {
                    c <<= 1;
                    alpha = s[3];
                   if (alpha >= ucThreshold) // if opaque 'enough', set the bit
                       c |= 1;
                    s += 4;
                }
                *d++ = c;
                cHasOpaque |= c;
            }
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            s = pDraw->pPixels;
            d = pMask;
            for (x=0; x<pDraw->iWidth; x+=8) { // groups of 8 pixels in each byte of mask
                c = 0;
                for (i=0; i<8; i++) {
                    c <<= 1;
                    alpha = s[1];
                   if (alpha >= ucThreshold) // if opaque 'enough', set the bit
                       c |= 1;
                    s += 2;
                }
                *d++ = c;
                cHasOpaque |= c;
            }
            break;
        case PNG_PIXEL_INDEXED:
            s = pDraw->pPixels;
            pPal = &pDraw->pPalette[768];
            d = pMask;
            for (x=0; x<pDraw->iWidth; x+=8) { // groups of 8 pixels in each byte of mask
                uint8_t ucPix = 0;
                c = 0;
                switch (pDraw->iBpp) {
                    case 2:
                        for (i=0; i<8; i++) {
                            if (i == 0 || i == 4)
                                ucPix = *s++;
                            c <<= 1;
                            alpha = pPal[ucPix >> 6]; // get palette alpha for this color
                           if (alpha >= ucThreshold) // if opaque 'enough', set the bit
                               c |= 1;
                            ucPix <<= 2;
                        }
                        break;
                    case 4:
                        for (i=0; i<8; i++) {
                            if ((i & 1) == 0)
                                ucPix = *s++;
                            c <<= 1;
                            alpha = pPal[ucPix >> 4]; // get palette alpha for this color
                           if (alpha >= ucThreshold) // if opaque 'enough', set the bit
                               c |= 1;
                            ucPix <<= 4;
                        }
                        break;
                    case 8:
                        for (i=0; i<8; i++) {
                            c <<= 1;
                            alpha = pPal[s[0]]; // get palette alpha for this color
                           if (alpha >= ucThreshold) // if opaque 'enough', set the bit
                               c |= 1;
                            s++;
                        }
                        break;
                } // switch on bit depth
                *d++ = c;
                cHasOpaque |= c;
            }
            break;
        default: // No alpha channel; make a mask of all 1's
            memset(pMask, 0xff, (pDraw->iWidth+7)>>3);
            cHasOpaque = 1;
            break;
    } // switch on pixel type
    return cHasOpaque; // let the caller know if any pixels are opaque
} /* PNGMakeMask() */
#ifdef ARDUINO_ESP32S3_DEV
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
void s3_rgb565(uint8_t *pSrc, uint8_t *pDest, int iCount, bool bBigEndian);
#ifdef __cplusplus
};
#endif
#endif
//
// Convert a line of native PNG pixels into RGB565
// handles all standard pixel types
// written for simplicity, not necessarily performance
//
PNG_STATIC void PNGRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndiannes, uint32_t u32Bkgd, int iHasAlpha)
{
    int x, j;
    uint16_t usPixel, *pDest = pPixels;
    uint8_t c=0, a, *pPal, *s = pDraw->pPixels;
    
    switch (pDraw->iPixelType) {
        case PNG_PIXEL_GRAY_ALPHA:
            if (u32Bkgd != 0xffffffff) {
                // gray level of background color choice
                uint8_t u8BG = (uint8_t)(((u32Bkgd & 0xff) + ((u32Bkgd >> 7) & 0x1fe) + ((u32Bkgd >> 16) & 0xff)) / 4);
                uint16_t usBG = usGrayTo565[u8BG];
                for (x=0; x<pDraw->iWidth; x++) {
                    c = *s++; // gray level
                    a = *s++;
                    if (a == 0) {
                        usPixel = usBG;
                    } else if (a == 255) { // fully opaque
                        usPixel = usGrayTo565[c];
                    } else { // mix the colors
                        usPixel = (c * a) + (u8BG * (255-a));
                        usPixel = usGrayTo565[(usPixel >> 8)];
                    }
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                } // for x
            } else {
                for (x=0; x<pDraw->iWidth; x++) {
                    c = *s++; // gray level
                    a = *s++;
                    j = (a * c) >> 8; // multiply by the alpha
                    usPixel = usGrayTo565[j];
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                }
            }
            break;
        case PNG_PIXEL_GRAYSCALE:
            switch (pDraw->iBpp) {
               case 8:
                for (x=0; x<pDraw->iWidth; x++) {
                    c = *s++;
                    usPixel = (c >> 3); // blue
                    usPixel |= ((c >> 2) << 5); // green
                    usPixel |= ((c >> 3) << 11); // red
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                }
                break;
               case 4:
                for (x=0; x<pDraw->iWidth; x+=2) {
                    uint8_t d;
                    d = *s++;
                    c = (d & 0xf0) | (d >> 4); // left pixel
                    usPixel = (c >> 3); // blue
                    usPixel |= ((c >> 2) << 5); // green
                    usPixel |= ((c >> 3) << 11); // red
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                    c = (d & 0xf) | (d << 4); // right pixel
                    usPixel = (c >> 3); // blue 
                    usPixel |= ((c >> 2) << 5); // green
                    usPixel |= ((c >> 3) << 11); // red
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                }
                break;
                case 2:
                {
                   const uint16_t us2Bit[4] = {0x0000, 0x39e7, 0x7bef, 0xffff};
                   for (x=0; x<pDraw->iWidth; x++) {
                       if ((x & 3) == 0) {
                           c = *s++;
                       }
                       usPixel = us2Bit[c >> 6];
                       if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                           usPixel = __builtin_bswap16(usPixel);
                       *pDest++ = usPixel;
                       c <<= 2;
                   } // for x
                }
                break;
                case 1:
                   for (x=0; x<pDraw->iWidth; x++) {
                       if ((x & 7) == 0) {
                           c = *s++;
                       }
                       if (c & 0x80) {
                           usPixel = 0xffff;
                       } else {
                           usPixel = 0;
                       }
                       *pDest++ = usPixel;
                       c <<= 1;
                   }
                break;
            } // switch on bpp
            break;
        case PNG_PIXEL_TRUECOLOR:
            for (x=0; x<pDraw->iWidth; x++) {
                usPixel = (s[2] >> 3); // blue
                usPixel |= ((s[1] >> 2) << 5); // green
                usPixel |= ((s[0] >> 3) << 11); // red
                if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                    usPixel = __builtin_bswap16(usPixel);
                *pDest++ = usPixel;
                s += 3;
            }
            break;
        case PNG_PIXEL_INDEXED: // palette color (can be 1/2/4 or 8 bits per pixel)
            if (pDraw->pFastPalette && !pDraw->iHasAlpha) { // faster RGB565 palette exists
               switch (pDraw->iBpp) {
                   case 8:
                       for (x=0; x<pDraw->iWidth; x++) {
                           c = *s++;
                           usPixel = pDraw->pFastPalette[c];
                           if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                               usPixel = __builtin_bswap16(usPixel);
                           *pDest++ = usPixel;
                       }
                       break;
                   case 4:
                       for (x=0; x<pDraw->iWidth; x+=2) {
                           c = *s++;
                           usPixel = pDraw->pFastPalette[c >> 4];
                           if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                               usPixel = __builtin_bswap16(usPixel);
                           *pDest++ = usPixel;
                           usPixel = pDraw->pFastPalette[c & 0xf];
                           if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                               usPixel = __builtin_bswap16(usPixel);
                           *pDest++ = usPixel;
                       }
                       break;
                   case 2:
                       for (x=0; x<pDraw->iWidth; x+=4) {
                           c = *s++;
                           for (j=0; j<4; j++) { // work on pairs of bits
                               usPixel = pDraw->pFastPalette[c >> 6];
                               if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                                   usPixel = __builtin_bswap16(usPixel);
                               *pDest++ = usPixel;
                               c <<= 2;
                           }
                       }
                       break;
                   case 1:
                       for (x=0; x<pDraw->iWidth; x++) {
                           if ((x & 7) == 0) {
                               c = *s++;
                           }
                           usPixel = pDraw->pFastPalette[c >> 7];
                           if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                               usPixel = __builtin_bswap16(usPixel);
                           *pDest++ = usPixel;
                           c <<= 1;
                       }
                       break;
               } // switch on bpp 
               return;
            }
            switch (pDraw->iBpp) {
                case 8: // 8-bit palette also supports palette alpha
                    if (pDraw->iHasAlpha) { // use the alpha to modify the palette
                        if (u32Bkgd != 0xffffffff) { // user wants to blend it with a background color
                            uint32_t b_r, b_g, b_b;
                            b_r = u32Bkgd & 0xff; b_g = (u32Bkgd & 0xff00) >> 8;
                            b_b = (u32Bkgd >> 16) & 0xff;
                            uint16_t u16Clr = (u32Bkgd & 0xf8) << 8;
                            u16Clr |= ((u32Bkgd & 0xfc00) >> 5);
                            u16Clr |= ((u32Bkgd & 0xf80000) >> 19);
                            for (x=0; x<pDraw->iWidth; x++) {
                                int a;
                                c = *s++;
                                a = pDraw->pPalette[768+c]; // get alpha
                                if (a == 0) { // pure BGColor
                                    usPixel = u16Clr;
                                } else if (a == 255) { // opaque
                                    pPal = &pDraw->pPalette[c * 3];
                                    usPixel = ((pPal[2] * a) >> 11); // blue
                                    usPixel |= (((pPal[1] * a) >> 10) << 5); // green
                                    usPixel |= (((pPal[0] * a) >> 11) << 11); // red
                                } else { // need to mix the bg & fg colors

                                    pPal = &pDraw->pPalette[c * 3];
                                    usPixel = ((((pPal[2] * a) + (b_b * (255-a)))) >> 11); // blue 
                                    usPixel |= ((((pPal[1] * a) + (b_g * (255-a)))>> 10) << 5); // green                                
                                    usPixel |= ((((pPal[0] * a) + (b_r * (255-a)))>> 11) << 11); // red                                 
                                } // need to mix with alpha
                                if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                                    usPixel = __builtin_bswap16(usPixel);
                                *pDest++ = usPixel;
                            } // for x
                        } else { // alpha 0 = black
                            for (x=0; x<pDraw->iWidth; x++) {
                                int a;
                                c = *s++;
                                a = pDraw->pPalette[768+c]; // get alpha
                                pPal = &pDraw->pPalette[c * 3];
                                usPixel = ((pPal[2] * a) >> 11); // blue
                                usPixel |= (((pPal[1] * a) >> 10) << 5); // green
                                usPixel |= (((pPal[0] * a) >> 11) << 11); // red
                                if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                                    usPixel = __builtin_bswap16(usPixel);
                                *pDest++ = usPixel;
                            } // for x
                        }
                    } else {
                        for (x=0; x<pDraw->iWidth; x++) {
                            c = *s++;
                            pPal = &pDraw->pPalette[c * 3];
                            usPixel = (pPal[2] >> 3); // blue
                            usPixel |= ((pPal[1] >> 2) << 5); // green
                            usPixel |= ((pPal[0] >> 3) << 11); // red
                            if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                                usPixel = __builtin_bswap16(usPixel);
                            *pDest++ = usPixel;
                        } // for x
                    } // not alpha palette
                    break;
                case 4:
                    for (x=0; x<pDraw->iWidth; x+=2) {
                        c = *s++;
                        pPal = &pDraw->pPalette[(c >> 4) * 3];
                        usPixel = (pPal[2] >> 3); // blue
                        usPixel |= ((pPal[1] >> 2) << 5); // green
                        usPixel |= ((pPal[0] >> 3) << 11); // red
                        if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                            usPixel = __builtin_bswap16(usPixel);
                        *pDest++ = usPixel;
                        pPal = &pDraw->pPalette[(c & 0xf) * 3];
                        usPixel = (pPal[2] >> 3); // blue
                        usPixel |= ((pPal[1] >> 2) << 5); // green
                        usPixel |= ((pPal[0] >> 3) << 11); // red
                        if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                            usPixel = __builtin_bswap16(usPixel);
                        *pDest++ = usPixel;
                    }
                    break;
                case 2:
                    for (x=0; x<pDraw->iWidth; x+=4) {
                        c = *s++;
                        for (j=0; j<4; j++) { // work on pairs of bits
                            pPal = &pDraw->pPalette[(c >> 6) * 3];
                            usPixel = (pPal[2] >> 3); // blue
                            usPixel |= ((pPal[1] >> 2) << 5); // green
                            usPixel |= ((pPal[0] >> 3) << 11); // red
                            if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                                usPixel = __builtin_bswap16(usPixel);
                            *pDest++ = usPixel;
                            c <<= 2;
                        }
                    }
                    break;
                case 1:
                    for (x=0; x<pDraw->iWidth; x++) {
                        if ((x & 7) == 0) {
                            c = *s++;
                        }
                        pPal = &pDraw->pPalette[(c >> 7) * 3];
                        usPixel = (pPal[2] >> 3); // blue
                        usPixel |= ((pPal[1] >> 2) << 5); // green
                        usPixel |= ((pPal[0] >> 3) << 11); // red
                        if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                            usPixel = __builtin_bswap16(usPixel);
                        *pDest++ = usPixel;
                        c <<= 1;
                    }
                    break;
            } // switch on bits per pixel
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA: // truecolor + alpha
            if (u32Bkgd != 0xffffffff) { // user wants to blend it with a background color
                uint32_t r, g, b, a;
                uint32_t b_r, b_g, b_b;
                b_r = u32Bkgd & 0xff; b_g = (u32Bkgd & 0xff00) >> 8;
                b_b = (u32Bkgd >> 16) & 0xff;
                uint16_t u16Clr = (u32Bkgd & 0xf8) << 8;
                u16Clr |= ((u32Bkgd & 0xfc00) >> 5);
                u16Clr |= ((u32Bkgd & 0xf80000) >> 19);
                for (x=0; x<pDraw->iWidth; x++) {
                    r = s[0]; g = s[1]; b = s[2]; a = s[3];
                    if (a == 0)
                        usPixel = u16Clr;
                    else if (a == 255) { // fully opaque
                        usPixel = (s[2] >> 3); // blue
                        usPixel |= ((s[1] >> 2) << 5); // green
                        usPixel |= ((s[0] >> 3) << 11); // red
                    } else { // mix the colors
                        r = ((r * a) + (b_r * (255-a))) >> 8;
                        g = ((g * a) + (b_g * (255-a))) >> 8;
                        b = ((b * a) + (b_b * (255-a))) >> 8;
                        usPixel = (b >> 3); // blue
                        usPixel |= ((g >> 2) << 5); // green
                        usPixel |= ((r >> 3) << 11); // red
                    }
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                    s += 4; // skip alpha
                }
            } else { // ignore alpha
#ifdef ARDUINO_ESP32S3_DEV
                s3_rgb565(s, (uint8_t *)pDest, pDraw->iWidth, (iEndiannes == PNG_RGB565_BIG_ENDIAN));
#else
                for (x=0; x<pDraw->iWidth; x++) {
                    usPixel = (s[2] >> 3); // blue
                    usPixel |= ((s[1] >> 2) << 5); // green
                    usPixel |= ((s[0] >> 3) << 11); // red
                    if (iEndiannes == PNG_RGB565_BIG_ENDIAN)
                        usPixel = __builtin_bswap16(usPixel);
                    *pDest++ = usPixel;
                    s += 4; // skip alpha
                }
#endif
            }
            break;
    }
} /* PNGRGB565() */
//
// Helper functions for memory based images
//
PNG_STATIC int32_t seekMem(PNGFILE *pFile, int32_t iPosition)
{
    if (iPosition < 0) iPosition = 0;
    else if (iPosition >= pFile->iSize) iPosition = pFile->iSize-1;
    pFile->iPos = iPosition;
    return iPosition;
} /* seekMem() */

PNG_STATIC int32_t readFLASH(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;

    iBytesRead = iLen;
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos;
    if (iBytesRead <= 0)
       return 0;
    memcpy_P(pBuf, &pFile->pData[pFile->iPos], iBytesRead);
    pFile->iPos += iBytesRead;
    return iBytesRead;
} /* readFLASH() */

PNG_STATIC int32_t readRAM(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;

    iBytesRead = iLen;
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos;
    if (iBytesRead <= 0)
       return 0;
    memcpy(pBuf, &pFile->pData[pFile->iPos], iBytesRead);
    pFile->iPos += iBytesRead;
    return iBytesRead;
} /* readRAM() */
//
// Verify it's a PNG file and then parse the IHDR chunk
// to get basic image size/type/etc
//
PNG_STATIC int PNGParseInfo(PNGIMAGE *pPage)
{
    uint8_t *s = pPage->ucFileBuf;
    int iBytesRead;
    
    pPage->iHasAlpha = pPage->iInterlaced = 0;
    pPage->szComment[0] = 0;
    // Read a few bytes to just parse the size/pixel info
    iBytesRead = (*pPage->pfnRead)(&pPage->PNGFile, s, 33);
    if (iBytesRead < 33) { // a PNG file this tiny? probably bad
        pPage->iError = PNG_INVALID_FILE;
        return pPage->iError;
    }

    if (MOTOLONG(s) != (int32_t)0x89504e47) { // check that it's a PNG file
        pPage->iError = PNG_INVALID_FILE;
        return pPage->iError;
    }
    if (MOTOLONG(&s[12]) == 0x49484452/*'IHDR'*/) {
        int len = MOTOLONG(&s[8]);
        if (len != 13) {
            pPage->iError = PNG_INVALID_FILE;
            return pPage->iError;
        }
        int crc = (int)crc32(0, &s[12], len+4);
        int hdrcrc = MOTOLONG(&s[16+len]);
        if (len != 13 || crc != hdrcrc) {
            pPage->iError = PNG_INVALID_FILE;
            return pPage->iError;
        }
        pPage->iWidth = MOTOLONG(&s[16]);
        pPage->iHeight = MOTOLONG(&s[20]);
        if (pPage->iWidth <= 0 || pPage->iHeight <= 0 || pPage->iWidth > PNG_MAX_BUFFERED_PIXELS / 4) {  // Image size is outside of valid range
            pPage->iError = PNG_TOO_BIG;
            return pPage->iError;
        }
        pPage->ucBpp = s[24]; // bits per pixel
        pPage->ucPixelType = s[25]; // pixel type
        pPage->iInterlaced = s[28];
        if (pPage->iInterlaced || pPage->ucBpp > 8) { // 16-bit pixels are not supported (yet)
            pPage->iError = PNG_UNSUPPORTED_FEATURE;
            return pPage->iError;
        }
        if (pPage->ucPixelType != PNG_PIXEL_GRAYSCALE && pPage->ucPixelType != PNG_PIXEL_TRUECOLOR && pPage->ucPixelType != PNG_PIXEL_INDEXED && pPage->ucPixelType != PNG_PIXEL_GRAY_ALPHA && pPage->ucPixelType != PNG_PIXEL_TRUECOLOR_ALPHA) {
            pPage->iError = PNG_INVALID_FILE;
            return pPage->iError;
        }
        if (pPage->ucBpp == 0 || pPage->ucBpp == 3 || pPage->ucBpp == 5 || pPage->ucBpp == 6) {
            pPage->iError = PNG_INVALID_FILE;
            return pPage->iError;
        }
        // calculate the number of bytes per line of pixels
        switch (pPage->ucPixelType) {
            case PNG_PIXEL_GRAYSCALE: // grayscale
            case PNG_PIXEL_INDEXED: // indexed
                pPage->iPitch = (pPage->iWidth * pPage->ucBpp + 7)/8; // bytes per pixel
                break;
            case PNG_PIXEL_TRUECOLOR: // truecolor
                pPage->iPitch = ((3 * pPage->ucBpp) * pPage->iWidth + 7)/8;
                break;
            case PNG_PIXEL_GRAY_ALPHA: // grayscale + alpha
                pPage->iPitch = ((2 * pPage->ucBpp) * pPage->iWidth + 7)/8;
                pPage->iHasAlpha = 1;
                break;
            case PNG_PIXEL_TRUECOLOR_ALPHA: // truecolor + alpha
                pPage->iPitch = ((4 * pPage->ucBpp) * pPage->iWidth + 7)/8;
                pPage->iHasAlpha = 1;
        } // switch
    }
    if ((pPage->iPitch+1)*2 + 32 > PNG_MAX_BUFFERED_PIXELS)
       return PNG_TOO_BIG;

    return PNG_SUCCESS;
} /* PNGParseInfo() */
//
// De-filter the current line of pixels
//
PNG_STATIC void DeFilter(uint8_t *pCurr, uint8_t *pPrev, int iWidth, int iPitch)
{
    uint8_t ucFilter = *pCurr++;
    int x, iBpp;
    if (iPitch <= iWidth)
        iBpp = 1;
    else
        iBpp = iPitch / iWidth;
    
    pPrev++; // skip filter of previous line
    switch (ucFilter) { // switch on filter type
        case PNG_FILTER_NONE:
            // nothing to do :)
            break;
        case PNG_FILTER_SUB:
            for (x=iBpp; x<iPitch; x++) {
                pCurr[x] += pCurr[x-iBpp];
            }
            break;
        case PNG_FILTER_UP:
            for (x = 0; x < iPitch; x++) {
               pCurr[x] += pPrev[x];
            }
            break;
        case PNG_FILTER_AVG:
            for (x = 0; x < iBpp; x++) {
               pCurr[x] = (pCurr[x] +
                  pPrev[x] / 2 );
            }
            for (x = iBpp; x < iPitch; x++) {
               pCurr[x] = pCurr[x] +
                  (pPrev[x] + pCurr[x-iBpp]) / 2;
            }
            break;
        case PNG_FILTER_PAETH:
            if (iBpp == 1) {
                int a, c;
                uint8_t *pEnd = &pCurr[iPitch];
                // First pixel/byte
                c = *pPrev++;
                a = *pCurr + c;
                *pCurr++ = (uint8_t)a;
                while (pCurr < pEnd) {
                   int b, pa, pb, pc, p;
                   a &= 0xff; // From previous iteration
                   b = *pPrev++;
                   p = b - c;
                   pc = a - c;
                   // assume no native ABS() instruction
                   pa = p < 0 ? -p : p;
                   pb = pc < 0 ? -pc : pc;
                   pc = (p + pc) < 0 ? -(p + pc) : p + pc;
                   // choose the best predictor
                   if (pb < pa) {
                      pa = pb; a = b;
                   }
                   if (pc < pa) a = c;
                   // Calculate current pixel
                   c = b;
                   a += *pCurr;
                   *pCurr++ = (uint8_t)a;
                }
            } else { // multi-byte
                uint8_t *pEnd = &pCurr[iBpp];
                // first pixel is treated the same as 'up'
                while (pCurr < pEnd) {
                   int a = *pCurr + *pPrev++;
                   *pCurr++ = (uint8_t)a;
                }
                pEnd = pEnd + (iPitch - iBpp);
                while (pCurr < pEnd) {
                   int a, b, c, pa, pb, pc, p;
                   c = pPrev[-iBpp];
                   a = pCurr[-iBpp];
                   b = *pPrev++;
                   p = b - c;
                   pc = a - c;
                    // assume no native ABS() instruction
                   pa = p < 0 ? -p : p;
                   pb = pc < 0 ? -pc : pc;
                   pc = (p + pc) < 0 ? -(p + pc) : p + pc;
                   if (pb < pa) {
                      pa = pb; a = b;
                   }
                   if (pc < pa) a = c;
                   a += *pCurr;
                   *pCurr++ = (uint8_t)a;
                }
            } // multi-byte
            break;
    } // switch on filter type
} /* DeFilter() */
//
// PNGInit
// Parse the PNG file header and confirm that it's a valid file
//
// returns 0 for success, 1 for failure
//
PNG_STATIC int PNGInit(PNGIMAGE *pPNG)
{
    return PNGParseInfo(pPNG); // gather info for image
} /* PNGInit() */
//
// Decode the PNG file
//
// You must call open() before calling decode()
// This function can be called repeatedly without having
// to close and re-open the file
//
PNG_STATIC int DecodePNG(PNGIMAGE *pPage, void *pUser, int iOptions)
{
    int err, y, iLen=0;
    int bDone, iOffset, iFileOffset, iBytesRead, iAvailable;
    int iMarker=0;
    uint8_t *tmp, *pCurr, *pPrev;
    z_stream d_stream; /* decompression stream */
    uint8_t *s = pPage->ucFileBuf;
    struct inflate_state *state;
    
    // Either the image buffer must be allocated or a draw callback must be set before entering
    if (pPage->pImage == NULL && pPage->pfnDraw == NULL) {
        pPage->iError = PNG_NO_BUFFER;
        return 0;
    }
    // Use internal buffer to maintain the current and previous lines
    y = (int)(intptr_t)&pPage->ucPixels[0];
    y &= 15; // make sure we're 16-byte aligned, -1 for filter byte
    y += (15 - y);
    pCurr = &pPage->ucPixels[y]; // so that the pixels are 16-byte aligned
    y += pPage->iPitch + 1; // both lines are 16-byte (minus 1)
    y += (15 - (y & 15));
    pPrev = &pPage->ucPixels[y];
    pPage->iError = PNG_SUCCESS;
    // Start decoding the image
    bDone = FALSE;
    // Inflate the compressed image data
    // The allocation functions are disabled and zlib has been modified
    // to not use malloc/free and instead the buffer is part of the PNG class
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;
    // Insert the memory pointer here to avoid having to use malloc() inside zlib
    state = (struct inflate_state FAR *)pPage->ucZLIB;
    d_stream.state = (struct internal_state FAR *)state;
    state->window = &pPage->ucZLIB[sizeof(struct inflate_state)]; // point to 32k dictionary buffer
    err = inflateInit(&d_stream);
#ifdef FUTURE
//    if (inpage->cCompression == PIL_COMP_IPHONE_FLATE)
//        err = mz_inflateInit2(&d_stream, -15); // undocumented option which ignores header and crcs
//    else
//        err = mz_inflateInit2(&d_stream, 15);
#endif // FUTURE
    
    iFileOffset = 8; // skip PNG file signature
    iOffset = 0; // internal buffer offset starts at 0
    iBytesRead = 0; // We haven't read yet anything
    y = 0;
    d_stream.avail_out = 0;
    d_stream.next_out = pPage->pImage;

    // continue until fully decoded
    // parse the markers until the next data block
    while (!bDone && y < pPage->iHeight)
    {
        if (iOffset > iBytesRead-8) { // need to read more data
            iFileOffset += (iOffset - iBytesRead);
            (*pPage->pfnSeek)(&pPage->PNGFile, iFileOffset);
            iBytesRead = (*pPage->pfnRead)(&pPage->PNGFile, s, PNG_FILE_BUF_SIZE);
            if (iBytesRead < 8) {
                pPage->iError = PNG_DECODE_ERROR;
                return 1;
            }
            iFileOffset += iBytesRead;
            iOffset = 0;
        }

        iLen = MOTOLONG(&s[iOffset]); // chunk length
        if (iLen < 0 || iLen + (iFileOffset - iBytesRead) > pPage->PNGFile.iSize) // invalid data
        {
            pPage->iError = PNG_DECODE_ERROR;
            return 1;
        }
        iMarker = MOTOLONG(&s[iOffset+4]);
        iOffset += 8; // point to the marker data
        iAvailable = iBytesRead - iOffset;
        switch (iMarker)
        {
            case 0x44474b62: // 'bKGD' DEBUG
                break;
            case 0x67414d41: //'gAMA'
                break;
#ifdef FUTURE
            case 0x6663544C: //'fcTL' frame control block for animated PNG (need to get size of this partial image)
                pPage->iWidth = MOTOLONG(&pPage->pData[iOffset + 4]); // frame width
                pPage->iHeight = MOTOLONG(&pPage->pData[iOffset + 8]); // frame height
                bDone = TRUE;
                break;
#endif
            case 0x504c5445: //'PLTE' palette colors
                memset(&pPage->ucPalette[768], 0xff, 256); // assume all colors are opaque unless specified
                if (iLen > 0 && iLen <= 768 && iLen <= iAvailable) {
                    memcpy(pPage->ucPalette, &s[iOffset], iLen);
                }
                if (iOptions & PNG_FAST_PALETTE) { // create a RGB565 palette
                    int i, iColors = 1 << pPage->ucBpp;
                    uint16_t usPixel, *d;
                    uint8_t *s = pPage->ucPalette;
                    d = (uint16_t *)&pPage->ucPixels[sizeof(pPage->ucPixels)-512];
                    for (i=0; i<iColors; i++) {
                    usPixel = (s[2] >> 3); // blue
                    usPixel |= ((s[1] >> 2) << 5); // green
                    usPixel |= ((s[0] >> 3) << 11); // red
                    *d++ = usPixel;
                    s += 3;
                    }
                }
                break;
            case 0x74524e53: //'tRNS' transparency info
                if (pPage->ucPixelType == PNG_PIXEL_INDEXED && iLen > 0 && iLen <= 256 && iLen <= iAvailable) // if palette exists
                {
                    memcpy(&pPage->ucPalette[768], &s[iOffset], iLen);
                    pPage->iHasAlpha = 1;
                }
                else if (iLen == 2) // for grayscale images
                {
                    pPage->iTransparent = s[iOffset + 1]; // lower part of 2-byte value is transparent color index
                    pPage->iHasAlpha = 1;
                }
                else if (iLen == 6) // transparent color for 24-bpp image
                {
                    pPage->iTransparent = s[iOffset + 5]; // lower part of 2-byte value is transparent color value
                    pPage->iTransparent |= (s[iOffset + 3] << 8);
                    pPage->iTransparent |= (s[iOffset + 1] << 16);
                    pPage->iHasAlpha = 1;
                }
                break;
            case 0x49444154: //'IDAT' image data block
                while (iLen) {
                    if (iOffset >= iBytesRead) {
                        // we ran out of data; get some more
                        iBytesRead = (*pPage->pfnRead)(&pPage->PNGFile, pPage->ucFileBuf, (iLen > PNG_FILE_BUF_SIZE) ? PNG_FILE_BUF_SIZE : iLen);
                        iFileOffset += iBytesRead;
                        iOffset = 0;
                    } else {
                        // number of bytes remaining in buffer
                        iBytesRead -= iOffset;
                    }
                    if (iBytesRead > iLen) { // we read too much
                        d_stream.next_in  = &pPage->ucFileBuf[iOffset];
                        d_stream.avail_in = iLen;
                        iOffset += iLen; // point to start of next marker
                        iBytesRead -= iLen; // keep remaining byte count
                        iLen = 0; // every byte will be decoded
                    } else {
                        d_stream.next_in  = &pPage->ucFileBuf[iOffset];
                        d_stream.avail_in = iBytesRead;
                        iLen -= iBytesRead;
                        iOffset += iBytesRead;
                        iBytesRead = 0;
                    }
            //        if (iMarker == 0x66644154) // data starts at offset 4 in APNG frame data block
            //        {
            //            d_stream.next_in += 4;
            //            d_stream.avail_in -= 4;
            //        }
                    err = 0;
                    while (err == Z_OK) {
                        if (d_stream.avail_out == 0) { // reset for next line
                            d_stream.avail_out = pPage->iPitch+1;
                            d_stream.next_out = pCurr;
                        } // otherwise it could be a continuation of an unfinished line
                        err = inflate(&d_stream, Z_NO_FLUSH, iOptions & PNG_CHECK_CRC);
                        if ((err == Z_OK || err == Z_STREAM_END) && d_stream.avail_out == 0) {// successfully decoded line
                            DeFilter(pCurr, pPrev, pPage->iWidth, pPage->iPitch);
                            if (pPage->pImage == NULL) { // no image buffer, send it line by line
                                PNGDRAW pngd;
                                pngd.pUser = pUser;
                                pngd.iPitch = pPage->iPitch;
                                pngd.iWidth = pPage->iWidth;
                                pngd.pPalette = pPage->ucPalette;
                                pngd.pFastPalette = (iOptions & PNG_FAST_PALETTE) ? (uint16_t *)&pPage->ucPixels[sizeof(pPage->ucPixels)-512] : NULL;
                                pngd.pPixels = pCurr+1;
                                pngd.iPixelType = pPage->ucPixelType;
                                pngd.iHasAlpha = pPage->iHasAlpha;
                                pngd.iBpp = pPage->ucBpp;
                                pngd.y = y;
                                if (!(*pPage->pfnDraw)(&pngd)) {
                                    pPage->iError = PNG_QUIT_EARLY;
                                    bDone = TRUE;
                                }
                            } else {
                                // copy to destination bitmap
                                memcpy(&pPage->pImage[y * pPage->iPitch], &pCurr[1], pPage->iPitch);
                            }
                            y++;
                        // swap current and previous lines
                        tmp = pCurr; pCurr = pPrev; pPrev = tmp;
                        } else { // some error
                            tmp = NULL;
                        }
                    }
                    if (err == Z_STREAM_END && d_stream.avail_out == 0) {
                        // successful decode, stop here
                        y = pPage->iHeight;
                        bDone = TRUE;
                    } else  if (err == Z_DATA_ERROR || err == Z_STREAM_ERROR) {
                        iLen = 0; // quit now
                        y = pPage->iHeight;
                        pPage->iError = PNG_DECODE_ERROR;
                        bDone = TRUE; // force loop to exit with error
                    } else if (err == Z_BUF_ERROR) {
                        y |= 0; // need more data
                    }
                } // while (iLen)
                if (iBytesRead) { // data remaining in buffer
                    // Restore iBytesRead to be total ucFileBuf bytes, not remainder
                    iBytesRead += iOffset;
                } else {
                    // We consumed everything, so move the pointer to the zero.
                    // We haven't read CRC for the block, but it doesn't matter as
                    // pfnSeek will move past it to the next chunk.
                    iOffset = 0;
                }
                break;
                //               case 0x69545874: //'iTXt'
                //               case 0x7a545874: //'zTXt'
            case 0x74455874: //'tEXt'
            {
                int i;
                char szTemp[64];
                char *pDest = NULL;
                memcpy(szTemp, &s[iOffset], 64); // get the label length (Title, Author, Description, Copyright, Creation Time, Software, Disclaimer, Warning, Source, Comment)
                i = (int)strlen(szTemp) + 1; // start of actual text
                if (strcmp(szTemp, "Comment") == 0 || strcmp(szTemp, "Description") == 0) pDest = &pPage->szComment[0];
                //else if (strcmp(szTemp, "Software") == 0) pDest = &pPage->szSoftware[0];
                //else if (strcmp(szTemp, "Author") == 0) pDest = &pPage->szArtist[0];
                if (pDest != NULL && iLen > i)
                {
                    if ((iLen - i) < PNG_COMMENT_SIZE)
                    {
                        memcpy(pPage->szComment, &s[iOffset + i], iLen - i);
                        pPage->szComment[iLen - i + 1] = 0;
                    }
                    else
                    {
                        memcpy(pPage->szComment, &s[iOffset + i], PNG_COMMENT_SIZE-1);
                        pPage->szComment[PNG_COMMENT_SIZE-1] = '\0';
                    }
                }
            }
            break;
        } // switch
        iOffset += (iLen + 4); // skip data + CRC
    } // while y < height
    err = inflateEnd(&d_stream);
    return pPage->iError;
} /* DecodePNG() */
//
// Post-decode display enhancement pipeline
// ----------------------------------------
// Three stages are applied to each decoded RGB565 line in a single streaming
// pass (no second traversal of the whole image):
//   1. gamma correction      - 256-entry LUT (integer only, no libm)
//   2. contrast stretch      - 256-entry LUT built from low/high clip points
//   3. mild sharpening       - 3x3 unsharp mask using two line buffers
//
// The pipeline is opt-in: existing decode/getLineAsRGB565 behaviour is not
// changed. Call PNGPostInit() once, then PNGPostLineRGB565() from the draw
// callback for every line, in increasing y order. Input and output are
// logical RGB565 words (RRRRRGGGGGGBBBBB), i.e. what getLineAsRGB565()
// produces with PNG_RGB565_LITTLE_ENDIAN.
//
// All scratch memory is supplied by the caller (the library still performs
// no malloc/free). Use PNGPostBufferSize() to size it.
//
// Consumer usage (declarations live at the bottom of this file; including
// PNGdec.cpp, as the repository's own tests do, makes them visible):
//   static PNGPOST post;
//   uint8_t linebuf[?];  // PNGPostBufferSize(width) bytes
//   PNGPostInit(&post, width, gammaQ8, lo, hi, sharpenQ8, linebuf, sizeof(linebuf));
//   PNGPostLineRGB565(&post, rgb565Line, outLine, width, pDraw->y);
//
#define PNGPOST_GAMMA_MIN      64   // 0.25 in Q8
#define PNGPOST_GAMMA_MAX    1024   // 4.00 in Q8
#define PNGPOST_SHARPEN_MAX   128   // 0.50 in Q8 (mild)
#define PNGPOST_CONTRAST_SPAN   8   // minimum high-low distance
#define PNGPOST_STAGE_GAMMA      1
#define PNGPOST_STAGE_CONTRAST   2
#define PNGPOST_STAGE_SHARPEN    4

// The pipeline API has external linkage so that a consumer translation unit
// can declare and call it without this header-style file being included
// twice (this file already reaches the build through PNGdec.cpp). See the
// block comment above for the calling convention.

// log2(x) in Q16 for x >= 1, fractional part interpolated from 33 entries
static const uint32_t usPNGPostLogTab[33] = {
       0,  2909,  5732,  8473, 11136, 13727, 16248, 18704,
   21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346,
   38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207,
   52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047,
   65536};

// 2^(-x) in Q16 for x in [0,1], same interpolation
static const uint32_t usPNGPostExpTab[33] = {
   65536, 64132, 62757, 61413, 60097, 58809, 57549, 56316,
   55109, 53928, 52773, 51642, 50535, 49452, 48393, 47356,
   46341, 45348, 44376, 43425, 42495, 41584, 40693, 39821,
   38968, 38133, 37316, 36516, 35734, 34968, 34219, 33486,
   32768};

//
// State for the post-processing pipeline. Kept in caller-provided memory so
// that this file introduces no static RAM usage and no allocator calls.
//
typedef struct png_post_tag
{
    int iWidth;             // pixels per line
    int iGammaQ8;           // gamma * 256
    int iContrastLow;       // black clip point (0..254)
    int iContrastHigh;      // white clip point (1..255), > low + 7
    int iSharpenQ8;         // amount * 256 (0..128)
    int iStageMask;         // PNGPOST_STAGE_* bits, default all three
    uint8_t ucGammaLUT[256];    // stage 1: 8-bit in -> 8-bit out
    uint8_t ucContrastLUT[256]; // stage 2: 8-bit in -> 8-bit out
    uint8_t *pPrevRGB;      // width*3 bytes, stage-2 output of row y-1
    uint8_t *pCurrRGB;      // width*3 bytes, stage-2 output of row y
    int iHavePrev;          // pPrevRGB holds the row before iLastY
    int iLastY;             // last row passed to the line function
    int iTraceY;            // row to capture for diagnostics (-1 = none)
    int iTraceX[3];         // three x positions to capture (-1 = unused)
    uint8_t ucTrace[3][3][3]; // [pixel][stage 0..2][R,G,B]
} PNGPOST;

//
// Return the number of bytes the caller must provide for the line buffers,
// or 0 for an invalid width.
//
int PNGPostBufferSize(int iWidth)
{
    if (iWidth <= 0 || iWidth > PNG_MAX_BUFFERED_PIXELS / 4)
        return 0;
    return (iWidth * 3 * 2) + 16; // two RGB888 line buffers, aligned
} /* PNGPostBufferSize() */

//
// Integer log2 in Q16 (no floating point)
//
uint32_t PNGPostLog2Q16(uint32_t u32Val)
{
    int iMSB = 0;
    uint32_t u32M, u32Frac, u32Base;
    uint32_t u32Temp = u32Val;

    while (u32Temp > 1) {
        u32Temp >>= 1;
        iMSB++;
    }
    if (iMSB <= 16)
        u32M = u32Val << (16 - iMSB);
    else
        u32M = u32Val >> (iMSB - 16);
    u32Frac = u32M - 65536; // 0..65535
    u32Base = usPNGPostLogTab[u32Frac >> 11];
    u32Base += (((int32_t)(usPNGPostLogTab[(u32Frac >> 11) + 1] - usPNGPostLogTab[u32Frac >> 11])
               * (int32_t)(u32Frac & 0x7ff)) >> 11);
    return ((uint32_t)iMSB << 16) + u32Base;
} /* PNGPostLog2Q16() */

//
// Return 255 * 2^e, e <= 0 (Q16 exponent), rounded and clamped to 0..255
//
int PNGPostExp255(int32_t iExp)
{
    uint32_t u32Ne, u32Frac, u32U;
    int iN, iIdx;

    u32Ne = (uint32_t)(-iExp);
    iN = (int)(u32Ne >> 16);
    if (iN > 8) // result would round to zero
        return 0;
    u32Frac = u32Ne & 0xffff;
    iIdx = (int)(u32Frac >> 11);
    u32U = usPNGPostExpTab[iIdx];
    u32U += (((int32_t)(usPNGPostExpTab[iIdx + 1] - usPNGPostExpTab[iIdx])
            * (int32_t)(u32Frac & 0x7ff)) >> 11);
    u32U = (255u * u32U) >> iN; // Q16 value scaled by 2^-n
    return (int)((u32U + 32768) >> 16);
} /* PNGPostExp255() */

//
// Configure the pipeline. Returns PNG_SUCCESS or PNG_INVALID_PARAMETER when
// any argument is out of range (or a required buffer is too small).
//
int PNGPostInit(PNGPOST *pPost, int iWidth, int iGammaQ8,
                           int iContrastLow, int iContrastHigh, int iSharpenQ8,
                           uint8_t *pBuffer, int iBufferSize)
{
    int i, iSpan;
    int32_t iLog255;

    if (pPost == NULL || pBuffer == NULL)
        return PNG_INVALID_PARAMETER;
    if (iWidth <= 0 || iWidth > PNG_MAX_BUFFERED_PIXELS / 4)
        return PNG_INVALID_PARAMETER;
    if (iGammaQ8 < PNGPOST_GAMMA_MIN || iGammaQ8 > PNGPOST_GAMMA_MAX)
        return PNG_INVALID_PARAMETER;
    if (iContrastLow < 0 || iContrastLow > 254)
        return PNG_INVALID_PARAMETER;
    if (iContrastHigh < 1 || iContrastHigh > 255)
        return PNG_INVALID_PARAMETER;
    if (iContrastHigh - iContrastLow < PNGPOST_CONTRAST_SPAN)
        return PNG_INVALID_PARAMETER;
    if (iSharpenQ8 < 0 || iSharpenQ8 > PNGPOST_SHARPEN_MAX)
        return PNG_INVALID_PARAMETER;
    if (iBufferSize < PNGPostBufferSize(iWidth))
        return PNG_INVALID_PARAMETER;

    memset(pPost, 0, sizeof(PNGPOST));
    pPost->iWidth = iWidth;
    pPost->iGammaQ8 = iGammaQ8;
    pPost->iContrastLow = iContrastLow;
    pPost->iContrastHigh = iContrastHigh;
    pPost->iSharpenQ8 = iSharpenQ8;
    pPost->iStageMask = PNGPOST_STAGE_GAMMA | PNGPOST_STAGE_CONTRAST | PNGPOST_STAGE_SHARPEN;
    pPost->pPrevRGB = pBuffer;
    pPost->pCurrRGB = pBuffer + (iWidth * 3);
    pPost->iLastY = -1;
    pPost->iTraceY = -1;
    pPost->iTraceX[0] = pPost->iTraceX[1] = pPost->iTraceX[2] = -1;

    // Stage 1: out = 255 * (in/255)^(gamma)
    iLog255 = (int32_t)PNGPostLog2Q16(255);
    pPost->ucGammaLUT[0] = 0;
    pPost->ucGammaLUT[255] = 255;
    for (i = 1; i < 255; i++) {
        int32_t iExp = (int32_t)(((int64_t)((int32_t)PNGPostLog2Q16((uint32_t)i) - iLog255) * iGammaQ8) >> 8);
        int iVal = PNGPostExp255(iExp);
        if (iVal < pPost->ucGammaLUT[i - 1]) // keep the curve monotonic
            iVal = pPost->ucGammaLUT[i - 1];
        pPost->ucGammaLUT[i] = (uint8_t)iVal;
    }

    // Stage 2: linear stretch/clip, out = (in-low)*255/(high-low)
    iSpan = iContrastHigh - iContrastLow;
    for (i = 0; i < 256; i++) {
        int iVal = ((i - iContrastLow) * 255 + (iSpan / 2)) / iSpan;
        if (iVal < 0) iVal = 0;
        else if (iVal > 255) iVal = 255;
        pPost->ucContrastLUT[i] = (uint8_t)iVal;
    }
    return PNG_SUCCESS;
} /* PNGPostInit() */

//
// Forget the previous line (call before starting a new image)
//
void PNGPostReset(PNGPOST *pPost)
{
    if (pPost == NULL)
        return;
    pPost->iHavePrev = 0;
    pPost->iLastY = -1;
} /* PNGPostReset() */

//
// Ask the pipeline to record the per-stage RGB output of three pixels on row
// iY. Out-of-range positions are rejected.
//
int PNGPostSetTrace(PNGPOST *pPost, int iY, int iX0, int iX1, int iX2)
{
    if (pPost == NULL || iY < 0)
        return PNG_INVALID_PARAMETER;
    if (iX0 < 0 || iX0 >= pPost->iWidth || iX1 < 0 || iX1 >= pPost->iWidth ||
        iX2 < 0 || iX2 >= pPost->iWidth)
        return PNG_INVALID_PARAMETER;
    pPost->iTraceY = iY;
    pPost->iTraceX[0] = iX0;
    pPost->iTraceX[1] = iX1;
    pPost->iTraceX[2] = iX2;
    memset(pPost->ucTrace, 0, sizeof(pPost->ucTrace));
    return PNG_SUCCESS;
} /* PNGPostSetTrace() */

//
// Select which stages run (default: all). Returns PNG_INVALID_PARAMETER for
// any bit outside 0..7. Disabling a stage passes pixels through unchanged.
//
int PNGPostSetStageMask(PNGPOST *pPost, int iMask)
{
    if (pPost == NULL || iMask < 0 || iMask > 7)
        return PNG_INVALID_PARAMETER;
    pPost->iStageMask = iMask;
    return PNG_SUCCESS;
} /* PNGPostSetStageMask() */

//
// Read back one traced pixel: iIndex 0..2, iStage 0=gamma, 1=contrast,
// 2=sharpen. pRGB receives three 8-bit channel values.
//
int PNGPostGetTrace(PNGPOST *pPost, int iIndex, int iStage, uint8_t *pRGB)
{
    if (pPost == NULL || pRGB == NULL || iIndex < 0 || iIndex > 2 ||
        iStage < 0 || iStage > 2)
        return PNG_INVALID_PARAMETER;
    if (pPost->iTraceX[iIndex] < 0)
        return PNG_INVALID_PARAMETER;
    pRGB[0] = pPost->ucTrace[iIndex][iStage][0];
    pRGB[1] = pPost->ucTrace[iIndex][iStage][1];
    pRGB[2] = pPost->ucTrace[iIndex][iStage][2];
    return PNG_SUCCESS;
} /* PNGPostGetTrace() */

//
// Process one RGB565 line through gamma -> contrast -> sharpen.
// pStage1/pStage2/pStage3 may be NULL; when supplied they receive the
// intermediate RGB565 results (pStage3 is required). Each line is walked
// exactly once: the LUT stages and the sharpener are fused into one loop
// that keeps a two-pixel stage-2 lookahead in registers.
// Returns PNG_SUCCESS or PNG_INVALID_PARAMETER.
//
// The sharpener is a separable 3x3 unsharp mask. The vertical blur reuses the
// previous line's stage-2 pixels (stored in pPrevRGB) instead of buffering a
// future line. On the first row the vertical tap degenerates to the current
// row, and at the left/right edge the horizontal tap is clamped, which keeps
// the constant border extension from darkening the frame.
//
int PNGPostLineRGB565Stages(PNGPOST *pPost, const uint16_t *pSrc,
        uint16_t *pStage1, uint16_t *pStage2, uint16_t *pStage3,
        int iWidth, int iY)
{
    int x, c, t;
    uint8_t *pPrev, *pCurr, *pTop;
    int bUsePrev, bTrace, bGamma, bContrast, bSharpen;
    int v0[3], v1[3], v2[3]; // stage-2 RGB of pixels x-1, x, x+1
    int iComputed;

    if (pPost == NULL || pSrc == NULL || pStage3 == NULL)
        return PNG_INVALID_PARAMETER;
    if (iWidth != pPost->iWidth || iY < 0)
        return PNG_INVALID_PARAMETER;

    pPrev = pPost->pPrevRGB;
    pCurr = pPost->pCurrRGB;
    // Only use the stored line if it really is the row immediately above
    bUsePrev = (pPost->iHavePrev && iY == pPost->iLastY + 1);
    pTop = bUsePrev ? pPrev : pCurr;
    bTrace = (iY == pPost->iTraceY);
    bGamma = (pPost->iStageMask & PNGPOST_STAGE_GAMMA) != 0;
    bContrast = (pPost->iStageMask & PNGPOST_STAGE_CONTRAST) != 0;
    bSharpen = (pPost->iStageMask & PNGPOST_STAGE_SHARPEN) != 0;

    // Run the two LUT stages for pixel x and record its stage-2 RGB
    #define PNGPOST_STAGE2(pX, pRGB) do { \
        uint16_t usPix_ = pSrc[pX]; \
        int r5_ = (usPix_ >> 11) & 0x1f; \
        int g6_ = (usPix_ >> 5) & 0x3f; \
        int b5_ = usPix_ & 0x1f; \
        int r8_ = (r5_ << 3) | (r5_ >> 2); /* 5 -> 8 bits */ \
        int g8_ = (g6_ << 2) | (g6_ >> 4); /* 6 -> 8 bits */ \
        int b8_ = (b5_ << 3) | (b5_ >> 2); \
        int r1_ = (bGamma) ? (int)pPost->ucGammaLUT[r8_] : r8_; \
        int g1_ = (bGamma) ? (int)pPost->ucGammaLUT[g8_] : g8_; \
        int b1_ = (bGamma) ? (int)pPost->ucGammaLUT[b8_] : b8_; \
        int r2_ = (bContrast) ? (int)pPost->ucContrastLUT[r1_] : r1_; \
        int g2_ = (bContrast) ? (int)pPost->ucContrastLUT[g1_] : g1_; \
        int b2_ = (bContrast) ? (int)pPost->ucContrastLUT[b1_] : b1_; \
        pCurr[(pX)*3+0] = (uint8_t)r2_; \
        pCurr[(pX)*3+1] = (uint8_t)g2_; \
        pCurr[(pX)*3+2] = (uint8_t)b2_; \
        if (pStage1 != NULL) pStage1[pX] = (uint16_t)(((r1_ & 0xf8) << 8) | ((g1_ & 0xfc) << 3) | (b1_ >> 3)); \
        if (pStage2 != NULL) pStage2[pX] = (uint16_t)(((r2_ & 0xf8) << 8) | ((g2_ & 0xfc) << 3) | (b2_ >> 3)); \
        if (bTrace) { \
            for (t = 0; t < 3; t++) { \
                if ((pX) == pPost->iTraceX[t]) { \
                    pPost->ucTrace[t][0][0] = (uint8_t)r1_; \
                    pPost->ucTrace[t][0][1] = (uint8_t)g1_; \
                    pPost->ucTrace[t][0][2] = (uint8_t)b1_; \
                    pPost->ucTrace[t][1][0] = (uint8_t)r2_; \
                    pPost->ucTrace[t][1][1] = (uint8_t)g2_; \
                    pPost->ucTrace[t][1][2] = (uint8_t)b2_; \
                } \
            } \
        } \
        (pRGB)[0] = r2_; (pRGB)[1] = g2_; (pRGB)[2] = b2_; \
    } while (0)

    // Prime the window: pixel 0 serves as its own left neighbour (edge clamp)
    PNGPOST_STAGE2(0, v1);
    v0[0] = v1[0]; v0[1] = v1[1]; v0[2] = v1[2];
    iComputed = 0;
    if (iWidth > 1) {
        PNGPOST_STAGE2(1, v2);
        iComputed = 1;
    } else {
        v2[0] = v1[0]; v2[1] = v1[1]; v2[2] = v1[2];
    }

    for (x = 0; x < iWidth; x++) {
        int xm = (x > 0) ? (x - 1) : 0;
        int xp = (x + 1 < iWidth) ? (x + 1) : (iWidth - 1);
        int out[3];

        if (bSharpen) {
            for (c = 0; c < 3; c++) {
                int topM = pTop[xm*3+c], top0 = pTop[x*3+c];
                int topP = pTop[xp*3+c];
                int vm = (topM + 3 * v0[c] + 2) >> 2;
                int v0c = (top0 + 3 * v1[c] + 2) >> 2;
                int vp = (topP + 3 * v2[c] + 2) >> 2;
                int iBlur = (vm + 2 * v0c + vp + 2) >> 2;
                int iVal = v1[c] + (((v1[c] - iBlur) * pPost->iSharpenQ8 + 128) >> 8);
                if (iVal < 0) iVal = 0;
                else if (iVal > 255) iVal = 255;
                out[c] = iVal;
            }
        } else {
            out[0] = v1[0]; out[1] = v1[1]; out[2] = v1[2];
        }
        pStage3[x] = (uint16_t)(((out[0] & 0xf8) << 8) | ((out[1] & 0xfc) << 3) | (out[2] >> 3));

        if (bTrace) {
            for (t = 0; t < 3; t++) {
                if (x == pPost->iTraceX[t]) {
                    pPost->ucTrace[t][2][0] = (uint8_t)out[0];
                    pPost->ucTrace[t][2][1] = (uint8_t)out[1];
                    pPost->ucTrace[t][2][2] = (uint8_t)out[2];
                }
            }
        }
        // Slide the window one pixel to the right
        v0[0] = v1[0]; v0[1] = v1[1]; v0[2] = v1[2];
        v1[0] = v2[0]; v1[1] = v2[1]; v1[2] = v2[2];
        if (x + 2 <= iWidth - 1 && iComputed < x + 2) {
            PNGPOST_STAGE2(x + 2, v2);
            iComputed = x + 2;
        } else {
            v2[0] = v1[0]; v2[1] = v1[1]; v2[2] = v1[2]; // edge clamp
        }
    } // for x
    #undef PNGPOST_STAGE2

    // The current stage-2 line becomes the previous line for the next call
    pPost->pPrevRGB = pCurr;
    pPost->pCurrRGB = pPrev;
    pPost->iHavePrev = 1;
    pPost->iLastY = iY;
    return PNG_SUCCESS;
} /* PNGPostLineRGB565Stages() */

//
// Convenience wrapper for the normal display path (final result only)
//
int PNGPostLineRGB565(PNGPOST *pPost, const uint16_t *pSrc,
                                 uint16_t *pDst, int iWidth, int iY)
{
    return PNGPostLineRGB565Stages(pPost, pSrc, NULL, NULL, pDst, iWidth, iY);
} /* PNGPostLineRGB565() */

