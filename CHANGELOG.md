# Changelog

All notable changes to this project are documented here.

This project has not had a tagged release yet. Dev builds are published from
the latest commit on the `dev` pre-release; the first versioned release will
be cut from the Unreleased section below.

## [Unreleased]

First build. Head tracking for Resident Evil Village via REFramework, ported
from the Resident Evil Requiem mod (shared RE Engine camera path).

### Added

- Decoupled look/aim head tracking driven by OpenTrack UDP (port 4242)
- 6DOF positional tracking (lean/peek) with per-axis limits
- View-matrix injection in the render phase with a pre/post save-restore
  sandwich so game logic (aim, raycasts, physics) only ever sees the clean
  camera rotation
- World-space (horizon-locked) and camera-local yaw modes
- Game-state detection to suppress tracking in menus, loading, and cutscenes
- Nav-cluster and Ctrl+Shift chord hotkeys (recenter, toggle, position, yaw)
- Crosshair and world-anchored GUI marker compensation
