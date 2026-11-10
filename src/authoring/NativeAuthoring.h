#pragma once
#include "../CrossfeedConfig.h"
#include <AK/Wwise/Plugin.h>
AK_DECLARE_PLUGIN_CONTAINER(Crossfeed);
template <class T> class NativeAuthoring : public AK::Wwise::Plugin::AudioPlugin {
  public:
    bool GetBankParameters(const GUID &platform, AK::Wwise::Plugin::DataWriter &writer) const override {
        return writer.WriteReal32(this->m_propertySet.GetReal32(platform, "Feed")) &&
               writer.WriteReal32(this->m_propertySet.GetReal32(platform, "GainDb")) &&
               writer.WriteInt8(CrossfeedConfig::BankVersion);
    }
};
