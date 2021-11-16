#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>
#include <vector>
#include <algorithm>	// for std::min()

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "func.hpp"

#define INLINE	static inline

#ifndef M_LN2
#define M_LN2	0.693147180559945309417
#endif

INLINE INT16 ReadLE16s(const UINT8* data);
INLINE INT32 ReadLE24s(const UINT8* data);
INLINE INT32 MaxVal_SampleBits(UINT8 bits);
INLINE double Linear2DB(double scale);

int DoAmplitudeStats(MultiWaveFile& mwf, UINT64 smplStart, UINT64 smplDurat, UINT32 interval)
{
	double smplDivide;
	std::vector<UINT8> smplBuf;
	size_t smplBufSCnt;	// sample buffer: sample count
	UINT32 smplSize;
	UINT32 smplRate;
	size_t readSmpls;
	UINT64 smplEnd;
	UINT64 smplPos;
	UINT16 curChn;
	UINT16 chnCnt;
	std::vector< INT32> smplMaxVal;
	std::vector<UINT64> smplMaxPos;
	std::vector< INT32> smplMinVal;
	std::vector<UINT64> smplMinPos;
	
	smplDivide = (double)MaxVal_SampleBits(mwf.GetBitDepth());
	smplSize = mwf.GetSampleSize();
	smplRate = mwf.GetSampleRate();
	chnCnt = mwf.GetChannels();
	smplMaxVal.resize(chnCnt);
	smplMaxPos.resize(chnCnt);
	smplMinVal.resize(chnCnt);
	smplMinPos.resize(chnCnt);
	// Note: The buffer size also determines the measurement interval.
	smplBufSCnt = interval ? interval : (smplRate * 1);	// fallback: buffer of 1 second
	smplBuf.resize(smplBufSCnt * smplSize);
	
	printf("second");
	for (curChn = 0; curChn < chnCnt; curChn ++)
		printf("\tsmplMin_%u\tsmplMax_%u\tamplitude_%u", 1 + curChn, 1 + curChn, 1 + curChn);
	printf("\n");
	
	mwf.SetSampleReadOffset(smplStart);
	smplEnd = smplStart + smplDurat;
	readSmpls = 0;
	for (smplPos = smplStart; smplPos < mwf.GetTotalSamples(); smplPos += readSmpls)
	{
		if (smplPos >= smplEnd)
			break;
		readSmpls = std::min(smplBuf.size(), (size_t)(smplPos - smplEnd));
		readSmpls = mwf.ReadSamples(readSmpls, smplBuf.data());
		if (! readSmpls)
			break;
		
		const UINT8* src = smplBuf.data();
		size_t curSmpl;
		std::fill(smplMaxVal.begin(), smplMaxVal.end(), 0);	std::fill(smplMaxPos.begin(), smplMaxPos.end(), 0);
		std::fill(smplMinVal.begin(), smplMinVal.end(), 0);	std::fill(smplMinPos.begin(), smplMinPos.end(), 0);
		switch(mwf.GetBitDepth())
		{
		case 16:
			for (curSmpl = 0; curSmpl < readSmpls; curSmpl ++, src += smplSize)
			{
				for (curChn = 0; curChn < chnCnt; curChn ++)
				{
					INT32 smplVal = ReadLE16s(&src[curChn * 2]);
					if (smplVal > smplMaxVal[curChn])
					{
						smplMaxVal[curChn] = smplVal;
						smplMaxPos[curChn] = smplPos + (UINT64)curSmpl;
					}
					if (smplVal < smplMinVal[curChn])
					{
						smplMinVal[curChn] = smplVal;
						smplMinPos[curChn] = smplPos + (UINT64)curSmpl;
					}
				}
			}
			break;
		case 24:
			for (curSmpl = 0; curSmpl < readSmpls; curSmpl ++, src += smplSize)
			{
				for (curChn = 0; curChn < chnCnt; curChn ++)
				{
					INT32 smplVal = ReadLE24s(&src[curChn * 3]);
					if (smplVal > smplMaxVal[curChn])
					{
						smplMaxVal[curChn] = smplVal;
						smplMaxPos[curChn] = smplPos + (UINT64)curSmpl;
					}
					if (smplVal < smplMinVal[curChn])
					{
						smplMinVal[curChn] = smplVal;
						smplMinPos[curChn] = smplPos + (UINT64)curSmpl;
					}
				}
			}
			break;
		}
#if 0
		printf("Second %u:\n", (UINT32)(smplPos / smplRate));
		for (curChn = 0; curChn < chnCnt; curChn ++)
		{
			INT32 smplDiff = smplMaxVal[curChn] - smplMinVal[curChn];
			double dbMin = Linear2DB(abs(smplMinVal[curChn]) / smplDivide);
			double dbMax = Linear2DB(abs(smplMaxVal[curChn]) / smplDivide);
			double dbDiff = Linear2DB(smplDiff / smplDivide / 2);
			printf("    Ch %u: [%d, %d] diff %d = [%.5f db, %.5f db] diff %.5f db\n", curChn,
				smplMinVal[curChn], smplMaxVal[curChn], smplDiff, dbMin, dbMax, dbDiff);
		}
#else
		printf("%.3f", (double)smplPos / smplRate);
		for (curChn = 0; curChn < chnCnt; curChn ++)
		{
			INT32 smplDiff = smplMaxVal[curChn] - smplMinVal[curChn];
			double dbMin = Linear2DB(abs(smplMinVal[curChn]) / smplDivide);
			double dbMax = Linear2DB(abs(smplMaxVal[curChn]) / smplDivide);
			double dbDiff = Linear2DB(smplDiff / smplDivide / 2);
			printf("\t%.8f\t%.8f\t%.8f", dbMin, dbMax, dbDiff);
		}
		printf("\n");
#endif
	}
	
	return 0;
}

INLINE INT16 ReadLE16s(const UINT8* data)
{
	return ((INT8)data[0x01] << 8) | (data[0x00] << 0);
}

INLINE INT32 ReadLE24s(const UINT8* data)
{
	return ((INT8)data[0x02] << 16) | (data[0x01] <<  8) | (data[0x00] <<  0);
}

INLINE INT32 MaxVal_SampleBits(UINT8 bits)
{
	INT32 mask_bm2 = 1 << (bits - 2);
	return mask_bm2 | (mask_bm2 - 1);	// return (1 << (bits-1)) - 1
}

INLINE double Linear2DB(double scale)
{
	return log(scale) * 6.0 / M_LN2;
}
