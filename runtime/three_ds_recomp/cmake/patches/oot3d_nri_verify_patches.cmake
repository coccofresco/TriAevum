if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()
if(NOT NGX_VERSION)
    message(FATAL_ERROR "NGX_VERSION is required")
endif()

# The renderer depends on every OOT3D patch of the pinned NRI, however the
# source was provided (Git fetch with PATCH_COMMAND, THREE_DS_RECOMP_NRI_SOURCE_DIR
# or a FETCHCONTENT_SOURCE_DIR_NRI override). An NRI without them still builds
# and fails only at run time, e.g. CmdBeginRendering dereferencing the NULL
# descriptor of a guide attachment an effect set leaves unbound. Each check
# names a marker the matching patch script writes.
set(_missing "")
function(oot3d_nri_require relative marker patch)
    set(_path "${NRI_SOURCE_DIR}/${relative}")
    if(NOT EXISTS "${_path}")
        set(_missing "${_missing}\n  ${relative}: missing file (${patch})" PARENT_SCOPE)
        return()
    endif()
    file(READ "${_path}" _source)
    string(FIND "${_source}" "${marker}" _found)
    if(_found EQUAL -1)
        set(_missing "${_missing}\n  ${relative}: ${patch} not applied" PARENT_SCOPE)
    endif()
endfunction()

function(oot3d_nri_require_copy relative patch)
    set(_path "${NRI_SOURCE_DIR}/${relative}")
    get_filename_component(_name "${relative}" NAME)
    set(_reference "${CMAKE_CURRENT_LIST_DIR}/nri_pipeline_libraries/${_name}")
    if(NOT EXISTS "${_path}")
        set(_missing "${_missing}\n  ${relative}: missing file (${patch})" PARENT_SCOPE)
        return()
    endif()
    file(SHA256 "${_path}" _actual)
    file(SHA256 "${_reference}" _expected)
    if(NOT _actual STREQUAL _expected)
        set(_missing "${_missing}\n  ${relative}: differs from the ${patch} copy" PARENT_SCOPE)
    endif()
endfunction()

oot3d_nri_require("CMakeLists.txt"
    "if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL \"Linux\" OR APPLE OR ANDROID))"
    "nri_android_platform")
oot3d_nri_require("CMakeLists.txt" "set(NGX_VERSION \"${NGX_VERSION}\")"
    "oot3d_nri_ngx_version")
oot3d_nri_require("Source/Shared/UpscalerInterface.hpp"
    "// DLSS Super Resolution needs an NVIDIA GPU, but not Vulkan RT"
    "oot3d_nri_dlss_capability")
oot3d_nri_require("CMakeLists.txt" "--vulkanVersion 1.2"
    "oot3d_nri_vulkan12_shaders")
oot3d_nri_require("Source/Validation/CommandBufferVal.hpp"
    "// OOT3D: Vulkan 1.2 attachment depth resolves, including the"
    "oot3d_nri_vulkan12_depth_resolve")
oot3d_nri_require("Include/NRIDescs.h" "#define TRIAEVUM_NRI_GPL_ABI 1"
    "nri_graphics_pipeline_libraries")
oot3d_nri_require_copy("Source/VK/PipelineLibraryMemoryVK.h"
    "nri_graphics_pipeline_libraries")
oot3d_nri_require_copy("Source/VK/GraphicsPipelineLibrariesVK.h"
    "nri_graphics_pipeline_libraries")
oot3d_nri_require_copy("Source/VK/DynamicPipelineStateVK.h"
    "nri_graphics_pipeline_libraries")
oot3d_nri_require("Source/VK/DescriptorVK.hpp"
    "// OOT3D: a storage view stays sampleable"
    "oot3d_nri_storage_view_sampled")
oot3d_nri_require("Source/VK/CommandBufferVK.hpp"
    "if (!attachmentDesc.descriptor) { // OOT3D: an unbound color attachment"
    "oot3d_nri_unbound_color_attachments")
oot3d_nri_require("Source/Validation/CommandBufferVal.hpp"
    "if (!renderingDesc.colors[i].descriptor) { // OOT3D: an unbound color attachment"
    "oot3d_nri_unbound_color_attachments")
oot3d_nri_require("Source/Shared/DeviceBase.h" "// OOT3D: read before destroying"
    "nri_destroy_lifetime")

if(_missing)
    message(FATAL_ERROR "NRI at ${NRI_SOURCE_DIR} lacks OOT3D patches:${_missing}")
endif()
