#include "pch.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"

#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>

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

// The post-render callback restores the clean camera in full, so at GUI draw
// time the game projects world anchors from the clean eye while the frame was
// drawn from the head-rotated one. FrameProjection's marker tangents are the
// screen-space shift of the view forward direction under that rotation, with no
// position contribution. Converting them to a pixel offset and shifting the
// element's root View glues the marker back onto its world target.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    const auto& projection = ref::GetFrameProjection();
    if (!projection.markerValid || !ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    float deltaX = -projection.markerTanRight * fx;
    float deltaY =  projection.markerTanUp * fy;

    if (!ref::ShiftElementView(guiMo, deltaX, deltaY)) return;

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        ref::LogInfo("Marker comp: fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f delta=(%.1f,%.1f)",
            fx, fy, projection.markerTanRight, projection.markerTanUp, deltaX, deltaY);
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
        ApplyMarkerCompensation(mo);
    }

    return true;
}

} // namespace RE8HT
