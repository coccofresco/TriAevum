param(
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json",
    [string]$Output = "",
    [string]$BuildRoot = "",
    [switch]$Verify,
    [switch]$Launch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$viewerSource = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_mesh_viewer.cpp"
$nativeAssetsSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeAssets.cpp"
$nativeDemoSceneSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeDemoScene.cpp"
$nativeRenderSceneSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeRenderScene.cpp"
$nativeRendererSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeRenderer.cpp"
$includeRoot = Join-Path $repoRoot "runtime/three_ds_recomp\include"
$vcpkgRoot = Join-Path $repoRoot "build-codex\vcpkg\installed\x64-windows-static"
$vcpkgInclude = Join-Path $vcpkgRoot "include"
$vcpkgLib = Join-Path $vcpkgRoot "lib"
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "OOT3D standalone manifest not found: $Manifest"
}
if (-not (Test-Path -LiteralPath $viewerSource)) {
    throw "OOT3D native mesh viewer source not found: $viewerSource"
}
if (-not (Test-Path -LiteralPath $nativeAssetsSource)) {
    throw "runtime/three_ds_recomp OOT3D native assets source not found: $nativeAssetsSource"
}
if (-not (Test-Path -LiteralPath $nativeDemoSceneSource)) {
    throw "runtime/three_ds_recomp OOT3D native demo scene source not found: $nativeDemoSceneSource"
}
if (-not (Test-Path -LiteralPath $nativeRenderSceneSource)) {
    throw "runtime/three_ds_recomp OOT3D native render scene source not found: $nativeRenderSceneSource"
}
if (-not (Test-Path -LiteralPath $nativeRendererSource)) {
    throw "runtime/three_ds_recomp OOT3D native renderer source not found: $nativeRendererSource"
}
if (-not (Test-Path -LiteralPath $vcpkgInclude)) {
    throw "vcpkg include directory not found: $vcpkgInclude"
}
if (-not (Test-Path -LiteralPath (Join-Path $vcpkgLib "glfw3.lib"))) {
    throw "vcpkg GLFW static library not found under: $vcpkgLib"
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command prompt not found: $vsDevCmd"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path -Parent $Manifest) "native_host\runtime/three_ds_recomp_native_mesh_viewer.json"
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path (Split-Path -Parent $Output) "native_mesh_viewer_build"
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$exe = Join-Path $BuildRoot "oot3d_native_mesh_viewer.exe"
$compileCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && " +
    "cl /nologo /std:c++20 /EHsc /utf-8 /MT " +
    "/I`"$includeRoot`" " +
    "/I`"$vcpkgInclude`" " +
    "/Fe:`"$exe`" " +
    "`"$viewerSource`" " +
    "`"$nativeAssetsSource`" " +
    "`"$nativeDemoSceneSource`" " +
    "`"$nativeRenderSceneSource`" " +
    "`"$nativeRendererSource`" " +
    "/link /LIBPATH:`"$vcpkgLib`" " +
    "glfw3.lib opengl32.lib gdi32.lib user32.lib shell32.lib advapi32.lib ole32.lib uuid.lib imm32.lib winmm.lib"

cmd.exe /d /c $compileCommand
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native mesh viewer compilation failed with exit code $LASTEXITCODE"
}

& $exe --manifest $Manifest --self-test --output $Output
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native mesh viewer self-test failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    if (-not (Test-Path -LiteralPath $Output)) {
        throw "OOT3D native mesh viewer output was not written: $Output"
    }
    $json = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
    if ($json.status -ne "valid") {
        throw "OOT3D native mesh viewer output is not valid: $Output"
    }
    if ($json.runtime_n64_asset_substitution_used -ne $false -or $json.shipwright_replacement_path_used -ne $false) {
        throw "OOT3D native mesh viewer reported forbidden runtime substitution: $Output"
    }
    if ($json.uses_glb_runtime_meshes -ne $false -or $json.uses_collision -ne $false) {
        throw "OOT3D native mesh viewer used GLB runtime meshes or collision: $Output"
    }
    if ($json.room.triangle_count -le 0 -or $json.link_child.triangle_count -le 0) {
        throw "OOT3D native mesh viewer did not load drawable room/link triangles: $Output"
    }
    if ($json.room.decoded_texture_count -le 0 -or $json.link_child.decoded_texture_count -le 0) {
        throw "OOT3D native mesh viewer did not decode native CMB textures for room/link: $Output"
    }
    if ($json.link_child.standing_csab -ne "boy/anim/nml_wait_free.csab" -or $json.link_child.standing_pose_valid -ne $true) {
        throw "OOT3D native mesh viewer did not load valid Link standing CSAB frame 0: $Output"
    }
    if ($json.link_child.selected_mesh_count -le 0 -or $json.link_child.skin_transform_count -ne $json.link_child.bone_count) {
        throw "OOT3D native mesh viewer did not prepare Link selected meshes and skin transforms: $Output"
    }
    if ($json.link_child.selected_rigid_primitive_count -le 0 -or $json.link_child.selected_skinned_primitive_count -le 0) {
        throw "OOT3D native mesh viewer did not cover both rigid and skinned Link CMB primitives: $Output"
    }
    if ($json.link_child.native_to_scene_scale -le 0 -or $json.link_child.bounds.max_extent -le 0) {
        throw "OOT3D native mesh viewer did not scale Link into scene units: $Output"
    }
    if ([math]::Abs([double]$json.link_child.bounds.max_extent - [double]$json.link_child.target_height_units) -gt 0.5) {
        throw "OOT3D native mesh viewer Link scene bounds do not match target height units: $Output"
    }
    if ($json.engine_render_scene.format -ne "oot3d_native_render_scene_v1") {
        throw "OOT3D native mesh viewer did not build the engine-side render scene: $Output"
    }
    if ($json.engine_render_scene.room.batch_count -le 0 -or $json.engine_render_scene.link_child.batch_count -le 0) {
        throw "OOT3D native render scene did not generate room/link render batches: $Output"
    }
    if ($json.engine_render_scene.room.triangle_count -ne $json.room.triangle_count -or
        $json.engine_render_scene.link_child.triangle_count -le 0) {
        throw "OOT3D native render scene triangle counts are not usable: $Output"
    }
    if ($json.engine_render_scene.room.uploadable_texture_count -le 0 -or
        $json.engine_render_scene.link_child.uploadable_texture_count -le 0) {
        throw "OOT3D native render scene did not expose uploadable RGBA8 textures: $Output"
    }
    if ($json.engine_renderer_submission.status -ne "valid") {
        throw "OOT3D native renderer submission failed: $Output"
    }
    $expectedTextureUploads = [int]$json.engine_render_scene.room.uploadable_texture_count +
        [int]$json.engine_render_scene.link_child.uploadable_texture_count
    if ([int]$json.engine_renderer_submission.texture_upload_count -ne $expectedTextureUploads) {
        throw "OOT3D native renderer did not upload all native render textures: $Output"
    }
    $expectedDrawCalls = [int]$json.engine_render_scene.room.batch_count +
        [int]$json.engine_render_scene.link_child.batch_count
    if ([int]$json.engine_renderer_submission.draw_call_count -ne $expectedDrawCalls) {
        throw "OOT3D native renderer did not submit all render batches: $Output"
    }
    $expectedTriangles = [int]$json.engine_render_scene.room.triangle_count +
        [int]$json.engine_render_scene.link_child.triangle_count
    if ([int]$json.engine_renderer_submission.triangle_count -ne $expectedTriangles -or
        [int]$json.engine_renderer_submission.missing_texture_draw_call_count -ne 0) {
        throw "OOT3D native renderer submission counts are invalid: $Output"
    }
}

if ($Launch) {
    & $exe --manifest $Manifest
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D native mesh viewer exited with code $LASTEXITCODE"
    }
}
