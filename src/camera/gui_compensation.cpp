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
    // via.Camera.get_ProjectionMatrix — exact per-axis focal lengths, avoiding
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
// convention) — the projection-matrix path has no such assumption.
static bool ComputeMarkerFocalLengths(float& fx, float& fy) {
    constexpr float kHalfW = 960.f;
    constexpr float kHalfH = 540.f;

    void* cam = CameraResolver().ResolveCamera();
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
                return true;
            }
        }
    }

    float fov = CameraResolver().ResolveFovDegrees(cam);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(fov, kHalfW, kHalfH, fx, fy);
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
// OnPostBeginRendering restores clean rotation but keeps the head-tracked
// position, so at GUI draw time the game's projection matrix is
// (clean rotation, head position). A world-anchored marker projected through
// that matrix already tracks head translation (lean parallax) for free; only
// the rotation needs compensating, because the rotation was reset to clean.
// g_marker carries exactly that: the screen-space tangent shift of the view
// forward direction under head rotation, with no position contribution
// (ProjectForwardToViewTangents). Converting it to a pixel offset and shifting
// the element's root View glues the marker back onto its world target.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiMethods.transformSetPosition) return;
    if (!g_marker.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ComputeMarkerFocalLengths(fx, fy)) return;

    float deltaX = -g_marker.tanRight * fx;
    float deltaY =  g_marker.tanUp * fy;

    auto viewRet = guiMo->invoke("get_View", ref::EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return;
    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);

    float pos[3] = { deltaX, deltaY, 0.f };
    InvokeMethodWithArg(g_guiMethods.transformSetPosition, view, (void*)&pos[0]);

    static int s_markerDiagFrame = 0;
    if ((s_markerDiagFrame++ % 120) == 0) {
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
