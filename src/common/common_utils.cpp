//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include "common_utils.h"
#include <iostream>
// =================================================================
// Utility functions, not directly tied to Intel Media SDK functionality
//

mfxStatus ReadPlaneData(mfxU16 w, mfxU16 h, mfxU8 *buf, mfxU8 *ptr, mfxU16 pitch, mfxU16 offset, FILE* fSource)
{
    mfxU32 nBytesRead;
    for (mfxU16 i = 0; i < h; i++) 
    {
        nBytesRead = (mfxU32)fread(buf, 1, w, fSource);
        if (w != nBytesRead)
            return MFX_ERR_MORE_DATA;
        for (mfxU16 j = 0; j < w; j++)
            ptr[i * pitch + j * 2 + offset] = buf[j];
    }
    return MFX_ERR_NONE;
}

mfxStatus LoadRawFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim)
{
    if(bSim) {
        // Simulate instantaneous access to 1000 "empty" frames. 
        static int frameCount = 0;
        if(1000 == frameCount++) return MFX_ERR_MORE_DATA;
        else return MFX_ERR_NONE;   
    }

    mfxStatus sts = MFX_ERR_NONE;
    mfxU32 nBytesRead;
    mfxU16 w, h, i, pitch; 
    mfxU8 *ptr;
    mfxFrameInfo* pInfo = &pSurface->Info;
    mfxFrameData* pData = &pSurface->Data;

    if (pInfo->CropH > 0 && pInfo->CropW > 0) {
        w = pInfo->CropW;
        h = pInfo->CropH;
    } 
    else {
        w = pInfo->Width;
        h = pInfo->Height;
    }

    pitch = pData->Pitch;
    ptr = pData->Y + pInfo->CropX + pInfo->CropY * pData->Pitch;

    // read luminance plane
    for(i = 0; i < h; i++) 
    {
        nBytesRead = (mfxU32)fread(ptr + i * pitch, 1, w, fSource);
        if (w != nBytesRead)
            return MFX_ERR_MORE_DATA;
    }

    mfxU8 buf[2048]; // maximum supported chroma width for nv12
    w /= 2;
    h /= 2;            
    ptr = pData->UV + pInfo->CropX + (pInfo->CropY / 2) * pitch;
    if (w > 2048)
        return MFX_ERR_UNSUPPORTED;

    // load U
    sts = ReadPlaneData(w, h, buf, ptr, pitch, 0, fSource);
    if(MFX_ERR_NONE != sts) return sts;
    // load V
    ReadPlaneData(w, h, buf, ptr, pitch, 1, fSource);
    if(MFX_ERR_NONE != sts) return sts;

    return MFX_ERR_NONE;   
}

mfxStatus LoadRawRGBFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim)
{
    if(bSim) {
        // Simulate instantaneous access to 1000 "empty" frames. 
        static int frameCount = 0;
        if(1000 == frameCount++) return MFX_ERR_MORE_DATA;
        else return MFX_ERR_NONE;   
    }

    size_t nBytesRead;
    mfxU16 w, h; 
    mfxFrameInfo* pInfo = &pSurface->Info;

    if (pInfo->CropH > 0 && pInfo->CropW > 0) {
        w = pInfo->CropW;
        h = pInfo->CropH;
    } 
    else {
        w = pInfo->Width;
        h = pInfo->Height;
    }

    for(mfxU16 i = 0; i < h; i++) 
    {
        nBytesRead = (mfxU32)fread(pSurface->Data.B + i * pSurface->Data.Pitch, 1, w*4, fSource);
        if (w*4 != nBytesRead)
            return MFX_ERR_MORE_DATA;
    }

    return MFX_ERR_NONE;   
}

mfxStatus WriteBitStreamFrame(mfxBitstream *pMfxBitstream, FILE* fSink)
{    
    mfxU32 nBytesWritten = (mfxU32)fwrite(pMfxBitstream->Data + pMfxBitstream->DataOffset, 1, pMfxBitstream->DataLength, fSink);
    if(nBytesWritten != pMfxBitstream->DataLength)
        return MFX_ERR_UNDEFINED_BEHAVIOR;

    pMfxBitstream->DataLength = 0;

    return MFX_ERR_NONE;
}

mfxStatus ReadBitStreamData(mfxBitstream *pBS, FILE* fSource)
{
    memmove(pBS->Data, pBS->Data + pBS->DataOffset, pBS->DataLength);
    pBS->DataOffset = 0;

    mfxU32 nBytesRead = (mfxU32)fread(pBS->Data + pBS->DataLength, 1, pBS->MaxLength - pBS->DataLength, fSource);
    if (0 == nBytesRead)
        return MFX_ERR_MORE_DATA;

    pBS->DataLength += nBytesRead;    

    return MFX_ERR_NONE;
}

mfxStatus WriteSection(mfxU8* plane, mfxU16 factor, mfxU16 chunksize, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j, FILE* fSink)
{
    if(chunksize != fwrite( plane + (pInfo->CropY * pData->Pitch / factor + pInfo->CropX)+ i * pData->Pitch + j,
                            1,
                            chunksize,
                            fSink))
        return MFX_ERR_UNDEFINED_BEHAVIOR;
    return MFX_ERR_NONE;
}

mfxU16 GetUVPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j, bool UVALUE)
{
	mfxU16 UV = UVALUE ? 0 : 1;
	mfxU16 factor = 1; // may be worng
	//mfxU16 chunksize= 1;

    return *(plane + (pInfo->CropY * pData->Pitch / factor + pInfo->CropX)+ i * pData->Pitch + j + UV);
                           
}

mfxU16 GetYPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j)
{
	mfxU16 factor = 2; // might be wrong
	//mfxU16 chunksize= 1;

    return *(plane + (pInfo->CropY * pData->Pitch / factor + pInfo->CropX)+ i * pData->Pitch + j);
                           
}

mfxU16 GetSumYPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j)
{
	return GetYPixelValue(plane, pInfo, pData, i, j) + GetYPixelValue(plane, pInfo, pData, i+1, j) +
		GetYPixelValue(plane, pInfo, pData, i, j+1) + GetYPixelValue(plane, pInfo, pData, i+1, j+1);
}



mfxStatus WriteRawFrame(mfxFrameSurface1 *pSurface, FILE* fSink, FILE* fdebug)
{
    mfxFrameInfo *pInfo = &pSurface->Info;
    mfxFrameData *pData = &pSurface->Data;
    mfxU32 i, j, h, w;
    mfxStatus sts = MFX_ERR_NONE;

	mfxU32 Green = 0x00;
	mfxU32 None = -127;

    for (i = 0; i < pInfo->CropH; i++)
        sts = WriteSection(pData->Y, 1, pInfo->CropW, pInfo, pData, i, 0, fSink);

    h = pInfo->CropH / 2;
    w = pInfo->CropW;

	static double Y1Stdev[8*60];
	static double Y2Stdev[8*60];
	static double Y3Stdev[8*60];
	static double Y4Stdev[8*60];
	static double UStdev[8*60];
	static double VStdev[8*60];

	for (i = 0; i < h; i++)
        for (j = 0; j < w; j += 2){
				if( i > h/10 - 2 && i < h/10 + 2 && j > w/4 + 15 && j < w/4 + 20 ) 	{
					int U = GetUVPixelValue( pData->UV, pInfo, pData, i, j, true);
					int V = GetUVPixelValue( pData->UV, pInfo, pData, i, j, false);
					int Y1 = GetYPixelValue( pData->Y, pInfo, pData, 2*i, j );
					int Y2 = GetYPixelValue( pData->Y, pInfo, pData, 2*i, j+1 );
					int Y3 = GetYPixelValue( pData->Y, pInfo, pData, 2*i+1, j );
					int Y4 = GetYPixelValue( pData->Y, pInfo, pData, 2*i+1, j+1 );

					if(fdebug)
						fprintf(fdebug, "[%d,%d]%d:%d:%d:%d::%d::%d\n", i,j,Y1,Y2,Y3,Y4,U,V);	
					
					//std::cout <<"[" << i << "," << j << "]" << ":" << Y1 << ":" << Y2 << ":" << Y3 << ":" << Y4 << "::" << U << "::" << V << std::endl;
					fwrite(&Green, 1, 1, fSink);
				}
			else{
				sts = WriteSection(pData->UV, 2, 1, pInfo, pData, i, j, fSink);
			}
		}
    
	for (i = 0; i < h; i++)
        for (j = 1; j < w; j += 2)
			if( i > h/10 - 2 && i < h/10 + 2 && j > w/4 + 15 && j < w/4 + 20 )
				fwrite(&Green, 1, 1, fSink);
			else
				sts = WriteSection(pData->UV, 2, 1, pInfo, pData, i, j, fSink);

    return sts;
}

