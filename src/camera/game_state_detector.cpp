#include "pch.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/manager_probe_checks.h>

namespace RE8HT {

namespace ref = cameraunlock::reframework;

// RE Village's managers do not answer to the fully-qualified names RE2 and RE3
// use, so the gate probes for them by shape instead.
//
// That leaves the title and main-menu screens, which render over a live 3D
// backdrop and so satisfy every probe. The one unambiguous signal there is that
// GUIMainMenu or GUITitle drew, which gui_compensation.cpp reports.
static ref::GameplayGate g_gate{&ref::DiscoverManagerProbes, &ref::ManagerProbeGameplayCheck};

ref::GameplayGate* GameplayGateInstance() { return &g_gate; }

bool IsInGameplay() { return g_gate.IsInGameplay(); }

void NotifyMainMenuDrawn() { g_gate.NotifyMenuDrawn(); }

} // namespace RE8HT
