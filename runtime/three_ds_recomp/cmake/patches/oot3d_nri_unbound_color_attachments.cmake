if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# The native PICA layouts keep their color attachment locations and leave the
# guides no effect requires unbound: a NULL color descriptor begins rendering
# with VK_NULL_HANDLE (writes discarded), as Vulkan dynamic rendering allows.
function(oot3d_patch_nri_file relative old new)
    set(_path "${NRI_SOURCE_DIR}/${relative}")
    file(READ "${_path}" _source)
    string(FIND "${_source}" "${new}" _already_patched)
    if(NOT _already_patched EQUAL -1)
        return()
    endif()
    string(FIND "${_source}" "${old}" _patch_site)
    if(_patch_site EQUAL -1)
        message(FATAL_ERROR "NRI ${relative} changed; update the OOT3D unbound color attachment patch")
    endif()
    string(REPLACE "${old}" "${new}" _source "${_source}")
    file(WRITE "${_path}" "${_source}")
endfunction()

oot3d_patch_nri_file("Source/VK/CommandBufferVK.hpp" [=[
static inline void FillRenderingAttachmentInfo(VkRenderingAttachmentInfo& attachmentInfo, const AttachmentDesc& attachmentDesc, Dim_t& renderWidth, Dim_t& renderHeight, Dim_t& layerNum) {
    const DescriptorVK& descriptorVK = *(DescriptorVK*)attachmentDesc.descriptor;
]=] [=[
static inline void FillRenderingAttachmentInfo(VkRenderingAttachmentInfo& attachmentInfo, const AttachmentDesc& attachmentDesc, Dim_t& renderWidth, Dim_t& renderHeight, Dim_t& layerNum) {
    if (!attachmentDesc.descriptor) { // OOT3D: an unbound color attachment
        attachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        return;
    }
    const DescriptorVK& descriptorVK = *(DescriptorVK*)attachmentDesc.descriptor;
]=])

oot3d_patch_nri_file("Source/Validation/CommandBufferVal.hpp" [=[
        NRI_RETURN_ON_FAILURE(&m_Device, renderingDesc.colors[i].descriptor, ReturnVoid(), "'colors[%u].descriptor' is NULL", i);
]=] [=[
        if (!renderingDesc.colors[i].descriptor) { // OOT3D: an unbound color attachment
            colors[i] = renderingDesc.colors[i];
            m_RenderTargets[i] = nullptr;
            continue;
        }
]=])
