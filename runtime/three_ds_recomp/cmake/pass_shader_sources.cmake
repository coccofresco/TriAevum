# Shared renderer-owned shader builders; no Vulkan device or title build.
function(triaevum_add_pass_shader_sources target)
    get_filename_component(runtime "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    target_sources(${target} PRIVATE
        "${runtime}/src/fast/oot3d/pica_scanout_effects.cpp"
        "${runtime}/src/fast/oot3d/toon_outline_shader.cpp"
        "${runtime}/src/fast/oot3d/ambient_occlusion_composite.cpp"
        "${runtime}/src/fast/oot3d/reflection_debug.cpp"
        "${runtime}/src/fast/oot3d/spatial_aa.cpp"
        "${runtime}/src/fast/oot3d/builtin_pass_shaders.cpp"
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
        "${runtime}/src/fast/renderer/shaderc_compiler.cpp"
        "${runtime}/src/fast/renderer/spirv_cache.cpp"
        "${runtime}/src/fast/renderer/cache_file.cpp"
    )
endfunction()
