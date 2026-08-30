#pragma once

// Internal shared state between camera_hook.cpp, gui_compensation.cpp,
// and gui_diagnostics.cpp. Not part of the public API.

#include "math_types.h"
#include "camera_hook.h"

#include <cameraunlock/reframework/camera_chain.h>

#include <cstdint>

namespace RE8HT {

// Clean camera matrix saved before head tracking is applied each frame.
struct CleanCameraMatrix {
    Matrix4x4f matrix;
    bool valid = false;
};

// Shared per-frame state (defined in camera_hook.cpp)
extern MarkerProjection g_marker;
extern CleanCameraMatrix g_cleanCameraMatrix;
extern uint64_t g_renderFrame;

// Shared resolver for the primary camera chain (transform, camera, live FOV).
cameraunlock::reframework::CameraTransformResolver& CameraResolver();

// The primary camera resolved for this render frame, or nullptr before the
// frame's first resolve. Reusing it keeps the GUI draw callback off the
// SceneManager chain walk.
void* CachedCamera();

} // namespace RE8HT
