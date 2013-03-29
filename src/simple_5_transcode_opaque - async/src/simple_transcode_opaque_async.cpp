//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT  // Disabling this flag removes printing of progress (saves CPU cycles)
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

    // =====================================================================
    // Intel Media SDK transcode opaque pipeline setup
    // - In this example we are decoding and encoding an AVC (H.264) stream
    // - Opaque memory handling introduced. Media SDK selects suitable memory surface internally
    // - Asynchronous operation by executing more than one encode operation simultaneously
    //

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "bbb1920x1080_trans_opaque_async.264", "wb");
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    // Initialize Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    // - Version 1.3 is selected since the opaque memory feature was added in this API release
    //   If more recent API features are needed, change the version accordingly
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = {3, 1}; // Note: API 1.3 !
    MFXVideoSession mfxSession;
    sts = mfxSession.Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder & encoder
    MFXVideoDECODE mfxDEC(mfxSession);
    MFXVideoENCODE mfxENC(mfxSession); 

    // Set required video parameters for decode
    mfxVideoParam mfxDecParams;
    memset(&mfxDecParams, 0, sizeof(mfxDecParams));
    mfxDecParams.mfx.CodecId = MFX_CODEC_AVC;
    mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;

    // Configure Media SDK to keep more operations in flight
    // - AsyncDepth represents the number of tasks that can be submitted, before synchronizing is required
    // - The choice of AsyncDepth = 4 is quite arbitrary but has proven to result in good performance
    mfxDecParams.AsyncDepth = 4;

    // Prepare Media SDK bit stream buffer for decoder
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
    
    sts = mfxDEC.DecodeHeader(&mfxBS, &mfxDecParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
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
    mfxEncParams.mfx.FrameInfo.CropW            = mfxDecParams.mfx.FrameInfo.CropW;
    mfxEncParams.mfx.FrameInfo.CropH            = mfxDecParams.mfx.FrameInfo.CropH;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(mfxEncParams.mfx.FrameInfo.CropW);
    mfxEncParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct)?
        MSDK_ALIGN16(mfxEncParams.mfx.FrameInfo.CropH) : MSDK_ALIGN32(mfxEncParams.mfx.FrameInfo.CropH);
    
    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;

    // Configure Media SDK to keep more operations in flight
    // - AsyncDepth represents the number of tasks that can be submitted, before synchronizing is required
    mfxEncParams.AsyncDepth = mfxDecParams.AsyncDepth;

    // Query number required surfaces for decoder
    mfxFrameAllocRequest DecRequest;
    memset(&DecRequest, 0, sizeof(DecRequest));
    sts = mfxDEC.QueryIOSurf(&mfxDecParams, &DecRequest);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC.QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

    mfxU16 nSurfNum = EncRequest.NumFrameSuggested + DecRequest.NumFrameSuggested + mfxEncParams.AsyncDepth;

    // Initialize shared surfaces for decoder and encoder
    // - Note that no buffer memory is allocated, for opaque memory this is handled by Media SDK internally
    // - Frame surface array keeps reference to all surfaces
    // - Opaque memory is configured with the mfxExtOpaqueSurfaceAlloc extended buffers 
    mfxFrameSurface1** pSurfaces = new mfxFrameSurface1*[nSurfNum];
    MSDK_CHECK_POINTER(pSurfaces, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nSurfNum; i++)
    {       
        pSurfaces[i] = new mfxFrameSurface1;
        MSDK_CHECK_POINTER(pSurfaces[i], MFX_ERR_MEMORY_ALLOC);
        memset(pSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pSurfaces[i]->Info), &(DecRequest.Info), sizeof(mfxFrameInfo));
    }

    mfxExtOpaqueSurfaceAlloc extOpaqueAllocDec;
    memset(&extOpaqueAllocDec, 0, sizeof(extOpaqueAllocDec));
    extOpaqueAllocDec.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    extOpaqueAllocDec.Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    mfxExtBuffer* pExtParamsDec = (mfxExtBuffer*)&extOpaqueAllocDec;

    mfxExtOpaqueSurfaceAlloc extOpaqueAllocEnc;
    memset(&extOpaqueAllocEnc, 0, sizeof(extOpaqueAllocEnc));
    extOpaqueAllocEnc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    extOpaqueAllocEnc.Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    mfxExtBuffer* pExtParamsEnc = (mfxExtBuffer*)&extOpaqueAllocEnc;

    extOpaqueAllocDec.Out.Surfaces = pSurfaces;
    extOpaqueAllocDec.Out.NumSurface = nSurfNum;
    extOpaqueAllocDec.Out.Type = DecRequest.Type;
    memcpy(&extOpaqueAllocEnc.In, &extOpaqueAllocDec.Out, sizeof(extOpaqueAllocDec.Out));

    mfxDecParams.ExtParam = &pExtParamsDec;
    mfxDecParams.NumExtParam = 1;
    mfxEncParams.ExtParam = &pExtParamsEnc;
    mfxEncParams.NumExtParam = 1;


    // Initialize the Media SDK decoder
    sts = mfxDEC.Init(&mfxDecParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

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
    // Start transcoding the frames
    //

#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    mfxSyncPoint syncpD;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    mfxU32 nFrame = 0;
    int nIndex = 0;  
    int nFirstSyncTask	= 0;
    int nTaskIdx		= 0;

    //
    // Stage 1: Main transcoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts)          
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
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
        else
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                Sleep(1); // just wait and then repeat the same call to DecodeFrameAsync

            if (MFX_ERR_MORE_DATA == sts)
            {
                sts = ReadBitStreamData(&mfxBS, fSource); // Read more data to input bit stream
                MSDK_BREAK_ON_ERROR(sts);            
            }

            if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts)
            {
                nIndex = GetFreeSurfaceIndex(pSurfaces, nSurfNum); // Find free frame surface 
                if (MFX_ERR_NOT_FOUND == nIndex)
                    return MFX_ERR_MEMORY_ALLOC;
            }
        
            // Decode a frame asychronously (returns immediately)
            sts = mfxDEC.DecodeFrameAsync(&mfxBS, pSurfaces[nIndex], &pmfxOutSurface, &syncpD);

            // Ignore warnings if output is available, 
            // if no output and no action required just repeat the DecodeFrameAsync call
            if (MFX_ERR_NONE < sts && syncpD) 
                sts = MFX_ERR_NONE;               
        
            if (MFX_ERR_NONE == sts)
            {         
                for (;;)
                {    
                    // Encode a frame asychronously (returns immediately)
                    sts = mfxENC.EncodeFrameAsync(NULL, pmfxOutSurface, &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 		
            
                    if (MFX_ERR_NONE < sts && !pTasks[nTaskIdx].syncp) // repeat the call if warning and no output
                    {
                        if (MFX_WRN_DEVICE_BUSY == sts)                
                            Sleep(1); // wait if device is busy                
                    }
                    else if (MFX_ERR_NONE < sts && pTasks[nTaskIdx].syncp)                 
                    {
                        sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
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
    }   

    // MFX_ERR_MORE_DATA means that file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);          
      
    //
    // Stage 2: Retrieve the buffered decoded frames
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts)        
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
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            printf("Frame number: %d\r", nFrame);
#endif
        }
        else
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                Sleep(1);

            nIndex = GetFreeSurfaceIndex(pSurfaces, nSurfNum); // Find free frame surface
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;            

            // Decode a frame asychronously (returns immediately)
            sts = mfxDEC.DecodeFrameAsync(NULL, pSurfaces[nIndex], &pmfxOutSurface, &syncpD);

            // Ignore warnings if output is available, 
            // if no output and no action required just repeat the DecodeFrameAsync call       
            if (MFX_ERR_NONE < sts && syncpD) 
                sts = MFX_ERR_NONE;

            if (MFX_ERR_NONE == sts)
            {
                for (;;)
                {    
                    // Encode a frame asychronously (returns immediately)
                    sts = mfxENC.EncodeFrameAsync(NULL, pmfxOutSurface, &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 		
            
                    if (MFX_ERR_NONE < sts && !pTasks[nTaskIdx].syncp) // repeat the call if warning and no output
                    {
                        if (MFX_WRN_DEVICE_BUSY == sts)                
                            Sleep(1); // wait if device is busy                
                    }
                    else if (MFX_ERR_NONE < sts && pTasks[nTaskIdx].syncp)                 
                    {
                        sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
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
    }

    // MFX_ERR_MORE_DATA indicates that all decode buffers has been fetched, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //
    // Stage 3: Retrieve the buffered encoded frames
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
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
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

                if (MFX_ERR_NONE < sts && !pTasks[nTaskIdx].syncp) // repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)                
                        Sleep(1); // wait if device is busy                
                }
                else if (MFX_ERR_NONE < sts && pTasks[nTaskIdx].syncp)                 
                {
                    sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
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
    // Stage 4: Sync all remaining tasks in task pool
    //
    while(pTasks[nFirstSyncTask].syncp)
    {
        sts = mfxSession.SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
        MSDK_BREAK_ON_ERROR(sts);

        pTasks[nFirstSyncTask].syncp = NULL;
        pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
        pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
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
    mfxDEC.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nSurfNum; i++)
        delete pSurfaces[i];
    MSDK_SAFE_DELETE_ARRAY(pSurfaces);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);
    for(int i=0;i<taskPoolSize;i++)
        MSDK_SAFE_DELETE_ARRAY(pTasks[i].mfxBS.Data);
    MSDK_SAFE_DELETE_ARRAY(pTasks);

    fclose(fSource);
    fclose(fSink);

    return 0;
}
