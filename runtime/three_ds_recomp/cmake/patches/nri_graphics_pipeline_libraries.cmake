if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# Pinned NRI extension, ABI v1: opt-in Vulkan factory flag, no descriptor ABI
# changes. Keep the implementation with its layout owner, not a global registry.
function(patch_nri relative old new)
    set(path "${NRI_SOURCE_DIR}/${relative}")
    file(READ "${path}" source)
    string(FIND "${source}" "${new}" present)
    if(NOT present EQUAL -1)
        return()
    endif()
    string(FIND "${source}" "${old}" location)
    if(location EQUAL -1)
        message(FATAL_ERROR "NRI GPL patch site changed: ${relative}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    file(WRITE "${path}" "${source}")
endfunction()

configure_file("${CMAKE_CURRENT_LIST_DIR}/nri_pipeline_libraries/GraphicsPipelineLibrariesVK.h"
    "${NRI_SOURCE_DIR}/Source/VK/GraphicsPipelineLibrariesVK.h" COPYONLY)
patch_nri("Include/NRIDescs.h" "#pragma once" "#pragma once\n#define TRIAEVUM_NRI_GPL_ABI 1")
patch_nri("Include/NRIDescs.h"
    [=[    FAIL_ON_CACHE_MISS  = NriBit(0) // "CreateGraphicsPipeline" returns "FAILURE" if the pipeline is not found in the supplied cache (requires "features.pipelineCacheControl")]=]
    [=[    FAIL_ON_CACHE_MISS  = NriBit(0), // "CreateGraphicsPipeline" returns "FAILURE" if the pipeline is not found in the supplied cache (requires "features.pipelineCacheControl")
    USE_GRAPHICS_PIPELINE_LIBRARY = NriBit(1) // TriAevum ABI v1: caller enabled VK_EXT_graphics_pipeline_library]=])
patch_nri("Source/VK/PipelineLayoutVK.h" "#pragma once"
    "#pragma once\n#include \"GraphicsPipelineLibrariesVK.h\"")
patch_nri("Source/VK/PipelineLayoutVK.h" "    ~PipelineLayoutVK();"
    "    mutable triaevum_nri::GraphicsPipelineLibraries m_GraphicsLibraries;\n\n    ~PipelineLayoutVK();")
patch_nri("Source/VK/PipelineLayoutVK.hpp" "PipelineLayoutVK::~PipelineLayoutVK() {"
    "PipelineLayoutVK::~PipelineLayoutVK() {\n    m_GraphicsLibraries.Clear(m_Device, m_Device.GetDispatchTable().DestroyPipeline, m_Device.GetVkAllocationCallbacks());")
patch_nri("Source/VK/PipelineVK.hpp"
    [=[    VkResult vkResult = vk.CreateGraphicsPipelines(m_Device, pipelineCache, 1, &info, m_Device.GetVkAllocationCallbacks(), &m_Handle);]=]
    [=[    VkResult vkResult = VK_ERROR_FEATURE_NOT_PRESENT;
    if (graphicsPipelineDesc.flags & GraphicsPipelineBits::USE_GRAPHICS_PIPELINE_LIBRARY) {
        std::vector<triaevum_nri::ProgramBytes> programs;
        for (uint32_t i = 0; i < graphicsPipelineDesc.shaderNum; ++i)
            programs.push_back({graphicsPipelineDesc.shaders[i].bytecode, graphicsPipelineDesc.shaders[i].size});
        vkResult = pipelineLayoutVK.m_GraphicsLibraries.Create(m_Device, vk.CreateGraphicsPipelines,
            vk.DestroyPipeline, m_Device.GetVkAllocationCallbacks(), pipelineCache, info, programs.data(), &m_Handle);
    }
    if (vkResult == VK_ERROR_FEATURE_NOT_PRESENT)
        vkResult = vk.CreateGraphicsPipelines(m_Device, pipelineCache, 1, &info, m_Device.GetVkAllocationCallbacks(), &m_Handle);]=])
