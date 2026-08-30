#include "pch.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "camera_internal.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>

#include <reframework/API.hpp>
#include <cstring>

namespace RE8HT {

namespace ref = cameraunlock::reframework;

using ref::CachedGetter;
using ref::InvokeMethodWithArg;

static struct {
    // Shift a GUI element's root View by a screen-space pixel offset.
    reframework::API::Method* transformSetPosition = nullptr;
    // via.Camera.get_ProjectionMatrix - exact per-axis focal lengths, avoiding
    // the FOV-convention / square-pixel guess the FOV fallback has to make.
    reframework::API::Method* getProjectionMatrix = nullptr;
} g_guiMethods;

void InitGUICompensationMethods() {
    g_guiMethods.transformSetPosition = ref::FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);

    auto tdb = reframework::API::get()->tdb();
    auto camType = tdb ? tdb->find_type("via.Camera") : nullptr;
    g_guiMethods.getProjectionMatrix = camType ? camType->find_method("get_ProjectionMatrix") : nullptr;

    Logger::Instance().Info("GUI compensation methods: setPos=%p projMat=%p",
        (void*)g_guiMethods.transformSetPosition, (void*)g_guiMethods.getProjectionMatrix);
}

// Pixel focal lengths for marker compensation. Prefer the camera's projection
// matrix (P00/P11 give the exact horizontal and vertical scale directly), and
// fall back to deriving them from the vertical FOV only when the projection
// matrix is unavailable. The FOV fallback assumes a 16:9 square-pixel canvas,
// which mis-scales the horizontal (yaw) axis relative to the vertical whenever
// the real projection differs (ultrawide, non-16:9, or a differing FOV axis
// convention) - the projection-matrix path has no such assumption.
static bool ComputeMarkerFocalLengths(float& fx, float& fy) {
    constexpr float kHalfW = 960.f;
    constexpr float kHalfH = 540.f;

    void* cam = CachedCamera();
    if (!cam) cam = CameraResolver().ResolveCamera();
    if (!cam) return false;

    if (g_guiMethods.getProjectionMatrix) {
        auto ret = g_guiMethods.getProjectionMatrix->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(cam), ref::EmptyArgs());
        if (!ret.exception_thrown) {
            // Matrix4x4 (64 bytes) returned by value in ret.bytes, row-major.
            auto* m = reinterpret_cast<const float*>(ret.bytes.data());
            if (cameraunlock::rendering::FocalLengthsFromProjection(m[0], m[5], kHalfW, kHalfH, fx, fy)) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    Logger::Instance().Info("Marker focal (projection): P00=%.4f P11=%.4f fx=%.1f fy=%.1f",
                        m[0], m[5], fx, fy);
                }
                // Square pixels: horizontal and vertical pixel focal lengths must
                // match. RE8's matrix reports them equal, but the RE3 build proved
                // this projection path can return P00 at half its true value
                // (fx ends up half of fy), which under-compensates yaw and drifts
                // the markers horizontally. fy (vertical) is the trusted value;
                // enforce fx = fy so a divergent matrix can never slip through.
                fx = fy;
                return true;
            }
        }
    }

    float fov = CameraResolver().ResolveFovDegrees(cam);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(fov, kHalfW, kHalfH, fx, fy);
}

// Per-frame memo over ComputeMarkerFocalLengths. The camera's projection is
// fixed for a rendered frame, but the draw callback fires once per compensated
// element, and each call would otherwise re-resolve the camera and pull its
// projection matrix across the managed VM for an identical answer.
static bool GetMarkerFocalLengthsCached(float& fx, float& fy) {
    static uint64_t s_frame = static_cast<uint64_t>(-1);
    static bool s_ok = false;
    static float s_fx = 0.f;
    static float s_fy = 0.f;

    if (s_frame != g_renderFrame) {
        s_frame = g_renderFrame;
        s_ok = ComputeMarkerFocalLengths(s_fx, s_fy);
    }
    if (!s_ok) return false;
    fx = s_fx;
    fy = s_fy;
    return true;
}

// RE8's world-anchored HUD markers. RE Village's HUD GameObjects are named
// per-purpose (unlike RE9/Requiem's numeric `Gui_ui20xx` scheme), so these are
// matched by exact name. GUIInteractIcon / GUIInteractFarIcon are the
// interaction prompts that float over world objects; GUIGuide is the objective
// guidance marker. All anchor to world points and so drift across the screen as
// the head rotates unless compensated.
static bool IsWorldMarker(const char* goName) {
    return strcmp(goName, "GUIInteractIcon") == 0
        || strcmp(goName, "GUIInteractFarIcon") == 0
        || strcmp(goName, "GUIGuide") == 0;
}

// --- Marker compensation ---
//
// OnPostBeginRendering restores the clean camera in full, so at GUI draw time
// the game projects world anchors from the clean eye while the frame was drawn
// from the head-rotated one. g_marker is the screen-space tangent shift of the
// view forward direction under that rotation, with no position contribution
// (ProjectForwardToViewTangents). Converting it to a pixel offset and shifting
// the element's root View glues the marker back onto its world target.
//
// Lean parallax is deliberately not added: it is lean/depth, this is one write
// for every marker in the GUI, and no single value is right for more than one
// depth. See UpdateMarkerProjection.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.transformSetPosition) return;
    if (!g_marker.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerFocalLengthsCached(fx, fy)) return;

    float deltaX = -g_marker.tanRight * fx;
    float deltaY =  g_marker.tanUp * fy;

    static CachedGetter s_getView("get_View");
    auto viewRet = s_getView.Invoke(guiMo);
    if (viewRet.exception_thrown || !viewRet.ptr) return;
    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);

    float pos[3] = { deltaX, deltaY, 0.f };
    InvokeMethodWithArg(g_guiMethods.transformSetPosition, view, (void*)&pos[0]);

    // Capped: the 120-frame interval alone streams for the whole
    // session, which buries the startup chain a user is asked to send.
    static int s_markerDiagFrame = 0;
    static int s_markerDiagFrameLeft = 5;
    if (s_markerDiagFrameLeft > 0 && (s_markerDiagFrame++ % 120) == 0) {
        s_markerDiagFrameLeft--;
        Logger::Instance().Info("Marker comp: fx=%.1f fy=%.1f tanR=%.4f tanU=%.4f delta=(%.1f,%.1f)",
            fx, fy, g_marker.tanRight, g_marker.tanUp, deltaX, deltaY);
    }
}

// --- Main dispatcher ---

void ResetGuiElementDumper() {
    ResetGuiDiagnostics();
}

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

    // Resolve the GameObject name
    char goName[128] = "?";
    reframework::API::ManagedObject* goMo = nullptr;
    static CachedGetter s_getGameObject("get_GameObject");
    static CachedGetter s_getName("get_Name");
    auto goRet = s_getGameObject.Invoke(mo);
    if (!goRet.exception_thrown && goRet.ptr) {
        goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
        auto nameRet = s_getName.Invoke(goMo);
        if (!nameRet.exception_thrown && nameRet.ptr) {
            ref::ReadManagedString(nameRet.ptr, goName, sizeof(goName));
        }
    }

    // Diagnostic scans
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
        if (Mod::Instance().AreMarkersHidden()) {
            return false;
        }
        ApplyMarkerCompensation(mo);
    }

    return true;
}

} // namespace RE8HT
