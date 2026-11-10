// Project Crossfeed Wwise adapter. No explicit allocations or locks in Execute.
#pragma once
#include "NativeParams.h"
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/AkWwiseSDKVersion.h>
#include <AK/Plugin/PluginServices/AkFXTailHandler.h>
#include <algorithm>
#include <cmath>
#include <cstring>

template <class T> class NativeEffect final : public AK::IAkInPlaceEffectPlugin {
    using DSP = typename T::DSP;
    NativeParams<T> *params_ = nullptr; // Borrowed from engine for this instance's lifetime.
    DSP dsp_;
    AkUInt32 tailFrames_ = 0;
    AkFXTailHandler tail_;
    bool wasVirtual_ = false;
    float monoL_[256]{}, monoR_[256]{};
    // SDK 2019.2.15 has no bCanRunOnObjectConfig field. Select the no-op
    // overload for that SDK; 2023/2025 explicitly reject Audio Objects here.
    // Detect the field rather than assuming every SDK shares the same layout.
    template <class Info>
    static auto DisableObjectConfig(Info &info, int)
        -> decltype(info.bCanRunOnObjectConfig = false, void()) {
        info.bCanRunOnObjectConfig = false;
    }
    template <class Info>
    static void DisableObjectConfig(Info &, long) {} // 2019 has no Audio Objects field.
  public:
    AKRESULT Init(AK::IAkPluginMemAlloc *, AK::IAkEffectPluginContext *, AK::IAkPluginParam *p,
                  AkAudioFormat &f) override {
        // This format policy is the same for all supported SDKs. In the 2019
        // host test, inheriting a 7.1 device output caused rejection; an explicit
        // Stereo bus fixed the test setup. That was not a DSP/API version fix.
        if (f.uSampleRate < CrossfeedConfig::MinSampleRate ||
            f.uSampleRate > CrossfeedConfig::MaxSampleRate ||
            f.channelConfig.eConfigType != AK_ChannelConfigType_Standard ||
            (f.channelConfig.uChannelMask != AK_SPEAKER_SETUP_MONO &&
             f.channelConfig.uChannelMask != AK_SPEAKER_SETUP_STEREO))
            return AK_UnsupportedChannelConfig;
        dsp_.configure(f.uSampleRate);
        tailFrames_ = CrossfeedConfig::tailFrames(f.uSampleRate);
        tail_ = AkFXTailHandler();
        wasVirtual_ = false;
        params_ = static_cast<NativeParams<T> *>(p);
        return AK_Success;
    }
    AKRESULT Term(AK::IAkPluginMemAlloc *a) override {
        AK_PLUGIN_DELETE(a, this);
        return AK_Success;
    }
    AKRESULT Reset() override {
        dsp_.reset();
        tail_ = AkFXTailHandler();
        wasVirtual_ = false;
        return AK_Success;
    }
    AKRESULT GetPluginInfo(AkPluginInfo &info) override {
        info = AkPluginInfo();
        info.eType = AkPluginTypeEffect;
        info.bIsInPlace = true;
        DisableObjectConfig(info, 0);
        info.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
        return AK_Success;
    }
    void Execute(AkAudioBuffer *buffer) override {
        if (!params_)
            return;
        // Prototype policy: discard inaudible history on returning from virtualization.
        // Never replay a stale frozen reverb tail; this is not elapsed-time simulation.
        if (wasVirtual_)
            Reset();
        tail_.HandleTail(buffer, tailFrames_);
        for (int i = 0; i < T::Count; ++i)
            dsp_.setParameter(i, params_->values[i]);
        dsp_.setGainDb(params_->gainDb);
        for (AkUInt32 offset = 0; offset < buffer->uValidFrames; offset += 256) {
            const auto n = std::min<AkUInt32>(256, buffer->uValidFrames - offset);
            float *l = buffer->GetChannel(0) + offset;
            if (buffer->NumChannels() == 2) {
                float *channels[] = {l, buffer->GetChannel(1) + offset};
                dsp_.processReplacing(channels, channels, n);
            } else {
                std::memcpy(monoL_, l, n * sizeof(float));
                std::memcpy(monoR_, l, n * sizeof(float));
                float *channels[] = {monoL_, monoR_};
                dsp_.processReplacing(channels, channels, n);
                for (AkUInt32 i = 0; i < n; ++i)
                    l[i] = .5f * (monoL_[i] + monoR_[i]);
            }
        }
    }
    AKRESULT TimeSkip(AkUInt32) override {
        wasVirtual_ = true;
        return AK_DataReady;
    }
};
