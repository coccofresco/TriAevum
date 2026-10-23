param(
    [Parameter(Mandatory=$true)][string]$Build,
    [Parameter(Mandatory=$true)][string]$Dependencies,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$Vcpkg = 'C:/vcpkg/installed/x64-windows-static',
    [string]$Archives = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
# Library outputs go to the configured archive directory, not to the build
# tree: read it from the cache so the smoke links what the build produced
# instead of stale copies left there by an older configure.
if (!$Archives) {
    $match = Select-String -Path (Join-Path $Build 'CMakeCache.txt') `
        -Pattern '^CMAKE_ARCHIVE_OUTPUT_DIRECTORY:[^=]*=(.+)$' | Select-Object -First 1
    if (!$match) { throw "cannot resolve the archive directory from $Build/CMakeCache.txt" }
    $Archives = $match.Matches[0].Groups[1].Value
}
$imgui = Join-Path $Dependencies 'imgui-src'
$output = Join-Path $Build 'f1-settings-smoke.exe'
$testObjects = Join-Path $Build 'f1-settings-smoke-objects'
[IO.Directory]::CreateDirectory($testObjects) | Out-Null
$imguiObjects = @()
foreach ($unit in @('imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets')) {
    $object = Join-Path $testObjects "$unit.obj"
    $source = Join-Path $imgui "$unit.cpp"
    $inputs = @($source, "$imgui/imgui.h", "$imgui/imgui_internal.h", $PSCommandPath, $Compiler)
    $newest = ($inputs | Get-Item | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
    if (!(Test-Path -LiteralPath $object) -or (Get-Item $object).LastWriteTime -lt $newest) {
        & $Compiler '-std=c++20' '-O1' '-fms-runtime-lib=static' '-DIMGUI_ENABLE_TEST_ENGINE' "-I$imgui" '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $imguiObjects += $object
}
$args = @('-std=c++20', '-O1', '-fms-runtime-lib=static', '-DIMGUI_ENABLE_TEST_ENGINE',
    "-I$imgui", "-I$repo/runtime/three_ds_recomp/include", "-I$Vcpkg/include",
    "-I$repo/tools/oot3d/native_game_runtime", "-I$repo/tools/oot3d/ui_topscreen",
    "-I$repo/runtime/three_ds_recomp/src/fast/renderer",
    "-I$repo/tools/three_ds/input",
    "$PSScriptRoot/f1_settings_panel_smoke.cpp",
    "$Archives/three_ds_recomp_runtime.lib",
    "$Archives/renderer_extension_core.lib",
    "$Archives/stb.lib", "$Vcpkg/lib/zs.lib",
    "$Vcpkg/lib/spdlog.lib", "$Vcpkg/lib/fmt.lib",
    "$Archives/oot3d_ui_topscreen.lib", "$Archives/oot3d_native_control_config.lib",
    "$Archives/three_ds_recomp_input.lib",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_native_controls_settings_panel.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_top_screen_settings_panel.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_game_language.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_game_language_panel.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_scene_environment.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_scene_environment_blend.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_scene_environment_codec.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_light_gizmo.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_scene_environment_panel.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_game.dir/tools/oot3d/native_game_runtime/oot3d_savestate_panel.cpp.obj",
    "$Build/CMakeFiles/oot3d_native_a32_savestate.dir/tools/oot3d/native_game_runtime/oot3d_savestate_library.cpp.obj",
    '-o', $output)
$args += $imguiObjects
& $Compiler @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
