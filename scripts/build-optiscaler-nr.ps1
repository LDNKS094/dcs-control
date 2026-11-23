#Requires -Version 7
# Build the screen backend from the checked-out OptiScaler NR fork.
param([string]$DistributionRoot, [string]$Revision)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
if (-not $Revision) {
    $Revision = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'integration-backends.json') -Raw | ConvertFrom-Json).optiScalerNr
}
$sourceRoot = Join-Path $workspaceRoot 'external/optiscaler-nr'
$commit = & git -C $sourceRoot rev-parse --verify "${Revision}^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Unknown OptiScaler NR revision: $Revision" }
$head = & git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $commit) { throw "Check out OptiScaler NR $commit before building." }
& git -C $sourceRoot merge-base --is-ancestor '6125a75b37f7e2a408e31934f4827e5660429203' $commit
if ($LASTEXITCODE -ne 0) { throw 'Unexpected OptiScaler NR baseline.' }
& git -C $sourceRoot diff --quiet HEAD --
if ($LASTEXITCODE -ne 0) { throw 'Commit or stash local OptiScaler NR changes before building.' }
$submodules = & git -C $sourceRoot submodule status --recursive
if ($LASTEXITCODE -ne 0 -or ($submodules -match '^[-+U]')) {
    throw 'Initialize the OptiScaler NR submodules at the recorded revisions before building.'
}
& (Join-Path $PSScriptRoot 'msvc.cmd') msbuild (Join-Path $sourceRoot 'OptiScaler.sln') /m:4 /p:Configuration=Release /p:Platform=x64 /p:PostBuildEventUseInBuild=false /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'OptiScaler NR build failed.' }
& (Join-Path $PSScriptRoot 'prepare-desktop-backend.ps1') -DistributionRoot $DistributionRoot -SourceCommit $commit
