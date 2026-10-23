# Precompiled SPIR-V packs (fast/renderer/spirv_pack.h). The source next to a
# pack embeds it with #embed; its object depends on the pack. A compiler
# without #embed (or TRIAEVUM_CXX_HAS_EMBED=OFF) includes <pack>.inc instead,
# a byte list generated in the build tree whenever the pack changes.
include_guard(GLOBAL)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#if !defined(__has_embed)\n#error no #embed\n#endif\nint main() { return 0; }"
    TRIAEVUM_CXX_HAS_EMBED)
set(TRIAEVUM_SPIRV_PACK_TO_INC "${CMAKE_CURRENT_LIST_DIR}/spirv_pack_to_inc.cmake")

function(triaevum_add_spirv_pack target source pack)
    get_filename_component(source "${source}" ABSOLUTE)
    get_filename_component(pack "${pack}" ABSOLUTE)
    target_sources(${target} PRIVATE "${source}")
    set(depends "${pack}")
    if(NOT TRIAEVUM_CXX_HAS_EMBED)
        get_filename_component(name "${pack}" NAME)
        set(directory "${CMAKE_BINARY_DIR}/spirv_pack_inc")
        set(list "${directory}/${name}.inc")
        if(NOT EXISTS "${list}" OR "${pack}" IS_NEWER_THAN "${list}")
            execute_process(COMMAND "${CMAKE_COMMAND}" "-DINPUT=${pack}" "-DOUTPUT=${list}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/spirv_pack_to_inc.cmake" COMMAND_ERROR_IS_FATAL ANY)
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pack}")
        target_include_directories(${target} PRIVATE "${directory}")
        set_property(SOURCE "${source}" TARGET_DIRECTORY ${target} APPEND PROPERTY
            COMPILE_DEFINITIONS TRIAEVUM_SPIRV_PACK_EMBED=0)
        list(APPEND depends "${list}")
    endif()
    set_property(SOURCE "${source}" TARGET_DIRECTORY ${target} APPEND PROPERTY OBJECT_DEPENDS ${depends})
endfunction()
