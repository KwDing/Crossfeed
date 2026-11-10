#include "Fixture.h"
#include "../../src/dsp/SoftClip.h"

static void checkCurve() {
    double previous = 0;
    for (int i = 0; i <= 20000; ++i) {
        const double input = i / 1000.0;
        double left = input, right = -.25 * input;
        SoftClip::process(left, right);
        check(left >= previous && left <= SoftClip::Ceiling, "soft curve monotone and bounded");
        check(right == -.25 * left, "soft curve preserves stereo ratio");
        if (input <= SoftClip::Knee)
            check(left == input, "soft curve linear below knee");
        previous = left;
    }
    const double h = 1.e-6;
    check(std::abs((SoftClip::shapedPeak(.85 + h) - .85) / h - 1) < 1.e-4,
          "soft curve slope continuous at knee");
    double left = std::numeric_limits<float>::max(), right = -left;
    SoftClip::process(left, right);
    check(std::isfinite(left) && left <= .98 && right == -left, "large finite soft input");
}

static float signal(int sample, unsigned rate, int mode, int channel) {
    const float tone = float(.999 * std::sin(2 * 3.141592653589793 * 100 * sample / rate));
    if (mode == 0) return channel == 0 ? tone : 0.f;
    if (mode == 1) return channel == 1 ? tone : 0.f;
    if (mode == 2) return tone;
    if (mode == 3) return channel == 0 ? tone : -tone;
    if (mode == 4) return sample == 10 ? 1.f : 0.f;
    const unsigned bits = 1664525u * (unsigned(sample) + 7919u * unsigned(channel)) + 1013904223u;
    return float(double(bits >> 8) / 8388608.0 - 1.0);
}

static void checkCase(RuntimeFixture &f, float feed, float gain, int mode) {
    check(f.p->SetParam(0, &feed, 4) == AK_Success, "protection Feed");
    check(f.p->SetParam(2, &gain, 4) == AK_Success, "protection Gain");
    f.fx->Reset();
    for (int offset = 0; offset < int(f.format.uSampleRate / 8); offset += 256) {
        for (int c = 0; c < f.channels; ++c)
            for (int i = 0; i < 256; ++i)
                f.data[c * 256 + i] = signal(offset + i, f.format.uSampleRate, mode, c);
        f.b.AttachContiguousDeinterleavedData(f.data.data(), 256, 256, f.format.channelConfig);
        f.b.eState = AK_DataReady;
        f.fx->Execute(&f.b);
        for (float value : f.data)
            check(std::isfinite(value) && std::abs(value) <= .9800001f && std::abs(value) < 1.f,
                  "DLL sample redline at all tested rates/Feed/Gain/channels");
    }
}

static void checkLinkedDll(RuntimeFixture &f) {
    float zero = 0;
    f.p->SetParam(0, &zero, 4);
    f.p->SetParam(2, &zero, 4);
    f.fx->Reset();
    std::fill(f.data.begin(), f.data.begin() + 256, 1.f);
    if (f.channels == 2)
        std::fill(f.data.begin() + 256, f.data.end(), .25f);
    f.b.AttachContiguousDeinterleavedData(f.data.data(), 256, 256, f.format.channelConfig);
    f.b.eState = AK_DataReady;
    f.fx->Execute(&f.b);
    // Independent oracle at p=1, not a call to the implementation under test.
    const double expected = .85 + .13 * .15 / .28;
    for (int i = 0; i < 256; ++i) {
        check(std::abs(f.data[i] - expected) < 1.e-7, "Feed zero still protects high-level samples");
        if (f.channels == 2)
            check(f.data[256 + i] == .25f * f.data[i], "DLL linked soft gain preserves ratio");
    }
}

void RuntimeFixture::checkProtection() {
    checkCurve();
    for (unsigned rate : {8000u, 24000u, 30000u, 30001u, 44100u, 48000u, 96000u, 192000u}) {
        format.uSampleRate = rate;
        check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "protection sample rate Init");
        for (float feed : {0.f, .316227766f, 1.f})
            for (float gain : {-6.f, -1.f, 0.f})
                for (int mode = 0; mode < 6; ++mode)
                    checkCase(*this, feed, gain, mode);
        checkLinkedDll(*this);
    }
    check(alloc.calls == allocations, "no Wwise allocations in protection processing");
    format.uSampleRate = 48000;
    check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "restore protection test format");
    auto defaults = bankBytes(.316227766f, CrossfeedConfig::DefaultGainDb);
    p->SetParamsBlock(defaults.data(), defaults.size());
}
