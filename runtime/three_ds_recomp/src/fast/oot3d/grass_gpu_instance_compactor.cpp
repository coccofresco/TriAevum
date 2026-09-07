#include "fast/oot3d/grass_gpu_instance_compactor.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <shaderc/shaderc.hpp>

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
constexpr VkDeviceSize kGrassInstanceBytes = 56U;
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
};
static_assert(sizeof(GrassGpuCompactionPush) == 64U);

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

VkShaderModule CompileCompute(VkDevice device, const char* source) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto compiled = compiler.CompileGlslToSpv(source, std::strlen(source), shaderc_compute_shader,
                                                    "grass_instance_compactor.comp", options);
    if (compiled.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("grass_instance_compactor.comp: " + compiled.GetErrorMessage());
    }
    const std::vector<uint32_t> words(compiled.cbegin(), compiled.cend());
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
    uint64_t StaticRevision = 0U;
    size_t StaticAnchorCount = 0U;
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

bool GrassGpuInstanceCompactor::Initialize(VkPhysicalDevice physicalDevice, VkDevice device) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    try {
        static constexpr const char* kComputeSource = R"glsl(
#version 450
layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

layout(std430, set=0, binding=0) readonly buffer StaticAnchors {
    uint words[];
} static_anchors;
layout(std430, set=0, binding=1) readonly buffer VisibleIndices {
    uint values[];
} visible_indices;
layout(std430, set=0, binding=2) readonly buffer InteractionSamples {
    vec4 values[];
} interaction_samples;
layout(std430, set=0, binding=3) writeonly buffer OutputInstances {
    float words[];
} output_instances;

struct ActorCollider {
    vec4 previous_radius;
    vec4 current_half_height;
    vec4 velocity_teleported;
};
layout(std430, set=0, binding=4) readonly buffer Actors {
    ActorCollider values[];
} actors;

layout(push_constant) uniform CompactionState {
    uvec4 counts_flags;
    vec4 interaction;
    vec4 collision;
    vec4 bend;
} state;

vec2 sample_interaction(vec3 base) {
    uint resolution = state.counts_flags.y;
    bool initialized = (state.counts_flags.w & 1u) != 0u;
    if (!initialized || resolution == 0u)
        return vec2(0.0);
    float extent = state.interaction.z;
    float cell = (extent * 2.0) / float(resolution);
    float half_cell = extent / float(resolution);
    if (base.x < state.interaction.x - extent + half_cell ||
        base.x > state.interaction.x + extent - half_cell ||
        base.z < state.interaction.y - extent + half_cell ||
        base.z > state.interaction.y + extent - half_cell)
        return vec2(0.0);
    float origin_x = state.interaction.x - extent;
    float origin_z = state.interaction.y - extent;
    vec2 grid = vec2(
        (base.x - origin_x) / cell - 0.5,
        (base.z - origin_z) / cell - 0.5);
    if (grid.x < 0.0 || grid.y < 0.0 ||
        grid.x > float(resolution - 1u) ||
        grid.y > float(resolution - 1u))
        return vec2(0.0);
    uvec2 lower = uvec2(floor(grid));
    uvec2 upper = min(
        lower + uvec2(1u),
        uvec2(resolution - 1u));
    vec2 fraction = grid - vec2(lower);
    vec2 result = vec2(0.0);
    uvec2 coordinates[4] = uvec2[4](
        uvec2(lower.x, lower.y),
        uvec2(upper.x, lower.y),
        uvec2(lower.x, upper.y),
        uvec2(upper.x, upper.y));
    float weights[4] = float[4](
        (1.0 - fraction.x) * (1.0 - fraction.y),
        fraction.x * (1.0 - fraction.y),
        (1.0 - fraction.x) * fraction.y,
        fraction.x * fraction.y);
    for (uint index = 0u; index < 4u; ++index) {
        uint sample_index =
            coordinates[index].y * resolution +
            coordinates[index].x;
        vec4 sample_value =
            interaction_samples.values[sample_index];
        if (!isnan(sample_value.z) &&
            abs(base.y - sample_value.z) <=
                state.interaction.w) {
            result += sample_value.xy * weights[index];
        }
    }
    return result;
}

vec2 resolve_direct_collision(vec3 base, float blade_height, ActorCollider actor) {
    if (state.collision.z <= 0.0 ||
        blade_height <= 0.0)
        return vec2(0.0);
    vec3 previous = actor.previous_radius.xyz;
    vec3 current = actor.current_half_height.xyz;
    float radius = max(0.01, actor.previous_radius.w * state.collision.x);
    // Reject the swept bounding box before projection, roots and normalization.
    if (any(lessThan(base.xz, min(previous.xz, current.xz) - radius)) ||
        any(greaterThan(base.xz, max(previous.xz, current.xz) + radius))) return vec2(0.0);
    vec2 segment = current.xz - previous.xz;
    float segment_length_squared = dot(segment, segment);
    float t = 1.0;
    if (actor.velocity_teleported.w <= 0.5 &&
        segment_length_squared > 1.0e-6) {
        t = clamp(
            dot(base.xz - previous.xz, segment) /
                segment_length_squared,
            0.0, 1.0);
    }
    vec3 closest = mix(previous, current, t);
    vec2 delta = base.xz - closest.xz;
    float distance = length(delta);
    if (distance >= radius)
        return vec2(0.0);
    float full_height = max(
        radius,
        actor.current_half_height.w * 2.0 *
            state.collision.y);
    float collider_bottom =
        closest.y - state.bend.y;
    float collider_top =
        closest.y + full_height + state.bend.y;
    if (base.y + blade_height < collider_bottom ||
        base.y > collider_top)
        return vec2(0.0);
    if (distance > 1.0e-5) {
        delta /= distance;
    } else {
        float velocity_length =
            length(actor.velocity_teleported.xz);
        float motion_length = sqrt(segment_length_squared);
        if (velocity_length > 1.0e-5)
            delta =
                actor.velocity_teleported.xz /
                velocity_length;
        else if (motion_length > 1.0e-5)
            delta = segment / motion_length;
        else
            delta = vec2(1.0, 0.0);
    }
    float speed =
        length(actor.velocity_teleported.xz);
    float velocity_gain =
        1.0 + clamp(speed / 300.0, 0.0, 2.0) *
            state.collision.w;
    float weight =
        (1.0 - distance / radius) *
        state.collision.z * velocity_gain;
    return delta * min(weight, state.bend.x);
}

void main() {
    uint visible_index = gl_GlobalInvocationID.x;
    if (visible_index >= state.counts_flags.x)
        return;
    uint static_index =
        visible_indices.values[visible_index];
    if (static_index >= state.counts_flags.z)
        return;
    uint source = static_index * 9u;
    vec4 base_height = uintBitsToFloat(uvec4(
        static_anchors.words[source + 0u],
        static_anchors.words[source + 1u],
        static_anchors.words[source + 2u],
        static_anchors.words[source + 3u]));
    vec2 half_width_phase = uintBitsToFloat(uvec2(
        static_anchors.words[source + 4u],
        static_anchors.words[source + 5u]));
    vec2 width_axis = unpackSnorm2x16(static_anchors.words[source + 6u]);
    vec2 oct = unpackSnorm2x16(static_anchors.words[source + 7u]);
    vec3 normal = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = max(-normal.z, 0.0);
    normal.xy += mix(vec2(fold), vec2(-fold), greaterThanEqual(normal.xy, vec2(0.0)));
    // Identical to GrassStableVisibilityValue; W was unused in the instance ABI.
    uint stable = static_anchors.words[source + 8u];
    stable ^= stable >> 16u;
    stable *= 0x7feb352du;
    stable ^= stable >> 15u;
    stable *= 0x846ca68bu;
    stable ^= stable >> 16u;
    vec4 world_normal = vec4(normalize(normal), float(stable >> 8u) / 16777216.0);
    vec2 dynamic_bend = sample_interaction(base_height.xyz);
    for (uint i = 0u; i < (state.counts_flags.w >> 1u); ++i)
        dynamic_bend += resolve_direct_collision(base_height.xyz, base_height.w, actors.values[i]);
    float bend_length = length(dynamic_bend);
    if (bend_length > state.bend.x && bend_length > 0.0)
        dynamic_bend *= state.bend.x / bend_length;

    uint destination = visible_index * 14u;
    output_instances.words[destination + 0u] =
        base_height.x;
    output_instances.words[destination + 1u] =
        base_height.y;
    output_instances.words[destination + 2u] =
        base_height.z;
    output_instances.words[destination + 3u] =
        base_height.w;
    output_instances.words[destination + 4u] =
        dynamic_bend.x;
    output_instances.words[destination + 5u] =
        dynamic_bend.y;
    output_instances.words[destination + 6u] =
        half_width_phase.x;
    output_instances.words[destination + 7u] =
        half_width_phase.y;
    output_instances.words[destination + 8u] =
        width_axis.x;
    output_instances.words[destination + 9u] =
        width_axis.y;
    output_instances.words[destination + 10u] =
        world_normal.x;
    output_instances.words[destination + 11u] =
        world_normal.y;
    output_instances.words[destination + 12u] =
        world_normal.z;
    output_instances.words[destination + 13u] =
        world_normal.w;
}
)glsl";
        const VkShaderModule shader = CompileCompute(device, kComputeSource);
        try {
            std::array<VkDescriptorSetLayoutBinding, 5U> bindings{};
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
            if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &mImpl->Pipeline) !=
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

        mImpl->EnsureBuffer(mImpl->VisibleIndices[slot], visibleBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false);
        std::memcpy(mImpl->VisibleIndices[slot].Mapped, request.VisibleAnchorIndices.data(),
                    static_cast<size_t>(visibleBytes));
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

        const std::array<VkDescriptorBufferInfo, 5U> bufferInfos{ {
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
        } };
        std::array<VkWriteDescriptorSet, 5U> writes{};
        for (uint32_t index = 0U; index < writes.size(); ++index) {
            writes[index] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mImpl->DescriptorSets[slot], index,   0U, 1U,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &bufferInfos[index],         nullptr,
            };
        }
        vkUpdateDescriptorSets(mImpl->Device, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);

        std::array<VkBufferMemoryBarrier, 5U> barriers{};
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
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImpl->OutputInstances[slot].Handle,
            0U,
            outputBytes,
        };
        vkCmdPipelineBarrier(request.CommandBuffer,
                             staticChanged ? VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT |
                                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
                                           : VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U, nullptr, barrierCount, barriers.data(), 0U,
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

        vkCmdBindPipeline(request.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mImpl->Pipeline);
        vkCmdBindDescriptorSets(request.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mImpl->PipelineLayout, 0U, 1U,
                                &mImpl->DescriptorSets[slot], 0U, nullptr);
        vkCmdPushConstants(request.CommandBuffer, mImpl->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(push),
                           &push);
        vkCmdDispatch(request.CommandBuffer, (push.CountsFlags[0] + kComputeGroupSize - 1U) / kComputeGroupSize, 1U,
                      1U);
        const VkBufferMemoryBarrier outputReady{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImpl->OutputInstances[slot].Handle,
            0U,
            outputBytes,
        };
        vkCmdPipelineBarrier(request.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0U, 0U, nullptr, 1U, &outputReady, 0U, nullptr);

        result.InstanceBuffer = mImpl->OutputInstances[slot].Handle;
        result.InstanceCount = push.CountsFlags[0];
        result.DynamicUploadedBytes = static_cast<uint64_t>(visibleBytes + interactionBytes + actorBytes);
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
}

void GrassGpuInstanceCompactor::Shutdown() {
    if (!mImpl || mImpl->Device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(mImpl->Device);
    mImpl->DestroyBuffer(mImpl->StaticAnchors);
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
    mImpl->StaticAnchorCount = 0U;
    mImpl->PhysicalDevice = VK_NULL_HANDLE;
    mImpl->Device = VK_NULL_HANDLE;
}

const std::string& GrassGpuInstanceCompactor::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
