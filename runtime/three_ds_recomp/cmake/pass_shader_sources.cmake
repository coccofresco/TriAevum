# Shared renderer-owned shader builders; no Vulkan device or title build.
include("${CMAKE_CURRENT_LIST_DIR}/spirv_pack.cmake")
function(triaevum_add_pass_shader_sources target)
    get_filename_component(runtime "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    target_sources(${target} PRIVATE
        "${runtime}/src/fast/oot3d/pica_scanout_effects.cpp"
        "${runtime}/src/fast/oot3d/toon_outline_shader.cpp"
        "${runtime}/src/fast/oot3d/ambient_occlusion_composite.cpp"
        "${runtime}/src/fast/oot3d/reflection_debug.cpp"
        "${runtime}/src/fast/oot3d/spatial_aa.cpp"
        "${runtime}/src/fast/oot3d/builtin_pass_shaders.cpp"
        "${runtime}/src/fast/oot3d/contact_shadow_shader.cpp"
        "${runtime}/src/fast/oot3d/grass_shader_sources.cpp"
        "${runtime}/src/fast/oot3d/hiz_depth_pyramid.cpp"
        "${runtime}/src/fast/oot3d/hiz_reflection.cpp"
        "${runtime}/src/fast/oot3d/linear_scene_color.cpp"
        "${runtime}/src/fast/oot3d/motion_vectors.cpp"
        "${runtime}/src/fast/oot3d/pica_display_transfer.cpp"
        "${runtime}/src/fast/oot3d/reflection_ibl.cpp"
        "${runtime}/src/fast/oot3d/scene_composite.cpp"
        "${runtime}/src/fast/oot3d/smaa_1x.cpp"
        "${runtime}/src/fast/oot3d/temporal_aa.cpp"
        "${runtime}/src/fast/oot3d/upscaler_guides.cpp"
        "${runtime}/src/fast/oot3d/upscaler_orientation.cpp"
        "${runtime}/src/fast/oot3d/native_ui_overlay.cpp"
        "${runtime}/src/fast/renderer/gtao_shader.cpp"
        # Diffuse screen-space GI kernels of the M5 runtime pass and the
        # module code their parameter builders use.
        "${runtime}/src/fast/renderer/diffuse_gi_pass_shader.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_gather_shader.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_temporal_shader.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_m4_shader.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_reference.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_gather.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_hierarchy.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_guiding.cpp"
        "${runtime}/src/fast/renderer/diffuse_gi_noise.cpp"
        # The OOT3D adapter kernel that fills the pass inputs from the guides.
        "${runtime}/src/fast/oot3d/diffuse_gi_adapter_shader.cpp"
        "${runtime}/src/fast/renderer3ds/pica_surface_lighting.cpp"
        "${runtime}/src/fast/renderer/shaderc_compiler.cpp"
        "${runtime}/src/fast/renderer/spirv_cache.cpp"
        "${runtime}/src/fast/renderer/cache_file.cpp"
    )
    triaevum_add_spirv_pack(${target} "${runtime}/src/fast/renderer/builtin_pass_binaries.cpp"
        "${runtime}/src/fast/renderer/builtin_pass.spvpack")
endfunction()
