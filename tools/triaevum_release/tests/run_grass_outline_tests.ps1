param(
    [Parameter(Mandatory=$true)][string]$Build,
    [Parameter(Mandatory=$true)][string]$Dependencies,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$Vcpkg = 'C:/vcpkg/installed/x64-windows-static',
    [string]$VulkanSdk = 'C:/VulkanSDK/1.4.350.0'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
$renderer = "$repo/runtime/three_ds_recomp"
$gtest = "$Dependencies/googletest-src/googletest"
$objects = Join-Path $Build 'grass-outline-test-objects'
[IO.Directory]::CreateDirectory($objects) | Out-Null
$flags = @('-std=c++20', '-O1', '-fms-runtime-lib=static', '-DNOMINMAX',
    '-DOOT3D_FOUNDATION_HAS_SHADERC=1', '-DOOT3D_PIPELINE_CACHE_HAS_SHADERC=1',
    '-Wno-character-conversion',
    "-I$renderer/include", "-I$Vcpkg/include", "-I$VulkanSdk/Include",
    "-I$gtest/include", "-I$gtest")
$sources = @(
    "$renderer/tests/oot3d_graphics_foundation_tests.cpp",
    "$renderer/tests/oot3d_toon_outline_gpu_tests.cpp",
    "$renderer/tests/oot3d_grass_lod_gpu_tests.cpp",
    "$renderer/tests/oot3d_graphics_settings_persistence_tests.cpp",
    "$renderer/tests/oot3d_effect_graph_tests.cpp",
    "$renderer/tests/oot3d_pica_shader_pipeline_cache_tests.cpp",
    "$renderer/tests/oot3d_pica_scene_frame_tests.cpp",
    "$renderer/tests/oot3d_azahar_texture_pack_tests.cpp",
    "$renderer/tests/oot3d_pica_pipeline_manifest_tests.cpp",
    "$renderer/tests/oot3d_grass_async_placement_builder_tests.cpp",
    "$renderer/tests/oot3d_grass_world_placement_cache_tests.cpp",
    "$renderer/tests/oot3d_grass_geometry_registry_tests.cpp",
    "$renderer/src/fast/oot3d/pica_fragment_lighting.cpp",
    "$renderer/src/fast/renderer3ds/pica_nri_shader_contract.cpp",
    "$renderer/src/fast/renderer3ds/pica_nri_upload.cpp",
    "$gtest/src/gtest-all.cc", "$gtest/src/gtest_main.cc")
$headers = (Get-ChildItem "$renderer/include/fast" -Recurse -Filter '*.h' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
$headers = (@($headers, (Get-Item "$renderer/tests/oot3d_vulkan_compute_fixture.h").LastWriteTime) |
    Sort-Object -Descending)[0]
$compiled = @()
foreach ($source in $sources) {
    $object = Join-Path $objects ((Split-Path $source -Leaf) + '.obj')
    $newest = (@((Get-Item $source).LastWriteTime, (Get-Item $PSCommandPath).LastWriteTime,
        (Get-Item $Compiler).LastWriteTime, $headers) | Sort-Object -Descending)[0]
    if (!(Test-Path -LiteralPath $object) -or (Get-Item $object).LastWriteTime -lt $newest) {
        & $Compiler @flags '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $compiled += $object
}
$output = Join-Path $Build 'grass-outline-tests.exe'
& $Compiler '-fms-runtime-lib=static' @compiled `
    "$Build/three_ds_recomp_runtime/src/three_ds_recomp_runtime.lib" `
    "$Build/three_ds_recomp_runtime/src/fast/renderer_3ds_pica_core.lib" `
    "$Build/three_ds_recomp_runtime/src/fast/renderer_extension_core.lib" `
    "$Build/three_ds_recomp_runtime/stb.lib" "$Vcpkg/lib/fmt.lib" `
    "$Vcpkg/lib/spdlog.lib" "$Vcpkg/lib/zs.lib" `
    "$VulkanSdk/Lib/shaderc_shared.lib" "$VulkanSdk/Lib/vulkan-1.lib" '-o' $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:PATH = "$VulkanSdk/Bin;$env:PATH"
& $output "--gtest_output=json:$Build/grass-outline-tests.json"
exit $LASTEXITCODE
