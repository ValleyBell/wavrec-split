#ifndef __FUNC_HPP__
#define __FUNC_HPP__

#include <vector>
#include <string>
#include "stdtype.h"

class MultiWaveFile;

// Magnitude Statistics
int DoMagnitudeStats(MultiWaveFile& mwf, UINT64 smplStart, UINT64 smplDurat, UINT32 interval);

// Split Detection
struct DetectOpts
{
	double ampSplit;	// split amplitude, <0: db, >0: sample value, examples: -81.64, 0x2A0
	double ampFinetune;	// amplitude for split point finetuning, examples: -85.15, 0x1C0
	double tSplit;		// split time in seconds
};
int DoSplitDetection(MultiWaveFile& mwf, const std::vector<std::string>& fileNameList, const DetectOpts& opts);

#endif	// __FUNC_HPP__
