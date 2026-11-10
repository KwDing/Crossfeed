// 2019.2 export/serialization unit test, not a Wwise Authoring host test.
#include "../src/authoring/AuthoringPre2021_1.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
static void check(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}
struct Properties final : AK::Wwise::IPluginPropertySet {
    GUID id{};
    float feed = .316227766f, gain = CrossfeedConfig::DefaultGainDb;
    bool missing = false, missingGain = false, wrongType = false, unexpectedRead = false;
    bool GetValue(const GUID &, LPCWSTR name, VARIANT &value) override {
        const bool isFeed = std::wcscmp(name, L"Feed") == 0, isGain = std::wcscmp(name, L"GainDb") == 0;
        if (!isFeed && !isGain) {
            unexpectedRead = true;
            return false;
        }
        if (missing || (isGain && missingGain))
            return false;
        value.vt = wrongType ? VT_R8 : VT_R4;
        value.fltVal = isFeed ? feed : gain;
        return true;
    }
    bool SetValue(const GUID &, LPCWSTR, const VARIANT &) override { return false; }
    bool PropertyHasRTPC(LPCWSTR) override { return false; }
    GUID GetCurrentPlatform() override { return id; }
    BasePlatformID GetCurrentBasePlatform() override { return BasePlatformID::Empty(); }
    BasePlatformID GetDefaultNativeAuthoringPlaybackPlatform() override {
        return BasePlatformID::Empty();
    }
    GUID GetAuthoringPlaybackPlatform() override { return id; }
    void NotifyInternalDataChanged(AkPluginParamID, bool) override {}
    bool CanLogUndos() override { return false; }
    AK::Wwise::IUndoManager *GetUndoManager() override { return nullptr; }
    void GetLicenseStatus(const GUID &, AK::Wwise::LicenseType &type, AK::Wwise::LicenseStatus &status,
                          UINT32 &days) override {
        type = AK::Wwise::LicenseType_Purchased;
        status = AK::Wwise::LicenseStatus_Valid;
        days = 0;
    }
    void GetAssetLicenseStatus(const GUID &p, AkUInt32, AK::Wwise::LicenseType &type,
                               AK::Wwise::LicenseStatus &status, UINT32 &days) override {
        GetLicenseStatus(p, type, status, days);
    }
    const GUID &GetID() const override { return id; }
    void WaapiCall(const char *, const char *, const char *, AK::IAkPluginMemAlloc *, char *&result,
                   char *&error) const override {
        result = nullptr;
        error = nullptr;
    }
};
struct Writer final : AK::Wwise::IWriteData {
    std::vector<unsigned char> bytes;
    bool fail = false;
    bool WriteData(LPCVOID, UINT32, UINT32 &) override { return false; }
    bool WriteUtf16String(const wchar_t *) override { return false; }
    bool WriteUtf8String(const char *) override { return false; }
    bool WriteBool(bool) override { return false; }
    bool WriteByte(BYTE value) override {
        if (fail)
            return false;
        bytes.push_back(value);
        return true;
    }
    bool WriteInt16(UINT16) override { return false; }
    bool WriteInt32(UINT32) override { return false; }
    bool WriteInt64(UINT64) override { return false; }
    bool WriteReal32(float value) override {
        if (fail)
            return false;
        const auto *first = reinterpret_cast<const unsigned char *>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(value));
        return true;
    }
    bool WriteReal64(double) override { return false; }
};
static void checkSerialization() {
    // Valid host registration/playback is deliberately not simulated here.
    Properties properties;
    Writer writer;
    CrossfeedAuthoringPre2021_1 author;
    check(!author.GetBankParameters(properties.id, &writer), "missing property set rejected");
    author.SetPluginPropertySet(&properties);
    check(!author.GetBankParameters(properties.id, nullptr), "null writer rejected");
    properties.feed = 0.5f;
    properties.gain = -4.f;
    check(author.GetBankParameters(properties.id, &writer), "serialize parameters");
    // Windows little-endian: Feed 0.5, Gain -4 dB, format version 1.
    const std::vector<unsigned char> expected{0, 0, 0, 0x3f, 0, 0, 0x80, 0xc0, 1};
    check(writer.bytes == expected, "exact nine-byte Bank layout");
    check(!properties.unexpectedRead, "only declared properties are queried");
    writer.bytes.clear();
    properties.missingGain = true;
    check(!author.GetBankParameters(properties.id, &writer) && writer.bytes.empty(),
          "missing gain writes nothing");
    properties.missingGain = false;
    writer.bytes.clear();
    properties.missing = true;
    check(!author.GetBankParameters(properties.id, &writer) && writer.bytes.empty(),
          "failed property read writes nothing");
    properties.missing = false;
    properties.wrongType = true;
    check(!author.GetBankParameters(properties.id, &writer) && writer.bytes.empty(),
          "wrong VARIANT type rejected");
    properties.wrongType = false;
    properties.feed = std::numeric_limits<float>::quiet_NaN();
    check(!author.GetBankParameters(properties.id, &writer) && writer.bytes.empty(),
          "invalid Feed writes nothing");
    properties.feed = .316227766f;
    writer.fail = true;
    check(!author.GetBankParameters(properties.id, &writer), "writer failure propagated");
    auto owned = new CrossfeedAuthoringPre2021_1();
    owned->Destroy();
}
int main(int argc, char **argv) {
    check(argc == 2, "Authoring DLL required");
    HMODULE dll = LoadLibraryA(argv[1]);
    check(dll != nullptr, "Authoring DLL loads outside host");
    using Factory = AK::Wwise::IPluginBase *(__stdcall *)(unsigned short, unsigned short);
    auto factory = reinterpret_cast<Factory>(GetProcAddress(dll, "AkCreatePlugin"));
    check(factory != nullptr, "2019 AkCreatePlugin export");
    check(factory(63, 4106) == nullptr && factory(64, 4105) == nullptr,
          "factory rejects unrelated identity");
    checkSerialization();
    FreeLibrary(dll);
    std::puts("{\"status\":\"passed\",\"pre2021_1_exports_and_serialization\":true,\"authoring_host\":"
              "\"not_run\"}");
}
