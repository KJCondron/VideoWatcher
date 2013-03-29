//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include "define_options.h"
#include "common_utils.h"


// =================================================================
// FFmpeg container handling functionality
//  - Container muxing/demuxing support
//  - Supports audio stream remuxing (no transcode!) in case PROCESS_AUDIO is selected
//

// Demuxer entrypoints
mfxStatus	FFmpeg_Reader_Init(const char *strFileName, mfxU32 videoType);
mfxStatus	FFmpeg_Reader_ReadNextFrame(mfxBitstream *pBS);
void		FFmpeg_Reader_Reset();
mfxStatus	FFmpeg_Reader_Close();

// Utility
mfxStatus	FFmpeg_Reader_Get_Framerate(mfxU32 *pFrameRateN, mfxU32 *pFrameRateD);

// Muxer entrypoints
mfxStatus	FFmpeg_Writer_Init(	const char *strFileName,
                                mfxU32 videoType,
                                mfxU16 nBitRate,
                                mfxU16 nDstWidth,
                                mfxU16 nDstHeight,
                                mfxU16 GopRefDist,
                                mfxU8* SPSbuf,
                                int SPSbufsize,
                                mfxU8* PPSbuf,
                                int PPSbufsize);
mfxStatus	FFmpeg_Writer_WriteNextFrame(mfxBitstream *pMfxBitstream);
mfxStatus	FFmpeg_Writer_Close();

