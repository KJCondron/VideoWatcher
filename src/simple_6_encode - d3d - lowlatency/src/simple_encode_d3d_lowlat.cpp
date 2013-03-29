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

#define MEASURE_LATENCY

#ifdef MEASURE_LATENCY
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#endif

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

    mfxU16 inputWidth = 1920;
    mfxU16 inputHeight = 1080;

    // =====================================================================
    // Intel Media SDK encode pipeline setup
    // - In this example we are encoding an AVC (H.264) stream
    // - Video memory surfaces are used
    // - Configure pipeline for low latency
    //

    // Open input YV12 YUV file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.yuv", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "test_d3d_ll.264", "wb");
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    // Initialize Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
#ifdef DX11_D3D
    impl |= MFX_IMPL_VIA_D3D11;
#endif
    mfxVersion ver = {0, 1};
    MFXVideoSession mfxSession;
    sts = mfxSession.Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


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

    // When using video memory we must provide Media SDK with an external allocator 
    sts = mfxSession.SetFrameAllocator(&mfxAllocator);
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
    
    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
    

    // Configuration for low latency
    mfxEncParams.AsyncDepth		= 1;
    mfxEncParams.mfx.GopRefDist = 1;

    mfxExtCodingOption extendedCodingOptions;
    memset(&extendedCodingOptions, 0, sizeof(extendedCodingOptions));
    extendedCodingOptions.Header.BufferId		= MFX_EXTBUFF_CODING_OPTION;
    extendedCodingOptions.Header.BufferSz		= sizeof(extendedCodingOptions);
    extendedCodingOptions.MaxDecFrameBuffering	= 1;
    mfxExtBuffer* extendedBuffers[1];
    extendedBuffers[0]			= (mfxExtBuffer*)&extendedCodingOptions;
    mfxEncParams.ExtParam		= extendedBuffers;
    mfxEncParams.NumExtParam	= 1;
    // ---


    // Create Media SDK encoder
    MFXVideoENCODE mfxENC(mfxSession); 

    // Validate video encode parameters (optional)
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching the requested configuration
    sts = mfxENC.Query(&mfxEncParams, &mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM); 
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number of required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC.QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

#ifdef DX11_D3D
    EncRequest.Type |= WILL_WRITE; // Hint to DX11 memory handler that application will write data to input surfaces
#endif
    
    // Allocate required surfaces
    mfxFrameAllocResponse mfxResponse;
    sts = mfxAllocator.Alloc(mfxAllocator.pthis, &EncRequest, &mfxResponse);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU16 nEncSurfNum = mfxResponse.NumFrameActual;

    // Allocate surface headers (mfxFrameSurface1) for decoder
    mfxFrameSurface1** pmfxSurfaces = new mfxFrameSurface1*[nEncSurfNum];
    MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < nEncSurfNum; i++)
    {
        pmfxSurfaces[i] = new mfxFrameSurface1;
        memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfaces[i]->Info), &(mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pmfxSurfaces[i]->Data.MemId = mfxResponse.mids[i];  
#ifndef ENABLE_INPUT
        // In case simulating direct access to frames we initialize the allocated surfaces with default pattern
        // - For true benchmark comparisons to async workloads all surfaces must have the same data
#ifndef DX11_D3D
        IDirect3DSurface9 *pSurface;
        D3DSURFACE_DESC desc;
        D3DLOCKED_RECT locked;
        pSurface = (IDirect3DSurface9 *)mfxResponse.mids[i];
        pSurface->GetDesc(&desc);
        pSurface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK);
        memset((mfxU8 *)locked.pBits, 100, desc.Height*locked.Pitch);  // Y plane
        memset((mfxU8 *)locked.pBits + desc.Height * locked.Pitch, 50, (desc.Height*locked.Pitch)/2);  // UV plane
        pSurface->UnlockRect();
#else
        // For now, just leave D3D11 surface data uninitialized
#endif
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

#ifdef MEASURE_LATENCY
    // Record e2e latency for first 1000 frames
    // - Store timing in map (for speed), where unique timestamp value is the key
    std::unordered_map<mfxU64, LARGE_INTEGER> encInTimeMap;
    std::unordered_map<mfxU64, LARGE_INTEGER> encOutTimeMap;
    encInTimeMap.rehash(1000);
    encOutTimeMap.rehash(1000);
    mfxU64 currentTimeStamp = 0; // key value to hashes storing timestamps
    LARGE_INTEGER tFreq;
    QueryPerformanceFrequency(&tFreq);
    double freqLat = (double)tFreq.QuadPart;
#endif

    int nEncSurfIdx = 0;
    mfxSyncPoint syncp;
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main encoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)        
    {        
        nEncSurfIdx = GetFreeSurfaceIndex(pmfxSurfaces, nEncSurfNum); // Find free frame surface  
        if (MFX_ERR_NOT_FOUND == nEncSurfIdx)
            return MFX_ERR_MEMORY_ALLOC;

        // Surface locking required when read/write D3D surfaces
        sts = mfxAllocator.Lock(mfxAllocator.pthis, pmfxSurfaces[nEncSurfIdx]->Data.MemId, &(pmfxSurfaces[nEncSurfIdx]->Data));
        MSDK_BREAK_ON_ERROR(sts);

        sts = LoadRawFrame(pmfxSurfaces[nEncSurfIdx], fSource);
        MSDK_BREAK_ON_ERROR(sts);

        sts = mfxAllocator.Unlock(mfxAllocator.pthis, pmfxSurfaces[nEncSurfIdx]->Data.MemId, &(pmfxSurfaces[nEncSurfIdx]->Data));
        MSDK_BREAK_ON_ERROR(sts);
                   
        for (;;)
        {    
#ifdef MEASURE_LATENCY
            if(encInTimeMap.size() < 1000){
                QueryPerformanceCounter(&encInTimeMap[currentTimeStamp]);
                pmfxSurfaces[nEncSurfIdx]->Data.TimeStamp = currentTimeStamp;
            }
#endif

            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, pmfxSurfaces[nEncSurfIdx], &mfxBS, &syncp); 
           
#ifdef MEASURE_LATENCY
            // Since no splitter, artificial timestamp insertion used
            if(encInTimeMap.size() < 1000){
                currentTimeStamp++;
            }
#endif

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

#ifdef MEASURE_LATENCY
            if(encOutTimeMap.size() < 1000)
                QueryPerformanceCounter(&encOutTimeMap[mfxBS.TimeStamp]);
#endif

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
#ifdef MEASURE_LATENCY
            if(encInTimeMap.size() < 1000){
                QueryPerformanceCounter(&encInTimeMap[currentTimeStamp]);
                pmfxSurfaces[nEncSurfIdx]->Data.TimeStamp = currentTimeStamp;
            }
#endif

            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, NULL, &mfxBS, &syncp);  

#ifdef MEASURE_LATENCY
            // Since no splitter, artificial timestamp insertion used
            if(encInTimeMap.size() < 1000){
                currentTimeStamp++;
            }
#endif

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

#ifdef MEASURE_LATENCY
            if(encOutTimeMap.size() < 1000)
                QueryPerformanceCounter(&encOutTimeMap[mfxBS.TimeStamp]);
#endif

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


#ifdef MEASURE_LATENCY
    std::vector<double> frameLatencies;

    // Store all frame latencies in vector
    for (std::unordered_map<mfxU64, LARGE_INTEGER>::iterator it = encInTimeMap.begin(); it != encInTimeMap.end(); ++it)
    {            
        int				tsKey = (*it).first;
        LARGE_INTEGER	time  = (*it).second;
        double			frameLatency = ( ((double)encOutTimeMap[ tsKey ].QuadPart - (double)time.QuadPart) / freqLat ) * 1000;
        frameLatencies.push_back(frameLatency);    
        //printf("[%4d]: %.2f\n", tsKey, frameLatency);
    }
    // Calculate overall latency metrics
    printf("Latency: AVG=%5.1f ms, MAX=%5.1f ms, MIN=%5.1f ms\n", 
            std::accumulate(frameLatencies.begin(), frameLatencies.end(), 0.0)/frameLatencies.size(),
            *std::max_element(frameLatencies.begin(), frameLatencies.end()),
            *std::min_element(frameLatencies.begin(), frameLatencies.end()));
#endif

    // ===================================================================
    // Clean up resources
    //  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
    //    some surfaces may still be locked by internal Media SDK resources.
    
    mfxENC.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nEncSurfNum; i++)
        delete pmfxSurfaces[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

    fclose(fSource);
    fclose(fSink);

    CleanupHWDevice();

    return 0;
}