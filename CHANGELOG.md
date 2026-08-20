# Changelog

All notable changes to this project are documented here.

This project has not had a tagged release yet. Dev builds are published from
the latest commit on the `dev` pre-release; the first versioned release will
be cut from the Unreleased section below.

## [Unreleased]

First build. Head tracking for Resident Evil Village via REFramework, ported
from the Resident Evil Requiem mod (shared RE Engine camera path).

### Logging

- Removed `HeadTracking_diag.csv`. It was written every frame with a flush per row, about 20 MB an hour of disk traffic on the render thread, always on and documented nowhere. `F9` hides and shows the world-anchored GUI markers; it never placed a marker in the log, and the unreachable code that claimed to has been removed.
- Capped the two per-frame crosshair/marker projection traces at five lines each per session. They ran every 120 frames for the whole session, about 770 KB an hour at 60 fps into REFramework's log.
- The log now names the config file it actually read (`Config loaded from <path>`), so an edit made to the wrong `HeadTracking.ini` is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Corrected the log path in the docs. It is `<game>/re2_framework_log.txt`, not `reframework/reframework_log.txt`; REFramework uses that generic name for every RE Engine title.

### Changed
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, and the mod's own centre. Your tracker owns the
  centre now. Set it there, with OpenTrack's Center bind, the CENTER button in
  a phone app, or your headset's own centring, and the mod applies what the
  tracker sends.
  Two centres in series was the problem: when the view was off you could not
  tell which side was wrong, and switching trackers meant centring in both.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `HeadTracking.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.
- The shipped `HeadTracking.ini` now matches the mod's built-in defaults: position sensitivity 1.0 on all three axes (was 2.0) and `InvertX=true` (was `false`). The file had drifted from the code, so a fresh install got double the documented position range and an uninverted lateral axis.

### Added

- Decoupled look/aim head tracking driven by OpenTrack UDP (port 4242)
- 6DOF positional tracking (lean/peek) with per-axis limits
- View-matrix injection in the render phase with a pre/post save-restore
  sandwich so game logic (aim, raycasts, physics) only ever sees the clean
  camera rotation
- World-space (horizon-locked) and camera-local yaw modes
- Game-state detection to suppress tracking in menus, loading, and cutscenes
- Nav-cluster and Ctrl+Shift chord hotkeys (toggle, position, yaw)
- Crosshair and world-anchored GUI marker compensation
