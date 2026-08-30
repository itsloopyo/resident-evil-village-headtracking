#include "pch.h"
#include "camera_hook.h"
#include "camera_internal.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/time/qpc_clock.h>
#include <cameraunlock/reframework/re_math.h>
#include <reframework/API.hpp>

namespace RE8HT {

namespace ref = cameraunlock::reframework;

// --- Shared per-frame state (extern-declared in camera_internal.h) ---

MarkerProjection g_marker;
CleanCameraMatrix g_cleanCameraMatrix;

// Per-frame flag: set true when OnPreBeginRendering applies head tracking.
static bool g_trackingAppliedThisFrame = false;

// Bumped once per processed render frame. The GUI draw callbacks fire during
// that same frame, so this is the key the focal-length memo invalidates on.
uint64_t g_renderFrame = 0;

// Saved game rotation - what the game INTENDED before we modified it
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

static ref::CameraTransformResolver g_cameraResolver;

ref::CameraTransformResolver& CameraResolver() {
    return g_cameraResolver;
}

// Per-frame caches. Both are invalidated together at the camera-controller
// update pre-hook and at the end of OnPostBeginRendering, so within a single
// render frame they hold the live primary camera / its transform.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = g_cameraResolver.ResolveTransform(&g_cachedCamera);
    return g_cachedTransform;
}

void* CachedCamera() { return g_cachedCamera; }

// --- Core head tracking application ---

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    // Save pre-rotation axes for position offset
    Matrix4x4f preRotationAxes = *worldMat;

    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        ref::ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
    } else {
        ref::ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
    }

    // --- Position (6DOF) ---
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        ref::ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = GetCameraTransformCached();
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        g_saved.gameMatrix = *worldMat;
        g_saved.hasGameMatrix = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        Logger::Instance().Info("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

// --- Camera controller discovery ---

// RE Village's RE Engine game code lives under the app.ropeway.* namespace
// (Village's internal codename is "ropeway"). These are fast-path candidates;
// the hooker's parent-chain walk discovers the real controller dynamically and
// logs the full component tree if none of these match.
static const char* const kControllerTypeCandidates[] = {
    "app.ropeway.camera.PlayerCameraController",
    "app.ropeway.PlayerCameraController",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
};

static ref::CameraControllerHooker g_controllerHooker{
    kControllerTypeCandidates,
    static_cast<int>(std::size(kControllerTypeCandidates)),
    CameraUpdatePreHook,
    CameraUpdatePostHook};

// Minimum gap between repeats of the camera-controller-not-found warning.
constexpr uint64_t kHookWarnIntervalUs = 30ull * 1000000ull;

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(g_cameraResolver.ResolveTransform())) return;

    // Wall-clock, not frame-count: a frame-gated warning writes hundreds of
    // lines an hour on a high-refresh display and buries the startup sequence.
    int attempts = g_controllerHooker.AttemptCount();
    uint64_t now = cameraunlock::time::QpcNowMicros();
    static uint64_t s_lastHookWarnUs = 0;
    if (attempts == 1 || (now - s_lastHookWarnUs) >= kHookWarnIntervalUs) {
        s_lastHookWarnUs = now;
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) - head tracking "
            "still active via the BeginRendering restore path", attempts);
    }
}

// --- Initialization ---

static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    // Camera controller discovery is deferred to gameplay (see
    // OnPreBeginRendering). At the main menu the primary camera GameObject
    // only carries render/post-process controllers; the real player camera
    // controller component is present once gameplay starts.
    DiscoverGUICameraAccess();
    InitGUICompensationMethods();

    Logger::Instance().Info("Methods cached");
    return true;
}

// Marker projection: rotation-only, and deliberately carrying no lean term.
//
// A marker sits at its own depth and parallax is lean/depth, so one screen-space
// translation cannot be right for more than one marker at a time - and the
// compensation below moves every marker in a GUI with a single write. What this
// leaves uncorrected is the markers' own parallax, since the engine projects
// them from the clean eye while the frame is drawn from the leaned one. That
// error is lean/depth, which fades with distance, and markers are mostly
// distant.
static void UpdateMarkerProjection(const Matrix4x4f& head) {
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (ref::ProjectForwardToViewTangents(g_cleanCameraMatrix.matrix, head, rawTanRight, rawTanUp)) {
        float dt = Mod::Instance().GetLastDeltaTime();
        // Internal projection-smoothing constant, deliberately independent of the user's tracking smoothing.
        constexpr float kSmoothing = 0.15f;

        static cameraunlock::math::SmoothedFloat s_tanRight;
        static cameraunlock::math::SmoothedFloat s_tanUp;

        g_marker.tanRight = s_tanRight.Update(rawTanRight, kSmoothing, dt);
        g_marker.tanUp = s_tanUp.Update(rawTanUp, kSmoothing, dt);
        g_marker.valid = true;
    } else {
        g_marker.valid = false;
    }

    // Capped: the 120-frame interval alone streams for the whole session,
    // which buries the startup chain a user is asked to send.
    static int s_projFrame = 0;
    static int s_projFrameLeft = 5;
    if (s_projFrameLeft > 0 && (s_projFrame++ % 120) == 0) {
        s_projFrameLeft--;
        const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;
        Logger::Instance().Info("Marker proj: tanR=%.4f tanU=%.4f valid=%d | "
            "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
            "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
            g_marker.tanRight, g_marker.tanUp, g_marker.valid,
            clean.m[2][0], clean.m[2][1], clean.m[2][2],
            clean.m[3][0], clean.m[3][1], clean.m[3][2],
            head.m[2][0], head.m[2][1], head.m[2][2],
            head.m[3][0], head.m[3][1], head.m[3][2]);
    }
}

// --- Public API ---

void OnPreBeginRendering() {
    // Before every gate below: the first-packet latch has to survive
    // AutoEnable=false, a menu, and a failed function cache, because those are
    // exactly the states a "no head tracking" report is trying to tell apart.
    Mod::Instance().LogFirstTrackerPose();

    // Drain hotkey requests on the render thread (same thread as the GUI draw
    // callback) so mode-cycle / marker-hide never mutate
    // render-owned state concurrently with the hotkey poller thread.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();
    ++g_renderFrame;

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, crosshair projection, GUI
    // marker compensation) reads cached values, so the rendered camera and
    // the smoother see the same wall-clock dt.
    Mod::Instance().TickFrame();

    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);

    // Save the clean matrix
    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    UpdateMarkerProjection(*worldMat);
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    // OnPreBeginRendering populated the per-frame transform cache this frame
    // (g_trackingAppliedThisFrame is only set after that succeeded), so reuse
    // it rather than re-walking the SceneManager -> ... -> Transform chain.
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        // Restore the clean camera in full - POSITION as well as rotation.
        //
        // Keeping the head-tracked translation row left the game aiming off a
        // leaned eye: the shot converges on the leaned eye's axis while the
        // round leaves the un-leaned body, so reticle and impact agree at
        // exactly one range and splay apart either side of it, swapping sides
        // as the player walks through it. Head tracking must not move where
        // bullets go.
        //
        // The lean still renders. Rotation is written and taken back at the
        // same two hooks and rotation is what the player sees, so the camera
        // matrix the renderer consumes is snapshotted between them; the
        // translation row is in that same matrix.
        *worldMat = g_cleanCameraMatrix.matrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace RE8HT
