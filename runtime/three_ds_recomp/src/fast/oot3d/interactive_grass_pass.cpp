#include "fast/oot3d/grass_stage_profile.h"
#include "fast/backends/oot3d_vulkan_gpu_profiler.h"
#include "fast/renderer3ds/slow_step_trace.h"
#include "fast/oot3d/interactive_grass_pass.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/renderer3ds/pica_surface_lighting_pass.h"
#include "fast/renderer3ds/pica_surface_passthrough_scale.h"

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
#include "fast/oot3d/grass_shadow_grid_pass.h"
#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "fast/oot3d/grass_render_telemetry.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_cluster_selection.h"
#include "fast/oot3d/grass_cluster_order.h"
#include "fast/oot3d/outline_occlusion_pass.h"
#include "fast/oot3d/visual_clock.h"

#define BS_THREAD_POOL_ENABLE_PAUSE
#define BS_THREAD_POOL_ENABLE_PRIORITY
#include <BS_thread_pool.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <future>
#include "fast/oot3d/grass_instance_layout.h"
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Fast::Oot3d {
namespace {

struct GrassDrawBatch {
    uint32_t FirstInstance = 0;
    uint32_t InstanceCount = 0;
    uint8_t BladeSegments = 1U;
    uint8_t PlaneCount = 1U;
    GrassPushConstants Push{};
    uint32_t PlacementIndex = 0;
    bool Grouped = false;
    uint32_t GroupDrawCapacity = 0;
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

// Read once: the CRT environment lookup locks and scans per call.
bool GrassDiagnosticsEnabled() {
    static const bool enabled = std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr;
    return enabled;
}
inline constexpr size_t kGrassClustersForParallelSelection = 8192U;
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

// Shadow grid data for every anchor of a non-empty placement.
bool GrassPlacementHasShadowGrid(const GrassWorldPlacement& placement) noexcept {
    return placement.ShadowReferences.size() == placement.Anchors.size() && !placement.ShadowPoints.empty() &&
           !placement.ShadowSurfaces.empty();
}

uint64_t GrassWorldPlacementIdentity(
    const GrassSceneMesh& mesh,
    const GrassPlacementKey& key) noexcept {
    uint64_t hash = kGrassIdentityFnvOffset;
    HashGrassIdentityValue(hash, mesh.WorldInstanceId != 0U ? mesh.WorldInstanceId : mesh.InstanceId);
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
    result.RimDistance = {settings.Appearance.ToonRimFadeStart, settings.Appearance.ToonRimFadeEnd,
        settings.Appearance.ToonRimEnabled ? 1.0F : 0.0F, 0.0F};
    result.TuftLod = {settings.LodEndFraction,
        settings.LodReferenceDistance > 0.0F ? settings.LodReferenceDistance : settings.DrawDistance,
        static_cast<float>(settings.FarTuftBladeCount), settings.TuftTransitionFraction};
    result.DistanceLod = {settings.DrawDistance, settings.LodStartFraction, settings.FarDensity, settings.DensityFadeFraction};
    result.TuftStyle = {settings.FarTuftSpread, settings.FarTuftDensity, settings.DrawFadeFraction,
        settings.FarTuftsEnabled && !settings.MidrangeClustersEnabled ? 1.0F : 0.0F};
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

VkShaderModule Compile(Renderer::CachedPassShaderCompiler& shaders, VkDevice device, const char* source,
                       Renderer::SpirvStage kind, const char* name,
                       const char* defineName = nullptr,
                       const char* defineValue = nullptr) {
    Renderer::ShaderDefines defines;
    if (defineName != nullptr && defineValue != nullptr) {
        defines.emplace_back(defineName, defineValue);
    }
    const auto words = shaders.Resolve(source, kind, name, defines);
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

std::array<Renderer3ds::ShadowBounds, 2> ResolveShadowFocus(
    std::span<const GrassInteractor> actors, const Renderer3ds::PicaResolvedDrawStreamView& scene,
    uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress) {
    std::array<const GrassInteractor*, 2> selected{};
    for (const auto& actor : actors)
        if (actor.Type == GrassInteractor::Kind::PlayerLink &&
            (!selected[0] || actor.StableId < selected[0]->StableId))
            selected[0] = &actor;
    float nearest = std::numeric_limits<float>::infinity();
    for (const auto& actor : actors) {
        if (actor.Type != GrassInteractor::Kind::Epona) continue;
        float distance = 0;
        if (selected[0])
            for (size_t axis = 0; axis < 3; ++axis) {
                const float delta = actor.Position[axis] - selected[0]->Position[axis];
                distance += delta * delta;
            }
        if (!selected[1] || distance < nearest ||
            (distance == nearest && actor.StableId < selected[1]->StableId)) {
            selected[1] = &actor;
            nearest = distance;
        }
    }
    std::array<Renderer3ds::ShadowBounds, 2> result{};
    for (const auto& draw : scene.Draws) {
        const auto& caster = draw.ShadowCaster;
        if (!caster.WorldBounds.Available ||
            draw.RenderTarget.RenderTargetNamespace != renderTargetNamespace ||
            draw.RenderTarget.ColorPhysicalAddress != colorPhysicalAddress) continue;
        for (size_t slot = 0; slot < selected.size(); ++slot) {
            if (!selected[slot] || caster.OwnerIdentity != selected[slot]->StableId) continue;
            auto& bounds = result[slot];
            if (!bounds.Available) bounds = caster.WorldBounds;
            else
                for (size_t axis = 0; axis < 3; ++axis) {
                    bounds.Minimum[axis] = std::min(bounds.Minimum[axis], caster.WorldBounds.Minimum[axis]);
                    bounds.Maximum[axis] = std::max(bounds.Maximum[axis], caster.WorldBounds.Maximum[axis]);
                }
        }
    }
    return result;
}

} // namespace

struct InteractiveGrassPass::Impl {
    ::Fast::Renderer3ds::PicaSurfaceLightingPass SurfaceLighting;
    ::Fast::Renderer3ds::PicaSurfaceLightingPass ShadowSurfaceLighting;
    GrassShadowGridPass ShadowGrid;
    Renderer::CachedPassShaderCompiler* SurfaceShaders = nullptr;
    std::vector<::Fast::Renderer3ds::PicaSurfaceLightingRequest> LightingRequests;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkPipelineCache PipelineCache = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> DescriptorSets{};
    struct DescriptorInvocation {
        VkDescriptorPool Pool = VK_NULL_HANDLE;
        VkDescriptorSet Set = VK_NULL_HANDLE;
    };
    struct DescriptorFrame {
        uint64_t FrameId = 0;
        size_t Used = 0;
        std::vector<DescriptorInvocation> Additional;
    };
    std::array<DescriptorFrame, 2> DescriptorFrames;
    VkDescriptorSet PreparedDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    // Canonical, instrumented, instrumented with the GI albedo guide
    // (dynamic rendering only; grass leaves the albedo below it).
    // [0..2] explicit PICA depth, [3..5] the same with rasterized depth.
    std::array<VkPipeline, 6> Pipelines{};
    struct BufferSlot {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0;
        void* Mapped = nullptr;
    };
    std::array<BufferSlot, 2> InstanceBuffers{};
    std::array<BufferSlot, 2> EnvironmentBuffers{};
    std::array<BufferSlot, 2> GroupBuffers{};
    std::array<BufferSlot, 2> ShadowVisibleIndices{};
    std::vector<std::array<uint32_t, 2>> DrawGroups;
    BufferSlot IndexBuffer;
    GrassIndexedTopology IndexedTopology;
    GrassSelectionCache SelectionCache;
    uint64_t SelectionRevision = 1;
    std::vector<GrassDrawBatch> Batches;
    std::vector<GrassEnvironmentRecord> EnvironmentRecords;
    std::vector<GrassPreparedPlacement> PreparedPlacements;
    std::vector<GrassAsyncPlacementRequest> PlacementRequests;
    GrassVisibleIndexBins VisibleIndexBins;
    std::array<std::vector<std::array<uint32_t, 2>>, kGrassLodBinCount> SelectedClusterRanges;
    std::vector<uint32_t> VisibleAnchorIndices;
    std::vector<GrassWorldAnchor> StaticAnchors;
    std::vector<GrassShadowGridPoint> ShadowPoints;
    std::vector<GrassShadowGridReference> ShadowReferences;
    std::vector<GrassShadowSurface> ShadowSurfaces;
    std::vector<GrassClusterWork> ClusterWork;
    std::vector<uint32_t> SelectionRoots;
    std::vector<std::vector<GrassClusterWork>> SelectionOutputs;
    struct ActivePlacement {
        uint64_t Identity = 0U;
        uint64_t ContentVersion = 0U;
        size_t AnchorCount = 0U;
        bool operator==(const ActivePlacement&) const = default;
    };
    std::vector<ActivePlacement> ActivePlacements;
    bool ActiveShadowGrid = false;
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
    // What each placement drew with in its last frame.
    struct MotionHistoryRecord {
        uint64_t FrameId = 0;
        std::array<float, 16> PositionToClip{};
        std::array<float, 2> JitterNdc{};
        std::array<float, 3> Eye{};
        float WindTime = 0.0F;
        uint32_t NativeProjection = 0U;
    };
    std::unordered_map<uint64_t, MotionHistoryRecord> MotionHistory;
    VkBuffer PreparedInstanceBuffer = VK_NULL_HANDLE;
    bool Prepared = false;
    std::string Reason;
    VkDescriptorSet AcquireDescriptorSet(size_t slotIndex, uint64_t frameId) {
        auto& frame = DescriptorFrames[slotIndex];
        if (frame.FrameId != frameId) {
            frame.FrameId = frameId;
            frame.Used = 0;
        }
        if (frame.Used++ == 0) return DescriptorSets[slotIndex];
        const size_t index = frame.Used - 2;
        if (index == frame.Additional.size()) frame.Additional.emplace_back();
        auto& invocation = frame.Additional[index];
        if (invocation.Set == VK_NULL_HANDLE) {
            const std::array<VkDescriptorPoolSize, 2> sizes{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}};
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.maxSets = 1; pool.poolSizeCount = static_cast<uint32_t>(sizes.size()); pool.pPoolSizes = sizes.data();
            if (vkCreateDescriptorPool(Device, &pool, nullptr, &invocation.Pool) != VK_SUCCESS)
                throw std::runtime_error("cannot create Grass invocation descriptor pool");
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = invocation.Pool;
            allocation.descriptorSetCount = 1; allocation.pSetLayouts = &DescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &allocation, &invocation.Set) != VK_SUCCESS) {
                vkDestroyDescriptorPool(Device, invocation.Pool, nullptr);
                invocation.Pool = VK_NULL_HANDLE;
                throw std::runtime_error("cannot allocate Grass invocation descriptors");
            }
        }
        return invocation.Set;
    }


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

    GrassClusterSelectionStats SelectClusterWork(const GrassWorldPlacement& world,
        const std::array<float, 16>& clip, const std::array<float, 3>& eye,
        const GrassLodPolicy& policy, float bladeRadiusScale, bool frustumCulling) {
        SelectionRoots.clear();
        if (world.Clusters.size() < kGrassClustersForParallelSelection || world.VisibilityNodes.empty() || WorkerCount() < 2)
            return SelectGrassClusterWork(world, clip, eye, policy, bladeRadiusScale, frustumCulling, ClusterWork);
        GrassClusterSelectionStats stats;
        stats.TestedNodes = SplitGrassClusterSelection(world, clip, eye, policy, bladeRadiusScale,
            frustumCulling, static_cast<uint32_t>(WorkerCount()), SelectionRoots);
        ClusterWork.clear();
        SelectionOutputs.resize(SelectionRoots.size());
        std::vector<std::future<GrassClusterSelectionStats>> tasks;
        tasks.reserve(SelectionRoots.size());
        // Immutable inputs and per-job output only. Admission and draw ordering
        // happen after all jobs complete, never in completion order.
        try {
            for (size_t i = 0; i < SelectionRoots.size(); ++i) {
                tasks.push_back(WorkerPool->submit_task([&, i] {
                    return SelectGrassClusterSubtreeWork(world, clip, eye, policy, bladeRadiusScale,
                        frustumCulling, SelectionRoots[i], SelectionOutputs[i]);
                }));
            }
            for (auto& task : tasks) {
                const auto result = task.get();
                stats.TestedNodes += result.TestedNodes;
                stats.CandidateClusters += result.CandidateClusters;
                stats.CandidateAnchors += result.CandidateAnchors;
            }
        } catch (...) {
            for (auto& task : tasks) if (task.valid()) task.wait();
            throw;
        }
        ClusterWork.reserve(stats.CandidateClusters);
        for (const auto& output : SelectionOutputs)
            ClusterWork.insert(ClusterWork.end(), output.begin(), output.end());
        OrderGrassClusters(ClusterWork, [](const auto& work) { return work.ClusterIndex; });
        return stats;
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
                                      Renderer::CachedPassShaderCompiler& shaders,
                                      VkRenderPass canonicalRenderPass,
                                      VkRenderPass instrumentedRenderPass,
                                      VkSampleCountFlagBits sampleCount,
                                      bool dynamicRendering,
                                      VkFormat depthFormat,
                                      VkPipelineCache pipelineCache) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->PipelineCache = pipelineCache;
    VkPhysicalDeviceProperties grassDeviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &grassDeviceProperties);
    mImpl->PlacementBuilder.SetCandidateCapacity(std::min(8U * 1024U * 1024U,
        grassDeviceProperties.limits.maxStorageBufferRange / static_cast<uint32_t>(sizeof(GrassWorldAnchor))));
    mImpl->Device = device;
    mImpl->SurfaceShaders = &shaders;
    try {
        const auto vertexSource = BuildGrassVertexShader();
        const auto fragmentSource = BuildGrassFragmentShader();
        const auto rasterDepthSource = BuildGrassFragmentShader(true);
        VkShaderModule vertex = Compile(shaders, device, vertexSource.c_str(),
                                        Renderer::SpirvStage::Vertex, "interactive_grass.vert");
        std::array<VkShaderModule, 4> fragments{};
        try {
            fragments[0] = Compile(
                shaders, device, fragmentSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_canonical.frag",
                "GRASS_AUXILIARY_OUTPUTS", "0");
            fragments[1] = Compile(
                shaders, device, fragmentSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_instrumented.frag",
                "GRASS_AUXILIARY_OUTPUTS", "1");
            fragments[2] = Compile(
                shaders, device, rasterDepthSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_canonical_raster_depth.frag",
                "GRASS_AUXILIARY_OUTPUTS", "0");
            fragments[3] = Compile(
                shaders, device, rasterDepthSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_instrumented_raster_depth.frag",
                "GRASS_AUXILIARY_OUTPUTS", "1");
            const std::array<VkDescriptorSetLayoutBinding, 9> environmentBindings{{
                {0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {2U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {3U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {4U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {5U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {6U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {7U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {8U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr}}};
            VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(environmentBindings.size());
            descriptorLayoutInfo.pBindings = environmentBindings.data();
            if (vkCreateDescriptorSetLayout(
                    device, &descriptorLayoutInfo, nullptr,
                    &mImpl->DescriptorSetLayout) != VK_SUCCESS) {
                throw std::runtime_error(
                    "cannot create interactive grass descriptor layout");
            }
            const std::array<VkDescriptorPoolSize, 2> poolSizes{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}}};
            VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets =
                static_cast<uint32_t>(
                    mImpl->DescriptorSets.size());
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
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
                 offsetof(GrassInstance, WorldNormal)},
                {4, 0, VK_FORMAT_R32_UINT,
                 offsetof(GrassInstance, SurfaceColor)},
                {5, 0, VK_FORMAT_R32G32_UINT, offsetof(GrassInstance, SurfaceReference)}};
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
            // Geometry guide: the blades' face normal and kind (G/B); the
            // native normal and depth (R/A) stay the outlines' native surface.
            VkPipelineColorBlendAttachmentState geometryAttachment{};
            geometryAttachment.colorWriteMask = VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
            const std::array<VkPipelineColorBlendAttachmentState,
                             kPicaColorAttachmentCount>
                attachments{attachment, guideAttachment,
                            guideAttachment, motionAttachment,
                            guideAttachment, VkPipelineColorBlendAttachmentState{},
                            geometryAttachment};
            static_assert(kPicaColorAttachmentCount == kPicaBaseInstrumentedColorAttachmentCount + 1U);
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
                VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R16G16B16A16_UNORM
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
            const std::array<uint32_t, 3> attachmentCounts{
                1U, static_cast<uint32_t>(kPicaBaseInstrumentedColorAttachmentCount),
                static_cast<uint32_t>(kPicaColorAttachmentCount)};
            const std::array<VkRenderPass, 2> renderPasses{
                canonicalRenderPass, instrumentedRenderPass};
            for (size_t slot = 0U; slot < mImpl->Pipelines.size(); ++slot) {
                const size_t index = slot % 3U;
                const bool rasterDepth = slot >= 3U;
                if (index == 2U && !dynamicRendering) continue;
                stages[1].module = fragments[std::min<size_t>(index, 1U) + (rasterDepth ? 2U : 0U)];
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
                        device, pipelineCache, 1, &pipeline, nullptr,
                        &mImpl->Pipelines[slot]) != VK_SUCCESS) {
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
                physicalDevice, device, shaders, pipelineCache);
        mImpl->IndexedTopology = BuildGrassIndexedTopology();
        const auto indexBytes = mImpl->IndexedTopology.Indices.size() * sizeof(uint32_t);
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
                                   const InteractiveGrassSettings& settings, const ToonSurfaceParameters& toon,
                                   ::Fast::Renderer3ds::PicaResolvedDrawStreamView scene, uint64_t frameId,
                                   uint64_t renderTargetNamespace, uint32_t framebufferColorPhysicalAddress,
                                   uint32_t frameSlot, const EffectGeometryProviderPlan& providerPlan,
                                   const NriDirectionalShadowHistorySnapshot& shadowHistory,
                                   const std::array<float, 2>& jitterPixels, bool temporalJitter, bool temporalMotion,
                                   Fast::Oot3dVulkanGpuProfiler* profiler) {
    GrassStageTimer prepareTimer(GrassStage::Prepare);
    mImpl->Prepared = false;
    mImpl->PreparedInstanceBuffer = VK_NULL_HANDLE;
    mImpl->LastBlades = 0;
    mImpl->LastMotionBlades = 0;
    const auto cpuStart = std::chrono::steady_clock::now();
    ::Fast::Renderer3ds::SlowStepTrace grassSteps("grass_prepare");
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
        if (GrassStageProfileEnabled()) {
            auto& f = GrassStageCurrent();
            f.PlacementHit += placementStatsAfter.Hits - placementStatsBefore.Hits;
            f.PlacementMiss += placementStatsAfter.Misses - placementStatsBefore.Misses;
            f.WorkerMs += placementStatsAfter.BuildMilliseconds - placementStatsBefore.BuildMilliseconds;
            f.DiskHit += placementStatsAfter.DiskHits - placementStatsBefore.DiskHits;
            f.DiskMiss += placementStatsAfter.DiskMisses - placementStatsBefore.DiskMisses;
            f.DiskReject += placementStatsAfter.DiskRejects - placementStatsBefore.DiskRejects;
            f.DiskWrite += placementStatsAfter.DiskWrites - placementStatsBefore.DiskWrites;
        }
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
        const bool requestShadows = settings.Appearance.ReceiveLighting &&
                                    providerPlan.Requires(EffectResource::DirectionalShadowHistory) &&
                                    shadowHistory.Valid() && shadowHistory.State.ProducedFrameId <= frameId &&
                                    shadowHistory.State.Strength > 0.0F;
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
        {
            GrassStageTimer interactionTimer(GrassStage::Interaction);
            mImpl->InteractionField.UpdateActors(deltaSeconds, settings, actors);
        }
        auto& batches = mImpl->Batches;
        auto& environmentRecords = mImpl->EnvironmentRecords;
        auto& preparedPlacements = mImpl->PreparedPlacements;
        environmentRecords.clear();
        preparedPlacements.clear();
        mImpl->LightingRequests.clear();
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

        // The configured rules, then those inherited by their twin textures
        // (the same image stored again) that have no rule of their own.
        GrassStageTimer requestsTimer(GrassStage::Requests);
        const auto inherited = GrassTwinRules::Instance().AllInherited(settings.Rules);
        std::vector<GrassPlacementRule> withInherited;
        if (!inherited.empty()) {
            withInherited = settings.Rules;
            withInherited.insert(withInherited.end(), inherited.begin(), inherited.end());
        }
        for (const auto& rule : inherited.empty() ? settings.Rules : withInherited) {
            if (!GrassTextureSourceCache::Instance().Contains(rule.Target.Rgba8Hash))
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
                placementRequest.LazyMaskTexture = rule.Target.Rgba8Hash;
                placementRequest.Overlays = GrassSceneBridge::Instance().OverlaysFor(mesh);
                placementRequest.LazyColorTexture = mesh.ObservedTextureHash;
                placementRequest.Rule = rule;
                placementRequest.Generation =
                    settings.Generation;
                placementRequest.Budget =
                    settings.MaxInstancesPerRoom;
                placementRequest.ClusterSize =
                    settings.CullingClusterSize;
                placementRequest.NormalOffset =
                    rule.NormalOffset;
                placementRequest.MidrangeCellExtent = settings.MidrangeClustersEnabled ? settings.MidrangeClusterCellExtent : 0.0F;
                placementRequest.MidrangeAdaptive = settings.MidrangeClustersEnabled && settings.MidrangeAdaptiveEnabled;
                placementRequest.MidrangeAdaptiveCapacity = settings.MidrangeAdaptiveCapacity;
                placementRequest.HeightScale =
                    settings.Appearance.HeightScale;
                placementRequest.ShadowGrid = requestShadows;
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
                prepared.Environment.Toon = toon;
                prepared.Environment.Toon.Flags[1] =
                    settings.Appearance.ReceiveLighting && mesh.Shading.ActiveLightCount > 0U ? 1.0F : 0.0F;
                placementRequests.push_back(std::move(placementRequest));
                preparedPlacements.push_back(std::move(prepared));
                mImpl->LightingRequests.push_back({mesh.SubmissionId,
                    settings.Appearance.ReceiveLighting ? static_cast<uint32_t>(mesh.Vertices->size()) : 0U,
                    0, false, mesh.RenderTargetNamespace});
            }
        }

        grassSteps.Mark("before_placements");
        const auto placementStart = std::chrono::steady_clock::now();
        requestsTimer.Stop();
        GrassStageTimer placementTimer(GrassStage::Placement);
        auto placements = mImpl->PlacementBuilder.Resolve(placementRequests);
        placementTimer.Stop();
        GrassStageTimer lightingTimer(GrassStage::Lighting);
        if (profiler)
            profiler->BeginScope(frameSlot, GpuProfileScope::GrassLighting, commandBuffer);
        grassSteps.Mark("placements");
        // Shadows that begin to reach shown grass (the key light at dusk)
        // need a shadow grid its placement may still be building: until it
        // is ready the grass stays unshadowed instead of the frame waiting.
        bool receiveShadows = requestShadows;
        for (const auto& result : placements)
            if (result.Placement && !result.Placement->Anchors.empty() &&
                !GrassPlacementHasShadowGrid(*result.Placement))
                receiveShadows = false;
        auto& surfaceLighting = receiveShadows ? mImpl->ShadowSurfaceLighting : mImpl->SurfaceLighting;
        if (!surfaceLighting.Sampler() && !surfaceLighting.Initialize(
                mImpl->PhysicalDevice, mImpl->Device, *mImpl->SurfaceShaders, receiveShadows,
                mImpl->PipelineCache))
            return finish(GrassRenderStatus::PipelineUnavailable, false);
        if (!surfaceLighting.Prepare(commandBuffer, frameSlot, frameId, scene, mImpl->LightingRequests,
                shadowHistory.State.WorldLightDirectionTowardSource))
            return finish(GrassRenderStatus::PipelineUnavailable, false);
        grassSteps.Mark("surface_lighting");
        if (profiler)
            profiler->EndScope(frameSlot, GpuProfileScope::GrassLighting, commandBuffer);
        lightingTimer.Stop();
        size_t nativeLit = 0;
        for (size_t i = 0; i < preparedPlacements.size(); ++i) {
            const auto& source = mImpl->LightingRequests[i];
            if (source.Available && settings.Appearance.ReceiveLighting) {
                preparedPlacements[i].Environment.NativeLighting = {source.AtlasBase, receiveShadows ? 3U : 1U,
                    ::Fast::Renderer3ds::PicaSurfacePassthroughScale(source.ColorResponse.PackedScales),
                    source.ColorResponse.Available ? 1U : 0U};
                preparedPlacements[i].Environment.Toon.Flags[1] = 1;
                ++nativeLit;
            }
        }
        if (GrassDiagnosticsEnabled() && frameId % 30 == 0)
            std::fprintf(stderr, "Grass native surface lighting: frame=%llu available=%zu requested=%zu\n",
                static_cast<unsigned long long>(frameId), nativeLit, preparedPlacements.size());
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
            telemetry.PreparedDrawClusters += static_cast<uint32_t>(result.Placement->Midrange.Groups.size());
            telemetry.LargeDrawClusters += result.Placement->Midrange.LargeGroupCount;
            telemetry.MaximumClusterMembers = std::max(telemetry.MaximumClusterMembers, result.Placement->Midrange.MaximumMemberCount);
            telemetry.CapacityLimitedRanges += result.Placement->Midrange.CapacityLimitedRanges;
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
        if (activePlacements != mImpl->ActivePlacements || receiveShadows != mImpl->ActiveShadowGrid) {
            mImpl->ActivePlacements = std::move(activePlacements);
            mImpl->ActiveShadowGrid = receiveShadows;
            mImpl->StaticAnchors.clear();
            mImpl->ShadowPoints.clear();
            mImpl->ShadowReferences.clear();
            mImpl->ShadowSurfaces.clear();
            mImpl->StaticAnchors.reserve(totalStaticAnchors);
            for (const auto& prepared : preparedPlacements) {
                if (receiveShadows) {
                    if (!GrassPlacementHasShadowGrid(*prepared.World))
                        throw std::runtime_error("grass shadow grid does not cover the immutable placement");
                    const auto pointBase = static_cast<uint32_t>(mImpl->ShadowPoints.size());
                    if (prepared.World->ShadowPoints.size() > std::numeric_limits<uint32_t>::max() - pointBase)
                        throw std::runtime_error("grass shadow grid address overflow");
                    const auto surfaceBase = static_cast<uint32_t>(mImpl->ShadowSurfaces.size());
                    if (prepared.World->ShadowSurfaces.size() > std::numeric_limits<uint32_t>::max() - surfaceBase)
                        throw std::runtime_error("grass shadow surface address overflow");
                    for (auto point : prepared.World->ShadowPoints) {
                        point.Normal[3] = std::bit_cast<float>(std::bit_cast<uint32_t>(point.Normal[3]) + surfaceBase);
                        mImpl->ShadowPoints.push_back(point);
                    }
                    mImpl->ShadowSurfaces.insert(mImpl->ShadowSurfaces.end(),
                        prepared.World->ShadowSurfaces.begin(), prepared.World->ShadowSurfaces.end());
                    for (auto reference : prepared.World->ShadowReferences) {
                        reference.First += pointBase;
                        reference.Second += pointBase;
                        mImpl->ShadowReferences.push_back(reference);
                    }
                }
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
        selectionKey.ClusterFarBladeFraction = settings.MidrangeClustersEnabled ? settings.MidrangeFarBladeFraction : 0.0F;
        for (const auto& prepared : preparedPlacements) {
            selectionKey.Views.push_back({prepared.Push.PositionToClip, prepared.Eye});
            environmentRecords.push_back(prepared.Environment);
        }
        // Each placement's previous frame lets the blades write exact motion,
        // as the native draws do with their previous vertex uniforms.
        if (!temporalMotion) mImpl->MotionHistory.clear();
        else {
            const auto motionKey = [&](const GrassPreparedPlacement& prepared) {
                uint64_t key = kGrassIdentityFnvOffset;
                HashGrassIdentityValue(key, prepared.World->Identity);
                HashGrassIdentityValue(key, renderTargetNamespace);
                HashGrassIdentityValue(key, framebufferColorPhysicalAddress);
                return key;
            };
            for (size_t index = 0U; index < preparedPlacements.size(); ++index) {
                const auto found = mImpl->MotionHistory.find(motionKey(preparedPlacements[index]));
                if (found == mImpl->MotionHistory.end() || found->second.FrameId + 1U != frameId ||
                    found->second.NativeProjection != preparedPlacements[index].Push.Flags[0]) continue;
                auto& record = environmentRecords[index];
                record.PreviousPositionToClip = found->second.PositionToClip;
                record.PreviousCameraTime = {found->second.Eye[0], found->second.Eye[1], found->second.Eye[2],
                                             found->second.WindTime};
                record.MotionState = {found->second.JitterNdc[0], found->second.JitterNdc[1], 1.0F, 0.0F};
            }
            for (const auto& prepared : preparedPlacements) {
                mImpl->MotionHistory[motionKey(prepared)] = {
                    frameId, prepared.Push.PositionToClip, {prepared.Push.JitterNdc[0], prepared.Push.JitterNdc[1]},
                    prepared.Eye, prepared.Environment.WindDirectionTime[2], prepared.Push.Flags[0]};
            }
            std::erase_if(mImpl->MotionHistory, [frameId](const auto& item) { return item.second.FrameId + 1U < frameId; });
        }
        GrassStageTimer selectionTimer(GrassStage::Selection);
        const bool reuseSelection = mImpl->SelectionCache.Matches(selectionKey);
        if (GrassStageProfileEnabled()) {
            if (reuseSelection)
                ++GrassStageCurrent().SelectionHit;
            else
                ++GrassStageCurrent().SelectionMiss;
        }
        if (!reuseSelection)
            ++mImpl->SelectionRevision;
        if (reuseSelection) {
            mImpl->LastBlades = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
            telemetry.VisibleBlades = mImpl->LastBlades;
            for (auto& batch : batches) {
                batch.Push = preparedPlacements[batch.PlacementIndex].Push;
                batch.Push.Flags[1] = batch.BladeSegments | (batch.Grouped ? 0x80000000U : 0U);
                batch.Push.Flags[2] = batch.PlaneCount;
                batch.Push.Flags[3] = batch.PlacementIndex;
            }
        } else {
            mImpl->SelectionCache.Reset();
            batches.clear();
            mImpl->DrawGroups.clear();
            mImpl->VisibleAnchorIndices.clear();
            for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& prepared = preparedPlacements[placementIndex];
                if (remaining == 0U) {
                    break;
                }
                for (auto& bin : mImpl->VisibleIndexBins) {
                    bin.clear();
                }
                for (auto& ranges : mImpl->SelectedClusterRanges) ranges.clear();
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
                const bool clusterOwned = settings.MidrangeClustersEnabled && !prepared.World->Midrange.Groups.empty();
                const auto selection = clusterOwned ? GrassClusterSelectionStats{} : mImpl->SelectClusterWork(
                    *prepared.World, prepared.Push.PositionToClip, prepared.Eye, lodPolicy,
                    bladeRadiusScale, settings.FrustumCulling);
                const auto clusterSelectionEnd = std::chrono::steady_clock::now();
                if (!clusterOwned)
                    telemetry.CullingWorkers = std::max(telemetry.CullingWorkers,
                        static_cast<uint32_t>(mImpl->SelectionRoots.size()));
                telemetry.VisibilityNodesTested += selection.TestedNodes;
                telemetry.CandidateClusters += selection.CandidateClusters;
                const auto retainedCandidates = selection.CandidateAnchors;
                size_t parallelWorkerCount = 0U;
                if (!clusterOwned && retainedCandidates >= kGrassAnchorsPerWorker * 2U &&
                    remaining >= kGrassAnchorsPerWorker && clusterWork.size() > 1U) {
                    parallelWorkerCount =
                        std::min({ mImpl->WorkerCount(), clusterWork.size(),
                                   static_cast<size_t>(retainedCandidates / kGrassAnchorsPerWorker) });
                }
                if (clusterOwned) {
                    uint64_t evaluated = 0;
                    const auto visible = SelectGrassDrawableClusters(*prepared.World, prepared.Push.PositionToClip,
                        prepared.Eye, lodPolicy, settings.MidrangeFarBladeFraction, bladeRadiusScale, settings.FrustumCulling, remaining, evaluated,
                        [&](auto selected, const GrassLodDecision& lod) {
                            const auto binIndex = GrassLodBinIndex(lod.BladeSegments, lod.PlaneCount);
                            auto& bin = mImpl->VisibleIndexBins[binIndex];
                            if constexpr (std::is_same_v<decltype(selected), uint32_t>) {
                                bin.push_back(prepared.StaticBaseIndex + selected);
                            } else {
                                const auto first = bin.size();
                                mImpl->SelectedClusterRanges[binIndex].push_back(
                                    {static_cast<uint32_t>(first), static_cast<uint32_t>(selected.size())});
                                bin.resize(first + selected.size());
                                std::transform(selected.begin(), selected.end(), bin.begin()+first,
                                    [&](uint32_t index) { return prepared.StaticBaseIndex + index; });
                            }
                        }, &telemetry.VisibilityNodesTested);
                    telemetry.CandidateClusters += evaluated;
                    remaining -= visible;
                    mImpl->LastBlades += visible;
                    telemetry.VisibleBlades += visible;
                } else if (parallelWorkerCount > 1U) {
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
                if (GrassDiagnosticsEnabled() && frameId % 60 == 0) {
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
                    const bool grouped = settings.MidrangeClustersEnabled && !tuft && bladeSegments <= 2 &&
                        !prepared.World->Midrange.Groups.empty();
                    if (grouped) {
                        const auto& map = prepared.World->Midrange.GroupForRoot;
                        const uint32_t firstGroup = static_cast<uint32_t>(mImpl->DrawGroups.size());
                        const auto groups = clusterOwned ? RebaseGrassMidrangeDraws(
                            mImpl->SelectedClusterRanges[binIndex], binInstanceCount, firstInstance) : PackGrassMidrangeDraws(
                            std::span<uint32_t>(mImpl->VisibleAnchorIndices).subspan(firstInstance, binInstanceCount),
                            map, prepared.StaticBaseIndex, firstInstance, clusterOwned);
                        mImpl->DrawGroups.insert(mImpl->DrawGroups.end(), groups.begin(), groups.end());
                        batchPush.Flags[1] |= 0x80000000U;
                        for (uint32_t first = firstGroup; first < mImpl->DrawGroups.size();) {
                            uint32_t end = first+1;
                            const auto count = GrassClusterDrawCapacity(mImpl->DrawGroups[first][1]);
                            while (end < mImpl->DrawGroups.size() && GrassClusterDrawCapacity(mImpl->DrawGroups[end][1]) == count) ++end;
                            batches.push_back({first, end-first, bladeSegments, planeCount, batchPush, placementIndex,
                                              true, count});
                            first = end;
                        }
                    } else batches.push_back({ firstInstance, binInstanceCount,
                                        static_cast<uint8_t>(tuft ? 0U : bladeSegments), planeCount, batchPush,
                                        placementIndex });
                }
            }
            mImpl->SelectionCache.Store(std::move(selectionKey));
        }

        const uint32_t instanceCount = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
        if (GrassDiagnosticsEnabled() && frameId % 60 == 0) {
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

        selectionTimer.Stop();
        const auto uploadStart = std::chrono::steady_clock::now();
        GrassStageTimer compactionTimer(GrassStage::Compaction);
        if (profiler)
            profiler->BeginScope(frameSlot, GpuProfileScope::GrassCompaction, commandBuffer);
        GrassGpuInstanceCompactionResult compaction;
        bool gpuCompacted = false;
        if (mImpl->GpuCompactorAvailable) {
            gpuCompacted = mImpl->InstanceCompactor.Compact(
                {
                    commandBuffer,
                    frameSlot,
                    frameId,
                    mImpl->StaticRevision,
                    std::span<const GrassWorldAnchor>(mImpl->StaticAnchors),
                    std::span<const uint32_t>(mImpl->VisibleAnchorIndices),
                    &mImpl->InteractionField,
                    actors,
                    &settings,
                    mImpl->SelectionRevision,
                    !temporalMotion,
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
            mImpl->EnsureBuffer(instanceBuffer, instanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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
                    anchor.SurfaceColor,
                    anchor.SurfaceReference,
                };
                instanceOutput[instanceIndex].WorldNormal[3] = GrassStableVisibilityValue(anchor.StableId);
            }
            mImpl->PreparedInstanceBuffer = instanceBuffer.Buffer;
            dynamicUploadedBytes = static_cast<uint64_t>(instanceBytes);
        }

        compactionTimer.Stop();
        if (profiler)
            profiler->EndScope(frameSlot, GpuProfileScope::GrassCompaction, commandBuffer);
        if (GrassStageProfileEnabled()) {
            GrassStageCurrent().SelectionUploadReuse += compaction.SelectionUploadReused;
            GrassStageCurrent().CompactionReuse += compaction.Reused;
        }
        // Disabled receivers reuse an already valid descriptor without a shadow
        // allocation, upload or dispatch. The shader never reads these bindings.
        const VkDescriptorBufferInfo unusedShadow{mImpl->PreparedInstanceBuffer, 0, sizeof(GrassInstance)};
        std::array<VkDescriptorBufferInfo, 4> shadowBuffers{unusedShadow, unusedShadow, unusedShadow, unusedShadow};
        if (receiveShadows) {
            GrassStageTimer shadowTimer(GrassStage::Shadows);
            if (profiler)
                profiler->BeginScope(frameSlot, GpuProfileScope::GrassShadows, commandBuffer);
            if (!mImpl->ShadowGrid.Available())
                mImpl->ShadowGrid.Initialize(mImpl->PhysicalDevice, mImpl->Device, *mImpl->SurfaceShaders,
                                             mImpl->PipelineCache);
            const auto focus = shadowHistory.State.ProducedFrameId == frameId && scene.FrameId == frameId
                ? ResolveShadowFocus(actors, scene, renderTargetNamespace, framebufferColorPhysicalAddress)
                : std::array<Renderer3ds::ShadowBounds, 2>{};
            const auto grid = mImpl->ShadowGrid.Execute(commandBuffer, frameSlot, frameId, mImpl->StaticRevision,
                mImpl->ShadowPoints, mImpl->ShadowReferences, mImpl->ShadowSurfaces, focus, providerPlan, shadowHistory);
            if (profiler)
                profiler->EndScope(frameSlot, GpuProfileScope::GrassShadows, commandBuffer);
            shadowBuffers[0] = grid.Visibility;
            shadowBuffers[1] = grid.References;
            shadowBuffers[3] = grid.Patches;
            const VkDeviceSize visibleBytes = mImpl->VisibleAnchorIndices.size() * sizeof(uint32_t);
            staticUploadedBytes += grid.StaticUploadedBytes;
            dynamicUploadedBytes += grid.DynamicUploadedBytes;
            if (gpuCompacted) {
                shadowBuffers[2] = {compaction.VisibleIndicesBuffer, 0, visibleBytes};
            } else {
                auto& visible = mImpl->ShadowVisibleIndices[slotIndex];
                mImpl->EnsureBuffer(visible, visibleBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                std::memcpy(visible.Mapped, mImpl->VisibleAnchorIndices.data(), static_cast<size_t>(visibleBytes));
                shadowBuffers[2] = {visible.Buffer, 0, visibleBytes};
                dynamicUploadedBytes += visibleBytes;
            }
            if (GrassDiagnosticsEnabled() && frameId % 30 == 0)
                std::fprintf(stderr, "[grass-shadow-grid] frame=%llu history=%llu points=%u roots=%u placements=%zu "
                    "refined=%u patches=%u cell=%.1f link=%d epona=%d\n",
                    static_cast<unsigned long long>(frameId),
                    static_cast<unsigned long long>(shadowHistory.State.ProducedFrameId),
                    grid.PointCount, instanceCount, preparedPlacements.size(),
                    grid.RefinementPointCount, grid.RefinementPatchCount, grid.RefinementCellSize,
                    focus[0].Available, focus[1].Available);
        }
        // Updating a set already recorded at another native anchor would make
        // the earlier draw read the later invocation's history/grid buffers.
        GrassStageTimer uploadTimer(GrassStage::Upload);
        const auto descriptorSet = mImpl->AcquireDescriptorSet(slotIndex, frameId);
        std::array<VkWriteDescriptorSet, 4> shadowWrites{};
        for (uint32_t index = 0; index < shadowWrites.size(); ++index) {
            auto& write = shadowWrites[index];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet; write.dstBinding = 4U + index;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.descriptorCount = 1;
            write.pBufferInfo = &shadowBuffers[index];
        }
        vkUpdateDescriptorSets(mImpl->Device, static_cast<uint32_t>(shadowWrites.size()), shadowWrites.data(), 0, nullptr);

        if (gpuCompacted)
            for (auto& record : environmentRecords)
                if (record.MotionState[2] > 0.5F) record.MotionState[3] = 1.0F;
        const VkDeviceSize environmentBytes = environmentRecords.size() * sizeof(GrassEnvironmentRecord);
        auto& environmentBuffer = mImpl->EnvironmentBuffers[slotIndex];
        mImpl->EnsureBuffer(environmentBuffer, environmentBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(environmentBuffer.Mapped, environmentRecords.data(), static_cast<size_t>(environmentBytes));
        const VkDescriptorBufferInfo environmentBufferInfo{ environmentBuffer.Buffer, 0U, environmentBytes };
        const VkWriteDescriptorSet environmentWrite{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0U, 0U, 1U,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &environmentBufferInfo,           nullptr
        };
        vkUpdateDescriptorSets(mImpl->Device, 1U, &environmentWrite, 0U, nullptr);
        VkDescriptorImageInfo nativeLightingImage{surfaceLighting.Sampler(), surfaceLighting.View(frameSlot),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet lightingWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        lightingWrite.dstSet = descriptorSet; lightingWrite.dstBinding = 1;
        lightingWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lightingWrite.descriptorCount = 1;
        lightingWrite.pImageInfo = &nativeLightingImage;
        vkUpdateDescriptorSets(mImpl->Device, 1, &lightingWrite, 0, nullptr);
        auto& groupBuffer = mImpl->GroupBuffers[slotIndex];
        const VkDeviceSize groupBytes = std::max<size_t>(1, mImpl->DrawGroups.size()) * sizeof(std::array<uint32_t,2>);
        mImpl->EnsureBuffer(groupBuffer, groupBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (!mImpl->DrawGroups.empty())
            std::memcpy(groupBuffer.Mapped, mImpl->DrawGroups.data(), mImpl->DrawGroups.size()*sizeof(std::array<uint32_t,2>));
        const std::array<VkDescriptorBufferInfo,2> clusterBuffers{{
            {mImpl->PreparedInstanceBuffer, 0, VkDeviceSize(instanceCount)*sizeof(GrassInstance)},
            {groupBuffer.Buffer, 0, groupBytes}}};
        for (uint32_t index = 0; index < 2; ++index) {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptorSet; write.dstBinding = index+2;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.descriptorCount = 1;
            write.pBufferInfo = &clusterBuffers[index];
            vkUpdateDescriptorSets(mImpl->Device, 1, &write, 0, nullptr);
        }
        // The CPU fallback keeps no bend history: its blades reuse the current
        // bend and never read this binding.
        const VkDescriptorBufferInfo previousBends = gpuCompacted
            ? VkDescriptorBufferInfo{compaction.PreviousBendBuffer, 0, VkDeviceSize(instanceCount) * 2U * sizeof(float)}
            : clusterBuffers[0];
        VkWriteDescriptorSet previousBendsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        previousBendsWrite.dstSet = descriptorSet; previousBendsWrite.dstBinding = 8;
        previousBendsWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; previousBendsWrite.descriptorCount = 1;
        previousBendsWrite.pBufferInfo = &previousBends;
        vkUpdateDescriptorSets(mImpl->Device, 1, &previousBendsWrite, 0, nullptr);
        for (const auto& batch : batches) {
            if (environmentRecords[batch.Push.Flags[3]].MotionState[2] <= 0.5F) continue;
            if (!batch.Grouped) mImpl->LastMotionBlades += batch.InstanceCount;
            else for (uint32_t group = 0; group < batch.InstanceCount; ++group)
                mImpl->LastMotionBlades += mImpl->DrawGroups[batch.FirstInstance + group][1];
        }
        telemetry.DrawCalls = static_cast<uint32_t>(batches.size());
        telemetry.ClusterDrawInstances = static_cast<uint32_t>(mImpl->DrawGroups.size());
        for (const auto& group : mImpl->DrawGroups) telemetry.ClusterRepresentedBlades += group[1];
        telemetry.UploadedBytes = static_cast<uint64_t>(dynamicUploadedBytes + staticUploadedBytes + environmentBytes + groupBytes);
        telemetry.DynamicUploadedBytes =
            dynamicUploadedBytes +
            static_cast<uint64_t>(environmentBytes + groupBytes);
        telemetry.StaticUploadedBytes =
            staticUploadedBytes;
        telemetry.GpuCompaction = gpuCompacted;
        telemetry.UploadMilliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
        mImpl->PreparedDescriptorSet = descriptorSet;
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
    const size_t pipelineIndex = attachments.NativeColorOnly() ? 0U
        : attachments.Requires(PicaAuxiliaryOutput::DiffuseAlbedoGuide) ? 2U : 1U;
    const VkPipeline pipeline = mImpl->Pipelines[pipelineIndex];
    const VkPipeline rasterDepthPipeline = mImpl->Pipelines[pipelineIndex + 3U];
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
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mImpl->PipelineLayout, 0U, 1U,
                            &mImpl->PreparedDescriptorSet, 0U, nullptr);
    const VkDeviceSize offset = 0U;
    vkCmdBindVertexBuffers(commandBuffer, 0U, 1U, &mImpl->PreparedInstanceBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mImpl->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
    const VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F };
    const VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);
    VkPipeline bound = pipeline;
    for (const auto& batch : mImpl->Batches) {
        const VkPipeline wanted = rasterDepthPipeline != VK_NULL_HANDLE &&
                GrassDepthIsRasterDepth(batch.Push)
            ? rasterDepthPipeline : pipeline;
        if (wanted != bound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            bound = wanted;
        }
        vkCmdPushConstants(commandBuffer, mImpl->PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(batch.Push),
                           &batch.Push);
        const auto& range = batch.Grouped ? mImpl->IndexedTopology.Groups[batch.BladeSegments-1][batch.PlaneCount-1] : batch.BladeSegments == 0U ? mImpl->IndexedTopology.Tuft :
            mImpl->IndexedTopology.Blades[batch.BladeSegments - 1U][batch.PlaneCount - 1U];
        const uint32_t count = batch.Grouped ? range.Count / kGrassMidrangeClusterCapacity * batch.GroupDrawCapacity : range.Count;
        vkCmdDrawIndexed(commandBuffer, count, batch.InstanceCount, range.First, 0, batch.FirstInstance);
    }
    mImpl->Prepared = false;
    return true;
}

void InteractiveGrassPass::SetCacheDirectory(std::filesystem::path directory) {
    static const bool disabled = [] {
        const auto* p = std::getenv("TRIAEVUM_GRASS_DISK_CACHE");
        return p && std::string_view(p) == "0";
    }();
    mImpl->PlacementBuilder.SetCacheDirectory(disabled ? std::filesystem::path{} : std::move(directory));
}
void InteractiveGrassPass::ResetTemporalState() {
    mImpl->SelectionCache.Reset();
    mImpl->InteractionField.Reset();
    mImpl->MotionHistory.clear();
    mImpl->InstanceCompactor.ResetHistory();
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
    mImpl->SurfaceLighting.Shutdown();
    mImpl->ShadowSurfaceLighting.Shutdown();
    mImpl->ShadowGrid.Shutdown();
    mImpl->GpuCompactorAvailable = false;
    if (mImpl->Device != VK_NULL_HANDLE) {
        mImpl->DestroyBuffer(mImpl->IndexBuffer);
        for (auto& buffer : mImpl->InstanceBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->EnvironmentBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->GroupBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->ShadowVisibleIndices)
            mImpl->DestroyBuffer(buffer);
        for (const auto pipeline : mImpl->Pipelines) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(mImpl->Device, pipeline, nullptr);
            }
        }
        if (mImpl->PipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mImpl->Device, mImpl->PipelineLayout, nullptr);
        for (auto& frame : mImpl->DescriptorFrames) {
            for (const auto& invocation : frame.Additional)
                if (invocation.Pool) vkDestroyDescriptorPool(mImpl->Device, invocation.Pool, nullptr);
            frame = {};
        }
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
    mImpl->PreparedDescriptorSet = VK_NULL_HANDLE;
    mImpl->Batches.clear();
    mImpl->SelectionCache.Reset();
    mImpl->IndexedTopology = {};
    mImpl->EnvironmentRecords.clear();
    mImpl->PreparedPlacements.clear();
    mImpl->PlacementRequests.clear();
    for (auto& bin : mImpl->VisibleIndexBins) {
        bin.clear();
    }
    for (auto& ranges : mImpl->SelectedClusterRanges) ranges.clear();
    mImpl->VisibleAnchorIndices.clear();
    mImpl->StaticAnchors.clear();
    mImpl->ShadowPoints.clear();
    mImpl->ShadowReferences.clear();
    mImpl->ShadowSurfaces.clear();
    mImpl->ActivePlacements.clear();
    mImpl->ActiveShadowGrid = false;
    mImpl->ClusterWork.clear();
    mImpl->SelectionRoots.clear();
    mImpl->SelectionOutputs.clear();
    mImpl->StaticRevision = 0U;
    mImpl->PreparedInstanceBuffer = VK_NULL_HANDLE;
    mImpl->Prepared = false;
    mImpl->PlacementBuilder.Clear();
    ResetTemporalState();
    GrassRenderTelemetry::Instance().Reset();
    mImpl->Device = VK_NULL_HANDLE;
    mImpl->PhysicalDevice = VK_NULL_HANDLE;
    mImpl->PipelineCache = VK_NULL_HANDLE;
}

uint32_t InteractiveGrassPass::LastBladeCount() const { return mImpl->LastBlades; }
uint32_t InteractiveGrassPass::LastMotionBladeCount() const {
    return mImpl->LastMotionBlades;
}
const std::string& InteractiveGrassPass::UnavailableReason() const { return mImpl->Reason; }

} // namespace Fast::Oot3d

#endif
