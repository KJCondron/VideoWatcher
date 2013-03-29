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

// For use with asynchronous task management
typedef struct {
    mfxBitstream mfxBS; 
    mfxSyncPoint syncp;
} Task;

// Get free task 
int GetFreeTaskIndex(Task* pTaskPool, mfxU16 nPoolSize)
{
    if(pTaskPool)
        for(int i=0;i<nPoolSize;i++)
            if(!pTaskPool[i].syncp)
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
    // - Asynchronous operation by executing more than one encode operation simultaneously
    //

    // Open input YV12 YUV file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.yuv", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "test_d3d_async.264", "wb");
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

    // Configure Media SDK to keep more operations in flight
    // - AsyncDepth represents the number of tasks that can be submitted, before synchronizing is required
    // - The choice of AsyncDepth = 4 is quite arbitrary but has proven to result in good performance
    mfxEncParams.AsyncDepth = 4;
    
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
        pmfxSurfaces[i]->Data.MemId = mfxResponse.mids[i]; // MID (memory id) represent one D3D NV12 surface

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
    
    // Create task pool to improve asynchronous performance (greater GPU utilization)
    mfxU16 taskPoolSize = mfxEncParams.AsyncDepth;  // number of tasks that can be submitted, before synchronizing is required
    Task* pTasks = new Task[taskPoolSize];
    memset(pTasks, 0, sizeof(Task) * taskPoolSize);
    for(int i=0;i<taskPoolSize;i++)
    {
        // Prepare Media SDK bit stream buffer
        pTasks[i].mfxBS.MaxLength = par.mfx.BufferSizeInKB * 1000;
        pTasks[i].mfxBS.Data = new mfxU8[pTasks[i].mfxBS.MaxLength];
        MSDK_CHECK_POINTER(pTasks[i].mfxBS.Data, MFX_ERR_MEMORY_ALLOC);
    }


    // ===================================
    // Start encoding the frames
    //
 
#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    int nEncSurfIdx		= 0;	
    int nTaskIdx		= 0;
    int nFirstSyncTask	= 0;
    mfxU32 nFrame		= 0;

    //
    // Stage 1: Main encoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)        
    {      
        nTaskIdx = GetFreeTaskIndex(pTasks, taskPoolSize); // Find free task
        if(MFX_ERR_NOT_FOUND == nTaskIdx)
        {
            // No more free tasks, need to sync
            sts = mfxSession.SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
        else
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
                // Encode a frame asychronously (returns immediately)
                sts = mfxENC.EncodeFrameAsync(NULL, pmfxSurfaces[nEncSurfIdx], &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 				

                if (MFX_ERR_NONE < sts && !pTasks[nTaskIdx].syncp) // Repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)                
                        Sleep(1); // Wait if device is busy, then repeat the same call            
                }
                else if (MFX_ERR_NONE < sts && pTasks[nTaskIdx].syncp)                 
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
        nTaskIdx = GetFreeTaskIndex(pTasks, taskPoolSize); // Find free task
        if(MFX_ERR_NOT_FOUND == nTaskIdx)
        {
            // No more free tasks, need to sync
            sts = mfxSession.SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
        else
        {
            for (;;)
            {                
                // Encode a frame asychronously (returns immediately)
                sts = mfxENC.EncodeFrameAsync(NULL, NULL, &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 

                if (MFX_ERR_NONE < sts && !pTasks[nTaskIdx].syncp) // Repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)                
                        Sleep(1); // Wait if device is busy, then repeat the same call                 
                }
                else if (MFX_ERR_NONE < sts && pTasks[nTaskIdx].syncp)                 
                {
                    sts = MFX_ERR_NONE; // Ignore warnings if output is available 
                    break;
                }
                else
                    break;
            }            
        }
    }    

    // MFX_ERR_MORE_DATA indicates that there are no more buffered frames, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //
    // Stage 3: Sync all remaining tasks in task pool
    //
    while(pTasks[nFirstSyncTask].syncp)
    {
        sts = mfxSession.SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
        MSDK_BREAK_ON_ERROR(sts);

        pTasks[nFirstSyncTask].syncp = NULL;
        nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

        ++nFrame;
#ifdef ENABLE_OUTPUT
        printf("Frame number: %d\r", nFrame);
#endif
    }


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
        delete pmfxSurfaces[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
    for(int i=0;i<taskPoolSize;i++)
        MSDK_SAFE_DELETE_ARRAY(pTasks[i].mfxBS.Data);
    MSDK_SAFE_DELETE_ARRAY(pTasks);

    fclose(fSource);
    fclose(fSink);

    CleanupHWDevice();

    return 0;
}