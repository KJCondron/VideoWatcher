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

    // Open input H.264 elementary stream (ES) file
    FILE* fSource;
    fopen_s(&fSource, "C:\\Users\\Karl\\Dropbox\\Public\\_S15.G41.Stormers.v.Crusaders_track1.h264", "rb");
    MSDK_CHECK_POINTER(fSource, MFX_ERR_NULL_PTR);

    // Create output YUV file
    FILE* fSink;
    fopen_s(&fSink, "C:\\Users\\Karl\\Documents\\GitHub\\VideoWatcher\\dectest1.yuv", "wb");

	FILE* fdebug;
    fopen_s(&fdebug, "C:\\Users\\Karl\\Documents\\GitHub\\VideoWatcher\\debug.txt", "w");
    
    MSDK_CHECK_POINTER(fSink, MFX_ERR_NULL_PTR);

    // Initialize Intel Media SDK session
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

    // Set required video parameters for decode
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

    // Validate video decode parameters (optional)
    // sts = mfxDEC.Query(&mfxVideoParams, &mfxVideoParams);  

    // Query number of required surfaces for decoder
    mfxFrameAllocRequest Request;
    memset(&Request, 0, sizeof(Request));
    sts = mfxDEC.QueryIOSurf(&mfxVideoParams, &Request);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU16 numSurfaces = Request.NumFrameSuggested;

    // Allocate surfaces for decoder
    // - Width and height of buffer must be aligned, a multiple of 32 
    // - Frame surface array keeps pointers all surface planes and general frame info
    mfxU16 width = (mfxU16)MSDK_ALIGN32(Request.Info.Width);
    mfxU16 height = (mfxU16)MSDK_ALIGN32(Request.Info.Height);
    mfxU8  bitsPerPixel = 12;  // NV12 format is a 12 bits per pixel format
    mfxU32 surfaceSize = width * height * bitsPerPixel / 8;
    mfxU8* surfaceBuffers = (mfxU8 *)new mfxU8[surfaceSize * numSurfaces];
    
    mfxFrameSurface1** pmfxSurfaces = new mfxFrameSurface1*[numSurfaces];
    MSDK_CHECK_POINTER(pmfxSurfaces, MFX_ERR_MEMORY_ALLOC);       
    for (int i = 0; i < numSurfaces; i++)
    {       
        pmfxSurfaces[i] = new mfxFrameSurface1;
        memset(pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        memcpy(&(pmfxSurfaces[i]->Info), &(mfxVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        pmfxSurfaces[i]->Data.Y = &surfaceBuffers[surfaceSize * i];
        pmfxSurfaces[i]->Data.U = pmfxSurfaces[i]->Data.Y + width * height;
        pmfxSurfaces[i]->Data.V = pmfxSurfaces[i]->Data.U + 1;
        pmfxSurfaces[i]->Data.Pitch = width;
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

	Region blackReg( "black", 13, 102, 6, 2 );
	Region whiteishReg( "rand", 10, 65, 6, 2 );
	Region clockReg( "clock", 23, 104, 4, 1 );

	Regions r;
	r.push_back(blackReg);
	r.push_back(whiteishReg);
	r.push_back(clockReg);

	typedef std::pair< std::string, std::vector< FrameSection> > FrameSections;
	std::vector< FrameSection > empty;
	std::vector< FrameSections > frameslist;
	//std::fill_n(frameslist.begin(), r.size(), empty);

	for( Regions::iterator it = r.begin(); it != r.end(); ++it) 
		frameslist.push_back( std::make_pair(it->m_name, empty) );
	
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
			if(nFrame > 22200 && nFrame < 22200 + 1000 * 50 + 1 && nFrame % 1000 == 0)
			{
            
				printf("Writing Frame number: %d\r", (nFrame - 22200) / 1000);
			
				fprintf(fdebug, "Writing Frame number: %d\n", (nFrame - 22200) / 1000);	

				sts = WriteRawFrame(pmfxOutSurface, fSink, fdebug, r);
				MSDK_BREAK_ON_ERROR(sts);
				
				Regions::const_iterator r_it = r.begin();
				std::vector< FrameSections >::iterator ll_it = frameslist.begin();

				for( ; r_it != r.end(); ++r_it, ++ll_it )
					(*ll_it).second.push_back( GetFrameSection( pmfxOutSurface, *r_it ) );
				
				 /*if( pmfxOutSurface->Info.FourCC == MFX_FOURCC_NV12 )       
					 printf("4cc - NV12 \n");

				 if( pmfxOutSurface->Info.FourCC == MFX_FOURCC_YV12 )       
					 printf("4cc - YV12 \n");

				 if( pmfxOutSurface->Info.FourCC == MFX_FOURCC_YUY2 )       
					 printf("4cc - YUY2 \n");

				 if( pmfxOutSurface->Info.FourCC == MFX_FOURCC_RGB3 )       
					 printf("4cc - RGB3 \n");

				 if( pmfxOutSurface->Info.FourCC == MFX_FOURCC_RGB4 )       
					 printf("4cc - RGB4 \n");*/

            
			}

			if(nFrame > 22200 + 500 * 3000 + 1)
			{
				
				for( std::vector< FrameSections >::iterator l_it = frameslist.begin();
					l_it != frameslist.end();
					++l_it )
				{
					Stats scorestdevs = CalcUVPixelStdev(l_it->second);
					writeStatsDebug(fdebug, l_it->first, scorestdevs);
				}


				fclose(fdebug);
				fclose(fSink);
			}

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
            sts = WriteRawFrame(pmfxOutSurface, fSink);
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
    MSDK_SAFE_DELETE_ARRAY(surfaceBuffers);
    MSDK_SAFE_DELETE_ARRAY(mfxBS.Data);

    fclose(fSource);
    fclose(fSink);

    return 0;
}
