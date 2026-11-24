# VDXR and Desktop integration build

Use PowerShell 7, the .NET 10 SDK, Visual Studio C++ Build Tools with a Windows SDK, Git, CMake and Ninja. Run the commands from the DCS Control repository root.

## Source dependencies

`scripts/integration-backends.json` pins both DCS forks. For a fresh checkout, clone and check out those revisions:

```powershell
$backends = Get-Content scripts/integration-backends.json -Raw | ConvertFrom-Json
git clone https://github.com/LDNKS094/CheekyFoveatedDLSS.git external/cheeky-vdxr/source
git -C external/cheeky-vdxr/source checkout --detach $backends.cheeky
git clone https://github.com/LDNKS094/OptiScaler-DLSSNR-PreSR-Multipass.git external/optiscaler-nr
git -C external/optiscaler-nr checkout --detach $backends.optiScalerNr
git -C external/optiscaler-nr submodule update --init --recursive
```

The VDXR build applies the DCS joint NR patch in a separate Cheeky worktree.

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

Use a fresh output directory for the first build. Open `artifacts/integration/DcsControl.exe` and select VDXR or Desktop. The component manifests under `components` record the source revisions, the joint NR patch digest and the desktop runtime hashes.

Get updates manually from the [fork’s Releases page](https://github.com/LDNKS094/dcs-control/releases).
