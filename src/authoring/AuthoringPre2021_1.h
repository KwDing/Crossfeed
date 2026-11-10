// Pre-2021.1 Authoring compatibility bridge, tested with SDK 2019.2.15.7667.
// That SDK lacks AK/Wwise/Plugin.h; it uses AudioPlugin.h, VARIANT properties,
// IWriteData and explicit Destroy instead. No custom MFC dialog is provided.
#pragma once
#include "../CrossfeedConfig.h"
#include <windows.h>
#include <atlstr.h>
#include <oleauto.h>
#include <AK/Wwise/AudioPlugin.h>
#include <cmath>

class CrossfeedAuthoringPre2021_1 final : public AK::Wwise::DefaultAudioPluginImplementation {
    AK::Wwise::IPluginPropertySet *properties_ = nullptr; // Borrowed; Wwise owns it.
  public:
    void Destroy() override { delete this; }
    void SetPluginPropertySet(AK::Wwise::IPluginPropertySet *properties) override {
        properties_ = properties;
    }
    bool GetBankParameters(const GUID &platform, AK::Wwise::IWriteData *writer) const override {
        if (!properties_ || !writer)
            return false;
        float values[2]{};
        const wchar_t *names[] = {L"Feed", L"GainDb"};
        for (int i = 0; i < 2; ++i) {
            VARIANT value{};
            const bool valid = properties_->GetValue(platform, names[i], value) && value.vt == VT_R4 &&
                               std::isfinite(value.fltVal);
            if (valid)
                values[i] = value.fltVal;
            VariantClear(&value);
            if (!valid)
                return false;
        }
        return writer->WriteReal32(values[0]) && writer->WriteReal32(values[1]) &&
               writer->WriteByte(CrossfeedConfig::BankVersion);
    }
};
