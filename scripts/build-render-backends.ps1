#Requires -Version 7
# Build rendering components directly into the release or development payload.
param([Parameter(Mandatory)][string]$DistributionRoot)
$ErrorActionPreference = 'Stop'
$DistributionRoot = [IO.Path]::GetFullPath($DistributionRoot)
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$backends = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'integration-backends.json') -Raw | ConvertFrom-Json
& (Join-Path $PSScriptRoot 'build-optiscaler-nr.ps1') -DistributionRoot $DistributionRoot
$componentRoot = Join-Path $DistributionRoot 'components/cheeky-vdxr'
$upstreamRoot = Join-Path $workspaceRoot 'external/cheeky-vdxr'
$source = & python (Join-Path $PSScriptRoot 'prepare-render-source.py') cheeky-vdxr
if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the Cheeky source.' }
$tree = & git -C $source rev-parse 'HEAD^{tree}'
& (Join-Path $PSScriptRoot 'msvc.cmd') msbuild (Join-Path $source 'CheekyFoveatedDLSS.sln') '/t:CheekyStandaloneProxy;CheekyFoveatedDLSSHost;CheekyOpenXRLayer' /m:4 /p:Configuration=Release /p:Platform=x64 /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Cheeky VDXR build failed.' }
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
Copy-Item -LiteralPath (Join-Path $DistributionRoot 'components/optiscaler-nr/OptiScaler/streamline/nvngx_dlssg.dll') -Destination $dlssg -Force
(Get-FileHash -LiteralPath $dlssg -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($dlssg + '.sha256') -Encoding ascii
$licenses = Join-Path $DistributionRoot 'licenses/cheeky-vdxr'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -Path (Join-Path $upstreamRoot 'package/licenses/*') -Destination $licenses -Force
[ordered]@{ sourceCommit = $backends.cheeky; patchedTree = $tree } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $componentRoot 'manifest.json') -Encoding utf8NoBOM
