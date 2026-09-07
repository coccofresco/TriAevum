if(NOT DEFINED FFX_SOURCE_DIR)
    message(FATAL_ERROR "FFX_SOURCE_DIR is required")
endif()

set(_backend "${FFX_SOURCE_DIR}/sdk/src/backends/vk/ffx_vk.cpp")
if(NOT EXISTS "${_backend}")
    message(FATAL_ERROR "FidelityFX Vulkan backend is unavailable")
endif()

file(READ "${_backend}" _source)
set(_original_source "${_source}")
set(_anchor
"        backendContext->vkFunctionTable.vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)vkDeviceContext->vkDeviceProcAddr(backendContext->device, \"vkGetBufferMemoryRequirements2KHR\");")
set(_patched
"        backendContext->vkFunctionTable.vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)vkDeviceContext->vkDeviceProcAddr(backendContext->device, \"vkGetBufferMemoryRequirements2KHR\");
        // Vulkan 1.1 promoted this entry point. Some applications correctly
        // enable only the core feature, so the KHR alias may be null even
        // though the physical device advertises dedicated allocation.
        if (backendContext->vkFunctionTable.vkGetBufferMemoryRequirements2KHR == nullptr)
            backendContext->vkFunctionTable.vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)vkDeviceContext->vkDeviceProcAddr(backendContext->device, \"vkGetBufferMemoryRequirements2\");")

string(FIND "${_source}" "${_patched}" _already_patched)
if(_already_patched EQUAL -1)
    string(FIND "${_source}" "${_anchor}" _anchor_found)
    if(_anchor_found EQUAL -1)
        message(FATAL_ERROR
            "Pinned FidelityFX Vulkan core-alias patch no longer applies")
    endif()
    string(REPLACE "${_anchor}" "${_patched}" _source "${_source}")
endif()

# The renderer's long-lived Vulkan device intentionally does not enable
# shaderFloat16. FidelityFX queries physical support rather than the enabled
# feature set, so keep this focused backend on its generated FP32 variants.
set(_fp16_anchor
"            deviceCapabilities->fp16Supported = (bool)shaderFloat18Int8Features.shaderFloat16;")
set(_fp16_patched
"            // OOT3D's existing Vulkan device does not enable shaderFloat16.
            // Physical support alone is not sufficient for valid SPIR-V use.
            deviceCapabilities->fp16Supported = false;")
string(FIND "${_source}" "${_fp16_patched}" _fp16_done)
if(_fp16_done EQUAL -1)
    string(FIND "${_source}" "${_fp16_anchor}" _fp16_found)
    if(_fp16_found EQUAL -1)
        message(FATAL_ERROR
            "Pinned FidelityFX Vulkan FP32 patch no longer applies")
    endif()
    string(REPLACE "${_fp16_anchor}" "${_fp16_patched}"
        _source "${_source}")
endif()

# FidelityFX 1.1.4 declares six global pool-size entries but passes five to
# Vulkan, omitting storage buffers used by the SSSR denoiser.
set(_pool_anchor
"        descriptorPoolCreateInfo.poolSizeCount = 5;")
set(_pool_patched
"        descriptorPoolCreateInfo.poolSizeCount = 6;")
string(FIND "${_source}" "${_pool_patched}" _pool_done)
if(_pool_done EQUAL -1)
    string(FIND "${_source}" "${_pool_anchor}" _pool_found)
    if(_pool_found EQUAL -1)
        message(FATAL_ERROR
            "Pinned FidelityFX Vulkan descriptor-pool patch no longer applies")
    endif()
    string(REPLACE "${_pool_anchor}" "${_pool_patched}"
        _source "${_source}")
endif()
if(NOT _source STREQUAL _original_source)
    file(WRITE "${_backend}" "${_source}")
endif()
