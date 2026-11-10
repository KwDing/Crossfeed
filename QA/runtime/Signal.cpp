#include "Fixture.h"
#include <complex>
void RuntimeFixture::checkStereoLevels() {
    float feed = .316227766f, initialGain = -1.f;
    p->SetParam(0, &feed, 4);
    p->SetParam(2, &initialGain, 4);
    fx->Reset();
    for (int block = 0; block < 20; ++block) {
        std::fill(data.begin(), data.begin() + 256, .1f);
        std::fill(data.begin() + 256, data.end(), 0.f);
        b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
        b.eState = AK_DataReady;
        fx->Execute(&b);
    }
    check(std::abs(data[511] / data[255] - feed) < 1.e-4f, "crossfeed opposite/direct DC ratio");
    fx->Reset();
    for (int i = 0; i < 256; ++i)
        data[i] = data[i + 256] = float(.1 * std::sin(i * .13));
    b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
    b.eState = AK_DataReady;
    fx->Execute(&b);
    for (int i = 0; i < 256; ++i)
        check(data[i] == data[i + 256], "crossfeed centered mono symmetric");
    feed = 0;
    p->SetParam(0, &feed, 4);
    fx->Reset();
    for (int i = 0; i < 256; ++i) {
        data[i] = float(.1 * std::sin(i * .13));
        data[i + 256] = 0;
    }
    auto bypass = data;
    fx->Execute(&b);
    for (size_t i = 0; i < data.size(); ++i)
        check(std::abs(data[i] - bypass[i] * .8912509381337456) < 1.e-8,
              "crossfeed zero feed retains -1dB output trim");
    float unityGain = 0.f;
    check(p->SetParam(2, &unityGain, 4) == AK_Success, "set unity output gain");
    fx->Reset();
    std::copy(bypass.begin(), bypass.end(), data.begin());
    fx->Execute(&b);
    check(data == bypass, "Feed zero and Gain zero preserve input exactly");
    float defaultGain = CrossfeedConfig::DefaultGainDb;
    p->SetParam(2, &defaultGain, 4);
    feed = .316227766f;
    p->SetParam(0, &feed, 4);
}
std::vector<float> RuntimeFixture::impulse(bool rightInput, bool varyingBlocks) {
    std::vector<float> result(4096 * 2);
    fx->Reset();
    for (int offset = 0; offset < 4096;) {
        const int n = std::min(4096 - offset, varyingBlocks ? (offset % 3 == 0 ? 1 : 127) : 256);
        std::fill(data.begin(), data.end(), 0.f);
        if (offset == 0)
            data[rightInput ? 256 : 0] = .1f; // Linear region: do not measure a clipped impulse.
        b.AttachContiguousDeinterleavedData(data.data(), 256, n, format.channelConfig);
        b.eState = AK_DataReady;
        fx->Execute(&b);
        for (int i = 0; i < n; ++i) {
            result[offset + i] = data[i];
            result[4096 + offset + i] = data[256 + i];
        }
        offset += n;
    }
    return result;
}
void RuntimeFixture::checkImpulse(const std::vector<float> &leftImpulse, const char *output) {
    const auto rightImpulse = impulse(true, false);
    check(leftImpulse == impulse(false, true), "crossfeed block partition continuity");
    fx->Reset();
    std::vector<float> large(8192);
    large[0] = .1f;
    AkAudioBuffer largeBuffer;
    largeBuffer.AttachContiguousDeinterleavedData(large.data(), 4096, 4096, format.channelConfig);
    largeBuffer.eState = AK_DataReady;
    fx->Execute(&largeBuffer);
    check(large == leftImpulse, "adapter chunks a 4096-frame buffer without changing output");
    for (int i = 0; i < 4096; ++i)
        check(leftImpulse[i] == rightImpulse[4096 + i] && leftImpulse[4096 + i] == rightImpulse[i],
              "crossfeed stereo symmetry");
    for (int i = 0; i < 14; ++i)
        check(leftImpulse[4096 + i] == 0, "no crossfeed before integer delay");
    if (output) {
        FILE *file = nullptr;
        fopen_s(&file, output, "w");
        check(file != nullptr, "impulse output file");
        std::fprintf(file, "sample,direct,cross\n");
        for (int i = 0; i < 4096; ++i)
            std::fprintf(file, "%d,%.12g,%.12g\n", i, leftImpulse[i], leftImpulse[4096 + i]);
        std::fclose(file);
    }
}
void RuntimeFixture::checkGain(const std::vector<float> &leftImpulse) {
    float gain = -4.f;
    check(p->SetParam(2, &gain, 4) == AK_Success, "set attenuating gain");
    const auto adjusted = impulse(false, false);
    const double scale = std::pow(10.0, (-4.0 - CrossfeedConfig::DefaultGainDb) / 20.0);
    for (size_t i = 0; i < adjusted.size(); ++i)
        check(std::abs(adjusted[i] - leftImpulse[i] * scale) < 1.e-7,
              "Gain scales both channels without changing crossfeed ratio");
    const auto newBank = bankBytes(.316227766f, -4.f);
    check(p->SetParamsBlock(newBank.data(), newBank.size()) == AK_Success, "new Bank restores gain");
    check(alloc.calls == allocations, "no Wwise allocations while processing gain");
    auto cloned = p->Clone(&alloc);
    check(cloned != nullptr, "clone gain preset");
    auto saved = p;
    p = cloned;
    check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "clone effect init");
    const auto clonedResult = impulse(false, false);
    check(clonedResult == adjusted, "clone preserves gain");
    p = saved;
    check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "restore effect init");
    cloned->Term(&alloc);
    allocations = alloc.calls; // Clone setup is outside the processing check.
    for (float requested : {-100.f, 100.f}) {
        check(p->SetParam(2, &requested, 4) == AK_Success, "clamp finite gain");
        const auto limited = impulse(false, false);
        const double expected = std::pow(10.0, ((requested < 0 ? -6.0 : 0.0) - CrossfeedConfig::DefaultGainDb) / 20.0);
        for (size_t i = 0; i < limited.size(); ++i)
            check(std::abs(limited[i] - leftImpulse[i] * expected) < 3.e-7,
                  "gain clamps at documented limits");
    }
    float restore = CrossfeedConfig::DefaultGainDb;
    p->SetParam(2, &restore, 4);
}
// Static power is not exact energy conservation for correlated inputs.
// The linear matrix has induced L2 gain <= sqrt(2) for |H|<=1 and Feed<=1;
// sample-wise soft clipping cannot increase this energy bound.
void RuntimeFixture::checkEnergyBound(float amount, int mode) {
    float gainZero = 0.f;
    p->SetParam(0, &amount, 4);
    p->SetParam(2, &gainZero, 4);
    fx->Reset();
    double inputEnergy = 0, outputEnergy = 0;
    for (int block = 0; block < 32; ++block) {
        for (int i = 0; i < 256; ++i) {
            const int sample = block * 256 + i;
            const double fade = std::min({1.0, sample / 128.0, (8191 - sample) / 128.0});
            const float left =
                float(fade * (.2 * std::sin(sample * .07) + .1 * std::cos(sample * .17)));
            const float right = mode == 0   ? 0.f
                                : mode == 1 ? left
                                : mode == 2 ? -left
                                            : float(fade * .23 * std::sin(sample * .113 + .8));
            data[i] = left;
            data[256 + i] = right;
            inputEnergy += double(left) * left + double(right) * right;
        }
        b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
        b.eState = AK_DataReady;
        fx->Execute(&b);
        for (float x : data)
            outputEnergy += double(x) * x;
    }
    do {
        std::fill(data.begin(), data.end(), 0.f);
        b.AttachContiguousDeinterleavedData(data.data(), 256, 0, format.channelConfig);
        b.eState = AK_NoMoreData;
        fx->Execute(&b);
        for (float x : data)
            outputEnergy += double(x) * x;
    } while (b.eState != AK_NoMoreData);
    check(outputEnergy <= inputEnergy * (2.0 + 1.e-5),
          "correlated signal energy stays within matrix bound");
}

// Exercise the plugin ABI at several rates, with the same effect reinitialized.
// At 700 Hz the -8 dB high shelf is at its -4 dB midpoint.
void RuntimeFixture::checkSampleRates() {
    for (unsigned rate : {8000u, 24000u, 30000u, 30001u, 44100u, 48000u, 96000u, 192000u}) {
        format.uSampleRate = rate;
        check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "rate-specific initialization");
        float feed = 1.f;
        p->SetParam(0, &feed, sizeof(feed));
        auto samples = impulse(false, true);
        const double omega = 2.0 * 3.141592653589793 * 700 / rate;
        std::complex<double> response{};
        for (int i = 0; i < 4096; ++i)
            response += double(samples[4096 + i]) * std::polar(1.0, -omega * i);
        check(std::abs(std::abs(response) / samples[0] - std::pow(10.0, -4.0 / 20.0)) < 1.e-5,
              "700 Hz shelf midpoint across rates");
        // The impulse centroid includes the shelf's own DC group delay. Subtract
        // its analytic value to isolate the integer and fractional delay stages.
        double mass = 0, moment = 0;
        for (int i = 0; i < 4096; ++i) {
            mass += samples[4096 + i];
            moment += i * double(samples[4096 + i]);
        }
        const double a = std::pow(10.0, -8.0 / 40.0);
        const double shelfDelay =
            (1 / std::sqrt(a) - std::sqrt(a)) / (std::sqrt(2.0) * std::tan(omega / 2));
        check(std::abs(moment / mass - shelfDelay - rate * .0003) < .001,
              "0.3 ms DC delay excluding shelf");
        double energy = 0, cutoff = 0;
        unsigned drained = 0;
        checkFixedTail(energy, drained, cutoff);
        checkCutoff(cutoff);
    }
    format.uSampleRate = 48000;
    check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "restore reference format");
}
