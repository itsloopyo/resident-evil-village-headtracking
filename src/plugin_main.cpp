#include "pch.h"

#include <reframework/API.hpp>

#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"
#include "camera/gui_diagnostics.h"
#include "core/config.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/plugin_bootstrap.h>

namespace ref = cameraunlock::reframework;

namespace {

// RE Village's RE Engine game code lives under the app.ropeway.* namespace
// (Village's internal codename is "ropeway"). These are fast-path candidates;
// the hooker's parent-chain walk discovers the real controller dynamically and
// logs the full component tree if none of these match.
const char* const kControllerTypeCandidates[] = {
    "app.ropeway.camera.PlayerCameraController",
    "app.ropeway.PlayerCameraController",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
};

// RE8 compensates world-anchored markers only - it has no reticle to place - so
// the pipeline computes the rotation-only tangents and skips the aim
// projection entirely.
const ref::PluginBootstrapDescriptor kPlugin = [] {
    ref::PluginBootstrapDescriptor d;
    d.logTag = "RE8HT";
    d.mod.displayName = RE8HT::RE8HT_PLUGIN_NAME;
    d.mod.version = RE8HT::RE8HT_VERSION;
    d.mod.config = RE8HT::kConfigSchema;
    d.camera.controllerCandidateTypes = kControllerTypeCandidates;
    d.camera.controllerCandidateCount =
        static_cast<int>(std::size(kControllerTypeCandidates));
    d.camera.gate = RE8HT::GameplayGateInstance();
    d.camera.onInit = []() {
        RE8HT::DiscoverGUICameraAccess();
        ref::InitGuiMethods();
    };
    d.camera.onFrameStart = &RE8HT::ProcessDeferredActions;
    d.preGuiDrawElement = &RE8HT::OnPreGuiDrawElement;
    d.registerExtraHotkeys = [](cameraunlock::input::HotkeyPoller& poller,
                                const ref::PluginConfig& config) {
        // F9: toggle hiding of the world-anchored GUI markers. The GUI draw
        // callback returns false for marker elements while the flag is set.
        // Full marker info is dumped to the log on first sight regardless.
        poller.AddHotkey(config.diagnosticMarkerKey, []() {
            RE8HT::RequestToggleMarkersHidden();
        });
    };
    return d;
}();

} // namespace

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;
    return ref::InitializePlugin(param, kPlugin);
}
