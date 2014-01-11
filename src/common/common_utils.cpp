//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#include "common_utils.h"
#include <iostream>
#include <algorithm>
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

	mfxU16 w4 = w*4;
    for(mfxU16 i = 0; i < h; i++) 
    {
        nBytesRead = (mfxU32)fread(pSurface->Data.B + i * pSurface->Data.Pitch, 1, w4, fSource);
        if (w4 != nBytesRead)
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

// top, left, width and depth are coords of UV pixels
FrameSection GetFrameSection(
	mfxFrameSurface1 *pSurface,
	const Region& r)
{
	mfxFrameInfo *pInfo = &pSurface->Info;
    mfxFrameData *pData = &pSurface->Data;
    
	FrameSection ret; //( (height+1) * (width+1) );

	for(mfxU32 ix = r.topRow() ; ix < r.bottomRow(); ++ix)
		for(mfxU32 iy = r.leftCol(); iy < r.rightCol(); ++iy)
			// ret[...] = 
			ret.push_back(UVPixel(pInfo, pData,ix,iy));

	return ret;
	
}


// for a set of frames, for each pixel in the frame calc the stdev
// of each element (YUV values) over the set of the frames
std::vector<PixelStDev> CalcUVPixelStdev( const std::vector< FrameSection >& frames )
{
	// we have 6 * frames.size * frames.begin->size numbers
	// we assume all frames are identical but that sould be enforced later

	mfxU32 fSize = frames.begin()->size();
	std::vector< PixelStDev > stdev(fSize);

	// we should flatten the data and do
	//for( mfxU32 xx =0; xx != N; ++xx )

	// for now
	for( std::vector< FrameSection >::const_iterator it = frames.begin();
		it != frames.end();
		++it)
	{
		// now iterate over the pixels in the frame
		mfxU32  f = 0; 
		for( FrameSection::const_iterator pIt = it->begin();
			 pIt != it->end();
			 ++pIt, ++f)
		{
			stdev[f] += *pIt; 
		}

	}

	return stdev;

}

mfxU16 GetSumYPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j)
{
	return GetYPixelValue(plane, pInfo, pData, i, j) + GetYPixelValue(plane, pInfo, pData, i+1, j) +
		GetYPixelValue(plane, pInfo, pData, i, j+1) + GetYPixelValue(plane, pInfo, pData, i+1, j+1);
}

struct inside
{
	mfxU32 _i, _j;
	inside(mfxU32 i,mfxU32 j) :_i(i), _j(j) {}

	bool operator()( const Regions::value_type& v )
	{
		return v.inside(_i,_j);
	}
};

std::pair<bool, std::string> insideAny( const Regions& rs, mfxU32 i,mfxU32 j )
{
	std::pair<bool, std::string> ret = std::make_pair( false, std::string("") );
	Regions::const_iterator it = rs.begin();
	while(!ret.first && it != rs.end() )
	{
		if( it->inside(i,j) )
			ret = std::make_pair( true, it->m_name );
		++it;
	}
	return ret;
}

bool borderUAny( const Regions& rs, mfxU32 i,mfxU32 j )
{
	bool ret = false;
	Regions::const_iterator it = rs.begin();
	while(!ret && it != rs.end() )
		ret = (it++)->onBorderU(i,j);

	return ret;
}

bool borderVAny( const Regions& rs, mfxU32 i,mfxU32 j )
{
	bool ret = false;
	Regions::const_iterator it = rs.begin();
	while(!ret && it != rs.end() )
		ret = (it++)->onBorderV(i,j);

	return ret;
}

mfxStatus WriteRawFrame(
	mfxFrameSurface1 *pSurface,
	FILE* fSink,
	FILE* fdebug,
	const Regions& regionsOfInterest)
{
	const Regions& roi = regionsOfInterest;
    mfxFrameInfo *pInfo = &pSurface->Info;
    mfxFrameData *pData = &pSurface->Data;
    mfxU32 i, j, h, w;
    mfxStatus sts = MFX_ERR_NONE;

	mfxU32 Green = 0x00;
	
    for (i = 0; i < pInfo->CropH; i++)
        sts = WriteSection(pData->Y, 1, pInfo->CropW, pInfo, pData, i, 0, fSink);

    h = pInfo->CropH / 2;
    w = pInfo->CropW;

	for (i = 0; i < h; i++)
        for (j = 0; j < w; j += 2){
				std::pair<bool, std::string> inside = insideAny(roi,i,j);
				if( inside.first )
				{
					UVPixel uv( pInfo, pData, i, j );
					fprintf(fdebug, inside.second.c_str() );
					uv.fprint(fdebug);	
				}
			
				if( borderUAny(roi,i,j) )
					fwrite(&Green, 1, 1, fSink);
				else
					sts = WriteSection(pData->UV, 2, 1, pInfo, pData, i, j, fSink);
			
		}
    
	for (i = 0; i < h; i++)
        for (j = 1; j < w; j += 2){
			if( borderVAny(roi,i,j)) 
				fwrite(&Green, 1, 1, fSink);
			else
				sts = WriteSection(pData->UV, 2, 1, pInfo, pData, i, j, fSink);
		}
    return sts;
}

void writeStatsDebug(FILE* fdebug, const std::string& name, const Stats& stats)
{
	fprintf(fdebug, name.c_str());
	int pCount=0;
	for( Stats::const_iterator sit = stats.begin();
		sit != stats.end();
		++sit, ++pCount)
		fprintf(fdebug, "[%d] Ave=%f:%f:%f:%f::%f:%f, StDev=%f:%f:%f:%f::%f:%f\n", 
					pCount,
					sit->Ave[2], sit->Ave[3], sit->Ave[4], sit->Ave[5], sit->Ave[0], sit->Ave[1],
					sit->StDev[2], sit->StDev[3], sit->StDev[4], sit->StDev[5], sit->StDev[0], sit->StDev[1]
				);
}

#define ASSERT(X) if(!X) throw "Assert failed" ;

/*double compare( const Stats& stats, const FrameSection& pixels )
{
	ASSERT(stats.size == pixels.size())

	FrameSection::const_iterator pit = pixels.begin();	
	Stats::const_iterator sit = stats.begin();

	for(; pit != pixels.end() && sit != stats.end();
		++sit, ++pit)
	{
		double ud = sit->AU - pit->U;
	}
	
}*/

const double R6::epsilon = 1e-10;

