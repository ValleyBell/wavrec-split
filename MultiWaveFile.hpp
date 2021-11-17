// Copyright 2021, Valley Bell
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __MULTIWAVEFILE_HPP__
#define __MULTIWAVEFILE_HPP__

#include <vector>
#include <string>
#include <stdio.h>	// for FILE
#include "stdtype.h"

#pragma pack(1)
typedef struct
{
	UINT16 wFormatTag;
	UINT16 nChannels;
	UINT32 nSamplesPerSec;
	UINT32 nAvgBytesPerSec;
	UINT16 nBlockAlign;
	UINT16 wBitsPerSample;
	//UINT16 cbSize;	// not required for WAVE_FORMAT_PCM
} WAVEFORMAT;	// as specified by mmsystem.h
#pragma pack()

#define WAVE_FORMAT_PCM			0x0001
#define WAVE_FORMAT_IEEE_FLOAT	0x0003

struct WaveInfo
{
	FILE* hFile;
	WAVEFORMAT format;
	UINT32 dataOfs;
	UINT32 smplCount;
};

struct WaveItem
{
	std::string fileName;
	UINT64 startSmpl;
	UINT64 smplCount;
	WaveInfo wi;
};

class MultiWaveFile
{
public:
	MultiWaveFile();
	~MultiWaveFile();
	static UINT8 LoadSingleWave(const std::string& fileName, WaveInfo& wi);
	UINT8 LoadWaveFiles(const std::vector<std::string>& fileList);
	void CloseFiles(void);
	
	size_t ReadSamples(size_t bufSize, void* buffer);	// returns the number of samples read
	
	UINT64 GetTotalSamples(void) const;
	UINT16 GetCompression(void) const;
	UINT16 GetChannels(void) const;
	UINT8 GetBitDepth(void) const;
	UINT32 GetSampleSize(void) const;
	UINT32 GetSampleRate(void) const;
	UINT64 GetSampleReadOffset(void) const;
	void SetSampleReadOffset(UINT64 readOffset);
	
private:
	size_t GetFileFromSample(UINT64 sample);
	
	std::vector<WaveItem> _files;
	UINT64 _totalSamples;
	
	UINT16 _compression;
	UINT8 _bitDepth;
	UINT16 _channels;
	UINT32 _sampleRate;
	
	UINT64 _smplOfs;	// current sample read offset
	size_t _smplOfsFile;
	
	UINT32 _dBufSmpls;
	std::vector<UINT8> _dataBuf;
};

#endif	// __MULTIWAVEFILE_HPP__
