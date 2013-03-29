//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT  // Disabling this flag removes all YUV file writing
#define ENABLE_BENCHMARK

#include "common_utils.h"

#ifndef DX11_D3D
#include "common_directx.h"
#define DEVICE_MGR_TYPE MFX_HANDLE_DIRECT3D_DEVICE_MANAGER9
#else
#include "common_directx11.h"
#define DEVICE_MGR_TYPE MFX_HANDLE_D3D11_DEVICE
#endif

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

    // =====================================================================
    // Intel Media SDK decode pipeline setup
    // - In this example we are decoding an AVC (H.264) stream
    // - Video memory surfaces are used to store the decoded frames
    //

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output YUV file
    FILE* fSink;
    fopen_s(&fSink, "dectest_d3d.yuv", "wb");
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    // Initialize Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    // - Version 1.0 is selected for greatest backwards compatibility.
    //   If more recent API features are needed, change the version accordingly
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
#ifdef DX11_D3D
    impl |= MFX_IMPL_VIA_D3D11;
#endif
    mfxVersion ver = {0, 1};
    MFXVideoSession mfxSession;
    sts = mfxSession.Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder
    MFXVideoDECODE mfxDEC(mfxSession);


    // Create DirectX device context
    mfxHDL deviceHandle;
    sts = CreateHWDevice(mfxSession, &deviceHandle, NULL);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);   

    // Provide device manager to Media SDK
    sts = mfxSession.SetHandle(DEVICE_MGR_TYPE, deviceHandle);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);   

    mfxFrameAllocator mfxAllocator;
    mfxAllocator.Alloc	= simple_alloc;
    mfxAllocator.Free	= simple_free;
    mfxAllocator.Lock	= simple_lock;
    mfxAllocator.Unlock = simple_unlock;
    mfxAllocator.GetHDL = simple_gethdl;

    // Since we are using video memory we must provide Media SDK with an external allocator 
    sts = mfxSession.SetFrameAllocator(&mfxAllocator);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


    // Set required video parameters for decode
    mfxVideoParam mfxVideoParams;
    memset(&mfxVideoParams, 0, sizeof(mfxVideoParams));
    mfxVideoParams.mfx.CodecId = MFX_CODEC_AVC;
    mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    
    // Prepare Media SDK bit stream buffer
    // - Arbitrary buffer size for this example
    mfxBitstream mfxBS; 
    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.MaxLength = 1024 * 1024;
    mfxBS.Data = new mfxU8[mfxBS.MaxLength];
    MSDK_CHECK_POINTER(mfxBS.Data, MFX_ERR_MEMORY_ALLOC);

    // Read a chunk of data from stream file into bit stream buffer
    // - Parse bit stream, searching for header and fill video parameters structure
    // - Abort if bit stream header is not found in the first bit stream buffer chunk
    sts = ReadBitStreamData(&mfxBS, fSource);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    
    sts = mfxDEC.DecodeHeader(&mfxBS, &mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    
    // Validate video decode parameters (optional)
    // sts = mfxDEC.Query(&mfxVideoParams, &mfxVideoParams);  

    // Query number of required surfaces for decoder
    mfxFrameAllocRequest Request;
    memset(&Request, 0, sizeof(Request));
    sts = mfxDEC.QueryIOSurf(&mfxVideoParams, &Request);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

#ifdef DX11_D3D
    Request.Type |= WILL_READ; // Hint to DX11 memory handler that application will read data from output surfaces
#endif	

    // Allocate required surfaces
    mfxFrameAllocResponse mfxResponse;
    sts = mfxAllocator.Alloc(mfxAllocator.pthis, &Request, &mfxResponse);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU16 numSurfaces = mfxResponse.NumFrameActual;

    // Allocate surface headers (mfxFrameSurface1) for decoder
    mfxFrameSurface1** pmfxSurfaces = new mfxFrameSurface1*[numSurfaces];
    MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < numSurfaces; i++)
    {       
        pmfxSurfaces[i] = new mfxFrameSurface1;
        memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfaces[i]->Info), &(mfxVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pmfxSurfaces[i]->Data.MemId = mfxResponse.mids[i]; // MID (memory id) represent one D3D NV12 surface  
    }  


    // Initialize the Media SDK decoder
    sts = mfxDEC.Init(&mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


    // ===============================================================
    // Start decoding the frames from the stream
    //

#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    mfxSyncPoint syncp;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    int nIndex = 0;  
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main decoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts)          
    {
        if (MFX_WRN_DEVICE_BUSY == sts)
            Sleep(1); // Wait if device is busy, then repeat the same call to DecodeFrameAsync

        if (MFX_ERR_MORE_DATA == sts)
        {
            sts = ReadBitStreamData(&mfxBS, fSource); // Read more data into input bit stream
            MSDK_BREAK_ON_ERROR(sts);            
        }

        if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts)
        {
            nIndex = GetFreeSurfaceIndex(pmfxSurfaces, numSurfaces); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;
        }
        
        // Decode a frame asychronously (returns immediately)
        //  - If input bitstream contains multiple frames DecodeFrameAsync will start decoding multiple frames, and remove them from bitstream
        sts = mfxDEC.DecodeFrameAsync(&mfxBS, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncp);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call
        if (MFX_ERR_NONE < sts && syncp) 
            sts = MFX_ERR_NONE;

        if (MFX_ERR_NONE == sts)
            sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Wait until decoded frame is ready
        
        if (MFX_ERR_NONE == sts)
        {
            ++nFrame;
#ifdef ENABLE_OUTPUT
            // Surface locking required when read/write D3D surfaces
            sts = mfxAllocator.Lock(mfxAllocator.pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
            MSDK_BREAK_ON_ERROR(sts);

            sts = WriteRawFrame(pmfxOutSurface, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            sts = mfxAllocator.Unlock(mfxAllocator.pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
            MSDK_BREAK_ON_ERROR(sts);

            printf("Frame number: %d\r", nFrame);
#endif
        }            
    }   

    // MFX_ERR_MORE_DATA means that file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);          
      
    //
    // Stage 2: Retrieve the buffered decoded frames
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts)        
    {        
        if (MFX_WRN_DEVICE_BUSY == sts)
            Sleep(1); // Wait if device is busy, then repeat the same call to DecodeFrameAsync

        nIndex = GetFreeSurfaceIndex(pmfxSurfaces, numSurfaces); // Find free frame surface
        if (MFX_ERR_NOT_FOUND == nIndex)
            return MFX_ERR_MEMORY_ALLOC;            

        // Decode a frame asychronously (returns immediately)
        sts = mfxDEC.DecodeFrameAsync(NULL, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncp);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call        
        if (MFX_ERR_NONE < sts && syncp) 
            sts = MFX_ERR_NONE;

        if (MFX_ERR_NONE == sts)
            sts = mfxSession.SyncOperation(syncp, 60000); // Synchronize. Waits until decoded frame is ready

        if (MFX_ERR_NONE == sts)
        {
            ++nFrame;
#ifdef ENABLE_OUTPUT
            // Surface locking required when read/write D3D surfaces
            sts = mfxAllocator.Lock(mfxAllocator.pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
            MSDK_BREAK_ON_ERROR(sts);

            sts = WriteRawFrame(pmfxOutSurface, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            sts = mfxAllocator.Unlock(mfxAllocator.pthis, pmfxOutSurface->Data.MemId, &(pmfxOutSurface->Data));
            MSDK_BREAK_ON_ERROR(sts);

            printf("Frame number: %d\r", nFrame); 
#endif
        }
    }

    // MFX_ERR_MORE_DATA indicates that all buffers has been fetched, exit in case of other errors
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
    
    mfxDEC.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < numSurfaces; i++)
        delete pmfxSurfaces[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

    fclose(fSource);
    fclose(fSink);

    CleanupHWDevice();
    
    return 0;
}
