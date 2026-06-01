#pragma once

// Internal shared state between camera_hook.cpp, gui_compensation.cpp,
// and gui_diagnostics.cpp. Not part of the public API.

#include "math_types.h"
#include "camera_hook.h"

namespace RE8HT {

// Byte offset of the world matrix inside an RE Engine via.Transform instance.
constexpr int kTransformWorldMatrixOffset = 0x80;

// Clean camera matrix saved before head tracking is applied each frame.
struct CleanCameraMatrix {
    Matrix4x4f matrix;
    bool valid = false;
};

// Shared per-frame state (defined in camera_hook.cpp)
extern CrosshairProjection g_crosshair;
extern MarkerProjection g_marker;
extern CleanCameraMatrix g_cleanCameraMatrix;

// Resolve the primary camera's transform via SceneManager chain.
void* ResolveCameraTransformInternal();

// via.Camera.get_FOV returns either a float or a double depending on the build.
// Pick whichever decodes to a plausible field of view in degrees, else 0.
inline float DecodeFovDegrees(float asFloat, double asDouble) {
    if (asFloat >= 10.0f && asFloat <= 170.0f) return asFloat;
    float fromDouble = static_cast<float>(asDouble);
    if (fromDouble >= 10.0f && fromDouble <= 170.0f) return fromDouble;
    return 0.0f;
}

} // namespace RE8HT
