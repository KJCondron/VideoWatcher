//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT  // Disabling this flag removes printing of progress (saves CPU cycles)
#define ENABLE_BENCHMARK

#define CONCURRENT_WORKLOADS 10

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


DWORD WINAPI TranscodeThread(LPVOID arg);

typedef struct {
    int id;
} ThreadData;


int main()
{
    HANDLE*	pTranscodeThreads = new HANDLE[CONCURRENT_WORKLOADS];
    ThreadData threadData[CONCURRENT_WORKLOADS];

    for(int i=0; i<CONCURRENT_WORKLOADS; ++i)
    {
        threadData[i].id = i;
        pTranscodeThreads[i] = CreateThread(NULL, 0, TranscodeThread, (LPVOID)&threadData[i], 0, NULL);
    }

    // Note: Max number of objects that can be waited for is 64 using WaitForMultiple() call
    // To gracefully handle more than 64 decode threads, use and wait for thread count event instead.
    WaitForMultipleObjects(CONCURRENT_WORKLOADS, pTranscodeThreads, TRUE, INFINITE);

    printf("\nAll transcode workloads complete\n");

    for(int i=0; i<CONCURRENT_WORKLOADS; ++i)
        CloseHandle(pTranscodeThreads[i]);

    delete [] pTranscodeThreads;
}


DWORD WINAPI TranscodeThread(LPVOID arg)
{
    ThreadData *pData = (ThreadData *)arg;
    int id = pData->id;

    mfxStatus sts = MFX_ERR_NONE;

    // =====================================================================
    // Intel Media SDK transcode opaque pipeline setup
    // - Transcode H.264 to H.264, resizing the encoded stream to half the resolution using VPP
    // - Multiple streams are transcoded concurrently
    // - Same input stream is used for all concurrent threadcoding threads
    //

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    char inFile[100] = "bbb640x480.264";
    fopen_s(&fSource, inFile, "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    char outFile[100] = "bbb320x240_xx.264";
    outFile[11] = '0' + (char)(id/10);
    outFile[12] = '0' + (char)(id%10);
    fopen_s(&fSink, outFile, "wb");
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    MFXVideoSession* pmfxSession = NULL;

    // Initialize Media SDK session
    // - MFX_IMPL_AUTO_ANY selects HW accelaration if available (on any adapter)
    // - Version 1.3 is selected since the opaque memory feature was added in this API release
    //   If more recent API features are needed, change the version accordingly
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = {3, 1}; // Note: API 1.3 !
    pmfxSession = new MFXVideoSession;
    MSDK_CHECK_POINTER(pmfxSession, MFX_ERR_NULL_PTR);
    sts = pmfxSession->Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder & encoder & VPP
    MFXVideoDECODE* pmfxDEC = new MFXVideoDECODE(*pmfxSession);
    MSDK_CHECK_POINTER(pmfxDEC, MFX_ERR_NULL_PTR);
    MFXVideoENCODE* pmfxENC = new MFXVideoENCODE(*pmfxSession); 
    MSDK_CHECK_POINTER(pmfxENC, MFX_ERR_NULL_PTR);
    MFXVideoVPP* pmfxVPP = new MFXVideoVPP(*pmfxSession); 
    MSDK_CHECK_POINTER(pmfxVPP, MFX_ERR_NULL_PTR);

    // Set required video parameters for decode
    mfxVideoParam mfxDecParams;
    memset(&mfxDecParams, 0, sizeof(mfxDecParams));
    mfxDecParams.mfx.CodecId = MFX_CODEC_AVC;
    mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;

    // Configure Media SDK to keep more operations in flight
    // - AsyncDepth represents the number of tasks that can be submitted, before synchronizing is required
    // - The choice of AsyncDepth = 3 is quite arbitrary but has proven to result in good performance
    mfxDecParams.AsyncDepth = 3;

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
    
    sts = pmfxDEC->DecodeHeader(&mfxBS, &mfxDecParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    
    // Initialize VPP parameters
    mfxVideoParam VPPParams;
    memset(&VPPParams, 0, sizeof(VPPParams));
    // Input data
    VPPParams.vpp.In.FourCC         = MFX_FOURCC_NV12;
    VPPParams.vpp.In.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;  
    VPPParams.vpp.In.CropX          = 0;
    VPPParams.vpp.In.CropY          = 0; 
    VPPParams.vpp.In.CropW          = mfxDecParams.mfx.FrameInfo.CropW;
    VPPParams.vpp.In.CropH          = mfxDecParams.mfx.FrameInfo.CropH;
    VPPParams.vpp.In.PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.In.FrameRateExtN  = 30;
    VPPParams.vpp.In.FrameRateExtD  = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.In.Width  = MSDK_ALIGN16(VPPParams.vpp.In.CropW);
    VPPParams.vpp.In.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.In.PicStruct)?
                                 MSDK_ALIGN16(VPPParams.vpp.In.CropH) : MSDK_ALIGN32(VPPParams.vpp.In.CropH);
    // Output data
    VPPParams.vpp.Out.FourCC        = MFX_FOURCC_NV12;     
    VPPParams.vpp.Out.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;             
    VPPParams.vpp.Out.CropX         = 0;
    VPPParams.vpp.Out.CropY         = 0; 
    VPPParams.vpp.Out.CropW         = VPPParams.vpp.In.CropW/2;  // Half the resolution of decode stream
    VPPParams.vpp.Out.CropH         = VPPParams.vpp.In.CropH/2;
    VPPParams.vpp.Out.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.Out.FrameRateExtN = 30;
    VPPParams.vpp.Out.FrameRateExtD = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.Out.Width  = MSDK_ALIGN16(VPPParams.vpp.Out.CropW); 
    VPPParams.vpp.Out.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.Out.PicStruct)?
                                    MSDK_ALIGN16(VPPParams.vpp.Out.CropH) : MSDK_ALIGN32(VPPParams.vpp.Out.CropH);

    VPPParams.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;

    // Configure Media SDK to keep more operations in flight
    // - AsyncDepth represents the number of tasks that can be submitted, before synchronizing is required
    VPPParams.AsyncDepth = mfxDecParams.AsyncDepth;


    // Initialize encoder parameters
    mfxVideoParam mfxEncParams;
    memset(&mfxEncParams, 0, sizeof(mfxEncParams));
    mfxEncParams.mfx.CodecId                    = MFX_CODEC_AVC;
    mfxEncParams.mfx.TargetUsage                = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams.mfx.TargetKbps                 = 500;
    mfxEncParams.mfx.RateControlMethod          = MFX_RATECONTROL_VBR; 
    mfxEncParams.mfx.FrameInfo.FrameRateExtN    = 30;
    mfxEncParams.mfx.FrameInfo.FrameRateExtD    = 1;
    mfxEncParams.mfx.FrameInfo.FourCC           = MFX_FOURCC_NV12;
    mfxEncParams.mfx.FrameInfo.ChromaFormat     = MFX_CHROMAFORMAT_YUV420;
    mfxEncParams.mfx.FrameInfo.PicStruct        = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams.mfx.FrameInfo.CropX            = 0; 
    mfxEncParams.mfx.FrameInfo.CropY            = 0;
    mfxEncParams.mfx.FrameInfo.CropW            = VPPParams.vpp.Out.CropW; // Half the resolution of decode stream
    mfxEncParams.mfx.FrameInfo.CropH            = VPPParams.vpp.Out.CropH;
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
    sts = pmfxDEC->QueryIOSurf(&mfxDecParams, &DecRequest);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = pmfxENC->QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);            

    // Query number of required surfaces for VPP
    mfxFrameAllocRequest VPPRequest[2];// [0] - in, [1] - out
    memset(&VPPRequest, 0, sizeof(mfxFrameAllocRequest)*2);
    sts = pmfxVPP->QueryIOSurf(&VPPParams, VPPRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);     


    // Determine the required number of surfaces for decoder output (VPP input) and for VPP output (encoder input)
    mfxU16 nSurfNumDecVPP = DecRequest.NumFrameSuggested + VPPRequest[0].NumFrameSuggested + VPPParams.AsyncDepth;
    mfxU16 nSurfNumVPPEnc = EncRequest.NumFrameSuggested + VPPRequest[1].NumFrameSuggested + VPPParams.AsyncDepth;


    // Initialize shared surfaces for decoder, VPP and encode 
    // - Note that no buffer memory is allocated, for opaque memory this is handled by Media SDK internally
    // - Frame surface array keeps reference to all surfaces
    // - Opaque memory is configured with the mfxExtOpaqueSurfaceAlloc extended buffers 
    mfxFrameSurface1** pSurfaces = new mfxFrameSurface1*[nSurfNumDecVPP];
    MSDK_CHECK_POINTER(pSurfaces, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nSurfNumDecVPP; i++)
    {       
        pSurfaces[i] = new mfxFrameSurface1;
        MSDK_CHECK_POINTER(pSurfaces[i], MFX_ERR_MEMORY_ALLOC);
        memset(pSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pSurfaces[i]->Info), &(DecRequest.Info), sizeof(mfxFrameInfo));
    }

    mfxFrameSurface1** pSurfaces2 = new mfxFrameSurface1*[nSurfNumVPPEnc];
    MSDK_CHECK_POINTER(pSurfaces2, MFX_ERR_MEMORY_ALLOC);
    for (int i = 0; i < nSurfNumVPPEnc; i++)
    {       
        pSurfaces2[i] = new mfxFrameSurface1;
        MSDK_CHECK_POINTER(pSurfaces2[i], MFX_ERR_MEMORY_ALLOC);
        memset(pSurfaces2[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pSurfaces2[i]->Info), &(EncRequest.Info), sizeof(mfxFrameInfo));
    }

    
    mfxExtOpaqueSurfaceAlloc extOpaqueAllocDec;
    memset(&extOpaqueAllocDec, 0, sizeof(extOpaqueAllocDec));
    extOpaqueAllocDec.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    extOpaqueAllocDec.Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    mfxExtBuffer* pExtParamsDec = (mfxExtBuffer*)&extOpaqueAllocDec;

    mfxExtOpaqueSurfaceAlloc extOpaqueAllocVPP;
    memset(&extOpaqueAllocVPP, 0, sizeof(extOpaqueAllocVPP));
    extOpaqueAllocVPP.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    extOpaqueAllocVPP.Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    mfxExtBuffer* pExtParamsVPP = (mfxExtBuffer*)&extOpaqueAllocVPP;

    mfxExtOpaqueSurfaceAlloc extOpaqueAllocEnc;
    memset(&extOpaqueAllocEnc, 0, sizeof(extOpaqueAllocEnc));
    extOpaqueAllocEnc.Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    extOpaqueAllocEnc.Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    mfxExtBuffer* pExtParamsENC = (mfxExtBuffer*)&extOpaqueAllocEnc;

    extOpaqueAllocDec.Out.Surfaces = pSurfaces;
    extOpaqueAllocDec.Out.NumSurface = nSurfNumDecVPP;
    extOpaqueAllocDec.Out.Type = DecRequest.Type;
    
    memcpy(&extOpaqueAllocVPP.In, &extOpaqueAllocDec.Out, sizeof(extOpaqueAllocDec.Out));
    extOpaqueAllocVPP.Out.Surfaces = pSurfaces2;
    extOpaqueAllocVPP.Out.NumSurface = nSurfNumVPPEnc;
    extOpaqueAllocVPP.Out.Type = EncRequest.Type;

    memcpy(&extOpaqueAllocEnc.In, &extOpaqueAllocVPP.Out, sizeof(extOpaqueAllocVPP.Out));

    mfxDecParams.ExtParam = &pExtParamsDec;
    mfxDecParams.NumExtParam = 1;
    VPPParams.ExtParam = &pExtParamsVPP;
    VPPParams.NumExtParam = 1;
    mfxEncParams.ExtParam = &pExtParamsENC;
    mfxEncParams.NumExtParam = 1;

    // Initialize the Media SDK decoder
    sts = pmfxDEC->Init(&mfxDecParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Initialize the Media SDK encoder
    sts = pmfxENC->Init(&mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);    

    // Initialize Media SDK VPP
    sts = pmfxVPP->Init(&VPPParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);    

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    mfxVideoParam par;
    memset(&par, 0, sizeof(par));
    sts = pmfxENC->GetVideoParam(&par);
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

    mfxSyncPoint syncpD, syncpV;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    mfxU32 nFrame		= 0;
    int nIndex			= 0; 
    int nIndex2			= 0; 
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
            sts = pmfxSession->SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            if((nFrame % 100) == 0)
                printf("(%d) Frame number: %d\n", id, nFrame);
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
                nIndex = GetFreeSurfaceIndex(pSurfaces, nSurfNumDecVPP); // Find free frame surface 
                if (MFX_ERR_NOT_FOUND == nIndex)
                    return MFX_ERR_MEMORY_ALLOC;
            }
        
            // Decode a frame asychronously (returns immediately)
            sts = pmfxDEC->DecodeFrameAsync(&mfxBS, pSurfaces[nIndex], &pmfxOutSurface, &syncpD);

            // Ignore warnings if output is available, 
            // if no output and no action required just repeat the DecodeFrameAsync call
            if (MFX_ERR_NONE < sts && syncpD) 
                sts = MFX_ERR_NONE;               
        
            if (MFX_ERR_NONE == sts)
            {         
                nIndex2 = GetFreeSurfaceIndex(pSurfaces2, nSurfNumVPPEnc); // Find free frame surface 
                if (MFX_ERR_NOT_FOUND == nIndex)
                    return MFX_ERR_MEMORY_ALLOC;

                for (;;)
                {
                    // Process a frame asychronously (returns immediately)
                    sts = pmfxVPP->RunFrameVPPAsync(pmfxOutSurface, pSurfaces2[nIndex2], NULL, &syncpV);

                    if (MFX_ERR_NONE < sts && !syncpV) // repeat the call if warning and no output
                    {
                        if (MFX_WRN_DEVICE_BUSY == sts)
                            Sleep(1); // wait if device is busy
                    }
                    else if (MFX_ERR_NONE < sts && syncpV)                 
                    {
                        sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
                        break;
                    }
                    else 
                        break; // not a warning               
                } 

                // VPP needs more data, let decoder decode another frame as input   
                if (MFX_ERR_MORE_DATA == sts)
                {
                    continue;
                }
                else if (MFX_ERR_MORE_SURFACE == sts)
                {
                    // Not relevant for the illustrated workload! Therefore not handled.
                    // Relevant for cases when VPP produces more frames at output than consumes at input. E.g. framerate conversion 30 fps -> 60 fps
                    break;
                }
                else
                    MSDK_BREAK_ON_ERROR(sts); 

                for (;;)
                {    
                    // Encode a frame asychronously (returns immediately)
                    sts = pmfxENC->EncodeFrameAsync(NULL, pSurfaces2[nIndex2], &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 		
            
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
            sts = pmfxSession->SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            if((nFrame % 100) == 0)
                printf("(%d) Frame number: %d\n", id, nFrame);
#endif
        }
        else
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                Sleep(1);

            nIndex = GetFreeSurfaceIndex(pSurfaces, nSurfNumDecVPP); // Find free frame surface
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;            

            // Decode a frame asychronously (returns immediately)
            sts = pmfxDEC->DecodeFrameAsync(NULL, pSurfaces[nIndex], &pmfxOutSurface, &syncpD);

            // Ignore warnings if output is available, 
            // if no output and no action required just repeat the DecodeFrameAsync call       
            if (MFX_ERR_NONE < sts && syncpD) 
                sts = MFX_ERR_NONE;

            if (MFX_ERR_NONE == sts)
            {
                nIndex2 = GetFreeSurfaceIndex(pSurfaces2, nSurfNumVPPEnc); // Find free frame surface 
                if (MFX_ERR_NOT_FOUND == nIndex)
                    return MFX_ERR_MEMORY_ALLOC;

                for (;;)
                {
                    // Process a frame asychronously (returns immediately)
                    sts = pmfxVPP->RunFrameVPPAsync(pmfxOutSurface, pSurfaces2[nIndex2], NULL, &syncpV);

                    if (MFX_ERR_NONE < sts && !syncpV) // repeat the call if warning and no output
                    {
                        if (MFX_WRN_DEVICE_BUSY == sts)
                            Sleep(1); // wait if device is busy
                    }
                    else if (MFX_ERR_NONE < sts && syncpV)                 
                    {
                        sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
                        break;
                    }
                    else 
                        break; // not a warning               
                } 

                // VPP needs more data, let decoder decode another frame as input   
                if (MFX_ERR_MORE_DATA == sts)
                {
                    continue;
                }
                else if (MFX_ERR_MORE_SURFACE == sts)
                {
                    // Not relevant for the illustrated workload! Therefore not handled.
                    // Relevant for cases when VPP produces more frames at output than consumes at input. E.g. framerate conversion 30 fps -> 60 fps
                    break;
                }
                else
                    MSDK_BREAK_ON_ERROR(sts); 

                for (;;)
                {    
                    // Encode a frame asychronously (returns immediately)
                    sts = pmfxENC->EncodeFrameAsync(NULL, pSurfaces2[nIndex2], &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 		
            
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
    // Stage 3: Retrieve buffered frames from VPP
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts)
    {
        nTaskIdx = GetFreeTaskIndex(pTasks, taskPoolSize); // Find free task
        if(MFX_ERR_NOT_FOUND == nTaskIdx)
        {
            // No more free tasks, need to sync
            sts = pmfxSession->SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            if((nFrame % 100) == 0)
                printf("(%d) Frame number: %d\n", id, nFrame);
#endif
        }
        else
        {
            nIndex2 = GetFreeSurfaceIndex(pSurfaces2, nSurfNumVPPEnc); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;

            for (;;)
            {
                // Process a frame asychronously (returns immediately)
                sts = pmfxVPP->RunFrameVPPAsync(NULL, pSurfaces2[nIndex2], NULL, &syncpV);

                if (MFX_ERR_NONE < sts && !syncpV) // repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)
                        Sleep(1); // wait if device is busy
                }
                else if (MFX_ERR_NONE < sts && syncpV)                 
                {
                    sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
                    break;
                }
                else 
                    break; // not a warning               
            } 

            if (MFX_ERR_MORE_SURFACE == sts)
            {
                // Not relevant for the illustrated workload! Therefore not handled.
                // Relevant for cases when VPP produces more frames at output than consumes at input. E.g. framerate conversion 30 fps -> 60 fps
                break;
            }
            else
                MSDK_BREAK_ON_ERROR(sts); 

            for (;;)
            {    
                // Encode a frame asychronously (returns immediately)
                sts = pmfxENC->EncodeFrameAsync(NULL, pSurfaces2[nIndex2], &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 		
            
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

    // MFX_ERR_MORE_DATA indicates that all VPP buffers has been fetched, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    //
    // Stage 4: Retrieve the buffered encoded frames
    //
    while (MFX_ERR_NONE <= sts)
    {       
        nTaskIdx = GetFreeTaskIndex(pTasks, taskPoolSize); // Find free task
        if(MFX_ERR_NOT_FOUND == nTaskIdx)
        {
            // No more free tasks, need to sync
            sts = pmfxSession->SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
            MSDK_BREAK_ON_ERROR(sts);

            pTasks[nFirstSyncTask].syncp = NULL;
            pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
            pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
            nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

            ++nFrame;
#ifdef ENABLE_OUTPUT
            if((nFrame % 100) == 0)
                printf("(%d) Frame number: %d\n", id, nFrame);
#endif
        }
        else
        {
            for (;;)
            {                
                // Encode a frame asychronously (returns immediately)
                sts = pmfxENC->EncodeFrameAsync(NULL, NULL, &pTasks[nTaskIdx].mfxBS, &pTasks[nTaskIdx].syncp); 	

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
    // Stage 5: Sync all remaining tasks in task pool
    //
    while(pTasks[nFirstSyncTask].syncp)
    {
        sts = pmfxSession->SyncOperation(pTasks[nFirstSyncTask].syncp, 60000);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        sts = WriteBitStreamFrame(&pTasks[nFirstSyncTask].mfxBS, fSink);
        MSDK_BREAK_ON_ERROR(sts);

        pTasks[nFirstSyncTask].syncp = NULL;
        pTasks[nFirstSyncTask].mfxBS.DataLength = 0;
        pTasks[nFirstSyncTask].mfxBS.DataOffset = 0;
        nFirstSyncTask = (nFirstSyncTask + 1) % taskPoolSize;

        ++nFrame;
#ifdef ENABLE_OUTPUT
        if((nFrame % 100) == 0)
            printf("(%d) Frame number: %d\n", id, nFrame);
#endif
    }

#ifdef ENABLE_BENCHMARK
    QueryPerformanceCounter(&tEnd);
    double duration = ((double)tEnd.QuadPart - (double)tStart.QuadPart)  / freq;
    printf("\n[%d] Execution time: %3.2fs (%3.2ffps)\n", pData->id, duration, nFrame/duration);
#endif

    // ===================================================================
    // Clean up resources
    //  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
    //    some surfaces may still be locked by internal Media SDK resources.
    
    pmfxENC->Close();
    pmfxDEC->Close();
    pmfxVPP->Close();

    delete pmfxENC;
    delete pmfxDEC;
    delete pmfxVPP;

    pmfxSession->Close();
    delete pmfxSession;

    for (int i = 0; i < nSurfNumDecVPP; i++)
        delete pSurfaces[i];
    for (int i = 0; i < nSurfNumVPPEnc; i++)
        delete pSurfaces2[i];
    MSDK_SAFE_DELETE_ARRAY(pSurfaces);
    MSDK_SAFE_DELETE_ARRAY(pSurfaces2);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);
    for(int i=0;i<taskPoolSize;i++)
        MSDK_SAFE_DELETE_ARRAY(pTasks[i].mfxBS.Data);
    MSDK_SAFE_DELETE_ARRAY(pTasks);

    fclose(fSource);
    fclose(fSink);

    return 0;
}
