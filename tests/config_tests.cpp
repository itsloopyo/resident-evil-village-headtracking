// CameraUnlock.ini against the table: the committed HeadTracking.ini is the table's fresh render
// byte for byte, the owner creates exactly those bytes, and each toggle's save changes the line of
// its own row and no other byte. `--render-config <path>` writes the fresh render to <path>
// instead and runs nothing else (pixi run render-config).
//
// Every owner here reads and creates a scratch Defaults.ini (DefaultsFile::At), never the
// player's own.

#include "config_owner_options.h"

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/reframework/plugin_config_table.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfg = cameraunlock::config;
using RE8HT::Config;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read a test file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write a test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string FreshRender() {
    return cfg::RenderCanonicalFresh(cameraunlock::reframework::PluginConfigTable(RE8HT::kConfigSchema),
                                     RE8HT::testing::Header());
}

std::wstring ScratchRoot() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp) + L"re8ht-config-tests-" + std::to_wstring(GetCurrentProcessId());
}

// A fresh plugin folder in %TEMP%, with Defaults.ini in a folder of its own.
struct Scratch {
    std::wstring folder;
    std::wstring defaults;
};

Scratch MakeScratch(const wchar_t* name) {
    const std::wstring root = ScratchRoot();
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring dir = root + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    const std::wstring global = root + L"\\" + name + L"-global";
    if (!CreateDirectoryW(global.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    return {dir, global + L"\\Defaults.ini"};
}

cfg::ConfigOwnerOptions<Config> Options(const Scratch& s) {
    return RE8HT::testing::OwnerOptions(s.folder, cfg::DefaultsFile::At(s.defaults));
}

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        if (end == std::string::npos) {
            lines.push_back(bytes.substr(start));
            break;
        }
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

void CommittedFileIsTheFreshRender() {
    std::printf("HeadTracking.ini, the committed file, is the table's fresh render\n");
    Check(ReadBytes(RE8HT_COMMITTED_CONFIG) == FreshRender(), "run pixi run render-config after changing a row");
}

void TheFileCarriesNoPoseShapingOrReticle() {
    std::printf("the file has no sensitivity, inversion or reticle setting\n");
    const std::string fresh = FreshRender();
    for (const char* banned : {"Sensitivity", "YawMultiplier", "PitchMultiplier", "RollMultiplier", "Invert", "Reticle", "Deadzone"}) {
        Check(fresh.find(banned) == std::string::npos, banned);
    }
}

// The file names no shaping, so what the plugin applies is SetDefaults' on every load: rotation
// 1:1, the lean at 1.0 and no axis inverted, on a fresh file and on one written back after a
// toggle. RE Village's InvertX correction is the import's: the table never inverts.
void TheShapingIsIdentity() {
    std::printf("the shaping is identity with no row naming it\n");
    const Config defaults = cameraunlock::reframework::PluginConfigTable(RE8HT::kConfigSchema).defaults();
    Check(defaults.positionSensitivityX == 1.0f && defaults.positionSensitivityY == 1.0f &&
              defaults.positionSensitivityZ == 1.0f,
          "position sensitivity is 1.0 on every axis");
    Check(defaults.yawMultiplier == 1.0f && defaults.pitchMultiplier == 1.0f && defaults.rollMultiplier == 1.0f,
          "rotation is 1:1");
    Check(!defaults.positionInvertX && !defaults.positionInvertY && !defaults.positionInvertZ, "no axis is inverted");

    const Scratch s = MakeScratch(L"shaping");
    {
        cfg::ConfigOwner<Config> owner(Options(s));
        owner.Load();
        owner.Save([](Config& c) { c.worldSpaceYaw = false; });
    }
    cfg::ConfigOwner<Config> owner(Options(s));
    const Config loaded = owner.Load().config;
    Check(loaded.positionSensitivityX == 1.0f && loaded.positionSensitivityY == 1.0f &&
              loaded.positionSensitivityZ == 1.0f && !loaded.positionInvertX,
          "a saved file still loads with identity shaping");
}

void EveryHotkeyDefaultIsTheFleets() {
    std::printf("every hotkey list the table defaults to parses, and is the fleet's default\n");
    const Config defaults = cameraunlock::reframework::PluginConfigTable(RE8HT::kConfigSchema).defaults();
    for (const std::string* list :
         {&defaults.toggleKeyBindings, &defaults.cycleTrackingModeKeyBindings, &defaults.yawModeKeyBindings}) {
        Check(cameraunlock::input::ParseKeyBindings(*list).ok(), list->c_str());
    }
    Check(defaults.toggleKeyBindings == "End, Ctrl+Shift+Y", "ToggleKey is End, Ctrl+Shift+Y");
    Check(defaults.cycleTrackingModeKeyBindings == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey is PageUp, Ctrl+Shift+G");
    Check(defaults.yawModeKeyBindings == "PageDown, Ctrl+Shift+H", "YawModeKey is PageDown, Ctrl+Shift+H");
    Check(cameraunlock::input::ParseKeyBindings(defaults.diagnosticMarkerKeyBindings).ok(), "DiagnosticMarkerKey parses");
    Check(defaults.diagnosticMarkerKeyBindings == "F9", "DiagnosticMarkerKey is F9, as every build shipped it");
    Check(defaults.worldSpaceYaw, "WorldSpaceYaw defaults to true");
}

void FirstLaunchCreatesTheCommittedFile() {
    std::printf("the first launch with no file creates the committed bytes\n");
    const Scratch s = MakeScratch(L"created");
    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "the load is Created");
    Check(ReadBytes(s.folder + L"\\" + RE8HT::testing::kConfigFileName) == FreshRender(),
          "the created file is the fresh render");
    Check(!Exists(s.folder + L"\\" + RE8HT::testing::kLegacyFileName), "no HeadTracking.ini is written");
    Check(Exists(s.defaults), "Defaults.ini is created where none was");
}

void TogglesSaveTheirRowsOnly() {
    std::printf("each toggle's save writes its own row and no other byte, and End never saves\n");
    const Scratch s = MakeScratch(L"saves");
    const std::wstring path = s.folder + L"\\" + RE8HT::testing::kConfigFileName;
    {
        cfg::ConfigOwner<Config> owner(Options(s));
        owner.Load();
    }
    const std::string fresh = ReadBytes(path);
    const std::string defaultsBefore = ReadBytes(s.defaults);

    // The changes PluginMod::ToggleYawMode and PluginMod::RequestCycleTrackingMode save.
    cfg::ConfigOwner<Config> owner(Options(s));
    owner.Load();
    const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.worldSpaceYaw = false; });
    Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw save is Saved");
    Check(!yaw.log.empty(), "the log says WorldSpaceYaw no longer follows Defaults.ini");
    const std::string afterYaw = ReadBytes(path);
    const std::vector<std::string> yawLines = ChangedLines(fresh, afterYaw);
    Check(yawLines.size() == 1 && yawLines[0] == "WorldSpaceYaw=false", "only WorldSpaceYaw=default became false");

    const cfg::ConfigSaveResult mode = owner.Save([](Config& c) { c.positionEnabled = false; });
    Check(mode.status == cfg::ConfigSaveStatus::Saved, "the mode save is Saved");
    const std::vector<std::string> modeLines = ChangedLines(afterYaw, ReadBytes(path));
    Check(modeLines.size() == 1 && modeLines[0] == "PositionEnabled=false",
          "a mode change writes PositionEnabled and nothing else");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.autoEnable = false; });
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is not Writable: End never persists");

    Check(ReadBytes(s.defaults) == defaultsBefore, "no save changes Defaults.ini");

    cfg::ConfigOwner<Config> again(Options(s));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    Check(reread.status == cfg::ConfigLoadStatus::Canonical, "the saved file reads back as canonical");
    Check(!reread.config.worldSpaceYaw, "the yaw choice survives a restart");
    Check(!reread.config.positionEnabled, "the tracking mode survives a restart");
    Check(reread.config.autoEnable, "EnableOnStartup is still the default");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            const std::string path = argv[2];
            WriteBytes(std::wstring(path.begin(), path.end()), FreshRender());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }

        std::printf("RE8HeadTracking config tests\n============================\n");
        CommittedFileIsTheFreshRender();
        TheFileCarriesNoPoseShapingOrReticle();
        TheShapingIsIdentity();
        EveryHotkeyDefaultIsTheFleets();
        FirstLaunchCreatesTheCommittedFile();
        TogglesSaveTheirRowsOnly();
        std::filesystem::remove_all(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
