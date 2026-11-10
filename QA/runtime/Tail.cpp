#include "Fixture.h"
void RuntimeFixture::checkScenarios(double &peak) {
    for (int scenario = 0; scenario < 8; ++scenario) {
        check(fx->Reset() == AK_Success, "reset");
        // Endpoints plus six deterministic intermediate settings, with three buffer sizes.
        for (int i = 0; i < 1; ++i) {
            float f = scenario == 0   ? 0
                      : scenario == 1 ? 1
                                      : float((scenario * 17 + i * 29) % 101) / 100.f;
            check(p->SetParam(i, &f, 4) == AK_Success, "set");
        }
        for (int block = 0; block < 400; ++block) {
            const int frames = block % 3 == 0 ? 1 : block % 3 == 1 ? 127 : 256;
            std::fill(data.begin(), data.end(), 0.f);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < frames; ++i)
                    data[c * 256 + i] = block < 200
                                            ? float(.08 * std::sin((block * 256 + i) * .071 + c))
                                            : (block == 200 && i == 0 ? .1f : 0.f);
            b.AttachContiguousDeinterleavedData(data.data(), 256, frames, format.channelConfig);
            b.eState = AK_DataReady;
            fx->Execute(&b);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < b.uValidFrames; ++i) {
                    float x = data[c * 256 + i];
                    check(std::isfinite(x), "finite output");
                    peak = std::max(peak, double(std::abs(x)));
                }
        }
    }
}
void RuntimeFixture::checkFixedTail(double &tailEnergy, unsigned &maxTail, double &cutoffPeak) {
    const unsigned expectedFrames = static_cast<unsigned>(std::ceil(format.uSampleRate * .005));
    // End with an impulse to measure the entire fixed drain, including block rounding.
    const auto bank = bankBytes(.316227766f, CrossfeedConfig::DefaultGainDb);
    check(p->SetParamsBlock(bank.data(), bank.size()) == AK_Success, "load fixed-tail test preset");
    for (int drainFrames : {1, 64, 256}) {
        fx->Reset();
        std::fill(data.begin(), data.end(), 0.f);
        data[255] = 1.f;
        b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
        b.eState = AK_DataReady;
        fx->Execute(&b);
        unsigned drained = 0;
        do {
            std::fill(data.begin(), data.end(), 0.f);
            b.AttachContiguousDeinterleavedData(data.data(), drainFrames, 0, format.channelConfig);
            b.eState = AK_NoMoreData;
            fx->Execute(&b);
            drained += b.uValidFrames;
            for (float x : data) {
                check(std::isfinite(x), "finite tail");
                tailEnergy += double(x) * x;
            }
            check(drained < expectedFrames + drainFrames, "tail terminates");
        } while (b.eState != AK_NoMoreData);
        maxTail = std::max(maxTail, drained);
        check(drained >= expectedFrames && drained < expectedFrames + drainFrames, "fixed 5ms tail rounded to buffers");
        for (int c = 0; c < channels; ++c)
            cutoffPeak =
                std::max(cutoffPeak, double(std::abs(data[c * drainFrames + b.uValidFrames - 1])));
    }
}
void RuntimeFixture::checkCutoff(double &cutoffPeak) {
    const unsigned expectedFrames = static_cast<unsigned>(std::ceil(format.uSampleRate * .005));
    // Settled DC and eight phases of a 700 Hz tone, maximum Feed and
    // bounded 0.9 input: check the tail's final transition to silence.
    // This does not remove a source click caused by an abrupt dry stop.
    float maximumFeed = 1.f, maximumGain = 0.f;
    check(p->SetParam(0, &maximumFeed, 4) == AK_Success, "maximum Feed");
    check(p->SetParam(2, &maximumGain, 4) == AK_Success, "maximum output gain at tail cutoff");
    for (int ending = 0; ending < 9; ++ending)
        for (int drainFrames : {1, 64, 256}) {
            fx->Reset();
            for (int block = 0; block < 32; ++block) {
                std::fill(data.begin(), data.end(), 0.f);
                for (int i = 0; i < 256; ++i)
                    data[i] = ending == 0 ? .9f
                                          : float(.9 * std::sin((block * 256 + i) * 2 *
                                                                    3.141592653589793 * 700 / format.uSampleRate +
                                                                (ending - 1) * 3.141592653589793 / 4));
                b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
                b.eState = AK_DataReady;
                fx->Execute(&b);
            }
            unsigned drained = 0;
            do {
                std::fill(data.begin(), data.end(), 0.f);
                b.AttachContiguousDeinterleavedData(data.data(), drainFrames, 0, format.channelConfig);
                b.eState = AK_NoMoreData;
                fx->Execute(&b);
                drained += b.uValidFrames;
                check(drained < expectedFrames + drainFrames, "fixed tail after sustained input");
            } while (b.eState != AK_NoMoreData);
            for (int c = 0; c < channels; ++c)
                cutoffPeak =
                    std::max(cutoffPeak, double(std::abs(data[c * drainFrames + b.uValidFrames - 1])));
        }
    check(cutoffPeak < 1.584893192e-5, "tail transition below -96dBFS");
}
void RuntimeFixture::checkVirtualization() {
    check(fx->TimeSkip(48000) == AK_DataReady, "virtual skip");
    std::fill(data.begin(), data.end(), 0.f);
    b.AttachContiguousDeinterleavedData(data.data(), 256, 256, format.channelConfig);
    b.eState = AK_DataReady;
    fx->Execute(&b);
    for (float x : data)
        check(std::isfinite(x) && std::abs(x) < 1.e-5f, "virtual history reset");
    check(alloc.calls == allocations, "no Wwise allocations in Execute/Reset");
}
