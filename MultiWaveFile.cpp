#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>	// for memset()/memcmp()
#include "stdtype.h"

#include "MultiWaveFile.hpp"

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE


static std::string GetTimeStrHMS(UINT32 smplRate, UINT64 smplPos);
static size_t GetLastSepPos(const std::string& fileName);
INLINE std::string GetFileTitle(const std::string& fileName);


MultiWaveFile::MultiWaveFile() :
	_totalSamples(0)
{
}

MultiWaveFile::~MultiWaveFile()
{
	CloseFiles();
}

UINT64 MultiWaveFile::GetTotalSamples(void) const
{
	return _totalSamples;
}

UINT16 MultiWaveFile::GetCompression(void) const
{
	return _compression;
}

UINT16 MultiWaveFile::GetChannels(void) const
{
	return _channels;
}

UINT8 MultiWaveFile::GetBitDepth(void) const
{
	return _bitDepth;
}

UINT32 MultiWaveFile::GetSampleSize(void) const
{
	return _channels * _bitDepth / 8;
}

UINT32 MultiWaveFile::GetSampleRate(void) const
{
	return _sampleRate;
}

UINT64 MultiWaveFile::GetSampleReadOffset(void) const
{
	return _smplOfs;
}

void MultiWaveFile::SetSampleReadOffset(UINT64 readOffset)
{
	_smplOfs = readOffset;
	if (_smplOfs >= GetTotalSamples())
		_smplOfs = 0;
}

size_t MultiWaveFile::GetFileFromSample(UINT64 sample)
{
	size_t curFile;
	
	for (curFile = 0; curFile < _files.size(); curFile ++)
	{
		const WaveItem& wItm = _files[curFile];
		if (sample < wItm.startSmpl + wItm.smplCount)
			return curFile;
	}
	return (size_t)-1;
}

/*static*/ UINT8 MultiWaveFile::LoadSingleWave(const std::string& fileName, WaveInfo& wi)
{
	FILE* hFile;
	char chnkFCC[4];
	UINT32 chnkSize;
	UINT8 found;
	
	memset(&wi, 0x00, sizeof(WaveInfo));
	
	hFile = fopen(fileName.c_str(), "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 0xFF;
	}
	
	fread(chnkFCC, 0x04, 1, hFile);
	if (memcmp(chnkFCC, "RIFF", 4))
	{
		fclose(hFile);
		printf("Bad file.\n");
		return 0xF0;
	}
	fseek(hFile, 0x08, SEEK_SET);
	fread(chnkFCC, 0x04, 1, hFile);
	if (memcmp(chnkFCC, "WAVE", 4))
	{
		fclose(hFile);
		printf("Bad file.\n");
		return 0xF1;
	}
	
	found = 0x00;
	while(! feof(hFile) && ! ferror(hFile))
	{
		fread(chnkFCC, 0x04, 1, hFile);
		fread(&chnkSize, 0x04, 1, hFile);
		size_t fPos = ftell(hFile);
		if (! memcmp(chnkFCC, "data", 4))
		{
			found |= 0x02;
			wi.dataOfs = (UINT32)ftell(hFile);
			break;
		}
		else if (! memcmp(chnkFCC, "fmt ", 4))
		{
			found |= 0x01;
			fread(&wi.format, sizeof(WAVEFORMAT), 1, hFile);
		}
		fseek(hFile, (long)(fPos + chnkSize), SEEK_SET);
	}
	if (! (found & 0x01))
	{
		fclose(hFile);
		printf("Format chunk not found.\n");
		return 0xF2;
	}
	if (! (found & 0x02))
	{
		fclose(hFile);
		printf("Data chunk not found.\n");
		return 0xF3;
	}
	
	wi.hFile = hFile;
	wi.smplCount = chnkSize / wi.format.nBlockAlign;
	
	return 0x00;
}

UINT8 MultiWaveFile::LoadWaveFiles(const std::vector<std::string>& fileList)
{
	size_t curFile;
	UINT8 retVal;
	
	CloseFiles();
	
	_totalSamples = 0;
	for (curFile = 0; curFile < fileList.size(); curFile ++)
	{
		WaveItem wItm;
		std::string fileTitle;
		
		wItm.fileName = fileList[curFile];
		fileTitle = GetFileTitle(wItm.fileName);
		retVal = MultiWaveFile::LoadSingleWave(wItm.fileName, wItm.wi);
		if (retVal)
		{
			printf("Error 0x%02X opening %s!\n", retVal, fileTitle.c_str());
			return retVal;
		}
		if (curFile == 0)
		{
			_compression = wItm.wi.format.wFormatTag;
			_channels = wItm.wi.format.nChannels;
			_bitDepth = (UINT8)wItm.wi.format.wBitsPerSample;
			_sampleRate = wItm.wi.format.nSamplesPerSec;
		}
		else
		{
			if (_compression != wItm.wi.format.wFormatTag ||
				_channels != wItm.wi.format.nChannels ||
				_bitDepth != wItm.wi.format.wBitsPerSample ||
				_sampleRate != wItm.wi.format.nSamplesPerSec)
			{
				fclose(wItm.wi.hFile);
				printf("File %s has a different format!\n", fileTitle.c_str());
				return 0x80;
			}
		}
		wItm.startSmpl = _totalSamples;
		wItm.smplCount = wItm.wi.smplCount;
		_totalSamples += wItm.smplCount;
		_files.push_back(wItm);
	}
	
	//_dBufSmpls = 0x10000;	// 64k samples
	//_dataBuf.resize(GetSampleSize() * _dBufSmpls);
	
	_smplOfs = 0;
	_smplOfsFile = 0;
	
	std::string duratStr = GetTimeStrHMS(_sampleRate, _totalSamples);
	printf("Opened %u %s. Format %u, Channels %u, Bits %u, Rate %u, Total Duration: %s\n",
		(unsigned)_files.size(), (_files.size() == 1) ? "file" : "files",
		_compression, _channels, _bitDepth, _sampleRate, duratStr.c_str());
	
	return 0x00;
}

void MultiWaveFile::CloseFiles(void)
{
	size_t curFile;
	
	for (curFile = 0; curFile < _files.size(); curFile ++)
	{
		const WaveInfo& wi = _files[curFile].wi;
		if (wi.hFile != NULL)
			fclose(wi.hFile);
	}
	_files.clear();
	
	return;
}

size_t MultiWaveFile::ReadSamples(size_t bufSize, void* buffer)
{
	size_t smplSize = GetSampleSize();
	size_t remSmpls = bufSize / smplSize;
	size_t bufPos = 0;
	UINT8* bufPtr = (UINT8*)buffer;
	WaveItem* wItm = (_smplOfsFile < _files.size()) ? &_files[_smplOfsFile] : NULL;
	
	if (wItm == NULL || _smplOfs < wItm->startSmpl || _smplOfs >= (wItm->startSmpl + wItm->smplCount))
	{
		_smplOfsFile = GetFileFromSample(_smplOfs);
		wItm = (_smplOfsFile != (size_t)-1) ? &_files[_smplOfsFile] : NULL;
	}
	
	while(wItm != NULL && remSmpls > 0)
	{
		size_t fileSmpl = (UINT32)(_smplOfs - wItm->startSmpl);
		size_t fileOfs = wItm->wi.dataOfs + fileSmpl * smplSize;
		size_t readSmpls;
		
		fseek(wItm->wi.hFile, (long)fileOfs, SEEK_SET);
		readSmpls = (UINT32)wItm->smplCount - fileSmpl;
		if (readSmpls > remSmpls)
			readSmpls = remSmpls;
		readSmpls = (UINT32)fread(&bufPtr[bufPos], smplSize, readSmpls, wItm->wi.hFile);
		if (readSmpls == 0)
			break;
		
		bufPos += readSmpls * smplSize;
		remSmpls -= readSmpls;
		_smplOfs += readSmpls;
		while(wItm != NULL && _smplOfs >= (wItm->startSmpl + wItm->smplCount))
		{
			_smplOfsFile ++;
			wItm = (_smplOfsFile < _files.size()) ? &_files[_smplOfsFile] : NULL;
		}
	}
	
	return bufSize / smplSize - remSmpls;
}

static std::string GetTimeStrHMS(UINT32 smplRate, UINT64 smplPos)
{
	char timeStr[0x20];
	UINT32 sec_smpls = (UINT32)(smplPos % (UINT64)smplRate);
	UINT32 seconds = (UINT32)(smplPos / (UINT64)smplRate);
	UINT32 mins = seconds / 60;
	UINT32 hours = mins / 60;
	sprintf(timeStr, "%02u:%02u:%02u.%03u", hours, mins % 60, seconds % 60, sec_smpls * 1000 / smplRate);
	return std::string(timeStr);
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

INLINE std::string GetFileTitle(const std::string& fileName)
{
	size_t sepPos = GetLastSepPos(fileName);
	return (sepPos == std::string::npos) ? fileName : fileName.substr(sepPos + 1);
}
