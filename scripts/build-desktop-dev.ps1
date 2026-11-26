#Requires -Version 7
# A local test build, without release packaging or game deployment.
param([string]$Output, [switch]$IncludeVdxr)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output = Join-Path $workspaceRoot 'artifacts/desktop-dev' }
& dotnet publish (Join-Path $workspaceRoot 'src/DcsVr.App/DcsVr.App.csproj') -c Release -r win-x64 --self-contained false -o $Output --nologo
if ($LASTEXITCODE -ne 0) { throw 'Desktop development app build failed.' }
if ($IncludeVdxr) {
    & (Join-Path $PSScriptRoot 'build-render-backends.ps1') -DistributionRoot $Output
} else {
    & (Join-Path $PSScriptRoot 'build-optiscaler-nr.ps1') -DistributionRoot $Output
}

# Engine Optimizations is independent of the screen backend. Reuse the local native build.
$engineRoot = Join-Path $Output 'components/dcsqvcull'
New-Item -ItemType Directory -Path $engineRoot -Force | Out-Null
foreach ($relative in @('artifacts/native/dcsqvcull/DcsQvCull.dll', 'artifacts/native/dcsqvcull/DcsQvCullPayload.dll', 'native/dcsqvcull/lua/DcsQvCull.lua')) {
    $source = Join-Path $workspaceRoot $relative
    $destination = Join-Path $engineRoot (Split-Path -Leaf $source)
    Copy-Item -LiteralPath $source -Destination $destination -Force
    (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($destination + '.sha256') -Encoding ascii
}
# Reuse the independent rendering adapter build.
$adapterRoot = Join-Path $Output 'components/dcsrender'
New-Item -ItemType Directory -Path $adapterRoot -Force | Out-Null
foreach ($filename in @('DcsRender.dll', 'DcsRender.lua')) {
    $source = Join-Path $workspaceRoot ('artifacts/native/dcsrender/' + $filename)
    $destination = Join-Path $adapterRoot $filename
    Copy-Item -LiteralPath $source -Destination $destination -Force
    (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -LiteralPath ($destination + '.sha256') -Encoding ascii
}
Write-Output (Join-Path $Output 'DcsControl.exe')
