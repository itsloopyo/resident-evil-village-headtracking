# Third-Party Notices

This project uses the following third-party software.

## REFramework

- **Version:** nightly-01394 (commit `0436e04`)
- **License:** MIT
- **Upstream:** https://github.com/praydog/REFramework
- **Usage:** Plugin host and SDK for RE Engine games. Provides method hooking, type-system access, and per-GUI-element draw callbacks for the head-tracking plugin.
- **Bundled:** yes. Bundled in the release ZIP as the install-time source; install.cmd extracts it directly from the vendored copy.

## OpenTrack

- **Version:** N/A (UDP protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** Head-tracking data is received over the OpenTrack UDP protocol (port 4242). No OpenTrack code is bundled or linked.
- **Bundled:** no.

## MinHook

- **Version:** git `master` (TsudaKageyu/minhook)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Native x64 function hooking (trampolines) used by the shared camera-hook layer to detour the RE Engine camera update and render functions.
- **Bundled:** yes. Fetched at build time and statically compiled into the plugin DLL.

## CameraUnlock Core Library

- **Version:** submodule (see `cameraunlock-core`)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared C++ library providing the UDP receiver, tracking-processing pipeline, smoothing, interpolation, hotkey input, and math utilities. Compiled into the plugin DLL.
- **Bundled:** yes. Source compiled into the plugin DLL.

---
