// Project-owned Linkwitz-like headphone crossfeed; rate-dependent coefficients and fixed time
// constants.
#pragma once
#include "../CrossfeedConfig.h"
#include "SoftClip.h"
#include <array>
#include <cmath>

class CrossfeedDSP {
    struct Coefficients {
        double b0, b1, b2, a1, a2;
    } coefficients_{};
    // RBJ high shelf: 700 Hz midpoint, -8 dB plateau, slope S=1.
    static Coefficients shelfAt(unsigned rate) {
        const double amplitude = std::pow(10.0, -8.0 / 40.0);
        const double omega = 2.0 * 3.14159265358979323846 * 700.0 / rate;
        const double c = std::cos(omega), beta = std::sqrt(2.0 * amplitude) * std::sin(omega);
        const double sum = amplitude + 1.0, diff = amplitude - 1.0;
        const double a0 = sum - diff * c + beta;
        return {amplitude * (sum + diff * c + beta) / a0, -2.0 * amplitude * (diff + sum * c) / a0,
                amplitude * (sum + diff * c - beta) / a0, 2.0 * (diff - sum * c) / a0,
                (sum - diff * c - beta) / a0};
    }
    struct Shelf {
        double first = 0, second = 0;
        double process(double input, const Coefficients &k) {
            const double output = k.b0 * input + first;
            first = k.b1 * input - k.a1 * output + second;
            second = k.b2 * input - k.a2 * output;
            if (std::abs(first) < 1.e-20)
                first = 0;
            if (std::abs(second) < 1.e-20)
                second = 0;
            return output;
        }
    } shelfLeft_, shelfRight_;
    struct FractionalDelay {
        double previousInput = 0, previousOutput = 0;
        double process(double input, double coefficient) {
            if (coefficient == 1.0)
                return input; // Exactly integer delay.
            const double output = coefficient * input + previousInput - coefficient * previousOutput;
            previousInput = std::abs(input) < 1.e-20 ? 0 : input;
            previousOutput = std::abs(output) < 1.e-20 ? 0 : output;
            return output;
        }
    } fractionalLeft_, fractionalRight_;
    static constexpr unsigned Capacity = CrossfeedConfig::MaxSampleRate *
                                         CrossfeedConfig::DelayNumerator /
                                         CrossfeedConfig::DelayDenominator;
    unsigned delayFrames_ = 0;
    double fractionalCoefficient_ = 1.0;
    std::array<float, Capacity> delayLeft_{}, delayRight_{};
    unsigned cursor_ = 0;
    float feed_ = 0.316227766f;
    double powerGain_ = 1.0 / std::sqrt(1.0 + double(feed_) * feed_);
    float gainDb_ = CrossfeedConfig::DefaultGainDb;
    double outputGain_ = std::pow(10.0, double(gainDb_) / 20.0);

  public:
    void configure(unsigned rate) {
        coefficients_ = shelfAt(rate);
        const unsigned units = rate * CrossfeedConfig::DelayNumerator;
        delayFrames_ = units / CrossfeedConfig::DelayDenominator;
        double fraction =
            double(units % CrossfeedConfig::DelayDenominator) / CrossfeedConfig::DelayDenominator;
        // Very small fractions put the allpass pole near -1. Transfer one
        // sample from the ring to the allpass to keep its decay short.
        if (fraction > 0.0 && fraction < 0.1) {
            --delayFrames_;
            fraction += 1.0;
        }
        fractionalCoefficient_ = (1.0 - fraction) / (1.0 + fraction);
        reset();
    }
    void reset() {
        shelfLeft_ = {};
        shelfRight_ = {};
        fractionalLeft_ = {};
        fractionalRight_ = {};
        delayLeft_.fill(0);
        delayRight_.fill(0);
        cursor_ = 0;
    }
    void setParameter(int, float value) {
        if (value != feed_) {
            feed_ = value;
            // Constant-power coefficients for uncorrelated, unfiltered branches.
            // Correlation and the shelf mean arbitrary signals are not energy invariant.
            powerGain_ = 1.0 / std::sqrt(1.0 + double(value) * value);
        }
    }
    void setGainDb(float value) {
        if (value != gainDb_) {
            gainDb_ = value;
            outputGain_ = std::pow(10.0, double(value) / 20.0);
        }
    }
    void processReplacing(float **input, float **output, int frames) {
        // User Gain precedes the final sample bound; no makeup gain follows it.
        const double normalization = outputGain_ * powerGain_;
        for (int i = 0; i < frames; ++i) {
            const float left = input[0][i], right = input[1][i];
            const float delayedLeft = delayLeft_[cursor_], delayedRight = delayRight_[cursor_];
            delayLeft_[cursor_] = left;
            delayRight_[cursor_] = right;
            if (++cursor_ == delayFrames_)
                cursor_ = 0;
            const double crossLeft = fractionalLeft_.process(
                shelfLeft_.process(delayedLeft, coefficients_), fractionalCoefficient_);
            const double crossRight = fractionalRight_.process(
                shelfRight_.process(delayedRight, coefficients_), fractionalCoefficient_);
            double mixedLeft = normalization * (left + feed_ * crossRight);
            double mixedRight = normalization * (right + feed_ * crossLeft);
            // Keep double intermediates until after clipping, even for large finite input.
            SoftClip::process(mixedLeft, mixedRight);
            output[0][i] = static_cast<float>(mixedLeft);
            output[1][i] = static_cast<float>(mixedRight);
        }
    }
};
