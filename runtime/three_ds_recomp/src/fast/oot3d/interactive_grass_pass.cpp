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
#include "fast/oot3d/grass_card_atlas.h"
#include "fast/oot3d/grass_card_lod.h"
#include "fast/oot3d/grass_card_renderer.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_pipeline_factory.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_selection_cache.h"
#include "fast/oot3d/grass_gpu_memory.h"
#include "fast/oot3d/grass_placement_audit.h"
#include "fast/oot3d/grass_residency.h"
#include "fast/oot3d/grass_selection_budget.h"
#include "fast/oot3d/grass_collision_resolver.h"
#include "fast/oot3d/grass_anchor_codec.h"
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
#include <iterator>
#include <atomic>
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
    // GPU selection: instance count and first instance come from this
    // VkDrawIndexedIndirectCommand in the selection control buffer.
    bool Indirect = false;
    VkDeviceSize IndirectOffset = 0;
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
inline constexpr size_t kGrassClusterPrefetchDistance = 16U;
inline constexpr size_t kGrassAnchorPrefetchDistance = 8U;

inline void GrassPrefetch(const void* address) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    __builtin_prefetch(address, 0, 3);
#else
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#endif
}
// A few sources times the views that render grass in one frame.
inline constexpr size_t kGrassCandidateCacheEntries = 16U;

// Diagnostic A/B: TRIAEVUM_GRASS_CANDIDATE_CACHE=0 selects every view with
// the full traversal again.
bool GrassCandidateCacheEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_CANDIDATE_CACHE");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// GPU fine selection (default when the device supports the compactor and the
// selector); TRIAEVUM_GRASS_GPU_SELECTION=0 keeps the CPU selection.
bool GrassGpuSelectionEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_GPU_SELECTION");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// Diagnostic: TRIAEVUM_GRASS_GPU_SELECTION_VERIFY=1 also runs the CPU
// selection and compares it with the GPU's when the frame slot comes back.
bool GrassGpuSelectionVerifyEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_GPU_SELECTION_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

// TRIAEVUM_GRASS_RESIDENCY=0 uploads the whole scene's anchors (A/B only).
bool GrassResidencyEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_RESIDENCY");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// Diagnostic: TRIAEVUM_GRASS_SELECTION_VERIFY=1 also runs the full traversal
// for every view and logs any difference in the cluster work.
bool GrassSelectionVerifyEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_SELECTION_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

// Diagnostic: TRIAEVUM_GRASS_SELECTION_HASH=1 adds a per-frame hash of the
// selected roots and batches to TRIAEVUM_GRASS_PROFILE (about 0.3 ms).
bool GrassSelectionHashEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_SELECTION_HASH");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

// Eye radius (world units), clip-row delta and translation scale of a
// candidate envelope. TRIAEVUM_GRASS_ENVELOPE="radius,delta,scale" tunes them.
std::array<float, 3> GrassCandidateEnvelopeParameters() {
    static const std::array<float, 3> parameters = [] {
        std::array<float, 3> result{96.0F, 0.04F, 2.0F};
        if (const char* value = std::getenv("TRIAEVUM_GRASS_ENVELOPE"))
            std::sscanf(value, "%f,%f,%f", &result[0], &result[1], &result[2]);
        return result;
    }();
    return parameters;
}
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
    // Each root's budget thinning bucket (grass_selection_budget.h), parallel
    // to Bins; empty for cluster-owned selections, which are never thinned.
    std::array<std::vector<uint16_t>, kGrassLodBinCount> Buckets;
    GrassBudgetHistogram Histogram{};
    GrassClusterCounters Counters;

    void Clear() {
        for (auto& bin : Bins) {
            bin.clear();
        }
        for (auto& buckets : Buckets) buckets.clear();
        Histogram.fill(0U);
        Counters = {};
    }
};

// One placement's evaluated roots (every visible root, before the budget):
// one output per evaluation job, in job order, so the assembly keeps the
// anchor traversal order.
struct GrassPlacementSelection {
    std::vector<GrassWorkerOutput> Outputs;
    size_t OutputCount = 0U;
    std::array<std::vector<std::array<uint32_t, 2>>, kGrassLodBinCount> ClusterRanges;
    bool ClusterOwned = false;
    uint32_t Visible = 0U;

    void Clear() {
        for (auto& output : Outputs) output.Clear();
        OutputCount = 0U;
        for (auto& ranges : ClusterRanges) ranges.clear();
        ClusterOwned = false;
        Visible = 0U;
    }
    GrassWorkerOutput& Output(size_t index) {
        if (Outputs.size() <= index) Outputs.resize(index + 1U);
        OutputCount = std::max(OutputCount, index + 1U);
        return Outputs[index];
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

// The bins the policy can produce (far, middle and near segments with one or
// two planes, distant tufts), in bin order, with their index ranges.
std::vector<GrassGpuSelectionSlot> BuildGrassGpuSelectionSlots(const GrassLodPolicy& policy,
                                                               const GrassIndexedTopology& topology) {
    std::vector<uint32_t> bins;
    const uint8_t middle = std::max<uint8_t>(policy.FarBladeSegments, (policy.NearBladeSegments + 1U) / 2U);
    for (const uint8_t segments : {policy.FarBladeSegments, middle, policy.NearBladeSegments})
        for (const uint8_t planes : {uint8_t(1U), uint8_t(2U)})
            bins.push_back(static_cast<uint32_t>(GrassLodBinIndex(segments, planes)));
    if (policy.FarTuftsEnabled) bins.push_back(static_cast<uint32_t>(kGrassLodBinCount - 1U));
    std::sort(bins.begin(), bins.end());
    bins.erase(std::unique(bins.begin(), bins.end()), bins.end());
    std::vector<GrassGpuSelectionSlot> slots;
    for (const auto bin : bins) {
        const auto& range = bin == kGrassLodBinCount - 1U ? topology.Tuft : topology.Blades[bin / 2U][bin % 2U];
        slots.push_back({bin, range.Count, range.First});
    }
    return slots;
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
    // [0..2] explicit PICA depth, [3..5] the same with rasterized depth,
    // [6..8] rasterized depth without discard (the whole pass).
    GrassBladePipelineSet Pipelines{};
    // Bit s: blades with s segments may fade (LOD stipple), so their batches
    // need the fading pass after the whole one.
    uint32_t FadingSegments = ~0U;
    struct BufferSlot {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Capacity = 0;
        void* Mapped = nullptr;
    };
    // CPU compaction: the selected anchors in root order, their bends and the
    // identity root -> anchor map the blades read through.
    std::array<BufferSlot, 2> CpuRootAnchors{};
    std::array<BufferSlot, 2> CpuRootBends{};
    std::array<BufferSlot, 2> CpuRootIndices{};
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
    // Per active placement, the roots of the current CPU selection.
    std::vector<GrassPlacementSelection> PlacementSelections;
    std::vector<uint32_t> VisibleAnchorIndices;
    // The active sources' immutable anchors, addressed by global static index
    // (source base + local index) without a concatenated copy.
    std::vector<std::shared_ptr<const GrassWorldPlacement>> StaticSources;
    std::vector<uint32_t> StaticBases;
    std::vector<std::span<const GrassWorldAnchor>> StaticSegments;
    size_t StaticAnchorCount = 0U;
    const GrassWorldAnchor& StaticAnchor(uint32_t index) const {
        const auto source = static_cast<size_t>(std::upper_bound(StaticBases.begin(), StaticBases.end(), index) -
                                                StaticBases.begin()) - 1U;
        return StaticSegments[source][index - StaticBases[source]];
    }
    std::vector<GrassShadowGridPoint> ShadowPoints;
    std::vector<GrassShadowGridReference> ShadowReferences;
    std::vector<GrassShadowSurface> ShadowSurfaces;
    std::vector<GrassClusterWork> ClusterWork;
    std::vector<uint32_t> SelectionRoots;
    std::vector<std::vector<GrassClusterWork>> SelectionOutputs;
    // Conservative cluster candidates per source and camera envelope.
    struct CandidateCacheEntry {
        uint64_t Identity = 0U;
        uint64_t ContentVersion = 0U;
        GrassLodPolicy Policy{};
        float BladeRadiusScale = 0.0F;
        bool FrustumCulling = false;
        GrassCandidateEnvelope Envelope{};
        std::vector<GrassClusterCandidate> Candidates;
        uint64_t LastUsed = 0U;
    };
    std::vector<CandidateCacheEntry> CandidateCache;
    uint64_t CandidateClock = 0U;
    std::vector<std::vector<GrassClusterCandidate>> CandidateOutputs;
    std::vector<std::vector<GrassClusterWork>> RefineOutputs;
    std::vector<GrassClusterWork> VerifyClusterWork;
    uint64_t VerifyChecks = 0U, VerifyMismatches = 0U;
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
    GrassStaticPlacementCache PlacementBuilder;
    GrassGpuInstanceCompactor InstanceCompactor;
    bool GpuCompactorAvailable = false;
    GrassGpuSelector GpuSelector;
    bool GpuSelectorAvailable = false;
    // The distant cards (grass_card_lod.h): one card set per active source,
    // their records concatenated with the shadow grid points rebased like
    // the blades' references, rebuilt with the static placements.
    GrassCardRenderer Cards;
    bool CardsAvailable = false;
    // The cards' programs and pipelines are created the first time a frame
    // enables them: disabled, the cards hold no device resources.
    bool CardsInitialized = false;
    GrassPipelineTargets CardTargets{};
    std::vector<std::shared_ptr<const GrassCardSet>> CardSets;
    std::vector<uint32_t> ShadowPointBases;
    GrassCardStream CardStream;
    std::vector<std::array<GrassCardLevelRanges, kGrassCardLevelCount>> CardRanges;
    std::vector<GrassCardRange> CardRuns;
    uint64_t CardRecordsRevision = 0U;
    uint64_t CardRecordsStaticRevision = 0U;
    bool CardRecordsShadowGrid = false;
    float CardClusterSize = 0.0F;
    std::vector<GrassCardDrawBatch> CardBatches;
    bool CardsPrepared = false;
    Fast::Oot3dVulkanGpuProfiler* Profiler = nullptr;
    uint32_t ProfilerSlot = 0U;

    void EnsureCardsInitialized() {
        if (CardsInitialized) return;
        CardsInitialized = true;
        CardsAvailable = Cards.Initialize(PhysicalDevice, Device, *SurfaceShaders, CardTargets);
        if (!CardsAvailable)
            std::fprintf(stderr, "[grass] distant cards unavailable: %s\n", Cards.UnavailableReason().c_str());
    }

    // A failure of the cards' preparation disables them for the session (the
    // blades keep their configured draw distance from the next frame); the
    // resources stay until Shutdown since recorded frames may read them.
    void DisableCards(const char* reason) {
        if (CardsAvailable) std::fprintf(stderr, "[grass] distant cards disabled: %s\n", reason);
        CardsAvailable = false;
        CardBatches.clear();
        CardsPrepared = false;
    }

    // The card records of the active sources for this cluster size and
    // shadow grid state; card sets of unchanged sources are reused.
    void EnsureCardRecords(float clusterSize, uint64_t frameId) {
        if (CardRecordsStaticRevision == StaticRevision && CardRecordsShadowGrid == ActiveShadowGrid &&
            CardClusterSize == clusterSize && CardSets.size() == StaticSources.size())
            return;
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::shared_ptr<const GrassCardSet>> sets;
        sets.reserve(StaticSources.size());
        size_t built = 0U;
        for (const auto& source : StaticSources) {
            std::shared_ptr<const GrassCardSet> set;
            for (const auto& candidate : CardSets)
                if (candidate && candidate->Identity == source->Identity &&
                    candidate->ContentVersion == source->ContentVersion && candidate->ClusterSize == clusterSize) {
                    set = candidate;
                    break;
                }
            if (!set) {
                set = std::make_shared<const GrassCardSet>(BuildGrassCards(*source, clusterSize));
                ++built;
            }
            sets.push_back(std::move(set));
        }
        CardSets = std::move(sets);
        CardStream.Records.clear();
        CardStream.Chunks.clear();
        CardRanges.clear();
        for (size_t index = 0; index < CardSets.size(); ++index)
            CardRanges.push_back(AppendGrassCards(*CardSets[index],
                index < ShadowPointBases.size() ? ShadowPointBases[index] : 0U, ActiveShadowGrid, CardStream));
        ++CardRecordsRevision;
        CardRecordsStaticRevision = StaticRevision;
        CardRecordsShadowGrid = ActiveShadowGrid;
        CardClusterSize = clusterSize;
        if (GrassDiagnosticsEnabled())
            std::fprintf(stderr, "[grass-cards] frame=%llu sources=%zu built=%zu cards=%zu build_ms=%.3f\n",
                         static_cast<unsigned long long>(frameId), CardSets.size(), built, CardStream.Records.size(),
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    // Memory pressure. A grass buffer the device would not allocate frees the
    // GPU path's buffers and suspends that path (the CPU path draws meanwhile)
    // until the placements are built; a failure on settled placements halves
    // the static candidates and the visible budget (MemoryShift). Stepping
    // back up waits longer after each relapse.
    // The camera-centred anchor store the GPU selection reads, the one being
    // built in the background, and the upload revisions of the stores (high
    // bit set, so they never equal a whole-scene StaticRevision).
    std::shared_ptr<const GrassResidency> Residency;
    std::future<std::shared_ptr<const GrassResidency>> ResidencyBuild;
    uint64_t ResidencyRevision = 0;
    uint64_t ResidencyUploadRevision = 0;
    static constexpr float kResidencyMargin = 1000.0F;

    void AdoptResidency(std::shared_ptr<const GrassResidency> residency) {
        Residency = std::move(residency);
        ResidencyUploadRevision = (1ULL << 63U) | ++ResidencyRevision;
        if (GrassPlacementAuditEnabled())
            std::fprintf(stderr, "TRIAEVUM_GRASS_RESIDENCY_STORE revision=%llu anchors=%zu eye=%.0f,%.0f,%.0f\n",
                         static_cast<unsigned long long>(ResidencyRevision), Residency->Anchors.size(),
                         Residency->Eye[0], Residency->Eye[1], Residency->Eye[2]);
    }

    // The store for this camera: the current one while it covers the eye; a
    // background build once the camera nears its margin; a build now when the
    // placements or the LOD policy changed (there is no valid store then).
    const GrassResidency* UpdateResidency(const std::array<float, 3>& eye, const GrassLodPolicy& policy,
                                          float bladeRadiusScale, bool shadowReferences) {
        const uint64_t key = GrassResidencyPolicyKey(policy, bladeRadiusScale);
        const auto matches = [&](const GrassResidency& residency) {
            return residency.Placements == StaticRevision && residency.PolicyKey == key &&
                   residency.ShadowReferences.empty() != shadowReferences;
        };
        if (ResidencyBuild.valid() && ResidencyBuild.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto built = ResidencyBuild.get();
            if (built && matches(*built)) AdoptResidency(std::move(built));
        }
        const bool current = Residency && matches(*Residency);
        if (current && GrassResidencyCovers(*Residency, eye)) return Residency.get();
        if (!current) {
            AdoptResidency(std::make_shared<const GrassResidency>(BuildGrassResidency(
                StaticSources, StaticRevision, eye, policy, bladeRadiusScale, kResidencyMargin, shadowReferences)));
            return Residency.get();
        }
        if (!ResidencyBuild.valid())
            ResidencyBuild = std::async(std::launch::async, [sources = StaticSources, revision = StaticRevision, eye,
                                                             policy, bladeRadiusScale, shadowReferences] {
                return std::make_shared<const GrassResidency>(BuildGrassResidency(
                    sources, revision, eye, policy, bladeRadiusScale, kResidencyMargin, shadowReferences));
            });
        // Until the next store is ready this one still covers the camera
        // (its margin is twice the distance that triggered the rebuild).
        return Residency.get();
    }

    uint32_t MemoryShift = 0;
    uint32_t BaseCandidateCapacity = 8U * 1024U * 1024U;
    bool GpuSuspendedForMemory = false;
    bool MemoryReleasePending = false;
    uint64_t MemoryReleaseFrame = 0;
    uint64_t GpuRetryFrame = 0;
    uint64_t LastMemoryChangeFrame = 0;
    uint64_t MemoryStepUpFrames = 3600;
    VkBuffer PreparedSelectionControl = VK_NULL_HANDLE;
    // GPU selection verification: the CPU selection of each frame slot.
    std::array<std::vector<uint32_t>, 2> VerifyCpuSelection;
    std::array<bool, 2> VerifyCpuValid{};
    uint64_t GpuVerifyFrames = 0U, GpuVerifyMismatchFrames = 0U, GpuVerifyMismatchRoots = 0U;
    GrassInteractionField InteractionField;
    uint64_t LastClockSequence = 0;
    uint64_t LastFallbackFrame = 0;
    bool HasClockSequence = false;
    bool HasFallbackFrame = false;
    double FallbackSeconds = 0.0;
    uint32_t LastBlades = 0;
    // The last selection's budget thinning cut (kGrassBudgetCutAll: none).
    uint32_t ThinningCut = kGrassBudgetCutAll;
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
    VkBuffer PreparedRootAnchors = VK_NULL_HANDLE;
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
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}};
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
            uint32_t count = hardware > 2U
                ? std::min(hardware - 2U, 12U)
                : 1U;
            // Diagnostic: TRIAEVUM_GRASS_WORKERS=n for worker-count sweeps.
            if (const char* value = std::getenv("TRIAEVUM_GRASS_WORKERS"))
                count = std::clamp<uint32_t>(static_cast<uint32_t>(std::strtoul(value, nullptr, 10)), 1U, 32U);
            WorkerPool =
                std::make_unique<BS::thread_pool>(count);
        }
        return static_cast<size_t>(
            WorkerPool->get_thread_count());
    }

    GrassClusterSelectionStats SelectExactClusterWork(const GrassWorldPlacement& world,
        const std::array<float, 16>& clip, const std::array<float, 3>& eye,
        const GrassLodPolicy& policy, float bladeRadiusScale, bool frustumCulling,
        std::vector<GrassClusterWork>& clusterWork) {
        SelectionRoots.clear();
        GrassStageTimer traverseTimer(GrassStage::SelectTraverse);
        if (world.Clusters.size() < kGrassClustersForParallelSelection || world.VisibilityNodes.empty() || WorkerCount() < 2)
            return SelectGrassClusterWork(world, clip, eye, policy, bladeRadiusScale, frustumCulling, clusterWork);
        GrassClusterSelectionStats stats;
        stats.TestedNodes = SplitGrassClusterSelection(world, clip, eye, policy, bladeRadiusScale,
            frustumCulling, static_cast<uint32_t>(WorkerCount()), SelectionRoots);
        clusterWork.clear();
        SelectionOutputs.resize(SelectionRoots.size());
        std::vector<std::future<GrassClusterSelectionStats>> tasks;
        tasks.reserve(SelectionRoots.size());
        std::vector<GrassJobTiming> timings(SelectionRoots.size());
        // Immutable inputs and per-job output only. Admission and draw ordering
        // happen after all jobs complete, never in completion order.
        try {
            for (size_t i = 0; i < SelectionRoots.size(); ++i) {
                tasks.push_back(WorkerPool->submit_task([&, i] {
                    const auto start = GrassJobTiming::Start();
                    const auto result = SelectGrassClusterSubtreeWork(world, clip, eye, policy, bladeRadiusScale,
                        frustumCulling, SelectionRoots[i], SelectionOutputs[i]);
                    timings[i].Finish(start);
                    return result;
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
        if (GrassStageProfileEnabled()) {
            auto& f = GrassStageCurrent();
            AddGrassJobTimings(f.TraverseJobs, f.TraverseJobMs, f.TraverseMaxJobMs, timings.data(), timings.size());
        }
        clusterWork.reserve(stats.CandidateClusters);
        for (const auto& output : SelectionOutputs)
            clusterWork.insert(clusterWork.end(), output.begin(), output.end());
        traverseTimer.Stop();
        GrassStageTimer orderTimer(GrassStage::SelectOrder);
        OrderGrassClusters(clusterWork, [](const auto& work) { return work.ClusterIndex; });
        return stats;
    }

    // Runs count independent jobs on the pool (inline when there is one) and
    // joins them all before returning, also when one throws.
    template<class Job> void RunJobs(size_t count, Job&& job, uint64_t& jobs, double& total, double& maximum) {
        std::vector<GrassJobTiming> timings(count);
        if (count <= 1U) {
            for (size_t i = 0; i < count; ++i) {
                const auto start = GrassJobTiming::Start();
                job(i);
                timings[i].Finish(start);
            }
        } else {
            std::vector<std::future<void>> tasks;
            tasks.reserve(count);
            try {
                for (size_t i = 0; i < count; ++i)
                    tasks.push_back(WorkerPool->submit_task([&, i] {
                        const auto start = GrassJobTiming::Start();
                        job(i);
                        timings[i].Finish(start);
                    }));
                for (auto& task : tasks) task.get();
            } catch (...) {
                for (auto& task : tasks) if (task.valid()) task.wait();
                throw;
            }
        }
        AddGrassJobTimings(jobs, total, maximum, timings.data(), timings.size());
    }

    // Conservative candidates of one envelope, in cluster-index order.
    GrassClusterSelectionStats BuildCandidates(const GrassWorldPlacement& world, const GrassCandidateEnvelope& envelope,
        const GrassLodPolicy& policy, float bladeRadiusScale, bool frustumCulling,
        std::vector<GrassClusterCandidate>& candidates) {
        GrassStageTimer traverseTimer(GrassStage::SelectTraverse);
        SelectionRoots.clear();
        const bool parallel = world.Clusters.size() >= kGrassClustersForParallelSelection &&
            !world.VisibilityNodes.empty() && WorkerCount() >= 2;
        GrassClusterSelectionStats stats;
        if (!parallel) {
            stats = SelectGrassClusterCandidates(world, envelope, policy, bladeRadiusScale, frustumCulling, 0U, candidates);
        } else {
            stats.TestedNodes = SplitGrassClusterCandidates(world, envelope, policy, bladeRadiusScale, frustumCulling,
                static_cast<uint32_t>(WorkerCount()), SelectionRoots);
            CandidateOutputs.resize(SelectionRoots.size());
            std::vector<GrassClusterSelectionStats> results(SelectionRoots.size());
            auto& f = GrassStageCurrent();
            uint64_t ignoredJobs = 0;
            double ignoredTotal = 0, ignoredMaximum = 0;
            const bool profile = GrassStageProfileEnabled();
            RunJobs(SelectionRoots.size(), [&](size_t i) {
                results[i] = SelectGrassClusterCandidates(world, envelope, policy, bladeRadiusScale, frustumCulling,
                    SelectionRoots[i], CandidateOutputs[i]);
            }, profile ? f.TraverseJobs : ignoredJobs, profile ? f.TraverseJobMs : ignoredTotal,
               profile ? f.TraverseMaxJobMs : ignoredMaximum);
            candidates.clear();
            for (size_t i = 0; i < results.size(); ++i) {
                stats.TestedNodes += results[i].TestedNodes;
                stats.CandidateAnchors += results[i].CandidateAnchors;
                candidates.insert(candidates.end(), CandidateOutputs[i].begin(), CandidateOutputs[i].end());
            }
        }
        traverseTimer.Stop();
        GrassStageTimer orderTimer(GrassStage::SelectOrder);
        OrderGrassClusters(candidates, [](const auto& candidate) { return candidate.ClusterIndex; });
        stats.CandidateClusters = static_cast<uint32_t>(candidates.size());
        return stats;
    }

    // The exact cluster work of the current view from an envelope's candidates.
    bool RefineCandidates(const GrassWorldPlacement& world, std::span<const GrassClusterCandidate> candidates,
        const std::array<float, 16>& clip, const std::array<float, 3>& eye, const GrassLodPolicy& policy,
        float bladeRadiusScale, bool frustumCulling, std::vector<GrassClusterWork>& clusterWork, uint64_t& anchors) {
        GrassStageTimer refineTimer(GrassStage::SelectRefine);
        const size_t jobs = candidates.size() >= kGrassClustersForParallelSelection && WorkerCount() >= 2
            ? std::min<size_t>(WorkerCount(), candidates.size() / (kGrassClustersForParallelSelection / 2U)) : 1U;
        if (jobs <= 1U)
            return RefineGrassClusterCandidates(world, candidates, clip, eye, policy, bladeRadiusScale, frustumCulling,
                clusterWork, anchors);
        RefineOutputs.resize(jobs);
        std::vector<uint64_t> jobAnchors(jobs);
        std::vector<uint8_t> valid(jobs);
        const size_t perJob = (candidates.size() + jobs - 1U) / jobs;
        auto& f = GrassStageCurrent();
        uint64_t ignoredJobs = 0;
        double ignoredTotal = 0, ignoredMaximum = 0;
        const bool profile = GrassStageProfileEnabled();
        RunJobs(jobs, [&](size_t i) {
            const size_t first = std::min(i * perJob, candidates.size());
            valid[i] = RefineGrassClusterCandidates(world, candidates.subspan(first,
                std::min(perJob, candidates.size() - first)), clip, eye, policy, bladeRadiusScale, frustumCulling,
                RefineOutputs[i], jobAnchors[i]) ? 1U : 0U;
        }, profile ? f.RefineJobs : ignoredJobs, profile ? f.RefineJobMs : ignoredTotal,
           profile ? f.RefineMaxJobMs : ignoredMaximum);
        clusterWork.clear();
        anchors = 0U;
        for (size_t i = 0; i < jobs; ++i) {
            if (!valid[i]) return false;
            anchors += jobAnchors[i];
            clusterWork.insert(clusterWork.end(), RefineOutputs[i].begin(), RefineOutputs[i].end());
        }
        return true;
    }

    // The candidates of the envelope that contains this view, built when none
    // does. Callers refine them with the exact decisions of this view, so the
    // selection never depends on which earlier view built them.
    CandidateCacheEntry& AcquireCandidates(const GrassWorldPlacement& world,
        const std::array<float, 16>& clip, const std::array<float, 3>& eye,
        const GrassLodPolicy& policy, float bladeRadiusScale, bool frustumCulling, uint32_t& testedNodes) {
        ++CandidateClock;
        CandidateCacheEntry* entry = nullptr;
        for (auto& candidate : CandidateCache)
            if (candidate.Identity == world.Identity && candidate.ContentVersion == world.ContentVersion &&
                candidate.Policy == policy && candidate.BladeRadiusScale == bladeRadiusScale &&
                candidate.FrustumCulling == frustumCulling &&
                GrassCandidateEnvelopeContains(candidate.Envelope, clip, eye)) {
                entry = &candidate;
                break;
            }
        if (entry != nullptr) {
            if (GrassStageProfileEnabled())
                ++GrassStageCurrent().CandidateHit;
        } else {
            if (CandidateCache.size() < kGrassCandidateCacheEntries) entry = &CandidateCache.emplace_back();
            else entry = &*std::min_element(CandidateCache.begin(), CandidateCache.end(),
                [](const auto& a, const auto& b) { return a.LastUsed < b.LastUsed; });
            const auto parameters = GrassCandidateEnvelopeParameters();
            entry->Identity = world.Identity;
            entry->ContentVersion = world.ContentVersion;
            entry->Policy = policy;
            entry->BladeRadiusScale = bladeRadiusScale;
            entry->FrustumCulling = frustumCulling;
            entry->Envelope = MakeGrassCandidateEnvelope(clip, eye, std::max(parameters[0], 0.0F),
                std::max(parameters[1], 0.0F), std::max(parameters[2], 0.0F));
            testedNodes += BuildCandidates(world, entry->Envelope, policy, bladeRadiusScale, frustumCulling,
                entry->Candidates).TestedNodes;
            if (GrassStageProfileEnabled())
                ++GrassStageCurrent().CandidateMiss;
        }
        entry->LastUsed = CandidateClock;
        return *entry;
    }

    // A retained prefix reached past its conservative end: drop the envelope.
    void RejectCandidates(CandidateCacheEntry& entry) {
        entry.Identity = 0U;
        entry.ContentVersion = 0U;
        if (GrassStageProfileEnabled())
            ++GrassStageCurrent().CandidateViolation;
    }

    // Cluster work of one source for the current view, materialized (the
    // render loop fuses the refinement into root evaluation instead).
    GrassClusterSelectionStats SelectClusterWork(const GrassWorldPlacement& world,
        const std::array<float, 16>& clip, const std::array<float, 3>& eye,
        const GrassLodPolicy& policy, float bladeRadiusScale, bool frustumCulling) {
        if (!GrassCandidateCacheEnabled())
            return SelectExactClusterWork(world, clip, eye, policy, bladeRadiusScale, frustumCulling, ClusterWork);
        GrassClusterSelectionStats stats;
        uint64_t anchors = 0U;
        bool refined = false;
        for (int attempt = 0; attempt < 2 && !refined; ++attempt) {
            auto& entry = AcquireCandidates(world, clip, eye, policy, bladeRadiusScale, frustumCulling, stats.TestedNodes);
            refined = RefineCandidates(world, entry.Candidates, clip, eye, policy, bladeRadiusScale, frustumCulling,
                ClusterWork, anchors);
            if (!refined) RejectCandidates(entry);
        }
        // The reference view lies inside its own envelope; failing twice means
        // a broken bound, so take the full traversal.
        if (!refined)
            return SelectExactClusterWork(world, clip, eye, policy, bladeRadiusScale, frustumCulling, ClusterWork);
        stats.CandidateClusters = static_cast<uint32_t>(ClusterWork.size());
        stats.CandidateAnchors = anchors;
        if (GrassSelectionVerifyEnabled()) {
            GrassStageTimer diagnosticsTimer(GrassStage::Diagnostics);
            const auto exact = SelectExactClusterWork(world, clip, eye, policy, bladeRadiusScale, frustumCulling,
                VerifyClusterWork);
            if (VerifyClusterWork != ClusterWork || exact.CandidateAnchors != anchors) {
                ++VerifyMismatches;
                size_t first = 0;
                while (first < std::min(VerifyClusterWork.size(), ClusterWork.size()) &&
                       VerifyClusterWork[first] == ClusterWork[first]) ++first;
                std::fprintf(stderr, "[grass-candidates] verify mismatch=%llu source=%llx exact=%zu cached=%zu first=%zu "
                    "exact_cluster=%u cached_cluster=%u\n", static_cast<unsigned long long>(VerifyMismatches),
                    static_cast<unsigned long long>(world.Identity), VerifyClusterWork.size(), ClusterWork.size(), first,
                    first < VerifyClusterWork.size() ? VerifyClusterWork[first].ClusterIndex : 0U,
                    first < ClusterWork.size() ? ClusterWork[first].ClusterIndex : 0U);
            }
            ++VerifyChecks;
        }
        return stats;
    }

    static constexpr uint32_t kMaximumMemoryShift = 4;
    static constexpr uint64_t kMemoryRetryFrames = 30;

    void SetMemoryShift(uint32_t shift, uint64_t frameId) {
        MemoryShift = shift;
        PlacementBuilder.SetCandidateCapacity(BaseCandidateCapacity >> shift);
        LastMemoryChangeFrame = frameId;
    }

    void OnOutOfMemory(uint64_t frameId) {
        if (PlacementBuilder.Stats().Pending == 0U && MemoryShift < kMaximumMemoryShift) {
            // A relapse soon after stepping up waits twice as long next time.
            if (frameId - LastMemoryChangeFrame < MemoryStepUpFrames) MemoryStepUpFrames *= 2U;
            SetMemoryShift(MemoryShift + 1U, frameId);
        }
        // This frame's command buffer, still recording, may use the GPU path's
        // buffers (an earlier invocation, or this one's selection upload):
        // they are freed by the next frame, after it has been submitted.
        MemoryReleasePending = true;
        MemoryReleaseFrame = frameId;
        GpuSuspendedForMemory = true;
        GpuRetryFrame = frameId + kMemoryRetryFrames;
        std::fprintf(stderr, "[grass] out of GPU memory: GPU path suspended, scale 1/%u\n", 1U << MemoryShift);
    }

    void UpdateMemoryScale(uint64_t frameId) {
        if (MemoryReleasePending && frameId != MemoryReleaseFrame) {
            GpuSelector.ReleaseMemory();
            InstanceCompactor.ReleaseMemory();
            MemoryReleasePending = false;
        }
        if (GpuSuspendedForMemory && frameId >= GpuRetryFrame && PlacementBuilder.Stats().Pending == 0U)
            GpuSuspendedForMemory = false;
        if (!GpuSuspendedForMemory && MemoryShift > 0U && frameId - LastMemoryChangeFrame >= MemoryStepUpFrames)
            SetMemoryShift(MemoryShift - 1U, frameId);
    }

    void DestroyBuffer(BufferSlot& slot) {
        if (slot.Mapped != nullptr) vkUnmapMemory(Device, slot.Memory);
        if (slot.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, slot.Buffer, nullptr);
        if (slot.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(Device, slot.Memory, nullptr);
            TrackGrassGpuMemory(false, -static_cast<int64_t>(slot.Capacity));
        }
        slot = {};
    }

    void EnsureBuffer(
        BufferSlot& slot, VkDeviceSize bytes,
        VkBufferUsageFlags usage, VkDeviceSize minimumCapacity = 1U << 20U) {
        if (bytes <= slot.Capacity) return;
        DestroyBuffer(slot);
        try {
            CreateBuffer(slot, bytes, usage, minimumCapacity);
        } catch (...) {
            // Never keep a half-built buffer: the next frame would take its
            // capacity for a usable, mapped allocation.
            DestroyBuffer(slot);
            throw;
        }
    }

    void CreateBuffer(BufferSlot& slot, VkDeviceSize bytes, VkBufferUsageFlags usage, VkDeviceSize minimumCapacity) {
        slot.Capacity = GrassBufferCapacity(std::max(bytes, minimumCapacity));
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
        if (GrassGpuAllocationRefused(PhysicalDevice, allocation.memoryTypeIndex, slot.Capacity) ||
            vkAllocateMemory(Device, &allocation, nullptr, &slot.Memory) != VK_SUCCESS)
            throw GrassGpuOutOfMemory(
                "cannot allocate interactive grass streaming buffer");
        TrackGrassGpuMemory(false, static_cast<int64_t>(slot.Capacity), "streaming", usage);
        if (vkBindBufferMemory(Device, slot.Buffer, slot.Memory, 0) != VK_SUCCESS ||
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
    mImpl->BaseCandidateCapacity = std::min(8U * 1024U * 1024U,
        grassDeviceProperties.limits.maxStorageBufferRange / static_cast<uint32_t>(sizeof(GrassWorldAnchor)));
    mImpl->SetMemoryShift(mImpl->MemoryShift, 0);
    mImpl->Device = device;
    mImpl->SurfaceShaders = &shaders;
    try {
        const auto vertexSource = BuildGrassVertexShader();
        const auto fragmentSource = BuildGrassFragmentShader();
        const auto rasterDepthSource = BuildGrassFragmentShader(true);
        const auto wholeSource = BuildGrassFragmentShader(true, true);
        VkShaderModule vertex = Compile(shaders, device, vertexSource.c_str(),
                                        Renderer::SpirvStage::Vertex, "interactive_grass.vert");
        std::array<VkShaderModule, 6> fragments{};
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
            fragments[4] = Compile(
                shaders, device, wholeSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_canonical_whole.frag",
                "GRASS_AUXILIARY_OUTPUTS", "0");
            fragments[5] = Compile(
                shaders, device, wholeSource.c_str(), Renderer::SpirvStage::Fragment,
                "interactive_grass_instrumented_whole.frag",
                "GRASS_AUXILIARY_OUTPUTS", "1");
            const std::array<VkDescriptorSetLayoutBinding, 10> environmentBindings{{
                {0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {2U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {3U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {4U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {5U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {6U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {7U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {8U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                {9U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_VERTEX_BIT, nullptr}}};
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
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 18}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}}};
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

            // No vertex input: each root reads its anchor and bend from storage.
            const GrassPipelineTargets targets{canonicalRenderPass, instrumentedRenderPass, sampleCount,
                                               dynamicRendering, depthFormat, pipelineCache};
            CreateGrassPipelineSet(device, targets, mImpl->PipelineLayout, vertex, fragments, mImpl->Pipelines);
            // The distant cards draw into the same targets, created when a
            // frame first enables them; without them the blades keep their
            // configured draw distance.
            mImpl->CardTargets = targets;
            mImpl->CardsInitialized = false;
            mImpl->CardsAvailable = false;
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
        mImpl->GpuSelectorAvailable = mImpl->GpuCompactorAvailable &&
            mImpl->GpuSelector.Initialize(physicalDevice, device, shaders, pipelineCache);
        if (mImpl->GpuCompactorAvailable && !mImpl->GpuSelectorAvailable)
            std::fprintf(stderr, "[grass] GPU selection unavailable: %s\n", mImpl->GpuSelector.UnavailableReason().c_str());
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
    mImpl->PreparedRootAnchors = VK_NULL_HANDLE;
    mImpl->CardsPrepared = false;
    mImpl->CardBatches.clear();
    mImpl->Profiler = profiler;
    mImpl->ProfilerSlot = frameSlot;
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
        if (GrassPlacementAuditEnabled()) {
            const auto gpu = GrassGpuMemoryInUse();
            AuditGrassPass(telemetry.FrameId, status, telemetry.VisibleBlades,
                           {placementStatsAfter.ResidentBytes, placementStatsAfter.Entries,
                            placementStatsAfter.Pending, placementStatsAfter.Evictions, gpu.DeviceLocal,
                            gpu.HostVisible, mImpl->MemoryShift},
                           status == GrassRenderStatus::Drawn ? mImpl->ThinningCut : kGrassBudgetCutAll);
        }
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
    mImpl->UpdateMemoryScale(frameId);
    const uint32_t visibleBudget = std::max(1U, settings.MaxInstancesPerRoom >> mImpl->MemoryShift);
    // With the distant cards the blades end where the clump cards are whole;
    // the cards then carry the grass to the horizon (grass_card_lod.h).
    GrassCardLodPolicy cardPolicy = BuildGrassCardLodPolicy(settings);
    if (cardPolicy.Enabled && mImpl->Device != VK_NULL_HANDLE) mImpl->EnsureCardsInitialized();
    if (!mImpl->CardsAvailable) cardPolicy.Enabled = false;
    const bool cardsEnabled = cardPolicy.Enabled;
    auto lodPolicy = BuildGrassLodPolicy(settings);
    lodPolicy.DrawDistance = GrassCardLodBladeDrawDistance(cardPolicy, lodPolicy.DrawDistance);
    const float bladeFadeFraction = GrassCardLodBladeFadeFraction(cardPolicy, settings.DrawFadeFraction);
    const float drawDistanceSquared = lodPolicy.DrawDistance * lodPolicy.DrawDistance;
    const GrassLodEvaluator lodEvaluator(lodPolicy);
    const float bladeRadiusScale = GrassBladeRadiusScale(
        settings.Appearance.BladeCurvature,
        settings.Appearance.ShapeVariation, settings.MaximumBend) +
        (settings.FarTuftsEnabled ? settings.Generation.BladeWidthMax *
            (1.0F + (settings.FarTuftBladeCount - 1U) * settings.FarTuftSpread) /
            std::max(settings.Generation.BladeHeightMin, 0.001F) : 0.0F);
    const GrassCardAtlasShape atlasShape = GrassCardAtlasShapeFromSettings(settings);
    // Segment counts drawn only below the whole-blade distance (with a margin
    // for the rounding of the two distance evaluations) have no fading blades:
    // their batches skip the fading pass.
    {
        const float wholeDistance = GrassWholeBladeDistance(
            settings.LodReferenceDistance > 0.0F ? settings.LodReferenceDistance : settings.DrawDistance,
            settings.LodStartFraction, settings.FarTuftsEnabled && !settings.MidrangeClustersEnabled,
            settings.LodEndFraction, lodPolicy.DrawDistance, bladeFadeFraction);
        mImpl->FadingSegments = 0U;
        for (uint32_t segments = kMinimumGrassBladeSegments; segments <= kMaximumGrassBladeSegments; ++segments)
            if (!(GrassSegmentsMaximumDistance(lodPolicy, static_cast<uint8_t>(segments)) * 1.0001F < wholeDistance))
                mImpl->FadingSegments |= 1U << segments;
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
        mImpl->VisibleAnchorIndices.reserve(visibleBudget);
        const size_t slotIndex = frameSlot % mImpl->EnvironmentBuffers.size();
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
            if (sceneMatch.MatchingMeshes > scopedMeshes.size())
                AuditGrassPlacement(rule.Target.Rgba8Hash, "other_target",
                                    sceneMatch.MatchingMeshes - scopedMeshes.size());
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
                prepared.Environment.DistanceLod[0] = lodPolicy.DrawDistance;
                prepared.Environment.TuftStyle[2] = bladeFadeFraction;
                prepared.Environment.CardDistances = {cardPolicy.BladeDistance, cardPolicy.ClumpDistance,
                                                      cardPolicy.GroupDistance, cardPolicy.BlendFraction};
                prepared.Environment.CardStyle = {atlasShape.HeightOverRadius[0], atlasShape.HeightOverRadius[1],
                                                  atlasShape.HeightOverRadius[2], cardsEnabled ? 1.0F : 0.0F};
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
            if (GrassPlacementAuditEnabled()) {
                const bool anchors = result.Placement && !result.Placement->Anchors.empty();
                AuditGrassPlacement(placementRequests[i].Rule.Target.Rgba8Hash,
                                    result.State == GrassAsyncPlacementState::Failed    ? "failed"
                                    : anchors                                           ? "ready"
                                    : result.State == GrassAsyncPlacementState::Ready   ? "no_anchors"
                                                                                        : "pending",
                                    anchors ? result.Placement->Anchors.size() : 0U);
            }
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
            const auto rebuildStart = std::chrono::steady_clock::now();
            mImpl->ActivePlacements = std::move(activePlacements);
            mImpl->ActiveShadowGrid = receiveShadows;
            mImpl->StaticSources.clear();
            mImpl->StaticBases.clear();
            mImpl->StaticSegments.clear();
            mImpl->StaticAnchorCount = totalStaticAnchors;
            mImpl->ShadowPoints.clear();
            mImpl->ShadowReferences.clear();
            mImpl->ShadowSurfaces.clear();
            mImpl->ShadowPointBases.clear();
            if (receiveShadows) {
                size_t points = 0U, references = 0U, surfaces = 0U;
                for (const auto& prepared : preparedPlacements) {
                    points += prepared.World->ShadowPoints.size();
                    references += prepared.World->ShadowReferences.size();
                    surfaces += prepared.World->ShadowSurfaces.size();
                }
                mImpl->ShadowPoints.reserve(points);
                mImpl->ShadowReferences.reserve(references);
                mImpl->ShadowSurfaces.reserve(surfaces);
            }
            for (const auto& prepared : preparedPlacements) {
                const auto pointBase = static_cast<uint32_t>(mImpl->ShadowPoints.size());
                mImpl->ShadowPointBases.push_back(pointBase);
                if (receiveShadows) {
                    if (!GrassPlacementHasShadowGrid(*prepared.World))
                        throw std::runtime_error("grass shadow grid does not cover the immutable placement");
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
                mImpl->StaticSources.push_back(prepared.World);
                mImpl->StaticBases.push_back(prepared.StaticBaseIndex);
                mImpl->StaticSegments.emplace_back(prepared.World->Anchors);
            }
            ++mImpl->StaticRevision;
            if (mImpl->StaticRevision == 0U) {
                ++mImpl->StaticRevision;
            }
            if (GrassDiagnosticsEnabled())
                std::fprintf(stderr, "[grass-entry] frame=%llu static_rebuild_ms=%.3f sources=%zu anchors=%zu "
                    "shadow_points=%zu\n", static_cast<unsigned long long>(frameId),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count(),
                    mImpl->ActivePlacements.size(), mImpl->StaticAnchorCount, mImpl->ShadowPoints.size());
        }
        if (cardsEnabled) {
            GrassStageTimer cardTimer(GrassStage::Cards);
            mImpl->EnsureCardRecords(settings.CullingClusterSize, frameId);
        }

        if (GrassPlacementAuditEnabled() && frameId % 100U == 0U) {
            // Residency study: the anchors a camera-centred store would hold
            // (each cluster's retained prefix at its nearest distance less a
            // movement margin, any view direction) against the whole scene.
            constexpr std::array<float, 4> margins{0.0F, 500.0F, 1000.0F, 2000.0F};
            std::array<uint64_t, 4> needed{};
            uint64_t total = 0;
            for (const auto& prepared : preparedPlacements) {
                const auto& world = *prepared.World;
                total += world.CullingAnchors.size();
                for (const auto& cluster : world.Clusters) {
                    if (cluster.FirstAnchor >= world.CullingAnchors.size()) continue;
                    const float radius = cluster.Radius + cluster.MaximumBladeHeight * (bladeRadiusScale - 1.0F);
                    float distanceSquared = 0.0F;
                    for (size_t axis = 0; axis < 3U; ++axis) {
                        const float delta = cluster.Center[axis] - prepared.Eye[axis];
                        distanceSquared += delta * delta;
                    }
                    const auto begin = world.CullingAnchors.begin() + cluster.FirstAnchor;
                    const auto end = begin + std::min<size_t>(cluster.AnchorCount, world.CullingAnchors.end() - begin);
                    for (size_t m = 0; m < margins.size(); ++m) {
                        const float nearest = std::max(0.0F, std::sqrt(distanceSquared) - radius - margins[m]);
                        if (nearest > lodPolicy.DrawDistance) continue;
                        const float retention = GrassLodRetentionUpperBound(lodPolicy, nearest);
                        needed[m] += std::upper_bound(begin, end, retention, [](float value, const auto& anchor) {
                            return value < anchor.StableVisibility;
                        }) - begin;
                    }
                }
            }
            std::fprintf(stderr, "TRIAEVUM_GRASS_RESIDENCY frame=%llu anchors=%llu margin0=%llu margin500=%llu "
                         "margin1000=%llu margin2000=%llu\n", static_cast<unsigned long long>(frameId),
                         static_cast<unsigned long long>(total), static_cast<unsigned long long>(needed[0]),
                         static_cast<unsigned long long>(needed[1]), static_cast<unsigned long long>(needed[2]),
                         static_cast<unsigned long long>(needed[3]));
        }
        GrassSelectionKey selectionKey{mImpl->StaticRevision, lodPolicy, bladeRadiusScale,
            visibleBudget, settings.FrustumCulling, {}};
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
        const bool gpuSelection = GrassGpuSelectionEnabled() && mImpl->GpuCompactorAvailable &&
            !mImpl->GpuSuspendedForMemory &&
            mImpl->GpuSelectorAvailable && preparedPlacements.size() <= kGrassGpuSelectionMaximumSources &&
            !(settings.MidrangeClustersEnabled && std::any_of(preparedPlacements.begin(), preparedPlacements.end(),
                [](const auto& prepared) { return !prepared.World->Midrange.Groups.empty(); }));
        const bool cpuSelection = !gpuSelection || GrassGpuSelectionVerifyEnabled();
        // The GPU selection reads the camera-centred store; the CPU selection
        // (and its verification) indexes the whole scene.
        const GrassResidency* residency =
            gpuSelection && !cpuSelection && GrassResidencyEnabled() && !preparedPlacements.empty()
                ? mImpl->UpdateResidency(preparedPlacements.front().Eye, lodPolicy, bladeRadiusScale, receiveShadows)
                : nullptr;
        if (!cpuSelection) mImpl->SelectionCache.Reset();
        const bool reuseSelection = cpuSelection && mImpl->SelectionCache.Matches(selectionKey);
        if (GrassStageProfileEnabled()) {
            if (reuseSelection)
                ++GrassStageCurrent().SelectionHit;
            else
                ++GrassStageCurrent().SelectionMiss;
        }
        if (!reuseSelection)
            ++mImpl->SelectionRevision;
        if (!cpuSelection) {
            batches.clear();
            mImpl->DrawGroups.clear();
            mImpl->VisibleAnchorIndices.clear();
        } else if (reuseSelection) {
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
            // Phase 1: every visible root of every placement, with its thinning
            // bucket. Cluster-owned placements (midrange groups) keep their own
            // truncation and take their roots out of the budget first.
            uint32_t ownedRemaining = visibleBudget;
            auto& placementSelections = mImpl->PlacementSelections;
            if (placementSelections.size() < preparedPlacements.size())
                placementSelections.resize(preparedPlacements.size());
            for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& prepared = preparedPlacements[placementIndex];
                auto& selection = placementSelections[placementIndex];
                selection.Clear();
                const bool clusterOwned = settings.MidrangeClustersEnabled && !prepared.World->Midrange.Groups.empty();
                selection.ClusterOwned = clusterOwned;
                if (clusterOwned && ownedRemaining == 0U) continue;
                const auto processCluster = [&](const GrassClusterWork& work, GrassWorkerOutput& output) {
                    GrassClusterCounters counters;
                    const auto& cluster = prepared.World->Clusters[work.ClusterIndex];
                    for (uint32_t anchorIndex = cluster.FirstAnchor; anchorIndex < work.AnchorEnd; ++anchorIndex) {
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
                            lodEvaluator.Resolve(distance, anchor.StableVisibility);
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
                        const auto bin = lod.DistantTuft ? kGrassLodBinCount - 1U
                                                         : GrassLodBinIndex(lod.BladeSegments, lod.PlaneCount);
                        const auto bucket = GrassBudgetBucket(anchor.StableVisibility, lod.Retention);
                        output.Bins[bin].push_back(prepared.StaticBaseIndex + anchorIndex);
                        output.Buckets[bin].push_back(static_cast<uint16_t>(bucket));
                        ++output.Histogram[bucket];
                        ++counters.Visible;
                    }
                    return counters;
                };

                const auto selectionStart = std::chrono::steady_clock::now();
                auto& clusterWork = mImpl->ClusterWork;
                size_t parallelWorkerCount = 0U;
                uint64_t retainedCandidates = 0U;
                size_t selectedClusters = 0U;
                bool fused = false;
                // Envelope candidates: the exact decision of each cluster for this
                // view runs right before its roots, in the same cluster order and
                // with the same budget admission as the materialized path.
                if (!clusterOwned && GrassCandidateCacheEnabled() && !GrassSelectionVerifyEnabled()) {
                    uint32_t testedNodes = 0U;
                    auto& entry = mImpl->AcquireCandidates(*prepared.World, prepared.Push.PositionToClip, prepared.Eye,
                        lodPolicy, bladeRadiusScale, settings.FrustumCulling, testedNodes);
                    telemetry.VisibilityNodesTested += testedNodes;
                    GrassStageTimer fusedTimer(GrassStage::SelectEvaluate);
                    const GrassClusterRefiner refiner(*prepared.World, prepared.Push.PositionToClip, prepared.Eye,
                        lodPolicy, bladeRadiusScale, settings.FrustumCulling);
                    const auto& candidates = entry.Candidates;
                    const size_t jobs = visibleBudget >= kGrassAnchorsPerWorker &&
                            candidates.size() >= kGrassClustersForParallelSelection && mImpl->WorkerCount() >= 2U
                        ? std::min(mImpl->WorkerCount(), candidates.size() / (kGrassClustersForParallelSelection / 2U))
                        : 1U;
                    std::atomic<bool> violation{false};
                    struct FusedStats {
                        GrassClusterCounters Counters;
                        uint64_t Clusters = 0U, Anchors = 0U;
                    };
                    std::vector<FusedStats> jobStats(jobs);
                    const auto evaluateRange = [&](size_t first, size_t last, GrassWorkerOutput& output,
                                                   FusedStats& stats) {
                        GrassClusterWork work;
                        const auto& clusters = prepared.World->Clusters;
                        const auto& cullingAnchors = prepared.World->CullingAnchors;
                        for (size_t i = first; i < last; ++i) {
                            // The candidates are sparse in index order: fetch the
                            // cluster records ahead, then the first culling anchors
                            // of clusters whose records are already on their way.
                            if (i + kGrassClusterPrefetchDistance < last)
                                GrassPrefetch(&clusters[candidates[i + kGrassClusterPrefetchDistance].ClusterIndex]);
                            if (i + kGrassAnchorPrefetchDistance < last) {
                                const auto& ahead = clusters[candidates[i + kGrassAnchorPrefetchDistance].ClusterIndex];
                                if (ahead.FirstAnchor < cullingAnchors.size())
                                    GrassPrefetch(&cullingAnchors[ahead.FirstAnchor]);
                            }
                            const auto result = refiner.Refine(candidates[i], work);
                            if (result == GrassClusterRefiner::Result::Rejected) continue;
                            if (result == GrassClusterRefiner::Result::Violation ||
                                violation.load(std::memory_order_relaxed)) {
                                violation.store(true, std::memory_order_relaxed);
                                return;
                            }
                            ++stats.Clusters;
                            stats.Anchors += work.AnchorEnd - prepared.World->Clusters[work.ClusterIndex].FirstAnchor;
                            const auto counters = processCluster(work, output);
                            stats.Counters.Evaluated += counters.Evaluated;
                            stats.Counters.Visible += counters.Visible;
                        }
                    };
                    for (size_t workerIndex = 0U; workerIndex < jobs; ++workerIndex) selection.Output(workerIndex);
                    if (jobs > 1U) {
                        const size_t perJob = (candidates.size() + jobs - 1U) / jobs;
                        auto& f = GrassStageCurrent();
                        uint64_t ignoredJobs = 0;
                        double ignoredTotal = 0, ignoredMaximum = 0;
                        const bool profile = GrassStageProfileEnabled();
                        mImpl->RunJobs(jobs, [&](size_t i) {
                            evaluateRange(std::min(i * perJob, candidates.size()),
                                std::min((i + 1U) * perJob, candidates.size()), selection.Outputs[i], jobStats[i]);
                        }, profile ? f.EvaluateJobs : ignoredJobs, profile ? f.EvaluateJobMs : ignoredTotal,
                           profile ? f.EvaluateMaxJobMs : ignoredMaximum);
                    } else {
                        evaluateRange(0U, candidates.size(), selection.Outputs[0], jobStats[0]);
                    }
                    if (violation.load(std::memory_order_relaxed)) {
                        mImpl->RejectCandidates(entry);
                        selection.Clear();
                    } else {
                        fused = true;
                        for (const auto& stats : jobStats) {
                            selectedClusters += stats.Clusters;
                            retainedCandidates += stats.Anchors;
                        }
                        telemetry.CullingWorkers = std::max(telemetry.CullingWorkers, static_cast<uint32_t>(jobs));
                        parallelWorkerCount = jobs;
                        for (size_t workerIndex = 0U; workerIndex < jobs; ++workerIndex) {
                            auto& output = selection.Outputs[workerIndex];
                            output.Counters = jobStats[workerIndex].Counters;
                            telemetry.EvaluatedAnchors += output.Counters.Evaluated;
                            selection.Visible += output.Counters.Visible;
                        }
                    }
                }
                GrassClusterSelectionStats clusterStats{};
                if (!fused && !clusterOwned) {
                    clusterStats = mImpl->SelectClusterWork(*prepared.World, prepared.Push.PositionToClip, prepared.Eye,
                        lodPolicy, bladeRadiusScale, settings.FrustumCulling);
                    telemetry.CullingWorkers = std::max(telemetry.CullingWorkers,
                        static_cast<uint32_t>(mImpl->SelectionRoots.size()));
                    selectedClusters = clusterWork.size();
                    retainedCandidates = clusterStats.CandidateAnchors;
                }
                const auto clusterSelectionEnd = std::chrono::steady_clock::now();
                telemetry.VisibilityNodesTested += clusterStats.TestedNodes;
                telemetry.CandidateClusters += selectedClusters;
                if (GrassStageProfileEnabled()) {
                    GrassStageCurrent().SelectClusters += selectedClusters;
                    GrassStageCurrent().SelectCandidates += retainedCandidates;
                }
                GrassStageTimer evaluateTimer(GrassStage::SelectEvaluate);
                if (!fused && !clusterOwned && retainedCandidates >= kGrassAnchorsPerWorker * 2U &&
                    visibleBudget >= kGrassAnchorsPerWorker && clusterWork.size() > 1U) {
                    parallelWorkerCount =
                        std::min({ mImpl->WorkerCount(), clusterWork.size(),
                                   static_cast<size_t>(retainedCandidates / kGrassAnchorsPerWorker) });
                }
                if (fused) {
                } else if (clusterOwned) {
                    uint64_t evaluated = 0;
                    auto& output = selection.Output(0U);
                    const auto visible = SelectGrassDrawableClusters(*prepared.World, prepared.Push.PositionToClip,
                        prepared.Eye, lodPolicy, settings.MidrangeFarBladeFraction, bladeRadiusScale, settings.FrustumCulling, ownedRemaining, evaluated,
                        [&](auto selected, const GrassLodDecision& lod) {
                            const auto binIndex = GrassLodBinIndex(lod.BladeSegments, lod.PlaneCount);
                            auto& bin = output.Bins[binIndex];
                            if constexpr (std::is_same_v<decltype(selected), uint32_t>) {
                                bin.push_back(prepared.StaticBaseIndex + selected);
                            } else {
                                const auto first = bin.size();
                                selection.ClusterRanges[binIndex].push_back(
                                    {static_cast<uint32_t>(first), static_cast<uint32_t>(selected.size())});
                                bin.resize(first + selected.size());
                                std::transform(selected.begin(), selected.end(), bin.begin()+first,
                                    [&](uint32_t index) { return prepared.StaticBaseIndex + index; });
                            }
                        }, &telemetry.VisibilityNodesTested);
                    telemetry.CandidateClusters += evaluated;
                    ownedRemaining -= visible;
                    output.Counters.Visible = visible;
                    selection.Visible = visible;
                } else if (parallelWorkerCount > 1U) {
                    telemetry.CullingWorkers =
                        std::max(telemetry.CullingWorkers, static_cast<uint32_t>(parallelWorkerCount));
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex)
                        selection.Output(workerIndex);
                    std::vector<std::future<void>> tasks;
                    tasks.reserve(parallelWorkerCount);
                    std::vector<GrassJobTiming> timings(parallelWorkerCount);
                    const size_t clustersPerWorker =
                        (clusterWork.size() + parallelWorkerCount - 1U) / parallelWorkerCount;
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        const size_t beginCluster = workerIndex * clustersPerWorker;
                        const size_t endCluster = std::min(beginCluster + clustersPerWorker, clusterWork.size());
                        tasks.push_back(mImpl->WorkerPool->submit_task([&, workerIndex, beginCluster, endCluster] {
                            const auto start = GrassJobTiming::Start();
                            auto& output = selection.Outputs[workerIndex];
                            for (size_t clusterIndex = beginCluster; clusterIndex < endCluster; ++clusterIndex) {
                                const auto counters = processCluster(clusterWork[clusterIndex], output);
                                output.Counters.Evaluated += counters.Evaluated;
                                output.Counters.Visible += counters.Visible;
                            }
                            timings[workerIndex].Finish(start);
                        }));
                    }
                    for (auto& task : tasks) {
                        task.get();
                    }
                    if (GrassStageProfileEnabled()) {
                        auto& f = GrassStageCurrent();
                        AddGrassJobTimings(f.EvaluateJobs, f.EvaluateJobMs, f.EvaluateMaxJobMs, timings.data(), timings.size());
                    }
                    for (size_t workerIndex = 0U; workerIndex < parallelWorkerCount; ++workerIndex) {
                        const auto& counters = selection.Outputs[workerIndex].Counters;
                        telemetry.EvaluatedAnchors += counters.Evaluated;
                        selection.Visible += counters.Visible;
                    }
                } else {
                    telemetry.CullingWorkers = std::max(telemetry.CullingWorkers, 1U);
                    auto& output = selection.Output(0U);
                    for (const auto& work : clusterWork) {
                        const auto counters = processCluster(work, output);
                        telemetry.EvaluatedAnchors += counters.Evaluated;
                        output.Counters.Evaluated += counters.Evaluated;
                        output.Counters.Visible += counters.Visible;
                        selection.Visible += counters.Visible;
                    }
                }
                evaluateTimer.Stop();
                telemetry.SelectionMilliseconds +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - selectionStart)
                        .count();
                if (GrassDiagnosticsEnabled() && frameId % 60 == 0) {
                    std::fprintf(stderr, "[grass-selection] frame=%llu source=%u clusters=%zu candidates=%llu cluster_ms=%.3f evaluate_ms=%.3f\n",
                        static_cast<unsigned long long>(frameId), placementIndex, selectedClusters,
                        static_cast<unsigned long long>(retainedCandidates),
                        std::chrono::duration<double, std::milli>(clusterSelectionEnd-selectionStart).count(),
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-clusterSelectionEnd).count());
                }
            }

            // The budget cut over every placement's visible roots: the roots with
            // the lowest thinning keys stay, the same fraction in every cluster
            // (grass_selection_budget.h). Cluster-owned roots were taken first.
            GrassStageTimer assembleTimer(GrassStage::SelectAssemble);
            GrassBudgetHistogram histogram{};
            for (size_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& selection = placementSelections[placementIndex];
                if (selection.ClusterOwned) continue;
                for (size_t outputIndex = 0U; outputIndex < selection.OutputCount; ++outputIndex)
                    for (size_t bucket = 0U; bucket < kGrassBudgetBuckets; ++bucket)
                        histogram[bucket] += selection.Outputs[outputIndex].Histogram[bucket];
            }
            const uint32_t thinningCut = GrassBudgetCut(histogram, ownedRemaining);
            mImpl->ThinningCut = thinningCut;
            for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& prepared = preparedPlacements[placementIndex];
                const auto& selection = placementSelections[placementIndex];
                if (selection.Visible == 0U) continue;
                const bool clusterOwned = selection.ClusterOwned;
                const bool thinned = !clusterOwned && thinningCut != kGrassBudgetCutAll;
                const uint32_t environmentRecordIndex = placementIndex;
                for (size_t binIndex = 0U; binIndex < kGrassLodBinCount; ++binIndex) {
                    const uint32_t firstInstance = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
                    for (size_t outputIndex = 0U; outputIndex < selection.OutputCount; ++outputIndex) {
                        const auto& bin = selection.Outputs[outputIndex].Bins[binIndex];
                        if (!thinned) {
                            mImpl->VisibleAnchorIndices.insert(mImpl->VisibleAnchorIndices.end(), bin.begin(),
                                                               bin.end());
                            continue;
                        }
                        const auto& buckets = selection.Outputs[outputIndex].Buckets[binIndex];
                        for (size_t i = 0U; i < bin.size(); ++i)
                            if (GrassBudgetAdmits(buckets[i], thinningCut)) mImpl->VisibleAnchorIndices.push_back(bin[i]);
                    }
                    const uint32_t binInstanceCount =
                        static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size()) - firstInstance;
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
                            selection.ClusterRanges[binIndex], binInstanceCount, firstInstance) : PackGrassMidrangeDraws(
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
            mImpl->LastBlades = static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
            telemetry.VisibleBlades = mImpl->LastBlades;
            mImpl->SelectionCache.Store(std::move(selectionKey));
            if (GrassStageProfileEnabled()) {
                GrassStageCurrent().SelectEvaluated += telemetry.EvaluatedAnchors;
                GrassStageCurrent().SelectVisible += telemetry.VisibleBlades;
            }
        }
        std::vector<GrassGpuSelectionSource> gpuSources;
        std::vector<GrassGpuSelectionSlot> gpuSlots;
        GrassGpuSelectionReadback gpuReadback;
        if (gpuSelection) {
            gpuReadback = mImpl->GpuSelector.ReadBack(frameSlot);
            const size_t verifySlot = frameSlot % mImpl->VerifyCpuSelection.size();
            if (GrassGpuSelectionVerifyEnabled()) {
                if (gpuReadback.Available && mImpl->VerifyCpuValid[verifySlot]) {
                    const auto& cpu = mImpl->VerifyCpuSelection[verifySlot];
                    const auto& gpu = gpuReadback.Visible;
                    // Roots only one side selected (a root at a threshold moves
                    // every later position, so positions alone overstate it).
                    auto cpuSorted = cpu, gpuSorted = gpu;
                    std::sort(cpuSorted.begin(), cpuSorted.end());
                    std::sort(gpuSorted.begin(), gpuSorted.end());
                    std::vector<uint32_t> onlyCpu, onlyGpu;
                    std::set_difference(cpuSorted.begin(), cpuSorted.end(), gpuSorted.begin(), gpuSorted.end(),
                                        std::back_inserter(onlyCpu));
                    std::set_difference(gpuSorted.begin(), gpuSorted.end(), cpuSorted.begin(), cpuSorted.end(),
                                        std::back_inserter(onlyGpu));
                    size_t differing = onlyCpu.size() + onlyGpu.size();
                    size_t first = 0U;
                    while (differing == 0U && first < cpu.size() && first < gpu.size() && cpu[first] == gpu[first]) ++first;
                    // Same roots in a different order is a layout error, not a threshold.
                    if (differing == 0U && (first != cpu.size() || cpu.size() != gpu.size())) differing = 1U;
                    if (!onlyCpu.empty() || !onlyGpu.empty())
                        std::fprintf(stderr, "[grass-gpu-selection-roots] frame=%llu only_cpu=%zu only_gpu=%zu "
                            "cpu_first=%u gpu_first=%u\n", static_cast<unsigned long long>(frameId), onlyCpu.size(),
                            onlyGpu.size(), onlyCpu.empty() ? 0U : onlyCpu.front(), onlyGpu.empty() ? 0U : onlyGpu.front());
                    ++mImpl->GpuVerifyFrames;
                    if (differing != 0U) {
                        ++mImpl->GpuVerifyMismatchFrames;
                        mImpl->GpuVerifyMismatchRoots += differing;
                    }
                    if (differing != 0U || frameId % 60 == 0)
                        std::fprintf(stderr, "[grass-gpu-selection] frame=%llu cpu=%zu gpu=%zu differing=%zu first=%zu "
                            "frames=%llu mismatch_frames=%llu mismatch_roots=%llu\n",
                            static_cast<unsigned long long>(frameId), cpu.size(), gpu.size(), differing,
                            differing != 0U ? first : size_t(0),
                            static_cast<unsigned long long>(mImpl->GpuVerifyFrames),
                            static_cast<unsigned long long>(mImpl->GpuVerifyMismatchFrames),
                            static_cast<unsigned long long>(mImpl->GpuVerifyMismatchRoots));
                }
                mImpl->VerifyCpuSelection[verifySlot] = mImpl->VisibleAnchorIndices;
                mImpl->VerifyCpuValid[verifySlot] = true;
            }
            gpuSlots = BuildGrassGpuSelectionSlots(lodPolicy, mImpl->IndexedTopology);
            batches.clear();
            mImpl->DrawGroups.clear();
            for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                const auto& prepared = preparedPlacements[placementIndex];
                if (residency)
                    gpuSources.push_back({residency->Clusters[placementIndex], residency->SourceBase[placementIndex],
                                          prepared.Push.PositionToClip, prepared.Eye});
                else
                    gpuSources.push_back({prepared.World->Clusters, prepared.StaticBaseIndex,
                                          prepared.Push.PositionToClip, prepared.Eye});
                for (uint32_t k = 0; k < gpuSlots.size(); ++k) {
                    const auto bin = gpuSlots[k].Bin;
                    const bool tuft = bin == kGrassLodBinCount - 1U;
                    const auto segments = static_cast<uint8_t>(tuft ? 0U : bin / 2U + 1U);
                    const auto planes = static_cast<uint8_t>(bin % 2U + 1U);
                    auto push = prepared.Push;
                    push.Flags[1] = segments;
                    push.Flags[2] = planes;
                    push.Flags[3] = placementIndex;
                    GrassDrawBatch batch{0U, 0U, segments, planes, push, placementIndex};
                    batch.Indirect = true;
                    batch.IndirectOffset = GrassGpuSelectionDrawOffset(placementIndex, k);
                    batches.push_back(batch);
                }
            }
            if (gpuReadback.Available) {
                mImpl->LastBlades = gpuReadback.Admitted;
                telemetry.VisibleBlades = gpuReadback.Admitted;
                mImpl->ThinningCut = gpuReadback.Cut;
            }
        }
        selectionTimer.Stop();
        if (GrassStageProfileEnabled() && GrassSelectionHashEnabled()) {
            // Identity of the selection for A/B runs: roots, order and batches.
            GrassStageTimer diagnosticsTimer(GrassStage::Diagnostics);
            uint64_t hash = kGrassIdentityFnvOffset;
            const auto mix = [&hash](uint64_t value) { hash = (hash ^ value) * kGrassIdentityFnvPrime; };
            for (const uint32_t index : mImpl->VisibleAnchorIndices) mix(index);
            for (const auto& batch : batches)
                mix((static_cast<uint64_t>(batch.FirstInstance) << 32U) ^ batch.InstanceCount ^
                    (static_cast<uint64_t>(batch.BladeSegments) << 24U) ^ (static_cast<uint64_t>(batch.PlaneCount) << 16U));
            GrassStageCurrent().SelectHash ^= hash;
        }

        // With the GPU selection the count is only known on the GPU: buffers and
        // descriptor ranges use the budget.
        const uint32_t instanceCount = gpuSelection ? visibleBudget
                                                    : static_cast<uint32_t>(mImpl->VisibleAnchorIndices.size());
        if (GrassDiagnosticsEnabled() && frameId % 60 == 0) {
            GrassStageTimer diagnosticsTimer(GrassStage::Diagnostics);
            size_t contacts = 0, samples = 0, boxPairs = 0;
            float maxBend = 0;
            const size_t step = std::max<size_t>(1, mImpl->VisibleAnchorIndices.size() / 4096);
            for (size_t i = 0; i < mImpl->VisibleAnchorIndices.size(); i += step) {
                const auto& anchor = mImpl->StaticAnchor(mImpl->VisibleAnchorIndices[i]);
                ++samples;
                for (const auto& actor : actors) {
                    // The compactor's swept-box rejection: pairs that pass it
                    // pay the projection, roots and normalization.
                    const float radius = std::max(0.01F, actor.Radius * settings.ColliderRadiusMultiplier);
                    boxPairs += anchor.BaseHeight[0] >= std::min(actor.PreviousPosition[0], actor.Position[0]) - radius &&
                        anchor.BaseHeight[0] <= std::max(actor.PreviousPosition[0], actor.Position[0]) + radius &&
                        anchor.BaseHeight[2] >= std::min(actor.PreviousPosition[2], actor.Position[2]) - radius &&
                        anchor.BaseHeight[2] <= std::max(actor.PreviousPosition[2], actor.Position[2]) + radius;
                    const auto collision = ResolveGrassCollision(
                        {anchor.BaseHeight[0], anchor.BaseHeight[1], anchor.BaseHeight[2]},
                        anchor.BaseHeight[3], actor, settings);
                    if (collision.Weight > 0) {
                        ++contacts;
                        maxBend = std::max(maxBend, std::hypot(collision.Bend[0], collision.Bend[1]));
                    }
                }
            }
            std::fprintf(stderr, "[grass-interaction] frame=%llu actors=%zu sampled_roots=%zu sampled_box_pairs=%zu "
                "sampled_contacts=%zu max_bend=%.4f\n",
                static_cast<unsigned long long>(frameId), actors.size(), samples, boxPairs, contacts, maxBend);
        }
        // The cards draw without blades (a view of distant grass only, or a
        // residency store with no anchor near the eye): the environment,
        // lighting and shadow grid still prepare, the blade buffers,
        // compaction and descriptors do not.
        const bool residentBlades = residency ? !residency->Anchors.empty() : mImpl->StaticAnchorCount != 0U;
        const bool drawBlades = instanceCount != 0U && !batches.empty() && residentBlades;
        const bool drawCards = cardsEnabled && !mImpl->CardStream.Records.empty() &&
                               mImpl->CardRanges.size() == preparedPlacements.size();
        if (!drawBlades && !drawCards) {
            return finish(GrassRenderStatus::Culled, false);
        }
        if (!drawBlades) {
            batches.clear();
            mImpl->DrawGroups.clear();
        }
        GrassGpuSelectionRequest selectionRequest;
        if (gpuSelection) {
            selectionRequest.Sources = gpuSources;
            selectionRequest.Slots = gpuSlots;
            selectionRequest.Policy = lodPolicy;
            selectionRequest.DrawDistanceSquared = drawDistanceSquared;
            selectionRequest.BladeRadiusScale = bladeRadiusScale;
            selectionRequest.FrustumCulling = settings.FrustumCulling;
            selectionRequest.Budget = visibleBudget;
            selectionRequest.ReadBackVisible = GrassGpuSelectionVerifyEnabled();
        }

        const auto uploadStart = std::chrono::steady_clock::now();
        GrassStageTimer compactionTimer(GrassStage::Compaction);
        if (profiler)
            profiler->BeginScope(frameSlot, GpuProfileScope::GrassCompaction, commandBuffer);
        GrassGpuInstanceCompactionResult compaction;
        bool gpuCompacted = false;
        const std::array<std::span<const GrassWorldAnchor>, 1> residentSegment{
            residency ? std::span<const GrassWorldAnchor>(residency->Anchors) : std::span<const GrassWorldAnchor>{}};
        const auto uploadedAnchors = residency
            ? GrassStaticAnchorSegments{residentSegment, residency->Anchors.size()}
            : GrassStaticAnchorSegments{mImpl->StaticSegments, mImpl->StaticAnchorCount};
        const uint64_t uploadedRevision = residency ? mImpl->ResidencyUploadRevision : mImpl->StaticRevision;
        if (drawBlades && mImpl->GpuCompactorAvailable && !mImpl->GpuSuspendedForMemory) {
            gpuCompacted = mImpl->InstanceCompactor.Compact(
                {
                    commandBuffer,
                    frameSlot,
                    frameId,
                    uploadedRevision,
                    uploadedAnchors,
                    std::span<const uint32_t>(mImpl->VisibleAnchorIndices),
                    &mImpl->InteractionField,
                    actors,
                    &settings,
                    mImpl->SelectionRevision,
                    !temporalMotion,
                    gpuSelection ? &mImpl->GpuSelector : nullptr,
                    selectionRequest,
                },
                compaction);
            if (!gpuCompacted || compaction.StaticAnchorBuffer == VK_NULL_HANDLE ||
                compaction.RootBendBuffer == VK_NULL_HANDLE || compaction.VisibleIndicesBuffer == VK_NULL_HANDLE ||
                compaction.InstanceCount != instanceCount) {
                // Memory comes back; a functional failure does not.
                if (!gpuCompacted && mImpl->InstanceCompactor.LastFailureOutOfMemory()) {
                    AuditGrassError(mImpl->InstanceCompactor.UnavailableReason());
                    mImpl->OnOutOfMemory(frameId);
                } else {
                    mImpl->GpuCompactorAvailable = false;
                }
                gpuCompacted = false;
            }
        }
        if (drawBlades && gpuSelection && !gpuCompacted) {
            // No CPU selection to fall back on in this frame; the next frame
            // selects on the CPU.
            if (!mImpl->GpuSuspendedForMemory) {
                std::fprintf(stderr, "[grass] GPU selection disabled: %s\n",
                    mImpl->InstanceCompactor.UnavailableReason().c_str());
                mImpl->GpuSelectorAvailable = false;
            }
            return finish(GrassRenderStatus::Culled, false);
        }
        mImpl->PreparedSelectionControl = gpuSelection ? compaction.SelectionControl : VK_NULL_HANDLE;

        uint64_t dynamicUploadedBytes = 0U;
        uint64_t staticUploadedBytes = 0U;
        // Root i draws anchor rootIndices[i] of rootAnchors, bent by rootBends[i].
        const VkDeviceSize rootIndexBytes = VkDeviceSize(instanceCount) * sizeof(uint32_t);
        const VkDeviceSize rootBendBytes = VkDeviceSize(instanceCount) * kGrassRootBendWords * sizeof(uint32_t);
        VkDescriptorBufferInfo rootAnchors{}, rootBends{}, rootIndices{};
        if (gpuCompacted) {
            rootAnchors = {compaction.StaticAnchorBuffer, 0, VK_WHOLE_SIZE};
            rootBends = {compaction.RootBendBuffer, 0, rootBendBytes};
            rootIndices = {compaction.VisibleIndicesBuffer, 0, rootIndexBytes};
            dynamicUploadedBytes = compaction.DynamicUploadedBytes;
            staticUploadedBytes = compaction.StaticUploadedBytes;
        } else if (drawBlades) {
            const VkDeviceSize anchorBytes = VkDeviceSize(instanceCount) * sizeof(GrassWorldAnchor);
            auto& anchorBuffer = mImpl->CpuRootAnchors[slotIndex];
            auto& bendBuffer = mImpl->CpuRootBends[slotIndex];
            auto& indexBuffer = mImpl->CpuRootIndices[slotIndex];
            mImpl->EnsureBuffer(anchorBuffer, anchorBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            mImpl->EnsureBuffer(bendBuffer, rootBendBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            mImpl->EnsureBuffer(indexBuffer, rootIndexBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto* anchorOutput = static_cast<GrassWorldAnchor*>(anchorBuffer.Mapped);
            auto* bendOutput = static_cast<std::array<uint32_t, kGrassRootBendWords>*>(bendBuffer.Mapped);
            auto* indexOutput = static_cast<uint32_t*>(indexBuffer.Mapped);
            for (size_t instanceIndex = 0U; instanceIndex < mImpl->VisibleAnchorIndices.size(); ++instanceIndex) {
                const uint32_t anchorIndex = mImpl->VisibleAnchorIndices[instanceIndex];
                if (anchorIndex >= mImpl->StaticAnchorCount) {
                    throw std::runtime_error("interactive grass visible anchor is invalid");
                }
                const auto& anchor = mImpl->StaticAnchor(anchorIndex);
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
                anchorOutput[instanceIndex] = anchor;
                // No bend history here: the blades reuse the current bend.
                bendOutput[instanceIndex] = {PackGrassBend(dynamicBend), PackGrassBend(dynamicBend)};
                indexOutput[instanceIndex] = static_cast<uint32_t>(instanceIndex);
            }
            rootAnchors = {anchorBuffer.Buffer, 0, anchorBytes};
            rootBends = {bendBuffer.Buffer, 0, rootBendBytes};
            rootIndices = {indexBuffer.Buffer, 0, rootIndexBytes};
            dynamicUploadedBytes = static_cast<uint64_t>(anchorBytes + rootBendBytes + rootIndexBytes);
        }
        mImpl->PreparedRootAnchors = rootAnchors.buffer;

        compactionTimer.Stop();
        if (profiler)
            profiler->EndScope(frameSlot, GpuProfileScope::GrassCompaction, commandBuffer);
        if (GrassStageProfileEnabled()) {
            GrassStageCurrent().SelectionUploadReuse += compaction.SelectionUploadReused;
            GrassStageCurrent().CompactionReuse += compaction.Reused;
        }
        if (gpuCompacted)
            for (auto& record : environmentRecords)
                if (record.MotionState[2] > 0.5F) record.MotionState[3] = 1.0F;
        const VkDeviceSize environmentBytes = environmentRecords.size() * sizeof(GrassEnvironmentRecord);
        auto& environmentBuffer = mImpl->EnvironmentBuffers[slotIndex];
        mImpl->EnsureBuffer(environmentBuffer, environmentBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(environmentBuffer.Mapped, environmentRecords.data(), static_cast<size_t>(environmentBytes));
        const VkDescriptorBufferInfo environmentBufferInfo{ environmentBuffer.Buffer, 0U, environmentBytes };
        // Disabled receivers reuse an already valid descriptor without a shadow
        // allocation, upload or dispatch. The shader never reads these bindings.
        const VkDescriptorBufferInfo unusedShadow{environmentBuffer.Buffer, 0, sizeof(GrassEnvironmentRecord)};
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
            // The references follow the uploaded anchors (one per root).
            const auto grid = mImpl->ShadowGrid.Execute(commandBuffer, frameSlot, frameId, uploadedRevision,
                mImpl->ShadowPoints,
                residency ? std::span<const GrassShadowGridReference>(residency->ShadowReferences)
                          : std::span<const GrassShadowGridReference>(mImpl->ShadowReferences),
                mImpl->ShadowSurfaces, focus, providerPlan, shadowHistory);
            if (profiler)
                profiler->EndScope(frameSlot, GpuProfileScope::GrassShadows, commandBuffer);
            shadowBuffers[0] = grid.Visibility;
            shadowBuffers[1] = grid.References;
            shadowBuffers[3] = grid.Patches;
            const VkDeviceSize visibleBytes = VkDeviceSize(instanceCount) * sizeof(uint32_t);
            staticUploadedBytes += grid.StaticUploadedBytes;
            dynamicUploadedBytes += grid.DynamicUploadedBytes;
            if (gpuCompacted) {
                shadowBuffers[2] = {compaction.VisibleIndicesBuffer, 0, visibleBytes};
            } else if (drawBlades) {
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
        const VkDescriptorImageInfo nativeLightingImage{surfaceLighting.Sampler(), surfaceLighting.View(frameSlot),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDeviceSize groupBytes = 0U;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (drawBlades) {
            descriptorSet = mImpl->AcquireDescriptorSet(slotIndex, frameId);
            std::array<VkWriteDescriptorSet, 4> shadowWrites{};
            for (uint32_t index = 0; index < shadowWrites.size(); ++index) {
                auto& write = shadowWrites[index];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSet; write.dstBinding = 4U + index;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.descriptorCount = 1;
                write.pBufferInfo = &shadowBuffers[index];
            }
            vkUpdateDescriptorSets(mImpl->Device, static_cast<uint32_t>(shadowWrites.size()), shadowWrites.data(), 0, nullptr);
            const VkWriteDescriptorSet environmentWrite{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0U, 0U, 1U,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      nullptr, &environmentBufferInfo,           nullptr
            };
            vkUpdateDescriptorSets(mImpl->Device, 1U, &environmentWrite, 0U, nullptr);
            VkWriteDescriptorSet lightingWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            lightingWrite.dstSet = descriptorSet; lightingWrite.dstBinding = 1;
            lightingWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lightingWrite.descriptorCount = 1;
            lightingWrite.pImageInfo = &nativeLightingImage;
            vkUpdateDescriptorSets(mImpl->Device, 1, &lightingWrite, 0, nullptr);
            auto& groupBuffer = mImpl->GroupBuffers[slotIndex];
            groupBytes = std::max<size_t>(1, mImpl->DrawGroups.size()) * sizeof(std::array<uint32_t,2>);
            mImpl->EnsureBuffer(groupBuffer, groupBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            if (!mImpl->DrawGroups.empty())
                std::memcpy(groupBuffer.Mapped, mImpl->DrawGroups.data(), mImpl->DrawGroups.size()*sizeof(std::array<uint32_t,2>));
            const VkDescriptorBufferInfo groups{groupBuffer.Buffer, 0, groupBytes};
            const std::array<std::pair<uint32_t, const VkDescriptorBufferInfo*>, 4> rootBuffers{{
                {2U, &rootAnchors}, {3U, &groups}, {8U, &rootBends}, {9U, &rootIndices}}};
            for (const auto& [binding, info] : rootBuffers) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = descriptorSet; write.dstBinding = binding;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.descriptorCount = 1;
                write.pBufferInfo = info;
                vkUpdateDescriptorSets(mImpl->Device, 1, &write, 0, nullptr);
            }
        }
        if (drawCards) {
            GrassStageTimer cardTimer(GrassStage::Cards);
            try {
                const auto cards = mImpl->Cards.Prepare(commandBuffer, frameSlot, frameId, atlasShape,
                    mImpl->CardStream.Records, mImpl->CardRecordsRevision,
                    {environmentBufferInfo, nativeLightingImage, shadowBuffers[0], shadowBuffers[3]});
                staticUploadedBytes += cards.StaticUploadedBytes;
                dynamicUploadedBytes += cards.DynamicUploadedBytes;
                // One batch per run of chunks in view per placement and
                // level, in level order so the nearer tiers resolve their
                // depth first.
                for (uint32_t level = 0; level < kGrassCardLevelCount; ++level)
                    for (uint32_t placementIndex = 0; placementIndex < preparedPlacements.size(); ++placementIndex) {
                        const auto& prepared = preparedPlacements[placementIndex];
                        const auto& range = mImpl->CardRanges[placementIndex][level];
                        if (range.Cards.Count == 0U) continue;
                        const GrassCardCullView cullView{prepared.Push.PositionToClip, prepared.FrustumRadiusScale,
                                                         prepared.Eye, atlasShape.HeightOverRadius[0],
                                                         settings.FrustumCulling};
                        mImpl->CardRuns.clear();
                        SelectGrassCardChunks(
                            std::span<const GrassCardChunk>(mImpl->CardStream.Chunks).subspan(range.Chunks.First,
                                                                                               range.Chunks.Count),
                            cardPolicy, level, cullView, mImpl->CardRuns);
                        for (const auto& run : mImpl->CardRuns) {
                            GrassCardDrawBatch batch;
                            batch.Push = prepared.Push;
                            batch.Push.Flags[1] = level;
                            batch.Push.Flags[2] = 0U;
                            batch.Push.Flags[3] = placementIndex;
                            batch.FirstCard = run.First;
                            batch.CardCount = run.Count;
                            mImpl->CardBatches.push_back(batch);
                            telemetry.Cards += run.Count;
                        }
                    }
                telemetry.CardDrawCalls = static_cast<uint32_t>(mImpl->CardBatches.size());
                telemetry.CardAtlasRebuilt = cards.AtlasRebuilt;
                mImpl->CardsPrepared = !mImpl->CardBatches.empty();
            } catch (const std::exception& exception) {
                // The cards are optional: their failure never costs the
                // blades of this frame, and the next frame draws the blades
                // to the configured distance.
                AuditGrassError(exception.what());
                mImpl->DisableCards(exception.what());
                telemetry.Cards = 0U;
                telemetry.CardDrawCalls = 0U;
                if (!drawBlades) return finish(GrassRenderStatus::Error, false);
            }
        }
        if (gpuSelection && gpuReadback.Available)
            for (uint32_t s = 0; s < preparedPlacements.size(); ++s)
                if (environmentRecords[s].MotionState[2] > 0.5F)
                    for (uint32_t k = 0; k < gpuSlots.size(); ++k) mImpl->LastMotionBlades += gpuReadback.Counts[s][k];
        for (const auto& batch : batches) {
            if (environmentRecords[batch.Push.Flags[3]].MotionState[2] <= 0.5F) continue;
            if (batch.Indirect) continue;
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
    } catch (const GrassGpuOutOfMemory& exception) {
        mImpl->SelectionCache.Reset();
        mImpl->CandidateCache.clear();
        mImpl->Reason = exception.what();
        AuditGrassError(mImpl->Reason);
        mImpl->OnOutOfMemory(frameId);
        return finish(GrassRenderStatus::Error, false);
    } catch (const std::exception& exception) {
        mImpl->SelectionCache.Reset();
        mImpl->CandidateCache.clear();
        mImpl->Reason = exception.what();
        AuditGrassError(mImpl->Reason);
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
        (mImpl->PreparedRootAnchors == VK_NULL_HANDLE && !mImpl->CardsPrepared) ||
        pipeline == VK_NULL_HANDLE ||
        commandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    if (mImpl->PreparedRootAnchors == VK_NULL_HANDLE) {
        DrawPreparedCards(commandBuffer, width, height, pipelineIndex);
        mImpl->Prepared = false;
        return true;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mImpl->PipelineLayout, 0U, 1U,
                            &mImpl->PreparedDescriptorSet, 0U, nullptr);
    vkCmdBindIndexBuffer(commandBuffer, mImpl->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
    const VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F };
    const VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(commandBuffer, 0U, 1U, &viewport);
    vkCmdSetScissor(commandBuffer, 0U, 1U, &scissor);
    VkPipeline bound = pipeline;
    const auto draw = [&](const GrassDrawBatch& batch, VkPipeline wanted, uint32_t passFlag) {
        if (wanted != bound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            bound = wanted;
        }
        GrassPushConstants push = batch.Push;
        push.Flags[1] |= passFlag;
        vkCmdPushConstants(commandBuffer, mImpl->PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(push), &push);
        const auto& range = batch.Grouped ? mImpl->IndexedTopology.Groups[batch.BladeSegments-1][batch.PlaneCount-1] : batch.BladeSegments == 0U ? mImpl->IndexedTopology.Tuft :
            mImpl->IndexedTopology.Blades[batch.BladeSegments - 1U][batch.PlaneCount - 1U];
        const uint32_t count = batch.Grouped ? range.Count / kGrassMidrangeClusterCapacity * batch.GroupDrawCapacity : range.Count;
        if (batch.Indirect)
            vkCmdDrawIndexedIndirect(commandBuffer, mImpl->PreparedSelectionControl, batch.IndirectOffset, 1U,
                                     sizeof(VkDrawIndexedIndirectCommand));
        else
            vkCmdDrawIndexed(commandBuffer, count, batch.InstanceCount, range.First, 0, batch.FirstInstance);
    };
    const auto rasterDepth = [&](const GrassDrawBatch& batch) {
        return rasterDepthPipeline != VK_NULL_HANDLE && GrassDepthIsRasterDepth(batch.Push);
    };
    // A discard anywhere in the program forfeits the early depth test, and
    // only fading blades and tufts need one. Raster-depth blade batches
    // therefore draw twice: first their wholly visible blades with the program
    // without discard, the batches with the most segments (the nearest) first,
    // so the early depth test rejects what they hide; then, in batch order,
    // their fading blades with the stipple, and every other batch. Segment
    // counts that only occur below the whole-blade distance have no fading
    // blades: the whole pass draws all of them and they skip the second.
    const VkPipeline wholePipeline = mImpl->Pipelines[pipelineIndex + 6U];
    const auto split = [&](const GrassDrawBatch& batch) {
        return wholePipeline != VK_NULL_HANDLE && batch.BladeSegments != 0U && rasterDepth(batch);
    };
    const auto mayFade = [&](const GrassDrawBatch& batch) {
        return batch.Grouped || ((mImpl->FadingSegments >> batch.BladeSegments) & 1U) != 0U;
    };
    for (auto batch = mImpl->Batches.rbegin(); batch != mImpl->Batches.rend(); ++batch)
        if (split(*batch)) draw(*batch, wholePipeline, mayFade(*batch) ? kGrassWholePassFlag : 0U);
    for (const auto& batch : mImpl->Batches) {
        const bool raster = rasterDepth(batch);
        if (GrassStageProfileEnabled()) {
            auto& f = GrassStageCurrent();
            ++(raster ? f.RasterDepthBatches : f.ExplicitDepthBatches);
            (raster ? f.RasterDepthInstances : f.ExplicitDepthInstances) += batch.InstanceCount;
        }
        if (split(batch) && !mayFade(batch)) continue;
        draw(batch, raster ? rasterDepthPipeline : pipeline, split(batch) ? kGrassFadingPassFlag : 0U);
    }
    DrawPreparedCards(commandBuffer, width, height, pipelineIndex);
    mImpl->Prepared = false;
    return true;
}

// The distant cards after the blades, in their own GPU scope when profiled.
void InteractiveGrassPass::DrawPreparedCards(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height,
                                             size_t pipelineIndex) {
    if (!mImpl->CardsPrepared) return;
    mImpl->CardsPrepared = false;
    if (mImpl->Profiler)
        mImpl->Profiler->BeginScope(mImpl->ProfilerSlot, GpuProfileScope::GrassCards, commandBuffer);
    mImpl->Cards.Draw(commandBuffer, width, height, pipelineIndex, mImpl->CardBatches);
    if (mImpl->Profiler)
        mImpl->Profiler->EndScope(mImpl->ProfilerSlot, GpuProfileScope::GrassCards, commandBuffer);
    if (GrassStageProfileEnabled()) {
        auto& f = GrassStageCurrent();
        f.CardBatches += mImpl->CardBatches.size();
        for (const auto& batch : mImpl->CardBatches) f.CardInstances += batch.CardCount;
    }
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
    mImpl->CandidateCache.clear();
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
    mImpl->PlacementSelections.clear();
    mImpl->GpuSelector.Shutdown();
    mImpl->GpuSelectorAvailable = false;
    mImpl->InstanceCompactor.Shutdown();
    mImpl->SurfaceLighting.Shutdown();
    mImpl->ShadowSurfaceLighting.Shutdown();
    mImpl->ShadowGrid.Shutdown();
    mImpl->Cards.Shutdown();
    mImpl->CardsAvailable = false;
    mImpl->CardsInitialized = false;
    mImpl->CardSets.clear();
    mImpl->ShadowPointBases.clear();
    mImpl->CardStream.Records.clear();
    mImpl->CardStream.Chunks.clear();
    mImpl->CardRanges.clear();
    mImpl->CardRuns.clear();
    mImpl->CardBatches.clear();
    mImpl->CardRecordsStaticRevision = 0U;
    mImpl->CardRecordsShadowGrid = false;
    mImpl->CardClusterSize = 0.0F;
    mImpl->CardsPrepared = false;
    mImpl->Profiler = nullptr;
    mImpl->GpuCompactorAvailable = false;
    if (mImpl->Device != VK_NULL_HANDLE) {
        mImpl->DestroyBuffer(mImpl->IndexBuffer);
        for (auto* slots : {&mImpl->CpuRootAnchors, &mImpl->CpuRootBends, &mImpl->CpuRootIndices})
            for (auto& buffer : *slots)
                mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->EnvironmentBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->GroupBuffers)
            mImpl->DestroyBuffer(buffer);
        for (auto& buffer : mImpl->ShadowVisibleIndices)
            mImpl->DestroyBuffer(buffer);
        DestroyGrassPipelineSet(mImpl->Device, mImpl->Pipelines);
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
    mImpl->CandidateCache.clear();
    mImpl->IndexedTopology = {};
    mImpl->EnvironmentRecords.clear();
    mImpl->PreparedPlacements.clear();
    mImpl->PlacementRequests.clear();
    mImpl->VisibleAnchorIndices.clear();
    mImpl->StaticSources.clear();
    mImpl->StaticBases.clear();
    mImpl->StaticSegments.clear();
    mImpl->StaticAnchorCount = 0U;
    mImpl->ShadowPoints.clear();
    mImpl->ShadowReferences.clear();
    mImpl->ShadowSurfaces.clear();
    mImpl->ActivePlacements.clear();
    mImpl->ActiveShadowGrid = false;
    mImpl->ClusterWork.clear();
    mImpl->SelectionRoots.clear();
    mImpl->SelectionOutputs.clear();
    mImpl->StaticRevision = 0U;
    mImpl->PreparedRootAnchors = VK_NULL_HANDLE;
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
