# VDXR and Desktop integration build

Use PowerShell 7, the .NET 10 SDK, Visual Studio C++ Build Tools with the Windows SDK and FXC, Python 3.10 or later, Git, CMake and Ninja. Run the commands from the DCS Control repository root.

## Source dependencies

`scripts/integration-backends.json` pins both upstream revisions. Clone the source repositories once.

```powershell
git clone https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS.git external/cheeky-vdxr/source
git clone https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass.git external/optiscaler-nr
```

The build applies the backend patches in separate worktrees and generates Cheeky's shader bytecode from source.

## Runtime inputs

- Extract `licenses` from the [Cheeky v0.7.0 standalone release](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS/releases/tag/v0.7.0) into `external/cheeky-vdxr/package`.
- Prepare the separately supplied runtime files under `external/optiscaler-runtime`, using the paths and SHA-256 values in [desktop-backend-runtime.json](../scripts/desktop-backend-runtime.json). Put the matching `NGX_LICENSE.txt`, `Streamline_LICENSE.txt` and `Streamline_THIRD_PARTY.md` in its `licenses` directory.
- To use Neural Rendering, import `nvngx_dlssnr.dll` under **Runtime file** in the app. Desktop and VDXR share the saved copy.

## Build

```powershell
./scripts/build-dcsqvcull.ps1
./scripts/build-dcsrender.ps1
./scripts/build-vdxr-dev.ps1 -Output (Join-Path $PWD 'artifacts/integration')
```

Open `artifacts/integration/DcsControl.exe` to test a development build. For distribution, use the upstream release pipeline in `scripts/build-release.ps1`. It packages the rendering backends under `files/components` alongside the other components.

For a fork preview, run `./scripts/build-release.ps1 -ForkPreview`. This build uses manual updates from the [fork’s Releases page](https://github.com/LDNKS094/dcs-control/releases). Standard builds use the upstream updater.
