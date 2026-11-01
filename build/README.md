<div align='center'>
    <img height="200" src='https://github.com/juicermv/GTA-GPS-Redux/assets/34755463/879efcd9-6d7b-43b6-8198-3452756ac9a2'>
    <img height="200" src='https://github.com/juicermv/GTA-GPS-Redux/assets/34755463/39e3170a-8aea-4666-aabc-430f925c1684'>
    <img height="200" src='https://github.com/juicermv/GTA-GPS-Redux/assets/34755463/dc40c010-6e32-4ba0-b669-b128618bbd5e'>
</div>

# GTA SA GPS Redux — SAMP + Single Player

Automatic GPS navigation for GTA San Andreas: draws a route on the radar and the map to your waypoint and (in single player) to mission markers. This fork makes the mod work in **both single player and SAMP / open.mp**.

> **Credits**
> This is a fork of [**GTA-GPS-Redux**](https://github.com/juicermv/GTA-GPS-Redux) by **juicermv**, who wrote the original mod and owns the original idea, design and code. All credit for the core mod goes to him.
> This fork is maintained by [hoatran2k11](https://github.com/hoatran2k11). Screenshots above are from the original repository.

## What's different in this fork

### New: SAMP support
- **Automatic mode detection.** If `samp.dll` is loaded the mod runs in SAMP mode, otherwise in single player mode. Override it with `forceMode` in the config.
- **Single player:** route to the map waypoint and to mission markers (as in the original).
- **SAMP:** route to the map waypoint. SAMP does not run the game's missions, so there are no mission markers. Optionally (`[Multiplayer] trackServerBlips = 1`) the mod also routes to the nearest coordinate blip created by the server. This option is off by default and experimental.
- `[Multiplayer] enabled = 0` turns the GPS off on SAMP.
- The mod only patches the game on **GTA SA 1.0 US** (the version SAMP requires); on any other version it does nothing instead of risking a crash.

### Fixes and improvements
- **Crash fix:** the background pathfinding thread read the player's vehicle pointer directly and could crash after leaving a vehicle. The needed data is now captured on the game thread first.
- **Null / uninitialized pointers:** `player` and the mission trace were uninitialized, and vehicle/ped/object lookups by handle were not checked for null. All fixed.
- **Config file:** the original rewrote `SA.GPS.CONF.ini` on every launch, which wiped all comments. It is now read-only, missing keys fall back to defaults, and the file is looked up next to the `.asi` first (works from `scripts/`, modloader or the game folder).
- **Stale routes:** changing the waypoint or the mission target now discards the old route and any in-flight result for it.
- **Mission target selection:** pickups and (with `trackMovingTargets = 0`) vehicle/ped blips no longer block routing to a valid, farther target.
- **Route rendering:** skips path nodes that are not loaded, only draws when there are vertices, and no longer accumulates a distance value every frame.
- **Dependencies/build:** removed the SIMDString dependency (plain `std::string`), fixed a const-correctness error that broke MSVC, and updated `CMakeLists.txt` / `project.lua`.
- Logger writes `SA.GPS.LOG.txt` next to the `.asi`.

## Known Issues
- The mod currently doesn't work with pickups due to technical reasons.
- In some missions, a nav route will not appear for a valid target. This is a limitation of SA's pathfinding, as some coordinates break it and make it not generate any path. This also happens with the target route.
- Pathfinding runs on a background thread while the game also uses the path data. This is inherited from the original and a small race risk remains.
- SAMP support is new. Blips created by servers (`trackServerBlips`) may behave differently from server to server.

## Installation

1. Via modloader
    - Create a new folder inside your modloader folder. You can name it whatever you want.
    - Drop/extract all the files from the release archive into that folder.

2. Via ASI loader / `scripts` folder
    - Drop/extract all the files from the release archive into your `scripts` folder.

Keep `SA.GPS.REDUX.asi` and `SA.GPS.CONF.ini` in the same folder. For SAMP, install it the same way in your GTA SA folder (an ASI loader is required).

## Configuration

See the default config: [SA.GPS.CONF.ini](SA.GPS.CONF.ini). New in this fork:

```ini
[Multiplayer]
enabled = 1            ; GPS on SAMP
trackServerBlips = 0   ; route to coordinate blips created by the server

[Misc]
forceMode = 0          ; 0 = auto, 1 = single player, 2 = SAMP
```

## Building

Requires a 32-bit (x86) toolchain: MSVC or MinGW. The first configure downloads plugin-sdk, so you need an internet connection.

```
git clone --recurse-submodules https://github.com/hoatran2k11/gps-samp.git
cd gps-samp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run it from an **x86** Developer Command Prompt / PowerShell for Visual Studio. The output is `build/SA.GPS.REDUX.asi`.

## Re-Upload Policy

This fork is based on the work of juicermv, whose original policy still applies:
if you'd like to distribute this mod, please ask juicermv for permission first.
If you downloaded this mod from anywhere other than https://github.com/juicermv/GTA-GPS-Redux or https://github.com/hoatran2k11/gps-samp, make sure the source can be trusted.

## Libraries
* [mINI](https://github.com/metayeti/mINI)
* [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)