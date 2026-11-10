#pragma once

namespace CrossfeedConfig {
inline constexpr float DefaultGainDb = 0.f;
inline constexpr float MinGainDb = -6.f;
inline constexpr float MaxGainDb = 0.f;
// Bank fields: Feed and GainDb (two Real32 values), then one format byte.
inline constexpr unsigned char BankVersion = 1;
inline constexpr unsigned BankBytes = 2 * sizeof(float) + 1;
inline constexpr unsigned MinSampleRate = 8000;
inline constexpr unsigned MaxSampleRate = 192000;
// Delay in samples = sampleRate * 3 / 10000 (0.3 ms).
inline constexpr unsigned DelayNumerator = 3;
inline constexpr unsigned DelayDenominator = 10000;
inline constexpr unsigned tailFrames(unsigned rate) { return (rate + 199) / 200; } // ceil(5 ms)
}
