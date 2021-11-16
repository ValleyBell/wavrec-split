#define _USE_MATH_DEFINES
#include <stdio.h>
#include <math.h>
#include <vector>
#include <string>

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "func.hpp"

#define INLINE	static inline

#ifndef M_LN2
#define M_LN2	0.693147180559945309417
#endif

struct SplitListItem
{
	UINT64 smplStart;
	UINT64 smplEnd;
	double gain;
	std::string fileName;
};


INLINE INT32 ReadLE24s(const UINT8* data);
INLINE INT32 MaxVal_SampleBits(UINT8 bits);
INLINE double Linear2DB(double scale);
INLINE double DB2Linear(double db);
INLINE INT32 OptAmplitude2Sample(double optVal, UINT32 maxSmplVal);
INLINE UINT64 RoundDownToUnit(UINT64 val, UINT64 unit);
static INT32 GetMaxSample24(const UINT8* buffer, UINT16 chnCnt);
static std::string GetTimeStrHMS(UINT32 smplRate, UINT64 smplPos);
static std::string GetTimeStrMS(UINT32 smplRate, UINT64 smplPos);


static void FinetuneTrimPoint(MultiWaveFile& mwf, SplitListItem& sli, INT32 silenceVal)
{
	std::vector<UINT8> smplBuf;
	UINT32 smplSize = mwf.GetSampleSize();
	UINT32 smplRate = mwf.GetSampleRate();
	UINT32 smplCnt;
	size_t readSmpls;
	UINT16 chnCnt = mwf.GetChannels();
	UINT64 smplReadOfs;
	INT32 maxVal;
	const UINT8* src;
	UINT32 curSmpl;
	
	smplBuf.resize(smplRate * 4 * smplSize);
	
	{
		INT8 initSign;
		INT8 curSign;
		
		// algorithm for Start Point:
		//	1. start at sli.smplStart, read up to 1 second back
		//	2. search back until reaching a level of (silenceVal / 4), with *opposite* sign of start level
		//	3. search forward until sign changes
		smplCnt = smplRate * 1;
		smplReadOfs = (sli.smplStart >= smplCnt) ? (sli.smplStart - smplCnt) : 0;
		mwf.SetSampleReadOffset(smplReadOfs);
		readSmpls = mwf.ReadSamples((smplCnt + 1) * smplSize, smplBuf.data());
		if (! readSmpls)
		{
			printf("Error reading samples from offset %llu, count %u!\n", smplReadOfs, smplCnt);
			return;
		}
		
		curSmpl = (UINT32)readSmpls - 1;
		src = smplBuf.data() + (curSmpl * smplSize);
		maxVal = GetMaxSample24(src, chnCnt);
		initSign = (maxVal < 0) ? -1 : 0;
		while(curSmpl > 0)
		{
			curSmpl --;	src -= smplSize;
			maxVal = GetMaxSample24(src, chnCnt);
			curSign = (maxVal < 0) ? -1 : 0;
			
			if (curSign != initSign && abs(maxVal) >= (silenceVal / 4))
				break;	// found [opposite sign + level >= (silenceVal / 4)]
		}
		initSign = curSign;
		
		for (; curSmpl < readSmpls - 1; curSmpl ++, src += smplSize)
		{
			maxVal = GetMaxSample24(src, chnCnt);
			curSign = (maxVal < 0) ? -1 : 0;
			
			if (curSign != initSign)
			{
				//printf("Trim Adjustment - Start: %llu -> %llu\n", sli.smplStart, smplReadOfs + curSmpl);
				sli.smplStart = smplReadOfs + curSmpl;
				break;	// found [opposite sign + level >= (silenceVal / 4)]
			}
		}
	}
	
	{
		// algorithm for End Point:
		//	1. start at sli.smplStart, read up to 1 second forward
		//	2. make average of all samples for each 1/10 second block
		//	3. stop when "block average" gets larger
		UINT16 curChn;
		UINT32 curBlk;
		UINT32 blkBaseSmpl;
		UINT32 blkSmpls;
		INT32 blkSmplVal;
		INT32 lastBlkSmplVal;
		
		smplReadOfs = sli.smplStart + RoundDownToUnit(sli.smplEnd - sli.smplStart, smplRate / 10);
		smplReadOfs -= smplRate / 10;
		//smplReadOfs = sli.smplEnd - smplRate / 10;
		mwf.SetSampleReadOffset(smplReadOfs);
		readSmpls = mwf.ReadSamples(smplBuf.size(), smplBuf.data());
		if (! readSmpls)
		{
			printf("Error reading samples from offset %llu, count %u!\n", smplReadOfs, smplCnt);
			return;
		}
		
		blkBaseSmpl = 0;
		lastBlkSmplVal = 0x7FFFFFFF;
		for (curBlk = 0; blkBaseSmpl < readSmpls; curBlk ++)
		{
			blkSmpls = smplRate / 10;
			if (blkBaseSmpl + blkSmpls > readSmpls)
				blkSmpls = (UINT32)(readSmpls - blkBaseSmpl);
			
			src = smplBuf.data() + (blkBaseSmpl * smplSize);
			INT64 blkSmplAcc = 0;
			for (curSmpl = 0; curSmpl < blkSmpls; curSmpl ++, src += smplSize)
			{
				for (curChn = 0; curChn < chnCnt; curChn ++)
					blkSmplAcc += abs(ReadLE24s(&src[curChn * 3]));
			}
			blkSmplVal = (INT32)(blkSmplAcc / (blkSmpls * chnCnt));
			if (blkSmplVal > lastBlkSmplVal)
				break;
			else
				lastBlkSmplVal = blkSmplVal;
			blkBaseSmpl += blkSmpls;
		}
		if (curBlk > 0)
		{
			//printf("Trim Adjustment - End: %llu -> %llu\n", sli.smplEnd, smplReadOfs + blkBaseSmpl);
			sli.smplEnd = smplReadOfs + blkBaseSmpl;
		}
	}
	
	return;
}

int DoSplitDetection(MultiWaveFile& mwf, const std::vector<std::string>& fileNameList, const DetectOpts& opts)
{
	const INT32 smplValRange = MaxVal_SampleBits(mwf.GetBitDepth());
	const INT32 splitSValSilence = OptAmplitude2Sample(opts.ampSplit, smplValRange);
	const INT32 splitSValFine = OptAmplitude2Sample(opts.ampFinetune, smplValRange);
	const UINT32 splitSmplCount = (UINT32)(opts.tSplit * mwf.GetSampleRate() + 0.5);
	std::vector<UINT8> smplBuf;
	UINT32 smplSize = mwf.GetSampleSize();
	UINT32 smplRate = mwf.GetSampleRate();
	UINT16 chnCnt = mwf.GetChannels();
	size_t readSmpls;
	UINT64 smplPos;
	UINT32 silenceSmplCnt;
	UINT32 songID;
	UINT64 songSmplStart;
	UINT64 songSmplEnd;
	INT32 maxSmplVal;
	
	std::vector<SplitListItem> splitList;
	
	// algorithm:
	//	1. sample >= 512 starts a song
	//	2. song stops after 5+ seconds of (all samples < 512)
	//	3. go to 1
	smplBuf.resize(smplRate * 10 * smplSize);	// buffer of 10 seconds
	
	// actual song search
	fprintf(stderr, "Determining split points ...\n");
	mwf.SetSampleReadOffset(0);
	splitList.clear();
	songSmplStart = songSmplEnd = 0;
	silenceSmplCnt = smplRate * 4 * chnCnt;
	maxSmplVal = 0;
	songID = (UINT32)-1;	// make first ID 0 even with pre-increment
	readSmpls = 0;
	for (smplPos = mwf.GetSampleReadOffset(); smplPos < mwf.GetTotalSamples(); smplPos += readSmpls)
	{
		readSmpls = mwf.ReadSamples(smplBuf.size(), smplBuf.data());
		if (! readSmpls)
			break;
		
		const UINT8* src = smplBuf.data();
		UINT32 curSmpl;
		UINT16 curChn;
		for (curSmpl = 0; curSmpl < readSmpls; curSmpl ++, src += smplSize)
		{
			for (curChn = 0; curChn < chnCnt; curChn ++)
			{
				INT32 smplVal = abs(ReadLE24s(&src[curChn * 3]));
				if (smplVal < splitSValSilence)
				{
					silenceSmplCnt ++;
					continue;
				}
				
				if (silenceSmplCnt >= splitSmplCount * chnCnt)
				{
					if (songSmplStart)
					{
						songSmplEnd = smplPos + curSmpl - silenceSmplCnt / chnCnt;
						if (songSmplEnd - songSmplStart < 10)
						{
							songID --;
							printf("Outlier at %s (%u samples)\n", GetTimeStrHMS(smplRate, songSmplStart).c_str(),
								(UINT32)(songSmplEnd - songSmplStart));
						}
						else
						{
							SplitListItem sli;
							sli.smplStart = songSmplStart;
							sli.smplEnd = songSmplEnd;
							sli.gain = maxSmplVal / (double)smplValRange;
							sli.fileName = (songID < fileNameList.size()) ? fileNameList[songID] : "";
							printf("Song %u: %s .. %s len %s  %s\n", songID, GetTimeStrHMS(smplRate, sli.smplStart).c_str(),
								GetTimeStrHMS(smplRate, sli.smplEnd).c_str(),
								GetTimeStrMS(smplRate, sli.smplEnd - sli.smplStart).c_str(), sli.fileName.c_str());
							splitList.push_back(sli);
						}
					}
					songID ++;
					songSmplStart = smplPos + curSmpl;
					maxSmplVal = 0;
				}
				if (maxSmplVal < smplVal)
					maxSmplVal = smplVal;
				silenceSmplCnt = 0;
			}
		}
	}
	if (songSmplStart)
	{
		songSmplEnd = smplPos - silenceSmplCnt / chnCnt + 1;
		
		SplitListItem sli;
		sli.smplStart = songSmplStart;
		sli.smplEnd = songSmplEnd;
		sli.gain = maxSmplVal / (double)smplValRange;
		sli.fileName = (songID < fileNameList.size()) ? fileNameList[songID] : "";
		printf("Song %u: %s .. %s %s\n", songID, GetTimeStrHMS(smplRate, sli.smplStart).c_str(),
			GetTimeStrHMS(smplRate, sli.smplEnd).c_str(), sli.fileName.c_str());
		splitList.push_back(sli);
	}
	
	printf("\n");
	fprintf(stderr, "Finetuning split points and generating trim list ...\n");
	size_t curFile;
	for (curFile = 0; curFile < splitList.size(); curFile ++)
	{
		SplitListItem& sli = splitList[curFile];
		FinetuneTrimPoint(mwf, sli, splitSValFine);
		double gainDB = Linear2DB(sli.gain) * -1;	// invert sign to turn "maximum amplitude" to "gain"
		printf("%.3f %llu %llu %s\n", gainDB, sli.smplStart, sli.smplEnd, sli.fileName.c_str());
	}
	
	return 0;
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

INLINE double DB2Linear(double db)
{
	return pow(2.0, db / 6.0);
}

INLINE INT32 OptAmplitude2Sample(double optVal, UINT32 maxSmplVal)
{
	if (optVal > 0)
		return (INT32)optVal;
	else
		return (INT32)(DB2Linear(optVal) * maxSmplVal);
}

INLINE UINT64 RoundDownToUnit(UINT64 val, UINT64 unit)
{
	return (val / unit) * unit;
}

static INT32 GetMaxSample24(const UINT8* buffer, UINT16 chnCnt)
{
	INT32 maxVal = ReadLE24s(&buffer[0]);
	for (UINT16 curChn = 1; curChn < chnCnt; curChn ++)
	{
		INT32 smplVal = ReadLE24s(&buffer[curChn * 3]);
		if (abs(smplVal) > abs(maxVal))
			maxVal = smplVal;
	}
	
	return maxVal;
}

static std::string GetTimeStrHMS(UINT32 smplRate, UINT64 smplPos)
{
	char timeStr[0x20];
	UINT32 sec_smpls = (UINT32)(smplPos % smplRate);
	UINT32 seconds = (UINT32)(smplPos / smplRate);
	UINT32 mins = seconds / 60;
	UINT32 hours = mins / 60;
	sprintf(timeStr, "%02u:%02u:%02u.%03u", hours, mins % 60, seconds % 60, sec_smpls * 1000 / smplRate);
	return std::string(timeStr);
}

static std::string GetTimeStrMS(UINT32 smplRate, UINT64 smplPos)
{
	char timeStr[0x20];
	UINT32 sec_smpls = (UINT32)(smplPos % smplRate);
	UINT32 seconds = (UINT32)(smplPos / smplRate);
	UINT32 mins = seconds / 60;
	sprintf(timeStr, "%02u:%02u.%03u", mins, seconds % 60, sec_smpls * 1000 / smplRate);
	return std::string(timeStr);
}
