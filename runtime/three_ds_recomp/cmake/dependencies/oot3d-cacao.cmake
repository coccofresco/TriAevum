include(FetchContent)

if(THREE_DS_RECOMP_CACAO_SOURCE_DIR)
    FetchContent_Declare(oot3d_cacao SOURCE_DIR "${THREE_DS_RECOMP_CACAO_SOURCE_DIR}")
else()
    FetchContent_Declare(oot3d_cacao
        GIT_REPOSITORY https://github.com/GPUOpen-Effects/FidelityFX-CACAO.git
        GIT_TAG 0ddca95e6714727a252ead345591ca8f2598f261
        GIT_SHALLOW FALSE
    )
endif()
FetchContent_GetProperties(oot3d_cacao)
if(NOT oot3d_cacao_POPULATED)
    FetchContent_Populate(oot3d_cacao)
endif()

if(NOT THREE_DS_RECOMP_DXC_EXECUTABLE)
    # Reuse the current SPIR-V compiler already selected for NRI. Cauldron's
    # historical compiler silently ignores vk::image_format attributes.
    if(EXISTS "${SHADERMAKE_DXC_VK_PATH}")
        set(THREE_DS_RECOMP_DXC_EXECUTABLE "${SHADERMAKE_DXC_VK_PATH}")
    else()
        find_program(THREE_DS_RECOMP_DXC_EXECUTABLE NAMES dxc
            HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
    endif()
endif()
if(NOT EXISTS "${THREE_DS_RECOMP_DXC_EXECUTABLE}")
    message(FATAL_ERROR "FidelityFX CACAO SPIR-V DXC is missing: ${THREE_DS_RECOMP_DXC_EXECUTABLE}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_cacao_src "${oot3d_cacao_SOURCE_DIR}/ffx-cacao/src")
set(_cacao_inc "${oot3d_cacao_SOURCE_DIR}/ffx-cacao/inc")
set(_cacao_generated "${CMAKE_CURRENT_BINARY_DIR}/oot3d-cacao-generated")
set(_cacao_stamp "${_cacao_generated}/cacao_spirv.stamp")

add_custom_command(
    OUTPUT "${_cacao_stamp}"
    COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_cacao_spirv.py"
        --source "${_cacao_src}"
        --output "${_cacao_generated}/PrecompiledShadersSPIRV"
        --dxc "${THREE_DS_RECOMP_DXC_EXECUTABLE}"
    DEPENDS
        "${THREE_DS_RECOMP_DXC_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_cacao_spirv.py"
        "${_cacao_src}/ffx_cacao.hlsl"
        "${_cacao_src}/ffx_cacao_bindings.hlsl"
        "${_cacao_src}/ffx_cacao_impl.cpp"
    VERBATIM
)
add_custom_target(oot3d_cacao_shaders DEPENDS "${_cacao_stamp}")
add_library(oot3d_cacao STATIC
    "${_cacao_src}/ffx_cacao.cpp"
    "${_cacao_src}/ffx_cacao_impl.cpp"
)
add_dependencies(oot3d_cacao oot3d_cacao_shaders)
# Headers are generated as a set. Make recompilation depend on the set's
# completion, not only an order dependency on the shader-generation target.
set_source_files_properties("${_cacao_src}/ffx_cacao_impl.cpp" PROPERTIES
    OBJECT_DEPENDS "${_cacao_stamp}")
target_compile_definitions(oot3d_cacao PRIVATE FFX_CACAO_ENABLE_VULKAN)
target_include_directories(oot3d_cacao PUBLIC "${_cacao_inc}" PRIVATE
    "${_cacao_src}" "${_cacao_generated}")
target_link_libraries(oot3d_cacao PUBLIC Vulkan::Vulkan)
set_property(TARGET oot3d_cacao PROPERTY CXX_STANDARD 17)
