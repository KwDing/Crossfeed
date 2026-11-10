#include "dsp/CrossfeedDSP.h"
#include "runtime/NativeEffect.h"
struct CrossfeedTraits {
    using DSP = CrossfeedDSP;
    static constexpr int Count = 1;
    inline static constexpr float Defaults[] = {.316227766f};
    inline static constexpr const char *Names[] = {"Feed"};
};
AK::IAkPlugin *CreateCrossfeedEffect(AK::IAkPluginMemAlloc *a) {
    return AK_PLUGIN_NEW(a, NativeEffect<CrossfeedTraits>());
}
AK::IAkPluginParam *CreateCrossfeedEffectParams(AK::IAkPluginMemAlloc *a) {
    return AK_PLUGIN_NEW(a, NativeParams<CrossfeedTraits>());
}
AK_IMPLEMENT_PLUGIN_FACTORY(CrossfeedEffect, AkPluginTypeEffect, 64, 4106)
#if !defined(CROSSFEED_RUNTIME_ONLY) && !defined(CROSSFEED_AUTHORING_PRE_2021_1)
#include "authoring/NativeAuthoring.h"
class AuthorCrossfeed : public NativeAuthoring<CrossfeedTraits> {};
AK_ADD_PLUGIN_CLASS_TO_CONTAINER(Crossfeed, AuthorCrossfeed, CrossfeedEffect);
#endif
