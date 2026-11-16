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
PNG_STATIC int DecodePNG(PNGIMAGE *pPage, void *pUser, int iOptions);

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

void PNG_setBuffer(PNGIMAGE *pPNG, uint8_t *pBuffer)
{
    pPNG->pImage = pBuffer;
} /* PNG_setBuffer() */

int PNG_decode(PNGIMAGE *pPNG, void *pUser, int iOptions)
{
    return DecodePNG(pPNG, pUser, iOptions);
} /* PNG_decode() */

int PNG_getBpp(PNGIMAGE *pPNG)
{
    return (int)pPNG->ucBpp;
} /* PNG_getBpp() */

int PNG_getPixelType(PNGIMAGE *pPNG)
{
    return (int)pPNG->ucPixelType;
} /* PNG_getPixelType() */

int PNG_hasAlpha(PNGIMAGE *pPNG)
{
    return pPNG->iHasAlpha;
} /* PNG_hasAlpha() */

int PNG_isInterlaced(PNGIMAGE *pPNG)
{
    return pPNG->iInterlaced;
} /* PNG_isInterlaced() */

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

//
// External (caller supplied) buffer mode
//
// Embedded targets often have no heap at all. A PNGEXTERNALBUFFERS structure
// can be passed to decode() through pUser with its magic field set. In that
// mode every working memory area (two de-filter lines, the 32K zlib window,
// the inflate state, the 2K chunk read buffer, palette and comment storage)
// comes from caller supplied buffers and the decoder performs no internal
// allocation. This also unlocks Adam7 interlaced images, which decode into
// the caller supplied full-image buffer (pImage set via setBuffer()).
//
// PNG_EXT_BUFFER_MAGIC deliberately sits in the high word so it cannot
// collide with a normal 32-bit pUser pointer value.
//
#define PNG_EXT_BUFFER_MAGIC 0x504e4758u /* 'PNGX' */
#define PNG_EXT_BUFFER_TYPES_DEFINED 1
typedef struct png_external_buffers_tag
{
    uint32_t u32Magic;       // must be PNG_EXT_BUFFER_MAGIC
    uint8_t *pLineBuffer;    // caller supplied line/work buffer
    int iLineBufferSize;     // size of the line buffer in bytes
    uint8_t *pWorkspace;     // caller supplied zlib/palette workspace
    int iWorkspaceSize;      // size of the workspace in bytes
    void *pUser;             // delivered to the draw callback in callback mode
    /* output fields, filled in by the decoder */
    int iLineBufferNeeded;   // minimum line buffer size for this image
    int iWorkspaceNeeded;    // minimum workspace size for this image
    int iImageBufferNeeded;  // full-image buffer size (interlaced images)
} PNGEXTERNALBUFFERS;

// Adam7 interlacing: start (x,y) and (dx,dy) step for each of the 7 passes
static const uint8_t PNGAdam7XStart[7] = {0, 4, 0, 2, 0, 1, 0};
static const uint8_t PNGAdam7YStart[7] = {0, 0, 4, 0, 2, 0, 1};
static const uint8_t PNGAdam7XStep[7]  = {8, 8, 4, 4, 2, 2, 1};
static const uint8_t PNGAdam7YStep[7]  = {8, 8, 8, 4, 4, 2, 2};

PNG_STATIC int PNGAlign16(int i)
{
    return (i + 15) & ~15;
} /* PNGAlign16() */

//
// Minimum workspace size (independent of image geometry):
// inflate state + 32K sliding window + 1K palette + 2K file chunk buffer
// + 128 byte comment string
//
PNG_STATIC int PNGWorkspaceSize(void)
{
    return PNGAlign16((int)sizeof(struct inflate_state)) + 32768 + 1024 +
           PNG_FILE_BUF_SIZE + PNG_COMMENT_SIZE;
} /* PNGWorkspaceSize() */

//
// Minimum line buffer size for an image with the given pitch
// (two filter-prefixed 16-byte aligned lines plus 512 bytes reserved at the
// top for the optional PNG_FAST_PALETTE RGB565 lookup table)
//
PNG_STATIC int PNGLineBufferSize(int iPitch)
{
    return PNGAlign16(16 + iPitch + 1) + PNGAlign16(iPitch + 1) + 512;
} /* PNGLineBufferSize() */

//
// Store the minimum buffer requirements of the currently open image into
// the PNGEXTERNALBUFFERS structure. The pointer may also be passed directly
// to decode() afterwards. Returns PNG_SUCCESS or PNG_INVALID_PARAMETER.
//
PNG_STATIC int PNGGetExternalBufferSizes(PNGIMAGE *pPage, PNGEXTERNALBUFFERS *pExt)
{
    if (pExt == NULL || pPage->iPitch <= 0)
        return PNG_INVALID_PARAMETER;
    pExt->u32Magic = PNG_EXT_BUFFER_MAGIC;
    pExt->iLineBufferNeeded = PNGLineBufferSize(pPage->iPitch);
    pExt->iWorkspaceNeeded = PNGWorkspaceSize();
    pExt->iImageBufferNeeded = pPage->iHeight * pPage->iPitch;
    return PNG_SUCCESS;
} /* PNGGetExternalBufferSizes() */

//
// Carve the individual working areas out of the caller supplied workspace.
// Returns PNG_SUCCESS or PNG_MEM_ERROR when a buffer is missing/too small.
//
PNG_STATIC int PNGSetupExternalBuffers(PNGIMAGE *pPage, PNGEXTERNALBUFFERS *pExt,
                                       uint8_t **ppZlib, uint8_t **ppFileBuf,
                                       uint8_t **ppPalette, uint16_t **ppFastPalette,
                                       char **ppComment)
{
    uint8_t *p;
    int iOff, iStateSize;

    if (pExt->pLineBuffer == NULL || pExt->iLineBufferSize <= 0 ||
        pExt->pWorkspace == NULL || pExt->iWorkspaceSize <= 0)
        return PNG_MEM_ERROR;
    if (pExt->iLineBufferSize < PNGLineBufferSize(pPage->iPitch))
        return PNG_MEM_ERROR;
    if (pExt->iWorkspaceSize < PNGWorkspaceSize())
        return PNG_MEM_ERROR;

    iStateSize = PNGAlign16((int)sizeof(struct inflate_state));
    memset(pExt->pWorkspace, 0, pExt->iWorkspaceSize);
    p = pExt->pWorkspace;
    *ppZlib = p;                                     // inflate state + window
    iOff = iStateSize + 32768;
    *ppPalette = (uint8_t *)&pExt->pWorkspace[iOff];  // 1024 byte palette
    iOff += 1024;
    *ppFileBuf = (uint8_t *)&pExt->pWorkspace[iOff];  // 2K chunk buffer
    iOff += PNG_FILE_BUF_SIZE;
    *ppComment = (char *)&pExt->pWorkspace[iOff];     // comment string
    // fast RGB565 palette lives at the top of the line buffer
    *ppFastPalette = (uint16_t *)&pExt->pLineBuffer[pExt->iLineBufferSize - 512];
    return PNG_SUCCESS;
} /* PNGSetupExternalBuffers() */

//
// Write one de-filtered Adam7 scanline into its positions inside the
// full-size image buffer
//
PNG_STATIC void PNGPlaceAdam7Line(uint8_t *pImage, const uint8_t *pLine,
                                  int iWidth, int iPitch, int y,
                                  int xStart, int xStep, int yStep,
                                  int iPixelType, int iBpp)
{
    int x, i;
    if (iBpp < 8) { // packed sub-byte pixels (grayscale / indexed)
        int iBitsPerPixel = iBpp;
        for (x = 0; x < iWidth; x++) {
            int iSrcBit = x * iBitsPerPixel;
            int iByte = iSrcBit >> 3;
            int iShift = 8 - iBitsPerPixel - (iSrcBit & 7);
            int iValue = (pLine[iByte] >> iShift) & ((1 << iBitsPerPixel) - 1);
            int iDstX = xStart + x * xStep;
            int iDstBit = iDstX * iBitsPerPixel;
            int iDstByte = iDstBit >> 3;
            int iDstShift = 8 - iBitsPerPixel - (iDstBit & 7);
            uint8_t *pDst = &pImage[y * iPitch + iDstByte];
            *pDst = (uint8_t)((*pDst & ~(((1 << iBitsPerPixel) - 1) << iDstShift)) |
                              (iValue << iDstShift));
        }
    } else {
        int iBytesPerPixel;
        switch (iPixelType) {
            case PNG_PIXEL_GRAY_ALPHA:
                iBytesPerPixel = 2 * (iBpp / 8);
                break;
            case PNG_PIXEL_TRUECOLOR:
                iBytesPerPixel = 3 * (iBpp / 8);
                break;
            case PNG_PIXEL_TRUECOLOR_ALPHA:
                iBytesPerPixel = 4 * (iBpp / 8);
                break;
            default: // grayscale / indexed, 8 bits per pixel
                iBytesPerPixel = 1;
                break;
        }
        for (x = 0; x < iWidth; x++) {
            int iDstX = xStart + x * xStep;
            uint8_t *pDst = &pImage[y * iPitch + iDstX * iBytesPerPixel];
            const uint8_t *pSrc = &pLine[x * iBytesPerPixel];
            for (i = 0; i < iBytesPerPixel; i++)
                pDst[i] = pSrc[i];
        }
    }
    (void)yStep; // the caller selects the destination row directly
} /* PNGPlaceAdam7Line() */

//
// Calculate the geometry of one Adam7 interlace pass from the image geometry
//
PNG_STATIC void PNGAdam7PassGeometry(PNGIMAGE *pPage, int iPass,
                                     int *piX, int *piY, int *piWidth,
                                     int *piHeight, int *piPitch)
{
    int xStart = PNGAdam7XStart[iPass];
    int yStart = PNGAdam7YStart[iPass];
    int xStep = PNGAdam7XStep[iPass];
    int yStep = PNGAdam7YStep[iPass];
    int iWidth, iHeight, iPitch;

    *piX = xStart;
    *piY = yStart;
    iWidth = (pPage->iWidth > xStart) ? (pPage->iWidth - xStart + xStep - 1) / xStep : 0;
    iHeight = (pPage->iHeight > yStart) ? (pPage->iHeight - yStart + yStep - 1) / yStep : 0;
    *piWidth = iWidth;
    *piHeight = iHeight;
    switch (pPage->ucPixelType) {
        case PNG_PIXEL_TRUECOLOR:
            iPitch = ((3 * pPage->ucBpp) * iWidth + 7) / 8;
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            iPitch = ((2 * pPage->ucBpp) * iWidth + 7) / 8;
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            iPitch = ((4 * pPage->ucBpp) * iWidth + 7) / 8;
            break;
        default: // grayscale / indexed
            iPitch = (iWidth * pPage->ucBpp + 7) / 8;
            break;
    }
    *piPitch = iPitch;
} /* PNGAdam7PassGeometry() */

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
        if (pPage->iWidth <= 0 || pPage->iHeight <= 0 ||
            pPage->iWidth > 65535 || pPage->iHeight > 65535) {  // Image size is outside of valid range
            pPage->iError = PNG_TOO_BIG;
            return pPage->iError;
        }
        pPage->ucBpp = s[24]; // bits per pixel
        pPage->ucPixelType = s[25]; // pixel type
        pPage->iInterlaced = s[28];
        if (pPage->ucBpp > 8) { // 16-bit pixels are not supported (yet)
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
    if (pPage->iPitch <= 0) { // malformed header
       pPage->iError = PNG_INVALID_FILE;
       return pPage->iError;
    }
    // The internal (default) line buffer capacity is enforced again in
    // DecodePNG(); external buffer mode uses caller supplied memory instead.

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
    uint8_t *s;
    struct inflate_state *state;
    /* memory areas which can be redirected to caller supplied buffers */
    uint8_t *pZlib, *pFileBuf, *pPalette;
    uint16_t *pFastPalette;
    char *pComment;
    PNGEXTERNALBUFFERS *pExt = NULL;
    int bExternal = 0;
    /* Adam7 interlace state */
    int iPass = 0, iPassX = 0, iPassY = 0, iPassWidth = 0, iPassHeight = 0;
    int iPassPitch = 0, iPassLine = 0;

    // External buffer mode is selected by passing a PNGEXTERNALBUFFERS
    // descriptor (with its magic field set) as the pUser argument
    if (pUser != NULL && ((PNGEXTERNALBUFFERS *)pUser)->u32Magic == PNG_EXT_BUFFER_MAGIC)
        pExt = (PNGEXTERNALBUFFERS *)pUser;
    pPage->iError = PNG_SUCCESS;

    if (pExt != NULL) {
        bExternal = 1;
        // fill in the minimum sizes so the caller can also use this call to
        // discover the buffer requirements (pass NULL buffers for a pure query)
        PNGGetExternalBufferSizes(pPage, pExt);
        // interlaced images are assembled into the full image buffer only
        if (pPage->iInterlaced && pPage->pImage == NULL) {
            pPage->iError = PNG_NO_BUFFER;
            return pPage->iError;
        }
        err = PNGSetupExternalBuffers(pPage, pExt, &pZlib, &pFileBuf, &pPalette,
                                      &pFastPalette, &pComment);
        if (err != PNG_SUCCESS) {
            pPage->iError = err;
            return err;
        }
        // carve the two de-filter lines out of the caller supplied line buffer
        y = (int)((16 - (((intptr_t)pExt->pLineBuffer) & 15)) & 15);
        pCurr = &pExt->pLineBuffer[y]; // pixels start 16-byte aligned
        y += PNGAlign16(pPage->iPitch + 1);
        pPrev = &pExt->pLineBuffer[y];
        pComment[0] = 0;
    } else {
        // Either an image buffer must be set or a draw callback supplied
        if (pPage->pImage == NULL && pPage->pfnDraw == NULL) {
            pPage->iError = PNG_NO_BUFFER;
            return 0;
        }
        // Default mode: interlacing and over-wide images exceed the internal
        // fixed-size buffers, so they remain rejected here exactly as before
        if (pPage->iInterlaced) {
            pPage->iError = PNG_UNSUPPORTED_FEATURE;
            return pPage->iError;
        }
        if (pPage->iWidth > PNG_MAX_BUFFERED_PIXELS / 4 ||
            (pPage->iPitch+1)*2 + 32 > PNG_MAX_BUFFERED_PIXELS) {
            pPage->iError = PNG_TOO_BIG;
            return pPage->iError;
        }
        pZlib = pPage->ucZLIB;
        pFileBuf = pPage->ucFileBuf;
        pPalette = pPage->ucPalette;
        pFastPalette = (uint16_t *)&pPage->ucPixels[sizeof(pPage->ucPixels)-512];
        pComment = &pPage->szComment[0];
        // Use internal buffer to maintain the current and previous lines
        y = (int)(intptr_t)&pPage->ucPixels[0];
        y &= 15; // make sure we're 16-byte aligned, -1 for filter byte
        y += (15 - y);
        pCurr = &pPage->ucPixels[y]; // so that the pixels are 16-byte aligned
        y += pPage->iPitch + 1; // both lines are 16-byte (minus 1)
        y += (15 - (y & 15));
        pPrev = &pPage->ucPixels[y];
    }
    s = pFileBuf;
    if (pPage->iInterlaced) {
        // interlaced output is scattered into a zeroed full-image buffer
        memset(pPage->pImage, 0, pPage->iHeight * pPage->iPitch);
        memset(pCurr, 0, pPage->iPitch + 1);
        memset(pPrev, 0, pPage->iPitch + 1);
        PNGAdam7PassGeometry(pPage, iPass, &iPassX, &iPassY,
                             &iPassWidth, &iPassHeight, &iPassPitch);
        while (iPass < 7 && (iPassWidth == 0 || iPassHeight == 0)) {
            iPass++;
            if (iPass < 7)
                PNGAdam7PassGeometry(pPage, iPass, &iPassX, &iPassY,
                                     &iPassWidth, &iPassHeight, &iPassPitch);
        }
    }
    // Start decoding the image
    bDone = FALSE;
    // Inflate the compressed image data
    // The allocation functions are disabled and zlib has been modified
    // to not use malloc/free; the state buffer is internal or caller supplied
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;
    // Insert the memory pointer here to avoid having to use malloc() inside zlib
    state = (struct inflate_state FAR *)pZlib;
    d_stream.state = (struct internal_state FAR *)state;
    state->window = &pZlib[sizeof(struct inflate_state)]; // point to 32k dictionary buffer
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
    while (!bDone && (!pPage->iInterlaced ? (y < pPage->iHeight) : 1))
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
                memset(&pPalette[768], 0xff, 256); // assume all colors are opaque unless specified
                if (iLen > 0 && iLen <= 768 && iLen <= iAvailable) {
                    memcpy(pPalette, &s[iOffset], iLen);
                }
                if (iOptions & PNG_FAST_PALETTE) { // create a RGB565 palette
                    int i, iColors = 1 << pPage->ucBpp;
                    uint16_t usPixel, *d;
                    uint8_t *pPalSrc = pPalette;
                    d = pFastPalette;
                    for (i=0; i<iColors; i++) {
                    usPixel = (pPalSrc[2] >> 3); // blue
                    usPixel |= ((pPalSrc[1] >> 2) << 5); // green
                    usPixel |= ((pPalSrc[0] >> 3) << 11); // red
                    *d++ = usPixel;
                    pPalSrc += 3;
                    }
                }
                break;
            case 0x74524e53: //'tRNS' transparency info
                if (pPage->ucPixelType == PNG_PIXEL_INDEXED && iLen > 0 && iLen <= 256 && iLen <= iAvailable) // if palette exists
                {
                    memcpy(&pPalette[768], &s[iOffset], iLen);
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
                if (pPage->iInterlaced) {
                    // Adam7: each pass is an independent sub-image fed by the
                    // same zlib stream; de-filtered lines are scattered into
                    // the full-size image buffer
                    while (iLen && !bDone) {
                        if (iOffset >= iBytesRead) {
                            // we ran out of data; get some more
                            iBytesRead = (*pPage->pfnRead)(&pPage->PNGFile, pFileBuf, (iLen > PNG_FILE_BUF_SIZE) ? PNG_FILE_BUF_SIZE : iLen);
                            iFileOffset += iBytesRead;
                            iOffset = 0;
                        } else {
                            // number of bytes remaining in buffer
                            iBytesRead -= iOffset;
                        }
                        if (iBytesRead > iLen) { // we read too much
                            d_stream.next_in  = &pFileBuf[iOffset];
                            d_stream.avail_in = iLen;
                            iOffset += iLen;
                            iBytesRead -= iLen;
                            iLen = 0;
                        } else {
                            d_stream.next_in  = &pFileBuf[iOffset];
                            d_stream.avail_in = iBytesRead;
                            iLen -= iBytesRead;
                            iOffset += iBytesRead;
                            iBytesRead = 0;
                        }
                        err = 0;
                        while (err == Z_OK && iPass < 7) {
                            if (d_stream.avail_out == 0) { // reset for next pass line
                                d_stream.avail_out = iPassPitch + 1;
                                d_stream.next_out = pCurr;
                            }
                            err = inflate(&d_stream, Z_NO_FLUSH, iOptions & PNG_CHECK_CRC);
                            if ((err == Z_OK || err == Z_STREAM_END) && d_stream.avail_out == 0) {
                                DeFilter(pCurr, pPrev, iPassWidth, iPassPitch);
                                PNGPlaceAdam7Line(pPage->pImage, &pCurr[1],
                                                  iPassWidth, pPage->iPitch,
                                                  iPassY + iPassLine * PNGAdam7YStep[iPass],
                                                  iPassX, PNGAdam7XStep[iPass],
                                                  PNGAdam7YStep[iPass],
                                                  pPage->ucPixelType, pPage->ucBpp);
                                iPassLine++;
                                // swap current and previous lines
                                tmp = pCurr; pCurr = pPrev; pPrev = tmp;
                                if (iPassLine >= iPassHeight) { // pass complete
                                    do {
                                        iPass++;
                                        if (iPass < 7) {
                                            PNGAdam7PassGeometry(pPage, iPass, &iPassX, &iPassY,
                                                                 &iPassWidth, &iPassHeight, &iPassPitch);
                                        }
                                    } while (iPass < 7 && (iPassWidth == 0 || iPassHeight == 0));
                                    iPassLine = 0;
                                    if (iPass >= 7) { // every pass decoded
                                        bDone = TRUE;
                                    } else { // new pass starts with an all-zero predecessor
                                        memset(pCurr, 0, iPassPitch + 1);
                                        memset(pPrev, 0, iPassPitch + 1);
                                    }
                                    d_stream.avail_out = 0;
                                }
                            }
                        }
                        if (err == Z_DATA_ERROR || err == Z_STREAM_ERROR) {
                            iLen = 0;
                            pPage->iError = PNG_DECODE_ERROR;
                            bDone = TRUE;
                        } else if (err == Z_BUF_ERROR) {
                            // need more compressed data
                        }
                    } // while (iLen)
                } else while (iLen) {
                    if (iOffset >= iBytesRead) {
                        // we ran out of data; get some more
                        iBytesRead = (*pPage->pfnRead)(&pPage->PNGFile, pFileBuf, (iLen > PNG_FILE_BUF_SIZE) ? PNG_FILE_BUF_SIZE : iLen);
                        iFileOffset += iBytesRead;
                        iOffset = 0;
                    } else {
                        // number of bytes remaining in buffer
                        iBytesRead -= iOffset;
                    }
                    if (iBytesRead > iLen) { // we read too much
                        d_stream.next_in  = &pFileBuf[iOffset];
                        d_stream.avail_in = iLen;
                        iOffset += iLen; // point to start of next marker
                        iBytesRead -= iLen; // keep remaining byte count
                        iLen = 0; // every byte will be decoded
                    } else {
                        d_stream.next_in  = &pFileBuf[iOffset];
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
                                pngd.pUser = bExternal ? pExt->pUser : pUser;
                                pngd.iPitch = pPage->iPitch;
                                pngd.iWidth = pPage->iWidth;
                                pngd.pPalette = pPalette;
                                pngd.pFastPalette = (iOptions & PNG_FAST_PALETTE) ? pFastPalette : NULL;
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
                if (strcmp(szTemp, "Comment") == 0 || strcmp(szTemp, "Description") == 0) pDest = pComment;
                //else if (strcmp(szTemp, "Software") == 0) pDest = &pPage->szSoftware[0];
                //else if (strcmp(szTemp, "Author") == 0) pDest = &pPage->szArtist[0];
                if (pDest != NULL && iLen > i)
                {
                    if ((iLen - i) < PNG_COMMENT_SIZE)
                    {
                        memcpy(pDest, &s[iOffset + i], iLen - i);
                        pDest[iLen - i + 1] = 0;
                    }
                    else
                    {
                        memcpy(pDest, &s[iOffset + i], PNG_COMMENT_SIZE-1);
                        pDest[PNG_COMMENT_SIZE-1] = '\0';
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
// Externally linkable minimum-buffer query for external buffer mode.
// The C++ PNG class contains its PNGIMAGE state as the first member, so the
// same function serves PNG& (C++) and PNGIMAGE* (plain C). It is defined here
// (rather than in the public header) so the public interface stays untouched.
//
#ifdef __cplusplus
extern "C"
#endif
int PNG_getExternalBufferSizes(void *pPNG, void *pExtBuf)
{
    PNGIMAGE *pPage = (PNGIMAGE *)pPNG;
    PNGEXTERNALBUFFERS *pExt = (PNGEXTERNALBUFFERS *)pExtBuf;

    if (pPage == NULL || pExt == NULL || pPage->iPitch <= 0)
        return PNG_INVALID_PARAMETER;
    pExt->u32Magic = PNG_EXT_BUFFER_MAGIC;
    pExt->iLineBufferNeeded = PNGLineBufferSize(pPage->iPitch);
    pExt->iWorkspaceNeeded = PNGWorkspaceSize();
    pExt->iImageBufferNeeded = pPage->iHeight * pPage->iPitch;
    return PNG_SUCCESS;
} /* PNG_getExternalBufferSizes() */

