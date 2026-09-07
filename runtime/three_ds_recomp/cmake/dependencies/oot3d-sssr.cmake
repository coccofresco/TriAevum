if(NOT THREE_DS_RECOMP_ENABLE_NRI OR NOT NRI_ENABLE_FFX_SDK)
    message(FATAL_ERROR
        "OOT3D FidelityFX SSSR requires the NRI FidelityFX SDK dependency")
endif()

FetchContent_GetProperties(ffx)
if(NOT ffx_POPULATED OR NOT EXISTS "${ffx_SOURCE_DIR}/sdk/CMakeLists.txt")
    message(FATAL_ERROR "The pinned FidelityFX SDK source is unavailable")
endif()

set(_ffx_sdk "${ffx_SOURCE_DIR}/sdk")
set(_ffx_include "${_ffx_sdk}/include")
set(_ffx_shared "${_ffx_sdk}/src/shared")
set(_ffx_backend_shared "${_ffx_sdk}/src/backends/shared")
set(_ffx_backend_vk "${_ffx_sdk}/src/backends/vk")
set(_ffx_components "${_ffx_sdk}/src/components")
set(_ffx_generated
    "${CMAKE_CURRENT_BINARY_DIR}/oot3d-ffx-sssr-generated")
set(_ffx_sc
    "${_ffx_sdk}/tools/binary_store/FidelityFX_SC.exe")

# The pinned GLSL callbacks declare 32-bit images where ffx_sssr.cpp creates
# compact images. Keep the donor intact and compile a checked source overlay.
set(_ffx_sssr_callbacks "${_ffx_include}/FidelityFX/gpu/sssr/ffx_sssr_callbacks_glsl.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ffx_sssr_callbacks}")
file(READ "${_ffx_sssr_callbacks}" _ffx_callbacks_source)
foreach(_contract IN ITEMS
        "SSSR_BIND_UAV_OUTPUT|rgba32f|rgba16f"
        "SSSR_BIND_UAV_RADIANCE|rgba32f|rgba16f"
        "SSSR_BIND_UAV_VARIANCE|r32f|r16f"
        "SSSR_BIND_UAV_EXTRACTED_ROUGHNESS|r32f|r8"
        "SSSR_BIND_UAV_BLUE_NOISE_TEXTURE|rg32f|rg8")
    string(REPLACE "|" ";" _fields "${_contract}")
    list(GET _fields 0 _binding)
    list(GET _fields 1 _before)
    list(GET _fields 2 _after)
    string(FIND "${_ffx_callbacks_source}" "${_binding}, ${_before})" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "SSSR image contract changed: ${_binding}")
    endif()
    string(REPLACE "${_binding}, ${_before})" "${_binding}, ${_after})"
        _ffx_callbacks_source "${_ffx_callbacks_source}")
endforeach()
set(_ffx_sssr_overlay "${_ffx_generated}/source/sssr/ffx_sssr_callbacks_glsl.h")
file(GENERATE OUTPUT "${_ffx_sssr_overlay}" CONTENT "${_ffx_callbacks_source}")

if(NOT EXISTS "${_ffx_sc}")
    message(FATAL_ERROR "The pinned FidelityFX shader compiler is unavailable")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DFFX_SOURCE_DIR=${ffx_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_LIST_DIR}/../patches/oot3d_fidelityfx_vk_core_aliases.cmake
    COMMAND_ERROR_IS_FATAL ANY)

function(_oot3d_ffx_compile_shader shader effect_define output_list)
    get_filename_component(_shader_name "${shader}" NAME_WE)
    set(_outputs
        "${_ffx_generated}/${_shader_name}_permutations.h"
        "${_ffx_generated}/${_shader_name}_wave64_permutations.h"
        "${_ffx_generated}/${_shader_name}_16bit_permutations.h"
        "${_ffx_generated}/${_shader_name}_wave64_16bit_permutations.h")
    set(_variant_names
        "${_shader_name}"
        "${_shader_name}_wave64"
        "${_shader_name}_16bit"
        "${_shader_name}_wave64_16bit")
    set(_half_values 0 0 1 1)
    foreach(_index RANGE 0 3)
        list(GET _outputs ${_index} _output)
        list(GET _variant_names ${_index} _variant)
        list(GET _half_values ${_index} _half)
        add_custom_command(
            OUTPUT "${_output}"
            COMMAND "${_ffx_sc}"
                -reflection
                -deps=gcc
                -DFFX_GPU=1
                -compiler=glslang
                -e CS
                --target-env vulkan1.2
                -S comp
                -Os
                -DFFX_GLSL=1
                "-D${effect_define}={0,1}"
                "-name=${_variant}"
                "-DFFX_HALF=${_half}"
                "-I${_ffx_generated}/source"
                "-I${_ffx_include}/FidelityFX/gpu"
                "-I${_ffx_include}/FidelityFX/gpu/sssr"
                "-I${_ffx_include}/FidelityFX/gpu/denoiser"
                "-output=${_ffx_generated}"
                "${shader}"
            DEPENDS "${shader}" "${_ffx_sssr_overlay}"
            DEPFILE "${_output}.d"
            VERBATIM)
    endforeach()
    set(${output_list} ${${output_list}} ${_outputs} PARENT_SCOPE)
endfunction()

file(GLOB _ffx_sssr_shaders
    "${_ffx_backend_vk}/shaders/sssr/*.glsl")
file(GLOB _ffx_denoiser_shaders
    "${_ffx_backend_vk}/shaders/denoiser/*.glsl")
set(_ffx_shader_outputs)
foreach(_shader IN LISTS _ffx_sssr_shaders)
    _oot3d_ffx_compile_shader(
        "${_shader}" FFX_SSSR_OPTION_INVERTED_DEPTH _ffx_shader_outputs)
endforeach()
foreach(_shader IN LISTS _ffx_denoiser_shaders)
    _oot3d_ffx_compile_shader(
        "${_shader}" FFX_DENOISER_OPTION_INVERTED_DEPTH _ffx_shader_outputs)
endforeach()
add_custom_target(oot3d_ffx_sssr_shaders DEPENDS ${_ffx_shader_outputs})

file(GLOB _ffx_shared_sources "${_ffx_shared}/*.cpp")
file(GLOB _ffx_denoiser_sources "${_ffx_components}/denoiser/*.cpp")
file(GLOB _ffx_sssr_sources "${_ffx_components}/sssr/*.cpp")

add_library(oot3d_ffx_denoiser STATIC
    ${_ffx_shared_sources}
    ${_ffx_denoiser_sources})
target_include_directories(oot3d_ffx_denoiser PUBLIC
    "${_ffx_include}"
    PRIVATE "${_ffx_shared}")
set_property(TARGET oot3d_ffx_denoiser PROPERTY CXX_STANDARD 17)

add_library(oot3d_ffx_sssr STATIC
    ${_ffx_shared_sources}
    ${_ffx_sssr_sources})
target_include_directories(oot3d_ffx_sssr PUBLIC
    "${_ffx_include}"
    PRIVATE "${_ffx_shared}" "${_ffx_components}")
target_link_libraries(oot3d_ffx_sssr PRIVATE oot3d_ffx_denoiser)
set_property(TARGET oot3d_ffx_sssr PROPERTY CXX_STANDARD 17)

add_library(oot3d_ffx_sssr_backend_vk STATIC
    "${CMAKE_CURRENT_LIST_DIR}/oot3d_ffx_vk_optional_stubs.cpp"
    "${_ffx_shared}/ffx_assert.cpp"
    "${_ffx_shared}/ffx_breadcrumbs_list.cpp"
    "${_ffx_shared}/ffx_message.cpp"
    "${_ffx_backend_shared}/ffx_shader_blobs.cpp"
    "${_ffx_backend_shared}/blob_accessors/ffx_denoiser_shaderblobs.cpp"
    "${_ffx_backend_shared}/blob_accessors/ffx_sssr_shaderblobs.cpp"
    "${_ffx_backend_vk}/ffx_vk.cpp"
    ${_ffx_shader_outputs})
add_dependencies(oot3d_ffx_sssr_backend_vk oot3d_ffx_sssr_shaders)
target_compile_definitions(oot3d_ffx_sssr_backend_vk PRIVATE
    FFX_DENOISER
    FFX_SSSR)
target_include_directories(oot3d_ffx_sssr_backend_vk PUBLIC
    "${_ffx_include}"
    PRIVATE
        "${_ffx_shared}"
        "${_ffx_backend_shared}"
        "${_ffx_backend_shared}/blob_accessors"
        "${_ffx_components}"
        "${_ffx_generated}")
target_link_libraries(oot3d_ffx_sssr_backend_vk PUBLIC Vulkan::Vulkan)
set_property(TARGET oot3d_ffx_sssr_backend_vk PROPERTY CXX_STANDARD 17)

if(MSVC)
    foreach(_target
            oot3d_ffx_denoiser
            oot3d_ffx_sssr
            oot3d_ffx_sssr_backend_vk)
        target_compile_options(${_target} PRIVATE
            /wd4100
            /wd4244
            /wd4267
            /wd4324)
    endforeach()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_target
            oot3d_ffx_denoiser
            oot3d_ffx_sssr
            oot3d_ffx_sssr_backend_vk)
        target_compile_options(${_target} PRIVATE
            -Wno-c++11-narrowing)
    endforeach()
endif()
