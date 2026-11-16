//
// pull_decode - verify the caller-paced (pull) PNG decoding entry point
//
// Builds only against the repository sources; no external libraries.
// Four scenarios run in one process on tiny generated fixtures:
//
//   1. normal stream      - full decode driven entirely by the pull callback
//   2. short reads        - callback never returns more than N bytes
//   3. pause and resume   - callback reports "no data right now" periodically,
//                           so the decode state machine suspends and resumes
//   4. early end          - source reports EOF in the middle of IDAT
//
// For each scenario the demo prints the decode result, the number of pull
// callback invocations, and compares the decoded pixels byte for byte with
// the classic one-shot path (openRAM + setBuffer + decode) for the same
// file. Illegal state usage is checked separately.
//
// Usage: pull_decode [samples_dir]
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define __LINUX__
#define ZLIB_DEBUG
#define INFLATE_STRICT
#include "../../src/zutil.c"
#include "../../src/inflate.c"
#include "../../src/inffast.c"
#include "../../src/inftrees.c"
#include "../../src/crc32.c"
#undef DO1
#undef DO8
#include "../../src/adler32.c"
#include "../../src/PNGdec.cpp" // pulls in png.inl

static int iTotalPass, iTotalFail;

static void check(const char *szWhat, int bOK, const char *szDetail)
{
    printf("  %-56s %s%s%s\n", szWhat, bOK ? "PASS" : "FAIL",
           (szDetail && szDetail[0]) ? "  " : "",
           (szDetail && szDetail[0]) ? szDetail : "");
    if (bOK) iTotalPass++; else iTotalFail++;
}

// ---------------------------------------------------------------------------
// Source simulator
// ---------------------------------------------------------------------------
typedef struct
{
    const uint8_t *pData;
    int iSize;
    int iPos;
    int iShort;       // 0 = no cap, else max bytes returned per call
    int iPauseEvery;  // 0 = never, else return 0 bytes every Nth call
    int iPauseLen;    // consecutive 0-byte replies per pause
    int iPausesLeft;  // internal
    int iCalls;       // invocations observed
    int iZero;        // empty replies observed
    int iEndAt;       // <0: no early end; else report EOF at this position
    int bOverReport;  // report more bytes than the buffer allows
    int iMaxAsked;    // largest iMaxLen the decoder asked for
} SOURCE;

static int32_t PullSource(void *pUser, uint8_t *pBuffer, int32_t iMaxLen)
{
    SOURCE *pSrc = (SOURCE *)pUser;
    int32_t iGot;

    pSrc->iCalls++;
    if (iMaxLen > pSrc->iMaxAsked)
        pSrc->iMaxAsked = iMaxLen;
    if (pSrc->bOverReport)
        return iMaxLen + 1; // violates the callback contract on purpose
    if (pSrc->iPausesLeft > 0) {
        pSrc->iPausesLeft--;
        pSrc->iZero++;
        return 0;
    }
    if (pSrc->iPauseEvery > 0 && (pSrc->iCalls % pSrc->iPauseEvery) == 0) {
        pSrc->iPausesLeft = pSrc->iPauseLen - 1;
        pSrc->iZero++;
        return 0;
    }
    if (pSrc->iEndAt >= 0 && pSrc->iPos >= pSrc->iEndAt)
        return -1; // source failure: truncated input
    iGot = pSrc->iSize - pSrc->iPos;
    if (pSrc->iShort > 0 && iGot > pSrc->iShort)
        iGot = pSrc->iShort;
    if (iGot > iMaxLen)
        iGot = iMaxLen;
    if (iGot <= 0)
        return -1;
    memcpy(pBuffer, &pSrc->pData[pSrc->iPos], (size_t)iGot);
    pSrc->iPos += iGot;
    return iGot;
}

// ---------------------------------------------------------------------------
// One-shot reference through the existing (untouched) path
// ---------------------------------------------------------------------------
typedef struct
{
    int iWidth, iHeight, iPitch;
    uint8_t *pPixels; // iHeight * iPitch, rows tightly packed
} REFERENCE;

// Compute the row pitch exactly like PNGParseInfo() does
static int RowPitch(int iWidth, int iBpp, int iPixelType)
{
    int iChannels;

    switch (iPixelType) {
        case PNG_PIXEL_TRUECOLOR:       iChannels = 3; break;
        case PNG_PIXEL_TRUECOLOR_ALPHA: iChannels = 4; break;
        case PNG_PIXEL_GRAY_ALPHA:      iChannels = 2; break;
        default:                        iChannels = 1; break;
    }
    return (iWidth * iBpp * iChannels + 7) / 8;
}

static int RefDecode(const uint8_t *pData, int iSize, REFERENCE *pRef)
{
    static PNG png;
    int rc;

    memset(pRef, 0, sizeof(*pRef));
    rc = png.openRAM((uint8_t *)pData, iSize, NULL);
    if (rc != PNG_SUCCESS)
        return rc;
    pRef->iWidth = png.getWidth();
    pRef->iHeight = png.getHeight();
    pRef->iPitch = RowPitch(pRef->iWidth, png.getBpp(), png.getPixelType());
    pRef->pPixels = (uint8_t *)calloc((size_t)(pRef->iHeight * pRef->iPitch), 1);
    if (pRef->pPixels == NULL)
        return PNG_MEM_ERROR;
    png.setBuffer(pRef->pPixels);
    rc = png.decode(NULL, 0);
    png.close();
    return rc;
}

// ---------------------------------------------------------------------------
// Pull-mode run
// ---------------------------------------------------------------------------
typedef struct
{
    int iCalls;      // pull callback invocations
    int iZero;       // empty replies
    int iBytes;      // bytes handed over by the callback
    int iLines;      // lines delivered (draw callback mode)
    int bOrderOK;    // lines arrived in order, starting at y = 0
    int iSuspends;   // times decode returned PNG_PULL_PAUSED
} PULLRESULT;

typedef struct
{
    uint8_t *pPixels;
    int iPitch;
    int iHeight;
    int iNextY;
    int bOrderOK;
    PULLRESULT *pRes;
} DRAWCAP;

static int AbortDraw(PNGDRAW *pDraw)
{
    (void)pDraw;
    return 0; // caller no longer wants lines
}

static int PullDraw(PNGDRAW *pDraw)
{
    DRAWCAP *p = (DRAWCAP *)pDraw->pUser;

    if (pDraw->y != p->iNextY)
        p->bOrderOK = 0;
    p->iNextY = pDraw->y + 1;
    if (p->pPixels && pDraw->y >= 0 && pDraw->y < p->iHeight) {
        memcpy(&p->pPixels[pDraw->y * p->iPitch], pDraw->pPixels,
               (size_t)pDraw->iPitch);
        p->pRes->iLines++;
    }
    return 1;
}

//
// Drive one pull-mode decode.
//   pFrameBuf != NULL -> decode into that caller buffer (no draw callback)
//   pFrameBuf == NULL -> decode through a draw callback and copy the rows
// All output is written to *ppOut (malloc'd) unless pFrameBuf was supplied.
//
static int RunPull(const uint8_t *pData, int iSize,
                   int iBlockSize, int iShort, int iPauseEvery, int iPauseLen,
                   int iEndAt, int iMaxPauses, int iHeight, int iPitch,
                   uint8_t *pFrameBuf, uint8_t **ppOut, PULLRESULT *pRes)
{
    static PNGIMAGE image;
    static PNGPULLSTATE st;
    SOURCE src;
    DRAWCAP cap;
    int rc, iGuard;

    memset(pRes, 0, sizeof(*pRes));
    memset(&src, 0, sizeof(src));
    src.pData = pData;
    src.iSize = iSize;
    src.iShort = iShort;
    src.iPauseEvery = iPauseEvery;
    src.iPauseLen = iPauseLen;
    src.iEndAt = iEndAt;

    memset(&cap, 0, sizeof(cap));
    cap.pPixels = (pFrameBuf == NULL) ?
        (uint8_t *)calloc((size_t)(iHeight * iPitch), 1) : NULL;
    cap.iPitch = iPitch;
    cap.iHeight = iHeight;
    cap.bOrderOK = 1;
    cap.pRes = pRes;
    if (iPitch > 0 && pFrameBuf == NULL && cap.pPixels == NULL)
        return PNG_MEM_ERROR;

    rc = PNGPullInit(&image, &st, PullSource, &src,
                     (pFrameBuf == NULL) ? PullDraw : NULL,
                     iBlockSize, iMaxPauses);
    if (rc != PNG_SUCCESS) {
        free(cap.pPixels);
        return rc;
    }
    if (pFrameBuf)
        image.pImage = pFrameBuf;

    iGuard = 0;
    do {
        rc = PNGPullDecode(&image, &st, &cap, 0);
        if (rc == PNG_PULL_PAUSED)
            pRes->iSuspends++;
        if (++iGuard > 1000000) { // no runaway loops
            rc = PNG_PULL_INVALID_STATE;
            break;
        }
    } while (rc == PNG_PULL_PAUSED);

    PNGPullGetCounters(&st, &pRes->iCalls, &pRes->iZero, &pRes->iBytes);
    pRes->bOrderOK = cap.bOrderOK;
    if (pFrameBuf == NULL)
        *ppOut = cap.pPixels;
    else
        *ppOut = NULL;
    return rc;
}

static int SameBytes(const uint8_t *pA, const uint8_t *pB, int iLen)
{
    return (pA != NULL && pB != NULL && memcmp(pA, pB, (size_t)iLen) == 0);
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------
typedef struct
{
    const char *szFile;
    int iBlockSize;
    int iShort;
    int iPauseEvery;
    int iPauseLen;
} CASE;

static int g_iScenarioFail;

static void RunCase(const CASE *pCase, const char *szDir, const uint8_t *pData,
                    int iSize, const REFERENCE *pRef)
{
    char szPath[512];
    int iLen = pRef->iHeight * pRef->iPitch;
    char szDetail[160];
    int rc, bFrame, bDraw;

    (void)szPath;
    printf("  %-13s block=%-4d short=%-3d pause=1/%d+%d\n",
           pCase->szFile, pCase->iBlockSize, pCase->iShort,
           pCase->iPauseEvery, pCase->iPauseLen);

    // (a) output through a caller-supplied frame buffer
    {
        PULLRESULT res;
        uint8_t *pBuf = (uint8_t *)calloc((size_t)iLen, 1);
        uint8_t *pOut = NULL;

        rc = RunPull(pData, iSize, pCase->iBlockSize, pCase->iShort,
                     pCase->iPauseEvery, pCase->iPauseLen, -1, 0,
                     pRef->iHeight, pRef->iPitch, pBuf, &pOut, &res);
        snprintf(szDetail, sizeof(szDetail),
                 "rc=%d calls=%d zero=%d suspends=%d bytes=%d/%d",
                 rc, res.iCalls, res.iZero, res.iSuspends, res.iBytes, iSize);
        bFrame = (rc == PNG_SUCCESS && SameBytes(pBuf, pRef->pPixels, iLen));
        check("frame-buffer decode succeeds", rc == PNG_SUCCESS, szDetail);
        check("frame-buffer pixels match one-shot path", bFrame, "");
        printf("      pull callbacks: %d, empty replies: %d, suspends: %d\n",
               res.iCalls, res.iZero, res.iSuspends);
        free(pBuf);
    }

    // (b) output through the existing draw callback
    {
        PULLRESULT res;
        uint8_t *pOut = NULL;

        rc = RunPull(pData, iSize, pCase->iBlockSize, pCase->iShort,
                     pCase->iPauseEvery, pCase->iPauseLen, -1, 0,
                     pRef->iHeight, pRef->iPitch, NULL, &pOut, &res);
        snprintf(szDetail, sizeof(szDetail),
                 "rc=%d calls=%d zero=%d lines=%d/%d order=%s",
                 rc, res.iCalls, res.iZero, res.iLines, pRef->iHeight,
                 res.bOrderOK ? "ok" : "bad");
        bDraw = (rc == PNG_SUCCESS && res.iLines == pRef->iHeight &&
                 res.bOrderOK && SameBytes(pOut, pRef->pPixels, iLen));
        check("draw-callback decode matches one-shot path", bDraw, szDetail);
        printf("      pull callbacks: %d, empty replies: %d, suspends: %d\n",
               res.iCalls, res.iZero, res.iSuspends);
        free(pOut);
    }
    if (!bFrame || !bDraw)
        g_iScenarioFail++;
}

int main(int argc, char *argv[])
{
    const char *szDir = (argc > 1) ? argv[1] : "samples";
    const char *szFiles[] = { "rgba.png", "rgb.png", "indexed8.png",
                              "gray8.png", "indexed4.png", "multi_idat.png" };
    int i, n;
    char szPath[512];

    printf("PNGdec pull-mode decoding demo\n\n");

    printf("== fixtures ==\n");
    for (i = 0; i < (int)(sizeof(szFiles) / sizeof(szFiles[0])); i++) {
        uint8_t *pData = NULL;
        int iSize = 0;
        REFERENCE ref;
        FILE *f;

        snprintf(szPath, sizeof(szPath), "%s/%s", szDir, szFiles[i]);
        f = fopen(szPath, "rb");
        if (f == NULL) {
            printf("  missing fixture: %s (run gen_samples.py first)\n", szPath);
            iTotalFail++;
            continue;
        }
        fclose(f);
        // LoadFile
        f = fopen(szPath, "rb");
        fseek(f, 0, SEEK_END);
        iSize = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        pData = (uint8_t *)malloc((size_t)iSize);
        if (pData == NULL || fread(pData, 1, (size_t)iSize, f) != (size_t)iSize) {
            fclose(f);
            free(pData);
            iTotalFail++;
            continue;
        }
        fclose(f);

        if (RefDecode(pData, iSize, &ref) != PNG_SUCCESS) {
            printf("  %-14s reference decode failed\n", szFiles[i]);
            iTotalFail++;
            free(pData);
            continue;
        }
        printf("  %-14s %4d bytes  %dx%d  pitch %d  (one-shot reference ok)\n",
               szFiles[i], iSize, ref.iWidth, ref.iHeight, ref.iPitch);

        printf("\n== scenario 1: normal stream ==\n");
        {
            CASE c = { szFiles[i], 512, 0, 0, 0 };
            RunCase(&c, szDir, pData, iSize, &ref);
        }
        printf("\n== scenario 2: short reads ==\n");
        {
            CASE c = { szFiles[i], 64, 17, 0, 0 };
            RunCase(&c, szDir, pData, iSize, &ref);
        }
        printf("\n== scenario 3: pause and resume ==\n");
        {
            CASE c = { szFiles[i], 32, 0, 3, 2 };
            RunCase(&c, szDir, pData, iSize, &ref);
        }
        printf("\n== scenario 3b: pause every block, 1 byte at a time ==\n");
        {
            CASE c = { szFiles[i], 1, 0, 2, 1 };
            RunCase(&c, szDir, pData, iSize, &ref);
        }
        printf("\n== scenario 4: early end ==\n");
        {
            PULLRESULT res;
            uint8_t *pBuf = (uint8_t *)calloc((size_t)(ref.iHeight * ref.iPitch), 1);
            uint8_t *pOut = NULL;
            int iCut = iSize / 2;
            char szDetail[128];
            int rc = RunPull(pData, iSize, 64, 0, 0, 0, iCut, 0,
                             ref.iHeight, ref.iPitch, pBuf, &pOut, &res);

            snprintf(szDetail, sizeof(szDetail),
                     "cut=%d rc=%d calls=%d", iCut, rc, res.iCalls);
            check("truncated source reports PNG_DECODE_ERROR",
                  rc == PNG_DECODE_ERROR, szDetail);
            check("no complete image is claimed", res.iLines < ref.iHeight, "");
            printf("      pull callbacks: %d\n", res.iCalls);
            free(pBuf);
        }
        printf("\n== scenario 4b: caller ends early (draw callback aborts) ==\n");
        {
            static PNGIMAGE image2;
            static PNGPULLSTATE st2;
            SOURCE src;
            DRAWCAP cap;
            PULLRESULT res2;
            int rc;

            memset(&res2, 0, sizeof(res2));
            memset(&src, 0, sizeof(src));
            src.pData = pData;
            src.iSize = iSize;
            src.iEndAt = -1;
            memset(&cap, 0, sizeof(cap));
            cap.bOrderOK = 1;
            cap.pRes = &res2;
            rc = PNGPullInit(&image2, &st2, PullSource, &src, AbortDraw, 32, 0);
            do {
                rc = PNGPullDecode(&image2, &st2, &cap, 0);
            } while (rc == PNG_PULL_PAUSED);
            check("draw callback abort -> PNG_QUIT_EARLY",
                  rc == PNG_QUIT_EARLY, "");
            check("decode stays stopped after abort",
                  PNGPullDecode(&image2, &st2, &cap, 0) ==
                  PNG_PULL_INVALID_STATE, "");
        }

        free(ref.pPixels);
        free(pData);
        n = 0;
        (void)n;
    }

    // -----------------------------------------------------------------------
    // Illegal / out-of-order use
    // -----------------------------------------------------------------------
    printf("\n== illegal state checks ==\n");
    {
        PNGIMAGE image;
        PNGPULLSTATE st;
        SOURCE src;
        uint8_t *pData = NULL;
        int iSize = 0;
        REFERENCE ref;
        FILE *f;
        int rc;

        snprintf(szPath, sizeof(szPath), "%s/%s", szDir, "rgb.png");
        f = fopen(szPath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            iSize = (int)ftell(f);
            fseek(f, 0, SEEK_SET);
            pData = (uint8_t *)malloc((size_t)iSize);
            if (pData) {
                size_t got = fread(pData, 1, (size_t)iSize, f);
                (void)got;
            }
            fclose(f);
        }
        if (pData == NULL || RefDecode(pData, iSize, &ref) != PNG_SUCCESS) {
            printf("  unable to load fixture for illegal-state checks\n");
            iTotalFail++;
        } else {
            memset(&src, 0, sizeof(src));
            src.pData = pData;
            src.iSize = iSize;
            src.iEndAt = -1;

            // decode before init (magic intact but callback NULL)
            memset(&image, 0, sizeof(image));
            memset(&st, 0, sizeof(st));
            rc = PNGPullDecode(&image, &st, NULL, 0);
            check("decode before init -> PNG_PULL_INVALID_STATE",
                  rc == PNG_PULL_INVALID_STATE, "");

            // init with a NULL callback
            rc = PNGPullInit(&image, &st, NULL, &src, NULL, 64, 4);
            check("init with NULL callback -> PNG_INVALID_PARAMETER",
                  rc == PNG_INVALID_PARAMETER, "");

            // invalid block sizes
            check("init block size 0 -> PNG_INVALID_PARAMETER",
                  PNGPullInit(&image, &st, PullSource, &src, NULL, 0, 4) ==
                  PNG_INVALID_PARAMETER, "");
            check("init block size too large -> PNG_INVALID_PARAMETER",
                  PNGPullInit(&image, &st, PullSource, &src, NULL,
                              PNG_FILE_BUF_SIZE + 1, 4) ==
                  PNG_INVALID_PARAMETER, "");
            check("init negative pause budget -> PNG_INVALID_PARAMETER",
                  PNGPullInit(&image, &st, PullSource, &src, NULL, 64, -1) ==
                  PNG_INVALID_PARAMETER, "");

            // valid init, then setter validation and no-buffer decode
            rc = PNGPullInit(&image, &st, PullSource, &src, NULL, 64, 4);
            check("valid init -> PNG_SUCCESS", rc == PNG_SUCCESS, "");
            check("set block size 0 -> PNG_INVALID_PARAMETER",
                  PNGPullSetBlockSize(&st, 0) == PNG_INVALID_PARAMETER, "");
            check("set max pauses < 0 -> PNG_INVALID_PARAMETER",
                  PNGPullSetMaxPauses(&st, -1) == PNG_INVALID_PARAMETER, "");
            check("set block size on uninitialised state -> INVALID_STATE",
                  PNGPullSetBlockSize(NULL, 64) == PNG_PULL_INVALID_STATE, "");
            rc = PNGPullDecode(&image, &st, NULL, 0);
            check("decode without buffer or draw callback -> PNG_NO_BUFFER",
                  rc == PNG_NO_BUFFER, "");

            // a state must not be used with a different image instance
            {
                PNGIMAGE other;
                PNGPULLSTATE stOther;
                PNGPullInit(&other, &stOther, PullSource, &src, NULL, 64, 0);
                other.pImage = image.pImage; // still NULL here
                check("state bound to another image -> PNG_PULL_INVALID_STATE",
                      PNGPullDecode(&image, &stOther, NULL, 0) ==
                      PNG_PULL_INVALID_STATE, "");
            }

            // out-of-order: install a buffer, then finish and decode again
            image.pImage = (uint8_t *)calloc((size_t)(ref.iHeight * ref.iPitch), 1);
            rc = PNGPullDecode(&image, &st, NULL, 0);
            while (rc == PNG_PULL_PAUSED)
                rc = PNGPullDecode(&image, &st, NULL, 0);
            check("first decode completes -> PNG_SUCCESS", rc == PNG_SUCCESS, "");
            free(image.pImage);
            rc = PNGPullDecode(&image, &st, NULL, 0);
            check("decode after completion -> PNG_PULL_INVALID_STATE",
                  rc == PNG_PULL_INVALID_STATE, "");

            // pause budget: every call returns 0 bytes, budget 3
            src.iPos = 0;
            src.iCalls = 0;
            src.iZero = 0;
            src.iPausesLeft = 0;
            src.iEndAt = -1;
            src.iPauseEvery = 1;
            src.iPauseLen = 1;
            rc = PNGPullInit(&image, &st, PullSource, &src, NULL, 64, 3);
            image.pImage = (uint8_t *)calloc((size_t)(ref.iHeight * ref.iPitch), 1);
            do {
                rc = PNGPullDecode(&image, &st, NULL, 0);
            } while (rc == PNG_PULL_PAUSED);
            check("pause budget exceeded -> PNG_PULL_PAUSE_LIMIT",
                  rc == PNG_PULL_PAUSE_LIMIT, "");
            free(image.pImage);

            // callback over-reports its block size
            memset(&src, 0, sizeof(src));
            src.pData = pData;
            src.iSize = iSize;
            src.iEndAt = -1;
            src.bOverReport = 1;
            PNGPullInit(&image, &st, PullSource, &src, NULL, 64, 0);
            image.pImage = (uint8_t *)calloc((size_t)(ref.iHeight * ref.iPitch), 1);
            rc = PNGPullDecode(&image, &st, NULL, 0);
            check("callback over-reports block -> PNG_PULL_BAD_BLOCK",
                  rc == PNG_PULL_BAD_BLOCK, "");

            free(image.pImage);
            free(ref.pPixels);
            free(pData);
        }
    }

    printf("\nTotal checks: %d passed, %d failed\n", iTotalPass, iTotalFail);
    return (iTotalFail == 0) ? 0 : 1;
}
