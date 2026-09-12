if(NOT DEFINED FFX_SOURCE_DIR)
    message(FATAL_ERROR "FFX_SOURCE_DIR is required")
endif()
set(_backend "${FFX_SOURCE_DIR}/sdk/src/backends/vk/ffx_vk.cpp")
file(READ "${_backend}" _source)
set(_original "${_source}")

# EffectContext is alignas(32), while the donor concatenates arrays rounded
# only to four bytes. Account for absolute-pointer alignment, even when the
# caller's allocation itself has only the normal malloc alignment.
set(_size_anchor "pipelineArraySize + resourceArraySize + contextArraySize,")
set(_size_fixed "pipelineArraySize + resourceArraySize + contextArraySize + alignof(BackendContext_VK::EffectContext) - 1,")
string(FIND "${_source}" "${_size_fixed}" _size_done)
if(_size_done EQUAL -1)
    string(FIND "${_source}" "${_size_anchor}" _size_found)
    if(_size_found EQUAL -1)
        message(FATAL_ERROR "FidelityFX scratch size contract changed")
    endif()
    string(REPLACE "${_size_anchor}" "${_size_fixed}" _source "${_source}")
endif()

set(_map_anchor "        // Map context array
        backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;")
set(_map_fixed "        // Map context array with its declared host alignment.
        constexpr uintptr_t contextAlignment = alignof(BackendContext_VK::EffectContext);
        pMem = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(pMem) + contextAlignment - 1) & ~(contextAlignment - 1));
        backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;")
string(FIND "${_source}" "${_map_fixed}" _map_done)
if(_map_done EQUAL -1)
    string(FIND "${_source}" "${_map_anchor}" _map_found)
    if(_map_found EQUAL -1)
        message(FATAL_ERROR "FidelityFX scratch context mapping changed")
    endif()
    string(REPLACE "${_map_anchor}" "${_map_fixed}" _source "${_source}")
endif()
if(NOT _source STREQUAL _original)
    file(WRITE "${_backend}" "${_source}")
endif()
