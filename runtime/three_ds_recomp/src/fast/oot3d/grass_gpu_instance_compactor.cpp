#include "fast/oot3d/grass_gpu_instance_compactor.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/grass_instance_layout.h"

#ifdef ENABLE_OOT3D_VULKAN


#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kComputeGroupSize = 256U;
constexpr VkDeviceSize kGrassInstanceBytes = sizeof(GrassInstance);
constexpr VkDeviceSize kStaticAnchorBytes = sizeof(GrassWorldAnchor);

struct alignas(16) GrassGpuInteractionSample {
    std::array<float, 4> Value{};
};
static_assert(sizeof(GrassGpuInteractionSample) == 16U);

struct alignas(16) GrassGpuActor {
    std::array<float, 4> PreviousRadius;
    std::array<float, 4> CurrentHalfHeight;
    std::array<float, 4> VelocityTeleported;
};
static_assert(sizeof(GrassGpuActor) == 48U);

struct alignas(16) GrassGpuCompactionPush {
    // visible count, interaction resolution, static count, flags
    std::array<uint32_t, 4> CountsFlags{};
    // interaction center X/Z, extent, vertical tolerance
    std::array<float, 4> Interaction{};
    // radius scale, height scale, push, velocity response
    std::array<float, 4> Collision{};
    // maximum bend, vertical margin
    std::array<float, 4> Bend{};
    // current frame stamp, previous frame stamp (0 without continuity)
    std::array<uint32_t, 4> History{};
};
static_assert(sizeof(GrassGpuCompactionPush) == 80U);

// Bend history entry: bend XY bits, frame stamp, replaced bend (half2).
constexpr VkDeviceSize kBendHistoryBytes = 4U * sizeof(uint32_t);
constexpr VkDeviceSize kPreviousBendBytes = 2U * sizeof(float);
constexpr uint32_t kDescriptorBindings = 7U;

// Never zero, so a cleared history entry matches no frame.
uint32_t BendHistoryStamp(uint64_t frameId) {
    return static_cast<uint32_t>(frameId) | 0x80000000U;
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t mask, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
        if ((mask & (1U << index)) != 0U && (properties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("grass compactor has no compatible memory type");
}

VkShaderModule CompileCompute(Renderer::CachedPassShaderCompiler& shaders, VkDevice device, const char* source) {
    const auto words = shaders.Resolve(source, Renderer::SpirvStage::Compute, "grass_instance_compactor.comp");
    const VkShaderModuleCreateInfo info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0U, words.size() * sizeof(uint32_t), words.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("cannot create grass compaction shader");
    }
    return module;
}

} // namespace

struct GrassGpuInstanceCompactor::Impl {
    struct Buffer {
        VkBuffer Handle = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0U;
        void* Mapped = nullptr;
    };

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFrameSlots> DescriptorSets{};
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    Buffer StaticAnchors;
    std::array<Buffer, kFrameSlots> StaticStaging{};
    std::array<Buffer, kFrameSlots> VisibleIndices{};
    std::array<Buffer, kFrameSlots> InteractionSamples{};
    std::array<Buffer, kFrameSlots> Actors{};
    std::array<Buffer, kFrameSlots> OutputInstances{};
    std::array<Buffer, kFrameSlots> PreviousBends{};
    Buffer BendHistory;
    struct SelectionSlot {
        uint64_t Static = 0, Selection = 0;
        size_t Count = 0;
        bool IdleOutput = false;
    };
    std::array<SelectionSlot, kFrameSlots> Selections{};
    uint64_t StaticRevision = 0U;
    size_t StaticAnchorCount = 0U;
    uint64_t HistoryFrameId = 0U;
    uint32_t HistoryPreviousStamp = 0U;
    bool HistoryValid = false;
    std::string Reason;

    void DestroyBuffer(Buffer& buffer) {
        if (buffer.Mapped != nullptr) {
            vkUnmapMemory(Device, buffer.Memory);
        }
        if (buffer.Handle != VK_NULL_HANDLE) {
            vkDestroyBuffer(Device, buffer.Handle, nullptr);
        }
        if (buffer.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(Device, buffer.Memory, nullptr);
        }
        buffer = {};
    }

    bool EnsureBuffer(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags memoryProperties, bool waitBeforeResize) {
        if (buffer.Handle != VK_NULL_HANDLE && bytes <= buffer.Capacity) {
            return false;
        }
        if (waitBeforeResize && buffer.Handle != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(Device);
        }
        DestroyBuffer(buffer);
        buffer.Capacity = std::bit_ceil(std::max<VkDeviceSize>(bytes, 4096U));
        const VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0U,
            buffer.Capacity,
            usage,
            VK_SHARING_MODE_EXCLUSIVE,
            0U,
            nullptr,
        };
        if (vkCreateBuffer(Device, &bufferInfo, nullptr, &buffer.Handle) != VK_SUCCESS) {
            throw std::runtime_error("cannot create grass compaction buffer");
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, buffer.Handle, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            FindMemoryType(PhysicalDevice, requirements.memoryTypeBits, memoryProperties),
        };
        if (vkAllocateMemory(Device, &allocation, nullptr, &buffer.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(Device, buffer.Handle, buffer.Memory, 0U) != VK_SUCCESS) {
            throw std::runtime_error("cannot allocate grass compaction buffer");
        }
        if ((memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U &&
            vkMapMemory(Device, buffer.Memory, 0U, buffer.Capacity, 0U, &buffer.Mapped) != VK_SUCCESS) {
            throw std::runtime_error("cannot map grass compaction buffer");
        }
        return true;
    }
};

GrassGpuInstanceCompactor::GrassGpuInstanceCompactor() : mImpl(std::make_unique<Impl>()) {
}

GrassGpuInstanceCompactor::~GrassGpuInstanceCompactor() {
    Shutdown();
}

bool GrassGpuInstanceCompactor::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                          Renderer::CachedPassShaderCompiler& shaders,
                                          VkPipelineCache pipelineCache) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    try {

        const VkShaderModule shader = CompileCompute(shaders, device, BuildGrassCompactionComputeShader().c_str());
        try {
            std::array<VkDescriptorSetLayoutBinding, kDescriptorBindings> bindings{};
            for (uint32_t index = 0U; index < bindings.size(); ++index) {
                bindings[index] = {
                    index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_COMPUTE_BIT, nullptr,
                };
            }
            const VkDescriptorSetLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0U,
                static_cast<uint32_t>(bindings.size()),
                bindings.data(),
            };
            if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mImpl->DescriptorSetLayout) != VK_SUCCESS) {
                throw std::runtime_error("cannot create grass compactor descriptor layout");
            }
            const VkDescriptorPoolSize poolSize{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                static_cast<uint32_t>(bindings.size() * kFrameSlots),
            };
            const VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0U, kFrameSlots, 1U, &poolSize,
            };
            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &mImpl->DescriptorPool) != VK_SUCCESS) {
                throw std::runtime_error("cannot create grass compactor descriptor pool");
            }
            std::array<VkDescriptorSetLayout, kFrameSlots> layouts{};
            layouts.fill(mImpl->DescriptorSetLayout);
            const VkDescriptorSetAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                nullptr,
                mImpl->DescriptorPool,
                kFrameSlots,
                layouts.data(),
            };
            if (vkAllocateDescriptorSets(device, &allocateInfo, mImpl->DescriptorSets.data()) != VK_SUCCESS) {
                throw std::runtime_error("cannot allocate grass compactor descriptors");
            }
            const VkPushConstantRange pushRange{
                VK_SHADER_STAGE_COMPUTE_BIT,
                0U,
                sizeof(GrassGpuCompactionPush),
            };
            const VkPipelineLayoutCreateInfo pipelineLayout{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                nullptr,
                0U,
                1U,
                &mImpl->DescriptorSetLayout,
                1U,
                &pushRange,
            };
            if (vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &mImpl->PipelineLayout) != VK_SUCCESS) {
                throw std::runtime_error("cannot create grass compactor pipeline layout");
            }
            const VkPipelineShaderStageCreateInfo stage{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0U,
                VK_SHADER_STAGE_COMPUTE_BIT,
                shader,
                "main",
                nullptr,
            };
            const VkComputePipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                nullptr,
                0U,
                stage,
                mImpl->PipelineLayout,
                VK_NULL_HANDLE,
                -1,
            };
            if (vkCreateComputePipelines(device, pipelineCache, 1U, &pipelineInfo, nullptr, &mImpl->Pipeline) !=
                VK_SUCCESS) {
                throw std::runtime_error("cannot create grass compactor pipeline");
            }
        } catch (...) {
            vkDestroyShaderModule(device, shader, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, shader, nullptr);
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
}

bool GrassGpuInstanceCompactor::Compact(const GrassGpuInstanceCompactionRequest& request,
                                        GrassGpuInstanceCompactionResult& result) {
    result = {};
    if (mImpl->Pipeline == VK_NULL_HANDLE || request.CommandBuffer == VK_NULL_HANDLE || request.Settings == nullptr ||
        request.InteractionField == nullptr || request.StaticRevision == 0U || request.StaticAnchors.empty() ||
        request.VisibleAnchorIndices.empty()) {
        return false;
    }
    if (request.VisibleAnchorIndices.size() > std::numeric_limits<uint32_t>::max() ||
        request.StaticAnchors.size() > std::numeric_limits<uint32_t>::max()) {
        mImpl->Reason = "grass compaction input exceeds uint32 range";
        return false;
    }
    try {
        const uint32_t slot = request.FrameSlot % kFrameSlots;
        const VkDeviceSize staticBytes = request.StaticAnchors.size() * kStaticAnchorBytes;
        const VkDeviceSize visibleBytes = request.VisibleAnchorIndices.size() * sizeof(uint32_t);
        const VkDeviceSize outputBytes = request.VisibleAnchorIndices.size() * kGrassInstanceBytes;
        auto& selection = mImpl->Selections[slot];
        const bool sameSelection = request.SelectionRevision != 0 && selection.Static == request.StaticRevision &&
                                   selection.Selection == request.SelectionRevision &&
                                   selection.Count == request.VisibleAnchorIndices.size();
        const bool idle = request.AllowIdleReuse && request.Actors.empty() && request.InteractionField->AtRest();
        if (sameSelection && idle && selection.IdleOutput && mImpl->StaticRevision == request.StaticRevision) {
            result.InstanceBuffer = mImpl->OutputInstances[slot].Handle;
            result.PreviousBendBuffer = mImpl->PreviousBends[slot].Handle;
            result.VisibleIndicesBuffer = mImpl->VisibleIndices[slot].Handle;
            result.InstanceCount = static_cast<uint32_t>(request.VisibleAnchorIndices.size());
            result.SelectionUploadReused = true;
            result.Reused = true;
            // This path is unavailable while motion vectors consume bend history.
            mImpl->HistoryValid = false;
            return true;
        }
        selection.IdleOutput = false;
        const auto displacements = request.InteractionField->Displacements();
        const auto heights = request.InteractionField->InteractionHeights();
        if (displacements.size() != heights.size() || displacements.empty()) {
            throw std::runtime_error("grass interaction field is incomplete");
        }
        const VkDeviceSize interactionBytes = displacements.size() * sizeof(GrassGpuInteractionSample);
        const VkDeviceSize actorBytes = std::max<size_t>(1, request.Actors.size()) * sizeof(GrassGpuActor);
        mImpl->EnsureBuffer(mImpl->Actors[slot], actorBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
        auto* actorOutput = static_cast<GrassGpuActor*>(mImpl->Actors[slot].Mapped);
        for (size_t i = 0; i < request.Actors.size(); ++i) {
            const auto& a = request.Actors[i];
            actorOutput[i] = {
                {a.PreviousPosition[0], a.PreviousPosition[1], a.PreviousPosition[2], a.Radius},
                {a.Position[0], a.Position[1], a.Position[2], a.HalfHeight},
                {a.Velocity[0], a.Velocity[1], a.Velocity[2], a.Teleported ? 1.0F : 0.0F},
            };
        }

        const bool staticChanged =
            mImpl->StaticRevision != request.StaticRevision || mImpl->StaticAnchorCount != request.StaticAnchors.size();
        bool staticResized = false;
        if (staticChanged) {
            staticResized = mImpl->EnsureBuffer(mImpl->StaticAnchors, staticBytes,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true);
            mImpl->EnsureBuffer(mImpl->StaticStaging[slot], staticBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
            std::memcpy(mImpl->StaticStaging[slot].Mapped, request.StaticAnchors.data(), static_cast<size_t>(staticBytes));
            if (!staticResized) {
                const VkBufferMemoryBarrier beforeCopy{
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    nullptr,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    mImpl->StaticAnchors.Handle,
                    0U,
                    staticBytes,
                };
                vkCmdPipelineBarrier(request.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 1U, &beforeCopy, 0U, nullptr);
            }
            const VkBufferCopy copy{ 0U, 0U, staticBytes };
            vkCmdCopyBuffer(request.CommandBuffer, mImpl->StaticStaging[slot].Handle, mImpl->StaticAnchors.Handle, 1U,
                            &copy);
            mImpl->StaticRevision = request.StaticRevision;
            mImpl->StaticAnchorCount = request.StaticAnchors.size();
            result.StaticUploadedBytes = static_cast<uint64_t>(staticBytes);
        }

        // The bend history follows static anchor indices: a new static set,
        // a new buffer or a temporal reset starts it over.
        const VkDeviceSize historyBytes = request.StaticAnchors.size() * kBendHistoryBytes;
        const bool historyResized = mImpl->EnsureBuffer(
            mImpl->BendHistory, historyBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true);
        const bool clearHistory = staticChanged || historyResized || !mImpl->HistoryValid;
        if (clearHistory) {
            if (!historyResized) {
                const VkBufferMemoryBarrier beforeClear{
                    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, mImpl->BendHistory.Handle, 0U, VK_WHOLE_SIZE,
                };
                vkCmdPipelineBarrier(request.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 1U, &beforeClear, 0U, nullptr);
            }
            vkCmdFillBuffer(request.CommandBuffer, mImpl->BendHistory.Handle, 0U, VK_WHOLE_SIZE, 0U);
        }
        uint32_t previousStamp = 0U;
        if (!clearHistory && mImpl->HistoryFrameId == request.FrameId) {
            previousStamp = mImpl->HistoryPreviousStamp;
        } else if (!clearHistory && mImpl->HistoryFrameId + 1U == request.FrameId) {
            previousStamp = BendHistoryStamp(mImpl->HistoryFrameId);
        }
        mImpl->HistoryFrameId = request.FrameId;
        mImpl->HistoryPreviousStamp = previousStamp;
        mImpl->HistoryValid = true;

        mImpl->EnsureBuffer(mImpl->VisibleIndices[slot], visibleBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
        if (!sameSelection)
            std::memcpy(mImpl->VisibleIndices[slot].Mapped, request.VisibleAnchorIndices.data(),
                        static_cast<size_t>(visibleBytes));
        result.SelectionUploadReused = sameSelection;
        mImpl->EnsureBuffer(mImpl->InteractionSamples[slot], interactionBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
        auto* interactionOutput = static_cast<GrassGpuInteractionSample*>(mImpl->InteractionSamples[slot].Mapped);
        for (size_t index = 0U; index < displacements.size(); ++index) {
            interactionOutput[index].Value = {
                displacements[index][0],
                displacements[index][1],
                heights[index],
                0.0F,
            };
        }
        mImpl->EnsureBuffer(mImpl->OutputInstances[slot], outputBytes,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
        const VkDeviceSize previousBendBytes = request.VisibleAnchorIndices.size() * kPreviousBendBytes;
        mImpl->EnsureBuffer(mImpl->PreviousBends[slot], previousBendBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);

        const std::array<VkDescriptorBufferInfo, kDescriptorBindings> bufferInfos{ {
            {
                mImpl->StaticAnchors.Handle,
                0U,
                staticBytes,
            },
            {
                mImpl->VisibleIndices[slot].Handle,
                0U,
                visibleBytes,
            },
            {
                mImpl->InteractionSamples[slot].Handle,
                0U,
                interactionBytes,
            },
            {
                mImpl->OutputInstances[slot].Handle,
                0U,
                outputBytes,
            },
            {mImpl->Actors[slot].Handle, 0U, actorBytes},
            {mImpl->BendHistory.Handle, 0U, historyBytes},
            {mImpl->PreviousBends[slot].Handle, 0U, previousBendBytes},
        } };
        std::array<VkWriteDescriptorSet, kDescriptorBindings> writes{};
        for (uint32_t index = 0U; index < writes.size(); ++index) {
            writes[index] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mImpl->DescriptorSets[slot], index,   0U, 1U,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &bufferInfos[index],         nullptr,
            };
        }
        vkUpdateDescriptorSets(mImpl->Device, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);

        std::array<VkBufferMemoryBarrier, 7U> barriers{};
        uint32_t barrierCount = 0U;
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            mImpl->Actors[slot].Handle, 0U, actorBytes,
        };
        if (staticChanged) {
            barriers[barrierCount++] = {
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                mImpl->StaticAnchors.Handle,
                0U,
                staticBytes,
            };
        }
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImpl->VisibleIndices[slot].Handle,
            0U,
            visibleBytes,
        };
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImpl->InteractionSamples[slot].Handle,
            0U,
            interactionBytes,
        };
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImpl->OutputInstances[slot].Handle,
            0U,
            outputBytes,
        };
        // After a clear the fill is the last writer; otherwise the previous compaction.
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
            VkAccessFlags(clearHistory ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, mImpl->BendHistory.Handle, 0U, historyBytes,
        };
        barriers[barrierCount++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, mImpl->PreviousBends[slot].Handle, 0U, previousBendBytes,
        };
        vkCmdPipelineBarrier(request.CommandBuffer,
                             (staticChanged || clearHistory ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0U) |
                                 VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0U, 0U, nullptr, barrierCount, barriers.data(), 0U,
                             nullptr);

        GrassGpuCompactionPush push;
        push.CountsFlags = {
            static_cast<uint32_t>(request.VisibleAnchorIndices.size()),
            request.InteractionField->Resolution(),
            static_cast<uint32_t>(request.StaticAnchors.size()),
            (request.InteractionField->Initialized() ? 1U : 0U) | (static_cast<uint32_t>(request.Actors.size()) << 1U),
        };
        const auto fieldCenter = request.InteractionField->Center();
        push.Interaction = {
            fieldCenter[0],
            fieldCenter[1],
            request.InteractionField->WorldExtent(),
            request.Settings->InteractionVerticalMargin,
        };
        push.Collision = {
            request.Settings->ColliderRadiusMultiplier,
            request.Settings->ColliderHeightMultiplier,
            request.Settings->CollisionPush,
            request.Settings->CollisionVelocityResponse,
        };
        push.Bend = {
            request.Settings->MaximumBend,
            request.Settings->InteractionVerticalMargin,
            0.0F,
            0.0F,
        };
        push.History = {BendHistoryStamp(request.FrameId), previousStamp, 0U, 0U};

        vkCmdBindPipeline(request.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mImpl->Pipeline);
        vkCmdBindDescriptorSets(request.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mImpl->PipelineLayout, 0U, 1U,
                                &mImpl->DescriptorSets[slot], 0U, nullptr);
        vkCmdPushConstants(request.CommandBuffer, mImpl->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(push),
                           &push);
        vkCmdDispatch(request.CommandBuffer, (push.CountsFlags[0] + kComputeGroupSize - 1U) / kComputeGroupSize, 1U,
                      1U);
        const std::array<VkBufferMemoryBarrier, 2U> outputReady{ {
            {
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                nullptr,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                mImpl->OutputInstances[slot].Handle,
                0U,
                outputBytes,
            },
            {
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, mImpl->PreviousBends[slot].Handle, 0U,
                previousBendBytes,
            },
        } };
        vkCmdPipelineBarrier(request.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0U, 0U, nullptr,
                             static_cast<uint32_t>(outputReady.size()), outputReady.data(), 0U, nullptr);

        result.InstanceBuffer = mImpl->OutputInstances[slot].Handle;
        result.PreviousBendBuffer = mImpl->PreviousBends[slot].Handle;
        result.InstanceCount = push.CountsFlags[0];
        result.VisibleIndicesBuffer = mImpl->VisibleIndices[slot].Handle;
        result.DynamicUploadedBytes =
            static_cast<uint64_t>((sameSelection ? 0 : visibleBytes) + interactionBytes + actorBytes);
        selection = { request.StaticRevision, request.SelectionRevision, request.VisibleAnchorIndices.size(), idle };
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
}

void GrassGpuInstanceCompactor::ResetHistory() {
    mImpl->HistoryValid = false;
    mImpl->Selections = {};
}

void GrassGpuInstanceCompactor::Shutdown() {
    if (!mImpl || mImpl->Device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(mImpl->Device);
    mImpl->DestroyBuffer(mImpl->StaticAnchors);
    mImpl->DestroyBuffer(mImpl->BendHistory);
    for (auto& buffer : mImpl->PreviousBends) {
        mImpl->DestroyBuffer(buffer);
    }
    for (auto& buffer : mImpl->StaticStaging) {
        mImpl->DestroyBuffer(buffer);
    }
    for (auto& buffer : mImpl->VisibleIndices) {
        mImpl->DestroyBuffer(buffer);
    }
    for (auto& buffer : mImpl->InteractionSamples) {
        mImpl->DestroyBuffer(buffer);
    }
    for (auto& buffer : mImpl->Actors) {
        mImpl->DestroyBuffer(buffer);
    }
    for (auto& buffer : mImpl->OutputInstances) {
        mImpl->DestroyBuffer(buffer);
    }
    if (mImpl->Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(mImpl->Device, mImpl->Pipeline, nullptr);
    }
    if (mImpl->PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(mImpl->Device, mImpl->PipelineLayout, nullptr);
    }
    if (mImpl->DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(mImpl->Device, mImpl->DescriptorPool, nullptr);
    }
    if (mImpl->DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(mImpl->Device, mImpl->DescriptorSetLayout, nullptr);
    }
    mImpl->DescriptorSetLayout = VK_NULL_HANDLE;
    mImpl->DescriptorPool = VK_NULL_HANDLE;
    mImpl->DescriptorSets = {};
    mImpl->PipelineLayout = VK_NULL_HANDLE;
    mImpl->Pipeline = VK_NULL_HANDLE;
    mImpl->StaticRevision = 0U;
    mImpl->Selections = {};
    mImpl->StaticAnchorCount = 0U;
    mImpl->HistoryValid = false;
    mImpl->PhysicalDevice = VK_NULL_HANDLE;
    mImpl->Device = VK_NULL_HANDLE;
}

const std::string& GrassGpuInstanceCompactor::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
