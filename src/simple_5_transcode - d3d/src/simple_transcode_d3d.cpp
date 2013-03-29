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
    // Intel Media SDK transcode D3D pipeline setup
    // - In this example we are decoding and encoding an AVC (H.264) stream
    // - Video memory surfaces are used
    //

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "bbb1920x1080_trans_d3d.264", "wb");
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

    // Create Media SDK decoder & encoder
    MFXVideoDECODE mfxDEC(mfxSession);
    MFXVideoENCODE mfxENC(mfxSession); 


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


    // Set required video parameters for decode
    mfxVideoParam mfxDecParams;
    memset(&mfxDecParams, 0, sizeof(mfxDecParams));
    mfxDecParams.mfx.CodecId = MFX_CODEC_AVC;

    mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

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
    
    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;


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

    DecRequest.NumFrameMin = DecRequest.NumFrameMin + EncRequest.NumFrameMin;
    DecRequest.NumFrameSuggested = DecRequest.NumFrameSuggested + EncRequest.NumFrameSuggested;

    // Allocate required surfaces for decoder and encoder
    mfxFrameAllocResponse DecResponse;
    sts = mfxAllocator.Alloc(mfxAllocator.pthis, &DecRequest, &DecResponse);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU16 numSurfacesDec = DecResponse.NumFrameActual;

    // Allocate surface headers (mfxFrameSurface1) for decoder and encoder
    mfxFrameSurface1** pmfxSurfacesDec = new mfxFrameSurface1*[numSurfacesDec];
    MSDK_CHECK_POINTER(pmfxSurfacesDec, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < numSurfacesDec; i++)
    {       
        pmfxSurfacesDec[i] = new mfxFrameSurface1;
        memset(pmfxSurfacesDec[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfacesDec[i]->Info), &(mfxDecParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pmfxSurfacesDec[i]->Data.MemId = DecResponse.mids[i]; // MID (memory id) represent one D3D NV12 surface  
    }  

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

    // Prepare Media SDK bit stream buffer for encoder
    mfxBitstream mfxEncBS; 
    memset(&mfxEncBS, 0, sizeof(mfxEncBS));
    mfxEncBS.MaxLength = par.mfx.BufferSizeInKB * 1000;
    mfxEncBS.Data = new mfxU8[mfxEncBS.MaxLength];
    MSDK_CHECK_POINTER(mfxEncBS.Data, MFX_ERR_MEMORY_ALLOC);


    // ===================================
    // Start transcoding the frames
    //

#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    mfxSyncPoint syncpD, syncpE;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    int nIndex = 0;  
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main transcoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts)          
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
            nIndex = GetFreeSurfaceIndex(pmfxSurfacesDec, numSurfacesDec); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;
        }
        
        // Decode a frame asychronously (returns immediately)
        sts = mfxDEC.DecodeFrameAsync(&mfxBS, pmfxSurfacesDec[nIndex], &pmfxOutSurface, &syncpD);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call
        if (MFX_ERR_NONE < sts && syncpD) 
            sts = MFX_ERR_NONE;               
        
        if (MFX_ERR_NONE == sts)
        {         
            for (;;)
            {    
                // Encode a frame asychronously (returns immediately)
                sts = mfxENC.EncodeFrameAsync(NULL, pmfxOutSurface, &mfxEncBS, &syncpE);
            
                if (MFX_ERR_NONE < sts && !syncpE) // repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)                
                        Sleep(1); // wait if device is busy                
                }
                else if (MFX_ERR_NONE < sts && syncpE)                 
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

            if(MFX_ERR_NONE == sts)
            {
                sts = mfxSession.SyncOperation(syncpE, 60000); // Synchronize. Wait until frame processing is ready  
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

                sts = WriteBitStreamFrame(&mfxEncBS, fSink);
                MSDK_BREAK_ON_ERROR(sts);

                ++nFrame;
#ifdef ENABLE_OUTPUT
                printf("Frame number: %d\r", nFrame);
#endif
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
        if (MFX_WRN_DEVICE_BUSY == sts)
            Sleep(1);

        nIndex = GetFreeSurfaceIndex(pmfxSurfacesDec, numSurfacesDec); // Find free frame surface
        if (MFX_ERR_NOT_FOUND == nIndex)
            return MFX_ERR_MEMORY_ALLOC;            

        // Decode a frame asychronously (returns immediately)
        sts = mfxDEC.DecodeFrameAsync(NULL, pmfxSurfacesDec[nIndex], &pmfxOutSurface, &syncpD);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call       
        if (MFX_ERR_NONE < sts && syncpD) 
            sts = MFX_ERR_NONE;

        if (MFX_ERR_NONE == sts)
        {
            for (;;)
            {    
                // Encode a frame asychronously (returns immediately)
                sts = mfxENC.EncodeFrameAsync(NULL, pmfxOutSurface, &mfxEncBS, &syncpE);
            
                if (MFX_ERR_NONE < sts && !syncpE) // repeat the call if warning and no output
                {
                    if (MFX_WRN_DEVICE_BUSY == sts)                
                        Sleep(1); // wait if device is busy                
                }
                else if (MFX_ERR_NONE < sts && syncpE)                 
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

            if(MFX_ERR_NONE == sts)
            {
                sts = mfxSession.SyncOperation(syncpE, 60000); // Synchronize. Wait until frame processing is ready    
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

                sts = WriteBitStreamFrame(&mfxEncBS, fSink);
                MSDK_BREAK_ON_ERROR(sts);

                ++nFrame;
#ifdef ENABLE_OUTPUT
                printf("Frame number: %d\r", nFrame);
#endif
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
        for (;;)
        {                
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, NULL, &mfxEncBS, &syncpE);            

            if (MFX_ERR_NONE < sts && !syncpE) // repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // wait if device is busy                
            }
            else if (MFX_ERR_NONE < sts && syncpE)                 
            {
                sts = MFX_ERR_NONE; // ignore warnings if output is available                                    
                break;
            }
            else
                break;
        }            

        if(MFX_ERR_NONE == sts)
        {
            sts = mfxSession.SyncOperation(syncpE, 60000); // Synchronize. Wait until frame processing is ready     
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            sts = WriteBitStreamFrame(&mfxEncBS, fSink);
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
    mfxDEC.Close();
    // mfxSession closed automatically on destruction
    
    for (int i = 0; i < numSurfacesDec; i++)
        delete pmfxSurfacesDec[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfacesDec);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);
    MSDK_SAFE_DELETE_ARRAY(mfxEncBS.Data);

    fclose(fSource);
    fclose(fSink);

    return 0;
}
