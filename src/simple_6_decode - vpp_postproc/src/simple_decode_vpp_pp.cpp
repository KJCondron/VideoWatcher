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
    // - For simplistic memory management, system memory surfaces are used to store the decoded frames
    //   (Note that when using HW acceleration D3D surfaces are prefered, for better performance)
    //
    //  - VPP used to post process (resize) the frame
    //

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    fopen_s(&fSource, "bbb1920x1080.264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output YUV file
    FILE* fSink;
    fopen_s(&fSink, "dectest_960x540.yuv", "wb");
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

    // Create Media SDK decoder
    MFXVideoDECODE mfxDEC(mfxSession);
    // Create Media SDK VPP component
    MFXVideoVPP mfxVPP(mfxSession); 

    // Set required video parameters for decode
    // - In this example we are decoding an AVC (H.264) stream
    // - For simplistic memory management, system memory surfaces are used to store the decoded frames
    //   (Note that when using HW acceleration D3D surfaces are prefered, for better performance)
    mfxVideoParam mfxVideoParams;
    memset(&mfxVideoParams, 0, sizeof(mfxVideoParams));
    mfxVideoParams.mfx.CodecId = MFX_CODEC_AVC;
    mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    
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
    VPPParams.vpp.In.CropW          = mfxVideoParams.mfx.FrameInfo.CropW;
    VPPParams.vpp.In.CropH          = mfxVideoParams.mfx.FrameInfo.CropH;
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
    VPPParams.vpp.Out.CropW         = VPPParams.vpp.In.CropW/2;  // Resize to half size resolution
    VPPParams.vpp.Out.CropH         = VPPParams.vpp.In.CropH/2;
    VPPParams.vpp.Out.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;
    VPPParams.vpp.Out.FrameRateExtN = 30;
    VPPParams.vpp.Out.FrameRateExtD = 1;
    // width must be a multiple of 16 
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture  
    VPPParams.vpp.Out.Width  = MSDK_ALIGN16(VPPParams.vpp.Out.CropW); 
    VPPParams.vpp.Out.Height = (MFX_PICSTRUCT_PROGRESSIVE == VPPParams.vpp.Out.PicStruct)?
                                    MSDK_ALIGN16(VPPParams.vpp.Out.CropH) : MSDK_ALIGN32(VPPParams.vpp.Out.CropH);

    VPPParams.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    // Query number of required surfaces for decoder
    mfxFrameAllocRequest DecRequest;
    memset(&DecRequest, 0, sizeof(DecRequest));
    sts = mfxDEC.QueryIOSurf(&mfxVideoParams, &DecRequest);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Query number of required surfaces for VPP
    mfxFrameAllocRequest VPPRequest[2];// [0] - in, [1] - out
    memset(&VPPRequest, 0, sizeof(mfxFrameAllocRequest)*2);
    sts = mfxVPP.QueryIOSurf(&VPPParams, VPPRequest);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);       


    // Determine the required number of surfaces for decoder output (VPP input) and for VPP output 
    mfxU16 nSurfNumDecVPP = DecRequest.NumFrameSuggested + VPPRequest[0].NumFrameSuggested;
    mfxU16 nSurfNumVPPOut = VPPRequest[1].NumFrameSuggested;


    // Allocate surfaces for decoder and VPP In
    // - Width and height of buffer must be aligned, a multiple of 32 
    // - Frame surface array keeps pointers all surface planes and general frame info
    mfxU16 width = (mfxU16)MSDK_ALIGN32(DecRequest.Info.Width);
    mfxU16 height = (mfxU16)MSDK_ALIGN32(DecRequest.Info.Height);
    mfxU8  bitsPerPixel = 12;  // NV12 format is a 12 bits per pixel format
    mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffers = (mfxU8 *)new mfxU8[surfaceSize * nSurfNumDecVPP];
    
    mfxFrameSurface1** pmfxSurfaces = new mfxFrameSurface1*[nSurfNumDecVPP];
    MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < nSurfNumDecVPP; i++)
    {       
        pmfxSurfaces[i] = new mfxFrameSurface1;
        memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfaces[i]->Info), &(mfxVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pmfxSurfaces[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
        pmfxSurfaces[i]->Data.U = pmfxSurfaces[i]->Data.Y + width * height;
        pmfxSurfaces[i]->Data.V = pmfxSurfaces[i]->Data.U + 1;
        pmfxSurfaces[i]->Data.Pitch = width;
    }  

    // Allocate surfaces for VPP Out
    // - Width and height of buffer must be aligned, a multiple of 32 
    // - Frame surface array keeps pointers all surface planes and general frame info
    width = (mfxU16)MSDK_ALIGN32(VPPRequest[1].Info.Width);
    height = (mfxU16)MSDK_ALIGN32(VPPRequest[1].Info.Height);
    bitsPerPixel = 12;  // NV12 format is a 12 bits per pixel format
    surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffers2 = (mfxU8 *)new mfxU8[surfaceSize * nSurfNumVPPOut];
    
    mfxFrameSurface1** pmfxSurfaces2 = new mfxFrameSurface1*[nSurfNumVPPOut];
    MSDK_CHECK_POINTER(pmfxSurfaces2, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < nSurfNumVPPOut; i++)
    {       
        pmfxSurfaces2[i] = new mfxFrameSurface1;
        memset(pmfxSurfaces2[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfaces2[i]->Info), &(VPPParams.vpp.Out), sizeof(mfxFrameInfo));
        pmfxSurfaces2[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
        pmfxSurfaces2[i]->Data.U = pmfxSurfaces2[i]->Data.Y + width * height;
        pmfxSurfaces2[i]->Data.V = pmfxSurfaces2[i]->Data.U + 1;
        pmfxSurfaces2[i]->Data.Pitch = width;
    }  

    // Initialize the Media SDK decoder
    sts = mfxDEC.Init(&mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Initialize Media SDK VPP
    sts = mfxVPP.Init(&VPPParams);
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

    mfxSyncPoint syncpD;
    mfxSyncPoint syncpV;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    int nIndex = 0;  
    int nIndex2 = 0;  
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
            nIndex = GetFreeSurfaceIndex(pmfxSurfaces, nSurfNumDecVPP); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;
        }
        
        // Decode a frame asychronously (returns immediately)
        sts = mfxDEC.DecodeFrameAsync(&mfxBS, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncpD);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call
        if (MFX_ERR_NONE < sts && syncpD) 
            sts = MFX_ERR_NONE;


        if (MFX_ERR_NONE == sts)
        {         
            nIndex2 = GetFreeSurfaceIndex(pmfxSurfaces2, nSurfNumVPPOut); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;

            for (;;)
            {
                // Process a frame asychronously (returns immediately)
                sts = mfxVPP.RunFrameVPPAsync(pmfxOutSurface, pmfxSurfaces2[nIndex2], NULL, &syncpV);

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
        }


        if (MFX_ERR_NONE == sts)
            sts = mfxSession.SyncOperation(syncpV, 60000); // Synchronize. Wait until decoded frame is ready
        
        if (MFX_ERR_NONE == sts)
        {
            ++nFrame;
#ifdef ENABLE_OUTPUT
            sts = WriteRawFrame(pmfxSurfaces2[nIndex2], fSink);
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

        nIndex = GetFreeSurfaceIndex(pmfxSurfaces, nSurfNumDecVPP); // Find free frame surface
        if (MFX_ERR_NOT_FOUND == nIndex)
            return MFX_ERR_MEMORY_ALLOC;            

        // Decode a frame asychronously (returns immediately)
        sts = mfxDEC.DecodeFrameAsync(NULL, pmfxSurfaces[nIndex], &pmfxOutSurface, &syncpD);

        // Ignore warnings if output is available, 
        // if no output and no action required just repeat the DecodeFrameAsync call        
        if (MFX_ERR_NONE < sts && syncpD) 
            sts = MFX_ERR_NONE;


        if (MFX_ERR_NONE == sts)
        {         
            nIndex2 = GetFreeSurfaceIndex(pmfxSurfaces2, nSurfNumVPPOut); // Find free frame surface 
            if (MFX_ERR_NOT_FOUND == nIndex)
                return MFX_ERR_MEMORY_ALLOC;

            for (;;)
            {
                // Process a frame asychronously (returns immediately)
                sts = mfxVPP.RunFrameVPPAsync(pmfxOutSurface, pmfxSurfaces2[nIndex2], NULL, &syncpV);

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
        }


        if (MFX_ERR_NONE == sts)
            sts = mfxSession.SyncOperation(syncpV, 60000); // Synchronize. Waits until decoded frame is ready

        if (MFX_ERR_NONE == sts)
        {
            ++nFrame;
#ifdef ENABLE_OUTPUT
            sts = WriteRawFrame(pmfxSurfaces2[nIndex2], fSink);
            MSDK_BREAK_ON_ERROR(sts);

            printf("Frame number: %d\r", nFrame);   
#endif
        }
    }

    // MFX_ERR_MORE_DATA means that decoder is done with buffered frames, need to go to VPP buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);          
      
    //
    // Stage 3: Retrieve the buffered VPP frames
    //
     while (MFX_ERR_NONE <= sts)
    {       
        nIndex2 = GetFreeSurfaceIndex(pmfxSurfaces2, nSurfNumVPPOut); // Find free frame surface 
        if (MFX_ERR_NOT_FOUND == nIndex2)
            return MFX_ERR_MEMORY_ALLOC;

        // Process a frame asychronously (returns immediately)
        sts = mfxVPP.RunFrameVPPAsync(NULL, pmfxSurfaces2[nIndex2], NULL, &syncpV);
        MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_SURFACE);
        MSDK_BREAK_ON_ERROR(sts);
        
        sts = mfxSession.SyncOperation(syncpV, 60000); // Synchronize. Wait until frame processing is ready
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        ++nFrame;
#ifdef ENABLE_OUTPUT
        sts = WriteRawFrame(pmfxSurfaces2[nIndex2], fSink);
        MSDK_BREAK_ON_ERROR(sts);

        printf("Frame number: %d\r", nFrame);
#endif
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
    mfxVPP.Close();
    // mfxSession closed automatically on destruction

    for (int i = 0; i < nSurfNumDecVPP; i++)
        delete pmfxSurfaces[i];
    for (int i = 0; i < nSurfNumVPPOut; i++)
        delete pmfxSurfaces2[i];
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces);
    MSDK_SAFE_DELETE_ARRAY(pmfxSurfaces2);
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffers);
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffers2);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

    fclose(fSource);
    fclose(fSink);

    return 0;
}
