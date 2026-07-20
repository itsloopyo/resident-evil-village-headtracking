> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

# Resident Evil Village Head Tracking

An unofficial flatscreen head tracking mod for Resident Evil Village that decouples where you look from where you aim, letting you glance around with a webcam, phone, or any OpenTrack-compatible tracker while your mouse keeps control of the crosshair, no VR headset required.

<!-- ![Mod GIF](https://raw.githubusercontent.com/itsloopyo/resident-evil-village-headtracking/main/assets/readme-clip.gif) -->

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Resident Evil Village](https://store.steampowered.com/app/1196590/Resident_Evil_Village/) (Steam)
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking app (smartphone, webcam, or dedicated hardware)
- Windows 10/11 (64-bit)

## Installation

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/resident-evil-village-headtracking/releases)
2. Extract it anywhere
3. Double-click `install.cmd` (it finds your game and installs REFramework if needed)
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`
5. Launch the game

The installer finds your game via Steam registry lookup. If it can't find the game:
- Set the `RE8_PATH` environment variable to your game folder, or
- Pass the path as an argument: `install.cmd "D:\Games\Resident Evil Village"`

### Manual Installation

If you prefer to place files by hand (or use the Nexus ZIP, which contains only the `reframework/plugins/` subtree):

1. Install [REFramework](https://github.com/praydog/REFramework-nightly/releases) for RE Village (extract to the game root so `dinput8.dll` sits next to the game EXE)
2. Copy `RE8HeadTracking.dll` into `<game>/reframework/plugins/`

The mod writes its config file alongside the DLL on first launch.

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases)
2. Configure your tracker as input
3. Set output to **UDP over network**
4. Host: `127.0.0.1`, Port: `4242`
5. Start tracking before launching the game

### VR Headset Setup

If you own a VR headset, its motion sensors make an excellent tracker even though the game itself stays flatscreen.

1. Connect the headset to the PC (Quest: Air Link or Virtual Desktop; PCVR headsets connect directly)
2. Launch SteamVR so the headset is tracked
3. In OpenTrack, set the input to **SteamVR**
4. Set output to **UDP over network** (`127.0.0.1:4242`)
5. Start tracking before launching the game

### Webcam Setup

No special hardware needed. OpenTrack's built-in **neuralnet tracker** uses any webcam for 6DOF face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**
2. Select your webcam in the tracker settings
3. Set output to **UDP over network** (`127.0.0.1:4242`)
4. Start tracking before launching the game
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed

### Phone App Setup

If your phone app sends a sufficiently filtered signal (built-in smoothing, stable sample rate), you can send directly to port 4242 without needing OpenTrack on PC. The mod includes interpolation for network jitter.

1. Install an OpenTrack-compatible head tracking app
2. Configure it to send to your PC's IP on port 4242 (run `ipconfig` to find it)
3. Set the protocol to OpenTrack/UDP

**With OpenTrack (optional):** If you want curve mapping or visual preview, route through OpenTrack. Set OpenTrack's input to "UDP over network" on a different port (e.g. 5252), point your phone app at that port, and set OpenTrack's output to `127.0.0.1:4242`. Make sure your firewall allows incoming UDP on the input port.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize:

```ini
[Network]
UDPPort=4242                ; UDP port for OpenTrack data

[Sensitivity]
YawMultiplier=1.0           ; Horizontal rotation (0.1-5.0)
PitchMultiplier=1.0         ; Vertical rotation (0.1-5.0)
RollMultiplier=1.0          ; Head tilt (0.0-2.0)

[Position]
SensitivityX=1.0            ; Lateral movement (0.1-10.0)
SensitivityY=1.0            ; Vertical movement (0.1-10.0)
SensitivityZ=1.0            ; Depth movement (0.1-10.0)
LimitX=0.30                 ; Max lateral offset in meters
LimitY=0.20                 ; Max vertical offset in meters
LimitZ=0.40                 ; Max forward offset in meters
LimitZBack=0.10             ; Max backward offset (prevents clipping through the player)
Smoothing=0.15              ; Position smoothing (0.0-0.99)
InvertX=true                ; Invert lateral axis
InvertY=false               ; Invert vertical axis
InvertZ=false               ; Invert depth axis
Enabled=true                ; Enable/disable 6DOF position tracking

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23              ; End - enable/disable tracking
RecenterKey=0x24            ; Home - recenter view
PositionToggleKey=0x21      ; Page Up - toggle position tracking
YawModeKey=0x22             ; Page Down - toggle world/local yaw
DiagnosticMarkerKey=0x78    ; F9 - place a marker in the diagnostic log

[General]
AutoEnable=true             ; Enable tracking on game start
WorldSpaceYaw=true          ; true = horizon-locked yaw (default), false = camera-local
```

Delete the file to reset to defaults.

## Troubleshooting

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in game root)
- Check `reframework/` folder exists with `plugins/RE8HeadTracking.dll` inside
- Try running the game as administrator once

**No tracking response:**
- Verify OpenTrack is running and outputting data
- Check UDP port matches (default 4242)
- Press **End** to enable tracking, **Home** to recenter
- Check firewall isn't blocking UDP port 4242

**Jittery / unstable tracking:**
- Increase position smoothing in HeadTracking.ini
- If using a phone app over WiFi, some jitter is expected

**Wrong rotation axis:**
- Adjust sensitivity multipliers or use the Invert settings in the Position section

**Yaw feels wrong when looking up or down at extreme angles:**
- Try toggling between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd` from the release folder. This removes the mod DLLs. REFramework is only removed if it was originally installed by this mod. To force-remove REFramework:

```powershell
uninstall.cmd /force
```

## Building from Source

### Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ desktop workload
- [pixi](https://pixi.sh) task runner

### Build

```bash
git clone --recurse-submodules https://github.com/itsloopyo/resident-evil-village-headtracking.git
cd resident-evil-village-headtracking

# Build and deploy to game (release)
pixi run install

# Build only (debug)
pixi run build

# Package for release
pixi run package
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil Village
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking software
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core) - Shared tracking, smoothing, and camera library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Capcom. Use at your own risk.
