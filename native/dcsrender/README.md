# DCS rendering adapter

An independent adapter for DCS scene lifecycle notifications and rendering-resource policy.
Its Lua hook records load begin/end, simulation start/stop and the first simulation frame
in `dcs.log`. The adapter owns scene generations and selects obsolete backend feature handles.
Each backend owns its GPU synchronization and feature release.

Build and check with `scripts/build-dcsrender.ps1`. The development outputs are
`artifacts/native/dcsrender/DcsRender.dll` and `DcsRender.lua`.
`scripts/build-desktop-dev.ps1` copies both to `components/dcsrender` in the development app.
DCS Control installs them together in the selected Saved Games profile's `Scripts/Hooks`
directory for managed rendering backends and when Free unused DLSS memory enables native reclamation.
Switching modes or Back to stock DCS restores existing files
and removes files created by the profile. Unrecorded copies matching the bundled component
hashes are handled by the existing leftover cleanup.

The Lua hook calls `DcsRender_Initialize`. The adapter connects to either OptiScaler in `dxgi.dll`
or `CheekyFoveatedDLSSRuntime.dll`, with another registration attempt at mission load if needed.
Successful registration pins the adapter DLL for the callback's lifetime.

For native D3D11 NGX, DcsQvCull reports the newest set's evaluation count and whether an older set
was used after replacement. DcsRender authorizes retirement after the first simulation frame
and the native set's existing load and evaluation thresholds. DcsQvCull executes the release
with its existing NGX synchronization and handle guards.

After the first simulation frame, the adapter waits for one successful Desktop evaluation or two
distinct VR bridge views. It requests release of older handles that have not evaluated in the new
generation. The VR path selects handles previously evaluated in a mission. Each backend applies
the requests after a successful Evaluate on its serialized feature path.

DCS callbacks only publish atomic scene flags and set the backend's explicit FG pause request
through `OptiScaler_SetFrameGenerationPausedV1`. Loading and simulation stop request a pause;
the first simulation frame clears it. The backend resumes FG with fresh rendering inputs.

`DcsRender_GetSceneStateV1` retains the observable 64-bit scene state: the high 32 bits contain
the load generation; the low 32 bits contain the flags in `src/scene_events.h`. Load begin
advances the generation and clears previous flags. Load end, simulation start and first frame
accumulate flags; simulation stop replaces them with `SimulationStopped`.

The native tests cover Desktop loading and menu handles, VR stereo readiness, retained handles,
and confirmed release events.

## Backend API

- `OptiScaler_RegisterReleaseLifecycleV2` and `Cheeky_RegisterReleaseLifecycleV1` take a callback with signature `void(uint32_t event, uint64_t handle)` and return 1 on registration.
- Events are Created (1), Evaluated (2), and Released (3). Callbacks can enqueue release requests.
- `OptiScaler_RequestReleaseV2` and `Cheeky_RequestReleaseV1` take a `uint64_t` handle and return 1 when queued. The Released event confirms completion.
- `OptiScaler_SetFrameGenerationPausedV1(bool paused)` controls the Desktop loading pause.
