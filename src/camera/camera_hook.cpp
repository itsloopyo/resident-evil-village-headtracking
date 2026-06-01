#include "pch.h"
#include "camera_hook.h"
#include "camera_internal.h"
#include "gui_compensation.h"
#include "gui_diagnostics.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/re_math.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <reframework/API.hpp>

#include "value_smoother.h"

#include <string>

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

// Method cache for the camera chain
static struct {
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getCameraFov = nullptr;
    bool initialized = false;
    bool failed = false;
} g_fn;

// Per-frame caches. Both are invalidated together at the camera-controller
// update pre-hook and at the end of OnPostBeginRendering, so within a single
// render frame they hold the live primary camera / its transform.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

// Walk via.SceneManager -> MainView -> PrimaryCamera -> GameObject -> Transform.
// When outCamera is non-null it also yields the intermediate PrimaryCamera, so
// callers that need both (FOV reads) avoid re-walking the first two links.
static void* ResolveCameraTransform(void** outCamera = nullptr) {
    const auto& api = reframework::API::get();
    auto sm = api->get_native_singleton("via.SceneManager");
    if (!sm) return nullptr;
    auto mv = ref::CallMethod(g_fn.getMainView, sm);
    if (!mv) return nullptr;
    auto cam = ref::CallMethod(g_fn.getPrimaryCamera, mv);
    if (!cam) return nullptr;
    if (outCamera) *outCamera = cam;
    auto go = ref::CallMethod(g_fn.getGameObject, cam);
    if (!go) return nullptr;
    return ref::CallMethod(g_fn.getTransform, go);
}

// Exposed for gui_diagnostics via camera_internal.h
void* ResolveCameraTransformInternal() {
    return ResolveCameraTransform();
}

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = ResolveCameraTransform(&g_cachedCamera);
    return g_cachedTransform;
}

// --- Core head tracking application ---

// Left-multiply the rotation part (upper-left 3x3) of worldMat's columns by a
// 3x3 rotation matrix: worldMat[0..2][c] = rot * worldMat[0..2][c].
static void ApplyRotation3x3(Matrix4x4f* worldMat, const float rot[3][3]) {
    for (int c = 0; c < 3; c++) {
        float c0 = worldMat->m[0][c];
        float c1 = worldMat->m[1][c];
        float c2 = worldMat->m[2][c];
        worldMat->m[0][c] = rot[0][0]*c0 + rot[0][1]*c1 + rot[0][2]*c2;
        worldMat->m[1][c] = rot[1][0]*c0 + rot[1][1]*c1 + rot[1][2]*c2;
        worldMat->m[2][c] = rot[2][0]*c0 + rot[2][1]*c1 + rot[2][2]*c2;
    }
}

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    // Save pre-rotation axes for position offset
    Matrix4x4f preRotationAxes = *worldMat;

    // --- Rotation ---
    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        // Horizon-locked yaw: M'' = R_pitchroll * M * R_yaw
        float cy = cosf(yr), sy = -sinf(yr);
        for (int r = 0; r < 3; r++) {
            float x = worldMat->m[r][0];
            float z = worldMat->m[r][2];
            worldMat->m[r][0] = x * cy - z * sy;
            worldMat->m[r][2] = x * sy + z * cy;
        }

        float hp = pr * 0.5f, hr = rr * 0.5f;
        REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
        REQuat qz = {0, 0, sinf(hr), cosf(hr)};
        REQuat qPR = QuatNorm(QuatMul(qx, qz));
        float prRot[3][3];
        QuatToMatrix3x3(qPR, prRot);

        ApplyRotation3x3(worldMat, prRot);
    } else {
        // Camera-local: all axes relative to camera orientation
        float hy = yr * 0.5f, hp = pr * 0.5f, hr = rr * 0.5f;
        REQuat qy = {0, sinf(hy), 0, cosf(hy)};
        REQuat qx = {sinf(hp), 0, 0, cosf(hp)};
        REQuat qz = {0, 0, sinf(hr), cosf(hr)};
        REQuat q = QuatNorm(QuatMul(QuatMul(qy, qx), qz));

        float headRot[3][3];
        QuatToMatrix3x3(q, headRot);

        ApplyRotation3x3(worldMat, headRot);
    }

    // --- Position (6DOF) ---
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        px = -px;
        const Matrix4x4f& gm = preRotationAxes;
        worldMat->m[3][0] += px * gm.m[0][0] + py * gm.m[1][0] + pz * gm.m[2][0];
        worldMat->m[3][1] += px * gm.m[0][1] + py * gm.m[1][1] + pz * gm.m[2][1];
        worldMat->m[3][2] += px * gm.m[0][2] + py * gm.m[1][2] + pz * gm.m[2][2];
    }
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + kTransformWorldMatrixOffset);
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + kTransformWorldMatrixOffset);
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

// Render/post-process controllers share the "Camera*Controller" /
// "*Controller" shape but are not the gameplay camera controller. Hooking
// one (e.g. app.CameraDOFController) for the save/restore sandwich would put
// the capture at the wrong point in the frame. Reject them so the walk keeps
// looking for the real player camera controller.
static bool IsEffectController(const char* cnm) {
    static const char* kEffectMarkers[] = {
        "DOF", "Fog", "ToneMap", "Bloom", "SSAO", "SSR", "ColorCorrect",
        "LensDistortion", "RenderResolutio", "Shake", "Vignette", "MotionBlur",
        "DepthOfField", "PostProcess", "Effect", "Exposure", "Vibration",
    };
    for (auto m : kEffectMarkers) {
        if (strstr(cnm, m)) return true;
    }
    return false;
}

static bool TryHookCameraController() {
    const auto& api = reframework::API::get();
    auto tdb = api->tdb();

    // RE Village's RE Engine game code lives under the app.ropeway.* namespace
    // (Village's internal codename is "ropeway"). These are fast-path
    // candidates; the parent-chain walk below discovers the real controller
    // dynamically and logs the full component tree if none of these match.
    static const char* controllerTypes[] = {
        "app.ropeway.camera.PlayerCameraController",
        "app.ropeway.PlayerCameraController",
        "app.PlayerCameraController",
        "app.camera.PlayerCameraController",
    };

    static const char* methodNames[] = {
        "onCameraUpdate",
        "lateUpdate",
        "update",
        "doUpdate",
        "lateUpdateImpl",
    };

    for (auto typeName : controllerTypes) {
        auto type = tdb->find_type(typeName);
        if (!type) continue;
        for (auto methodName : methodNames) {
            auto method = type->find_method(methodName);
            if (!method) continue;
            auto id = method->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
            Logger::Instance().Info("Hooked %s.%s (id=%u)", typeName, methodName, id);
            return true;
        }
    }

    // Walk parent chain to discover camera controller dynamically
    Logger::Instance().Info("Camera controller: hardcoded names failed, walking parent chain...");

    void* camTransform = ResolveCameraTransform();
    if (camTransform) {
        auto txMo = reinterpret_cast<reframework::API::ManagedObject*>(camTransform);
        for (int depth = 0; depth < 8; depth++) {
            auto goRet = txMo->invoke("get_GameObject", ref::EmptyArgs());
            if (goRet.exception_thrown || !goRet.ptr) break;
            auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);

            char goName[128] = "?";
            auto nameRet = goMo->invoke("get_Name", ref::EmptyArgs());
            if (!nameRet.exception_thrown && nameRet.ptr) {
                ref::ReadManagedString(nameRet.ptr, goName, sizeof(goName));
            }

            auto compsRet = goMo->invoke("get_Components", ref::EmptyArgs());
            if (compsRet.exception_thrown || !compsRet.ptr) {
                Logger::Instance().Info("  parent[%d] GO=\"%s\": no components", depth, goName);
            } else {
                auto compArr = reinterpret_cast<reframework::API::ManagedObject*>(compsRet.ptr);
                auto lenRet = compArr->invoke("get_Length", ref::EmptyArgs());
                uint32_t compCount = lenRet.exception_thrown ? 0 : lenRet.dword;
                Logger::Instance().Info("  parent[%d] GO=\"%s\": %u components", depth, goName, compCount);

                for (uint32_t i = 0; i < compCount && i < 32; i++) {
                    auto comp = ref::ArrayGetValue(compArr, (int)i);
                    if (!comp) continue;
                    auto compTd = comp->get_type_definition();
                    if (!compTd) continue;
                    const char* cns = compTd->get_namespace();
                    const char* cnm = compTd->get_name();
                    if (!cns) cns = "";
                    if (!cnm) cnm = "?";
                    Logger::Instance().Info("    [%u] %s.%s", i, cns, cnm);

                    // Accept a player camera controller (preferred) or a generic
                    // "Camera*Controller", but never a render/post-process effect
                    // controller (DOF, fog, bloom, ...) which shares the shape.
                    bool isCameraController = cnm
                        && ((strstr(cnm, "Camera") && strstr(cnm, "Controller"))
                            || (strstr(cnm, "Player") && strstr(cnm, "Camera")))
                        && !IsEffectController(cnm);
                    if (isCameraController) {
                        char fullName[256];
                        snprintf(fullName, sizeof(fullName), "%s.%s", cns, cnm);
                        Logger::Instance().Info("  -> Candidate camera controller: %s", fullName);

                        auto candidateType = tdb->find_type(fullName);
                        if (candidateType) {
                            for (auto mn : methodNames) {
                                auto m = candidateType->find_method(mn);
                                if (!m) continue;
                                auto id = m->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
                                Logger::Instance().Info("  -> Hooked %s.%s (id=%u)", fullName, mn, id);
                                return true;
                            }
                        }
                    }
                }
            }

            auto parentRet = txMo->invoke("get_Parent", ref::EmptyArgs());
            if (parentRet.exception_thrown || !parentRet.ptr) break;
            txMo = reinterpret_cast<reframework::API::ManagedObject*>(parentRet.ptr);
        }
    }

    return false;
}

// --- Initialization ---

static bool InitCachedFunctions() {
    if (g_fn.initialized) return !g_fn.failed;
    g_fn.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");

    if (!smType || !svType || !camType || !goType) { g_fn.failed = true; return false; }

    g_fn.getMainView = smType->find_method("get_MainView");
    g_fn.getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    g_fn.getGameObject = camType->find_method("get_GameObject");
    g_fn.getTransform = goType->find_method("get_Transform");
    g_fn.getCameraFov = camType->find_method("get_FOV");

    if (!g_fn.getMainView || !g_fn.getPrimaryCamera || !g_fn.getGameObject || !g_fn.getTransform) {
        g_fn.failed = true;
        return false;
    }
    if (!g_fn.getCameraFov) {
        Logger::Instance().Error("via.Camera.get_FOV method not found — GUI marker compensation will be disabled");
    }

    // Camera controller discovery is deferred to gameplay (see
    // OnPreBeginRendering). At the main menu the primary camera GameObject
    // only carries render/post-process controllers; the real player camera
    // controller component is present once gameplay starts.
    DiscoverGUICameraAccess();
    InitGUICompensationMethods();

    Logger::Instance().Info("Methods cached");
    return true;
}

// Run camera-controller discovery once we are in gameplay, retrying each
// frame until it succeeds. Deferring past the menu avoids latching onto a
// render effect controller before the gameplay camera rig exists.
static void EnsureCameraControllerHooked() {
    static bool s_hooked = false;
    static int s_attempts = 0;
    if (s_hooked) return;
    if (TryHookCameraController()) {
        s_hooked = true;
        return;
    }
    if (++s_attempts == 1 || (s_attempts % 300) == 0) {
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) — head tracking "
            "still active via the BeginRendering restore path", s_attempts);
    }
}

// Read the live primary-camera FOV (degrees) via the cached method chain.
// Returns 0 if the chain is unavailable or the value is implausible.
static float ReadLivePrimaryCameraFov() {
    if (!g_fn.getCameraFov) return 0.0f;

    // The per-frame transform resolve already produced the primary camera;
    // reuse it instead of re-walking SceneManager -> MainView -> PrimaryCamera.
    // Fall back to a fresh walk if the cache is somehow empty.
    void* cam = g_cachedCamera;
    if (!cam) {
        auto sm = reframework::API::get()->get_native_singleton("via.SceneManager");
        if (!sm || !g_fn.getMainView || !g_fn.getPrimaryCamera) return 0.0f;
        auto mv = g_fn.getMainView->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(sm), ref::EmptyArgs());
        if (mv.exception_thrown || !mv.ptr) return 0.0f;
        auto camRet = g_fn.getPrimaryCamera->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(mv.ptr), ref::EmptyArgs());
        if (camRet.exception_thrown || !camRet.ptr) return 0.0f;
        cam = camRet.ptr;
    }
    auto fov = g_fn.getCameraFov->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(cam), ref::EmptyArgs());
    if (fov.exception_thrown) return 0.0f;
    return DecodeFovDegrees(fov.f, fov.d);
}

// Crosshair projection: where the clean aim point appears on the head-tracked
// screen. Smoothed to eliminate jitter from perspective-division noise and
// per-frame FOV fluctuations. Reads the clean matrix from g_cleanCameraMatrix.
static void UpdateCrosshairProjection(const Matrix4x4f& head) {
    const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;

    constexpr float kAimDist = 50.0f;
    float aimPtX = clean.m[3][0] + kAimDist * clean.m[2][0];
    float aimPtY = clean.m[3][1] + kAimDist * clean.m[2][1];
    float aimPtZ = clean.m[3][2] + kAimDist * clean.m[2][2];

    float dx = aimPtX - head.m[3][0];
    float dy = aimPtY - head.m[3][1];
    float dz = aimPtZ - head.m[3][2];

    float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

    if (vz > 1e-4f) {
        float rawTanRight = vx / vz;
        float rawTanUp = vy / vz;

        // Read FOV from live camera; keep the last good value if unavailable.
        float rawFov = g_crosshair.fovDegrees;
        float liveFov = ReadLivePrimaryCameraFov();
        if (liveFov > 10.f) rawFov = liveFov;

        // Smooth screen-space projection values to eliminate jitter from
        // perspective division noise and per-frame FOV fluctuations.
        // Uses the same baseline smoothing factor as the rotation pipeline.
        float dt = Mod::Instance().GetLastDeltaTime();
        constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
        float t = cameraunlock::math::CalculateSmoothingFactor(kCrosshairSmoothing, dt);

        static ExponentialSmoother s_tanRight;
        static ExponentialSmoother s_tanUp;
        static ExponentialSmoother s_fov;

        g_crosshair.tanRight = s_tanRight.Update(rawTanRight, t);
        g_crosshair.tanUp = s_tanUp.Update(rawTanUp, t);
        g_crosshair.fovDegrees = s_fov.Update(rawFov, t);
        g_crosshair.valid = g_crosshair.fovDegrees > 10.f;

        float roll = 0.f, yaw = 0.f, pitch = 0.f;
        Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
        g_crosshair.rollDegrees = roll;
    } else {
        g_crosshair.valid = false;
    }

    static int s_projFrame = 0;
    if ((s_projFrame++ % 120) == 0) {
        Logger::Instance().Info("Crosshair proj: tanR=%.4f tanU=%.4f vz=%.2f fov=%.1f valid=%d | "
            "clean fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) | "
            "head fwd=(%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f)",
            g_crosshair.tanRight, g_crosshair.tanUp, vz, g_crosshair.fovDegrees, g_crosshair.valid,
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
//
// To isolate the rotation contribution, project a forward-pointing
// vector (no head-position offset) through the head-rotated basis.
// Translation drops out cleanly: when the head hasn't rotated, vx/vy
// collapse to zero regardless of how far the head has translated.
static void UpdateMarkerProjection(const Matrix4x4f& head) {
    constexpr float kRotationDepth = 50.0f;  // arbitrary — depth cancels for rotation-only
    const Matrix4x4f& clean = g_cleanCameraMatrix.matrix;

    float dx = kRotationDepth * clean.m[2][0];
    float dy = kRotationDepth * clean.m[2][1];
    float dz = kRotationDepth * clean.m[2][2];

    float vx = dx * head.m[0][0] + dy * head.m[0][1] + dz * head.m[0][2];
    float vy = dx * head.m[1][0] + dy * head.m[1][1] + dz * head.m[1][2];
    float vz = dx * head.m[2][0] + dy * head.m[2][1] + dz * head.m[2][2];

    if (vz > 1e-4f) {
        float rawTanRight = vx / vz;
        float rawTanUp = vy / vz;

        float dt = Mod::Instance().GetLastDeltaTime();
        constexpr float kSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
        float t = cameraunlock::math::CalculateSmoothingFactor(kSmoothing, dt);

        static ExponentialSmoother s_tanRight;
        static ExponentialSmoother s_tanUp;

        g_marker.tanRight = s_tanRight.Update(rawTanRight, t);
        g_marker.tanUp = s_tanUp.Update(rawTanUp, t);
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

    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + kTransformWorldMatrixOffset);

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
    void* transform = nullptr;
    __try { transform = GetCameraTransformCached(); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + kTransformWorldMatrixOffset);
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
