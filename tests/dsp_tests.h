#pragma once
#include "settings.h"
#include <vector>

// Portable processing checks shared by the Windows test binary and the Linux/macOS one.
void require(bool ok,const char* message);
std::vector<float> signal(unsigned frames,unsigned channels);
std::vector<float> run(const std::vector<float>& input,unsigned channels,const std::vector<unsigned>& sizes,micfilter::Settings settings);
std::vector<float> runRate(const std::vector<float>& input,unsigned channels,unsigned rate,const std::vector<unsigned>& sizes,micfilter::Settings settings);
void dspTests();
void rateTests();
