#pragma once

namespace RE8HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback — restores clean matrix
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Crosshair projection state (shared per-frame state defined in
// camera_hook.cpp; read by GUI compensation via camera_internal.h).
struct CrosshairProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float fovDegrees = 75.0f;
    float rollDegrees = 0.0f;
    bool valid = false;
};

// Marker projection — same shape as the crosshair projection but computed at
// a smaller assumed depth so translation parallax has the right magnitude
// for typical world-anchored UI markers (interaction prompts, objective
// icons). Rotation is depth-independent so the angular shift matches the
// crosshair; only the position-offset contribution differs.
struct MarkerProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    bool valid = false;
};

} // namespace RE8HT
