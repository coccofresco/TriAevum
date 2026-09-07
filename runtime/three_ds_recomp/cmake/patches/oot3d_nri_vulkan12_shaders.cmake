if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# The renderer creates a Vulkan 1.2 instance/device. ShaderMake defaults to
# Vulkan 1.3, which emits SPIR-V 1.6 and makes NIS invalid on that device even
# when the adapter otherwise reports support.
set(_cmake "${NRI_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_cmake}" _source)
set(_old [=[
        list(APPEND SHADERMAKE_COMMANDS COMMAND ${SHADERMAKE_PATH} -p SPIRV --compiler "${SHADERMAKE_DXC_VK_PATH}" ${SHADERMAKE_GENERAL_ARGS})
]=])
set(_new [=[
        list(APPEND SHADERMAKE_COMMANDS COMMAND ${SHADERMAKE_PATH} -p SPIRV --compiler "${SHADERMAKE_DXC_VK_PATH}" --vulkanVersion 1.2 ${SHADERMAKE_GENERAL_ARGS})
]=])

string(FIND "${_source}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    return()
endif()
string(FIND "${_source}" "${_old}" _patch_site)
if(_patch_site EQUAL -1)
    message(FATAL_ERROR
        "NRI ShaderMake Vulkan command changed; update the OOT3D patch")
endif()
string(REPLACE "${_old}" "${_new}" _source "${_source}")
file(WRITE "${_cmake}" "${_source}")
