#include "oracle_adapter.h"

#include "pch.h"
#include "core/config.h"

namespace oracle_api {

namespace {

Config Copy(const re8ht_oracle::Config& c) {
    Config out;
    out.udpPort = c.udpPort;
    out.yawMultiplier = c.yawMultiplier;
    out.pitchMultiplier = c.pitchMultiplier;
    out.rollMultiplier = c.rollMultiplier;
    out.localSmoothing = c.localSmoothing;
    out.remoteSmoothing = c.remoteSmoothing;
    out.toggleKey = c.toggleKey;
    out.positionToggleKey = c.positionToggleKey;
    out.yawModeKey = c.yawModeKey;
    out.diagnosticMarkerKey = c.diagnosticMarkerKey;
    out.positionSensitivityX = c.positionSensitivityX;
    out.positionSensitivityY = c.positionSensitivityY;
    out.positionSensitivityZ = c.positionSensitivityZ;
    out.positionLimitX = c.positionLimitX;
    out.positionLimitY = c.positionLimitY;
    out.positionLimitZ = c.positionLimitZ;
    out.positionLimitZBack = c.positionLimitZBack;
    out.positionInvertX = c.positionInvertX;
    out.positionInvertY = c.positionInvertY;
    out.positionInvertZ = c.positionInvertZ;
    out.positionEnabled = c.positionEnabled;
    out.autoEnable = c.autoEnable;
    out.worldSpaceYaw = c.worldSpaceYaw;
    return out;
}

}  // namespace

Config Defaults() {
    return Copy(re8ht_oracle::Config{});
}

// The published build's Mod::LoadConfig (src/core/mod.cpp at 56c327d), less its logging.
LoadStatus LoadOrCreate(const char* iniPath, Config& out) {
    re8ht_oracle::Config c;
    LoadStatus status = LoadStatus::Read;
    if (!c.Load(iniPath)) {
        c.SetDefaults();
        c.Save(iniPath);
        status = LoadStatus::Created;
    }
    out = Copy(c);
    return status;
}

}  // namespace oracle_api
