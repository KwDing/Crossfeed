// Offline fixture through the actual runtime DLL. No Wwise project or third-party media.
#include <windows.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(2);
    }
}
#include "TestAllocator.h"
using Alloc = TestAllocator;
static void wav(const std::string &path, const std::vector<float> &interleaved) {
    FILE *file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    check(file != nullptr, "open WAV");
    auto u16 = [&](uint16_t value) { check(std::fwrite(&value, 2, 1, file) == 1, "write WAV"); };
    auto u32 = [&](uint32_t value) { check(std::fwrite(&value, 4, 1, file) == 1, "write WAV"); };
    const auto bytes = static_cast<uint32_t>(interleaved.size() * sizeof(float));
    std::fwrite("RIFF", 1, 4, file);
    u32(48 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, file);
    u32(16);
    u16(3);
    u16(2);
    u32(48000);
    u32(48000 * 8);
    u16(8);
    u16(32);
    std::fwrite("fact", 1, 4, file);
    u32(4);
    u32(static_cast<uint32_t>(interleaved.size() / 2));
    std::fwrite("data", 1, 4, file);
    u32(bytes);
    check(std::fwrite(interleaved.data(), sizeof(float), interleaved.size(), file) ==
              interleaved.size(),
          "write samples");
    std::fclose(file);
}
// Equal-power pan approximates a listener turning halfway around a stationary source.
static std::vector<float> pannedNoise() {
    constexpr int sampleRate = 48000;
    constexpr int frames = 9 * sampleRate;
    constexpr double halfPi = 1.5707963267948966;
    std::vector<float> samples(frames * 2);
    uint32_t seed = 0x41c64e6du;
    for (int i = 0; i < frames; ++i) {
        seed = 1664525u * seed + 1013904223u;
        const double noise = (double(seed >> 8) / 8388608.0 - 1.0) * 0.2;
        const double time = double(i) / sampleRate;
        const double pan = std::clamp((time - 3.0) / 3.0, 0.0, 1.0);
        const double fade = std::min({1.0, i / 960.0, (frames - 1 - i) / 960.0});
        samples[2 * i] = pan == 1.0 ? 0.f : float(noise * fade * std::cos(pan * halfPi));
        samples[2 * i + 1] = float(noise * fade * std::sin(pan * halfPi));
    }
    return samples;
}

static std::vector<float> render(AK::IAkPluginParam *params, AK::IAkInPlaceEffectPlugin *effect,
                                 const AkAudioFormat &format, const std::vector<float> &input,
                                 float feed) {
    const int frameCount = static_cast<int>(input.size() / 2);
    check(params->SetParam(0, &feed, sizeof(feed)) == AK_Success, "set feed");
    effect->Reset();
    std::vector<float> result(input.size());
    float block[512]{};
    for (int offset = 0; offset < frameCount; offset += 256) {
        const int n = std::min(256, frameCount - offset);
        for (int j = 0; j < n; ++j) {
            block[j] = input[2 * (offset + j)];
            block[256 + j] = input[2 * (offset + j) + 1];
        }
        AkAudioBuffer buffer;
        buffer.AttachContiguousDeinterleavedData(block, 256, static_cast<AkUInt16>(n),
                                                 format.channelConfig);
        buffer.eState = AK_DataReady;
        effect->Execute(&buffer);
        for (int j = 0; j < n; ++j) {
            result[2 * (offset + j)] = block[j];
            result[2 * (offset + j) + 1] = block[256 + j];
        }
    }
    return result;
}
static std::vector<float> readInput(const char *path) {
    FILE *file = nullptr;
    fopen_s(&file, path, "rb");
    check(file != nullptr, "open stereo float input");
    std::fseek(file, 0, SEEK_END);
    const long bytes = std::ftell(file);
    std::rewind(file);
    check(bytes > 0 && bytes % 8 == 0, "interleaved float32 stereo input");
    std::vector<float> input(bytes / 4);
    check(std::fread(input.data(), 4, input.size(), file) == input.size(), "read input");
    std::fclose(file);
    for (float value : input) check(std::isfinite(value), "finite demo input");
    return input;
}
int main(int argc, char **argv) {
    check(argc == 3 || argc == 4, "runtime DLL, output directory, optional 48k stereo float32 input");
    HMODULE dll = LoadLibraryA(argv[1]);
    check(dll != nullptr, "load runtime");
    auto head = reinterpret_cast<AK::PluginRegistration **>(GetProcAddress(dll, "g_pAKPluginList"));
    check(head && *head && !(*head)->pNext && (*head)->m_ulPluginID == 4106, "one Crossfeed factory");
    Alloc allocator;
    auto params = (*head)->m_pCreateParamFunc(&allocator);
    check(params && params->Init(&allocator, nullptr, 0) == AK_Success, "parameter init");
    float gain = 0.f;
    check(params->SetParam(2, &gain, sizeof(gain)) == AK_Success, "demo Gain zero");
    auto effect = static_cast<AK::IAkInPlaceEffectPlugin *>((*head)->m_pCreateFunc(&allocator));
    check(effect != nullptr, "effect factory");
    AkAudioFormat format{};
    format.uSampleRate = 48000;
    format.channelConfig.SetStandard(AK_SPEAKER_SETUP_STEREO);
    check(effect->Init(&allocator, nullptr, params, format) == AK_Success, "effect init");
    const auto source = argc == 4 ? readInput(argv[3]) : pannedNoise();
    const auto processed = render(params, effect, format, source, .316227766f);
    for (float value : processed)
        check(std::isfinite(value) && std::abs(value) < 1, "actual DLL sample redline");
    const std::string output = argv[2];
    wav(output + "/demo_source.wav", source);
    wav(output + "/demo_crossfeed.wav", processed);
    effect->Term(&allocator);
    params->Term(&allocator);
    check(allocator.live == 0, "demo allocator balanced");
    FreeLibrary(dll);
}
