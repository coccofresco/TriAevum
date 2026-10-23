#include "fast/oot3d/grass_card_renderer.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_gpu_memory.h"
#include "fast/oot3d/grass_shader_sources.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {

// Frames a retired resource waits before destruction: the frames in flight
// that may still read it, with a margin.
constexpr uint64_t kRetireFrames = 4U;

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t mask, VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred = 0U) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    const auto find = [&](VkMemoryPropertyFlags flags) -> int {
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            if ((mask & (1U << index)) != 0U && (properties.memoryTypes[index].propertyFlags & flags) == flags)
                return static_cast<int>(index);
        return -1;
    };
    int index = preferred != 0U ? find(required | preferred) : -1;
    if (index < 0) index = find(required);
    if (index < 0) throw std::runtime_error("grass cards have no compatible memory type");
    return static_cast<uint32_t>(index);
}

} // namespace

struct GrassCardRenderer::Impl {
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> DescriptorSets{};
    struct DescriptorInvocation {
        VkDescriptorPool Pool = VK_NULL_HANDLE;
        VkDescriptorSet Set = VK_NULL_HANDLE;
    };
    struct DescriptorFrame {
        uint64_t FrameId = 0U;
        size_t Used = 0U;
        std::vector<DescriptorInvocation> Additional;
    };
    std::array<DescriptorFrame, 2> DescriptorFrames;
    VkDescriptorSet PreparedDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    GrassPipelineSet Pipelines{};
    VkSampler AtlasSampler = VK_NULL_HANDLE;

    struct Buffer {
        VkBuffer Handle = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0U;
        void* Mapped = nullptr;
    };
    // The card records: immutable for a revision. A new revision gets a new
    // buffer and the previous one retires, so every recorded command keeps
    // the buffer it was recorded with (several invocations per frame, frames
    // in flight).
    Buffer Records;
    uint64_t RecordRevision = 0U;
    VkDeviceSize RecordBytes = 0U;
    bool RecordsValid = false;

    struct Atlas {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Bytes = 0U;
        uint64_t Key = 0U;
        uint32_t Mips = 0U;
    };
    Atlas CurrentAtlas;
    struct Retired {
        uint64_t FrameId = 0U;
        Atlas Image;
        Buffer Staging;
    };
    std::vector<Retired> RetiredResources;
    std::string Reason;

    void DestroyBuffer(Buffer& buffer) noexcept {
        if (buffer.Mapped != nullptr) vkUnmapMemory(Device, buffer.Memory);
        if (buffer.Handle != VK_NULL_HANDLE) vkDestroyBuffer(Device, buffer.Handle, nullptr);
        if (buffer.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(Device, buffer.Memory, nullptr);
            TrackGrassGpuMemory(false, -static_cast<int64_t>(buffer.Capacity));
        }
        buffer = {};
    }

    void CreateBuffer(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        buffer.Capacity = GrassBufferCapacity(std::max<VkDeviceSize>(bytes, 65536U));
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = buffer.Capacity;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &info, nullptr, &buffer.Handle) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass card buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, buffer.Handle, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(PhysicalDevice, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (GrassGpuAllocationRefused(PhysicalDevice, allocation.memoryTypeIndex, buffer.Capacity) ||
            vkAllocateMemory(Device, &allocation, nullptr, &buffer.Memory) != VK_SUCCESS)
            throw GrassGpuOutOfMemory("cannot allocate grass card buffer");
        TrackGrassGpuMemory(false, static_cast<int64_t>(buffer.Capacity), "cards", usage);
        if (vkBindBufferMemory(Device, buffer.Handle, buffer.Memory, 0) != VK_SUCCESS ||
            vkMapMemory(Device, buffer.Memory, 0, buffer.Capacity, 0, &buffer.Mapped) != VK_SUCCESS)
            throw std::runtime_error("cannot map grass card buffer");
    }

    void DestroyAtlas(Atlas& atlas) noexcept {
        if (atlas.View != VK_NULL_HANDLE) vkDestroyImageView(Device, atlas.View, nullptr);
        if (atlas.Image != VK_NULL_HANDLE) vkDestroyImage(Device, atlas.Image, nullptr);
        if (atlas.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(Device, atlas.Memory, nullptr);
            TrackGrassGpuMemory(true, -static_cast<int64_t>(atlas.Bytes));
        }
        atlas = {};
    }

    void CollectRetired(uint64_t frameId) noexcept {
        std::erase_if(RetiredResources, [&](Retired& retired) {
            if (frameId < retired.FrameId + kRetireFrames) return false;
            DestroyAtlas(retired.Image);
            DestroyBuffer(retired.Staging);
            return true;
        });
    }

    // Replaces the record buffer with one holding the given records; the
    // previous buffer retires with the frame.
    void ReplaceRecords(uint64_t frameId, std::span<const GrassCardRecord> records, uint64_t revision,
                        GrassCardPrepareResult& result) {
        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(std::max<size_t>(records.size(), 1U)) * sizeof(GrassCardRecord);
        Buffer replacement;
        try {
            CreateBuffer(replacement, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        } catch (...) {
            DestroyBuffer(replacement);
            throw;
        }
        if (!records.empty()) std::memcpy(replacement.Mapped, records.data(), records.size() * sizeof(GrassCardRecord));
        if (Records.Handle != VK_NULL_HANDLE) RetiredResources.push_back({frameId, {}, Records});
        Records = replacement;
        RecordRevision = revision;
        RecordBytes = bytes;
        RecordsValid = true;
        result.DynamicUploadedBytes += bytes;
    }

    // Builds the atlas image on the calling thread and records its upload.
    void UploadAtlas(VkCommandBuffer commandBuffer, uint64_t frameId, const GrassCardAtlasShape& shape,
                     GrassCardPrepareResult& result) {
        const auto image = BuildGrassCardAtlas(shape);
        Atlas atlas;
        atlas.Key = image.Key;
        atlas.Mips = static_cast<uint32_t>(image.Mips.size());
        Buffer staging;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = {image.Size, image.Size, 1U};
            info.mipLevels = atlas.Mips;
            info.arrayLayers = image.Layers;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(Device, &info, nullptr, &atlas.Image) != VK_SUCCESS)
                throw std::runtime_error("cannot create grass card atlas");
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(Device, atlas.Image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex =
                FindMemoryType(PhysicalDevice, requirements.memoryTypeBits, 0U, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (GrassGpuAllocationRefused(PhysicalDevice, allocation.memoryTypeIndex, requirements.size) ||
                vkAllocateMemory(Device, &allocation, nullptr, &atlas.Memory) != VK_SUCCESS)
                throw GrassGpuOutOfMemory("cannot allocate grass card atlas");
            atlas.Bytes = requirements.size;
            TrackGrassGpuMemory(true, static_cast<int64_t>(atlas.Bytes), "card_atlas");
            if (vkBindImageMemory(Device, atlas.Image, atlas.Memory, 0) != VK_SUCCESS)
                throw std::runtime_error("cannot bind grass card atlas");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = atlas.Image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            view.format = info.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, atlas.Mips, 0U, image.Layers};
            if (vkCreateImageView(Device, &view, nullptr, &atlas.View) != VK_SUCCESS)
                throw std::runtime_error("cannot create grass card atlas view");

            VkDeviceSize total = 0U;
            for (const auto& mip : image.Mips) total += mip.size();
            CreateBuffer(staging, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            std::vector<VkBufferImageCopy> copies(atlas.Mips);
            VkDeviceSize offset = 0U;
            for (uint32_t mip = 0; mip < atlas.Mips; ++mip) {
                std::memcpy(static_cast<uint8_t*>(staging.Mapped) + offset, image.Mips[mip].data(),
                            image.Mips[mip].size());
                auto& copy = copies[mip];
                copy.bufferOffset = offset;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0U, image.Layers};
                copy.imageExtent = {image.MipSize(mip), image.MipSize(mip), 1U};
                offset += image.Mips[mip].size();
            }
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = 0U;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = atlas.Image;
            barrier.subresourceRange = view.subresourceRange;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U,
                                 0U, nullptr, 0U, nullptr, 1U, &barrier);
            vkCmdCopyBufferToImage(commandBuffer, staging.Handle, atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<uint32_t>(copies.size()), copies.data());
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &barrier);
            result.StaticUploadedBytes += total;
            result.AtlasRebuilt = true;
        } catch (...) {
            DestroyBuffer(staging);
            DestroyAtlas(atlas);
            throw;
        }
        // The previous atlas may still be read by frames in flight.
        if (CurrentAtlas.Image != VK_NULL_HANDLE) RetiredResources.push_back({frameId, CurrentAtlas, {}});
        RetiredResources.push_back({frameId, {}, staging});
        CurrentAtlas = atlas;
    }

    VkDescriptorSet AcquireDescriptorSet(size_t slotIndex, uint64_t frameId) {
        auto& frame = DescriptorFrames[slotIndex];
        if (frame.FrameId != frameId) {
            frame.FrameId = frameId;
            frame.Used = 0U;
        }
        if (frame.Used++ == 0U) return DescriptorSets[slotIndex];
        const size_t index = frame.Used - 2U;
        if (index == frame.Additional.size()) frame.Additional.emplace_back();
        auto& invocation = frame.Additional[index];
        if (invocation.Set == VK_NULL_HANDLE) {
            const std::array<VkDescriptorPoolSize, 2> sizes{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4U}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2U}}};
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.maxSets = 1U;
            pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
            pool.pPoolSizes = sizes.data();
            if (vkCreateDescriptorPool(Device, &pool, nullptr, &invocation.Pool) != VK_SUCCESS)
                throw std::runtime_error("cannot create grass card invocation descriptor pool");
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = invocation.Pool;
            allocation.descriptorSetCount = 1U;
            allocation.pSetLayouts = &DescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &allocation, &invocation.Set) != VK_SUCCESS) {
                vkDestroyDescriptorPool(Device, invocation.Pool, nullptr);
                invocation.Pool = VK_NULL_HANDLE;
                throw std::runtime_error("cannot allocate grass card invocation descriptors");
            }
        }
        return invocation.Set;
    }
};

GrassCardRenderer::GrassCardRenderer() : mImpl(std::make_unique<Impl>()) {}
GrassCardRenderer::~GrassCardRenderer() { Shutdown(); }

bool GrassCardRenderer::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                   Renderer::CachedPassShaderCompiler& shaders, const GrassPipelineTargets& targets) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::array<VkShaderModule, 4> fragments{};
    try {
        const auto vertexSource = BuildGrassCardVertexShader();
        const auto fragmentSource = BuildGrassCardFragmentShader(false);
        const auto conservativeSource = BuildGrassCardFragmentShader(true);
        vertex = CompileGrassShaderModule(shaders, device, vertexSource.c_str(), Renderer::SpirvStage::Vertex,
                                          "interactive_grass_card.vert");
        fragments[0] = CompileGrassShaderModule(shaders, device, fragmentSource.c_str(),
            Renderer::SpirvStage::Fragment, "interactive_grass_card_canonical.frag", "GRASS_AUXILIARY_OUTPUTS", "0");
        fragments[1] = CompileGrassShaderModule(shaders, device, fragmentSource.c_str(),
            Renderer::SpirvStage::Fragment, "interactive_grass_card_instrumented.frag", "GRASS_AUXILIARY_OUTPUTS", "1");
        // The raster-depth slots of the set: the same blade depth, declared
        // never nearer than the rasterized one so the early depth test stays.
        fragments[2] = CompileGrassShaderModule(shaders, device, conservativeSource.c_str(),
            Renderer::SpirvStage::Fragment, "interactive_grass_card_canonical_raster_depth.frag",
            "GRASS_AUXILIARY_OUTPUTS", "0");
        fragments[3] = CompileGrassShaderModule(shaders, device, conservativeSource.c_str(),
            Renderer::SpirvStage::Fragment, "interactive_grass_card_instrumented_raster_depth.frag",
            "GRASS_AUXILIARY_OUTPUTS", "1");
        // The blade bindings the card programs share keep their numbers (0
        // environment, 1 lighting, 4 shadow grid, 7 patches); 10 holds the
        // records, 11 the atlas.
        const std::array<VkDescriptorSetLayoutBinding, 6> bindings{{
            {0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {4U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {7U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {10U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {11U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mImpl->DescriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass card descriptor layout");
        const std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8U}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4U}}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = static_cast<uint32_t>(mImpl->DescriptorSets.size());
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &mImpl->DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass card descriptor pool");
        const std::array<VkDescriptorSetLayout, 2> layouts{mImpl->DescriptorSetLayout, mImpl->DescriptorSetLayout};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = mImpl->DescriptorPool;
        allocate.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocate.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocate, mImpl->DescriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("cannot allocate grass card descriptors");
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
                                       sizeof(GrassPushConstants)};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1U;
        pipelineLayout.pSetLayouts = &mImpl->DescriptorSetLayout;
        pipelineLayout.pushConstantRangeCount = 1U;
        pipelineLayout.pPushConstantRanges = &push;
        if (vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &mImpl->PipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass card pipeline layout");
        CreateGrassPipelineSet(device, targets, mImpl->PipelineLayout, vertex, fragments, mImpl->Pipelines);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(device, &sampler, nullptr, &mImpl->AtlasSampler) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass card atlas sampler");
        for (const VkShaderModule fragment : fragments) vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        for (const VkShaderModule fragment : fragments)
            if (fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragment, nullptr);
        if (vertex != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertex, nullptr);
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
}

bool GrassCardRenderer::Available() const noexcept { return mImpl->Pipelines[0] != VK_NULL_HANDLE; }
const std::string& GrassCardRenderer::UnavailableReason() const noexcept { return mImpl->Reason; }

GrassCardPrepareResult GrassCardRenderer::Prepare(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                                                  uint64_t frameId, const GrassCardAtlasShape& shape,
                                                  std::span<const GrassCardRecord> records, uint64_t recordsRevision,
                                                  const GrassCardFrameInputs& inputs) {
    GrassCardPrepareResult result;
    mImpl->PreparedDescriptorSet = VK_NULL_HANDLE;
    if (!Available()) throw std::runtime_error("grass cards are unavailable");
    mImpl->CollectRetired(frameId);
    const uint64_t key = GrassCardAtlasKey(shape);
    if (mImpl->CurrentAtlas.Image == VK_NULL_HANDLE || mImpl->CurrentAtlas.Key != key)
        mImpl->UploadAtlas(commandBuffer, frameId, shape, result);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(std::max<size_t>(records.size(), 1U)) * sizeof(GrassCardRecord);
    if (!mImpl->RecordsValid || mImpl->RecordRevision != recordsRevision || mImpl->RecordBytes != bytes)
        mImpl->ReplaceRecords(frameId, records, recordsRevision, result);
    const VkDescriptorSet set = mImpl->AcquireDescriptorSet(frameSlot % mImpl->DescriptorSets.size(), frameId);
    const VkDescriptorBufferInfo recordInfo{mImpl->Records.Handle, 0U, bytes};
    const VkDescriptorImageInfo atlasInfo{mImpl->AtlasSampler, mImpl->CurrentAtlas.View,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 6> writes{};
    const auto bufferWrite = [&](size_t index, uint32_t binding, const VkDescriptorBufferInfo* info) {
        writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0U, 1U,
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, info, nullptr};
    };
    const auto imageWrite = [&](size_t index, uint32_t binding, const VkDescriptorImageInfo* info) {
        writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0U, 1U,
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, info, nullptr, nullptr};
    };
    bufferWrite(0U, 0U, &inputs.Environment);
    imageWrite(1U, 1U, &inputs.NativeLighting);
    bufferWrite(2U, 4U, &inputs.ShadowVisibility);
    bufferWrite(3U, 7U, &inputs.ShadowPatches);
    bufferWrite(4U, 10U, &recordInfo);
    imageWrite(5U, 11U, &atlasInfo);
    vkUpdateDescriptorSets(mImpl->Device, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);
    mImpl->PreparedDescriptorSet = set;
    return result;
}

void GrassCardRenderer::Draw(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height, size_t pipelineIndex,
                             std::span<const GrassCardDrawBatch> batches) {
    if (mImpl->PreparedDescriptorSet == VK_NULL_HANDLE || batches.empty() || pipelineIndex >= 3U) return;
    const VkPipeline explicitPipeline = mImpl->Pipelines[pipelineIndex];
    const VkPipeline conservativePipeline = mImpl->Pipelines[pipelineIndex + 3U];
    if (explicitPipeline == VK_NULL_HANDLE) return;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mImpl->PipelineLayout, 0U, 1U,
                            &mImpl->PreparedDescriptorSet, 0U, nullptr);
    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);
    VkPipeline bound = VK_NULL_HANDLE;
    for (const auto& batch : batches) {
        if (batch.CardCount == 0U) continue;
        // With the raster depth mapping the conservative-depth program keeps
        // the early depth test; any other mapping writes the depth freely.
        const VkPipeline wanted = GrassDepthIsRasterDepth(batch.Push) && conservativePipeline != VK_NULL_HANDLE
            ? conservativePipeline : explicitPipeline;
        if (wanted != bound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            bound = wanted;
        }
        vkCmdPushConstants(commandBuffer, mImpl->PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(batch.Push),
                           &batch.Push);
        vkCmdDraw(commandBuffer, 6U, batch.CardCount, 0U, batch.FirstCard);
    }
    mImpl->PreparedDescriptorSet = VK_NULL_HANDLE;
}

void GrassCardRenderer::Shutdown() {
    if (!mImpl) return;
    if (mImpl->Device != VK_NULL_HANDLE) {
        for (auto& retired : mImpl->RetiredResources) {
            mImpl->DestroyAtlas(retired.Image);
            mImpl->DestroyBuffer(retired.Staging);
        }
        mImpl->RetiredResources.clear();
        mImpl->DestroyAtlas(mImpl->CurrentAtlas);
        mImpl->DestroyBuffer(mImpl->Records);
        if (mImpl->AtlasSampler != VK_NULL_HANDLE) vkDestroySampler(mImpl->Device, mImpl->AtlasSampler, nullptr);
        DestroyGrassPipelineSet(mImpl->Device, mImpl->Pipelines);
        if (mImpl->PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mImpl->Device, mImpl->PipelineLayout, nullptr);
        for (auto& frame : mImpl->DescriptorFrames) {
            for (const auto& invocation : frame.Additional)
                if (invocation.Pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mImpl->Device, invocation.Pool, nullptr);
            frame = {};
        }
        if (mImpl->DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mImpl->Device, mImpl->DescriptorPool, nullptr);
        if (mImpl->DescriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mImpl->Device, mImpl->DescriptorSetLayout, nullptr);
    }
    mImpl->Pipelines = {};
    mImpl->AtlasSampler = VK_NULL_HANDLE;
    mImpl->PipelineLayout = VK_NULL_HANDLE;
    mImpl->DescriptorPool = VK_NULL_HANDLE;
    mImpl->DescriptorSetLayout = VK_NULL_HANDLE;
    mImpl->DescriptorSets = {};
    mImpl->PreparedDescriptorSet = VK_NULL_HANDLE;
    mImpl->RecordRevision = 0U;
    mImpl->RecordBytes = 0U;
    mImpl->RecordsValid = false;
    mImpl->Device = VK_NULL_HANDLE;
    mImpl->PhysicalDevice = VK_NULL_HANDLE;
}

} // namespace Fast::Oot3d

#endif
