#Requires -Version 7
# Assemble only the tested NR/SR/FG runtime payload. This script does not touch DCS.
param([string]$DistributionRoot, [Parameter(Mandatory)][string]$SourceCommit)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
if (-not $DistributionRoot) { $DistributionRoot = Join-Path $workspaceRoot 'artifacts/desktop-dev' }
$componentRoot = Join-Path $DistributionRoot 'components/optiscaler-nr'
$runtimeRoot = Join-Path $workspaceRoot 'external/optiscaler-runtime'
$runtime = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'desktop-backend-runtime.json') -Raw | ConvertFrom-Json
$files = @()
function Add-ComponentFile([string]$Source, [string]$Relative, [string]$ExpectedHash) {
    $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ExpectedHash -and $hash -ne $ExpectedHash) { throw "Unexpected desktop runtime: $Relative" }
    $destination = Join-Path $componentRoot $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination -Force
    $hash | Set-Content -LiteralPath ($destination + '.sha256') -Encoding ascii
    [ordered]@{ path = $Relative; sha256 = $hash }
}
$files += Add-ComponentFile (Join-Path $workspaceRoot 'external/optiscaler-nr/x64/Release/OptiScaler.dll') 'dxgi.dll' ''
foreach ($file in $runtime.files) {
    $files += Add-ComponentFile (Join-Path $runtimeRoot $file.path) $file.path $file.sha256
}
$ini = @'
[Upscalers]
Dx11Upscaler=dlss_12
Dx12Upscaler=dlss

[DlssNr]
Enabled=true
RunBeforeSR=true
FinishedPicture=false
DeferredDLSS=false
DenoiseFirst=false
Passes=1
WorkingScale=1.0
ApplyModel=true
SpatialCompression=false
SpatialCenterX=80
SpatialCenterY=80
SpatialWorkX=90
SpatialWorkY=90

[FrameGen]
Enabled=false
FGInput=upscaler
FGOutput=dlssg
FGNvngxReplacement=None

[DLSSG]
InterpolationCount=1

[Plugins]
LoadAsiPlugins=false

[Log]
LogToFile=false
LogLevel=2

[Hotfix]
CheckForUpdate=false
'@
$iniPath = Join-Path $componentRoot 'OptiScaler.ini'
$ini | Set-Content -LiteralPath $iniPath -Encoding utf8NoBOM
$iniHash = (Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash.ToLowerInvariant()
$iniHash | Set-Content -LiteralPath ($iniPath + '.sha256') -Encoding ascii
$files += [ordered]@{ path = 'OptiScaler.ini'; sha256 = $iniHash }
[ordered]@{ sourceCommit = $SourceCommit; runtimeSourceCommit = $runtime.sourceCommit; files = @($files) } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $componentRoot 'manifest.json') -Encoding utf8NoBOM
$licenseRoot = Join-Path $DistributionRoot 'licenses/optiscaler-nr'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $workspaceRoot 'external/optiscaler-nr/LICENSE') -Destination (Join-Path $licenseRoot 'OptiScaler_LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $workspaceRoot 'external/optiscaler-nr/external/directx_agility_sdk/LICENSE.txt') -Destination (Join-Path $licenseRoot 'DirectX_LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $workspaceRoot 'external/optiscaler-nr/external/FidelityFX-SDK-v2/3rdpartynotice.md') -Destination (Join-Path $licenseRoot 'FidelityFX_THIRD_PARTY.md') -Force
foreach ($name in @('Streamline_LICENSE.txt', 'Streamline_THIRD_PARTY.md', 'NGX_LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $runtimeRoot "licenses/$name") -Destination (Join-Path $licenseRoot $name) -Force
}
Write-Host "Desktop NR/SR/FG component: $componentRoot"
