#define _USE_MATH_DEFINES
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <math.h>

#include "stdtype.h"
#include "MultiWaveFile.hpp"
#include "func.hpp"
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


struct SplitOpts
{
	std::string dstPath;
	UINT32 leadSamples;
	UINT32 trailSamples;
};

static UINT8 DoSplitFiles(MultiWaveFile& mwf, const std::vector<std::string>& tlLines, const SplitOpts& splitOpts, const TrimOpts& trimOpts);
static UINT8 ReadFileIntoStrVector(const std::string& fileName, std::vector<std::string>& result);
static UINT8 TimeStr2Sample(const char* time, UINT32 sampleRate, UINT64* result);
static size_t GetLastSepPos(const std::string& fileName);
INLINE std::string GetDirPath(const std::string& fileName);
INLINE std::string GetFileTitle(const std::string& fileName);
static void CreateDirTree(const std::string& dirPath);


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
	DetectOpts detOpts = {-81.64, -85.15, 3.0};
	TrimOpts trimOpts = {false, false};
	SplitOpts splitOpts = {".", 0, 0};
	
	cliApp.require_subcommand();
	
	CLI::App* scMag = cliApp.add_subcommand("ampstat", "amplitude statistics");
	CLI_AddInputFileGroup(scMag, wavFileNames, wavFileList);
	scMag->add_option("-s, --start", tStart, "Start Time in [HH:]MM:ss or sample number (plain integer)");
	scMag->add_option("-t, --length", tLen, "Length in [HH:]MM:ss or number of samples");
	scMag->add_option("-i, --interval", tDelta, "Measurement interval, number of samples");
	
	CLI::App* scDetect = cliApp.add_subcommand("detect", "detect split points");
	CLI_AddInputFileGroup(scDetect, wavFileNames, wavFileList);
	scDetect->add_option("-n, --split-names", splitFileName, "TXT file that lists file names for resulting split list")->check(CLI::ExistingFile)->required();
	scDetect->add_option("-a, --amp-split", detOpts.ampSplit, "Amplitude for defining splitting silence (<0: db, >0: sample value)");
	scDetect->add_option("-A, --amp-finetune", detOpts.ampFinetune, "Amplitude for split point finetuning (must be lower than amp-split)");
	scDetect->add_option("-t, --time", detOpts.tSplit, "Minimum time of silence for splitting files (in seconds)");
	
	CLI::App* scSplit = cliApp.add_subcommand("split", "split into multiple files");
	CLI_AddInputFileGroup(scSplit, wavFileNames, wavFileList);
	scSplit->add_option("-t, --trim-list", splitFileName, "TXT file that lists trim points and file names")->check(CLI::ExistingFile)->required();
	scSplit->add_flag("-g, --apply-gain", trimOpts.applyGain, "apply trim list gain (ignored by default)");
	scSplit->add_flag("-1, --force-16b", trimOpts.force16bit, "enforce 16-bit output");
	scSplit->add_option("-o, --output-path", splitOpts.dstPath, "output path");
	scSplit->add_option("-b, --begin-silence", splitOpts.leadSamples, "additional leading samples of silence");
	scSplit->add_option("-e, --end-silence", splitOpts.trailSamples, "additional trailing samples of silence");

	CLI11_PARSE(cliApp, argc, argv);
	
	if (! wavFileList.empty())
	{
		UINT8 retVal = ReadFileIntoStrVector(wavFileList, wavFileNames);
		if (retVal & 0x80)
		{
			fprintf(stderr, "Failed to load WAV list!\n");
			return 1;
		}
		else if (wavFileNames.empty())
		{
			fprintf(stderr, "WAV list is empty!\n");
			return 2;
		}
	}
	
	if (cliApp.got_subcommand(scMag))
	{
		MultiWaveFile mwf;
		UINT8 retVal;
		UINT64 smplStart;
		UINT64 smplDurat;
		
		fprintf(stderr, "Amplitude Statistics\n");
		fprintf(stderr, "--------------------\n");
		
		retVal = mwf.LoadWaveFiles(wavFileNames);
		if (retVal)
		{
			fprintf(stderr, "WAVE Loading failed!\n");
			return 3;
		}
		if (mwf.GetCompression() != WAVE_FORMAT_PCM)
		{
			fprintf(stderr, "Unsupported compression type: %u\n", mwf.GetCompression());
			fprintf(stderr, "Only uncompressed PCM is supported.\n");
			return 4;
		}
		if (! (mwf.GetBitDepth() == 16 || mwf.GetBitDepth() == 24))
		{
			fprintf(stderr, "Unsupported bit depth: %u\n", mwf.GetBitDepth());
			fprintf(stderr, "Only 16 and 24 bit WAVs are supported.\n");
			return 4;
		}
		
		smplStart = 0;
		retVal = TimeStr2Sample(tStart.c_str(), mwf.GetSampleRate(), &smplStart);
		if (retVal & 0x80)
		{
			fprintf(stderr, "Format of Start Time is invalid!\n");
			return 1;
		}
		smplDurat = mwf.GetTotalSamples();
		retVal = TimeStr2Sample(tLen.c_str(), mwf.GetSampleRate(), &smplDurat);
		if (retVal & 0x80)
		{
			fprintf(stderr, "Format of Length is invalid!\n");
			return 1;
		}
		
		return DoAmplitudeStats(mwf, smplStart, smplDurat, tDelta);
	}
	else if (cliApp.got_subcommand(scDetect))
	{
		std::vector<std::string> splitNames;
		MultiWaveFile mwf;
		UINT8 retVal;
		
		retVal = ReadFileIntoStrVector(splitFileName, splitNames);
		if (retVal & 0x80)
		{
			fprintf(stderr, "Failed to load split name list!\n");
			return 1;
		}
		// An empty list is valid in this case.
		if ((detOpts.ampSplit < 0.0 && detOpts.ampFinetune < 0.0) ||
			(detOpts.ampSplit > 0.0 && detOpts.ampFinetune > 0.0))
		{
			if (detOpts.ampFinetune > detOpts.ampSplit)
			{
				fprintf(stderr, "Error: Finetune amplitude must be smaller than split amplitude!\n");
				return 1;
			}
		}
		
		fprintf(stderr, "Detect Split Points\n");
		fprintf(stderr, "-------------------\n");
		
		retVal = mwf.LoadWaveFiles(wavFileNames);
		if (retVal)
		{
			fprintf(stderr, "WAVE Loading failed!\n");
			return 3;
		}
		if (mwf.GetCompression() != WAVE_FORMAT_PCM)
		{
			fprintf(stderr, "Unsupported compression type: %u\n", mwf.GetCompression());
			fprintf(stderr, "Only uncompressed PCM is supported.\n");
			return 4;
		}
		if (mwf.GetBitDepth() != 24)
		{
			fprintf(stderr, "Unsupported bit depth: %u\n", mwf.GetBitDepth());
			fprintf(stderr, "Only 24 bit WAVs are supported.\n");
			return 4;
		}
		
		return DoSplitDetection(mwf, splitNames, detOpts);
	}
	else if (cliApp.got_subcommand(scSplit))
	{
		std::vector<std::string> splitNames;
		MultiWaveFile mwf;
		UINT8 retVal;
		
		retVal = ReadFileIntoStrVector(splitFileName, splitNames);
		if (retVal & 0x80)
		{
			fprintf(stderr, "Failed to load trim list!\n");
			return 1;
		}
		else if (splitNames.empty())
		{
			fprintf(stderr, "Trim list is empty!\n");
			return 2;
		}
		
		if (!splitOpts.dstPath.empty())
		{
			std::string& dstPath = splitOpts.dstPath;
#ifdef _WIN32
			std::replace_if(dstPath.begin(), dstPath.end(), [](char c){ return c == '\\'; }, '/');	// '\\' -> '/'
#endif
			if (dstPath.back() != '/')
				dstPath.push_back('/');
		}
		
		fprintf(stderr, "Split Files\n");
		fprintf(stderr, "-----------\n");
		
		retVal = mwf.LoadWaveFiles(wavFileNames);
		if (retVal)
		{
			fprintf(stderr, "WAVE Loading failed!\n");
			return 3;
		}
		if (mwf.GetCompression() != WAVE_FORMAT_PCM)
		{
			fprintf(stderr, "Unsupported compression type: %u\n", mwf.GetCompression());
			fprintf(stderr, "Only uncompressed PCM is supported.\n");
			return 4;
		}
		if (! (mwf.GetBitDepth() == 16 || mwf.GetBitDepth() == 24))
		{
			fprintf(stderr, "Unsupported bit depth: %u\n", mwf.GetBitDepth());
			fprintf(stderr, "Only 16 and 24 bit WAVs are supported.\n");
			return 4;
		}
		
		return DoSplitFiles(mwf, splitNames, splitOpts, trimOpts);
	}
	
	return 0;
}

static UINT8 DoSplitFiles(MultiWaveFile& mwf, const std::vector<std::string>& tlLines, const SplitOpts& splitOpts, const TrimOpts& trimOpts)
{
	std::vector<TrimInfo> trimList;
	size_t curFile;
	std::vector<double> chnGain(mwf.GetChannels(), 0.0);
	
	for (curFile = 0; curFile < tlLines.size(); curFile ++)
	{
		const std::string& tLine = tlLines[curFile];
		size_t colPos[8];
		size_t curCol;
		double num;
		char* endPtr;
		
		colPos[0] = 0;
		for (curCol = 1; curCol < 4; curCol ++)
		{
			size_t spcPos = tLine.find(' ', colPos[curCol - 1]);
			if (spcPos == std::string::npos)
				break;
			colPos[curCol] = spcPos + 1;
		}
		
		num = strtod(&tLine[colPos[0]], &endPtr);
		if (endPtr != &tLine[colPos[0]])	// first value is a number? (with sign)
		{
			if (curCol < 4)
			{
				fprintf(stderr, "Invalid line: %s\n", tLine.c_str());
				continue;
			}
			TrimInfo ti;
			ti.gain = num;
			ti.smplStart = (UINT64)strtoull(&tLine[colPos[1]], NULL, 0);
			if (ti.smplStart >= splitOpts.leadSamples)
				ti.smplStart -= splitOpts.leadSamples;
			else
				ti.smplStart = 0;
			ti.smplEnd = (UINT64)strtoull(&tLine[colPos[2]], NULL, 0);
			ti.smplEnd += splitOpts.trailSamples;
			ti.fileName = tLine.substr(colPos[3]);
			ti.chnGain = chnGain;
			trimList.push_back(ti);
		}
		else if (tLine[0] == 'b')	// balance
		{
			UINT16 curChn;
			for (curChn = 0; curChn < chnGain.size() && (1U + curChn) < curCol; curChn ++)
				chnGain[curChn] = atof(&tLine[colPos[1 + curChn]]);
			for (; curChn < chnGain.size(); curChn ++)
				chnGain[curChn] = 0.0;
		}
	}
	
	for (curFile = 0; curFile < trimList.size(); curFile ++)
	{
		TrimInfo& ti = trimList[curFile];
		std::string fullPath;
		UINT8 retVal;
		
		fullPath = splitOpts.dstPath + ti.fileName;
		CreateDirTree(GetDirPath(fullPath));
		
		fprintf(stderr, "Writing %s ...\n", ti.fileName.c_str());
		ti.fileName = fullPath;
		retVal = DoWaveTrim(mwf, ti, trimOpts);
		if (retVal)
			fprintf(stderr, "Error creating %s!\n", fullPath.c_str());
	}
	
	return 0;
}

static UINT8 ReadFileIntoStrVector(const std::string& fileName, std::vector<std::string>& result)
{
	FILE* hFile;
	std::vector<std::string> lines;
	std::string tempStr;
	
	hFile = fopen(fileName.c_str(), "rt");
	if (hFile == NULL)
	{
		//fprintf(stderr, "Error opening file!\n");
		return 0xFF;
	}
	
	result.clear();
	while(! feof(hFile) && ! ferror(hFile))
	{
		tempStr.resize(0x200);
		char* ptr = fgets(&tempStr[0], (int)tempStr.size(), hFile);
		if (ptr == NULL)
			break;
		tempStr.resize(strlen(&tempStr[0]));
		
		while(! tempStr.empty() && iscntrl((UINT8)tempStr.back()))
			tempStr.pop_back();
		
		result.push_back(tempStr);
	}
	
	fclose(hFile);
	return 0x00;
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

static size_t GetLastSepPos(const std::string& fileName)
{
	size_t sepPos;
	size_t wSepPos;	// Windows separator
	
	sepPos = fileName.rfind('/');
	wSepPos = fileName.rfind('\\');
	if (wSepPos == std::string::npos)
		return sepPos;
	else if (sepPos == std::string::npos)
		return wSepPos;
	return (wSepPos > sepPos) ? wSepPos : sepPos;
}

INLINE std::string GetDirPath(const std::string& fileName)
{
	size_t sepPos = GetLastSepPos(fileName);
	return (sepPos == std::string::npos) ? "" : fileName.substr(0, sepPos + 1);
}

INLINE std::string GetFileTitle(const std::string& fileName)
{
	size_t sepPos = GetLastSepPos(fileName);
	return (sepPos == std::string::npos) ? fileName : fileName.substr(sepPos + 1);
}

static void CreateDirTree(const std::string& dirPath)
{
	size_t dirSepPos;
	
	dirSepPos = dirPath.find('/');
	while(dirSepPos != std::string::npos)
	{
		MakeDir(dirPath.substr(0, dirSepPos).c_str());
		dirSepPos = dirPath.find('/', dirSepPos + 1);
	}
	
	return;
}
