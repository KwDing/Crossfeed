#include "AuthoringPre2021_1.h"
#include <mutex>

// Pre-2021.1 compatibility: SDK 2019.2.15 expects the AkCreatePlugin export and
// explicit engine-factory registration, rather than the 2023/2025 container API.
// Register outside DllMain. The SDK helper retains one engine-module reference
// for the host process; call_once avoids another reference per effect instance.
extern "C"
    __declspec(dllexport) AK::Wwise::IPluginBase *__stdcall AkCreatePlugin(unsigned short company,
                                                                           unsigned short plugin) {
    if (company != 64 || plugin != 4106)
        return nullptr;
    static std::once_flag once;
    static AKRESULT registered = AK_Fail;
    std::call_once(once, [] { registered = AK::Wwise::RegisterWwisePlugin(); });
    return registered == AK_Success ? new CrossfeedAuthoringPre2021_1() : nullptr;
}
