#include "fast/oot3d/interactive_grass_pass.h"
#include "fast/oot3d/pica_attachment_contract.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_async_placement_builder.h"
#include "fast/oot3d/grass_static_placement_cache.h"
#include "fast/oot3d/grass_blade_geometry.h"
#include "fast/oot3d/grass_blade_shape.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_selection_cache.h"
#include "fast/oot3d/grass_selection_budget.h"
#include "fast/oot3d/grass_collision_resolver.h"
#include "fast/oot3d/grass_gpu_instance_compactor.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "fast/oot3d/grass_render_telemetry.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/outline_occlusion_pass.h"
#include "fast/oot3d/visual_clock.h"

#define BS_THREAD_POOL_ENABLE_PAUSE
#define BS_THREAD_POOL_ENABLE_PRIORITY
#include <BS_thread_pool.hpp>
#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Fast::Oot3d {
namespace {

struct GrassInstance {
    std::array<float, 4> BaseHeight{};
    std::array<float, 4> BendAndHalfWidth{};
    std::array<float, 2> WidthAxis{1.0F, 0.0F};
    std::array<float, 4> WorldNormal{
        0.0F, 1.0F, 0.0F, 0.0F};
};
static_assert(sizeof(GrassInstance) == 56U);

struct alignas(16) GrassEnvironmentRecord {
    // W is 0 when disabled, 1 for regular depth and 2 for flipped depth.
    std::array<float, 4> ColorAndMode{};
    std::array<std::array<float, 2>, kGrassFogLutEntryCount> Lut{};
    // direction X/Z, visual seconds, strength
    std::array<float, 4> WindDirectionTime{};
    // speed, spatial scale, gust strength, gust frequency
    std::array<float, 4> WindPrimary{};
    // turbulence, per-blade randomness, maximum bend, enabled
    std::array<float, 4> WindDetail{};
    std::array<float, 4> CameraPosition{};
    std::array<float, 4> AppearanceRoot{};
    std::array<float, 4> AppearanceTip{};
    // Average texture RGB and effective texture-color influence.
    std::array<float, 4> TextureColor{};
    // Root/tip brightness, receive-lighting flag, active light count.
    std::array<float, 4> TextureBrightnessFlags{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightDirections{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightDiffuse{};
    std::array<std::array<float, 4>, kGrassNativeLightCount>
        LightAmbient{};
    std::array<float, 4> ViewForward{};
    std::array<float, 4> ViewSide{};
    std::array<float, 4> ViewUp{};
    std::array<float, 4> BladeShape{};
    std::array<float, 4> TuftLod{};
    // Draw distance, density start/end, per-anchor fade softness.
    std::array<float, 4> DistanceLod{};
    // Tuft spread/density, final fade range, enabled.
    std::array<float, 4> TuftStyle{};
};
static_assert(sizeof(GrassEnvironmentRecord) == 1424U);

struct GrassPushConstants {
    std::array<float, 16> PositionToClip{};
    std::array<float, 4> JitterNdc{};
    std::array<float, 4> DepthState{}; // scale, offset, w-buffer, valid
    // native projection, segments, planes, fog-record index
    std::array<uint32_t, 4> Flags{};
};

struct GrassDrawBatch {
    uint32_t FirstInstance = 0;
    uint32_t InstanceCount = 0;
    uint8_t BladeSegments = 1U;
    uint8_t PlaneCount = 1U;
    GrassPushConstants Push{};
    uint32_t PlacementIndex = 0;
};

struct GrassPreparedPlacement {
    std::shared_ptr<const GrassWorldPlacement> World;
    GrassPushConstants Push{};
    GrassEnvironmentRecord Environment{};
    GrassFrustumRadiusScale FrustumRadiusScale{};
    std::array<float, 3> Eye{};
    uint32_t StaticBaseIndex = 0U;
};

inline constexpr size_t kGrassLodBinCount =
    static_cast<size_t>(kMaximumGrassBladeSegments) * 2U + 1U;
inline constexpr size_t kGrassAnchorsPerWorker = 8192U;
inline constexpr uint64_t kGrassIdentityFnvOffset =
    14695981039346656037ULL;
inline constexpr uint64_t kGrassIdentityFnvPrime = 1099511628211ULL;

template <typename Value>
void HashGrassIdentityValue(
    uint64_t& hash, const Value& value) noexcept {
    const auto* bytes =
        reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0U; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) *
               kGrassIdentityFnvPrime;
    }
}

uint64_t GrassWorldPlacementIdentity(
    const GrassSceneMesh& mesh,
    const GrassPlacementKey& key) noexcept {
    uint64_t hash = kGrassIdentityFnvOffset;
    HashGrassIdentityValue(hash, mesh.InstanceId);
    HashGrassIdentityValue(hash, key.GeometryId);
    HashGrassIdentityValue(hash, key.ContentVersion);
    HashGrassIdentityValue(hash, key.TextureHash);
    HashGrassIdentityValue(hash, key.RuleId);
    HashGrassIdentityValue(hash, mesh.MapperSlot);
    return hash == 0U ? 1U : hash;
}

using GrassVisibleIndexBins =
    std::array<std::vector<uint32_t>, kGrassLodBinCount>;

struct GrassClusterCounters {
    uint64_t Evaluated = 0U;
    uint32_t Visible = 0U;
};

struct GrassWorkerOutput {
    GrassVisibleIndexBins Bins;
    GrassClusterCounters Counters;

    void Clear() {
        for (auto& bin : Bins) {
            bin.clear();
        }
        Counters = {};
    }
};

size_t GrassLodBinIndex(
    uint8_t bladeSegments, uint8_t planeCount) {
    const auto segments = std::clamp<uint8_t>(
        bladeSegments, kMinimumGrassBladeSegments,
        kMaximumGrassBladeSegments);
    const auto planes = std::clamp<uint8_t>(planeCount, 1U, 2U);
    return (static_cast<size_t>(segments) - 1U) * 2U +
           (static_cast<size_t>(planes) - 1U);
}

GrassEnvironmentRecord BuildGrassEnvironmentRecord(
    const GrassPicaFogState& fog, bool receiveFog,
    bool picaDepthAvailable,
    const InteractiveGrassSettings& settings,
    const GrassShadingEnvironment& shading,
    const std::optional<std::array<float, 3>>&
        textureAverage,
    double visualSeconds,
    const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view,
    const std::array<float, 3>& viewForward,
    const std::array<float, 3>& viewSide,
    const std::array<float, 3>& viewUp) {
    GrassEnvironmentRecord result;
    result.TuftLod = {settings.LodEndFraction,
        settings.LodReferenceDistance > 0.0F ? settings.LodReferenceDistance : settings.DrawDistance,
        static_cast<float>(settings.FarTuftBladeCount), settings.TuftTransitionFraction};
    result.DistanceLod = {settings.DrawDistance, settings.LodStartFraction, settings.FarDensity, settings.DensityFadeFraction};
    result.TuftStyle = {settings.FarTuftSpread, settings.FarTuftDensity, settings.DrawFadeFraction,
        settings.FarTuftsEnabled ? 1.0F : 0.0F};
    result.BladeShape = {
        settings.Appearance.BladeCurvature,
        settings.Appearance.BladeDroop,
        settings.Appearance.ShapeVariation,
        settings.Appearance.BladeTwistDegrees * std::numbers::pi_v<float> / 180.0F};
    if (receiveFog && fog.Enabled && picaDepthAvailable) {
        result.ColorAndMode = {
            fog.Color[0], fog.Color[1], fog.Color[2],
            fog.Flip ? 2.0F : 1.0F};
        result.Lut = fog.Lut;
    }
    const float direction =
        settings.WindDirectionDegrees *
        std::numbers::pi_v<float> / 180.0F;
    result.WindDirectionTime = {
        std::cos(direction), std::sin(direction),
        static_cast<float>(visualSeconds),
        settings.WindStrength};
    result.WindPrimary = {
        settings.WindSpeed,
        settings.WindSpatialScale,
        settings.WindGustStrength,
        settings.WindGustFrequency};
    result.WindDetail = {
        settings.WindTurbulence,
        settings.WindRandomness,
        settings.MaximumBend,
        settings.WindStrength > 0.0F &&
                settings.MaximumBend > 0.0F
            ? 1.0F
            : 0.0F};
    result.CameraPosition = {
        view.Eye[0], view.Eye[1], view.Eye[2], 0.0F};
    result.AppearanceRoot = {
        settings.Appearance.RootColor[0],
        settings.Appearance.RootColor[1],
        settings.Appearance.RootColor[2], 1.0F};
    result.AppearanceTip = {
        settings.Appearance.TipColor[0],
        settings.Appearance.TipColor[1],
        settings.Appearance.TipColor[2], 1.0F};
    if (textureAverage.has_value()) {
        result.TextureColor = {
            (*textureAverage)[0],
            (*textureAverage)[1],
            (*textureAverage)[2],
            settings.Appearance.TextureColorInfluence};
    }
    result.TextureBrightnessFlags = {
        settings.Appearance.TextureRootBrightness,
        settings.Appearance.TextureTipBrightness,
        settings.Appearance.ReceiveLighting ? 1.0F : 0.0F,
        static_cast<float>(shading.ActiveLightCount)};
    for (size_t index = 0U;
         index < shading.ActiveLightCount &&
         index < kGrassNativeLightCount;
         ++index) {
        const auto& light = shading.Lights[index];
        result.LightDirections[index] = {
            light.DirectionWorldTowardSource[0],
            light.DirectionWorldTowardSource[1],
            light.DirectionWorldTowardSource[2],
            light.Valid ? 1.0F : 0.0F};
        result.LightDiffuse[index] = {
            light.Diffuse[0], light.Diffuse[1],
            light.Diffuse[2], 0.0F};
        result.LightAmbient[index] = {
            light.Ambient[0], light.Ambient[1],
            light.Ambient[2], 0.0F};
    }
    result.ViewForward = {
        viewForward[0], viewForward[1],
        viewForward[2], 0.0F};
    result.ViewSide = {
        viewSide[0], viewSide[1],
        viewSide[2], 0.0F};
    result.ViewUp = {
        viewUp[0], viewUp[1], viewUp[2], 0.0F};
    return result;
}

std::optional<std::array<float, 16>> InvertAffineRowMajor(
    const std::array<float, 16>& matrix) {
    const float determinant =
        matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
        matrix[1] * (matrix[4] * matrix[10] - matrix[6] * matrix[8]) +
        matrix[2] * (matrix[4] * matrix[9] - matrix[5] * matrix[8]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12F)
        return std::nullopt;
    const float inverseDeterminant = 1.0F / determinant;
    std::array<float, 16> inverse{
        (matrix[5] * matrix[10] - matrix[6] * matrix[9]) * inverseDeterminant,
        (matrix[2] * matrix[9] - matrix[1] * matrix[10]) * inverseDeterminant,
        (matrix[1] * matrix[6] - matrix[2] * matrix[5]) * inverseDeterminant,
        0.0F,
        (matrix[6] * matrix[8] - matrix[4] * matrix[10]) * inverseDeterminant,
        (matrix[0] * matrix[10] - matrix[2] * matrix[8]) * inverseDeterminant,
        (matrix[2] * matrix[4] - matrix[0] * matrix[6]) * inverseDeterminant,
        0.0F,
        (matrix[4] * matrix[9] - matrix[5] * matrix[8]) * inverseDeterminant,
        (matrix[1] * matrix[8] - matrix[0] * matrix[9]) * inverseDeterminant,
        (matrix[0] * matrix[5] - matrix[1] * matrix[4]) * inverseDeterminant,
        0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    inverse[3] = -(inverse[0] * matrix[3] + inverse[1] * matrix[7] +
                   inverse[2] * matrix[11]);
    inverse[7] = -(inverse[4] * matrix[3] + inverse[5] * matrix[7] +
                   inverse[6] * matrix[11]);
    inverse[11] = -(inverse[8] * matrix[3] + inverse[9] * matrix[7] +
                    inverse[10] * matrix[11]);
    return inverse;
}

std::array<float, 16> MultiplyRowMajor(
    const std::array<float, 16>& left,
    const std::array<float, 16>& right) {
    std::array<float, 16> result{};
    for (size_t row = 0; row < 4U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            for (size_t inner = 0; inner < 4U; ++inner) {
                result[row * 4U + column] +=
                    left[row * 4U + inner] *
                    right[inner * 4U + column];
            }
        }
    }
    return result;
}

GrassPushConstants ProjectionForMesh(const GrassSceneMesh& mesh,
                                     const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view) {
    GrassPushConstants push;
    push.PositionToClip = view.WorldToClip;
    if (!mesh.PicaModelToClipAvailable || mesh.TransformBakedIntoVertices)
        return push;
    const auto worldToModel = InvertAffineRowMajor(mesh.ModelToWorld);
    if (!worldToModel.has_value())
        return push;

    std::array<float, 16> picaModelToClipRowMajor{};
    for (size_t row = 0; row < 4U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            picaModelToClipRowMajor[row * 4U + column] =
                mesh.PicaModelToClip[column * 4U + row];
        }
    }
    const auto worldToClipRowMajor =
        MultiplyRowMajor(picaModelToClipRowMajor, *worldToModel);
    for (size_t row = 0; row < 4U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            push.PositionToClip[column * 4U + row] =
                worldToClipRowMajor[row * 4U + column];
        }
    }
    push.Flags[0] = 1U;
    return push;
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t mask,
                        VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((mask & (1U << index)) != 0U &&
            (properties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("interactive grass has no compatible memory type");
}

VkShaderModule Compile(VkDevice device, const char* source,
                       shaderc_shader_kind kind, const char* name,
                       const char* defineName = nullptr,
                       const char* defineValue = nullptr) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    if (defineName != nullptr && defineValue != nullptr) {
        options.AddMacroDefinition(defineName, defineValue);
    }
    const auto result = compiler.CompileGlslToSpv(source, std::strlen(source),
                                                  kind, name, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(std::string(name) + ": " +
                                 result.GetErrorMessage());
    }
    const std::vector<uint32_t> words(result.cbegin(), result.cend());
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error(std::string("cannot create ") + name);
    }
    return module;
}

std::array<float, 3> Normalize(std::array<float, 3> value) {
    const float length = std::sqrt(
        value[0] * value[0] + value[1] * value[1] +
        value[2] * value[2]);
    if (!std::isfinite(length) || length <= 1.0e-6F) {
        return {};
    }
    for (float& component : value) {
        component /= length;
    }
    return value;
}

std::array<float, 3> Cross(const std::array<float, 3>& left,
                           const std::array<float, 3>& right) {
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    };
}

} // namespace

struct InteractiveGrassPass::Impl {
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> DescriptorSets{};
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> Pipelines{};
    struct BufferSlot {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0;
        void* Mapped = nullptr;
    };
    std::array<BufferSlot, 2> InstanceBuffers{};
    std::array<BufferSlot, 2> EnvironmentBuffers{};
    BufferSlot IndexBuffer;
    GrassIndexedTopology IndexedTopology;
    GrassSelectionCache SelectionCache;
    std::vector<GrassDrawBatch> Batches;
    std::vector<GrassEnvironmentRecord> EnvironmentRecords;
    std::vector<GrassPreparedPlacement> PreparedPlacements;
    std::vector<GrassAsyncPlacementRequest> PlacementRequests;
    GrassVisibleIndexBins VisibleIndexBins;
    std::vector<uint32_t> VisibleAnchorIndices;
    std::vector<GrassWorldAnchor> StaticAnchors;
    std::vector<GrassClusterWork> ClusterWork;
    struct ActivePlacement {
        uint64_t Identity = 0U;
        uint64_t ContentVersion = 0U;
        size_t AnchorCount = 0U;
        bool operator==(const ActivePlacement&) const = default;
    };
    std::vector<ActivePlacement> ActivePlacements;
    uint64_t StaticRevision = 0U;
    std::unique_ptr<BS::thread_pool> WorkerPool;
    std::vector<GrassWorkerOutput> WorkerOutputs;
    GrassStaticPlacementCache PlacementBuilder;
    GrassGpuInstanceCompactor InstanceCompactor;
    bool GpuCompactorAvailable = false;
    GrassInteractionField InteractionField;
    uint64_t LastClockSequence = 0;
    uint64_t LastFallbackFrame = 0;
    bool HasClockSequence = false;
    bool HasFallbackFrame = false;
    double FallbackSeconds = 0.0;
    uint32_t LastBlades = 0;
    uint32_t LastMotionBlades = 0;
    VkBuffer PreparedInstanceBuffer = VK_NULL_HANDLE;
    uint32_t PreparedFrameSlot = 0U;
    bool Prepared = false;
    std::string Reason;

    size_t WorkerCount() {
        if (WorkerPool == nullptr) {
            const uint32_t hardware =
                std::thread::hardware_concurrency();
            const uint32_t count = hardware > 2U
                ? std::min(hardware - 2U, 12U)
                : 1U;
            WorkerPool =
                std::make_unique<BS::thread_pool>(count);
        }
        return static_cast<size_t>(
            WorkerPool->get_thread_count());
    }

    void DestroyBuffer(BufferSlot& slot) {
        if (slot.Mapped != nullptr) vkUnmapMemory(Device, slot.Memory);
        if (slot.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, slot.Buffer, nullptr);
        if (slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Device, slot.Memory, nullptr);
        slot = {};
    }

    void EnsureBuffer(
        BufferSlot& slot, VkDeviceSize bytes,
        VkBufferUsageFlags usage, VkDeviceSize minimumCapacity = 1U << 20U) {
        if (bytes <= slot.Capacity) return;
        DestroyBuffer(slot);
        slot.Capacity = std::bit_ceil(std::max(bytes, minimumCapacity));
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = slot.Capacity;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &bufferInfo, nullptr, &slot.Buffer) != VK_SUCCESS)
            throw std::runtime_error(
                "cannot create interactive grass streaming buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(Device, slot.Buffer, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(
            PhysicalDevice, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(Device, &allocation, nullptr, &slot.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(Device, slot.Buffer, slot.Memory, 0) != VK_SUCCESS ||
            vkMapMemory(Device, slot.Memory, 0, slot.Capacity, 0, &slot.Mapped) != VK_SUCCESS)
            throw std::runtime_error(
                "cannot map interactive grass streaming buffer");
    }
};

InteractiveGrassPass::InteractiveGrassPass() : mImpl(std::make_unique<Impl>()) {}
InteractiveGrassPass::~InteractiveGrassPass() { Shutdown(); }

bool InteractiveGrassPass::Initialize(VkPhysicalDevice physicalDevice,
                                      VkDevice device,
                                      VkRenderPass canonicalRenderPass,
                                      VkRenderPass instrumentedRenderPass,
                                      VkSampleCountFlagBits sampleCount,
                                      bool dynamicRendering,
                                      VkFormat depthFormat) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    VkPhysicalDeviceProperties grassDeviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &grassDeviceProperties);
    mImpl->PlacementBuilder.SetCandidateCapacity(std::min(8U * 1024U * 1024U,
        grassDeviceProperties.limits.maxStorageBufferRange / static_cast<uint32_t>(sizeof(GrassWorldAnchor))));
    mImpl->Device = device;
    try {
        static const std::string vertexSource =
            std::string("#version 450\n") + std::string(kGrassBladeShapeShader) +
            std::string(kGrassDistantTuftShader) + std::string(kGrassIndexedVertexShader) + R"glsl(
layout(location=0) in vec4 in_base_height;
layout(location=1) in vec4 in_bend_half_width;
layout(location=2) in vec2 in_width_axis;
layout(location=3) in vec4 in_world_normal;
struct GrassEnvironmentRecord {
    vec4 color_and_mode;
    vec2 lut[128];
    vec4 wind_direction_time;
    vec4 wind_primary;
    vec4 wind_detail;
    vec4 camera_position;
    vec4 appearance_root;
    vec4 appearance_tip;
    vec4 texture_color;
    vec4 texture_brightness_flags;
    vec4 light_directions[3];
    vec4 light_diffuse[3];
    vec4 light_ambient[3];
    vec4 view_forward;
    vec4 view_side;
    vec4 view_up;
    vec4 blade_shape;
    vec4 tuft_lod;
    vec4 distance_lod;
    vec4 tuft_style;
};
layout(std430, set=0, binding=0) readonly buffer GrassEnvironmentState {
    GrassEnvironmentRecord records[];
} environment_state;
layout(push_constant) uniform GrassState {
    mat4 position_to_clip;
    vec4 jitter_ndc;
    vec4 depth_state;
    uvec4 flags;
} grass;
layout(location=0) out vec4 blade_color;
layout(location=1) out vec4 blade_normal_guide;
layout(location=2) out vec4 blade_ambient_guide;
layout(location=3) out vec4 tuft_sample;
layout(location=4) flat out float lod_visibility;

void evaluate_shading(
    uint environment_index, vec3 world_normal,
    out vec3 lighting, out vec3 ambient_response) {
    vec4 flags =
        environment_state.records[
            environment_index].texture_brightness_flags;
    uint light_count =
        uint(clamp(flags.w, 0.0, 3.0));
    if (flags.z <= 0.5 || light_count == 0u) {
        lighting = vec3(1.0);
        ambient_response = vec3(1.0);
        return;
    }
    vec3 ambient = vec3(0.0);
    vec3 direct = vec3(0.0);
    for (uint index = 0u; index < light_count; ++index) {
        vec4 direction =
            environment_state.records[
                environment_index].light_directions[index];
        if (direction.w <= 0.5)
            continue;
        float diffuse_factor = abs(clamp(
            dot(world_normal, direction.xyz), -1.0, 1.0));
        ambient +=
            environment_state.records[
                environment_index].light_ambient[index].rgb;
        direct +=
            environment_state.records[
                environment_index].light_diffuse[index].rgb *
            diffuse_factor;
    }
    vec3 total = ambient + direct;
    lighting = clamp(total, 0.0, 1.0);
    ambient_response = vec3(
        total.x > 1.0e-6
            ? clamp(ambient.x / total.x, 0.0, 1.0) : 1.0,
        total.y > 1.0e-6
            ? clamp(ambient.y / total.y, 0.0, 1.0) : 1.0,
        total.z > 1.0e-6
            ? clamp(ambient.z / total.z, 0.0, 1.0) : 1.0);
}

vec3 evaluate_blade_color(
    uint environment_index, float height_factor,
    vec3 lighting) {
    vec3 root =
        environment_state.records[
            environment_index].appearance_root.rgb;
    vec3 tip =
        environment_state.records[
            environment_index].appearance_tip.rgb;
    vec4 texture =
        environment_state.records[
            environment_index].texture_color;
    vec4 flags =
        environment_state.records[
            environment_index].texture_brightness_flags;
    vec3 root_color =
        mix(root, texture.rgb * flags.x, texture.w) *
        lighting;
    vec3 tip_color =
        mix(tip, texture.rgb * flags.y, texture.w) *
        lighting;
    root_color =
        floor(clamp(root_color, 0.0, 1.0) * 255.0 + 0.5) /
        255.0;
    tip_color =
        floor(clamp(tip_color, 0.0, 1.0) * 255.0 + 0.5) /
        255.0;
    return mix(root_color, tip_color, height_factor);
}

vec2 evaluate_wind(
    uint environment_index,
    vec3 world_position, float anchor_phase) {
    vec4 wind_direction_time =
        environment_state.records[
            environment_index].wind_direction_time;
    vec4 wind_primary =
        environment_state.records[
            environment_index].wind_primary;
    vec4 wind_detail =
        environment_state.records[
            environment_index].wind_detail;
    if (wind_detail.w <= 0.5)
        return vec2(0.0);
    vec2 direction = wind_direction_time.xy;
    vec2 lateral =
        vec2(-direction.y, direction.x);
    float spatial =
        dot(world_position.xz, direction) * 0.01 *
        wind_primary.y;
    float random_phase =
        anchor_phase *
        clamp(wind_detail.y, 0.0, 1.0);
    float primary = sin(
        wind_direction_time.z * wind_primary.x +
        spatial + random_phase);
    float gust_phase =
        wind_direction_time.z *
            wind_primary.w * 6.28318530718 +
        spatial * 0.21 + random_phase * 0.37;
    float gust = 0.5 + 0.5 * sin(gust_phase);
    float turbulence = sin(
        wind_direction_time.z *
            (wind_primary.x * 1.71 + 0.31) -
        world_position.x * 0.017 +
        world_position.z * 0.013 +
        random_phase * 2.13);
    float directional_amount =
        wind_direction_time.w *
        (0.55 + primary * 0.30 +
         gust * wind_primary.z * 0.45);
    float lateral_amount =
        wind_direction_time.w *
        wind_detail.x * turbulence * 0.35;
    vec2 bend =
        direction * directional_amount +
        lateral * lateral_amount;
    float bend_length = length(bend);
    if (bend_length > wind_detail.z &&
        bend_length > 1.0e-6)
        bend *= wind_detail.z / bend_length;
    return bend;
}

void main() {
    bool tuft = grass.flags.y == 0u;
    uint segments = clamp(grass.flags.y, 1u, 12u);
    uint plane;
    float height_factor;
    float width_sign;
    grass_indexed_vertex(uint(gl_VertexIndex), segments, tuft, plane, height_factor, width_sign);
    float tuft_coverage = 1.0;
    vec4 lod = environment_state.records[grass.flags.w].tuft_lod;
    vec4 distance_lod = environment_state.records[grass.flags.w].distance_lod;
    vec4 tuft_style = environment_state.records[grass.flags.w].tuft_style;
    float distance = length(in_base_height.xyz - environment_state.records[grass.flags.w].camera_position.xyz);
    float normalized_distance = distance/max(lod.y,1.0e-6);
    float tuft_weight = grass_tuft_weight(normalized_distance,lod.x,lod.x+lod.w);
    float retention = grass_density_retention(normalized_distance,distance_lod.y,distance_lod.z);
    if (tuft_style.w > 0.5)
        retention *= grass_tuft_retention_scale(tuft_weight,lod.z,tuft_style.y);
    lod_visibility = grass_visibility_fade(clamp(retention,0.0,1.0),in_world_normal.w,distance_lod.w);
    lod_visibility *= 1.0-grass_tuft_weight(distance,distance_lod.x*(1.0-tuft_style.z),distance_lod.x);
    if (tuft) {
        float choice = grass_lod_choice(in_world_normal.w);
        float growth = clamp((tuft_weight-choice)/max(1.0-choice,1.0e-6),0.0,1.0);
        tuft_coverage += (lod.z-1.0) * growth * tuft_style.x;
    }
    tuft_sample = vec4(width_sign, height_factor, tuft_coverage, in_bend_half_width.w);

    vec2 width_axis = in_width_axis;
    if (grass.flags.z == 1u) {
        vec2 camera_delta =
            in_base_height.xz -
            environment_state.records[
                grass.flags.w].camera_position.xz;
        float camera_distance = length(camera_delta);
        width_axis = camera_distance > 1.0e-6
            ? vec2(
                  camera_delta.y / camera_distance,
                  -camera_delta.x / camera_distance)
            : vec2(1.0, 0.0);
    } else {
        width_axis = normalize(width_axis);
    }
    if (plane != 0u)
        width_axis = vec2(-width_axis.y, width_axis.x);
    float bend_factor =
        height_factor * (0.65 + 0.35 * height_factor);
    float width_factor =
        tuft ? tuft_coverage : height_factor >= 1.0 ? 0.0 :
        1.0 - 0.75 * height_factor;
    vec2 normalized_bend =
        evaluate_wind(
            grass.flags.w, in_base_height.xyz,
            in_bend_half_width.w) +
        in_bend_half_width.xy;
    float bend_length = length(normalized_bend);
    float maximum_bend =
        environment_state.records[
            grass.flags.w].wind_detail.z;
    if (bend_length > maximum_bend &&
        bend_length > 1.0e-6)
        normalized_bend *=
            maximum_bend / bend_length;
    vec2 bend = normalized_bend * in_base_height.w;
    float twist;
    vec3 shape = grass_blade_shape(
        height_factor, in_bend_half_width.w, normalize(in_width_axis),
        environment_state.records[grass.flags.w].blade_shape, twist);
    if (!tuft) width_axis = mat2(cos(twist), sin(twist), -sin(twist), cos(twist)) * width_axis;
    vec3 position = in_base_height.xyz;
    position += shape * in_base_height.w;
    position.xz += bend * bend_factor;
    position.xz += width_axis * in_bend_half_width.z *
                   width_factor * width_sign;

    vec4 clip = grass.position_to_clip * vec4(position, 1.0);
    if (grass.flags.x != 0u) {
        // Match the generated PICA vertex shader exactly. Its projection is
        // already rotated for the physical CTR framebuffer.
        gl_Position = vec4(clip.x, clip.y, -clip.z, clip.w);
    } else {
        // Renderer-owned world projections are logical/unrotated and must be
        // transposed into the physical top framebuffer before scanout.
        gl_Position = vec4(clip.y, clip.x, clip.z, clip.w);
    }
    gl_Position.xy += grass.jitter_ndc.xy * gl_Position.w;
    vec3 world_normal = normalize(in_world_normal.xyz);
    vec3 lighting;
    vec3 ambient_response;
    evaluate_shading(
        grass.flags.w, world_normal,
        lighting, ambient_response);
    blade_color = vec4(
        evaluate_blade_color(
            grass.flags.w, height_factor, lighting),
        1.0);
    vec3 view_side =
        environment_state.records[
            grass.flags.w].view_side.xyz;
    vec3 guide_normal =
        dot(view_side, view_side) > 1.0e-8
            ? vec3(
                  dot(view_side, world_normal),
                  dot(
                      environment_state.records[
                          grass.flags.w].view_up.xyz,
                      world_normal),
                  -dot(
                      environment_state.records[
                          grass.flags.w].view_forward.xyz,
                      world_normal))
            : vec3(0.0, 0.0, 1.0);
    blade_normal_guide = vec4(
        guide_normal * 0.5 + 0.5,
        0.25098039215686274);
    blade_ambient_guide =
        vec4(ambient_response, 1.0);
}
)glsl";
        static const std::string fragmentSource = std::string("#version 450\n") +
            std::string(kGrassDistantTuftShader) + R"glsl(
layout(location=0) in vec4 blade_color;
layout(location=1) in vec4 blade_normal_guide;
layout(location=2) in vec4 blade_ambient_guide;
layout(location=3) in vec4 tuft_sample;
layout(location=4) flat in float lod_visibility;
struct GrassEnvironmentRecord {
    vec4 color_and_mode;
    vec2 lut[128];
    vec4 wind_direction_time;
    vec4 wind_primary;
    vec4 wind_detail;
    vec4 camera_position;
    vec4 appearance_root;
    vec4 appearance_tip;
    vec4 texture_color;
    vec4 texture_brightness_flags;
    vec4 light_directions[3];
    vec4 light_diffuse[3];
    vec4 light_ambient[3];
    vec4 view_forward;
    vec4 view_side;
    vec4 view_up;
    vec4 blade_shape;
    vec4 tuft_lod;
    vec4 distance_lod;
    vec4 tuft_style;
};
layout(std430, set=0, binding=0) readonly buffer GrassEnvironmentState {
    GrassEnvironmentRecord records[];
} environment_state;
layout(push_constant) uniform GrassState {
    mat4 position_to_clip;
    vec4 jitter_ndc;
    vec4 depth_state;
    uvec4 flags;
} grass;
layout(location=0) out vec4 out_color;
#if GRASS_AUXILIARY_OUTPUTS
layout(location=1) out vec4 out_normal_guide;
layout(location=2) out vec4 out_material_guide;
layout(location=3) out vec4 out_rigid_motion;
layout(location=4) out vec4 out_ambient_guide;
#endif
void main() {
    // Blade-local stipple, not frame/screen-space noise. Discard before every
    // guide/depth write; fading geometry cannot leave invisible occluders.
    if (lod_visibility < 1.0) {
        vec2 cell = floor(vec2(tuft_sample.x*tuft_sample.z,tuft_sample.y)*32.0);
        float threshold = fract(sin(dot(cell,vec2(12.9898,78.233))+tuft_sample.w*37.719)*43758.5453);
        if (lod_visibility <= threshold) discard;
    }
    if (grass.flags.y == 0u && !grass_tuft_covered(tuft_sample,
            uint(environment_state.records[grass.flags.w].tuft_lod.z),
            environment_state.records[grass.flags.w].tuft_style.x)) discard;
    vec4 resolved_color = blade_color;
#if GRASS_AUXILIARY_OUTPUTS
    out_normal_guide = blade_normal_guide;
    // Grass topology, LOD and wind can change every presentation. Let the
    // motion pass reconstruct camera motion from depth and reject temporal
    // history for these pixels instead of emitting unstable rigid vectors.
    out_material_guide = vec4(0.0, 1.0, 0.0, 1.0);
    out_ambient_guide = blade_ambient_guide;
    out_rigid_motion = vec4(0.0);
#endif
    float resolved_depth = gl_FragCoord.z;
    if (grass.depth_state.w > 0.5) {
        float pica_z_over_w =
            grass.flags.x != 0u ? -gl_FragCoord.z : gl_FragCoord.z;
        resolved_depth =
            pica_z_over_w * grass.depth_state.x +
            grass.depth_state.y;
        if (grass.depth_state.z > 0.5)
            resolved_depth /= max(gl_FragCoord.w, 1.0e-7);
    }
    vec4 fog_color_and_mode =
        environment_state.records[
            grass.flags.w].color_and_mode;
    if (fog_color_and_mode.w > 0.5) {
        float fog_depth =
            fog_color_and_mode.w > 1.5
                ? 1.0 - resolved_depth
                : resolved_depth;
        float fog_index = fog_depth * 128.0;
        float floor_index = clamp(floor(fog_index), 0.0, 127.0);
        vec2 sample_pair =
            environment_state.records[
                grass.flags.w].lut[
                uint(floor_index)];
        float fog_factor = clamp(
            sample_pair.x +
                sample_pair.y * (fog_index - floor_index),
            0.0, 1.0);
        resolved_color.rgb = mix(
            fog_color_and_mode.rgb,
            resolved_color.rgb, fog_factor);
    }
    out_color = resolved_color;
    gl_FragDepth = clamp(resolved_depth, 0.0, 1.0);
#if GRASS_AUXILIARY_OUTPUTS
    // Only rasterized, depth-visible blades/tufts occlude native contours.
    // The separate native geometry guide remains untouched.
    out_rigid_motion.a = 1.0 - gl_FragDepth;
#endif
}
)glsl";
        VkShaderModule vertex = Compile(device, vertexSource.c_str(),
                                        shaderc_vertex_shader, "interactive_grass.vert");
        std::array<VkShaderModule, 2> fragments{};
        try {
            fragments[0] = Compile(
                device, fragmentSource.c_str(), shaderc_fragment_shader,
                "interactive_grass_canonical.frag",
                "GRASS_AUXILIARY_OUTPUTS", "0");
            fragments[1] = Compile(
                device, fragmentSource.c_str(), shaderc_fragment_shader,
                "interactive_grass_instrumented.frag",
                "GRASS_AUXILIARY_OUTPUTS", "1");
            const VkDescriptorSetLayoutBinding environmentBinding{
                0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U,
                VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr};
            VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            descriptorLayoutInfo.bindingCount = 1U;
            descriptorLayoutInfo.pBindings = &environmentBinding;
            if (vkCreateDescriptorSetLayout(
                    device, &descriptorLayoutInfo, nullptr,
                    &mImpl->DescriptorSetLayout) != VK_SUCCESS) {
                throw std::runtime_error(
                    "cannot create interactive grass descriptor layout");
            }
            const VkDescriptorPoolSize poolSize{
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                static_cast<uint32_t>(
                    mImpl->DescriptorSets.size())};
            VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets =
                static_cast<uint32_t>(
                    mImpl->DescriptorSets.size());
            poolInfo.poolSizeCount = 1U;
            poolInfo.pPoolSizes = &poolSize;
            if (vkCreateDescriptorPool(
                    device, &poolInfo, nullptr,
                    &mImpl->DescriptorPool) != VK_SUCCESS) {
                throw std::runtime_error(
                    "cannot create interactive grass descriptor pool");
            }
            std::array<VkDescriptorSetLayout, 2> descriptorLayouts{
                mImpl->DescriptorSetLayout,
                mImpl->DescriptorSetLayout};
            VkDescriptorSetAllocateInfo descriptorAllocate{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            descriptorAllocate.descriptorPool =
                mImpl->DescriptorPool;
            descriptorAllocate.descriptorSetCount =
                static_cast<uint32_t>(
                    descriptorLayouts.size());
            descriptorAllocate.pSetLayouts =
                descriptorLayouts.data();
            if (vkAllocateDescriptorSets(
                    device, &descriptorAllocate,
                    mImpl->DescriptorSets.data()) != VK_SUCCESS) {
                throw std::runtime_error(
                    "cannot allocate interactive grass descriptors");
            }
            VkPushConstantRange push{
                                     VK_SHADER_STAGE_VERTEX_BIT |
                                         VK_SHADER_STAGE_FRAGMENT_BIT,
                                     0,
                                     sizeof(GrassPushConstants)};
            VkPipelineLayoutCreateInfo layoutInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1U;
            layoutInfo.pSetLayouts =
                &mImpl->DescriptorSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &push;
            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                                       &mImpl->PipelineLayout) != VK_SUCCESS)
                throw std::runtime_error("cannot create interactive grass pipeline layout");

            VkPipelineShaderStageCreateInfo stages[] = {
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragments[0], "main", nullptr}};
            VkVertexInputBindingDescription binding{
                0, sizeof(GrassInstance),
                VK_VERTEX_INPUT_RATE_INSTANCE};
            const VkVertexInputAttributeDescription attributes[] = {
                {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(GrassInstance, BaseHeight)},
                {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(GrassInstance, BendAndHalfWidth)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT,
                 offsetof(GrassInstance, WidthAxis)},
                {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(GrassInstance, WorldNormal)}};
            VkPipelineVertexInputStateCreateInfo vertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(std::size(attributes));
            vertexInput.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0F;
            VkPipelineMultisampleStateCreateInfo multisample{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = 0xFU;
            VkPipelineColorBlendAttachmentState guideAttachment{};
            guideAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendAttachmentState motionAttachment{};
            ConfigureOutlineCoverageBlend(motionAttachment,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
            const std::array<VkPipelineColorBlendAttachmentState,
                             kPicaColorAttachmentCount>
                attachments{attachment, guideAttachment,
                            guideAttachment, motionAttachment,
                            guideAttachment};
            VkPipelineColorBlendStateCreateInfo blend{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.pAttachments = attachments.data();
            const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamicStates;
            VkGraphicsPipelineCreateInfo pipeline{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipeline.stageCount = 2;
            pipeline.pStages = stages;
            pipeline.pVertexInputState = &vertexInput;
            pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport;
            pipeline.pRasterizationState = &raster;
            pipeline.pMultisampleState = &multisample;
            pipeline.pDepthStencilState = &depth;
            pipeline.pColorBlendState = &blend;
            pipeline.pDynamicState = &dynamic;
            pipeline.layout = mImpl->PipelineLayout;
            const std::array<VkFormat, kPicaColorAttachmentCount> colorFormats{
                VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                VK_FORMAT_R32G32B32A32_SFLOAT
            };
            VkPipelineRenderingCreateInfoKHR rendering{
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
            if (dynamicRendering) {
                if (depthFormat == VK_FORMAT_UNDEFINED)
                    throw std::runtime_error(
                        "interactive grass dynamic depth format is invalid");
                rendering.pColorAttachmentFormats = colorFormats.data();
                rendering.depthAttachmentFormat = depthFormat;
                if (depthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
                    depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
                    depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
                    rendering.stencilAttachmentFormat = depthFormat;
                pipeline.pNext = &rendering;
                pipeline.renderPass = VK_NULL_HANDLE;
            }
            const std::array<uint32_t, 2> attachmentCounts{
                1U, static_cast<uint32_t>(colorFormats.size())};
            const std::array<VkRenderPass, 2> renderPasses{
                canonicalRenderPass, instrumentedRenderPass};
            for (size_t index = 0U; index < mImpl->Pipelines.size(); ++index) {
                stages[1].module = fragments[index];
                blend.attachmentCount = attachmentCounts[index];
                if (dynamicRendering) {
                    rendering.colorAttachmentCount = attachmentCounts[index];
                } else {
                    if (renderPasses[index] == VK_NULL_HANDLE) {
                        throw std::runtime_error(
                            "interactive grass render pass is unavailable");
                    }
                    pipeline.renderPass = renderPasses[index];
                }
                if (vkCreateGraphicsPipelines(
                        device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                        &mImpl->Pipelines[index]) != VK_SUCCESS) {
                    throw std::runtime_error(
                        "cannot create interactive grass pipeline");
                }
            }
        } catch (...) {
            for (const auto fragment : fragments) {
                if (fragment != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, fragment, nullptr);
                }
            }
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        for (const auto fragment : fragments) {
            vkDestroyShaderModule(device, fragment, nullptr);
        }
        vkDestroyShaderModule(device, vertex, nullptr);
        mImpl->GpuCompactorAvailable =
            mImpl->InstanceCompactor.Initialize(
                physicalDevice, device);
        mImpl->IndexedTopology = BuildGrassIndexedTopology();
        const auto indexBytes = mImpl->IndexedTopology.Indices.size() * sizeof(uint16_t);
        mImpl->EnsureBuffer(mImpl->IndexBuffer, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 4096);
        std::memcpy(mImpl->IndexBuffer.Mapped, mImpl->IndexedTopology.Indices.data(), indexBytes);
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
}

bool InteractiveGrassPass::Prepare(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height,
                                   const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view,
                                   const InteractiveGrassSettings& settings,
                                   uint64_t frameId, uint64_t renderTargetNamespace,
                                   uint32_t framebufferColorPhysicalAddress, uint32_t frameSlot,
                                   const EffectGeometryProviderPlan& providerPlan,
                                   const std::array<float, 2>& jitterPixels, bool temporalJitter) {
    mImpl->Prepared = false;
    mImpl->PreparedInstanceBuffer = VK_NULL_HANDLE;
    mImpl->LastBlades = 0;
    mImpl->LastMotionBlades = 0;
    const auto cpuStart = std::chrono::steady_clock::now();
    GrassRenderTelemetrySnapshot telemetry;
    telemetry.FrameId = frameId;
    telemetry.AvailableMeshes = GrassSceneBridge::Instance().Size();
    telemetry.AvailableTextures = GrassSceneBridge::Instance().AvailableTextures().size();
    telemetry.ConfiguredRules = settings.Rules.size();
    const auto placementStatsBefore =
        mImpl->PlacementBuilder.Stats();
    const auto finish = [this, &telemetry, cpuStart,
                         placementStatsBefore](
                            GrassRenderStatus status, bool executed) {
        const auto cpuEnd = std::chrono::steady_clock::now();
        const auto placementStatsAfter =
            mImpl->PlacementBuilder.Stats();
        telemetry.WorldPlacementCacheHits =
            placementStatsAfter.Hits -
            placementStatsBefore.Hits;
        telemetry.WorldPlacementCacheMisses =
            placementStatsAfter.Misses -
            placementStatsBefore.Misses;
        telemetry.WorldPlacementCacheEntries =
            placementStatsAfter.Entries;
        telemetry.PendingPlacements =
            placementStatsAfter.Pending;
        telemetry.AsyncPlacementBuilds =
            placementStatsAfter.Builds;
        telemetry.AsyncPlacementFailures =
            placementStatsAfter.Failures;
        telemetry.AsyncPlacementBuildMilliseconds =
            placementStatsAfter.BuildMilliseconds;
        telemetry.CpuMilliseconds = std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();
        telemetry.Status = status;
        telemetry.Executed = executed;
        GrassRenderTelemetry::Instance().Publish(telemetry);
        return executed;
    };
    if (!providerPlan.Valid() ||
        providerPlan.Stage() != EffectStage::BeforeTransparent ||
        !providerPlan.ProducesExtensionGeometry() ||
        !providerPlan.ExportsExtensionGeometry()) {
        return finish(GrassRenderStatus::ContractUnavailable, false);
    }
    if (settings.Quality == GrassQuality::Off) {
        return finish(GrassRenderStatus::Disabled, false);
    }
    if (mImpl->Pipelines[0] == VK_NULL_HANDLE ||
        mImpl->Pipelines[1] == VK_NULL_HANDLE) {
        return finish(GrassRenderStatus::PipelineUnavailable, false);
    }
    if (!view.CameraAvailable) {
        return finish(GrassRenderStatus::CameraUnavailable, false);
    }
    if (settings.Rules.empty()) {
        return finish(GrassRenderStatus::NoRules, false);
    }
    if (settings.MaxInstancesPerRoom == 0) {
        return finish(GrassRenderStatus::NoBudget, false);
    }
    try {
        const auto actors = GrassInteractionBridge::Instance().LatestActors();
        const auto clock = VisualClock::Instance().Snapshot();
        float deltaSeconds = 0.0F;
        double visualSeconds = 0.0;
        if (clock.Available) {
            visualSeconds = clock.Seconds;
            if (!mImpl->HasClockSequence || clock.Sequence != mImpl->LastClockSequence) {
                deltaSeconds = clock.DeltaSeconds;
                mImpl->LastClockSequence = clock.Sequence;
                mImpl->HasClockSequence = true;
            }
        } else {
            if (!mImpl->HasFallbackFrame || frameId != mImpl->LastFallbackFrame) {
                deltaSeconds = 1.0F / 60.0F;
                mImpl->FallbackSeconds += deltaSeconds;
                mImpl->LastFallbackFrame = frameId;
                mImpl->HasFallbackFrame = true;
            }
            visualSeconds = mImpl->FallbackSeconds;
        }
        mImpl->InteractionField.UpdateActors(deltaSeconds, settings, actors);
        auto& batches = mImpl->Batches;
        auto& environmentRecords = mImpl->EnvironmentRecords;
        auto& preparedPlacements = mImpl->PreparedPlacements;
        environmentRecords.clear();
        preparedPlacements.clear();
        auto& placementRequests = mImpl->PlacementRequests;
        placementRequests.clear();
        batches.reserve(settings.Rules.size() * 4U);
        environmentRecords.reserve(settings.Rules.size());
        preparedPlacements.reserve(settings.Rules.size() * 2U);
        mImpl->VisibleAnchorIndices.reserve(settings.MaxInstancesPerRoom);
        const size_t slotIndex = frameSlot % mImpl->InstanceBuffers.size();
        const std::array<float, 2> currentJitterNdc =
            temporalJitter ? std::array<float, 2>{ 2.0F * jitterPixels[0] / static_cast<float>(width),
                                                   2.0F * jitterPixels[1] / static_cast<float>(height) }
                           : std::array<float, 2>{};
        const GrassSceneScope sceneScope{ frameId, renderTargetNamespace, framebufferColorPhysicalAddress };
        const auto viewForward =
            Normalize({ view.At[0] - view.Eye[0], view.At[1] - view.Eye[1], view.At[2] - view.Eye[2] });
        const auto viewSide = Normalize(Cross(viewForward, { 0.0F, 1.0F, 0.0F }));
        const auto viewUp = Cross(viewSide, viewForward);

        for (const auto& rule : settings.Rules) {
            const auto mask =
                GrassTextureSourceCache::Instance().AcquireMaskShared(rule.Target.Rgba8Hash, rule.Channel);
            if (mask == nullptr || mask->Samples.empty())
                continue;
            ++telemetry.ReadyMasks;
            const auto textureAverage = GrassTextureSourceCache::Instance().AcquireAverageColor(rule.Target.Rgba8Hash);
            auto sceneMatch = GrassSceneBridge::Instance().Match(rule.Target, &sceneScope);
            telemetry.MatchingMeshes += sceneMatch.MatchingMeshes;
            const auto& scopedMeshes = sceneMatch.ScopedMeshes;
            telemetry.ScopedMeshes += scopedMeshes.size();
            for (const auto& mesh : scopedMeshes) {
                if (mesh.Vertices == nullptr || mesh.Indices == nullptr) {
                    continue;
                }
                uint64_t placementVersion = mesh.ContentVersion ^ GrassPlacementRuleVersion(rule, settings.Generation);
                placementVersion ^= static_cast<uint64_t>(std::bit_cast<uint32_t>(settings.CullingClusterSize)) +
                                    0x9e3779b97f4a7c15ULL + (placementVersion << 6U) + (placementVersion >> 2U);
                GrassPlacementKey key{ mesh.GeometryId, placementVersion, mesh.TextureHash, rule.RuleId };
                GrassAsyncPlacementRequest placementRequest;
                placementRequest.PlacementKey = key;
                placementRequest.SourceContentVersion =
                    mesh.ContentVersion;
                placementRequest.WorldIdentity =
                    GrassWorldPlacementIdentity(mesh, key);
                placementRequest.FrameId = frameId;
                placementRequest.InstanceId = mesh.InstanceId;
                placementRequest.TextureHash = mesh.TextureHash;
                placementRequest.MapperSlot = mesh.MapperSlot;
                placementRequest.MaterialWrapS =
                    mesh.MaterialWrapS;
                placementRequest.MaterialWrapT =
                    mesh.MaterialWrapT;
                placementRequest.ModelToWorld =
                    mesh.ModelToWorld;
                placementRequest.TransformBakedIntoVertices =
                    mesh.TransformBakedIntoVertices;
                placementRequest.Vertices = mesh.Vertices;
                placementRequest.Indices = mesh.Indices;
                placementRequest.Mask = mask;
                placementRequest.Rule = rule;
                placementRequest.Generation =
                    settings.Generation;
                placementRequest.Budget =
                    settings.MaxInstancesPerRoom;
                placementRequest.ClusterSize =
                    settings.CullingClusterSize;
                placementRequest.NormalOffset =
                    rule.NormalOffset;
                placementRequest.HeightScale =
                    settings.Appearance.HeightScale;
                GrassPushConstants push = ProjectionForMesh(mesh, view);
                push.JitterNdc[0] = currentJitterNdc[0];
                push.JitterNdc[1] = currentJitterNdc[1];
                push.DepthState = { mesh.Shading.Depth.Scale, mesh.Shading.Depth.Offset,
                                    mesh.Shading.Depth.WBuffering ? 1.0F : 0.0F,
                                    mesh.Shading.Depth.Valid && push.Flags[0] != 0U ? 1.0F : 0.0F };
                GrassPreparedPlacement prepared;
                prepared.Push = push;
                prepared.FrustumRadiusScale = BuildGrassFrustumRadiusScale(push.PositionToClip);
                prepared.Eye = GrassCameraFromProjection(push.PositionToClip).value_or(view.Eye);
                prepared.Environment = BuildGrassEnvironmentRecord(
                    mesh.Shading.Fog, settings.Appearance.ReceiveFog, push.DepthState[3] > 0.5F, settings, mesh.Shading,
                    textureAverage, visualSeconds, view, viewForward, viewSide, viewUp);
                prepared.Environment.CameraPosition = {
                    prepared.Eye[0], prepared.Eye[1], prepared.Eye[2], 0.0F};
                placementRequests.push_back(std::move(placementRequest));
                preparedPlacements.push_back(std::move(prepared));
            }
        }

        const auto placementStart = std::chrono::steady_clock::now();
        // The pass already handles pending placements and retries next frame.
        // Do not stop the Mac presentation/game clock while workers generate
        // the title's dense grass; publish each immutable result when ready.
#if defined(__APPLE__)
        auto placements = mImpl->PlacementBuilder.Resolve(placementRequests, false);
#else
        auto placements = mImpl->PlacementBuilder.Resolve(placementRequests);
#endif
        telemetry.PlacementMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - placementStart).count();
        for (size_t i = 0; i < placements.size(); ++i) {
            auto& result = placements[i];
            if (result.Queued) {
                ++telemetry.PlacementCacheMisses;
                telemetry.LastMissGeometryId = placementRequests[i].PlacementKey.GeometryId;
                telemetry.LastMissContentVersion = placementRequests[i].SourceContentVersion;
            } else if (result.State == GrassAsyncPlacementState::Ready) ++telemetry.PlacementCacheHits;
            if (!result.Placement || result.Placement->Anchors.empty() ||
                result.Placement->CullingAnchors.size() != result.Placement->Anchors.size()) continue;
            ++telemetry.Placements;
            telemetry.ExtractedAnchors += result.Placement->Anchors.size();
            telemetry.Clusters += result.Placement->Clusters.size();
            preparedPlacements[i].World = std::move(result.Placement);
        }
        std::erase_if(preparedPlacements, [](const auto& placement) { return placement.World == nullptr; });

        if (preparedPlacements.empty()) {
            if (telemetry.ReadyMasks == 0U) {
                return finish(GrassRenderStatus::NoMasks, false);
            }
            if (telemetry.MatchingMeshes == 0U) {
                return finish(GrassRenderStatus::NoMatchingTexture, false);
            }
            if (telemetry.ScopedMeshes == 0U) {
                return finish(GrassRenderStatus::NoScopedSurfaces, false);
            }
            if (mImpl->PlacementBuilder.Stats().Pending != 0U) {
                return finish(
                    GrassRenderStatus::PlacementPending, false);
            }
            if (telemetry.ExtractedAnchors == 0U) {
                return finish(GrassRenderStatus::NoAnchors, false);
            }
            return finish(GrassRenderStatus::Culled, false);
        }

        std::vector<Impl::ActivePlacement> activePlacements;
        activePlacements.reserve(preparedPlacements.size());
        size_t totalStaticAnchors = 0U;
        for (auto& prepared : preparedPlacements) {
            if (prepared.World->Anchors.size() > std::numeric_limits<uint32_t>::max() - totalStaticAnchors) {
                throw std::runtime_error("interactive grass static anchor index overflow");
            }
            prepared.StaticBaseIndex = static_cast<uint32_t>(totalStaticAnchors);
            totalStaticAnchors += prepared.World->Anchors.size();
            activePlacements.push_back(
                { prepared.World->Identity, prepared.World->ContentVersion, prepared.World->Anchors.size() });
        }
        if (activePlacements != mImpl->ActivePlacements) {
            mImpl->ActivePlacements = std::move(activePlacements);
            mImpl->StaticAnchors.clear();
            mImpl->StaticAnchors.reserve(totalStaticAnchors);
            for (const auto& prepared : preparedPlacements) {
                mImpl->StaticAnchors.insert(mImpl->StaticAnchors.end(), prepared.World->Anchors.begin(),
                                            prepared.World->Anchors.end());
            }
            ++mImpl->StaticRevision;
            if (mImpl->StaticRevision == 0U) {
                ++mImpl->StaticRevision;
            }
        }

        const float drawDistanceSquared = settings.DrawDistance * settings.DrawDistance;
        const auto lodPolicy = BuildGrassLodPolicy(settings);
        const float bladeRadiusScale = GrassBladeRadiusScale(
            settings.Appearance.BladeCurvature,
            settings.Appearance.ShapeVariation, settings.MaximumBend) +
            (settings.FarTuftsEnabled ? settings.Generation.BladeWidthMax *
                (1.0F + (settings.FarTuftBladeCount - 1U) * settings.FarTuftSpread) /
                std::max(settings.Generation.BladeHeightMin, 0.001F) : 0.0F);
        uint32_t remaining = settings.MaxInstancesPerRoom;
        GrassSelectionKey selectionKey{mImpl->StaticRevision, lodPolicy, bladeRadiusScale,
            settings.MaxInstancesPerRoom, settings.FrustumCulling, {}};
        for (const auto& prepared : preparedPlacements) {
            selectionKey.Views.push_back({prepared.Push.PositionToClip, prepared.Eye});
            environmentRecords.push_back(prepared.Environment);
        }
        const bool reuseSelection = mImpl->SelectionCache.Matches(selectionKey);
        if (reuseSelection) {
            mImpl->LastBlades = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
            telemetry.VisibleBlades = mImpl->LastBlades;
            for (auto& batch : batches) {
                batch.Push = preparedPlacements[batch.PlacementIndex].Push;
                batch.Push.Flags[1] = batch.BladeSegments;
                batch.Push.Flags[2] = batch.PlaneCount;
                batch.Push.Flags[3] = batch.PlacementIndex;
            }
        } else {
            mImpl->SelectionCache.Reset();
            batches.clear();
            mImpl->VisibleAnchorIndices.clear();
            for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& prepared = preparedPlacements[placementIndex];
                if (remaining == 0U) {
                    break;
                }
                for (auto& bin : mImpl->VisibleIndexBins) {
                    bin.clear();
                }
                const auto processCluster = [&](const GrassClusterWork& work, GrassVisibleIndexBins& outputBins,
                                                uint32_t visibleLimit) {
                    GrassClusterCounters counters;
                    if (visibleLimit == 0U) {
                        return counters;
                    }
                    const auto& cluster = prepared.World->Clusters[work.ClusterIndex];
                    for (uint32_t anchorIndex = cluster.FirstAnchor;
                         anchorIndex < work.AnchorEnd && counters.Visible < visibleLimit; ++anchorIndex) {
                        ++counters.Evaluated;
                        const auto& anchor = prepared.World->CullingAnchors[anchorIndex];
                        const float dx = anchor.Position[0] - prepared.Eye[0];
                        const float dy = anchor.Position[1] - prepared.Eye[1];
                        const float dz = anchor.Position[2] - prepared.Eye[2];
                        const float distanceSquared = dx * dx + dy * dy + dz * dz;
                        if (distanceSquared > drawDistanceSquared) {
                            continue;
                        }
                        const float distance = std::sqrt(distanceSquared);
                        const auto lod =
                            ResolveGrassLodWithStableVisibility(lodPolicy, distance, anchor.StableVisibility);
                        if (!lod.Visible) {
                            continue;
                        }
                        if (work.CullIndividualBlades) {
                            const auto& fullAnchor = prepared.World->Anchors[anchorIndex];
                            const std::array<float, 3> bladeCenter{
                                fullAnchor.BaseHeight[0],
                                fullAnchor.BaseHeight[1],
                                fullAnchor.BaseHeight[2],
                            };
                            if (!GrassSphereIntersectsFrustum(prepared.Push.PositionToClip, prepared.FrustumRadiusScale,
                                                              bladeCenter,
                                                              fullAnchor.BaseHeight[3] * bladeRadiusScale +
                                                                  fullAnchor.HalfWidthPhase[0] * 2.0F)) {
                                continue;
                            }
                        }
                        outputBins[lod.DistantTuft ? kGrassLodBinCount - 1U
                                                   : GrassLodBinIndex(lod.BladeSegments, lod.PlaneCount)]
                            .push_back(prepared.StaticBaseIndex + anchorIndex);
                        ++counters.Visible;
                    }
                    return counters;
                };

                const auto selectionStart = std::chrono::steady_clock::now();
                auto& clusterWork = mImpl->ClusterWork;
                const auto selection = SelectGrassClusterWork(
                    *prepared.World, prepared.Push.PositionToClip, prepared.Eye, lodPolicy,
                    bladeRadiusScale, settings.FrustumCulling, clusterWork);
                const auto clusterSelectionEnd = std::chrono::steady_clock::now();
                telemetry.VisibilityNodesTested += selection.TestedNodes;
                telemetry.CandidateClusters += selection.CandidateClusters;
                const auto retainedCandidates = selection.CandidateAnchors;
                size_t parallelWorkerCount = 0U;
                if (retainedCandidates >= kGrassAnchorsPerWorker * 2U &&
                    remaining >= kGrassAnchorsPerWorker && clusterWork.size() > 1U) {
                    parallelWorkerCount =
                        std::min({ mImpl->WorkerCount(), clusterWork.size(),
                                   static_cast<size_t>(retainedCandidates / kGrassAnchorsPerWorker) });
                }
                if (parallelWorkerCount > 1U) {
                    telemetry.CullingWorkers =
                        std::max(telemetry.CullingWorkers, static_cast<uint32_t>(parallelWorkerCount));
                    mImpl->WorkerOutputs.resize(parallelWorkerCount);
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        mImpl->WorkerOutputs[workerIndex].Clear();
                    }
                    std::vector<std::future<void>> tasks;
                    tasks.reserve(parallelWorkerCount);
                    const size_t clustersPerWorker =
                        (clusterWork.size() + parallelWorkerCount - 1U) / parallelWorkerCount;
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        const size_t beginCluster = workerIndex * clustersPerWorker;
                        const size_t endCluster = std::min(beginCluster + clustersPerWorker, clusterWork.size());
                        tasks.push_back(mImpl->WorkerPool->submit_task([&, workerIndex, beginCluster, endCluster] {
                            auto& output = mImpl->WorkerOutputs[workerIndex];
                            uint32_t workerRemaining = remaining;
                            for (size_t clusterIndex = beginCluster; clusterIndex < endCluster && workerRemaining != 0;
                                 ++clusterIndex) {
                                const auto counters = processCluster(clusterWork[clusterIndex], output.Bins, workerRemaining);
                                output.Counters.Evaluated += counters.Evaluated;
                                output.Counters.Visible += counters.Visible;
                                workerRemaining -= counters.Visible;
                            }
                        }));
                    }
                    for (auto& task : tasks) {
                        task.get();
                    }
                    uint32_t visible = 0U;
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        auto& output = mImpl->WorkerOutputs[workerIndex];
                        auto& counters = output.Counters;
                        telemetry.EvaluatedAnchors += counters.Evaluated;
                        counters.Visible = LimitGrassSelectionBins(output.Bins, remaining);
                        remaining -= counters.Visible;
                        visible += counters.Visible;
                    }
                    mImpl->LastBlades += visible;
                    telemetry.VisibleBlades += visible;
                } else {
                    telemetry.CullingWorkers = std::max(telemetry.CullingWorkers, 1U);
                    for (const auto& work : clusterWork) {
                        if (remaining == 0U) {
                            break;
                        }
                        const auto counters = processCluster(work, mImpl->VisibleIndexBins, remaining);
                        telemetry.EvaluatedAnchors += counters.Evaluated;
                        remaining -= counters.Visible;
                        mImpl->LastBlades += counters.Visible;
                        telemetry.VisibleBlades += counters.Visible;
                    }
                }
                telemetry.SelectionMilliseconds +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - selectionStart)
                        .count();
                if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr && frameId % 60 == 0) {
                    std::fprintf(stderr, "[grass-selection] frame=%llu source=%u clusters=%zu candidates=%llu cluster_ms=%.3f evaluate_ms=%.3f\n",
                        static_cast<unsigned long long>(frameId), placementIndex, clusterWork.size(),
                        static_cast<unsigned long long>(retainedCandidates),
                        std::chrono::duration<double, std::milli>(clusterSelectionEnd-selectionStart).count(),
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-clusterSelectionEnd).count());
                }

                const uint32_t environmentRecordIndex = placementIndex;
                bool hasInstances = false;
                if (parallelWorkerCount > 1U) {
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        hasInstances |= mImpl->WorkerOutputs[workerIndex].Counters.Visible != 0U;
                    }
                } else {
                    for (const auto& bin : mImpl->VisibleIndexBins) {
                        hasInstances |= !bin.empty();
                    }
                }
                if (!hasInstances) {
                    continue;
                }
                for (size_t binIndex = 0U; binIndex < mImpl->VisibleIndexBins.size(); ++binIndex) {
                    const uint32_t firstInstance = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
                    uint32_t binInstanceCount = 0U;
                    const auto appendBin = [&](const std::vector<uint32_t>& bin) {
                        mImpl->VisibleAnchorIndices.insert(mImpl->VisibleAnchorIndices.end(), bin.begin(), bin.end());
                        binInstanceCount += static_cast<uint32_t>(bin.size());
                    };
                    if (parallelWorkerCount > 1U) {
                        for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                            appendBin(mImpl->WorkerOutputs[workerIndex].Bins[binIndex]);
                        }
                    } else {
                        appendBin(mImpl->VisibleIndexBins[binIndex]);
                    }
                    if (binInstanceCount == 0U) {
                        continue;
                    }
                    const uint8_t bladeSegments = static_cast<uint8_t>(binIndex / 2U + 1U);
                    const uint8_t planeCount = static_cast<uint8_t>(binIndex % 2U + 1U);
                    auto batchPush = prepared.Push;
                    const bool tuft = binIndex == kGrassLodBinCount - 1U;
                    batchPush.Flags[1] = tuft ? 0U : bladeSegments;
                    batchPush.Flags[2] = planeCount;
                    batchPush.Flags[3] = environmentRecordIndex;
                    batches.push_back({ firstInstance, binInstanceCount,
                                        static_cast<uint8_t>(tuft ? 0U : bladeSegments), planeCount, batchPush,
                                        placementIndex });
                }
            }
            mImpl->SelectionCache.Store(std::move(selectionKey));
        }

        const uint32_t instanceCount = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
        if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr && frameId % 60 == 0) {
            size_t contacts = 0;
            float maxBend = 0;
            const size_t step = std::max<size_t>(1, mImpl->VisibleAnchorIndices.size() / 4096);
            for (size_t i = 0; i < mImpl->VisibleAnchorIndices.size(); i += step) {
                const auto& anchor = mImpl->StaticAnchors[mImpl->VisibleAnchorIndices[i]];
                for (const auto& actor : actors) {
                    const auto collision = ResolveGrassCollision(
                        {anchor.BaseHeight[0], anchor.BaseHeight[1], anchor.BaseHeight[2]},
                        anchor.BaseHeight[3], actor, settings);
                    if (collision.Weight > 0) {
                        ++contacts;
                        maxBend = std::max(maxBend, std::hypot(collision.Bend[0], collision.Bend[1]));
                    }
                }
            }
            std::fprintf(stderr, "[grass-interaction] frame=%llu actors=%zu sampled_contacts=%zu max_bend=%.4f\n",
                static_cast<unsigned long long>(frameId), actors.size(), contacts, maxBend);
        }
        if (instanceCount == 0U || batches.empty()) {
            return finish(GrassRenderStatus::Culled, false);
        }

        const auto uploadStart = std::chrono::steady_clock::now();
        GrassGpuInstanceCompactionResult compaction;
        bool gpuCompacted = false;
        if (mImpl->GpuCompactorAvailable) {
            gpuCompacted = mImpl->InstanceCompactor.Compact(
                {
                    commandBuffer,
                    frameSlot,
                    mImpl->StaticRevision,
                    std::span<const GrassWorldAnchor>(mImpl->StaticAnchors),
                    std::span<const uint32_t>(mImpl->VisibleAnchorIndices),
                    &mImpl->InteractionField,
                    actors,
                    &settings,
                },
                compaction);
            if (!gpuCompacted || compaction.InstanceBuffer == VK_NULL_HANDLE ||
                compaction.InstanceCount != instanceCount) {
                mImpl->GpuCompactorAvailable = false;
                gpuCompacted = false;
            }
        }

        uint64_t dynamicUploadedBytes = 0U;
        uint64_t staticUploadedBytes = 0U;
        if (gpuCompacted) {
            mImpl->PreparedInstanceBuffer = compaction.InstanceBuffer;
            dynamicUploadedBytes = compaction.DynamicUploadedBytes;
            staticUploadedBytes = compaction.StaticUploadedBytes;
        } else {
            auto& instanceBuffer = mImpl->InstanceBuffers[slotIndex];
            const VkDeviceSize instanceBytes = static_cast<VkDeviceSize>(instanceCount) * sizeof(GrassInstance);
            mImpl->EnsureBuffer(instanceBuffer, instanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            auto* instanceOutput = static_cast<GrassInstance*>(instanceBuffer.Mapped);
            for (size_t instanceIndex = 0U; instanceIndex < mImpl->VisibleAnchorIndices.size(); ++instanceIndex) {
                const uint32_t anchorIndex = mImpl->VisibleAnchorIndices[instanceIndex];
                if (anchorIndex >= mImpl->StaticAnchors.size()) {
                    throw std::runtime_error("interactive grass visible anchor is invalid");
                }
                const auto& anchor = mImpl->StaticAnchors[anchorIndex];
                const std::array<float, 3> base{ anchor.BaseHeight[0], anchor.BaseHeight[1], anchor.BaseHeight[2] };
                const auto interaction =
                    mImpl->InteractionField.Contains(base[0], base[2])
                        ? mImpl->InteractionField.Sample(base[0], base[2], base[1], settings.InteractionVerticalMargin)
                        : std::array<float, 2>{};
                std::array<float, 2> dynamicBend = interaction;
                for (const auto& actor : actors) {
                    const auto collision = ResolveGrassCollision(base, anchor.BaseHeight[3], actor, settings);
                    dynamicBend[0] += collision.Bend[0];
                    dynamicBend[1] += collision.Bend[1];
                }
                const float bendLength = std::hypot(dynamicBend[0], dynamicBend[1]);
                if (bendLength > settings.MaximumBend && bendLength > 0) {
                    for (auto& value : dynamicBend) value *= settings.MaximumBend / bendLength;
                }
                instanceOutput[instanceIndex] = {
                    anchor.BaseHeight,
                    {
                        dynamicBend[0],
                        dynamicBend[1],
                        anchor.HalfWidthPhase[0],
                        anchor.HalfWidthPhase[1],
                    },
                    UnpackGrassDirection(anchor.PackedWidthAxis),
                    UnpackGrassNormal(anchor.PackedWorldNormal),
                };
                instanceOutput[instanceIndex].WorldNormal[3] = GrassStableVisibilityValue(anchor.StableId);
            }
            mImpl->PreparedInstanceBuffer = instanceBuffer.Buffer;
            dynamicUploadedBytes = static_cast<uint64_t>(instanceBytes);
        }

        const VkDeviceSize environmentBytes = environmentRecords.size() * sizeof(GrassEnvironmentRecord);
        auto& environmentBuffer = mImpl->EnvironmentBuffers[slotIndex];
        mImpl->EnsureBuffer(environmentBuffer, environmentBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(environmentBuffer.Mapped, environmentRecords.data(), static_cast<size_t>(environmentBytes));
        const VkDescriptorBufferInfo environmentBufferInfo{ environmentBuffer.Buffer, 0U, environmentBytes };
        const VkWriteDescriptorSet environmentWrite{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mImpl->DescriptorSets[slotIndex], 0U,     0U, 1U,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &environmentBufferInfo,           nullptr
        };
        vkUpdateDescriptorSets(mImpl->Device, 1U, &environmentWrite, 0U, nullptr);
        telemetry.DrawCalls = static_cast<uint32_t>(batches.size());
        telemetry.UploadedBytes = static_cast<uint64_t>(dynamicUploadedBytes + staticUploadedBytes + environmentBytes);
        telemetry.DynamicUploadedBytes =
            dynamicUploadedBytes +
            static_cast<uint64_t>(environmentBytes);
        telemetry.StaticUploadedBytes =
            staticUploadedBytes;
        telemetry.GpuCompaction = gpuCompacted;
        telemetry.UploadMilliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
        mImpl->PreparedFrameSlot = static_cast<uint32_t>(slotIndex);
        mImpl->Prepared = true;
        return finish(GrassRenderStatus::Drawn, true);
    } catch (const std::exception& exception) {
        mImpl->SelectionCache.Reset();
        mImpl->Reason = exception.what();
        return finish(GrassRenderStatus::Error, false);
    }
}

bool InteractiveGrassPass::DrawPrepared(
    VkCommandBuffer commandBuffer, uint32_t width, uint32_t height,
    const EffectGeometryProviderPlan& providerPlan,
    const PicaAttachmentRequirements& attachments) {
    const size_t pipelineIndex = attachments.NativeColorOnly() ? 0U : 1U;
    const VkPipeline pipeline = mImpl->Pipelines[pipelineIndex];
    if (!providerPlan.Valid() ||
        providerPlan.Stage() != EffectStage::BeforeTransparent ||
        !providerPlan.ProducesExtensionGeometry() ||
        !providerPlan.ExportsExtensionGeometry() ||
        !mImpl->Prepared ||
        mImpl->PreparedInstanceBuffer == VK_NULL_HANDLE ||
        pipeline == VK_NULL_HANDLE ||
        commandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    const size_t slotIndex = mImpl->PreparedFrameSlot % mImpl->DescriptorSets.size();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mImpl->PipelineLayout, 0U, 1U,
                            &mImpl->DescriptorSets[slotIndex], 0U, nullptr);
    const VkDeviceSize offset = 0U;
    vkCmdBindVertexBuffers(commandBuffer, 0U, 1U, &mImpl->PreparedInstanceBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mImpl->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT16);
    const VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F };
    const VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);
    for (const auto& batch : mImpl->Batches) {
        vkCmdPushConstants(commandBuffer, mImpl->PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(batch.Push),
                           &batch.Push);
        const auto& range = batch.BladeSegments == 0U ? mImpl->IndexedTopology.Tuft :
            mImpl->IndexedTopology.Blades[batch.BladeSegments - 1U][batch.PlaneCount - 1U];
        vkCmdDrawIndexed(commandBuffer, range.Count, batch.InstanceCount, range.First, 0, batch.FirstInstance);
    }
    mImpl->Prepared = false;
    return true;
}

void InteractiveGrassPass::ResetTemporalState() {
    mImpl->SelectionCache.Reset();
    mImpl->InteractionField.Reset();
    mImpl->LastClockSequence = 0;
    mImpl->LastFallbackFrame = 0;
    mImpl->HasClockSequence = false;
    mImpl->HasFallbackFrame = false;
    mImpl->FallbackSeconds = 0.0;
}

void InteractiveGrassPass::Shutdown() {
    if (!mImpl) return;
    mImpl->WorkerPool.reset();
    mImpl->WorkerOutputs.clear();
    mImpl->InstanceCompactor.Shutdown();
    mImpl->GpuCompactorAvailable = false;
    if (mImpl->Device != VK_NULL_HANDLE) {
        mImpl->DestroyBuffer(mImpl->IndexBuffer);
        for (auto& buffer : mImpl->InstanceBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->EnvironmentBuffers)
            mImpl->DestroyBuffer(buffer);
        for (const auto pipeline : mImpl->Pipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(mImpl->Device, pipeline, nullptr);
            }
        }
        if (mImpl->PipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mImpl->Device, mImpl->PipelineLayout, nullptr);
        if (mImpl->DescriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(
                mImpl->Device, mImpl->DescriptorPool, nullptr);
        if (mImpl->DescriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(
                mImpl->Device, mImpl->DescriptorSetLayout, nullptr);
    }
    mImpl->Pipelines = {};
    mImpl->PipelineLayout = VK_NULL_HANDLE;
    mImpl->DescriptorPool = VK_NULL_HANDLE;
    mImpl->DescriptorSetLayout = VK_NULL_HANDLE;
    mImpl->DescriptorSets = {};
    mImpl->Batches.clear();
    mImpl->SelectionCache.Reset();
    mImpl->IndexedTopology = {};
    mImpl->EnvironmentRecords.clear();
    mImpl->PreparedPlacements.clear();
    mImpl->PlacementRequests.clear();
    for (auto& bin : mImpl->VisibleIndexBins) {
        bin.clear();
    }
    mImpl->VisibleAnchorIndices.clear();
    mImpl->StaticAnchors.clear();
    mImpl->ActivePlacements.clear();
    mImpl->StaticRevision = 0U;
    mImpl->PreparedInstanceBuffer = VK_NULL_HANDLE;
    mImpl->PreparedFrameSlot = 0U;
    mImpl->Prepared = false;
    mImpl->PlacementBuilder.Clear();
    ResetTemporalState();
    GrassRenderTelemetry::Instance().Reset();
    mImpl->Device = VK_NULL_HANDLE;
    mImpl->PhysicalDevice = VK_NULL_HANDLE;
}

uint32_t InteractiveGrassPass::LastBladeCount() const { return mImpl->LastBlades; }
uint32_t InteractiveGrassPass::LastMotionBladeCount() const {
    return mImpl->LastMotionBlades;
}
const std::string& InteractiveGrassPass::UnavailableReason() const { return mImpl->Reason; }

} // namespace Fast::Oot3d

#endif
