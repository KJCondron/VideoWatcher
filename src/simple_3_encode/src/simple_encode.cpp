//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT    // Disabling this flag removes printing of progress (saves CPU cycles)
#define ENABLE_INPUT     // Disabling this flag removes all YUV file reading. Replaced by pre-initialized surface data. Workload runs for 1000 frames
#define ENABLE_BENCHMARK

#include "common_utils.h"

// Get free raw frame surface
int GetFreeSurfaceIndex(mfxFrameSurface1** pSurfacesPool, mfxU16 nPoolSize)
{    
    if (pSurfacesPool)
        for (mfxU16 i = 0; i < nPoolSize; i++)
            if (0 == pSurfacesPool[i]->Data.Locked)
                return i;
    return MFX_ERR_NOT_FOUND;
}

int main()
{  
    mfxStatus sts = MFX_ERR_NONE;

    mfxU16 inputWidth = 1920;
    mfxU16 inputHeight = 1080;

    // =====================================================================
    // Intel Media SDK encode pipeline setup
    // - In this example we are encoding an AVC (H.264) stream
    // - For simplistic memory management, system memory surfaces are used to store the raw frames
    //   (Note that when using HW acceleration D3D surfaces are prefered, for better performance)
    //

    // Open input YV12 YUV file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.yuv", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "test.264", "wb");
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    // Initialize Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    // - Version 1.0 is selected for greatest backwards compatibility.
    //   If more recent API features are needed, change the version accordingly
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = {0, 1};
    MFXVideoSession mfxSession;
    sts = mfxSession.Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Initialize encoder parameters
    mfxVideoParam mfxEncParams;
    memset(&mfxEncParams, 0, sizeof(mfxEncParams));
    mfxEncParams.mfx.CodecId                    = MFX_CODEC_AVC;
    mfxEncParams.mfx.TargetUsage                = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams.mfx.TargetKbps                 = 2000;
    mfxEncParams.mfx.RateControlMethod          = MFX_RATECONTROL_VBR; 
    mfxEncParams.mfx.FrameInfo.FrameRateExtN    = 30;
    mfxEncParams.mfx.FrameInfo.FrameRateExtD    = 1;
    mfxEncParams.mfx.FrameInfo.FourCC           = MFX_FOURCC_NV12;
    mfxEncParams.mfx.FrameInfo.ChromaFormat     = MFX_CHROMAFORMAT_YUV420;
    mfxEncParams.mfx.FrameInfo.PicStruct        = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams.mfx.FrameInfo.CropX            = 0; 
    mfxEncParams.mfx.FrameInfo.CropY            = 0;
    mfxEncParams.mfx.FrameInfo.CropW            = inputWidth;
    mfxEncParams.mfx.FrameInfo.CropH            = inputHeight;
    // Width must be a multiple of 16 
    // Height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    mfxEncParams.mfx.FrameInfo.Width  = MSDK_ALIGN16(inputWidth);
    mfxEncParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct)?
        MSDK_ALIGN16(inputHeight) : MSDK_ALIGN32(inputHeight);
    
    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    
    // Create Media SDK encoder
    MFXVideoENCODE mfxENC(mfxSession); 

    // Validate video encode parameters (optional)
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching the requested configuration
    sts = mfxENC.Query(&mfxEncParams, &mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM); 
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC.QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

    mfxU16 nEncSurfNum = EncRequest.NumFrameSuggested;

    // Allocate surfaces for encoder
    // - Width and height of buffer must be aligned, a multiple of 32 
    // - Frame surface array keeps pointers all surface planes and general frame info
    mfxU16 width = (mfxU16)MSDK_ALIGN32(EncRequest.Info.Width);
    mfxU16 height = (mfxU16)MSDK_ALIGN32(EncRequest.Info.Height);
    mfxU8  bitsPerPixel = 12;  // NV12 format is a 12 bits per pixel format
    mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffers = (mfxU8 *)new mfxU8[surfaceSize * nEncSurfNum];

    mfxFrameSurface1** pEncSurfaces = new mfxFrameSurface1*[nEncSurfNum];
    MSDK_CHECK_POINTER(pEncSurfaces, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nEncSurfNum; i++)
    {       
        pEncSurfaces[i] = new mfxFrameSurface1;
        memset(pEncSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pEncSurfaces[i]->Info), &(mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pEncSurfaces[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
        pEncSurfaces[i]->Data.U = pEncSurfaces[i]->Data.Y + width * height;
        pEncSurfaces[i]->Data.V = pEncSurfaces[i]->Data.U + 1;
        pEncSurfaces[i]->Data.Pitch = width;
#ifndef ENABLE_INPUT
        // In case simulating direct access to frames we initialize the allocated surfaces with default pattern
        // - For true benchmark comparisons to async workloads all surfaces must have the same data
        memset(pEncSurfaces[i]->Data.Y, 100, width * height);  // Y plane
        memset(pEncSurfaces[i]->Data.U, 50, (width * height)/2);  // UV plane
#endif
    }

    // Initialize the Media SDK encoder
    sts = mfxENC.Init(&mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);    

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    mfxVideoParam par;
    memset(&par, 0, sizeof(par));
    sts = mfxENC.GetVideoParam(&par);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts); 

    // Prepare Media SDK bit stream buffer
    mfxBitstream mfxBS; 
    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.MaxLength = par.mfx.BufferSizeInKB * 1000;
    mfxBS.Data = new mfxU8[mfxBS.MaxLength];
    MSDK_CHECK_POINTER(mfxBS.Data, MFX_ERR_MEMORY_ALLOC);


    // ===================================
    // Start encoding the frames
    //
 
#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    int nEncSurfIdx = 0;
    mfxSyncPoint syncp;
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main encoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)        
    {        
        nEncSurfIdx = GetFreeSurfaceIndex(pEncSurfaces, nEncSurfNum); // Find free frame surface  
        if (MFX_ERR_NOT_FOUND == nEncSurfIdx)
            return MFX_ERR_MEMORY_ALLOC;

        sts = LoadRawFrame(pEncSurfaces[nEncSurfIdx], fSource);
        MSDK_BREAK_ON_ERROR(sts);
                   
        for (;;)
        {    
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, pEncSurfaces[nEncSurfIdx], &mfxBS, &syncp);
            
            if (MFX_ERR_NONE < sts && !syncp) // Repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // Wait if device is busy, then repeat the same call            
            }
            else if (MFX_ERR_NONE < sts && syncp)                 
            {
                sts = MFX_ERR_NONE; // Ignore warnings if output is available                                    
                break;
            }
            else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
            {
                // Allocate more bitstream buffer memory here if needed...
                break;                
            }
            else
                break;
        }  

        if(MFX_ERR_NONE == sts)
        {
            sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Wait until encoded frame is ready
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
    }

    // MFX_ERR_MORE_DATA means that the input file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    
    //
    // Stage 2: Retrieve the buffered encoded frames
    //
    while (MFX_ERR_NONE <= sts)
    {       
        for (;;)
        {                
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, NULL, &mfxBS, &syncp);            

            if (MFX_ERR_NONE < sts && !syncp) // Repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // Wait if device is busy, then repeat the same call                 
            }
            else if (MFX_ERR_NONE < sts && syncp)                 
            {
                sts = MFX_ERR_NONE; // Ignore warnings if output is available                                    
                break;
            }
            else
                break;
        }            

        if(MFX_ERR_NONE == sts)
        {
            sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Wait until encoded frame is ready
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
    }    

    // MFX_ERR_MORE_DATA indicates that there are no more buffered frames, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

#ifdef ENABLE_BENCHMARK
    QueryPerformanceCounter(&tEnd);
    double duration = ((double)tEnd.QuadPart - (double)tStart.QuadPart)  / freq;
    printf("\nExecution time: %3.2fs (%3.2ffps)\n", duration, nFrame/duration);
#endif

    // ===================================================================
    // Clean up resources
    //  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
    //    some surfaces may still be locked by internal Media SDK resources.
    
    mfxENC.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nEncSurfNum; i++)
        delete pEncSurfaces[i];
    MSDK_SAFE_DELETE_ARRAY(pEncSurfaces);
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffers);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

    fclose(fSource);
    fclose(fSink);

    return 0;
}