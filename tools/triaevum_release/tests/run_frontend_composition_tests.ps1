param(
    [Parameter(Mandatory=$true)][string]$Build,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$Vcpkg = 'C:/vcpkg/installed/x64-windows-static'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
$output = Join-Path $Build 'frontend-composition-tests.exe'
$args = @('-std=c++20', '-O1', '-fms-runtime-lib=static',
    "-I$repo/tools/oot3d/native_game_runtime", "-I$repo/tools/oot3d/native_pica_frontend",
    "-I$repo/tools/oot3d/ui_topscreen", "-I$repo/runtime/three_ds_recomp/include",
    "-I$repo/tools/oot3d/decomp_support/evidence/zelda3drecomp/849140697187b895",
    "-I$Vcpkg/include",
    "$repo/tools/oot3d/native_game_runtime/oot3d_top_screen_frontend_composition_tests.cpp",
    '-o', $output)
& $Compiler @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
