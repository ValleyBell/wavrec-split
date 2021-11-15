#define _USE_MATH_DEFINES
#include <stdio.h>
#include <vector>
#include <string>
#include <math.h>

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "libs/CLI11.hpp"

#define INLINE	static inline


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


// --- utility functions ---
INLINE INT16 ReadLE16s(const UINT8* data);
INLINE void WriteLE16s(UINT8* data, INT16 value);
INLINE INT32 ReadLE24s(const UINT8* data);
INLINE void WriteLE24s(UINT8* data, INT32 value);
static std::vector<std::string> ReadFileIntoStrVector(const std::string& fileName);
static UINT8 TimeStr2Sample(const char* time, UINT32 sampleRate, UINT64* result);

// --- main functions ---
#include "func.hpp"


static CLI::Option_group* CLI_AddInputFileGroup(CLI::App* app, std::vector<std::string>& wavNames, std::string& wavList)
{
	CLI::Option_group* optGrp = app->add_option_group("infiles");
	optGrp->add_option("-f, --file", wavNames, "WAV file")->check(CLI::ExistingFile);
	optGrp->add_option("-l, --list", wavList, "TXT file that lists WAVs")->check(CLI::ExistingFile);
	optGrp->require_option(1);
	return optGrp;
}

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
	DetectOpts detOpts = {-81.64, -85.15, 3.0};
	
	cliApp.require_subcommand();
	
	CLI::App* scMag = cliApp.add_subcommand("magstat", "magnitude statistics");
	CLI_AddInputFileGroup(scMag, wavFileNames, wavFileList);
	scMag->add_option("-s, --start", tStart, "Start Time in [HH:]MM:ss or sample number (plain integer)");
	scMag->add_option("-t, --length", tLen, "Length in [HH:]MM:ss or number of samples");
	scMag->add_option("-i, --interval", tDelta, "Measurement interval, number of samples");
	
	CLI::App* scDetect = cliApp.add_subcommand("detect", "detect split points");
	CLI_AddInputFileGroup(scDetect, wavFileNames, wavFileList);
	scDetect->add_option("-s, --split-names", splitFileName, "TXT file that lists files for resulting split list")->check(CLI::ExistingFile)->required();
	scDetect->add_option("-a, --amp-split", detOpts.ampSplit, "Amplitude for defining splitting silence (<0: db, >0: sample value)");
	scDetect->add_option("-A, --amp-finetune", detOpts.ampFinetune, "Amplitude for split point finetuning (must be lower than amp-split)");
	scDetect->add_option("-t, --time", detOpts.tSplit, "Minimum time of silence for splitting files (in seconds)");
	
	CLI::App* scSplit = cliApp.add_subcommand("split", "split into multiple files");
	CLI_AddInputFileGroup(scSplit, wavFileNames, wavFileList);
	scSplit->add_option("-s, --split", splitFileName, "TXT file that lists split points")->check(CLI::ExistingFile)->required();
	scSplit->add_option("-G, --no-gain", noGain, "ignore split list gain");
	scSplit->add_option("-1, --force-16b", out16Bit, "enforce 16-bit output");
	scSplit->add_option("-o, --output-path", dstPath, "output path");
	
	CLI11_PARSE(cliApp, argc, argv);
	
	if (! wavFileList.empty())
	{
		wavFileNames = ReadFileIntoStrVector(wavFileList);
		if (wavFileNames.empty())
		{
			printf("Failed to load WAV list or list is empty!\n");
			return 1;
		}
	}
	
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
			printf("Only uncompressed PCM is supported.\n");
			return 4;
		}
		if (! (mwf.GetBitDepth() == 16 || mwf.GetBitDepth() == 24))
		{
			printf("Unsupported bit depth: %u\n", mwf.GetBitDepth());
			printf("Only 16 and 24 bit are supported.\n");
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
		std::vector<std::string> splitNames;
		MultiWaveFile mwf;
		UINT8 retVal;
		
		splitNames = ReadFileIntoStrVector(splitFileName);
		if (splitNames.empty())
		{
			printf("Failed to split name list or list is empty!\n");
			return 1;
		}
		if ((detOpts.ampSplit < 0.0 && detOpts.ampFinetune < 0.0) ||
			(detOpts.ampSplit > 0.0 && detOpts.ampFinetune > 0.0))
		{
			if (detOpts.ampFinetune > detOpts.ampSplit)
			{
				printf("Error: Finetune amplitude must be smaller than split amplitude!\n");
				return 1;
			}
		}
		
		printf("Detect Split Points\n");
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
			printf("Only uncompressed PCM is supported.\n");
			return 4;
		}
		if (mwf.GetBitDepth() != 24)
		{
			printf("Unsupported bit depth: %u\n", mwf.GetBitDepth());
			printf("Only 16 and 24 bit are supported.\n");
			return 4;
		}
		
		return DoSplitDetection(mwf, splitNames, detOpts);
	}
	else if (cliApp.got_subcommand(scSplit))
	{
		printf("Split Files\n");
	}
	
	return 0;
}

// --- utility functions ---
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
