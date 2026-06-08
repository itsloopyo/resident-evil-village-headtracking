#pragma once

namespace RE8HT {

// Returns true if the player is in active gameplay (not paused, menu, loading, etc.)
bool IsInGameplay();

// Call periodically to refresh cached game state
void RefreshGameState();

// Returns true once after transitioning from non-gameplay to gameplay (for auto-recenter)
bool ShouldRecenter();

// Trigger diagnostic logging for the next few game state checks
void TriggerGameStateDiag();

// Report that a title-screen / main-menu GUI element drew this frame. RE
// Village renders a live 3D backdrop behind the title and main menu that
// passes every gameplay tier (camera present, GlobalSpeed=1, no pause/event/
// transition flags), so the only reliable non-gameplay signal there is that
// GUIMainMenu / GUITitle are actively drawn. Called from the GUI hook.
void NotifyMainMenuDrawn();

} // namespace RE8HT
