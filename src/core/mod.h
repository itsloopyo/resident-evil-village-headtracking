#pragma once

#include "config.h"
#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>
#include <cstdio>
#include <string>

namespace RE8HT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleTrackingMode();
    void ToggleYawMode();
    void PlaceDiagnosticMarker();
    void ToggleMarkersHidden();

    // Hotkey callbacks fire on the HotkeyPoller's background thread. Recenter,
    // CycleTrackingMode and ToggleMarkersHidden all touch state owned by the
    // render thread (non-atomic processor/interpolator smoothing state and the
    // GUI element-dumper's unordered_set). Mutating that set concurrently with
    // the on_pre_gui_draw_element callback that inserts into it is a genuine
    // heap-corruption race. The hotkey thread only requests the action here;
    // ProcessDeferredActions() runs it on the render thread.
    void RequestRecenter() { m_recenterRequested.Request(); }
    void RequestCycleTrackingMode() { m_cycleModeRequested.Request(); }
    void RequestToggleMarkersHidden() { m_toggleMarkersRequested.Request(); }
    void ProcessDeferredActions();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Caches the smoothed rotation and position so every in-frame consumer
    // (camera matrix, crosshair projection, GUI marker compensation) reads
    // an identical value. Without this, per-element GUI calls would each
    // re-tick the pipeline with a fragmented dt, leaving the rendered
    // camera advancing on a partial-frame dt while position smoothing sees
    // an even smaller one.
    void TickFrame();

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);
    bool IsPositionEnabled() const { return m_session.IsPositionActive(); }
    bool IsRotationEnabled() const { return m_session.IsRotationActive(); }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }
    float GetLastDeltaTime() const { return m_lastDeltaTime; }
    bool AreMarkersHidden() const { return m_markersHidden.load(); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    void InitDiagnosticLog();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_markersHidden{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};
    std::atomic<bool> m_worldSpaceYaw{false};

    // Deferred hotkey-action requests, consumed on the render thread by
    // ProcessDeferredActions(). See the Request* methods above.
    cameraunlock::input::DeferredAction m_recenterRequested;
    cameraunlock::input::DeferredAction m_cycleModeRequested;
    cameraunlock::input::DeferredAction m_toggleMarkersRequested;

    uint64_t m_lastFrameTickTime = 0;
    float m_lastDeltaTime = 0.016f;

    // Diagnostic logging
    std::string m_pluginDir;
    FILE* m_diagFile = nullptr;
    uint64_t m_diagStartTime = 0;
    bool m_diagMarkerPending = false;
    int m_diagMarkerCount = 0;
};

} // namespace RE8HT
