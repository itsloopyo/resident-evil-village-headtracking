#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE8HT {

using Config = cameraunlock::reframework::PluginConfig;

// RE Village's INI schema: the [Position] Invert keys, plus the F9
// DiagnosticMarkerKey that toggles hiding the world-anchored markers. No
// [Flashlight] section - the beam is not tracked on this title.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE8 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ true,
    /*positionSensitivity*/ 1.0f,
    /*modId*/ "re8",
};

} // namespace RE8HT
