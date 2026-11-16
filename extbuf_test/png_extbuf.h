//
// png_extbuf.h - consumer side declarations for PNGdec's external buffer mode
//
// This header is test/demo scaffolding, not part of the library. The decoder
// itself (src/png.inl) owns the implementation; it selects external buffer
// mode when decode() receives a PNGEXTERNALBUFFERS pointer whose magic field
// equals PNG_EXT_BUFFER_MAGIC through its pUser argument. No existing public
// function signature changes.
//
#ifndef __PNG_EXTBUF_H__
#define __PNG_EXTBUF_H__
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PNG_EXT_BUFFER_MAGIC 0x504e4758u /* 'PNGX' */

/* keep in sync with png.inl's png_external_buffers_tag.
 * When png.inl is included directly (plain-C build model) the types and the
 * query function already exist, so don't redefine them here.
 */
#ifndef PNG_EXT_BUFFER_TYPES_DEFINED
typedef struct png_external_buffers_tag
{
    uint32_t u32Magic;
    uint8_t *pLineBuffer;
    int iLineBufferSize;
    uint8_t *pWorkspace;
    int iWorkspaceSize;
    void *pUser;
    /* output fields */
    int iLineBufferNeeded;
    int iWorkspaceNeeded;
    int iImageBufferNeeded;
} PNGEXTERNALBUFFERS;

/*
 * Fill in the minimum buffer sizes for the image currently open in pPNG.
 * pPNG is a void* so the same declaration works against the C++ PNG class or
 * the C PNGIMAGE type without touching the public header.
 * Returns PNG_SUCCESS (0) or PNG_INVALID_PARAMETER.
 */
int PNG_getExternalBufferSizes(void *pPNG, PNGEXTERNALBUFFERS *pExt);
#endif /* PNG_EXT_BUFFER_TYPES_DEFINED */

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "PNGdec.h"
/*
 * Run a decode entirely from caller supplied buffers.
 * The PNG class signature is unchanged; the descriptor rides in pUser.
 */
inline int PNG_decodeExternal(PNG &png, PNGEXTERNALBUFFERS *pExt, int iOptions)
{
    pExt->u32Magic = PNG_EXT_BUFFER_MAGIC;
    return png.decode(pExt, iOptions);
}
/* query minimum sizes through the C++ object */
inline int PNG_getExternalBufferSizes(PNG &png, PNGEXTERNALBUFFERS *pExt)
{
    pExt->u32Magic = PNG_EXT_BUFFER_MAGIC;
    return PNG_getExternalBufferSizes((void *)&png, pExt);
}
#endif
#endif // __PNG_EXTBUF_H__
