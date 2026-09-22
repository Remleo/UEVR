# UEVR ![build](https://github.com/praydog/UEVR/actions/workflows/dev-release.yml/badge.svg)

## About this fork

This is a fork of [praydog/UEVR](https://github.com/praydog/UEVR) where I fix and improve UEVR for the games I bring
to VR: whatever a game needs to run, and whatever makes it play better. Builds are on the [Releases page](https://github.com/Remleo/UEVR/releases); each release
lists what it changes on top of the nightly it is based on.

- **STALKER 2** (Update 2, UE 5.5.4) is playable: no more freeze of about a minute after injection, cvars are found,
  and the UI shows in the headset.
- **The Outer Worlds 2**: console commands work again (`stat fps`, `user_script.txt`), and head tracking stays locked
  behind fullscreen menus.

Whatever is useful beyond my games goes to praydog as a pull request, and once he merges it or solves the problem
his own way, the fork drops its version. UEVR is praydog's work: for documentation, support and donations use the
links below, which all point to him.

### Building and contributing

- Work from the `remleo` branch, the default one here. Clone with submodules: `git clone --recursive`. Its UESDK
  submodule points at [Remleo/UESDK](https://github.com/Remleo/UESDK), branch `remleo`, which carries the UESDK side
  of the fixes.
- `remleo` is praydog's `master` with the fork's commits on top, and it is rebased whenever `master` moves, so its
  history is rewritten. Rebase your work onto it rather than merging it in.
- The last commits on `remleo` are fork-only: this README and "Local only", which points the submodule at the
  fork. They never go into a pull request to praydog.
- A fix meant for praydog starts from his `master`, not from `remleo`, so the pull request carries only that fix.
  Changes to UESDK go to [praydog/UESDK](https://github.com/praydog/UESDK) first.
- Build as upstream does (CMake, Visual Studio 2022, target `uevr`); the output is `UEVRBackend.dll`, to be dropped
  into a nightly package in place of praydog's.

---

Universal Unreal Engine VR Mod (4/5)

## Supported Engine Versions

4.8 - 5.4

## Links

- [Download (Stable release)](https://github.com/praydog/UEVR/releases)
- [Download (Nightly release)](https://github.com/praydog/UEVR-nightly/releases/latest)
- [Documentation](https://praydog.github.io/uevr-docs)
- [Flat2VR Discord](https://flat2vr.com)

## Features

- Full 6DOF support out of the box (HMD movement)
- Full stereoscopic 3D out of the box
- Native UE4/UE5 stereo rendering system
- Frontend GUI for easy process injection
- Supports OpenVR and OpenXR runtimes
- 3 rendering modes: Native Stereo, Synchronized Sequential, and Alternating/AFR
- Automatic handling of most in-game UI so it is projected into 3D space
- Optional 3DOF motion controls out of the box in many games, essentially emulating a semi-native VR experience
- Optional roomscale movement in many games, moving the player character itself in 3D space along with the headset
- User-authored UI-based system for adding motion controls and first person to games that don't support them
- In-game menu with shortcuts for adjusting settings
- Access to various CVars for fixing broken shaders/effects/performance issues
- Optional depth buffer integration for improved latency on some headsets
- Per-game configurations
- [C++ Plugin system](https://praydog.github.io/uevr-docs/plugins/getting_started.html) and [Blueprint support](https://praydog.github.io/uevr-docs/plugins/blueprint.html) for modders to add additional features like motion controls

## Getting Started

Before launching, ensure you have installed .NET 6.0 SDK. It should tell you where to install it upon first open, but if not, you can [download it from here](https://dotnet.microsoft.com/en-us/download/dotnet/6.0). Most people should click x64 in the top left table, under the Installers column, next to windows.

Download the latest release from the [Releases page](https://github.com/praydog/UEVR/releases)

1. Launch UEVRInjector.exe
2. Launch the target game
3. Locate the game in the process dropdown list
4. Select your desired runtime (OpenVR/OpenXR)
5. Toggle existing VR plugin nullification (if necessary)
6. Configure pre-injection settings
7. Inject

## To-dos before injection

1. Disable HDR (it will still work without it, but the game will be darker than usual if it is)
2. Start as administrator if the game is not visible in the list
3. Pass `-nohmd` to the game's command line and/or delete VR plugins from the game directory if the game contains any existing VR plugins
4. Disable any overlays that may conflict and cause crashes (Rivatuner, ASUS software, Razer software, Overwolf, etc...)
5. Disable graphical options in-game that may cause crashes or severe issues like DLSS Frame Generation
6. Consider disabling `Hardware Accelerated GPU Scheduling` in your Windows `Graphics settings`

## In-Game Menu

Press the **Insert** key or **L3+R3** on an XInput based controller to access the in-game menu, which opens by default at startup. With the menu open, hold **RT** for various shortcuts:

- RT + Left Stick: Move the camera left/right/forward/back
- RT + Right Stick: Move the camera up/down
- RT + B: Reset camera offset
- RT + Y: Recenter view
- RT + X: Reset standing origin

## Quick overview of rendering methods

### Native Stereo

When it works, it looks the best, performs the best (usually). Can cause crashes or graphical bugs if the game does not play well with it.

Temporal effects like TAA are fully intact. DLSS/FSR2 usually work completely fine with no ghosting in this mode.

Fully synchronized eye rendering. Works with the majority of games. Uses the actual stereo rendering pipeline in the Unreal Engine to achieve a stereoscopic image.

### Synchronized Sequential

A form of AFR. Can fix many rendering bugs that are introduced with Native Stereo. Renders two frames **sequentially** in a **synchronized** fashion on the same engine tick.

Fully synchronized eye rendering. Game world does not advance time between frames.

Looks normal but temporal effects like TAA will have ghosting/doubling effect. Motion blur will need to be turned off.

This is the first alternative option that should be used if Native Stereo is not working as expected or you are encountering graphical bugs.

**Skip Draw** skips the viewport draw on the next engine tick. Usually works the best but sometimes particle effects may not play at the correct speed.

**Skip Tick** skips the next engine tick entirely. Usually buggy but does fix particle effects and sometimes brings higher performance.

### AFR

Alternated Frame Rendering. Renders each eye on separate frames in an alternating fashion, with the game world advancing time in between frames. Causes eye desyncs and usually nausea along with it.

Not synchronized. Generally should not be used unless the other two are unusable in some way.
