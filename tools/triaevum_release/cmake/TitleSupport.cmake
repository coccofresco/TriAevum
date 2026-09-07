function(triaevum_add_title_support root pinned)
    set(a32 "${root}/tools/oot3d/native_a32_runtime")
    set(host "${root}/tools/oot3d/native_game_runtime")
    add_library(triaevum_title_whole_aot_support STATIC EXCLUDE_FROM_ALL
        "${host}/oot3d_native_a32_memory.cpp"
        "${host}/oot3d_native_a32_memory_bus_defaults.cpp"
        "${pinned}/recomp/a32_vfp_binary64.cpp"
        "${a32}/oot3d_native_a32_vfp_ops.cpp"
        "${pinned}/recomp/a32_vfp_transport.cpp")
    target_include_directories(triaevum_title_whole_aot_support PUBLIC
        "${host}" "${pinned}" "${pinned}/recomp"
        "${a32}" "${a32}/mass_cpp")
    target_link_libraries(triaevum_title_whole_aot_support PUBLIC nlohmann_json::nlohmann_json)
    target_compile_features(triaevum_title_whole_aot_support PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(triaevum_title_whole_aot_support PRIVATE /EHsc)
    endif()
    if(WIN32)
        target_compile_definitions(triaevum_title_whole_aot_support PRIVATE NOMINMAX)
        set_property(TARGET triaevum_title_whole_aot_support PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET triaevum_title_whole_aot_support PROPERTY FOLDER Tools)
endfunction()
