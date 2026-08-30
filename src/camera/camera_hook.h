#pragma once

namespace RE8HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback - restores the clean
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Rotation-only tangents for world-anchored GUI markers (interaction prompts,
// objective icons). They carry no lean term - see UpdateMarkerProjection in
// camera_hook.cpp.
struct MarkerProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    bool valid = false;
};

} // namespace RE8HT
