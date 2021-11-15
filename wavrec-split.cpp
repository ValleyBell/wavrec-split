#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <algorithm>	// for std::transform()
#include <math.h>

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "libs/CLI11.hpp"

#define INLINE	static inline

#ifndef M_LN2
#define M_LN2	0.693147180559945309417
#endif


#ifdef _WIN32
#include <direct.h>	// for _mkdir()
#define MakeDir(x)	_mkdir(x)
#else
#include <sys/stat.h>	// for mkdir()
#define MakeDir(x)	mkdir(x, 0755)
#endif

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


// --- utility functions functions ---
INLINE INT16 ReadLE16s(const UINT8* data);
INLINE void WriteLE16s(UINT8* data, INT16 value);
INLINE INT32 ReadLE24s(const UINT8* data);
INLINE void WriteLE24s(UINT8* data, INT32 value);
static std::vector<std::string> ReadFileIntoStrVector(const std::string& fileName);
static UINT8 TimeStr2Sample(const char* time, UINT32 sampleRate, UINT64* result);
// --- main functions ---
static UINT8 DoMagnitudeStats(MultiWaveFile& mwf, UINT64 smplStart, UINT64 smplDurat, UINT32 interval);


int main(int argc, char* argv[])
{
	CLI::App cliApp{"Wave Splitter"};
	std::string tStart;
	std::string tLen;
	UINT32 tDelta = 0;
	std::vector<std::string> wavFileNames;
	std::string wavFileList;
	std::string splitFileName;
	std::string dstPath;
	bool noGain;
	bool out16Bit;
	CLI::Option* optFile;
	CLI::Option* optList;
	
	cliApp.require_subcommand();
	
	CLI::App* scMag = cliApp.add_subcommand("magstat", "magnitude statistics");
	optFile = scMag->add_option("-f, --file", wavFileNames, "WAV file")->check(CLI::ExistingFile);
	optList = scMag->add_option("-l, --list", wavFileList, "TXT file that lists WAVs")->check(CLI::ExistingFile);
	optFile->excludes(optList);
	optList->excludes(optFile);
	scMag->add_option("-s, --start", tStart, "Start Time in [HH:]MM:ss or sample number (plain integer)");
	scMag->add_option("-t, --length", tLen, "Length in [HH:]MM:ss or number of samples");
	scMag->add_option("-i, --interval", tDelta, "Measurement interval, number of samples");
	
	CLI::App* scDetect = cliApp.add_subcommand("detect", "detect split points");
	optFile = scDetect->add_option("-f, --file", wavFileNames, "WAV file")->check(CLI::ExistingFile);
	optList = scDetect->add_option("-l, --list", wavFileList, "TXT file that lists WAVs")->check(CLI::ExistingFile);
	optFile->excludes(optList);
	optList->excludes(optFile);
	
	CLI::App* scSplit = cliApp.add_subcommand("split", "split into multiple files");
	optFile = scSplit->add_option("-f, --file", wavFileNames, "WAV file")->check(CLI::ExistingFile);
	optList = scSplit->add_option("-l, --list", wavFileList, "TXT file that lists WAVs")->check(CLI::ExistingFile);
	optFile->excludes(optList);
	optList->excludes(optFile);
	scSplit->add_option("-s, --split", splitFileName, "TXT file that lists split points")->check(CLI::ExistingFile);
	scSplit->add_option("-G, --no-gain", noGain, "ignore split list gain");
	scSplit->add_option("-1, --force-16b", out16Bit, "enforce 16-bit output");
	scSplit->add_option("-o, --output-path", dstPath, "output path");
	
	CLI11_PARSE(cliApp, argc, argv);
	
	if (cliApp.got_subcommand(scMag))
	{
		MultiWaveFile mwf;
		UINT8 retVal;
		UINT64 smplStart;
		UINT64 smplDurat;
		
		printf("Magnitude Statistics\n");
		printf("--------------------\n");
		
		retVal = mwf.LoadWaveFiles(wavFileNames);
		if (retVal)
		{
			printf("WAVE Loading failed!\n");
			return 3;
		}
		if (mwf.GetCompression() != WAVE_FORMAT_PCM)
		{
			printf("Unsupported compression type: %u\n", mwf.GetCompression());
			return 4;
		}
		if (! (mwf.GetBitDepth() == 16 || mwf.GetBitDepth() == 24))
		{
			printf("Unsupported bit depth: %u\n", mwf.GetBitDepth());
			return 4;
		}
		
		smplStart = 0;
		retVal = TimeStr2Sample(tStart.c_str(), mwf.GetSampleRate(), &smplStart);
		if (retVal & 0x80)
		{
			printf("Format of Start Time is invalid!\n");
			return 1;
		}
		smplDurat = mwf.GetTotalSamples();
		retVal = TimeStr2Sample(tLen.c_str(), mwf.GetSampleRate(), &smplDurat);
		if (retVal & 0x80)
		{
			printf("Format of Length is invalid!\n");
			return 1;
		}
		
		return DoMagnitudeStats(mwf, smplStart, smplDurat, tDelta);
	}
	else if (cliApp.got_subcommand(scDetect))
	{
		printf("Detect Split Points\n");
	}
	else if (cliApp.got_subcommand(scSplit))
	{
		printf("Split Files\n");
	}
	
	return 0;
}

// --- utility functions functions ---
INLINE INT16 ReadLE16s(const UINT8* data)
{
	return ((INT8)data[0x01] << 8) | (data[0x00] << 0);
}

INLINE void WriteLE16s(UINT8* data, INT16 value)
{
	data[0x00] = (value >> 0) & 0xFF;
	data[0x01] = (value >> 8) & 0xFF;
	return;
}

INLINE INT32 ReadLE24s(const UINT8* data)
{
	return ((INT8)data[0x02] << 16) | (data[0x01] <<  8) | (data[0x00] <<  0);
}

INLINE void WriteLE24s(UINT8* data, INT32 value)
{
	data[0x00] = (value >>  0) & 0xFF;
	data[0x01] = (value >>  8) & 0xFF;
	data[0x02] = (value >> 16) & 0xFF;
	return;
}

INLINE INT32 MaxVal_SampleBits(UINT8 bits)
{
	INT32 mask_bm2 = 1 << (bits - 2);
	return mask_bm2 | (mask_bm2 - 1);	// return (1 << (bits-1)) - 1
}

static std::vector<std::string> ReadFileIntoStrVector(const std::string& fileName)
{
	FILE* hFile;
	std::vector<std::string> lines;
	std::string tempStr;
	
	hFile = fopen(fileName.c_str(), "rt");
	if (hFile == NULL)
	{
		//printf("Error opening file!\n");
		return std::vector<std::string>();
	}
	
	while(! feof(hFile) && ! ferror(hFile))
	{
		tempStr.resize(0x200);
		char* ptr = fgets(&tempStr[0], (int)tempStr.size(), hFile);
		if (ptr == NULL)
			break;
		tempStr.resize(strlen(&tempStr[0]));
		
		while(! tempStr.empty() && iscntrl((UINT8)tempStr.back()))
			tempStr.pop_back();
		
		lines.push_back(tempStr);
	}
	
	fclose(hFile);
	return lines;
}

static UINT8 TimeStr2Sample(const char* time, UINT32 sampleRate, UINT64* result)
{
	if (*time == '\0')
		return 0x01;	// empty value
	
	{
		// try to parse integer sample value
		char* endPtr;
		unsigned long long parseULL = strtoull(time, &endPtr, 0);
		if (*endPtr == '\0' && endPtr != time)
		{
			*result = (UINT64)parseULL;
			return 0x00;
		}
	}
	
	{
		unsigned int h;
		unsigned int m;
		double s;
		double sf;
		UINT64 sec_total;
		int items;
		
		// determine time format
		// try format HH:MM:ss.ms
		items = sscanf(time, "%u:%u:%lf", &h, &m, &s);
		if (items == 2)
		{
			// try format MM:ss.ms
			h = 0;
			items = sscanf(time, "%u:%lf", &m, &s);
		}
		if (items < 2)
			return 0xFF;	// unknown format
		
		sf = floor(s);
		sec_total = (UINT64)h * 3600 + (UINT64)m * 60 + (unsigned int)sf;
		*result = sec_total * sampleRate + (UINT64)((s - sf) * sampleRate + 0.5);
		return 0x00;
	}
}

// --- main functions ---
static UINT8 DoMagnitudeStats(MultiWaveFile& mwf, UINT64 smplStart, UINT64 smplDurat, UINT32 interval)
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
			double dbMin = 6.0 * log(abs(smplMinVal[curChn]) / smplDivide) / M_LN2;
			double dbMax = 6.0 * log(abs(smplMaxVal[curChn]) / smplDivide) / M_LN2;
			double dbDiff = 6.0 * log(smplDiff / smplDivide / 2) / M_LN2;
			printf("    Ch %u: [%d, %d] diff %d = [%.5f db, %.5f db] diff %.5f db\n", curChn,
				smplMinVal[curChn], smplMaxVal[curChn], smplDiff, dbMin, dbMax, dbDiff);
		}
#else
		printf("%.3f", (double)smplPos / smplRate);
		for (curChn = 0; curChn < chnCnt; curChn ++)
		{
			INT32 smplDiff = smplMaxVal[curChn] - smplMinVal[curChn];
			double dbMin = 6.0 * log(abs(smplMinVal[curChn]) / smplDivide) / M_LN2;
			double dbMax = 6.0 * log(abs(smplMaxVal[curChn]) / smplDivide) / M_LN2;
			double dbDiff = 6.0 * log(smplDiff / smplDivide / 2) / M_LN2;
			printf("\t%.8f\t%.8f\t%.8f", dbMin, dbMax, dbDiff);
		}
		printf("\n");
#endif
	}
	
	return 0;
}
