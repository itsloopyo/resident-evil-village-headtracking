#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE8HT {

using Config = cameraunlock::reframework::PluginConfig;

// The game's name as cameraunlock-core's data/games.json spells it, written at
// the top of CameraUnlock.ini.
inline constexpr const char* kGameName = "Resident Evil Village";

// RE Village's schema: the F9 DiagnosticMarkerKey that toggles hiding the
// world-anchored markers, and no [Flashlight] section - the beam is not tracked
// on this title. The [Position] Invert keys are still named because the legacy
// import reads them out of an old HeadTracking.ini, applies the InvertX
// correction modId "re8" keys, and reports a changed one as dropped.
//
// canonicalConfig: settings live in reframework\plugins\CameraUnlock.ini, and
// HeadTracking.ini, the file every earlier build read, is imported once while
// CameraUnlock.ini is absent and never written.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE8 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ true,
    /*positionSensitivity*/ 1.0f,
    /*modId*/ "re8",
    /*canonicalConfig*/ true,
};

} // namespace RE8HT
