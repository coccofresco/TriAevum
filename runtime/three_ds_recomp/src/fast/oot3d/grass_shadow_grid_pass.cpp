#include "fast/oot3d/grass_shadow_grid_pass.h"
#include "fast/oot3d/grass_shader_sources.h"

#ifdef ENABLE_OOT3D_VULKAN
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {
void Check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("grass shadow grid Vulkan resource failure");
}
} // namespace

struct GrassShadowGridPass::Impl {
    struct Buffer {
        VkBuffer Handle{};
        VkDeviceMemory Memory{};
        VkDeviceSize Capacity{};
        void* Mapped{};
    };
    struct Invocation {
        Buffer Points, References, Visibility, Uniforms, StaticUniforms, SurfacePatches, Patches;
        VkDescriptorPool Pool{};
        // The refinement's (composed map) and the coarse grid's (static depth).
        VkDescriptorSet Set{};
        VkDescriptorSet StaticSet{};
        uint64_t Revision{};
        size_t SurfaceCount = 0;
    };
    struct Frame {
        uint64_t Id = ~uint64_t{};
        size_t Used = 0;
        std::vector<std::unique_ptr<Invocation>> Invocations;
    };
    VkDevice Device{};
    VkPhysicalDeviceMemoryProperties Memory{};
    VkDeviceSize MaximumStorageRange{};
    uint32_t DispatchCapacity = 0;
    VkDescriptorSetLayout DescriptorLayout{};
    VkPipelineLayout Layout{};
    VkPipeline Pipeline{};
    std::array<Frame, 2> Frames;
    GrassShadowRefinement Refinement;

    void Destroy(Buffer& buffer) {
        if (buffer.Mapped)
            vkUnmapMemory(Device, buffer.Memory);
        if (buffer.Handle)
            vkDestroyBuffer(Device, buffer.Handle, nullptr);
        if (buffer.Memory)
            vkFreeMemory(Device, buffer.Memory, nullptr);
        buffer = {};
    }
    void Ensure(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, bool mapped) {
        if (bytes <= buffer.Capacity)
            return;
        // This invocation slot is reused only after its owning frame fence.
        Destroy(buffer);
        buffer.Capacity = std::bit_ceil(std::max<VkDeviceSize>(bytes, 4096));
        VkBufferCreateInfo create{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        create.size = buffer.Capacity;
        create.usage = usage;
        Check(vkCreateBuffer(Device, &create, nullptr, &buffer.Handle));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, buffer.Handle, &requirements);
        const VkMemoryPropertyFlags flags =
            mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                   : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint32_t type = 0;
        for (; type < Memory.memoryTypeCount; ++type)
            if ((requirements.memoryTypeBits & (1U << type)) &&
                (Memory.memoryTypes[type].propertyFlags & flags) == flags)
                break;
        if (type == Memory.memoryTypeCount)
            throw std::runtime_error("grass shadow grid memory type unavailable");
        VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        Check(vkAllocateMemory(Device, &allocate, nullptr, &buffer.Memory));
        Check(vkBindBufferMemory(Device, buffer.Handle, buffer.Memory, 0));
        if (mapped)
            Check(vkMapMemory(Device, buffer.Memory, 0, buffer.Capacity, 0, &buffer.Mapped));
    }
    Invocation& Acquire(uint32_t frameSlot, uint64_t frameId) {
        auto& frame = Frames[frameSlot % Frames.size()];
        if (frame.Id != frameId) {
            frame.Id = frameId;
            frame.Used = 0;
        }
        if (frame.Used == frame.Invocations.size())
            frame.Invocations.push_back(std::make_unique<Invocation>());
        auto& invocation = *frame.Invocations[frame.Used++];
        if (!invocation.Pool) {
            const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 },
                                                               { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
                                                               { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 } } };
            VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pool.maxSets = 2;
            pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
            pool.pPoolSizes = sizes.data();
            Check(vkCreateDescriptorPool(Device, &pool, nullptr, &invocation.Pool));
            const std::array<VkDescriptorSetLayout, 2> layouts{ DescriptorLayout, DescriptorLayout };
            std::array<VkDescriptorSet, 2> sets{};
            VkDescriptorSetAllocateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            set.descriptorPool = invocation.Pool;
            set.descriptorSetCount = 2;
            set.pSetLayouts = layouts.data();
            Check(vkAllocateDescriptorSets(Device, &set, sets.data()));
            invocation.Set = sets[0];
            invocation.StaticSet = sets[1];
        }
        return invocation;
    }
};

GrassShadowGridPass::GrassShadowGridPass() : mImpl(std::make_unique<Impl>()) {
}
GrassShadowGridPass::~GrassShadowGridPass() {
    Shutdown();
}
bool GrassShadowGridPass::Available() const noexcept {
    return mImpl->Pipeline != VK_NULL_HANDLE;
}

void GrassShadowGridPass::Initialize(VkPhysicalDevice physical, VkDevice device,
                                     Renderer::CachedPassShaderCompiler& shaders,
                                     VkPipelineCache pipelineCache) {
    Shutdown();
    auto& p = *mImpl;
    p.Device = device;
    vkGetPhysicalDeviceMemoryProperties(physical, &p.Memory);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    p.MaximumStorageRange = properties.limits.maxStorageBufferRange;
    p.DispatchCapacity = static_cast<uint32_t>(std::min<uint64_t>(
        uint64_t(properties.limits.maxComputeWorkGroupCount[0]) * 64U, std::numeric_limits<uint32_t>::max() - 63U));
    const std::array<VkDescriptorSetLayoutBinding, 7> bindings{
        { { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
          { 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } }
    };
    VkDescriptorSetLayoutCreateInfo descriptor{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptor.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptor.pBindings = bindings.data();
    Check(vkCreateDescriptorSetLayout(device, &descriptor, nullptr, &p.DescriptorLayout));
    VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 4 };
    VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &p.DescriptorLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(device, &layout, nullptr, &p.Layout));
    const auto words =
        shaders.Resolve(BuildGrassShadowGridComputeShader(), Renderer::SpirvStage::Compute, "grass_shadow_grid.comp");
    VkShaderModuleCreateInfo module{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    module.codeSize = words.size() * sizeof(uint32_t);
    module.pCode = words.data();
    VkShaderModule shader{};
    Check(vkCreateShaderModule(device, &module, nullptr, &shader));
    VkComputePipelineCreateInfo pipeline{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipeline.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                       nullptr,
                       0,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       shader,
                       "main",
                       nullptr };
    pipeline.layout = p.Layout;
    const auto result = vkCreateComputePipelines(device, pipelineCache, 1, &pipeline, nullptr, &p.Pipeline);
    vkDestroyShaderModule(device, shader, nullptr);
    Check(result);
}

GrassShadowGridResult GrassShadowGridPass::Execute(VkCommandBuffer command, uint32_t frameSlot, uint64_t frameId,
                                                   uint64_t revision, std::span<const GrassShadowGridPoint> points,
                                                   std::span<const GrassShadowGridReference> references,
                                                   std::span<const GrassShadowSurface> surfaces,
                                                   const std::array<Renderer3ds::ShadowBounds, 2>& focus,
                                                   const EffectGeometryProviderPlan& plan,
                                                   const NriDirectionalShadowHistorySnapshot& history) {
    if (!plan.Valid() || !plan.Requires(EffectResource::DirectionalShadowHistory) ||
        !plan.Requires(EffectResource::DirectionalShadowStaticHistory) || !history.StaticValid() ||
        history.State.ProducedFrameId > frameId || history.State.Strength <= 0 || !Available() ||
        command == VK_NULL_HANDLE || points.empty() || references.empty() || surfaces.empty() || revision == 0)
        throw std::runtime_error("grass shadow grid invocation lacks declared receiver inputs");
    auto& p = *mImpl;
    if (points.size_bytes() > p.MaximumStorageRange || references.size_bytes() > p.MaximumStorageRange)
        throw std::runtime_error("grass shadow grid exceeds device storage range");
    if (points.size() > std::numeric_limits<uint32_t>::max() - kGrassShadowRefinementPointBudget)
        throw std::runtime_error("grass shadow grid exceeds uint32 addressing");
    // A current pose must not refine an older immutable atlas snapshot.
    BuildGrassShadowRefinement(surfaces,
        history.State.ProducedFrameId == frameId ? focus : std::array<Renderer3ds::ShadowBounds, 2>{},
        history.State.WorldLightDirectionTowardSource, p.Refinement);
    const auto& refinement = p.Refinement;
    const auto coarseCount = static_cast<uint32_t>(points.size());
    const uint32_t count = coarseCount + refinement.PointCount;
    const VkDeviceSize surfaceBytes = refinement.SurfacePatches.size() * sizeof(refinement.SurfacePatches[0]);
    const VkDeviceSize patchBytes = std::max<size_t>(1, refinement.Patches.size()) * sizeof(GrassShadowRefinementPatch);
    const VkDeviceSize visibilityBytes = uint64_t{count} * sizeof(GrassShadowGridVisibility);
    if (surfaceBytes > p.MaximumStorageRange || patchBytes > p.MaximumStorageRange ||
        visibilityBytes > p.MaximumStorageRange)
        throw std::runtime_error("grass refined shadow grid exceeds device storage range");
    auto& invocation = p.Acquire(frameSlot, frameId);
    uint64_t uploaded = 0;
    if (invocation.Revision != revision) {
        p.Ensure(invocation.Points, points.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        p.Ensure(invocation.References, references.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        std::memcpy(invocation.Points.Mapped, points.data(), points.size_bytes());
        std::memcpy(invocation.References.Mapped, references.data(), references.size_bytes());
        uploaded = points.size_bytes() + references.size_bytes();
        invocation.Revision = revision;
    }
    uint64_t dynamicUploaded = sizeof(PicaDirectionalShadowReceiverUniforms);
    p.Ensure(invocation.SurfacePatches, surfaceBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    if (invocation.SurfaceCount != surfaces.size() ||
        std::memcmp(invocation.SurfacePatches.Mapped, refinement.SurfacePatches.data(), surfaceBytes) != 0) {
        std::memcpy(invocation.SurfacePatches.Mapped, refinement.SurfacePatches.data(), surfaceBytes);
        invocation.SurfaceCount = surfaces.size();
        dynamicUploaded += surfaceBytes;
    }
    p.Ensure(invocation.Patches, patchBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    auto* patches = static_cast<GrassShadowRefinementPatch*>(invocation.Patches.Mapped);
    for (size_t i = 0; i < refinement.Patches.size(); ++i) {
        patches[i] = refinement.Patches[i];
        patches[i].Address[0] += coarseCount;
    }
    dynamicUploaded += refinement.Patches.size() * sizeof(GrassShadowRefinementPatch);
    p.Ensure(invocation.Visibility, visibilityBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    p.Ensure(invocation.Uniforms, sizeof(PicaDirectionalShadowReceiverUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
             true);
    p.Ensure(invocation.StaticUniforms, sizeof(PicaDirectionalShadowReceiverUniforms),
             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    const DirectionalShadowMatrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const auto uniforms = PackPicaDirectionalShadowReceiverUniforms(identity, history.State);
    std::memcpy(invocation.Uniforms.Mapped, &uniforms, uniforms.UsedBytes());
    // The static depth: the same layouts and pages, each layout in its own
    // atlas of one region.
    auto staticState = history.State;
    staticState.AtlasRegions = 1U;
    staticState.Region = 0U;
    if (staticState.Older) {
        auto older = std::make_shared<PicaDirectionalShadowOlderLayout>(*staticState.Older);
        older->Region = 0U;
        staticState.Older = std::move(older);
    }
    const auto staticUniforms = PackPicaDirectionalShadowReceiverUniforms(identity, staticState);
    std::memcpy(invocation.StaticUniforms.Mapped, &staticUniforms, staticUniforms.UsedBytes());
    dynamicUploaded += sizeof(PicaDirectionalShadowReceiverUniforms);
    const std::array<VkDescriptorBufferInfo, 5> buffers{ { { invocation.Points.Handle, 0, points.size_bytes() },
                                                           { invocation.Visibility.Handle, 0, visibilityBytes },
                                                           { invocation.Uniforms.Handle, 0, sizeof(uniforms) },
                                                           { invocation.SurfacePatches.Handle, 0, surfaceBytes },
                                                           { invocation.Patches.Handle, 0, patchBytes } } };
    const VkDescriptorImageInfo atlas{ history.Sampler, history.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo staticAtlas{ history.Sampler, history.StaticViews[0],
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo staticOlderAtlas{
        history.Sampler, history.StaticViews[1] != VK_NULL_HANDLE ? history.StaticViews[1] : history.StaticViews[0],
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorBufferInfo staticUniformBuffer{ invocation.StaticUniforms.Handle, 0, sizeof(staticUniforms) };
    std::array<VkWriteDescriptorSet, 14> writes{};
    for (size_t i = 0; i < writes.size(); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = i < 7U ? invocation.Set : invocation.StaticSet;
        writes[i].descriptorCount = 1;
    }
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &buffers[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &buffers[1];
    writes[2].dstBinding = 10;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &atlas;
    writes[3].dstBinding = 12;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].pBufferInfo = &buffers[2];
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo = &buffers[3];
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo = &buffers[4];
    // The composed map holds both layouts' regions.
    writes[6].dstBinding = 11;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].pImageInfo = &atlas;
    // The coarse grid's set: the same buffers, the static depth.
    for (size_t i = 0; i < 7U; ++i) {
        const auto set = writes[7U + i].dstSet;
        writes[7U + i] = writes[i];
        writes[7U + i].dstSet = set;
    }
    writes[9].pImageInfo = &staticAtlas;
    writes[10].pBufferInfo = &staticUniformBuffer;
    writes[13].pImageInfo = &staticOlderAtlas;
    vkUpdateDescriptorSets(p.Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    // External atlas transitions belong to the graph's VSM producer. Its
    // ShaderRead completion includes vertex, fragment and compute stages.
    VkMemoryBarrier upload{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    upload.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    upload.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &upload, 0,
                         nullptr, 0, nullptr);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.Pipeline);
    // The coarse grid without the actors, then the refinement around them.
    const std::array<std::pair<VkDescriptorSet, std::array<uint32_t, 2>>, 2> ranges{
        { { invocation.StaticSet, { 0U, coarseCount } }, { invocation.Set, { coarseCount, count } } } };
    for (const auto& [set, range] : ranges) {
        if (range[0] == range[1])
            continue;
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, p.Layout, 0, 1, &set, 0, nullptr);
        for (uint32_t first = range[0]; first < range[1];) {
            const uint32_t batch = std::min(range[1] - first, p.DispatchCapacity);
            const std::array<uint32_t, 4> push{ first + batch, first, coarseCount,
                                              static_cast<uint32_t>(refinement.Patches.size()) };
            vkCmdPushConstants(command, p.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push.data());
            vkCmdDispatch(command, (batch + 63U) / 64U, 1, 1);
            first += batch;
        }
    }
    VkBufferMemoryBarrier ready{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ready.srcQueueFamilyIndex = ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ready.buffer = invocation.Visibility.Handle;
    ready.size = visibilityBytes;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 1, &ready, 0, nullptr);
    return { { invocation.Visibility.Handle, 0, visibilityBytes },
             { invocation.References.Handle, 0, references.size_bytes() },
             buffers[4], coarseCount, refinement.PointCount,
             static_cast<uint32_t>(refinement.Patches.size()), refinement.CellSize,
             uploaded, dynamicUploaded };
}

void GrassShadowGridPass::Shutdown() {
    auto& p = *mImpl;
    if (p.Device) {
        for (auto& frame : p.Frames)
            for (auto& entry : frame.Invocations) {
                p.Destroy(entry->Points);
                p.Destroy(entry->References);
                p.Destroy(entry->Visibility);
                p.Destroy(entry->Uniforms);
                p.Destroy(entry->StaticUniforms);
                p.Destroy(entry->SurfacePatches);
                p.Destroy(entry->Patches);
                if (entry->Pool)
                    vkDestroyDescriptorPool(p.Device, entry->Pool, nullptr);
            }
        if (p.Pipeline)
            vkDestroyPipeline(p.Device, p.Pipeline, nullptr);
        if (p.Layout)
            vkDestroyPipelineLayout(p.Device, p.Layout, nullptr);
        if (p.DescriptorLayout)
            vkDestroyDescriptorSetLayout(p.Device, p.DescriptorLayout, nullptr);
    }
    mImpl = std::make_unique<Impl>();
}
} // namespace Fast::Oot3d
#endif
