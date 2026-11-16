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
// ---------------------------------------------------------------------------
// Pull-mode (caller-paced) decoding
// ---------------------------------------------------------------------------
// The classic DecodePNG() path above pushes through the whole file using the
// read/seek callbacks stored in PNGIMAGE. This second entry point turns the
// flow around: the decoder asks the caller for the next block whenever it
// needs bytes. The caller may hand back a short block, or 0 bytes meaning
// "nothing right now"; in that case PNGPullDecode() returns PNG_PULL_PAUSED
// with all of its state saved and can be called again later to resume at
// exactly the same point.
//
// The implementation is a resumable state machine over the PNG chunk stream.
// It feeds zlib, de-filters and delivers lines in exactly the same order as
// DecodePNG(), so the one-shot path and its pixel output are untouched. This
// block only adds new symbols; no existing declaration or signature changes.
//
// Typical use:
//   static PNGIMAGE img;
//   static PNGPULLSTATE st;
//   PNGPullInit(&img, &st, PullCB, pSource, DrawCB, 64, 4);
//   img.pImage = pFrameBuffer;         // or leave NULL and use DrawCB
//   do {
//       rc = PNGPullDecode(&img, &st, pDrawUser, 0);
//   } while (rc == PNG_PULL_PAUSED);
//
// Pull callback contract (PNG_PULL_CALLBACK):
//   > 0 : number of bytes copied to pBuffer, never more than iMaxLen
//         (returning fewer bytes than asked is fine)
//   == 0: no data available at the moment; the decoder pauses
//   < 0 : end of input / source failure; decode fails with PNG_DECODE_ERROR
//
// PNGPullDecode() returns:
//   PNG_SUCCESS           image complete
//   PNG_PULL_PAUSED       source returned 0 bytes; call again to resume
//   PNG_DECODE_ERROR      truncated or corrupt stream (e.g. source ended early)
//   PNG_QUIT_EARLY        the draw callback returned 0
//   PNG_NO_BUFFER         neither an image buffer nor a draw callback was set
//   PNG_PULL_PAUSE_LIMIT  more than iMaxPauses consecutive empty replies
//   PNG_PULL_BAD_BLOCK    the callback returned more bytes than requested
//   PNG_PULL_INVALID_STATE the API was used out of order (or state is invalid)
//
// iBlockSize is the largest block requested per callback (1..PNG_FILE_BUF_SIZE).
// iMaxPauses is the maximum number of consecutive 0-byte replies tolerated;
// 0 means unlimited.
//
// The PNGPULLSTATE (and PNGIMAGE) must outlive the decode and must not be
// copied: it owns the 32K zlib window and the pointers into its own buffers.
//
#define PNG_PULL_MAGIC          0x504e4750u /* 'PNGP' */
#define PNG_PULL_PAUSED         1
#define PNG_PULL_INVALID_STATE  20
#define PNG_PULL_BAD_BLOCK      21
#define PNG_PULL_PAUSE_LIMIT    22

typedef int32_t (PNG_PULL_CALLBACK)(void *pUser, uint8_t *pBuffer, int32_t iMaxLen);

// internal phases of the resumable chunk parser
enum {
    PNG_PULL_PHASE_SIGNATURE = 0,
    PNG_PULL_PHASE_CHUNK_HEADER,
    PNG_PULL_PHASE_CHUNK_DATA,
    PNG_PULL_PHASE_IDAT,
    PNG_PULL_PHASE_SKIP,
    PNG_PULL_PHASE_CRC,
    PNG_PULL_PHASE_DONE,
    PNG_PULL_PHASE_ERROR
};

typedef struct png_pull_state_tag
{
    uint32_t u32Magic;              // set by PNGPullInit()
    PNGIMAGE *pImage;               // image this state belongs to
    PNG_PULL_CALLBACK *pfnPull;     // source callback
    void *pUser;                    // source user pointer
    int iPhase;                     // PNG_PULL_PHASE_*
    int iError;                     // last error (PNG_* or PNG_PULL_*)
    int iBlockSize;                 // largest block per callback
    int iMaxPauses;                 // consecutive empty replies allowed (0 = any)
    int iPauseStreak;               // consecutive empty replies so far
    int iCallbackCount;             // total source callback invocations
    int iPauseCount;                // total empty replies seen
    int iBytesPulled;               // total bytes obtained from the source
    int iOptions;                   // decode options for this run
    int bHeaderDone;                // IHDR parsed
    int bZlibReady;                 // inflate initialized
    int32_t iChunkType;             // current chunk type
    int32_t iChunkLength;           // current chunk declared length
    int iChunkRemaining;            // data bytes left in the current chunk
    int iCrcRemaining;              // CRC bytes left to skip for the chunk
    int iLine;                      // lines delivered so far
    z_stream d_stream;              // persistent zlib stream
    uint8_t ucZLIB[32768 + sizeof(struct inflate_state)]; // 32K window + state
    uint8_t ucFileBuf[PNG_FILE_BUF_SIZE]; // staging buffer for pulled bytes
    int iBufPos;                    // read offset inside ucFileBuf
    int iBufLen;                    // valid bytes in ucFileBuf
    uint8_t *pCurr;                 // current de-filter line (pitch+1 bytes)
    uint8_t *pPrev;                 // previous de-filter line (pitch+1 bytes)
} PNGPULLSTATE;

//
// Refill / reposition helpers for the staging buffer
//
static void PNGPullCompact(PNGPULLSTATE *pSt)
{
    int iAvail = pSt->iBufLen - pSt->iBufPos;

    if (pSt->iBufPos > 0) {
        if (iAvail > 0)
            memmove(pSt->ucFileBuf, &pSt->ucFileBuf[pSt->iBufPos], iAvail);
        pSt->iBufPos = 0;
        pSt->iBufLen = iAvail;
    }
} /* PNGPullCompact() */

//
// Make sure at least iNeed bytes are contiguous in ucFileBuf.
// Returns PNG_SUCCESS, PNG_PULL_PAUSED or a PNG error code.
//
static int PNGPullNeed(PNGPULLSTATE *pSt, int iNeed)
{
    if (iNeed <= 0)
        return PNG_SUCCESS;
    if (iNeed > PNG_FILE_BUF_SIZE)
        return PNG_INVALID_PARAMETER;
    for (;;) {
        int iAvail, iWant, iRoom;
        int32_t iGot;

        if (pSt->iBufPos > 0)
            PNGPullCompact(pSt);
        iAvail = pSt->iBufLen - pSt->iBufPos;
        if (iAvail >= iNeed)
            return PNG_SUCCESS;
        iRoom = PNG_FILE_BUF_SIZE - pSt->iBufLen;
        if (iRoom <= 0) { // request cannot fit: treat as malformed input
            pSt->iError = PNG_DECODE_ERROR;
            return PNG_DECODE_ERROR;
        }
        iWant = pSt->iBlockSize;
        if (iWant > iRoom)
            iWant = iRoom;
        iGot = (*pSt->pfnPull)(pSt->pUser, &pSt->ucFileBuf[pSt->iBufLen], iWant);
        pSt->iCallbackCount++;
        if (iGot > iWant) { // callback violated the block contract
            pSt->iError = PNG_PULL_BAD_BLOCK;
            return PNG_PULL_BAD_BLOCK;
        }
        if (iGot < 0) { // source hit end of input
            pSt->iError = PNG_DECODE_ERROR;
            return PNG_DECODE_ERROR;
        }
        if (iGot == 0) {
            pSt->iPauseCount++;
            pSt->iPauseStreak++;
            if (pSt->iMaxPauses > 0 && pSt->iPauseStreak > pSt->iMaxPauses) {
                pSt->iError = PNG_PULL_PAUSE_LIMIT;
                return PNG_PULL_PAUSE_LIMIT;
            }
            return PNG_PULL_PAUSED;
        }
        pSt->iPauseStreak = 0;
        pSt->iBufLen += (int)iGot;
        pSt->iBytesPulled += (int)iGot;
    }
} /* PNGPullNeed() */

//
// Consume up to *piRemaining bytes from the staging buffer, pulling more
// when needed. Partial progress is recorded in *piRemaining, so the operation
// can be suspended and resumed at any byte boundary.
//
static int PNGPullConsume(PNGPULLSTATE *pSt, int *piRemaining)
{
    while (*piRemaining > 0) {
        int iAvail = pSt->iBufLen - pSt->iBufPos;
        int rc, iTake;

        if (iAvail == 0) {
            rc = PNGPullNeed(pSt, 1);
            if (rc != PNG_SUCCESS)
                return rc;
            iAvail = pSt->iBufLen - pSt->iBufPos;
        }
        iTake = (iAvail < *piRemaining) ? iAvail : *piRemaining;
        pSt->iBufPos += iTake;
        *piRemaining -= iTake;
    }
    return PNG_SUCCESS;
} /* PNGPullConsume() */

//
// Validate IHDR and fill in the image geometry, mirroring PNGParseInfo()
// s points to the 8-byte chunk header (length + 'IHDR'), followed by the
// 13 bytes of data and the 4-byte CRC.
//
static int PNGPullParseIHDR(PNGIMAGE *pImage, uint8_t *s)
{
    int len = MOTOLONG(&s[0]);
    int crc, hdrcrc;

    if (MOTOLONG(&s[4]) != 0x49484452 /*'IHDR'*/ || len != 13)
        return PNG_INVALID_FILE;
    crc = (int)crc32(0, &s[4], 17);
    hdrcrc = MOTOLONG(&s[8 + 13]);
    if (crc != hdrcrc)
        return PNG_INVALID_FILE;
    pImage->iWidth = MOTOLONG(&s[8]);
    pImage->iHeight = MOTOLONG(&s[12]);
    if (pImage->iWidth <= 0 || pImage->iHeight <= 0 ||
        pImage->iWidth > PNG_MAX_BUFFERED_PIXELS / 4)
        return PNG_TOO_BIG;
    pImage->ucBpp = s[16];
    pImage->ucPixelType = s[17];
    pImage->iInterlaced = s[20];
    if (pImage->iInterlaced || pImage->ucBpp > 8)
        return PNG_UNSUPPORTED_FEATURE;
    if (pImage->ucPixelType != PNG_PIXEL_GRAYSCALE &&
        pImage->ucPixelType != PNG_PIXEL_TRUECOLOR &&
        pImage->ucPixelType != PNG_PIXEL_INDEXED &&
        pImage->ucPixelType != PNG_PIXEL_GRAY_ALPHA &&
        pImage->ucPixelType != PNG_PIXEL_TRUECOLOR_ALPHA)
        return PNG_INVALID_FILE;
    if (pImage->ucBpp == 0 || pImage->ucBpp == 3 ||
        pImage->ucBpp == 5 || pImage->ucBpp == 6)
        return PNG_INVALID_FILE;
    switch (pImage->ucPixelType) {
        case PNG_PIXEL_GRAYSCALE:
        case PNG_PIXEL_INDEXED:
            pImage->iPitch = (pImage->iWidth * pImage->ucBpp + 7) / 8;
            break;
        case PNG_PIXEL_TRUECOLOR:
            pImage->iPitch = ((3 * pImage->ucBpp) * pImage->iWidth + 7) / 8;
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            pImage->iPitch = ((2 * pImage->ucBpp) * pImage->iWidth + 7) / 8;
            pImage->iHasAlpha = 1;
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            pImage->iPitch = ((4 * pImage->ucBpp) * pImage->iWidth + 7) / 8;
            pImage->iHasAlpha = 1;
            break;
    }
    if ((pImage->iPitch + 1) * 2 + 32 > PNG_MAX_BUFFERED_PIXELS)
        return PNG_TOO_BIG;
    return PNG_SUCCESS;
} /* PNGPullParseIHDR() */

//
// Initialize a pull-mode decode.
// Returns PNG_SUCCESS or PNG_INVALID_PARAMETER for bad arguments.
//
int PNGPullInit(PNGIMAGE *pImage, PNGPULLSTATE *pSt,
                PNG_PULL_CALLBACK *pfnPull, void *pUser,
                PNG_DRAW_CALLBACK *pfnDraw, int iBlockSize, int iMaxPauses)
{
    if (pImage == NULL || pSt == NULL || pfnPull == NULL)
        return PNG_INVALID_PARAMETER;
    if (iBlockSize <= 0 || iBlockSize > PNG_FILE_BUF_SIZE || iMaxPauses < 0)
        return PNG_INVALID_PARAMETER;
    memset(pImage, 0, sizeof(PNGIMAGE));
    memset(pSt, 0, sizeof(PNGPULLSTATE));
    pSt->u32Magic = PNG_PULL_MAGIC;
    pSt->pImage = pImage;
    pSt->pfnPull = pfnPull;
    pSt->pUser = pUser;
    pSt->iBlockSize = iBlockSize;
    pSt->iMaxPauses = iMaxPauses;
    pSt->iPhase = PNG_PULL_PHASE_SIGNATURE;
    pImage->pfnDraw = pfnDraw;
    pImage->pfnOpen = NULL;
    pImage->pfnClose = NULL;
    pImage->pfnRead = NULL; // pull mode never uses the push-style callbacks
    pImage->pfnSeek = NULL;
    return PNG_SUCCESS;
} /* PNGPullInit() */

//
// Adjust the block size / pause budget before (or between) decode calls
//
int PNGPullSetBlockSize(PNGPULLSTATE *pSt, int iBlockSize)
{
    if (pSt == NULL || pSt->u32Magic != PNG_PULL_MAGIC)
        return PNG_PULL_INVALID_STATE;
    if (iBlockSize <= 0 || iBlockSize > PNG_FILE_BUF_SIZE)
        return PNG_INVALID_PARAMETER;
    pSt->iBlockSize = iBlockSize;
    return PNG_SUCCESS;
} /* PNGPullSetBlockSize() */

int PNGPullSetMaxPauses(PNGPULLSTATE *pSt, int iMaxPauses)
{
    if (pSt == NULL || pSt->u32Magic != PNG_PULL_MAGIC)
        return PNG_PULL_INVALID_STATE;
    if (iMaxPauses < 0)
        return PNG_INVALID_PARAMETER;
    pSt->iMaxPauses = iMaxPauses;
    return PNG_SUCCESS;
} /* PNGPullSetMaxPauses() */

//
// Counters for the last (or current) decode: source callbacks, empty replies
// and total bytes obtained.
//
int PNGPullGetCounters(PNGPULLSTATE *pSt, int *pCalls, int *pPauses, int *pBytes)
{
    if (pSt == NULL || pSt->u32Magic != PNG_PULL_MAGIC)
        return PNG_PULL_INVALID_STATE;
    if (pCalls) *pCalls = pSt->iCallbackCount;
    if (pPauses) *pPauses = pSt->iPauseCount;
    if (pBytes) *pBytes = pSt->iBytesPulled;
    return PNG_SUCCESS;
} /* PNGPullGetCounters() */

int PNGPullGetError(PNGPULLSTATE *pSt)
{
    if (pSt == NULL || pSt->u32Magic != PNG_PULL_MAGIC)
        return PNG_PULL_INVALID_STATE;
    return pSt->iError;
} /* PNGPullGetError() */

//
// Feed one block of input to zlib and emit every complete line it produces.
// Mirrors the inner loops of DecodePNG() so the output is identical.
// Returns PNG_SUCCESS when the image is complete, PNG_PULL_PAUSED when the
// source has no data, or a PNG error code.
//
static int PNGPullIDAT(PNGIMAGE *pImage, PNGPULLSTATE *pSt, void *pUser)
{
    z_stream *pStream = &pSt->d_stream;

    for (;;) {
        int rc, err, iConsumed;
        uInt iBefore;

        if (pStream->avail_in == 0) {
            if (pSt->iChunkRemaining == 0)
                return PNG_SUCCESS; // this IDAT chunk is exhausted
            rc = PNGPullNeed(pSt, 1);
            if (rc != PNG_SUCCESS)
                return rc;
            {
                int iAvail = pSt->iBufLen - pSt->iBufPos;
                if (iAvail > pSt->iChunkRemaining)
                    iAvail = pSt->iChunkRemaining;
                pStream->next_in = &pSt->ucFileBuf[pSt->iBufPos];
                pStream->avail_in = (uInt)iAvail;
            }
        }
        if (pStream->avail_out == 0) { // start a new line
            pStream->avail_out = pImage->iPitch + 1;
            pStream->next_out = pSt->pCurr;
        }
        iBefore = pStream->avail_in;
        err = inflate(pStream, Z_NO_FLUSH, pSt->iOptions & PNG_CHECK_CRC);
        iConsumed = (int)(iBefore - pStream->avail_in);
        pSt->iBufPos += iConsumed;
        pSt->iChunkRemaining -= iConsumed;

        if ((err == Z_OK || err == Z_STREAM_END) && pStream->avail_out == 0) {
            uint8_t *tmp;
            DeFilter(pSt->pCurr, pSt->pPrev, pImage->iWidth, pImage->iPitch);
            if (pImage->pImage == NULL) { // no image buffer, send it line by line
                PNGDRAW pngd;
                pngd.pUser = pUser;
                pngd.iPitch = pImage->iPitch;
                pngd.iWidth = pImage->iWidth;
                pngd.pPalette = pImage->ucPalette;
                pngd.pFastPalette = (pSt->iOptions & PNG_FAST_PALETTE) ?
                    (uint16_t *)&pImage->ucPixels[sizeof(pImage->ucPixels) - 512] : NULL;
                pngd.pPixels = pSt->pCurr + 1;
                pngd.iPixelType = pImage->ucPixelType;
                pngd.iHasAlpha = pImage->iHasAlpha;
                pngd.iBpp = pImage->ucBpp;
                pngd.y = pSt->iLine;
                if (!(*pImage->pfnDraw)(&pngd)) {
                    pSt->iError = PNG_QUIT_EARLY;
                    pSt->iPhase = PNG_PULL_PHASE_ERROR;
                    return PNG_QUIT_EARLY;
                }
            } else { // copy to the destination bitmap
                memcpy(&pImage->pImage[pSt->iLine * pImage->iPitch],
                       &pSt->pCurr[1], pImage->iPitch);
            }
            pSt->iLine++;
            tmp = pSt->pCurr; pSt->pCurr = pSt->pPrev; pSt->pPrev = tmp;
            if (pSt->iLine >= pImage->iHeight) { // complete image
                pSt->iPhase = PNG_PULL_PHASE_DONE;
                return PNG_SUCCESS;
            }
        }
        if (err == Z_STREAM_END) {
            // stream ended before all lines were produced
            pSt->iError = PNG_DECODE_ERROR;
            pSt->iPhase = PNG_PULL_PHASE_ERROR;
            return PNG_DECODE_ERROR;
        }
        if (err == Z_DATA_ERROR || err == Z_STREAM_ERROR) {
            pSt->iError = PNG_DECODE_ERROR;
            pSt->iPhase = PNG_PULL_PHASE_ERROR;
            return PNG_DECODE_ERROR;
        }
        if (err != Z_OK && err != Z_BUF_ERROR) { // Z_MEM_ERROR / Z_NEED_DICT
            pSt->iError = PNG_DECODE_ERROR;
            pSt->iPhase = PNG_PULL_PHASE_ERROR;
            return PNG_DECODE_ERROR;
        }
        // Z_OK / Z_BUF_ERROR: keep feeding input or start the next line
        if (pSt->iChunkRemaining == 0 && pStream->avail_in == 0)
            return PNG_SUCCESS; // this IDAT chunk is finished
    }
} /* PNGPullIDAT() */

//
// Handle one complete non-IDAT chunk whose data is contiguous at s.
// Mirrors the marker switch in DecodePNG().
//
static void PNGPullChunk(PNGIMAGE *pImage, PNGPULLSTATE *pSt, int32_t iMarker,
                         uint8_t *s, int iLen)
{
    switch (iMarker) {
        case 0x504c5445: //'PLTE'
            memset(&pImage->ucPalette[768], 0xff, 256);
            if (iLen > 0 && iLen <= 768)
                memcpy(pImage->ucPalette, s, iLen);
            if (pSt->iOptions & PNG_FAST_PALETTE) {
                int i, iColors = 1 << pImage->ucBpp;
                uint16_t usPixel, *d;
                uint8_t *p = pImage->ucPalette;
                d = (uint16_t *)&pImage->ucPixels[sizeof(pImage->ucPixels) - 512];
                for (i = 0; i < iColors; i++) {
                    usPixel = (p[2] >> 3);
                    usPixel |= ((p[1] >> 2) << 5);
                    usPixel |= ((p[0] >> 3) << 11);
                    *d++ = usPixel;
                    p += 3;
                }
            }
            break;
        case 0x74524e53: //'tRNS'
            if (pImage->ucPixelType == PNG_PIXEL_INDEXED && iLen > 0 && iLen <= 256) {
                memcpy(&pImage->ucPalette[768], s, iLen);
                pImage->iHasAlpha = 1;
            } else if (iLen == 2) {
                pImage->iTransparent = s[1];
                pImage->iHasAlpha = 1;
            } else if (iLen == 6) {
                pImage->iTransparent = s[5];
                pImage->iTransparent |= (s[3] << 8);
                pImage->iTransparent |= (s[1] << 16);
                pImage->iHasAlpha = 1;
            }
            break;
        case 0x74455874: //'tEXt'
            if (iLen > 0) {
                int i, iCopy;
                char szTemp[64];
                char *pDest = NULL;
                memset(szTemp, 0, sizeof(szTemp));
                iCopy = (iLen < (int)sizeof(szTemp) - 1) ? iLen : (int)sizeof(szTemp) - 1;
                memcpy(szTemp, s, iCopy); // never read past the chunk data
                i = (int)strlen(szTemp) + 1;
                if (strcmp(szTemp, "Comment") == 0 || strcmp(szTemp, "Description") == 0)
                    pDest = &pImage->szComment[0];
                if (pDest != NULL && iLen > i) {
                    if ((iLen - i) < PNG_COMMENT_SIZE) {
                        memcpy(pImage->szComment, &s[i], iLen - i);
                        pImage->szComment[iLen - i + 1] = 0;
                    } else {
                        memcpy(pImage->szComment, &s[i], PNG_COMMENT_SIZE - 1);
                        pImage->szComment[PNG_COMMENT_SIZE - 1] = '\0';
                    }
                }
            }
            break;
        default: // bKGD / gAMA / unknown chunks are ignored, like DecodePNG()
            break;
    }
} /* PNGPullChunk() */

//
// Pull-mode decode. Call repeatedly until it no longer returns
// PNG_PULL_PAUSED. The entire decoder state lives in pSt, so a pause at any
// byte boundary is transparent to the caller.
//
int PNGPullDecode(PNGIMAGE *pImage, PNGPULLSTATE *pSt, void *pUser, int iOptions)
{
    if (pImage == NULL || pSt == NULL || pSt->u32Magic != PNG_PULL_MAGIC ||
        pSt->pfnPull == NULL || pSt->pImage != pImage)
        return PNG_PULL_INVALID_STATE;
    if (pSt->iPhase == PNG_PULL_PHASE_ERROR)
        return PNG_PULL_INVALID_STATE; // must re-init to decode again
    if (pImage->pImage == NULL && pImage->pfnDraw == NULL) {
        pImage->iError = PNG_NO_BUFFER;
        return PNG_NO_BUFFER;
    }
    pSt->iOptions = iOptions;
    if (pSt->iPhase == PNG_PULL_PHASE_DONE)
        return PNG_PULL_INVALID_STATE; // must re-init to decode again

    for (;;) {
        int rc;

        switch (pSt->iPhase) {
        case PNG_PULL_PHASE_SIGNATURE:
            rc = PNGPullNeed(pSt, 8);
            if (rc != PNG_SUCCESS)
                return rc;
            if (MOTOLONG(&pSt->ucFileBuf[pSt->iBufPos]) != (int32_t)0x89504e47) {
                pSt->iError = PNG_INVALID_FILE;
                pSt->iPhase = PNG_PULL_PHASE_ERROR;
                return PNG_INVALID_FILE;
            }
            pSt->iBufPos += 8;
            pSt->iPhase = PNG_PULL_PHASE_CHUNK_HEADER;
            break;

        case PNG_PULL_PHASE_CHUNK_HEADER:
        {
            int len;
            uint8_t *s;

            rc = PNGPullNeed(pSt, 8);
            if (rc != PNG_SUCCESS)
                return rc;
            s = &pSt->ucFileBuf[pSt->iBufPos];
            len = MOTOLONG(s);
            pSt->iChunkLength = len;
            pSt->iChunkType = MOTOLONG(&s[4]);
            if (len < 0 || len > 0x7ffffff0) { // malformed length
                pSt->iError = PNG_DECODE_ERROR;
                pSt->iPhase = PNG_PULL_PHASE_ERROR;
                return PNG_DECODE_ERROR;
            }
            if (pSt->iChunkType == 0x49484452) { //'IHDR'
                if (pSt->bHeaderDone || len != 13) {
                    pSt->iError = PNG_INVALID_FILE;
                    pSt->iPhase = PNG_PULL_PHASE_ERROR;
                    return PNG_INVALID_FILE;
                }
                rc = PNGPullNeed(pSt, 8 + 13 + 4);
                if (rc != PNG_SUCCESS)
                    return rc;
                rc = PNGPullParseIHDR(pImage, &pSt->ucFileBuf[pSt->iBufPos]);
                if (rc != PNG_SUCCESS) {
                    pSt->iError = rc;
                    pSt->iPhase = PNG_PULL_PHASE_ERROR;
                    return rc;
                }
                // set up the line buffers (same alignment rule as DecodePNG)
                {
                    int y = (int)(intptr_t)&pImage->ucPixels[0];
                    y &= 15;
                    y += (15 - y);
                    pSt->pCurr = &pImage->ucPixels[y];
                    y += pImage->iPitch + 1;
                    y += (15 - (y & 15));
                    pSt->pPrev = &pImage->ucPixels[y];
                    pSt->d_stream.avail_out = 0;
                    pSt->d_stream.next_out = pImage->pImage;
                }
                pSt->bHeaderDone = 1;
                pSt->iBufPos += 8 + 13 + 4;
                pSt->iPhase = PNG_PULL_PHASE_CHUNK_HEADER;
                break;
            }
            if (!pSt->bHeaderDone) { // IHDR must be the first chunk
                pSt->iError = PNG_INVALID_FILE;
                pSt->iPhase = PNG_PULL_PHASE_ERROR;
                return PNG_INVALID_FILE;
            }
            pSt->iBufPos += 8;
            pSt->iChunkRemaining = len;
            if (pSt->iChunkType == 0x49444154) { //'IDAT'
                if (!pSt->bZlibReady) {
                    struct inflate_state *state;
                    // zlib state and window live inside the pull state, so no
                    // malloc/free is performed (same trick as DecodePNG)
                    memset(&pSt->d_stream, 0, sizeof(pSt->d_stream));
                    state = (struct inflate_state FAR *)pSt->ucZLIB;
                    pSt->d_stream.state = (struct internal_state FAR *)state;
                    state->window = &pSt->ucZLIB[sizeof(struct inflate_state)];
                    rc = inflateInit(&pSt->d_stream);
                    if (rc != Z_OK) {
                        pSt->iError = PNG_MEM_ERROR;
                        pSt->iPhase = PNG_PULL_PHASE_ERROR;
                        return PNG_MEM_ERROR;
                    }
                    pSt->d_stream.avail_out = 0;
                    pSt->d_stream.next_out = pImage->pImage;
                    pSt->bZlibReady = 1;
                }
                pSt->iPhase = PNG_PULL_PHASE_IDAT;
            } else if (len == 0) {
                pSt->iCrcRemaining = 4; pSt->iPhase = PNG_PULL_PHASE_CRC;
            } else if (len <= PNG_FILE_BUF_SIZE) {
                pSt->iPhase = PNG_PULL_PHASE_CHUNK_DATA;
            } else { // ancillary chunk too large to buffer: discard it
                pSt->iPhase = PNG_PULL_PHASE_SKIP;
            }
            break;
        }

        case PNG_PULL_PHASE_CHUNK_DATA:
            rc = PNGPullNeed(pSt, pSt->iChunkRemaining);
            if (rc != PNG_SUCCESS)
                return rc;
            PNGPullChunk(pImage, pSt, pSt->iChunkType,
                         &pSt->ucFileBuf[pSt->iBufPos], pSt->iChunkRemaining);
            pSt->iBufPos += pSt->iChunkRemaining;
            pSt->iChunkRemaining = 0;
            pSt->iCrcRemaining = 4; pSt->iPhase = PNG_PULL_PHASE_CRC;
            break;

        case PNG_PULL_PHASE_IDAT:
            rc = PNGPullIDAT(pImage, pSt, pUser);
            if (rc == PNG_PULL_PAUSED)
                return rc;
            if (rc != PNG_SUCCESS)
                return rc;
            if (pSt->iPhase == PNG_PULL_PHASE_DONE)
                return PNG_SUCCESS;
            if (pSt->iChunkRemaining == 0) { // chunk done, skip its CRC
                pSt->iCrcRemaining = 4;
                pSt->iPhase = PNG_PULL_PHASE_CRC;
            }
            break;

        case PNG_PULL_PHASE_SKIP:
            rc = PNGPullConsume(pSt, &pSt->iChunkRemaining);
            if (rc != PNG_SUCCESS)
                return rc;
            pSt->iCrcRemaining = 4; pSt->iPhase = PNG_PULL_PHASE_CRC;
            break;

        case PNG_PULL_PHASE_CRC:
            rc = PNGPullConsume(pSt, &pSt->iCrcRemaining);
            if (rc != PNG_SUCCESS)
                return rc;
            pSt->iPhase = PNG_PULL_PHASE_CHUNK_HEADER;
            break;

        default: // unreachable
            pSt->iPhase = PNG_PULL_PHASE_ERROR;
            pSt->iError = PNG_PULL_INVALID_STATE;
            return PNG_PULL_INVALID_STATE;
        }
    }
} /* PNGPullDecode() */
