#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Tools/Common/AkAssert.h>
// Authoring API changed in Wwise 2021.1. Tested 2023.1.17/2025.1.8 use
// Plugin.h containers; tested 2019.2.15 uses AuthoringPre2021_1.cpp instead.
// Runtime-only builds must not depend on either Authoring API.
#if !defined(CROSSFEED_RUNTIME_ONLY) && !defined(CROSSFEED_AUTHORING_PRE_2021_1)
#include <AK/Wwise/Plugin.h>
AK_DEFINE_PLUGIN_CONTAINER(Crossfeed);
AK_EXPORT_PLUGIN_CONTAINER(Crossfeed);
#endif
DEFINE_PLUGIN_REGISTER_HOOK;
// SDK 2023.1.17.8841 lacks this macro; SDK 2025.1.8.9170 provides it.
// Keep the feature check so older SDKs compile without the newer assert hook.
#ifdef DEFINE_PLUGIN_ASSERT_HOOK
DEFINE_PLUGIN_ASSERT_HOOK;
#endif
