#Requires -Version 7
# Build the independent DCS scene-event adapter without game deployment.
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$wrapper = Join-Path $PSScriptRoot 'msvc.cmd'
$build = Join-Path $workspaceRoot 'artifacts/native/dcsrender'
& $wrapper cmake -S (Join-Path $workspaceRoot 'native/dcsrender') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'DCS rendering adapter configuration failed.' }
& $wrapper cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'DCS rendering adapter build failed.' }
& $wrapper ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'DCS rendering adapter checks failed.' }
Copy-Item -LiteralPath (Join-Path $workspaceRoot 'native/dcsrender/lua/DcsRender.lua') -Destination $build -Force
