#pragma once

namespace cameraunlock::reframework { class GameplayGate; }

namespace RE8HT {

// The gate the camera pipeline consults before writing the camera.
cameraunlock::reframework::GameplayGate* GameplayGateInstance();

// True while the player is in active gameplay (not paused, in a menu, loading,
// or in a cutscene).
bool IsInGameplay();

// GUIMainMenu / GUITitle drew this frame. Called from the GUI draw hook; see
// the note in the .cpp for why this signal is needed.
void NotifyMainMenuDrawn();

} // namespace RE8HT
