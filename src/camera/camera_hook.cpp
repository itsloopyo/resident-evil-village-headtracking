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
#include <cameraunlock/reframework/re_math.h>
#include <reframework/API.hpp>

namespace RE8HT {

namespace ref = cameraunlock::reframework;

// --- Shared per-frame state (extern-declared in camera_internal.h) ---

CrosshairProjection g_crosshair;
MarkerProjection g_marker;
CleanCameraMatrix g_cleanCameraMatrix;

// Per-frame flag: set true when OnPreBeginRendering applies head tracking.
static bool g_trackingAppliedThisFrame = false;

// Saved game rotation — what the game INTENDED before we modified it
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

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(g_cameraResolver.ResolveTransform())) return;

    int attempts = g_controllerHooker.AttemptCount();
    if (attempts == 1 || (attempts % 300) == 0) {
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) — head tracking "
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

// Crosshair projection: where the clean aim point appears on the head-tracked
// screen. Smoothed to eliminate jitter from perspective-division noise and
// per-frame FOV fluctuations. Reads the clean matrix from g_cleanCameraMatrix.
static void UpdateCrosshairProjection(const Matrix4x4f& head) {
    const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;

    constexpr float kAimDist = 50.0f;
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (ref::ProjectAimToViewTangents(clean, head, kAimDist, rawTanRight, rawTanUp)) {
        // Read FOV from the live camera resolved this frame; hold the previous
        // value when the read fails.
        float rawFov = g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
        if (rawFov <= 0.f) rawFov = g_crosshair.fovDegrees;

        float dt = Mod::Instance().GetLastDeltaTime();
        constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);

        static cameraunlock::math::SmoothedFloat s_tanRight;
        static cameraunlock::math::SmoothedFloat s_tanUp;
        static cameraunlock::math::SmoothedFloat s_fov;

        g_crosshair.tanRight = s_tanRight.Update(rawTanRight, kCrosshairSmoothing, dt);
        g_crosshair.tanUp = s_tanUp.Update(rawTanUp, kCrosshairSmoothing, dt);
        g_crosshair.fovDegrees = s_fov.Update(rawFov, kCrosshairSmoothing, dt);
        g_crosshair.valid = g_crosshair.fovDegrees > 10.f;

        float roll = 0.f, yaw = 0.f, pitch = 0.f;
        Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
        g_crosshair.rollDegrees = roll;
    } else {
        g_crosshair.valid = false;
    }

    static int s_projFrame = 0;
    if ((s_projFrame++ % 120) == 0) {
        Logger::Instance().Info("Crosshair proj: tanR=%.4f tanU=%.4f fov=%.1f valid=%d | "
            "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
            "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
            g_crosshair.tanRight, g_crosshair.tanUp, g_crosshair.fovDegrees, g_crosshair.valid,
            clean.m[2][0], clean.m[2][1], clean.m[2][2],
            clean.m[3][0], clean.m[3][1], clean.m[3][2],
            head.m[2][0], head.m[2][1], head.m[2][2],
            head.m[3][0], head.m[3][1], head.m[3][2]);
    }
}

// Marker projection: rotation-only. OnPostBeginRendering restores clean
// rotation but keeps the head-tracked position, so at GUI draw time the
// game's projection matrix is (clean rotation, head position). Anything
// the GUI projects through that matrix gets translation parallax for
// free — leaning shifts the world anchor's screen position the same way
// it shifts the rendered scene, so the marker tracks the target without
// any help from us. Only rotation needs to be compensated manually
// (because the rotation was reset to clean).
static void UpdateMarkerProjection(const Matrix4x4f& head) {
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (ref::ProjectForwardToViewTangents(g_cleanCameraMatrix.matrix, head, rawTanRight, rawTanUp)) {
        float dt = Mod::Instance().GetLastDeltaTime();
        constexpr float kSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);

        static cameraunlock::math::SmoothedFloat s_tanRight;
        static cameraunlock::math::SmoothedFloat s_tanUp;

        g_marker.tanRight = s_tanRight.Update(rawTanRight, kSmoothing, dt);
        g_marker.tanUp = s_tanUp.Update(rawTanUp, kSmoothing, dt);
        g_marker.valid = true;
    } else {
        g_marker.valid = false;
    }
}

// --- Public API ---

void OnPreBeginRendering() {
    // Drain hotkey requests on the render thread (same thread as the GUI draw
    // callback) so Recenter / mode-cycle / marker-hide never mutate
    // render-owned state concurrently with the hotkey poller thread.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();
    if (ShouldRecenter()) {
        Mod::Instance().Recenter();
    }

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

    UpdateCrosshairProjection(*worldMat);
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
        // Restore clean ROTATION but keep head-tracked POSITION.
        Matrix4x4f restored = g_cleanCameraMatrix.matrix;
        restored.m[3][0] = worldMat->m[3][0];
        restored.m[3][1] = worldMat->m[3][1];
        restored.m[3][2] = worldMat->m[3][2];
        *worldMat = restored;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace RE8HT
