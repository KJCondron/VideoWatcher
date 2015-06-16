//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2013 Intel Corporation. All Rights Reserved.
//

#pragma once 

#include <stdio.h>
#include <windows.h>
#include <vector>
#include <boost/array.hpp>

#include "mfxvideo++.h"

#define MSDK_PRINT_RET_MSG(ERR)			{printf("\nReturn on error: error code %d,\t%s\t%d\n\n", ERR, __FILE__, __LINE__);}
#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_POINTER(P, ERR)      {if (!(P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_IGNORE_MFX_STS(P, X)       {if ((X) == (P)) {P = MFX_ERR_NONE;}}
#define MSDK_BREAK_ON_ERROR(P)          {if (MFX_ERR_NONE != (P)) break;}
#define MSDK_SAFE_DELETE_ARRAY(P)       {if (P) {delete[] P; P = NULL;}}
#define MSDK_ALIGN32(X)					(((mfxU32)((X)+31)) & (~ (mfxU32)31))
#define MSDK_ALIGN16(value)             (((value + 15) >> 4) << 4)
#define MSDK_SAFE_RELEASE(X)            {if (X) { X->Release(); X = NULL; }}
#define MSDK_MAX(A, B)			        (((A) > (B)) ? (A) : (B))

#include <string>
#include <sstream>
#include <iomanip>

// =================================================================
// Utility functions, not directly tied to Media SDK functionality
//

// LoadRawFrame: Reads raw frame from YUV file (YV12) into NV12 surface
// - YV12 is a more common format for for YUV files than NV12 (therefore the conversion during read and write)
// - For the simulation case, the surface is filled with default image data
// LoadRawRGBFrame: Reads raw RGB32 frames from file into RGB32 surface
// - For the simulation case, the surface is filled with default image data
#ifdef ENABLE_INPUT
    mfxStatus LoadRawFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = false);
    mfxStatus LoadRawRGBFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = false);
#else
    mfxStatus LoadRawFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = true);
    mfxStatus LoadRawRGBFrame(mfxFrameSurface1* pSurface, FILE* fSource, bool bSim = true);
#endif

	struct Region
	{
		mfxU32 m_top, m_left, m_height, m_width;
		std::string m_name;
		FILE* m_file;

		Region( 
			std::string name,
			mfxU32 top,
			mfxU32 left,
			mfxU32 height,
			mfxU32 width,
			FILE* debugFile) :
		m_name(name), m_top(top), m_left(left), m_height(height), m_width(width),
			m_file(debugFile)
		{}

		// i is UV coord j/2 is UV coord (see docs)
		bool inside( mfxU32 i, mfxU32 j ) const
		{
			return ( i > topRow()  && i < bottomRow() &&
					 j > leftCol() && j < rightCol() );
				
		}

		bool onBorderU( mfxU32 i, mfxU32 j ) const
		{
			return ( i == topRow() || i == bottomRow() ) &&
				   ( j >= leftCol() && j <= rightCol() )  ||  // top & bottom rows
			       ( i >= topRow() && i <= bottomRow() )  && 
				   ( j == leftCol() || j == rightCol() ); // left & right cols				
		}

		bool onBorderV( mfxU32 i, mfxU32 j ) const
		{
		return ( i == topRow() || i == bottomRow() ) &&
				  ( j >= leftCol() && j <= rightCol() )  ||  // top & bottom rows
			      ( i >= topRow() && i <= bottomRow() )  && 
				  ( j == leftCol()+1 || j == rightCol()+1 ); // left & right cols				
		}

		mfxU32 topRow() const { return m_top; }
		mfxU32 bottomRow() const { return m_top+m_height; }
		mfxU32 leftCol() const { return m_left*2; }
		mfxU32 rightCol() const { return m_left*2+m_width*2; }

		FILE* file() const { return m_file; }
		
		 
	};

	typedef std::vector<Region> Regions;

// Write raw YUV (NV12) surface to YUV (YV12) file
mfxStatus WriteRawFrame(
	mfxFrameSurface1 *pSurface,
	FILE* fSink,
	const Regions& regionsOfInterest = Regions());

// Write bit stream data for frame to file
mfxStatus WriteBitStreamFrame(mfxBitstream *pMfxBitstream, FILE* fSink);
// Read bit stream data from file. Stream is read as large chunks (= many frames)
mfxStatus ReadBitStreamData(mfxBitstream *pBS, FILE* fSource);


// KJC added


mfxU16 GetUVPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j, bool UVALUE);
mfxU16 GetYPixelValue(mfxU8* plane, mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j);

struct UVPixel
{
	UVPixel(mfxFrameInfo *pInfo, mfxFrameData *pData, mfxU32 i, mfxU32 j) :
		m_i(i), m_j(j)
	{
		U  = GetUVPixelValue( pData->UV, pInfo, pData, i, j, true);
		V  = GetUVPixelValue( pData->UV, pInfo, pData, i, j, false);
		Y1 = GetYPixelValue( pData->Y, pInfo, pData, 2*i, j );
		Y2 = GetYPixelValue( pData->Y, pInfo, pData, 2*i, j+1 );
		Y3 = GetYPixelValue( pData->Y, pInfo, pData, 2*i+1, j );
		Y4 = GetYPixelValue( pData->Y, pInfo, pData, 2*i+1, j+1 );
	}

	void fprint( FILE* file ) const
	{
		if(file)
		fprintf(file, "[%d,%d]%d:%d:%d:%d::%d::%d\n", 
		               m_i, m_j/2,Y1,Y2,Y3,Y4,U,V);	
						
	}

	std::string getAttr() const {
		char s = ',';
		std::stringstream ss;
		ss.fill(' ');
		ss << std::setw(3) << U << s <<
			  std::setw(3) << V << s <<
			  std::setw(3) << Y1 << s <<
			  std::setw(3) << Y2 << s << 
			  std::setw(3) << Y3 << s <<
			  std::setw(3) << Y4;
		return ss.str(); 
	}

	mfxU16 U, V, Y1, Y2, Y3, Y4;
	mfxU32 m_i,m_j;
																   
};

typedef std::vector< UVPixel > FrameSection;

FrameSection GetFrameSection(
	mfxFrameSurface1 *pSurface,
	const Region& r);

struct R6 : public boost::array<double, 6>
{
	R6() 
	{ fill(0.0); }

	static const double epsilon;

	bool operator==( const R6&  rhs)
	{
		bool ret = true;
		for(size_t ii = 0; ret && ii < 6; ++ii )
			ret == ret && fabs( (*this)[ii] - rhs[ii] ) < epsilon;
		
		return ret;
	}

	R6 operator-( const R6&  rhs)
	{
		R6 ret(*this);
		for(size_t ii = 0; ii < 6; ++ii )
			ret[ii] -= rhs[ii];

		return ret;
	}

	R6 operator*( const R6&  rhs)
	{
		R6 ret(*this);
		for(size_t ii = 0; ii < 6; ++ii )
			ret[ii] *= rhs[ii];

		return ret;
	}

	R6 operator*( double d )
	{
		R6 ret(*this);
		for(size_t ii = 0; ii < 6; ++ii )
			ret[ii] *= d;

		return ret;
	}

	R6 operator/( double d)
	{
		return operator*(1/d);
	}

	void operator+=(const UVPixel& pix)
	{
		R6& me = *this;
		me[0] += pix.U;
		me[1] += pix.V;
		me[2] += pix.Y1;
		me[3] += pix.Y2;
		me[4] += pix.Y3;
		me[5] += pix.Y4;
	}

	void addSq(const UVPixel& pix)
	{
		R6& me = *this;
		me[0] += pix.U * pix.U;
		me[1] += pix.V * pix.V;
		me[2] += pix.Y1 * pix.Y1;
		me[3] += pix.Y2 * pix.Y2;
		me[4] += pix.Y3 * pix.Y3;
		me[5] += pix.Y4 * pix.Y4;
	}
};

struct PixelStDev
{
	PixelStDev() :
		count(0)
	{}

	PixelStDev(size_t N, const R6& ave, const R6& stdev) :
		count(N), Ave(ave), StDev(stdev)
	{
		S = Ave * count;
	}


	R6 S, S2, Ave, StDev;
	size_t count;

	void operator +=( const UVPixel& pix )
	{
		++count;
		S+=pix; 
		S2.addSq(pix);
		StDev = stDev(count, S2, S); 
		Ave = S / count;
	}

private:
	static R6 stDev( size_t N, R6 sumSq, R6 sum )
	{
		R6 ret;
		for(size_t ii = 0; ii<6; ++ii)
			ret[ii] = sqrt(N * sumSq[ii] - sum[ii]*sum[ii]) / N;

		return ret;
	}

	static R6 calcSumSq( size_t N, R6 stdev, R6 sum )
	{
		R6 ret;
		
		for(size_t ii = 0; ii<6; ++ii)
			ret[ii] = (N * N * stdev[ii] * stdev[ii] + sum[ii]*sum[ii]) / N;

		return ret;
	}

};

std::vector<PixelStDev> CalcUVPixelStdev( const std::vector< FrameSection >& frames );

typedef std::vector<PixelStDev> Stats;
				
void writeStatsDebug(FILE* fdebug, const std::string& name, const Stats& stats);

double compare( const Stats& stats, const FrameSection& pixels); 
