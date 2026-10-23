if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# NRI restricts every Vulkan image view to its view type's usage, so a storage
# view is storage-only. The upscaler hands that view to NGX as the output, and
# anything layered on NGX reads the resources it is given: the DLSS 5 neural
# rendering pass (OptiScaler-DLSS5) samples the output view it receives, and a
# storage-only view samples as black. A storage view of a texture that is also
# a shader resource keeps the sampled usage, as the image itself allows; the
# view's use by NRI and the pipelines is unchanged.
set(_descriptor "${NRI_SOURCE_DIR}/Source/VK/DescriptorVK.hpp")
file(READ "${_descriptor}" _source)
set(_old [=[
    usageInfo.usage = GetImageViewUsage(textureViewDesc.type);
    if (textureViewDesc.type == TextureView::COLOR_ATTACHMENT && (textureDesc.usage & TextureUsageBits::INPUT_ATTACHMENT))
        usageInfo.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
]=])
set(_new [=[
    usageInfo.usage = GetImageViewUsage(textureViewDesc.type);
    if (textureViewDesc.type == TextureView::COLOR_ATTACHMENT && (textureDesc.usage & TextureUsageBits::INPUT_ATTACHMENT))
        usageInfo.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    // OOT3D: a storage view stays sampleable when its texture is (consumers layered on NGX read it).
    if ((textureViewDesc.type == TextureView::STORAGE_TEXTURE || textureViewDesc.type == TextureView::STORAGE_TEXTURE_ARRAY) &&
        (textureDesc.usage & TextureUsageBits::SHADER_RESOURCE))
        usageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
]=])

string(FIND "${_source}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    return()
endif()
string(FIND "${_source}" "${_old}" _patch_site)
if(_patch_site EQUAL -1)
    message(FATAL_ERROR "NRI image view usage changed; update the OOT3D storage view patch")
endif()
string(REPLACE "${_old}" "${_new}" _source "${_source}")
file(WRITE "${_descriptor}" "${_source}")
