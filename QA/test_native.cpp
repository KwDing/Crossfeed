// Loads the actual runtime DLL and exercises Wwise factory/parameter/buffer ABI.
#include "runtime/Fixture.h"
static void checkBankFormat(AK::IAkPluginParam *p) {
    // Published Windows layout: Feed 0.5, Gain -4 dB, version 1.
    std::array<unsigned char, 9> bank{0, 0, 0, 0x3f, 0, 0, 0x80, 0xc0, 1};
    check(p->SetParamsBlock(bank.data(), bank.size()) == AK_Success, "read current Bank");
    check(p->SetParamsBlock(bank.data(), bank.size() - 1) == AK_InvalidParameter, "reject truncated Bank");
    ++bank.back();
    check(p->SetParamsBlock(bank.data(), bank.size()) == AK_InvalidParameter, "reject unknown format");
    bank = bankBytes(.5f, std::numeric_limits<float>::quiet_NaN());
    check(p->SetParamsBlock(bank.data(), bank.size()) == AK_InvalidParameter, "reject nonfinite field");
}

static void checkParameters(AK::IAkPluginParam *p, Alloc &alloc) {
    float nan = std::numeric_limits<float>::quiet_NaN(), v = .5f;
    check(p->SetParam(0, &nan, 4) == AK_InvalidParameter, "reject NaN");
    float infinity = std::numeric_limits<float>::infinity();
    check(p->SetParam(0, &infinity, 4) == AK_InvalidParameter, "reject infinity");
    check(p->SetParam(999, &v, 4) == AK_InvalidParameter, "reject bad ID");
    check(p->SetParamsBlock(&v, 1) == AK_InvalidParameter, "reject bad bank size");
    check(p->SetParam(1, &v, 4) == AK_InvalidParameter, "reject unknown parameter");
    check(p->SetParam(2, &nan, 4) == AK_InvalidParameter, "reject NaN gain");
    checkBankFormat(p);
    auto copy = p->Clone(&alloc);
    check(copy != nullptr, "clone");
    copy->Term(&alloc);
}
void RuntimeFixture::initialize() {
    AkPluginInfo info;
    check(fx->GetPluginInfo(info) == AK_Success && info.bIsInPlace, "in-place effect info");
// SDK 2019.2.15's AkPluginInfo has no Audio Objects capability field.
// Only read this member with the newer SDKs; production code detects it in
// NativeEffect::DisableObjectConfig without assuming a shared struct layout.
#if AK_WWISESDK_VERSION_MAJOR != 2019
    check(!info.bCanRunOnObjectConfig, "reject Audio Objects bus");
#endif
    format.uSampleRate = 48000;
    format.channelConfig.SetStandard(channels == 1 ? AK_SPEAKER_SETUP_MONO : AK_SPEAKER_SETUP_STEREO);
    for (unsigned invalidRate : {0u, 7999u, 192001u}) {
        format.uSampleRate = invalidRate;
        check(fx->Init(&alloc, nullptr, p, format) == AK_UnsupportedChannelConfig, "reject out-of-range rate");
    }
    format.uSampleRate = 48000;
    format.channelConfig.SetStandard(AK_SPEAKER_SETUP_5POINT1);
    check(fx->Init(&alloc, nullptr, p, format) == AK_UnsupportedChannelConfig, "reject surround");
    format.channelConfig.SetStandard(channels == 1 ? AK_SPEAKER_SETUP_MONO : AK_SPEAKER_SETUP_STEREO);
    check(fx->Init(&alloc, nullptr, p, format) == AK_Success, "effect init");
}
static void checkDefaults(RuntimeFixture &fixture, AK::PluginRegistration *reg) {
    auto fresh = reg->m_pCreateParamFunc(&fixture.alloc);
    check(fresh && fresh->Init(&fixture.alloc, nullptr, 0) == AK_Success, "fresh default params");
    check(fixture.fx->Init(&fixture.alloc, nullptr, fresh, fixture.format) == AK_Success, "fresh default effect");
    const auto defaults = fixture.impulse(false, false);
    const double expected = .1 / std::sqrt(1 + .316227766 * .316227766);
    check(std::abs(defaults[0] - expected) < 1.e-7, "new instance defaults to Gain 0dB");
    check(fixture.fx->Init(&fixture.alloc, nullptr, fixture.p, fixture.format) == AK_Success, "restore test params");
    fresh->Term(&fixture.alloc);
    fixture.allocations = fixture.alloc.calls;
}
static void checkPlugin(AK::PluginRegistration *reg, const char *output, int &plugins) {
    Alloc alloc;
    auto p = reg->m_pCreateParamFunc(&alloc);
    check(p && p->Init(&alloc, nullptr, 0) == AK_Success, "params init");
    check(reg->m_ulCompanyID == 64 && reg->m_ulPluginID == 4106, "Crossfeed identity");
    checkParameters(p, alloc);
    double peak = 0, tailEnergy = 0, cutoffPeak = 0;
    unsigned maxTail = 0;
    for (int channels : {1, 2}) {
        auto fx = static_cast<AK::IAkInPlaceEffectPlugin *>(reg->m_pCreateFunc(&alloc));
        check(fx != nullptr, "factory");
        RuntimeFixture fixture{alloc, p,          fx, channels, {}, std::vector<float>(channels * 256),
                               {},    alloc.calls};
        fixture.initialize();
        fixture.allocations = alloc.calls;
        if (channels == 2)
            checkDefaults(fixture, reg);
        if (channels == 2) {
            fixture.checkSampleRates();
            fixture.checkStereoLevels();
            const auto response = fixture.impulse(false, false);
            fixture.checkImpulse(response, output);
            fixture.checkGain(response);
            for (float amount : {0.f, .25f, .316227766f, .5f, .75f, 1.f})
                for (int mode = 0; mode < 4; ++mode)
                    fixture.checkEnergyBound(amount, mode);
            float gain = CrossfeedConfig::DefaultGainDb;
            p->SetParam(2, &gain, sizeof(gain));
        }
        fixture.checkProtection();
        fixture.checkScenarios(peak);
        fixture.checkFixedTail(tailEnergy, maxTail, cutoffPeak);
        fixture.checkCutoff(cutoffPeak);
        fixture.checkVirtualization();
        fx->Term(&alloc);
    }
    p->Term(&alloc);
    check(alloc.live == 0, "balanced allocator lifetime");
    std::printf("%s{\"plugin_id\":%u,\"finite\":true,\"mono_stereo\":true,\"peak\":%.9g,\"tail_"
                "energy\":%.9g,\"tail_frames\":%u,\"cutoff_peak\":%.9g,\"allocator_balanced\":true}",
                plugins ? "," : "", reg->m_ulPluginID, peak, tailEnergy, maxTail, cutoffPeak);
    ++plugins;
}
int main(int argc, char **argv) {
    check(argc == 2 || argc == 3, "DLL argument and optional impulse output");
    HMODULE dll = LoadLibraryA(argv[1]);
    check(dll != nullptr, "LoadLibrary");
    auto head = (AK::PluginRegistration **)GetProcAddress(dll, "g_pAKPluginList");
    check(head && *head, "factory export");
    std::printf("{\"sample_rate\":48000,\"plugins\":[");
    int plugins = 0;
    for (auto reg = *head; reg; reg = reg->pNext) {
        checkPlugin(reg, argc == 3 ? argv[2] : nullptr, plugins);
    }
    check(plugins == 1, "one Crossfeed registration");
    std::printf("],\"status\":\"passed\"}\n");
    FreeLibrary(dll);
}
