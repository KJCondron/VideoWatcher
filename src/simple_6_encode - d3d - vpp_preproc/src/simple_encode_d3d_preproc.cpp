//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#define ENABLE_OUTPUT    // Disabling this flag removes printing of progress (saves CPU cycles)
#define ENABLE_INPUT     // Disabling this flag removes all RGB file reading. Replaced by pre-initialized surface data. Workload runs for 1000 frames
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

    mfxU16 inputWidth = 640;
    mfxU16 inputHeight = 480;

    // =====================================================================
    // Intel Media SDK VPP and encode pipeline setup.
    // - Showcasing RGB32 color conversion to NV12 via VPP then encode
    // - In this example we are encoding an AVC (H.264) stream
    // - Video memory surfaces are used
    //

    // Open input YV12 YUV file
    FILE* fSource;
    fopen_s(&fSource, "input640x480.rgb32", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output elementary stream (ES) H.264 file
    FILE* fSink;
    fopen_s(&fSink, "test_d3d.264", "wb");
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


    // Initialize VPP parameters
    mfxVideoParam VPPParams;
    memset(&VPPParams, 0, sizeof(VPPParams));
    // Input data
    VPPParams.vpp.In.FourCC         = MFX_FOURCC_RGB4;
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
    VPPParams.vpp.Out.CropW         = inputWidth;
    VPPParams.vpp.Out.CropH         = inputHeight;
    VPPParams.vpp.Out.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.Out.FrameRateExtN = 30;
    VPPParams.vpp.Out.FrameRateExtD = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.Out.Width  = MSDK_ALIGN16(VPPParams.vpp.Out.CropW); 
    VPPParams.vpp.Out.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.Out.PicStruct)?
                                    MSDK_ALIGN16(VPPParams.vpp.Out.CropH) : MSDK_ALIGN32(VPPParams.vpp.Out.CropH);

    VPPParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;


    // Create Media SDK encoder
    MFXVideoENCODE mfxENC(mfxSession); 
    // Create Media SDK VPP component
    MFXVideoVPP mfxVPP(mfxSession);

    // Query number of required surfaces for encoder
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC.QueryIOSurf(&mfxEncParams, &EncRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

    // Query number of required surfaces for VPP
    mfxFrameAllocRequest VPPRequest[2];// [0] - in, [1] - out
    memset(&VPPRequest, 0, sizeof(mfxFrameAllocRequest)*2);
    sts = mfxVPP.QueryIOSurf(&VPPParams, VPPRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);             

#ifdef DX11_D3D
    VPPRequest[0].Type |= WILL_WRITE; // Hint to DX11 memory handler that application will write data to VPP input surfaces
#endif

    EncRequest.Type |= MFX_MEMTYPE_FROM_VPPOUT; // surfaces are shared between VPP output and encode input

    // Determine the required number of surfaces for VPP input and for VPP output (encoder input)
    mfxU16 nSurfNumVPPIn = VPPRequest[0].NumFrameSuggested;
    mfxU16 nSurfNumVPPOutEnc = EncRequest.NumFrameSuggested + VPPRequest[1].NumFrameSuggested;

    EncRequest.NumFrameSuggested = nSurfNumVPPOutEnc;
    
    // Allocate required surfaces
    mfxFrameAllocResponse mfxResponseVPPIn;
    mfxFrameAllocResponse mfxResponseVPPOutEnc;
    sts = mfxAllocator.Alloc(mfxAllocator.pthis, &VPPRequest[0], &mfxResponseVPPIn);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    sts = mfxAllocator.Alloc(mfxAllocator.pthis, &EncRequest, &mfxResponseVPPOutEnc);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Allocate surface headers (mfxFrameSurface1) for VPPIn
    mfxFrameSurface1** pmfxSurfacesVPPIn = new mfxFrameSurface1*[nSurfNumVPPIn];
    MSDK_CHECK_POINTER(pmfxSurfacesVPPIn, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < nSurfNumVPPIn; i++)
    {
        pmfxSurfacesVPPIn[i] = new mfxFrameSurface1;
        memset(pmfxSurfacesVPPIn[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfacesVPPIn[i]->Info), &(VPPParams.vpp.In), sizeof(mfxFrameInfo));
        pmfxSurfacesVPPIn[i]->Data.MemId = mfxResponseVPPIn.mids[i]; 

#ifndef ENABLE_INPUT
        // In case simulating direct access to frames we initialize the allocated surfaces with default pattern
        // - For true benchmark comparisons to async workloads all surfaces must have the same data
#ifndef DX11_D3D
        IDirect3DSurface9 *pSurface;
        D3DSURFACE_DESC desc;
        D3DLOCKED_RECT locked;
        pSurface = (IDirect3DSurface9 *)mfxResponseVPPIn.mids[i];
        pSurface->GetDesc(&desc);
        pSurface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK);
        memset((mfxU8 *)locked.pBits, 100, desc.Height*locked.Pitch);  // RGBA
        pSurface->UnlockRect();
#else
        // For now, just leave D3D11 surface data uninitialized
#endif
#endif
    }  

    mfxFrameSurface1** pVPPSurfacesVPPOutEnc = new mfxFrameSurface1*[nSurfNumVPPOutEnc];
    MSDK_CHECK_POINTER(pVPPSurfacesVPPOutEnc, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < nSurfNumVPPOutEnc; i++)
    {       
        pVPPSurfacesVPPOutEnc[i] = new mfxFrameSurface1;
        memset(pVPPSurfacesVPPOutEnc[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pVPPSurfacesVPPOutEnc[i]->Info), &(VPPParams.vpp.Out), sizeof(mfxFrameInfo));
        pVPPSurfacesVPPOutEnc[i]->Data.MemId = mfxResponseVPPOutEnc.mids[i];
    }  


    // Disable default VPP operations
    mfxExtVPPDoNotUse extDoNotUse;
    memset(&extDoNotUse, 0, sizeof(mfxExtVPPDoNotUse));
    extDoNotUse.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
    extDoNotUse.Header.BufferSz = sizeof(mfxExtVPPDoNotUse);
    extDoNotUse.NumAlg  = 4;
    extDoNotUse.AlgList = new mfxU32 [extDoNotUse.NumAlg];    
    MSDK_CHECK_POINTER(extDoNotUse.AlgList,  MFX_ERR_MEMORY_ALLOC);
    extDoNotUse.AlgList[0] = MFX_EXTBUFF_VPP_DENOISE; // turn off denoising (on by default)
    extDoNotUse.AlgList[1] = MFX_EXTBUFF_VPP_SCENE_ANALYSIS; // turn off scene analysis (on by default)
    extDoNotUse.AlgList[2] = MFX_EXTBUFF_VPP_DETAIL; // turn off detail enhancement (on by default)
    extDoNotUse.AlgList[3] = MFX_EXTBUFF_VPP_PROCAMP; // turn off processing amplified (on by default)


    // Initialize the Media SDK encoder
    sts = mfxENC.Init(&mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);    

    // Initialize Media SDK VPP
    sts = mfxVPP.Init(&VPPParams);
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
    // Start processing frames
    //
 
#ifdef ENABLE_BENCHMARK
    LARGE_INTEGER tStart, tEnd;
    QueryPerformanceFrequency(&tStart);
    double freq = (double)tStart.QuadPart;
    QueryPerformanceCounter(&tStart);
#endif

    int nEncSurfIdx = 0;
    int nVPPSurfIdx = 0;
    mfxSyncPoint syncpVPP, syncpEnc;
    mfxU32 nFrame = 0;

    //
    // Stage 1: Main VPP/encoding loop
    //
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts)        
    {        
        nVPPSurfIdx = GetFreeSurfaceIndex(pmfxSurfacesVPPIn, nSurfNumVPPIn); // Find free input frame surface
        if (MFX_ERR_NOT_FOUND == nVPPSurfIdx)
            return MFX_ERR_MEMORY_ALLOC;

        // Surface locking required when read/write D3D surfaces
        sts = mfxAllocator.Lock(mfxAllocator.pthis, pmfxSurfacesVPPIn[nVPPSurfIdx]->Data.MemId, &(pmfxSurfacesVPPIn[nVPPSurfIdx]->Data));
        MSDK_BREAK_ON_ERROR(sts);

        sts = LoadRawRGBFrame(pmfxSurfacesVPPIn[nVPPSurfIdx], fSource); // Load frame from file into surface
        MSDK_BREAK_ON_ERROR(sts);
           
        sts = mfxAllocator.Unlock(mfxAllocator.pthis, pmfxSurfacesVPPIn[nVPPSurfIdx]->Data.MemId, &(pmfxSurfacesVPPIn[nVPPSurfIdx]->Data));
        MSDK_BREAK_ON_ERROR(sts);

        nEncSurfIdx = GetFreeSurfaceIndex(pVPPSurfacesVPPOutEnc, nSurfNumVPPOutEnc); // Find free output frame surface
        if (MFX_ERR_NOT_FOUND == nEncSurfIdx)
            return MFX_ERR_MEMORY_ALLOC;

        // Process a frame asychronously (returns immediately)
        sts = mfxVPP.RunFrameVPPAsync(pmfxSurfacesVPPIn[nVPPSurfIdx], pVPPSurfacesVPPOutEnc[nEncSurfIdx], NULL, &syncpVPP);
        if (MFX_ERR_MORE_DATA == sts)
            continue;

        // MFX_ERR_MORE_SURFACE means output is ready but need more surface (example: Frame Rate Conversion 30->60)
        // * Not handled in this example!

        MSDK_BREAK_ON_ERROR(sts);
                   
        for (;;)
        {    
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, pVPPSurfacesVPPOutEnc[nEncSurfIdx], &mfxBS, &syncpEnc); 
           
            if (MFX_ERR_NONE < sts && !syncpEnc) // Repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // Wait if device is busy, then repeat the same call            
            }
            else if (MFX_ERR_NONE < sts && syncpEnc)                 
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
            sts = mfxSession.SyncOperation(syncpEnc, 60000); // Synchronize. Wait until encoded frame is ready
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
    // Stage 2: Retrieve the buffered VPP frames
    //
    while (MFX_ERR_NONE <= sts)
    {       
        nEncSurfIdx = GetFreeSurfaceIndex(pVPPSurfacesVPPOutEnc, nSurfNumVPPOutEnc); // Find free output frame surface
        if (MFX_ERR_NOT_FOUND == nEncSurfIdx)
            return MFX_ERR_MEMORY_ALLOC;

        // Process a frame asychronously (returns immediately)
        sts = mfxVPP.RunFrameVPPAsync(NULL, pVPPSurfacesVPPOutEnc[nEncSurfIdx], NULL, &syncpVPP);
        MSDK_BREAK_ON_ERROR(sts);

        for (;;)
        {    
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, pVPPSurfacesVPPOutEnc[nEncSurfIdx], &mfxBS, &syncpEnc); 
           
            if (MFX_ERR_NONE < sts && !syncpEnc) // Repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // Wait if device is busy, then repeat the same call            
            }
            else if (MFX_ERR_NONE < sts && syncpEnc)                 
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
            sts = mfxSession.SyncOperation(syncpEnc, 60000); // Synchronize. Wait until encoded frame is ready
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

    //
    // Stage 3: Retrieve the buffered encoder frames
    //
    while (MFX_ERR_NONE <= sts)
    {       
        for (;;)
        {                
            // Encode a frame asychronously (returns immediately)
            sts = mfxENC.EncodeFrameAsync(NULL, NULL, &mfxBS, &syncpEnc);  

            if (MFX_ERR_NONE < sts && !syncpEnc) // Repeat the call if warning and no output
            {
                if (MFX_WRN_DEVICE_BUSY == sts)                
                    Sleep(1); // Wait if device is busy, then repeat the same call                 
            }
            else if (MFX_ERR_NONE < sts && syncpEnc)                 
            {
                sts = MFX_ERR_NONE; // Ignore warnings if output is available 
                break;
            }
            else
                break;
        }            

        if(MFX_ERR_NONE == sts)
        {
            sts = mfxSession.SyncOperation(syncpEnc, 60000); // Synchronize. Wait until encoded frame is ready
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
    mfxVPP.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nSurfNumVPPIn; i++)
        delete pmfxSurfacesVPPIn[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfacesVPPIn);
    for (int i = 0; i < nSurfNumVPPOutEnc; i++)
        delete pVPPSurfacesVPPOutEnc[i];
    MSDK_SAFE_DELETE_ARRAY(pVPPSurfacesVPPOutEnc);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);
    MSDK_SAFE_DELETE_ARRAY(extDoNotUse.AlgList);

    fclose(fSource);
    fclose(fSink);

    CleanupHWDevice();

    return 0;
}