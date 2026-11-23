#Requires -Version 7
# Build both rendering backends and assemble a VDXR development app.
param(
    [string]$Output,
    [string]$CheekyRevision,
    [string]$OptiScalerNrRevision
)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$backends = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'integration-backends.json') -Raw | ConvertFrom-Json
if (-not $CheekyRevision) { $CheekyRevision = $backends.cheeky }
if (-not $Output) { $Output = Join-Path $workspaceRoot 'artifacts/vdxr-dev' }
$componentRoot = Join-Path $Output 'components/cheeky-vdxr'
$upstreamRoot = Join-Path $workspaceRoot 'external/cheeky-vdxr'
$repository = Join-Path $upstreamRoot 'source'
$commit = & git -C $repository rev-parse --verify "${CheekyRevision}^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Unknown Cheeky revision: $CheekyRevision" }
$patch = Join-Path $workspaceRoot 'patches/cheeky/dcs-joint-nr.patch'
$patchHash = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
# Keep the DCS patch in a separate worktree so the fork remains a clean dependency.
$buildRoot = Join-Path $workspaceRoot 'artifacts/native/cheeky-vdxr'
$source = Join-Path $buildRoot ($commit.Substring(0,12) + '-' + $patchHash.Substring(0,12))
if (!(Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    & git -C $repository worktree add --detach $source $commit
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check out the Cheeky build source.' }
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $commit) { throw "Unexpected Cheeky checkout in $source" }
& git -C $source diff --quiet
if ($LASTEXITCODE -ne 0) { throw "Unexpected local changes in $source" }
& git -C $source apply --reverse --cached --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    & git -C $source apply --index $patch
    if ($LASTEXITCODE -ne 0) { throw 'The DCS joint NR patch did not apply to Cheeky.' }
}
& (Join-Path $PSScriptRoot 'msvc.cmd') msbuild (Join-Path $source 'CheekyFoveatedDLSS.sln') '/t:CheekyStandaloneProxy;CheekyFoveatedDLSSHost;CheekyOpenXRLayer' /m:4 /p:Configuration=Release /p:Platform=x64 /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Cheeky VDXR build failed.' }
& (Join-Path $PSScriptRoot 'build-desktop-dev.ps1') -Output $Output -OptiScalerNrRevision $OptiScalerNrRevision
$nativeBuild = Join-Path $source 'bin/Release'
$files = @{
    'dxgi.dll' = Join-Path $nativeBuild 'standalone-loader/dxgi.dll'
    'CheekyFoveatedDLSS/CheekyFoveatedDLSSHost.dll' = Join-Path $nativeBuild 'CheekyFoveatedDLSS/CheekyFoveatedDLSSHost.dll'
    'CheekyFoveatedDLSS/CheekyFoveatedDLSSRuntime.dll' = Join-Path $nativeBuild 'CheekyFoveatedDLSS/CheekyFoveatedDLSSRuntime.dll'
    'CheekyOpenXRLayer.dll' = Join-Path $nativeBuild 'CheekyOpenXRLayer.dll'
    'XR_APILAYER_CHEEKY_foveated_dlss.json' = Join-Path $nativeBuild 'XR_APILAYER_CHEEKY_foveated_dlss.json'
}
foreach ($file in $files.GetEnumerator()) {
    $destination = Join-Path $componentRoot $file.Key
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.Value -Destination $destination -Force
    (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($destination + '.sha256') -Encoding ascii
}
$dlssg = Join-Path $componentRoot 'nvngx_dlssg.dll'
Copy-Item -LiteralPath (Join-Path $Output 'components/optiscaler-nr/OptiScaler/streamline/nvngx_dlssg.dll') -Destination $dlssg -Force
(Get-FileHash -LiteralPath $dlssg -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($dlssg + '.sha256') -Encoding ascii
$licenses = Join-Path $Output 'licenses/cheeky-vdxr'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -Path (Join-Path $upstreamRoot 'package/licenses/*') -Destination $licenses -Force
[ordered]@{ sourceCommit = $commit; jointNrPatchSha256 = $patchHash } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $componentRoot 'manifest.json') -Encoding utf8NoBOM
Write-Output (Join-Path $Output 'DcsControl.exe')
