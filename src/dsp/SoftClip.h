#pragma once
#include <algorithm>
#include <cmath>

// Memoryless stereo-linked rational soft knee. Finite input samples are assumed.
// Bounds samples, not reconstructed true peaks; nonlinear distortion is intentional.
struct SoftClip {
    static constexpr double Knee = .85;
    static constexpr double Ceiling = .98;
    static double shapedPeak(double peak) {
        if (peak <= Knee)
            return peak;
        const double excess = peak - Knee;
        const double width = Ceiling - Knee;
        // Value and first derivative meet the identity segment at Knee.
        return Knee + width * (excess / (width + excess));
    }
    static void process(double &left, double &right) {
        const double peak = std::max(std::abs(left), std::abs(right));
        // Invalid audio is a host/input error: do not hide it as a passing sample.
        if (!std::isfinite(left) || !std::isfinite(right) || peak <= Knee)
            return;
        const double gain = shapedPeak(peak) / peak;
        left *= gain;
        right *= gain;
    }
};
