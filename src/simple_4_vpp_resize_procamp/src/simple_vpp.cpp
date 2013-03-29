//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT    // Disabling this flag removes all YUV file writing
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
    // Intel Media SDK Video Pre/Post Processing (VPP) pipeline setup
    // - Showcasing two VPP features
    //   - Resize (frame width and height is halved)
    //   - ProcAmp: Increase brightness
    //

    // Open input YV12 YUV file
    FILE* fSource;
    fopen_s(&fSource, "C:\\Users\\Karl\\Documents\\Programming\\intel-media-sdk-tutorial-020813\\S15.2013.Hurricanes.vs.Blues.x264_track1.h264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output YUV file
    FILE* fSink;
    fopen_s(&fSink, "C:\\Users\\Karl\\Documents\\Programming\\intel-media-sdk-tutorial-020813\\S15.2013.Hurricanes.vs.Blues.x264_track1_bright.yuv", "wb");
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

    // Initialize VPP parameters
    // - For simplistic memory management, system memory surfaces are used to store the raw frames
    //   (Note that when using HW acceleration D3D surfaces are prefered, for better performance)
    mfxVideoParam VPPParams;
    memset(&VPPParams, 0, sizeof(VPPParams));
    // Input data
    VPPParams.vpp.In.FourCC         = MFX_FOURCC_NV12;
    VPPParams.vpp.In.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;  
    VPPParams.vpp.In.CropX          = 0;
    VPPParams.vpp.In.CropY          = 0; 
    VPPParams.vpp.In.CropW          = inputWidth;
    VPPParams.vpp.In.CropH          = inputHeight;
    VPPParams.vpp.In.PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.In.FrameRateExtN  = 30;
    VPPParams.vpp.In.FrameRateExtD  = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.In.Width  = MSDK_ALIGN16(inputWidth);
    VPPParams.vpp.In.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.In.PicStruct)?
                                 MSDK_ALIGN16(inputHeight) : MSDK_ALIGN32(inputHeight);
    // Output data
    VPPParams.vpp.Out.FourCC        = MFX_FOURCC_NV12;     
    VPPParams.vpp.Out.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;             
    VPPParams.vpp.Out.CropX         = 0;
    VPPParams.vpp.Out.CropY         = 0; 
    VPPParams.vpp.Out.CropW         = inputWidth/2;
    VPPParams.vpp.Out.CropH         = inputHeight/2;
    VPPParams.vpp.Out.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.Out.FrameRateExtN = 30;
    VPPParams.vpp.Out.FrameRateExtD = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.Out.Width  = MSDK_ALIGN16(VPPParams.vpp.Out.CropW); 
    VPPParams.vpp.Out.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.Out.PicStruct)?
                                    MSDK_ALIGN16(VPPParams.vpp.Out.CropH) : MSDK_ALIGN32(VPPParams.vpp.Out.CropH);

    VPPParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    
    // Create Media SDK VPP component
    MFXVideoVPP mfxVPP(mfxSession); 

    // Query number of required surfaces for VPP
    mfxFrameAllocRequest VPPRequest[2];// [0] - in, [1] - out
    memset(&VPPRequest, 0, sizeof(mfxFrameAllocRequest)*2);
    sts = mfxVPP.QueryIOSurf(&VPPParams, VPPRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

    mfxU16 nVPPSurfNumIn = VPPRequest[0].NumFrameSuggested;
    mfxU16 nVPPSurfNumOut = VPPRequest[1].NumFrameSuggested;

    // Allocate surfaces for VPP: In
    // - Width and height of buffer must be aligned, a multiple of 32 
    // - Frame surface array keeps pointers all surface planes and general frame info
    mfxU16 width = (mfxU16)MSDK_ALIGN32(VPPParams.vpp.In.Width);
    mfxU16 height = (mfxU16)MSDK_ALIGN32(VPPParams.vpp.In.Height);
    mfxU8  bitsPerPixel = 12;  // NV12 format is a 12 bits per pixel format
    mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffersIn = (mfxU8 *)new mfxU8[surfaceSize * nVPPSurfNumIn];
 
    mfxFrameSurface1** pVPPSurfacesIn = new mfxFrameSurface1*[nVPPSurfNumIn];
    MSDK_CHECK_POINTER(pVPPSurfacesIn, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nVPPSurfNumIn; i++)
    {       
        pVPPSurfacesIn[i] = new mfxFrameSurface1;
        memset(pVPPSurfacesIn[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pVPPSurfacesIn[i]->Info), &(VPPParams.vpp.In), sizeof(mfxFrameInfo));
        pVPPSurfacesIn[i]->Data.Y = &surfaceBuffersIn[surfaceSize * i];
        pVPPSurfacesIn[i]->Data.U = pVPPSurfacesIn[i]->Data.Y + width * height;
        pVPPSurfacesIn[i]->Data.V = pVPPSurfacesIn[i]->Data.U + 1;
        pVPPSurfacesIn[i]->Data.Pitch = width;

#ifndef ENABLE_INPUT
        // In case simulating direct access to frames we initialize the allocated surfaces with default pattern
        memset(pVPPSurfacesIn[i]->Data.Y, 100, width * height);  // Y plane
        memset(pVPPSurfacesIn[i]->Data.U, 50, (width * height)/2);  // UV plane
#endif
    }

    // Allocate surfaces for VPP: Out
    width = (mfxU16)MSDK_ALIGN32(VPPParams.vpp.Out.Width);
    height = (mfxU16)MSDK_ALIGN32(VPPParams.vpp.Out.Height);
    surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffersOut = (mfxU8 *)new mfxU8[surfaceSize * nVPPSurfNumOut];

    mfxFrameSurface1** pVPPSurfacesOut = new mfxFrameSurface1*[nVPPSurfNumOut];
    MSDK_CHECK_POINTER(pVPPSurfacesOut, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nVPPSurfNumOut; i++)
    {       
        pVPPSurfacesOut[i] = new mfxFrameSurface1;
        memset(pVPPSurfacesOut[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pVPPSurfacesOut[i]->Info), &(VPPParams.vpp.Out), sizeof(mfxFrameInfo));
        pVPPSurfacesOut[i]->Data.Y = &surfaceBuffersOut[surfaceSize * i];
        pVPPSurfacesOut[i]->Data.U = pVPPSurfacesOut[i]->Data.Y + width * height;
        pVPPSurfacesOut[i]->Data.V = pVPPSurfacesOut[i]->Data.U + 1;
        pVPPSurfacesOut[i]->Data.Pitch = width;
    }


    // Initialize extended buffer for frame processing
    // - Process amplifier (ProcAmp) used to control brightness 
    // - mfxExtVPPDoUse:   Define the processing algorithm to be used
    // - mfxExtVPPProcAmp: ProcAmp configuration
    // - mfxExtBuffer:     Add extended buffers to VPP parameter configuration
    mfxExtVPPDoUse extDoUse;
    mfxU32 tabDoUseAlg[1]; 
    extDoUse.Header.BufferId = MFX_EXTBUFF_VPP_DOUSE;
    extDoUse.Header.BufferSz = sizeof(mfxExtVPPDoUse);
    extDoUse.NumAlg  = 1;
    extDoUse.AlgList = tabDoUseAlg;
    tabDoUseAlg[0] = MFX_EXTBUFF_VPP_PROCAMP;

    mfxExtVPPProcAmp procampConfig;
    procampConfig.Header.BufferId = MFX_EXTBUFF_VPP_PROCAMP;
    procampConfig.Header.BufferSz = sizeof(mfxExtVPPProcAmp);
    procampConfig.Hue        = 0.0f;  // Default
    procampConfig.Saturation = 1.0f;  // Default
    procampConfig.Contrast   = 1.0;   // Default
    procampConfig.Brightness = 40.0;  // Adjust brightness

    mfxExtBuffer* ExtBuffer[2];
    ExtBuffer[0] = (mfxExtBuffer*)&extDoUse;
    ExtBuffer[1] = (mfxExtBuffer*)&procampConfig;
    VPPParams.NumExtParam = 2;
    VPPParams.ExtParam = (mfxExtBuffer**)&ExtBuffer[0];


    // Initialize Media SDK VPP
    sts = mfxVPP.Init(&VPPParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);    


    // ===================================
    // Start processing the frames
    //
 
#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    int nSurfIdxIn = 0, nSurfIdxOut = 0; 
    mfxSyncPoint syncp;
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main processing loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)        
    {        
        nSurfIdxIn = GetFreeSurfaceIndex(pVPPSurfacesIn, nVPPSurfNumIn); // Find free input frame surface
        if (MFX_ERR_NOT_FOUND == nSurfIdxIn)
            return MFX_ERR_MEMORY_ALLOC;

        sts = LoadRawFrame(pVPPSurfacesIn[nSurfIdxIn], fSource); // Load frame from file into surface
        MSDK_BREAK_ON_ERROR(sts);
           
        nSurfIdxOut = GetFreeSurfaceIndex(pVPPSurfacesOut, nVPPSurfNumOut); // Find free output frame surface
        if (MFX_ERR_NOT_FOUND == nSurfIdxOut)
            return MFX_ERR_MEMORY_ALLOC;

        // Process a frame asychronously (returns immediately)
        sts = mfxVPP.RunFrameVPPAsync(pVPPSurfacesIn[nSurfIdxIn], pVPPSurfacesOut[nSurfIdxOut], NULL, &syncp);
        if (MFX_ERR_MORE_DATA == sts)
            continue;

        // MFX_ERR_MORE_SURFACE means output is ready but need more surface (example: Frame Rate Conversion 30->60)
        // * Not handled in this example!

        MSDK_BREAK_ON_ERROR(sts);

        sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Wait until frame processing is ready
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        ++nFrame;
#ifdef ENABLE_OUTPUT
        sts = WriteRawFrame(pVPPSurfacesOut[nSurfIdxOut], fSink);
        MSDK_BREAK_ON_ERROR(sts);

        printf("Stg1 Frame number: %d\r", nFrame);
#endif	
    }

    // MFX_ERR_MORE_DATA means that the input file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    
    //
    // Stage 2: Retrieve the buffered VPP frames
    //
    while (MFX_ERR_NONE <= sts)
    {       
        nSurfIdxOut = GetFreeSurfaceIndex(pVPPSurfacesOut, nVPPSurfNumOut); // Find free frame surface
        if (MFX_ERR_NOT_FOUND == nSurfIdxOut)
            return MFX_ERR_MEMORY_ALLOC;

        // Process a frame asychronously (returns immediately)
        sts = mfxVPP.RunFrameVPPAsync(NULL, pVPPSurfacesOut[nSurfIdxOut], NULL, &syncp);
        MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_SURFACE);
        MSDK_BREAK_ON_ERROR(sts);
        
        sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Wait until frame processing is ready
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        ++nFrame;
#ifdef ENABLE_OUTPUT
        sts = WriteRawFrame(pVPPSurfacesOut[nSurfIdxOut], fSink);
        MSDK_BREAK_ON_ERROR(sts);

        printf("Stg2 Frame number: %d\r", nFrame);
#endif
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
    
    mfxVPP.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nVPPSurfNumIn; i++)
        delete pVPPSurfacesIn[i];
    MSDK_SAFE_DELETE_ARRAY(pVPPSurfacesIn);
    for (int i = 0; i < nVPPSurfNumOut; i++)
        delete pVPPSurfacesOut[i];
    MSDK_SAFE_DELETE_ARRAY(pVPPSurfacesOut);
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffersIn);
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffersOut);

    fclose(fSource);
    fclose(fSink);

    return 0;
}