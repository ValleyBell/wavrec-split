// Copyright 2021, Valley Bell
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __FUNC_HPP__
#define __FUNC_HPP__

#include <vector>
#include <string>
#include "stdtype.h"

class MultiWaveFile;

// Amplitude Statistics
int DoAmplitudeStats(MultiWaveFile& mwf, UINT64 smplStart, UINT64 smplDurat, UINT32 interval);

// Split Detection
struct DetectOpts
{
	double ampSplit;	// split amplitude, <0: db, >0: sample value, examples: -81.64, 0x2A0
	double ampFinetune;	// amplitude for split point finetuning, examples: -85.15, 0x1C0
	double tSplit;		// split time in seconds
};
int DoSplitDetection(MultiWaveFile& mwf, const std::vector<std::string>& fileNameList, const DetectOpts& opts);

// Splitting/Trimming
struct TrimOpts
{
	bool force16bit;	// output 16-bit WAV even for 24-bit input
	bool applyGain;		// enable applying gain
};
struct TrimInfo
{
	std::string fileName;
	UINT64 smplStart;
	UINT64 smplEnd;
	double gain;	// global track gain (in db)
	std::vector<double> chnGain;	// additional per-channel gain (in db)
};
UINT8 DoWaveTrim(MultiWaveFile& mwf, const TrimInfo& trim, const TrimOpts& opts);

#endif	// __FUNC_HPP__
