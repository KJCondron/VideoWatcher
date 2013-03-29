//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#pragma once 

#include <stdio.h>
#include <windows.h>

#include "mfxvideo++.h"

#define MSDK_PRINT_RET_MSG(ERR)			{printf("\nReturn on error: error code %d,\t%s\t%d\n\n", ERR, __FILE__, __LINE__);}
#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_POINTER(P, ERR)      {if (!(P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_IGNORE_MFX_STS(P, X)       {if ((X) == (P)) {P = MFX_ERR_NONE;}}
#define MSDK_BREAK_ON_ERROR(P)          {if (MFX_ERR_NONE != (P)) break;}
#define MSDK_SAFE_DELETE_ARRAY(P)       {if (P) {delete[] P; P = NULL;}}
#define MSDK_ALIGN32(X)					(((mfxU32)((X)+31)) & (~ (mfxU32)31))
#define MSDK_ALIGN16(value)             (((value + 15) >> 4) << 4)
#define MSDK_SAFE_RELEASE(X)            {if (X) { X->Release(); X = NULL; }}
#define MSDK_MAX(A, B)			        (((A) > (B)) ? (A) : (B))


// =================================================================
// Utility functions, not directly tied to Media SDK functionality
//

// LoadRawFrame: Reads raw frame from YUV file (YV12) into NV12 surface
// - YV12 is a more common format for for YUV files than NV12 (therefore the conversion during read and write)
// - For the simulation case, the surface is filled with default image data
// LoadRawRGBFrame: Reads raw RGB32 frames from file into RGB32 surface
// - For the simulation case, the surface is filled with default image data
#ifdef ENABLE_INPUT
    mfxStatus LoadRawFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = false);
    mfxStatus LoadRawRGBFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = false);
#else
    mfxStatus LoadRawFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = true);
    mfxStatus LoadRawRGBFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = true);
#endif
// Write raw YUV (NV12) surface to YUV (YV12) file
mfxStatus WriteRawFrame(mfxFrameSurface1 *pSurface, FILE* fSink, FILE* fdebug = 0);

// Write bit stream data for frame to file
mfxStatus WriteBitStreamFrame(mfxBitstream *pMfxBitstream, FILE* fSink);
// Read bit stream data from file. Stream is read as large chunks (= many frames)
mfxStatus ReadBitStreamData(mfxBitstream *pBS, FILE* fSource);


