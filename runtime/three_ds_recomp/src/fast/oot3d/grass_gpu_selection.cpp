#include "fast/oot3d/grass_gpu_selection.h"
#include "fast/oot3d/grass_shader_sources.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kGroupSize = 256U;
constexpr uint32_t kCounters = 8U;
constexpr uint32_t kRecordWords = 6U;
constexpr uint32_t kHeaderWords = 64U;
constexpr uint32_t kSourceWords = 32U;
constexpr uint32_t kControlWords = 1024U;
constexpr uint32_t kBindings = 8U;
constexpr uint32_t kNoSlot = 0xFFU;
static_assert(sizeof(GrassWorldCluster) == 8U * sizeof(uint32_t));

struct alignas(16) SelectionPolicy {
    std::array<float, 4> Distance{};
    std::array<float, 4> Lod{};
    std::array<float, 4> Tuft{};
    std::array<uint32_t, 4> Counts{};
};
static_assert(sizeof(SelectionPolicy) == 64U);

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t mask, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0U; index < properties.memoryTypeCount; ++index)
        if ((mask & (1U << index)) != 0U && (properties.memoryTypes[index].propertyFlags & required) == required)
            return index;
    throw std::runtime_error("grass selection has no compatible memory type");
}

} // namespace

struct GrassGpuSelector::Impl {
    struct Buffer {
        VkBuffer Handle = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0U;
        void* Mapped = nullptr;
    };
    struct SlotState {
        bool Recorded = false;
        uint32_t Sources = 0U;
        uint32_t Slots = 0U;
        bool Visible = false;
    };

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFrameSlots> DescriptorSets{};
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> Pipelines{};
    Buffer Clusters;
    std::array<Buffer, kFrameSlots> ClusterStaging{};
    Buffer AnchorSlots;
    Buffer Records;
    Buffer Groups;
    std::array<Buffer, kFrameSlots> Sources{};
    std::array<Buffer, kFrameSlots> Visible{};
    std::array<Buffer, kFrameSlots> Control{};
    std::array<Buffer, kFrameSlots> Readback{};
    std::array<SlotState, kFrameSlots> SlotStates{};
    uint64_t ClusterRevision = 0U;
    uint32_t ClusterSlots = 0U;
    std::vector<uint32_t> GroupBase;
    std::vector<uint32_t> GroupCount;
    std::string Reason;

    void DestroyBuffer(Buffer& buffer) {
        if (buffer.Mapped != nullptr) vkUnmapMemory(Device, buffer.Memory);
        if (buffer.Handle != VK_NULL_HANDLE) vkDestroyBuffer(Device, buffer.Handle, nullptr);
        if (buffer.Memory != VK_NULL_HANDLE) vkFreeMemory(Device, buffer.Memory, nullptr);
        buffer = {};
    }

    // Shared (not per frame slot) buffers may still be read by the previous
    // frame: growing them waits for the device, as the compactor does.
    bool EnsureBuffer(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      bool waitBeforeResize) {
        if (buffer.Handle != VK_NULL_HANDLE && bytes <= buffer.Capacity) return false;
        if (waitBeforeResize && buffer.Handle != VK_NULL_HANDLE) vkDeviceWaitIdle(Device);
        DestroyBuffer(buffer);
        buffer.Capacity = std::bit_ceil(std::max<VkDeviceSize>(bytes, 4096U));
        const VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0U, buffer.Capacity, usage,
                                      VK_SHARING_MODE_EXCLUSIVE, 0U, nullptr};
        if (vkCreateBuffer(Device, &info, nullptr, &buffer.Handle) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass selection buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, buffer.Handle, &requirements);
        const VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            FindMemoryType(PhysicalDevice, requirements.memoryTypeBits, properties)};
        if (vkAllocateMemory(Device, &allocation, nullptr, &buffer.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(Device, buffer.Handle, buffer.Memory, 0U) != VK_SUCCESS)
            throw std::runtime_error("cannot allocate grass selection buffer");
        if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U &&
            vkMapMemory(Device, buffer.Memory, 0U, buffer.Capacity, 0U, &buffer.Mapped) != VK_SUCCESS)
            throw std::runtime_error("cannot map grass selection buffer");
        return true;
    }
};

GrassGpuSelector::GrassGpuSelector() : mImpl(std::make_unique<Impl>()) {}

GrassGpuSelector::~GrassGpuSelector() { Shutdown(); }

bool GrassGpuSelector::Available() const noexcept { return mImpl->Pipelines[0] != VK_NULL_HANDLE; }

const std::string& GrassGpuSelector::UnavailableReason() const noexcept { return mImpl->Reason; }

bool GrassGpuSelector::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                  Renderer::CachedPassShaderCompiler& shaders, VkPipelineCache pipelineCache) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    try {
        std::array<VkDescriptorSetLayoutBinding, kBindings> bindings{};
        for (uint32_t index = 0U; index < kBindings; ++index)
            bindings[index] = {index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        const VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr,
                                                         0U, kBindings, bindings.data()};
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mImpl->DescriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass selection descriptor layout");
        const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBindings * kFrameSlots};
        const VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0U,
                                                  kFrameSlots, 1U, &poolSize};
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &mImpl->DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass selection descriptor pool");
        std::array<VkDescriptorSetLayout, kFrameSlots> layouts{};
        layouts.fill(mImpl->DescriptorSetLayout);
        const VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                                       mImpl->DescriptorPool, kFrameSlots, layouts.data()};
        if (vkAllocateDescriptorSets(device, &allocateInfo, mImpl->DescriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("cannot allocate grass selection descriptors");
        const VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(SelectionPolicy)};
        const VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0U,
                                                        1U, &mImpl->DescriptorSetLayout, 1U, &pushRange};
        if (vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &mImpl->PipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("cannot create grass selection pipeline layout");
        constexpr std::array<const char*, 3> names{"grass_selection_count.comp", "grass_selection_scan.comp",
                                                   "grass_selection_scatter.comp"};
        const auto source = BuildGrassSelectionComputeShader();
        for (uint32_t pass = 0U; pass < 3U; ++pass) {
            const auto words = shaders.Resolve(source, Renderer::SpirvStage::Compute, names[pass],
                                               {{"GRASS_SELECTION_PASS", std::to_string(pass)}});
            const VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0U,
                                                      words.size() * sizeof(uint32_t), words.data()};
            VkShaderModule module = VK_NULL_HANDLE;
            if (words.empty() || vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
                throw std::runtime_error("cannot create grass selection shader");
            const VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0U,
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U, VK_SHADER_STAGE_COMPUTE_BIT,
                 module, "main", nullptr},
                mImpl->PipelineLayout, VK_NULL_HANDLE, -1};
            const auto created = vkCreateComputePipelines(device, pipelineCache, 1U, &pipelineInfo, nullptr,
                                                          &mImpl->Pipelines[pass]);
            vkDestroyShaderModule(device, module, nullptr);
            if (created != VK_SUCCESS)
                throw std::runtime_error("cannot create grass selection pipeline");
        }
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
}

GrassGpuSelectionReadback GrassGpuSelector::ReadBack(uint32_t frameSlot) const {
    GrassGpuSelectionReadback readback;
    const uint32_t slot = frameSlot % kFrameSlots;
    const auto& state = mImpl->SlotStates[slot];
    const auto& buffer = mImpl->Readback[slot];
    if (!state.Recorded || buffer.Mapped == nullptr) return readback;
    const auto* words = static_cast<const uint32_t*>(buffer.Mapped);
    readback.Available = true;
    readback.Admitted = words[3];
    for (uint32_t s = 0U; s < state.Sources; ++s)
        for (uint32_t k = 0U; k < state.Slots; ++k)
            readback.Counts[s][k] = words[GrassGpuSelectionDrawOffset(s, k) / sizeof(uint32_t) + 1U];
    if (state.Visible && kControlWords + readback.Admitted <= buffer.Capacity / sizeof(uint32_t))
        readback.Visible.assign(words + kControlWords, words + kControlWords + readback.Admitted);
    return readback;
}

bool GrassGpuSelector::Record(const GrassGpuSelectionRequest& request, GrassGpuSelectionResult& result) {
    result = {};
    if (!Available() || request.CommandBuffer == VK_NULL_HANDLE || request.StaticAnchors == VK_NULL_HANDLE ||
        request.Sources.empty() || request.Sources.size() > kGrassGpuSelectionMaximumSources ||
        request.Slots.empty() || request.Slots.size() > kGrassGpuSelectionMaximumSlots || request.Budget == 0U ||
        request.StaticAnchorCount == 0U || request.StaticAnchorCount > std::numeric_limits<uint32_t>::max())
        return false;
    try {
        auto& impl = *mImpl;
        const uint32_t slot = request.FrameSlot % kFrameSlots;
        const auto cmd = request.CommandBuffer;
        constexpr VkMemoryPropertyFlags kDevice = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        constexpr VkMemoryPropertyFlags kHost = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        // Every shared buffer below may still be in use by the previous frame.
        const VkMemoryBarrier previous{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 1U, &previous, 0U, nullptr, 0U,
            nullptr);

        // Clusters of the active sources in one flat index space, each source
        // padded to whole workgroups.
        std::vector<uint32_t> groupBase(request.Sources.size()), groupCount(request.Sources.size());
        uint32_t groups = 0U;
        for (size_t s = 0; s < request.Sources.size(); ++s) {
            const auto count = request.Sources[s].Clusters.size();
            if (count > (std::numeric_limits<uint32_t>::max() / 2U) - groups * size_t(kGroupSize))
                throw std::runtime_error("grass selection cluster count overflow");
            groupBase[s] = groups;
            groupCount[s] = static_cast<uint32_t>((count + kGroupSize - 1U) / kGroupSize);
            groups += groupCount[s];
        }
        if (groups == 0U) return false;
        const uint32_t clusterSlots = groups * kGroupSize;
        if (impl.ClusterRevision != request.StaticRevision || impl.ClusterSlots != clusterSlots ||
            impl.GroupBase != groupBase) {
            const VkDeviceSize clusterBytes = VkDeviceSize(clusterSlots) * sizeof(GrassWorldCluster);
            const bool resized = impl.EnsureBuffer(impl.Clusters, clusterBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kDevice, true);
            (void)resized;
            auto& stagingBuffer = impl.ClusterStaging[slot];
            impl.EnsureBuffer(stagingBuffer, clusterBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kHost, false);
            auto* staging = static_cast<GrassWorldCluster*>(stagingBuffer.Mapped);
            std::memset(staging, 0, static_cast<size_t>(clusterBytes));
            for (size_t s = 0; s < request.Sources.size(); ++s) {
                const auto clusters = request.Sources[s].Clusters;
                std::memcpy(staging + size_t(groupBase[s]) * kGroupSize, clusters.data(), clusters.size_bytes());
            }
            const VkBufferCopy copy{0U, 0U, clusterBytes};
            vkCmdCopyBuffer(cmd, stagingBuffer.Handle, impl.Clusters.Handle, 1U, &copy);
            impl.ClusterRevision = request.StaticRevision;
            impl.ClusterSlots = clusterSlots;
            impl.GroupBase = groupBase;
        }
        impl.EnsureBuffer(impl.AnchorSlots, request.StaticAnchorCount * sizeof(uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kDevice, true);
        impl.EnsureBuffer(impl.Records, VkDeviceSize(clusterSlots) * kRecordWords * sizeof(uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kDevice, true);
        impl.EnsureBuffer(impl.Groups, VkDeviceSize(groups) * 2U * kCounters * sizeof(uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kDevice, true);
        const VkDeviceSize visibleBytes = VkDeviceSize(request.Budget) * sizeof(uint32_t);
        impl.EnsureBuffer(impl.Visible[slot], visibleBytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kDevice, false);
        impl.EnsureBuffer(impl.Control[slot], kControlWords * sizeof(uint32_t),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kDevice, false);
        const VkDeviceSize readbackBytes =
            (kControlWords + (request.ReadBackVisible ? request.Budget : 0U)) * VkDeviceSize(sizeof(uint32_t));
        impl.EnsureBuffer(impl.Readback[slot], readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHost, false);

        // Header, slot table and per-source views.
        const VkDeviceSize sourceBytes = (kHeaderWords + kSourceWords * request.Sources.size()) * sizeof(uint32_t);
        impl.EnsureBuffer(impl.Sources[slot], sourceBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHost, false);
        auto* words = static_cast<uint32_t*>(impl.Sources[slot].Mapped);
        std::memset(words, 0, static_cast<size_t>(sourceBytes));
        words[0] = static_cast<uint32_t>(request.Sources.size());
        words[1] = groups;
        words[2] = static_cast<uint32_t>(request.Slots.size());
        words[3] = request.Budget;
        for (uint32_t bin = 0U; bin < kGrassGpuSelectionBinCount; ++bin) words[4U + bin] = kNoSlot;
        for (uint32_t k = 0U; k < request.Slots.size(); ++k) {
            const auto& topology = request.Slots[k];
            if (topology.Bin >= kGrassGpuSelectionBinCount || (k != 0U && request.Slots[k - 1U].Bin >= topology.Bin))
                throw std::runtime_error("grass selection slots must be distinct bins in bin order");
            words[4U + topology.Bin] = k;
            words[32U + k * 2U] = topology.IndexCount;
            words[33U + k * 2U] = topology.FirstIndex;
        }
        for (size_t s = 0; s < request.Sources.size(); ++s) {
            const auto& source = request.Sources[s];
            auto* record = words + kHeaderWords + kSourceWords * s;
            std::memcpy(record, source.PositionToClip.data(), sizeof(source.PositionToClip));
            std::memcpy(record + 16, source.Eye.data(), sizeof(source.Eye));
            const auto scale = BuildGrassFrustumRadiusScale(source.PositionToClip);
            const std::array<float, 3> radiusScale{scale.X, scale.Y, scale.W};
            std::memcpy(record + 19, radiusScale.data(), sizeof(radiusScale));
            record[22] = source.StaticBase;
            record[24] = static_cast<uint32_t>(source.Clusters.size());
            record[25] = groupBase[s];
            record[26] = groupCount[s];
        }

        const std::array<VkDescriptorBufferInfo, kBindings> infos{{
            {request.StaticAnchors, 0U, VkDeviceSize(request.StaticAnchorCount) * sizeof(GrassWorldAnchor)},
            {impl.Clusters.Handle, 0U, VkDeviceSize(clusterSlots) * sizeof(GrassWorldCluster)},
            {impl.Sources[slot].Handle, 0U, sourceBytes},
            {impl.AnchorSlots.Handle, 0U, request.StaticAnchorCount * sizeof(uint32_t)},
            {impl.Records.Handle, 0U, VkDeviceSize(clusterSlots) * kRecordWords * sizeof(uint32_t)},
            {impl.Groups.Handle, 0U, VkDeviceSize(groups) * 2U * kCounters * sizeof(uint32_t)},
            {impl.Visible[slot].Handle, 0U, visibleBytes},
            {impl.Control[slot].Handle, 0U, kControlWords * sizeof(uint32_t)},
        }};
        std::array<VkWriteDescriptorSet, kBindings> writes{};
        for (uint32_t index = 0U; index < kBindings; ++index)
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, impl.DescriptorSets[slot], index, 0U,
                             1U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &infos[index], nullptr};
        vkUpdateDescriptorSets(impl.Device, kBindings, writes.data(), 0U, nullptr);

        const auto& policy = request.Policy;
        SelectionPolicy push;
        const float reference = policy.LodReferenceDistance > 0.0F ? policy.LodReferenceDistance : policy.DrawDistance;
        push.Distance = {policy.DrawDistance, request.DrawDistanceSquared, reference, policy.LodStart};
        push.Lod = {policy.LodEnd, policy.FarDensity, policy.SegmentStartDistance, policy.SegmentEndDistance};
        push.Tuft = {policy.TuftTransitionFraction, policy.FarTuftDensity, policy.SegmentLodSoftness,
                     request.BladeRadiusScale};
        push.Counts = {policy.NearBladeSegments, policy.FarBladeSegments,
                       (policy.FarTuftsEnabled ? 1U : 0U) | (request.FrustumCulling ? 2U : 0U),
                       policy.FarTuftBladeCount};

        const VkMemoryBarrier uploads{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 1U, &uploads, 0U, nullptr, 0U, nullptr);
        const VkMemoryBarrier between{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, impl.PipelineLayout, 0U, 1U,
                                &impl.DescriptorSets[slot], 0U, nullptr);
        for (uint32_t pass = 0U; pass < 3U; ++pass) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, impl.Pipelines[pass]);
            vkCmdPushConstants(cmd, impl.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(push), &push);
            vkCmdDispatch(cmd, pass == 1U ? 1U : groups, 1U, 1U);
            if (pass != 2U)
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0U, 1U, &between, 0U, nullptr, 0U, nullptr);
        }
        const VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0U, 1U, &ready, 0U, nullptr, 0U, nullptr);
        std::array<VkBufferCopy, 1> controlCopy{{{0U, 0U, kControlWords * sizeof(uint32_t)}}};
        vkCmdCopyBuffer(cmd, impl.Control[slot].Handle, impl.Readback[slot].Handle, 1U, controlCopy.data());
        if (request.ReadBackVisible) {
            const VkBufferCopy visibleCopy{0U, kControlWords * sizeof(uint32_t), visibleBytes};
            vkCmdCopyBuffer(cmd, impl.Visible[slot].Handle, impl.Readback[slot].Handle, 1U, &visibleCopy);
        }
        const VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &host, 0U,
                             nullptr, 0U, nullptr);
        impl.SlotStates[slot] = {true, static_cast<uint32_t>(request.Sources.size()),
                                 static_cast<uint32_t>(request.Slots.size()), request.ReadBackVisible};
        result.Visible = impl.Visible[slot].Handle;
        result.Control = impl.Control[slot].Handle;
        result.Capacity = request.Budget;
        impl.Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
}

void GrassGpuSelector::Shutdown() {
    if (!mImpl || mImpl->Device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(mImpl->Device);
    auto& impl = *mImpl;
    for (auto* buffer : {&impl.Clusters, &impl.AnchorSlots, &impl.Records, &impl.Groups})
        impl.DestroyBuffer(*buffer);
    for (auto* set : {&impl.ClusterStaging, &impl.Sources, &impl.Visible, &impl.Control, &impl.Readback})
        for (auto& buffer : *set) impl.DestroyBuffer(buffer);
    for (auto& pipeline : impl.Pipelines) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(impl.Device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (impl.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(impl.Device, impl.PipelineLayout, nullptr);
    if (impl.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(impl.Device, impl.DescriptorPool, nullptr);
    if (impl.DescriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(impl.Device, impl.DescriptorSetLayout, nullptr);
    impl.PipelineLayout = VK_NULL_HANDLE;
    impl.DescriptorPool = VK_NULL_HANDLE;
    impl.DescriptorSetLayout = VK_NULL_HANDLE;
    impl.DescriptorSets = {};
    impl.SlotStates = {};
    impl.ClusterRevision = 0U;
    impl.ClusterSlots = 0U;
    impl.GroupBase.clear();
    impl.Device = VK_NULL_HANDLE;
}

} // namespace Fast::Oot3d

#endif
