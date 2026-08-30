#include "pch.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"

#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>
#include <cstring>

namespace RE8HT {

namespace ref = cameraunlock::reframework;

// --- F9 marker-hide toggle ---

static std::atomic<bool> g_markersHidden{false};
static cameraunlock::input::DeferredAction g_toggleMarkersRequested;

void RequestToggleMarkersHidden() { g_toggleMarkersRequested.Request(); }

bool AreMarkersHidden() { return g_markersHidden.load(); }

void ProcessDeferredActions() {
    if (!g_toggleMarkersRequested.Consume()) return;
    bool now = !g_markersHidden.load();
    g_markersHidden.store(now);
    ref::LogInfo("World-anchored GUI markers: %s", now ? "HIDDEN" : "VISIBLE");
    // Re-arm the element dumper so the next few frames capture fresh state
    // (e.g. Visible=true while actually looking at an interactable).
    ResetGuiDiagnostics();
}

// --- Marker compensation ---

// RE8's world-anchored HUD markers. RE Village's HUD GameObjects are named
// per-purpose (unlike Requiem's numeric `Gui_ui20xx` scheme), so these are
// matched by exact name. GUIInteractIcon / GUIInteractFarIcon are the
// interaction prompts that float over world objects; GUIGuide is the objective
// guidance marker. All anchor to world points and so drift across the screen as
// the head rotates unless compensated.
static bool IsWorldMarker(const char* goName) {
    return strcmp(goName, "GUIInteractIcon") == 0
        || strcmp(goName, "GUIInteractFarIcon") == 0
        || strcmp(goName, "GUIGuide") == 0;
}

// GUIInteractIcon is the close-up prompt; the engine swaps to GUIInteractFarIcon
// beyond its range. Only the close form gets the lean term below.
static bool IsNearPromptMarker(const char* goName) {
    return strcmp(goName, "GUIInteractIcon") == 0;
}

// Assumed distance to the world object a close-up interaction prompt sits on.
//
// The prompt's screen shift under a lean is lean/depth, so the term needs a
// depth and nothing here can measure one. What makes a constant safe rather than
// a guess is the residual: correcting with an assumed depth d_a leaves
// f*lean*(1/d_true - 1/d_a), which is smaller than the f*lean/d_true the mod
// leaves today for every d_a > d_true/2. So the constant belongs at the top of
// the range the element appears over, and only an element the game itself splits
// into a near and a far form has a top to its range. GUIInteractFarIcon and
// GUIGuide do not, and stay rotation-only, which is the d_a -> infinity case and
// is never worse than doing nothing.
constexpr float kNearPromptDepthMeters = 3.0f;

// The post-render callback restores the clean camera in full, position row
// included, so at GUI draw time the game projects world anchors from the
// un-leaned eye while the frame was drawn from the leaned one. Two things
// differ, and the marker needs both put back.
//
// Rotation is FrameProjection's marker tangents: the screen shift of the view
// forward direction under the head rotation, depth-independent.
//
// Translation is lean/depth. For a near prompt it is worth the assumed depth
// above - a 0.30 m lateral lean displaces a 3 m anchor by fy*0.30/3, about 70 px
// on the reference canvas - so that branch projects the whole anchor ray, which
// carries the rotation with it. Anything without a bounded depth takes the
// rotation-only tangents.
static bool ComputeMarkerDelta(bool nearPrompt, float fx, float fy,
                               float& deltaX, float& deltaY) {
    const auto& projection = ref::GetFrameProjection();

    if (!nearPrompt) {
        if (!projection.markerValid) return false;
        deltaX = -projection.markerTanRight * fx;
        deltaY =  projection.markerTanUp * fy;
        return true;
    }

    if (!projection.cleanToHeadValid) return false;

    // The prompt is drawn over what the player is looking at, so its clean-view
    // ray is taken as the view forward: (0, 0, depth) in clean-camera axes.
    const float* lean = projection.cleanLocalPositionDelta;
    float rawX = 0.f, rawY = 0.f;
    if (!ref::ProjectCleanRayToHeadGui(projection.cleanToHead, 0.f,
                                       -lean[0], -lean[1], kNearPromptDepthMeters - lean[2],
                                       fx, fy, rawX, rawY)) {
        return false;
    }

    static cameraunlock::math::SmoothedFloat s_deltaX;
    static cameraunlock::math::SmoothedFloat s_deltaY;
    const float dt = ref::PluginMod::Instance().GetLastDeltaTime();
    deltaX = s_deltaX.Update(rawX, ref::kProjectionSmoothing, dt);
    deltaY = s_deltaY.Update(rawY, ref::kProjectionSmoothing, dt);
    return true;
}

static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo, bool nearPrompt) {
    if (!ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    float deltaX = 0.f, deltaY = 0.f;
    if (!ComputeMarkerDelta(nearPrompt, fx, fy, deltaX, deltaY)) return;

    if (!ref::ShiftElementView(guiMo, deltaX, deltaY)) return;

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send. The lean
    // triple is the only place the head's translation reaches the log, and it is
    // the quantity the near-prompt branch turns on.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        const auto& projection = ref::GetFrameProjection();
        ref::LogInfo("Marker comp: near=%d fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f "
            "lean=(%.3f,%.3f,%.3f) delta=(%.1f,%.1f)",
            nearPrompt ? 1 : 0, fx, fy, projection.markerTanRight, projection.markerTanUp,
            projection.cleanLocalPositionDelta[0], projection.cleanLocalPositionDelta[1],
            projection.cleanLocalPositionDelta[2], deltaX, deltaY);
    }
}

// --- Main dispatcher ---

bool OnPreGuiDrawElement(void* element, void* context) {
    if (!element) return true;

    TryDumpContext(context);
    TryDumpMatrixDiagnostic();

    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(element);
    auto td = mo->get_type_definition();
    if (!td) return true;
    const char* tns = td->get_namespace();
    const char* tnm = td->get_name();
    if (!tnm) return true;

    char goName[128] = "?";
    reframework::API::ManagedObject* goMo = nullptr;
    {
        static reframework::API::Method* s_getGameObject = nullptr;
        auto goRet = ref::InvokeCached(mo, s_getGameObject, "get_GameObject", ref::EmptyArgs());
        if (!goRet.exception_thrown && goRet.ptr) {
            goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
        }
    }
    ref::ReadGuiElementName(mo, goName, sizeof(goName));

    ScanGuiGoName(goName, tns, tnm);
    TryDumpGuiElement(mo, td, goName, goMo);

    // Title / main-menu suppression signal: these screens render over a live 3D
    // backdrop that otherwise passes every gameplay tier, so their presence is
    // the only reliable "not gameplay" marker here.
    if (strcmp(goName, "GUIMainMenu") == 0 || strcmp(goName, "GUITitle") == 0) {
        NotifyMainMenuDrawn();
    }

    if (IsWorldMarker(goName)) {
        // HIDE GATE: when the F9 marker toggle is on, skip drawing the
        // world-anchored markers entirely (returning false skips the element),
        // leaving the crosshair, ammo, and every other HUD element visible.
        if (AreMarkersHidden()) {
            return false;
        }
        ApplyMarkerCompensation(mo, IsNearPromptMarker(goName));
    }

    return true;
}

} // namespace RE8HT
