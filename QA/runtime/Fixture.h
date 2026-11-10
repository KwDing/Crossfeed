#pragma once
#include "../../src/CrossfeedConfig.h"
#include <array>
#include <windows.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

inline void check(bool b, const char *msg) {
    if (!b) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(2);
    }
}
#include "../TestAllocator.h"
using Alloc = TestAllocator;
struct RuntimeFixture {
    Alloc &alloc;
    AK::IAkPluginParam *p;
    AK::IAkInPlaceEffectPlugin *fx;
    int channels;
    AkAudioFormat format{};
    std::vector<float> data;
    AkAudioBuffer b;
    size_t allocations;
    void initialize();
    void checkSampleRates();
    void checkStereoLevels();
    std::vector<float> impulse(bool rightInput, bool varyingBlocks);
    void checkImpulse(const std::vector<float> &leftImpulse, const char *output);
    void checkGain(const std::vector<float> &leftImpulse);
    void checkEnergyBound(float amount, int mode);
    void checkProtection();
    void checkScenarios(double &peak);
    void checkFixedTail(double &tailEnergy, unsigned &maxTail, double &cutoffPeak);
    void checkCutoff(double &cutoffPeak);
    void checkVirtualization();
};

inline std::array<unsigned char, CrossfeedConfig::BankBytes> bankBytes(float feed, float gainDb) {
    std::array<unsigned char, CrossfeedConfig::BankBytes> bytes{};
    const float fields[]{feed, gainDb};
    std::memcpy(bytes.data(), fields, sizeof(fields));
    bytes.back() = CrossfeedConfig::BankVersion;
    return bytes;
}
