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
#ifndef __PNGDEC__
#define __PNGDEC__
#if defined( __MACH__ ) || defined( __LINUX__ ) || defined( __MCUXPRESSO )
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#define memcpy_P memcpy
#define PROGMEM
#else
#include <Arduino.h>
#endif
#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"
//
// PNG Decoder
// Written by Larry Bank
// Copyright (c) 2021 BitBank Software, Inc.
// 
// Designed to decode most PNG images (1-32 bpp)
// using less than 40K of RAM
//
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
/* Defines and variables */
#define PNG_FILE_BUF_SIZE 2048
// Number of bytes to reserve for current and previous lines
// Defaults to 320 32-bit pixels max width
// but can be overidden with a macro defined at compile time
#ifndef PNG_MAX_BUFFERED_PIXELS
#if defined( __LINUX__ ) || defined( __MACH__ )
#define PNG_MAX_BUFFERED_PIXELS 16386
#else
#define PNG_MAX_BUFFERED_PIXELS ((320*4 + 1)*2)
#endif
#endif
#ifndef PNG_COMMENT_SIZE
#define PNG_COMMENT_SIZE 128
#endif // PNG_COMMENT_SIZE

#ifndef __PNGENC__
// PNG filter type
enum {
    PNG_FILTER_NONE=0,
    PNG_FILTER_SUB,
    PNG_FILTER_UP,
    PNG_FILTER_AVG,
    PNG_FILTER_PAETH,
    PNG_FILTER_COUNT
};

// decode options
enum {
    PNG_CHECK_CRC = 1,
    PNG_FAST_PALETTE = 2
};

// source pixel type
enum {
  PNG_PIXEL_GRAYSCALE=0,
    PNG_PIXEL_TRUECOLOR=2,
    PNG_PIXEL_INDEXED=3,
    PNG_PIXEL_GRAY_ALPHA=4,
    PNG_PIXEL_TRUECOLOR_ALPHA=6
};

// Error codes returned by getLastError()
enum {
    PNG_SUCCESS = 0,
    PNG_INVALID_PARAMETER,
    PNG_DECODE_ERROR,
    PNG_MEM_ERROR,
    PNG_NO_BUFFER,
    PNG_UNSUPPORTED_FEATURE,
    PNG_INVALID_FILE,
    PNG_TOO_BIG,
    PNG_QUIT_EARLY
};

typedef struct png_file_tag
{
  int32_t iPos; // current file position
  int32_t iSize; // file size
  uint8_t *pData; // memory file pointer
  void * fHandle; // class pointer to File/SdFat or whatever you want
} PNGFILE;

#endif // !__PNGENC__

// RGB565 endianness
enum {
    PNG_RGB565_LITTLE_ENDIAN = 0,
    PNG_RGB565_BIG_ENDIAN
};

// Option flags for getLineAsRGBA8888() / decodeRGBA8888()
enum {
    PNG_RGBA8888_PREMULTIPLY = 1 // premultiply the color channels by the alpha channel
};

enum {
    PNG_MEM_RAM=0,
    PNG_MEM_FLASH
};

typedef struct png_draw_tag
{
    int y; // starting x,y of this line
    int iWidth; // size of this line
    int iPitch; // bytes per line
    int iPixelType; // PNG pixel type (0,2,3,4,6)
    int iBpp; // bits per color stimulus
    int iHasAlpha; // flag indicating the presence of an alpha palette
    void *pUser; // user supplied pointer
    uint8_t *pPalette;
    uint16_t *pFastPalette;
    uint8_t *pPixels;
} PNGDRAW;

// Callback function prototypes
typedef int32_t (PNG_READ_CALLBACK)(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
typedef int32_t (PNG_SEEK_CALLBACK)(PNGFILE *pFile, int32_t iPosition);
typedef void * (PNG_OPEN_CALLBACK)(const char *szFilename, int32_t *pFileSize);
typedef int (PNG_DRAW_CALLBACK)(PNGDRAW *);
typedef void (PNG_CLOSE_CALLBACK)(void *pHandle);

//
// our private structure to hold a JPEG image decode state
//
typedef struct png_image_tag
{
    int iWidth, iHeight; // image size
    uint8_t ucBpp, ucPixelType;
    uint8_t ucMemType;
    uint8_t *pImage;
    int iPitch; // bytes per line
    int iHasAlpha;
    int iInterlaced;
    uint32_t iTransparent; // transparent color index/value
    int iError;
    PNG_READ_CALLBACK *pfnRead;
    PNG_SEEK_CALLBACK *pfnSeek;
    PNG_OPEN_CALLBACK *pfnOpen;
    PNG_DRAW_CALLBACK *pfnDraw;
    PNG_CLOSE_CALLBACK *pfnClose;
    PNGFILE PNGFile;
    uint8_t ucZLIB[32768 + sizeof(struct inflate_state)]; // put this here to avoid needing malloc/free
    uint8_t ucPalette[1024];
    uint8_t ucPixels[PNG_MAX_BUFFERED_PIXELS];
    uint8_t ucFileBuf[PNG_FILE_BUF_SIZE]; // holds temp file data
    char szComment[PNG_COMMENT_SIZE];
} PNGIMAGE;

#ifdef __cplusplus
#define PNG_STATIC static
//
// The PNG class wraps portable C code which does the actual work
//
class PNG
{
  public:
    int openRAM(uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw);
    int openFLASH(uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw);
    int open(const char *szFilename, PNG_OPEN_CALLBACK *pfnOpen, PNG_CLOSE_CALLBACK *pfnClose, PNG_READ_CALLBACK *pfnRead, PNG_SEEK_CALLBACK *pfnSeek, PNG_DRAW_CALLBACK *pfnDraw);
    void close();
    int decode(void *pUser, int iOptions);
    int getWidth();
    int getHeight();
    int getBpp();
    int hasAlpha();
    uint32_t getTransparentColor();
    int isInterlaced();
    uint8_t * getPalette();
    int getPixelType();
    int getLastError();
    int getBufferSize();
    char *getComment();
    uint8_t *getBuffer();
    void setBuffer(uint8_t *pBuffer);
    uint8_t getAlphaMask(PNGDRAW *pDraw, uint8_t *pMask, uint8_t ucThreshold);
    void getLineAsRGB565(PNGDRAW *pDraw, uint16_t *pPixels, int iEndianness, uint32_t u32Bkgd);
    // Convert a line of native pixels (all supported types) into RGBA8888
    // (byte order R,G,B,A in memory). iFlags: 0 or PNG_RGBA8888_PREMULTIPLY
    int getLineAsRGBA8888(PNGDRAW *pDraw, uint32_t *pPixels, int iFlags);
    // Decode the image (or only the rows [yStart, yStart+yCount)) to RGBA8888.
    // pOut must point to yCount * getWidth() uint32_t pixels. Decoding stops
    // after the last requested row, so trailing rows cost no time nor memory.
    int decodeRGBA8888(uint32_t *pOut, int yStart, int yCount, int iFlags);

  private:
    typedef struct rgba8888_ctx_tag {
        PNG *pPNG;
        uint32_t *pOut;
        int yStart, yCount, iFlags;
    } RGBA8888CTX;
    static int RGBA8888Draw(PNGDRAW *pDraw);
    static void PremultiplyRGBA8888(uint32_t *pPixels, int iWidth);
    PNGIMAGE _png;
};
//
// Premultiply a line of RGBA8888 pixels by its alpha channel
//
inline void PNG::PremultiplyRGBA8888(uint32_t *pPixels, int iWidth)
{
    for (int x = 0; x < iWidth; x++) {
        uint32_t u32 = pPixels[x];
        uint32_t a = u32 >> 24;
        if (a != 255) {
            uint32_t r = (((u32 & 0xff) * a) + 127) / 255;
            uint32_t g = ((((u32 >> 8) & 0xff) * a) + 127) / 255;
            uint32_t b = ((((u32 >> 16) & 0xff) * a) + 127) / 255;
            pPixels[x] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
} /* PremultiplyRGBA8888() */
//
// Convert a line of native PNG pixels (all supported types) into RGBA8888
// Each output uint32_t holds R,G,B,A in memory byte order (0xAABBGGRR)
//
inline int PNG::getLineAsRGBA8888(PNGDRAW *pDraw, uint32_t *pPixels, int iFlags)
{
    int x, j;
    uint8_t c=0, a, *pPal, *s;
    uint32_t *pDest = pPixels;

    if (pDraw == NULL || pPixels == NULL || pDraw->pPixels == NULL)
        return PNG_INVALID_PARAMETER;
    if (iFlags & ~PNG_RGBA8888_PREMULTIPLY) // unknown option bits
        return PNG_INVALID_PARAMETER;
    s = pDraw->pPixels;
    switch (pDraw->iPixelType) {
        case PNG_PIXEL_GRAYSCALE:
            switch (pDraw->iBpp) {
                case 8:
                    for (x=0; x<pDraw->iWidth; x++) {
                        c = *s++;
                        *pDest++ = 0xff000000 | (c * 0x010101);
                    }
                    break;
                case 4:
                    for (x=0; x<pDraw->iWidth; x++) {
                        if ((x & 1) == 0)
                            c = *s++;
                        j = (x & 1) ? ((c & 0xf) | ((c << 4) & 0xf0)) : ((c & 0xf0) | (c >> 4));
                        *pDest++ = 0xff000000 | (j * 0x010101);
                    }
                    break;
                case 2:
                    for (x=0; x<pDraw->iWidth; x++) {
                        if ((x & 3) == 0)
                            c = *s++;
                        j = (c >> 6) * 0x55;
                        *pDest++ = 0xff000000 | (j * 0x010101);
                        c <<= 2;
                    }
                    break;
                case 1:
                    for (x=0; x<pDraw->iWidth; x++) {
                        if ((x & 7) == 0)
                            c = *s++;
                        j = (c & 0x80) ? 0xff : 0;
                        *pDest++ = 0xff000000 | (j * 0x010101);
                        c <<= 1;
                    }
                    break;
                default:
                    return PNG_UNSUPPORTED_FEATURE;
            } // switch on bpp
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            for (x=0; x<pDraw->iWidth; x++) {
                c = *s++; // gray level
                a = *s++; // alpha
                *pDest++ = ((uint32_t)a << 24) | (c * 0x010101);
            }
            break;
        case PNG_PIXEL_TRUECOLOR:
            for (x=0; x<pDraw->iWidth; x++) {
                *pDest++ = 0xff000000 | s[0] | (s[1] << 8) | (s[2] << 16);
                s += 3;
            }
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            for (x=0; x<pDraw->iWidth; x++) {
                *pDest++ = s[0] | (s[1] << 8) | (s[2] << 16) | ((uint32_t)s[3] << 24);
                s += 4;
            }
            break;
        case PNG_PIXEL_INDEXED: // palette color (1/2/4/8 bits per pixel)
            for (x=0; x<pDraw->iWidth; x++) {
                switch (pDraw->iBpp) {
                    case 8:
                        j = *s++;
                        break;
                    case 4:
                        if ((x & 1) == 0)
                            c = *s++;
                        j = (x & 1) ? (c & 0xf) : (c >> 4);
                        break;
                    case 2:
                        if ((x & 3) == 0)
                            c = *s++;
                        j = c >> 6;
                        c <<= 2;
                        break;
                    case 1:
                        if ((x & 7) == 0)
                            c = *s++;
                        j = c >> 7;
                        c <<= 1;
                        break;
                    default:
                        return PNG_UNSUPPORTED_FEATURE;
                }
                pPal = &pDraw->pPalette[j * 3];
                a = (pDraw->iHasAlpha) ? pDraw->pPalette[768 + j] : 0xff;
                *pDest++ = ((uint32_t)a << 24) | pPal[0] | (pPal[1] << 8) | (pPal[2] << 16);
            }
            break;
        default:
            return PNG_UNSUPPORTED_FEATURE;
    } // switch on pixel type
    if (iFlags & PNG_RGBA8888_PREMULTIPLY)
        PremultiplyRGBA8888(pPixels, pDraw->iWidth);
    return PNG_SUCCESS;
} /* getLineAsRGBA8888() */
//
// Internal draw callback for decodeRGBA8888()
// Skips rows outside the requested range and stops the decode early
// once the last requested row has been delivered
//
inline int PNG::RGBA8888Draw(PNGDRAW *pDraw)
{
    RGBA8888CTX *pCtx = (RGBA8888CTX *)pDraw->pUser;
    PNG *pPNG = pCtx->pPNG;
    int y = pDraw->y;
    int x;
    uint32_t *pDest;

    if (y < pCtx->yStart)
        return 1; // skip rows above the requested range
    if (y >= pCtx->yStart + pCtx->yCount)
        return 0; // past the requested range: stop decoding early
    pDest = &pCtx->pOut[(y - pCtx->yStart) * pDraw->iWidth];
    pPNG->getLineAsRGBA8888(pDraw, pDest, pCtx->iFlags & ~PNG_RGBA8888_PREMULTIPLY);
    // Apply tRNS transparency for grayscale and truecolor images; the
    // transparent color is not part of the PNGDRAW structure
    if (pPNG->_png.iHasAlpha && pPNG->_png.ucBpp == 8) {
        if (pPNG->_png.ucPixelType == PNG_PIXEL_GRAYSCALE) {
            uint32_t u32T = pPNG->_png.iTransparent & 0xff;
            for (x = 0; x < pDraw->iWidth; x++) {
                if ((pDest[x] & 0xff) == u32T)
                    pDest[x] &= 0x00ffffff; // transparent pixel
            }
        } else if (pPNG->_png.ucPixelType == PNG_PIXEL_TRUECOLOR) {
            uint32_t u32T = ((pPNG->_png.iTransparent >> 16) & 0xff) | // R
                            (pPNG->_png.iTransparent & 0xff00) |         // G
                            ((pPNG->_png.iTransparent & 0xff) << 16);    // B
            for (x = 0; x < pDraw->iWidth; x++) {
                if ((pDest[x] & 0xffffff) == u32T)
                    pDest[x] &= 0x00ffffff; // transparent pixel
            }
        }
    }
    if (pCtx->iFlags & PNG_RGBA8888_PREMULTIPLY)
        PremultiplyRGBA8888(pDest, pDraw->iWidth);
    return 1;
} /* RGBA8888Draw() */
//
// Decode the image (or a range of its rows) directly into RGBA8888 pixels
// The three stages (RGBA8888 conversion, premultiply, row range) are
// controlled independently: conversion is always on, premultiply is an
// iFlags bit, and the row range is set by yStart/yCount
//
inline int PNG::decodeRGBA8888(uint32_t *pOut, int yStart, int yCount, int iFlags)
{
    RGBA8888CTX ctx;
    PNG_DRAW_CALLBACK *pfnOld;
    int rc;

    if (pOut == NULL || yStart < 0 || yCount <= 0 || yStart >= _png.iHeight) {
        _png.iError = PNG_INVALID_PARAMETER;
        return PNG_INVALID_PARAMETER;
    }
    if (iFlags & ~PNG_RGBA8888_PREMULTIPLY) { // unknown option bits
        _png.iError = PNG_INVALID_PARAMETER;
        return PNG_INVALID_PARAMETER;
    }
    if (_png.pImage != NULL) { // incompatible with setBuffer() whole-image mode
        _png.iError = PNG_INVALID_PARAMETER;
        return PNG_INVALID_PARAMETER;
    }
    if (yStart + yCount > _png.iHeight)
        yCount = _png.iHeight - yStart; // clamp to the image bounds
    ctx.pPNG = this;
    ctx.pOut = pOut;
    ctx.yStart = yStart;
    ctx.yCount = yCount;
    ctx.iFlags = iFlags;
    pfnOld = _png.pfnDraw;
    _png.pfnDraw = RGBA8888Draw;
    rc = decode(&ctx, 0);
    _png.pfnDraw = pfnOld;
    if (rc == PNG_QUIT_EARLY) { // we intentionally stopped after the last row
        _png.iError = PNG_SUCCESS;
        rc = PNG_SUCCESS;
    }
    return rc;
} /* decodeRGBA8888() */
#else
#define PNG_STATIC
int PNG_openRAM(PNGIMAGE *pPNG, uint8_t *pData, int iDataSize, PNG_DRAW_CALLBACK *pfnDraw);
int PNG_openFile(PNGIMAGE *pPNG, const char *szFilename, PNG_DRAW_CALLBACK *pfnDraw);
int PNG_getWidth(PNGIMAGE *pPNG);
int PNG_getHeight(PNGIMAGE *pPNG);
int PNG_decode(PNGIMAGE *pPNG, void *pUser, int iOptions);
void PNG_close(PNGIMAGE *pPNG);
int PNG_getLastError(PNGIMAGE *pPNG);
int PNG_getBpp(PNGIMAGE *pPNG);
int PNG_getLastError(PNGIMAGE *pPNG);
int PNG_getBufferSize(PNGIMAGE *pPNG);
uint8_t *PNG_getPalette(PNGIMAGE *pPNG);
int PNG_getPixelType(PNGIMAGE *pPNG);
int PNG_hasAlpha(PNGIMAGE *pPNG);
int PNG_isInterlaced(PNGIMAGE *pPNG);
uint8_t *PNG_getBuffer(PNGIMAGE *pPNG);
void PNG_setBuffer(PNGIMAGE *pPNG, uint8_t *pBuffer);
#endif // __cplusplus

// Due to unaligned memory causing an exception, we have to do these macros the slow way
#ifndef MOTOLONG
#define INTELSHORT(p) ((*p) + (*(p+1)<<8))
#define INTELLONG(p) ((*p) + (*(p+1)<<8) + (*(p+2)<<16) + (*(p+3)<<24))
#define MOTOSHORT(p) (((*(p))<<8) + (*(p+1)))
#define MOTOLONG(p) (((*p)<<24) + ((*(p+1))<<16) + ((*(p+2))<<8) + (*(p+3)))
#endif

// Must be a 32-bit target processor
#define REGISTER_WIDTH 32

#endif // __PNGDEC__
