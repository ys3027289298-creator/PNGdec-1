//
// limit_test.cpp - test harness for the PNGdec resource-limit feature
//
// usage: limit_test <file.png> [dump.raw]
//
// Limits are configured through the environment variables PNG_MAX_PIXELS,
// PNG_MAX_MEM and PNG_MAX_RATIO (0 disables a check). Prints the decode
// result, the library's rejection reason (via getComment()), peak RSS and
// elapsed time. If dump.raw is given and decoding succeeds, the raw
// decoded pixel buffer is written there for byte-for-byte comparison.
//
#include <PNGdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

static PNG png;

static const char *szErrName(int iErr)
{
    static const char *names[] = {
        "PNG_SUCCESS", "PNG_INVALID_PARAMETER", "PNG_DECODE_ERROR",
        "PNG_MEM_ERROR", "PNG_NO_BUFFER", "PNG_UNSUPPORTED_FEATURE",
        "PNG_INVALID_FILE", "PNG_TOO_BIG", "PNG_QUIT_EARLY"
    };
    if (iErr < 0 || iErr > 8) return "UNKNOWN";
    return names[iErr];
} /* szErrName() */

static long lPeakRSS(void)
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_maxrss; // KB on Linux
} /* lPeakRSS() */

static double dNow(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
} /* dNow() */

int main(int argc, char *argv[])
{
    FILE *f;
    uint8_t *pData, *pBuf;
    long lSize;
    int rc, iErr;
    double dT0, dT1;
    const char *szReason;

    if (argc < 2) {
        fprintf(stderr, "usage: limit_test <file.png> [dump.raw]\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    lSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    pData = (uint8_t *)malloc(lSize);
    if (fread(pData, 1, lSize, f) != (size_t)lSize) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    fclose(f);

    dT0 = dNow();
    rc = png.openRAM(pData, (int)lSize, NULL);
    iErr = png.getLastError();
    szReason = png.getComment();
    if (rc != PNG_SUCCESS) {
        dT1 = dNow();
        printf("RESULT file=%s stage=open rc=%d err=%d(%s) reason=\"%s\" peak_kb=%ld time_ms=%.3f\n",
               argv[1], rc, iErr, szErrName(iErr),
               szReason ? szReason : "", lPeakRSS(), dT1 - dT0);
        free(pData);
        return 0;
    }
    printf("SPECS file=%s w=%d h=%d bpp=%d type=%d bufsize=%d decoder_struct=%zu est_mem=%lld\n",
           argv[1], png.getWidth(), png.getHeight(), png.getBpp(),
           png.getPixelType(), png.getBufferSize(), sizeof(PNG),
           (long long)png.getBufferSize() + png.getHeight() + (long long)sizeof(PNG));

    pBuf = (uint8_t *)malloc(png.getBufferSize());
    png.setBuffer(pBuf);
    rc = png.decode(NULL, 0);
    dT1 = dNow();
    iErr = png.getLastError();
    szReason = png.getComment();
    printf("RESULT file=%s stage=decode rc=%d err=%d(%s) reason=\"%s\" peak_kb=%ld time_ms=%.3f\n",
           argv[1], rc, iErr, szErrName(iErr),
           szReason ? szReason : "", lPeakRSS(), dT1 - dT0);

    if (rc == PNG_SUCCESS && argc > 2) {
        f = fopen(argv[2], "wb");
        if (f) {
            fwrite(png.getBuffer(), 1, png.getBufferSize(), f);
            fclose(f);
        }
    }
    free(pBuf);
    free(pData);
    return 0;
} /* main() */
