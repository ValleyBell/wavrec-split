// Copyright 2021, Valley Bell
// SPDX-License-Identifier: GPL-2.0-or-later
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <string.h>	// for memcpy()
#include <math.h>
#include <vector>
#include <string>

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "func.hpp"

#define INLINE	static inline


static std::vector<UINT8> GenerateWavHeader(const MultiWaveFile& baseFmt, UINT8 forceBits = 0);
INLINE INT16 ReadLE16s(const UINT8* data);
INLINE INT32 ReadLE24s(const UINT8* data);
INLINE void WriteLE16s(UINT8* data, INT16 value);
INLINE void WriteLE24s(UINT8* data, INT32 value);
INLINE double DB2Linear(double db);


static std::vector<UINT8> GenerateWavHeader(const MultiWaveFile& baseFmt, UINT8 forceBits)
{
	std::vector<UINT8> waveHdr;
	WAVEFORMAT wFmt;
	UINT32 chnkLen;
	UINT32 basePos;
	
	// main header + format chunk header + format data + data chunk header
	waveHdr.resize(0x0C + 0x08 + sizeof(WAVEFORMAT) + 0x08);
	
	wFmt.wFormatTag = baseFmt.GetCompression();
	wFmt.nChannels = baseFmt.GetChannels();
	wFmt.wBitsPerSample = (forceBits == 0) ? baseFmt.GetBitDepth() : forceBits;
	wFmt.nSamplesPerSec = baseFmt.GetSampleRate();
	wFmt.nBlockAlign = wFmt.nChannels * wFmt.wBitsPerSample / 8;
	wFmt.nAvgBytesPerSec = wFmt.nSamplesPerSec * wFmt.nBlockAlign;
	
	memcpy(&waveHdr[0x00], "RIFF", 0x04);
	chnkLen = 0;
	memcpy(&waveHdr[0x04], &chnkLen, 0x04);
	memcpy(&waveHdr[0x08], "WAVE", 0x04);
	memcpy(&waveHdr[0x0C], "fmt ", 0x04);
	chnkLen = sizeof(WAVEFORMAT);
	memcpy(&waveHdr[0x10], &chnkLen, 0x04);
	memcpy(&waveHdr[0x14], &wFmt, chnkLen);
	basePos = 0x14 + chnkLen;
	memcpy(&waveHdr[basePos + 0x00], "data", 0x04);
	chnkLen = 0;
	memcpy(&waveHdr[basePos + 0x04], &chnkLen, 0x04);
	
	return waveHdr;
}

UINT8 DoWaveTrim(MultiWaveFile& mwf, const TrimInfo& trim, const TrimOpts& opts)
{
	std::vector<UINT8> waveHdr;
	std::vector<UINT8> smplBuf;
	std::vector<double> chnGain;
	UINT32 smplSizeS = mwf.GetSampleSize();
	UINT32 smplSizeD = mwf.GetSampleSize();
	size_t smplBufSmpls;
	UINT16 chnBits = mwf.GetBitDepth();
	UINT16 chnCnt = mwf.GetChannels();
	UINT16 curChn;
	UINT64 smplCnt;
	FILE* hFile;
	size_t writeSmpls;
	size_t overflowCnt;
	
	if (chnBits == 24 && opts.force16bit)
	{
		chnBits += 16 * 100;
		smplSizeD = smplSizeD * 2 / 3;
	}
	
	chnGain.resize(chnCnt);
	if (!opts.applyGain)
	{
		std::fill(chnGain.begin(), chnGain.end(), 1.0);
	}
	else
	{
		for (curChn = 0; curChn < chnCnt; curChn ++)
		{
			double gain = trim.gain;
			if (curChn < trim.chnGain.size())
				gain += trim.chnGain[curChn];
			chnGain[curChn] = DB2Linear(gain);
		}
	}
	
	smplBufSmpls = mwf.GetSampleRate() * 10;	// buffer for 10 seconds of data
	smplBuf.resize(smplBufSmpls * smplSizeS);
	
	hFile = fopen(trim.fileName.c_str(), "wb");
	if (hFile == NULL)
		return 0xFF;	// open failed
	
	waveHdr = GenerateWavHeader(mwf, chnBits / 100);
	writeSmpls = fwrite(&waveHdr[0], 0x01, waveHdr.size(), hFile);
	if (writeSmpls < waveHdr.size())
	{
		fclose(hFile);
		return 0xFE;	// failed to write header (no space left?)
	}
	
	mwf.SetSampleReadOffset(trim.smplStart);
	smplCnt = trim.smplEnd - trim.smplStart;
	writeSmpls = 0;
	overflowCnt = 0;
	while(smplCnt > 0)
	{
		size_t curSmpChn;
		size_t readSmpls = (smplBufSmpls < smplCnt) ? smplBufSmpls : (size_t)smplCnt;
		readSmpls = mwf.ReadSamples(readSmpls * smplSizeS, smplBuf.data());
		if (! readSmpls)
			break;
		switch(chnBits)
		{
		case 16:
			for (curSmpChn = 0; curSmpChn < readSmpls * chnCnt; curSmpChn ++)
			{
				INT32 smplVal = ReadLE16s(&smplBuf[curSmpChn * 2]);
				smplVal = (INT32)(smplVal * chnGain[curSmpChn % chnCnt]);
				if (smplVal < -0x8000)
				{
					smplVal = -0x8000;
					overflowCnt ++;
				}
				else if (smplVal > +0x7FFF)
				{
					smplVal = +0x7FFF;
					overflowCnt ++;
				}
				WriteLE16s(&smplBuf[curSmpChn * 2], (INT16)smplVal);
			}
			break;
		case 24:
			for (curSmpChn = 0; curSmpChn < readSmpls * chnCnt; curSmpChn ++)
			{
				INT32 smplVal = ReadLE24s(&smplBuf[curSmpChn * 3]);
				if (chnGain[curSmpChn % chnCnt] != 1.0)
					smplVal = (INT32)(smplVal * chnGain[curSmpChn % chnCnt]);
				if (smplVal < -0x800000)
				{
					smplVal = -0x800000;
					overflowCnt ++;
				}
				else if (smplVal > +0x7FFFFF)
				{
					smplVal = +0x7FFFFF;
					overflowCnt ++;
				}
				WriteLE24s(&smplBuf[curSmpChn * 3], smplVal);
			}
			break;
		case 1624:	// 24 -> 16 bit conversion
			for (curSmpChn = 0; curSmpChn < readSmpls * chnCnt; curSmpChn ++)
			{
				INT32 smplVal = ReadLE24s(&smplBuf[curSmpChn * 3]);
				smplVal = (INT32)(smplVal * chnGain[curSmpChn % chnCnt]);
				smplVal = (smplVal + 0x80) >> 8;	// round with "half up" method, results in even distribution
				if (smplVal < -0x8000)
				{
					smplVal = -0x8000;
					overflowCnt ++;
				}
				else if (smplVal > +0x7FFF)
				{
					smplVal = +0x7FFF;
					overflowCnt ++;
				}
				WriteLE16s(&smplBuf[curSmpChn * 2], (INT16)smplVal);
			}
			break;
		}
		writeSmpls += fwrite(&smplBuf[0], smplSizeD, readSmpls, hFile);
		smplCnt -= readSmpls;
	}
	if (overflowCnt > 0)
		printf("Warning! Clipped %u samples due to overflow\n", overflowCnt);
	
	{
		UINT32 chnkLen = (UINT32)(writeSmpls * smplSizeD);
		fseek(hFile, (long)(waveHdr.size() - 0x04), SEEK_SET);
		fwrite(&chnkLen, 0x04, 1, hFile);	// write 'data' length
		
		chnkLen += (UINT32)(waveHdr.size() - 0x08);
		fseek(hFile, 0x04, SEEK_SET);
		fwrite(&chnkLen, 0x04, 1, hFile);	// write 'RIFF' length
	}
	
	fclose(hFile);
	return 0x00;
}


INLINE INT16 ReadLE16s(const UINT8* data)
{
	return ((INT8)data[0x01] << 8) | (data[0x00] << 0);
}

INLINE INT32 ReadLE24s(const UINT8* data)
{
	return ((INT8)data[0x02] << 16) | (data[0x01] <<  8) | (data[0x00] <<  0);
}

INLINE void WriteLE16s(UINT8* data, INT16 value)
{
	data[0x00] = (value >> 0) & 0xFF;
	data[0x01] = (value >> 8) & 0xFF;
	return;
}

INLINE void WriteLE24s(UINT8* data, INT32 value)
{
	data[0x00] = (value >>  0) & 0xFF;
	data[0x01] = (value >>  8) & 0xFF;
	data[0x02] = (value >> 16) & 0xFF;
	return;
}

INLINE double DB2Linear(double db)
{
	return pow(2.0, db / 6.0);
}
