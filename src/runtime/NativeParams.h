#pragma once
#include "../CrossfeedConfig.h"
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
template <class T> class NativeParams final : public AK::IAkPluginParam {
  public:
    float values[T::Count]{};
    float gainDb = CrossfeedConfig::DefaultGainDb;
    NativeParams() {
        for (int i = 0; i < T::Count; ++i)
            values[i] = T::Defaults[i];
    }
    AK::IAkPluginParam *Clone(AK::IAkPluginMemAlloc *a) override {
        return AK_PLUGIN_NEW(a, NativeParams(*this));
    }
    AKRESULT Init(AK::IAkPluginMemAlloc *, const void *block, AkUInt32 size) override {
        return size ? SetParamsBlock(block, size) : AK_Success;
    }
    AKRESULT Term(AK::IAkPluginMemAlloc *a) override {
        AK_PLUGIN_DELETE(a, this);
        return AK_Success;
    }
    AKRESULT SetParamsBlock(const void *block, AkUInt32 size) override {
        if (!block || size != CrossfeedConfig::BankBytes)
            return AK_InvalidParameter;
        const auto* bytes = static_cast<const unsigned char*>(block);
        if (bytes[size - 1] != CrossfeedConfig::BankVersion)
            return AK_InvalidParameter;
        float fields[2];
        std::memcpy(fields, bytes, sizeof(fields));
        if (!std::isfinite(fields[0]) || !std::isfinite(fields[1]))
            return AK_InvalidParameter;
        values[0] = std::clamp(fields[0], 0.f, 1.f);
        gainDb = std::clamp(fields[1], CrossfeedConfig::MinGainDb, CrossfeedConfig::MaxGainDb);
        return AK_Success;
    }
    AKRESULT SetParam(AkPluginParamID id, const void *value, AkUInt32 size) override {
        if ((id >= T::Count && id != T::Count + 1) || !value || size != sizeof(float))
            return AK_InvalidParameter;
        float f;
        std::memcpy(&f, value, sizeof(f));
        if (!std::isfinite(f))
            return AK_InvalidParameter;
        if (id == T::Count + 1) {
            gainDb = std::clamp(f, CrossfeedConfig::MinGainDb, CrossfeedConfig::MaxGainDb);
            return AK_Success;
        }
        values[id] = std::clamp(f, 0.f, 1.f);
        return AK_Success;
    }
};
