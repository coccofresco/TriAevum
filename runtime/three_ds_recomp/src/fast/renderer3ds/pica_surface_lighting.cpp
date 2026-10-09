#include "fast/renderer3ds/pica_surface_lighting.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fast::Renderer3ds {
namespace {

using Vec4 = std::array<float, 4>;

bool ReadUniform(std::span<const uint8_t> bytes, uint8_t slot, Vec4& value) noexcept {
    const size_t offset = static_cast<size_t>(slot) * sizeof(value);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    std::memcpy(value.data(), bytes.data() + offset, sizeof(value));
    return std::all_of(value.begin(), value.end(), [](float component) { return std::isfinite(component); });
}

Vec4 Cross(const Vec4& a, const Vec4& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0], 0.0F};
}

bool InvertViewLinear(const std::array<Vec4, 3>& view, std::array<Vec4, 3>& inverse) noexcept {
    const std::array<Vec4, 3> columns{Cross(view[1], view[2]), Cross(view[2], view[0]), Cross(view[0], view[1])};
    const float determinant = view[0][0] * columns[0][0] + view[0][1] * columns[0][1] + view[0][2] * columns[0][2];
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12F) return false;
    for (size_t row = 0; row < 3U; ++row)
        for (size_t column = 0; column < 3U; ++column)
            inverse[row][column] = columns[column][row] / determinant;
    return true;
}

} // namespace

std::optional<PicaSurfaceLightingView> BuildPicaSurfaceLightingView(
    const std::array<float, 16>& worldToView) noexcept {
    if (!std::all_of(worldToView.begin(), worldToView.end(),
                     [](float value) { return std::isfinite(value); })) return std::nullopt;
    PicaSurfaceLightingView result;
    result.WorldToView = worldToView;
    for (size_t row = 0; row < 3U; ++row)
        for (size_t column = 0; column < 4U; ++column)
            result.Rows[row][column] = worldToView[column * 4U + row];
    if (!InvertViewLinear(result.Rows, result.ViewToWorldLinear)) return std::nullopt;
    return result;
}

void AssignPicaSurfaceWater(PicaSurfaceLightingUniforms& uniforms, const PicaSurfaceLightingView& view,
                            const PicaSceneWaterBodies& bodies, const std::array<float, 3>& worldMinimum,
                            const std::array<float, 3>& worldMaximum, float seconds, bool wholeDraw,
                            const std::array<PicaWaterRing, kPicaWaterRings>* rings) noexcept {
    std::array<uint32_t, kMaximumPicaSceneWaterBodies> reached{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < std::min(bodies.Count, kMaximumPicaSceneWaterBodies); ++i) {
        if (PicaWaterSurfaceReaches(worldMinimum, worldMaximum, bodies.Bodies[i])) reached[count++] = i;
    }
    // More bodies than slots: the largest overlaps.
    const auto overlap = [&](uint32_t index) {
        const auto& body = bodies.Bodies[index];
        return (std::min(worldMaximum[0], body.MaximumXZ[0]) - std::max(worldMinimum[0], body.MinimumXZ[0])) *
               (std::min(worldMaximum[2], body.MaximumXZ[1]) - std::max(worldMinimum[2], body.MinimumXZ[1]));
    };
    std::stable_sort(reached.begin(), reached.begin() + count,
                     [&](uint32_t a, uint32_t b) { return overlap(a) > overlap(b); });
    count = std::min(count, kMaximumPicaSurfaceWaterBodies);
    for (uint32_t slot = 0; slot < count; ++slot) {
        const auto& body = bodies.Bodies[reached[slot]];
        uniforms.WaterFootprints[slot] = {body.MinimumXZ[0], body.MinimumXZ[1], body.MaximumXZ[0], body.MaximumXZ[1]};
        uniforms.WaterHeights[slot] = body.SurfaceY;
    }
    uniforms.WaterViewTranslation = {view.Rows[0][3], view.Rows[1][3], view.Rows[2][3],
                                     static_cast<float>(count + (wholeDraw ? 8U : 0U))};
    uniforms.WaterOptics = kPicaSurfaceWaterOptics;
    uniforms.WaterRipples = {std::fmod(std::max(seconds, 0.0F), 3600.0F), kPicaSurfaceWaterRipples[0],
                             kPicaSurfaceWaterRipples[1], -1.0e9F};
    if (rings != nullptr)
        for (uint32_t i = 0; i < kPicaWaterRings; ++i)
            uniforms.WaterRings[i] = {(*rings)[i].CenterXZ[0], (*rings)[i].CenterXZ[1], (*rings)[i].Birth,
                                      (*rings)[i].Strength};
}

std::optional<PicaSurfaceLightingUniforms> BuildPicaSurfaceLightingUniforms(
    const PicaVertexShaderHookLayout& hooks, uint32_t booleanUniforms,
    std::span<const uint8_t> floatUniformBytes,
    const PicaNativeLightBinding& binding,
    const PicaEnvironmentLighting& environment,
    const PicaSurfaceLightingView* cameraView,
    const PicaPointShadowAssignment* pointShadows,
    bool nativeFragmentLambert) noexcept {
    const auto pointShadowRow = [pointShadows](uint64_t identity) {
        if (pointShadows == nullptr || identity == 0U) return -1.0F;
        for (size_t row = 0; row < pointShadows->Identities.size(); ++row)
            if (pointShadows->Identities[row] == identity) return static_cast<float>(row);
        return -1.0F;
    };
    const auto* surface = hooks.SurfaceProgram();
    const auto* transform = hooks.TransformProgram();
    if (surface == nullptr || transform == nullptr) return std::nullopt;
    PicaSurfaceLightingUniforms result;
    if (!ReadUniform(floatUniformBytes, surface->DiffuseUniform, result.MaterialDiffuse) ||
        !ReadUniform(floatUniformBytes, surface->AmbientUniform, result.MaterialAmbient)) return std::nullopt;
    result.Control[0] = (booleanUniforms >> surface->LightingBooleanUniform) & 1U;
    // A vertex-lighting block the rows cannot hold keeps a lit draw native.
    if (!surface->VertexLightsReconstructed && result.Control[0] != 0U) return std::nullopt;
    Vec4 enabledValue{};
    if (surface->VertexLightsReconstructed &&
        !ReadUniform(floatUniformBytes, surface->LightEnabledValue.Uniform, enabledValue)) return std::nullopt;
    for (size_t slot = 0; surface->VertexLightsReconstructed && slot < result.Lights.size(); ++slot) {
        auto& light = result.Lights[slot];
        const uint8_t first = surface->LightDirectionUniforms[slot];
        Vec4 ambient{};
        if (!ReadUniform(floatUniformBytes, first, light.DirectionEnabled) ||
            !ReadUniform(floatUniformBytes, first + 1U, light.Diffuse) ||
            !ReadUniform(floatUniformBytes, first + 2U, ambient)) return std::nullopt;
        light.DirectionEnabled[3] = light.DirectionEnabled[3] == enabledValue[surface->LightEnabledValue.Component] ? 1.0F : 0.0F;
        for (size_t component = 0; component < 3U; ++component) light.DirectionEnabled[component] = -light.DirectionEnabled[component];
        if (result.Control[0] != 0U && light.DirectionEnabled[3] != 0.0F)
            for (size_t channel = 0; channel < 3; ++channel)
                result.ResolvedAmbient[channel] += ambient[channel] * result.MaterialAmbient[channel];
    }
    std::span<const PicaScenePointLight> scenePoints;
    if (binding.ScenePoints) {
        if (binding.ScenePoints->Count > binding.ScenePoints->Lights.size()) return std::nullopt;
        scenePoints = {binding.ScenePoints->Lights.data(), binding.ScenePoints->Count};
    }
    std::span<const PicaSceneDirectionalLight> sceneDirectionals;
    if (binding.SceneDirectionals) {
        if (binding.SceneDirectionals->Count > binding.SceneDirectionals->Lights.size()) return std::nullopt;
        sceneDirectionals = {binding.SceneDirectionals->Lights.data(), binding.SceneDirectionals->Count};
    }
    if (environment.PlacedLightCount > environment.PlacedLights.size()) return std::nullopt;
    const bool needsWorldView = environment.Enabled || !scenePoints.empty() || !sceneDirectionals.empty() ||
        std::any_of(binding.Sources.begin(), binding.Sources.end(), [](const auto& light) {
            return light.Kind == PicaNativeLightSource::Type::Point;
        });
    if (needsWorldView && cameraView == nullptr) return std::nullopt;
    std::array<Vec4, 3> localView{};
    const auto& view = cameraView != nullptr ? cameraView->Rows : localView;
    if (cameraView != nullptr) {
        result.ViewToWorldLinear = cameraView->ViewToWorldLinear;
    } else {
        for (uint8_t row = 0; row < 3U; ++row)
            if (!ReadUniform(floatUniformBytes, transform->ViewFirstUniform + row, localView[row])) return std::nullopt;
        if (!InvertViewLinear(localView, result.ViewToWorldLinear)) return std::nullopt;
    }
    for (size_t slot = 0; slot < result.Lights.size(); ++slot) {
        auto& light = result.Lights[slot];
        const auto& source = binding.Sources[slot];
        const bool pointSource = source.Kind == PicaNativeLightSource::Type::Point && source.Radius > 0.0F;
        if (result.Control[0] == 0U || light.DirectionEnabled[3] == 0.0F ||
            (environment.Enabled && environment.NativeLocalStrength <= 0.0F)) {
            light = {};
            continue;
        }
        if (source.SourceIdentity != 0 &&
            (std::any_of(scenePoints.begin(), scenePoints.end(),
                [&](const auto& point) { return point.SourceIdentity == source.SourceIdentity; }) ||
             std::any_of(sceneDirectionals.begin(), sceneDirectionals.end(),
                [&](const auto& directional) { return directional.SourceIdentity == source.SourceIdentity; }))) {
            // Remove only the duplicated direct source. Native slot zero may
            // also carry LightContext ambient, which does not belong to it.
            light = {};
            continue;
        }
        if (!pointSource) continue;
        light.PositionRadius[3] = source.Radius;
        light.Diffuse[3] = pointShadowRow(source.SourceIdentity);
        for (size_t component = 0; component < 3U; ++component) {
            light.PositionRadius[component] = view[component][3];
            for (size_t axis = 0; axis < 3U; ++axis)
                light.PositionRadius[component] += view[component][axis] * source.PositionWorld[axis];
            // The selected native record contains attenuation at the actor's
            // reference point. Remove that attenuation, not material/color gain.
            const float gain = source.ReferenceColor[component] > 0.0F
                ? light.Diffuse[component] / source.ReferenceColor[component] : 1.0F;
            light.Diffuse[component] = source.UnattenuatedColor[component] * gain;
        }
    }
    const bool hasDiffuse = std::any_of(result.MaterialDiffuse.begin(),
        result.MaterialDiffuse.begin() + 3, [](float value) { return value != 0.0F; });
    // Fragment-lit Lambert leaves native primary color (material or vertex
    // color) in the TEV. Feed irradiance to its fragment-lighting operand;
    // folding material here as well would square its response.
    const bool nativePrimary = nativeFragmentLambert && result.Control[0] == 0U;
    const Vec4 unitReflectance{1.0F, 1.0F, 1.0F, 1.0F};
    const auto& reflectance = nativePrimary ? unitReflectance :
        hasDiffuse ? result.MaterialDiffuse : result.MaterialAmbient;
    if (nativePrimary) result.Control[3] |= 2U;
    const float sceneDirectStrength = environment.Enabled ? environment.NativeDirectStrength : 1.0F;
    if (sceneDirectStrength > 0.0F) {
        for (const auto& source : sceneDirectionals) {
            const uint32_t slot = result.Control[2]++;
            // A sky light below the horizon lights nothing directly.
            const float horizon = PicaSceneDirectHorizon(source, binding.SceneDirectionals->WorldUp);
            for (size_t row = 0; row < 3; ++row) {
                for (size_t axis = 0; axis < 3; ++axis)
                    result.SceneDirections[slot][row] += view[row][axis] * source.DirectionTowardSource[axis];
                result.SceneDirectionalColors[slot][row] =
                    source.Color[row] * sceneDirectStrength * horizon * reflectance[row];
            }
        }
    }
    for (const auto& source : scenePoints) {
        if (source.Group >= environment.PointLights.size()) return std::nullopt;
        const auto& settings = environment.PointLights[source.Group];
        if (!settings.Enabled || settings.Intensity <= 0 || source.Radius <= 0) continue;
        auto& point = result.ScenePoints[result.Control[1]++];
        point.PositionRadius[3] = source.Radius * settings.RadiusScale;
        point.Color[3] = pointShadowRow(source.SourceIdentity);
        for (size_t row = 0; row < 3; ++row) {
            point.PositionRadius[row] = view[row][3];
            for (size_t axis = 0; axis < 3; ++axis)
                point.PositionRadius[row] += view[row][axis] * source.PositionWorld[axis];
            point.Color[row] = source.Color[row] * settings.ColorTint[row] *
                settings.Intensity * reflectance[row];
        }
    }
    // Placed lights, in their own slots.
    uint32_t placedCount = 0;
    for (uint32_t index = 0; environment.Enabled && index < environment.PlacedLightCount; ++index) {
        const auto& source = environment.PlacedLights[index];
        const bool sun = source.Kind == PicaPlacedLightKind::Sun;
        if (source.Kind > PicaPlacedLightKind::Sun || (!sun && source.Reach <= 0.0F)) continue;
        auto& placed = result.Placed[placedCount++];
        placed.PositionReach[3] = source.Reach;
        placed.Color[3] = source.CastShadows && !sun ? pointShadowRow(source.SourceIdentity) : -1.0F;
        placed.AxisKind = { source.AxisWorld[0], source.AxisWorld[1], source.AxisWorld[2],
                            static_cast<float>(source.Kind) + (source.TwoSided ? 8.0F : 0.0F) };
        if (source.Kind == PicaPlacedLightKind::Spot)
            placed.TangentShape = { source.CosOuter, source.CosInner, 0.0F, 0.0F };
        else
            placed.TangentShape = { source.TangentWorld[0] * source.HalfWidth, source.TangentWorld[1] * source.HalfWidth,
                                    source.TangentWorld[2] * source.HalfWidth, source.HalfHeight };
        for (size_t row = 0; row < 3; ++row) {
            placed.PositionReach[row] = view[row][3];
            for (size_t axis = 0; axis < 3; ++axis)
                placed.PositionReach[row] += view[row][axis] * source.PositionWorld[axis];
            placed.Color[row] = source.Color[row] * reflectance[row];
        }
    }
    result.Control[3] |= placedCount << 8U;
    const float ambientGain = !environment.Enabled || environment.PreserveSceneBrightness
        ? 1.0F : environment.NativeAmbientStrength;
    result.EnvironmentParameters[1] = ambientGain;
    if (environment.Enabled) {
        result.EnvironmentParameters = {environment.NativeLocalStrength, ambientGain,
                                        environment.FogStrength, 1.0F};
        const bool preserveReference = environment.PreserveSceneBrightness;
        result.Control[3] = (result.Control[3] & ~1U) | (preserveReference ? 1U : 0U);
        // The profile replaces illumination, not material reflectance. Native
        // TEV gains still apply, so dropping these coefficients overexposes
        // materials authored with reduced diffuse/ambient response.
        // Ambient-only materials use their authored ambient reflectance for
        // the new direct lights rather than acquiring an arbitrary white one.
        for (size_t channel = 0; channel < 3; ++channel)
            result.EnvironmentAmbient[channel] = environment.AmbientColor[channel] *
                environment.AmbientIntensity * (nativePrimary ? 1.0F : result.MaterialAmbient[channel]);
        for (size_t slot = 0; slot < environment.Lights.size(); ++slot) {
            const auto& source = environment.Lights[slot];
            if (source.Intensity <= 0.0F) continue;
            const auto direction = slot == 0
                ? ResolvePicaEnvironmentKeyDirection(environment, binding.SceneDirectionals.get())
                : source.DirectionTowardSource;
            const auto colour = slot == 0
                ? ResolvePicaEnvironmentKeyColor(environment, binding.SceneDirectionals.get())
                : source.Color;
            result.EnvironmentDirections[slot][3] = source.Intensity;
            for (size_t row = 0; row < 3; ++row) {
                for (size_t axis = 0; axis < 3; ++axis)
                    result.EnvironmentDirections[slot][row] += view[row][axis] * direction[axis];
                result.EnvironmentColors[slot][row] = colour[row] * reflectance[row];
            }
        }
    }
    for (size_t channel = 0; channel < 3; ++channel)
        result.ResolvedAmbient[channel] = result.ResolvedAmbient[channel] * ambientGain +
                                          result.EnvironmentAmbient[channel];
    if (pointShadows != nullptr) {
        result.PointShadow = pointShadows->Parameters;
        result.PointShadowAtlas = pointShadows->Atlas;
    }
    return result;
}

void FoldPicaSurfaceMaterialForPbr(PicaSurfaceLightingUniforms& u) noexcept {
    if ((u.Control[3] & 2U) == 0U) return;
    const bool hasDiffuse = std::any_of(u.MaterialDiffuse.begin(), u.MaterialDiffuse.begin() + 3,
        [](float value) { return value != 0.0F; });
    const auto& reflectance = hasDiffuse ? u.MaterialDiffuse : u.MaterialAmbient;
    const auto fold = [&](auto& color) {
        for (size_t channel = 0; channel < 3; ++channel) color[channel] *= reflectance[channel];
    };
    for (uint32_t n = 0; n < u.Control[1]; ++n) fold(u.ScenePoints[n].Color);
    for (uint32_t n = 0; n < u.Control[2]; ++n) fold(u.SceneDirectionalColors[n]);
    for (uint32_t n = 0; n < ((u.Control[3] >> 8U) & 255U); ++n) fold(u.Placed[n].Color);
    for (auto& color : u.EnvironmentColors) fold(color);
    for (size_t channel = 0; channel < 3; ++channel) {
        u.EnvironmentAmbient[channel] *= u.MaterialAmbient[channel];
        u.ResolvedAmbient[channel] *= u.MaterialAmbient[channel];
    }
    u.Control[3] &= ~2U;
}

uint32_t ExcludePicaSceneDirectionalsLitNatively(
    PicaSurfaceLightingUniforms& uniforms,
    std::span<const std::array<float, 3>> nativeDirectionalViewDirections) noexcept {
    // The same direction within the native light vector's half-float
    // precision (about 5 degrees).
    constexpr float kSameDirection = 0.995F;
    const auto unit = [](std::array<float, 3> value) {
        const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        if (!std::isfinite(length) || length <= 1.0e-6F) return std::array<float, 3>{};
        for (float& component : value) component /= length;
        return value;
    };
    uint32_t kept = 0, removed = 0;
    const uint32_t count = std::min<uint32_t>(uniforms.Control[2], kMaximumPicaSceneDirectionalLights);
    for (uint32_t slot = 0; slot < count; ++slot) {
        const auto scene = unit({uniforms.SceneDirections[slot][0], uniforms.SceneDirections[slot][1],
                                 uniforms.SceneDirections[slot][2]});
        const bool native = std::any_of(nativeDirectionalViewDirections.begin(),
            nativeDirectionalViewDirections.end(), [&](const auto& direction) {
                const auto light = unit(direction);
                return scene[0] * light[0] + scene[1] * light[1] + scene[2] * light[2] >= kSameDirection;
            });
        if (native) {
            ++removed;
            continue;
        }
        uniforms.SceneDirections[kept] = uniforms.SceneDirections[slot];
        uniforms.SceneDirectionalColors[kept] = uniforms.SceneDirectionalColors[slot];
        ++kept;
    }
    for (uint32_t slot = kept; slot < count; ++slot) {
        uniforms.SceneDirections[slot] = {};
        uniforms.SceneDirectionalColors[slot] = {};
    }
    uniforms.Control[2] = kept;
    return removed;
}

std::string PicaSurfaceLightingDeclarations(bool fragmentOperand) {
    return std::string(R"glsl(
layout(location=9) in vec3 pica_surface_normal;
layout(location=10) in vec4 pica_surface_tint;
struct PicaSurfaceLight {
    vec4 direction_enabled;
    vec4 diffuse;
    vec4 position_radius;
};
struct PicaScenePointLight {
    vec4 position_radius;
    vec4 color;
};
struct PicaPlacedLight {
    vec4 position_reach;
    vec4 color;
    vec4 axis_kind;
    vec4 tangent_shape;
};
layout(set=0,binding=16,std140) uniform PicaSurfaceLighting {
    vec4 material_diffuse;
    vec4 material_ambient;
    vec4 resolved_ambient;
    uvec4 control;
    vec4 view_to_world_linear[3];
    PicaSurfaceLight lights[3];
    vec4 environment_parameters;
    vec4 environment_ambient;
    vec4 environment_directions[2];
    vec4 environment_colors[2];
    vec4 scene_directions[2];
    vec4 scene_directional_colors[2];
    PicaScenePointLight scene_points[32];
    vec4 gi_sources;
    vec4 point_shadow;
    vec4 point_shadow_atlas;
    PicaPlacedLight placed_lights[16];
    vec4 water_view_translation;
    vec4 water_footprints[4];
    vec4 water_heights;
    vec4 water_optics;
    vec4 water_ripples;
    vec4 water_flow;
    vec4 water_rings[16];
} pica_surface;
vec3 pica_surface_water_world = vec3(0.0);
float pica_surface_water_footprint = 0.0;
// The water face: its world normal (up on a body's plane) and tangent frame
// (world x and z on a horizontal plane), and whether it lies on a body's
// plane, where the reflection trace finds it.
vec3 pica_surface_water_up = vec3(0.0, 1.0, 0.0);
vec3 pica_surface_water_tangent = vec3(1.0, 0.0, 0.0);
vec3 pica_surface_water_bitangent = vec3(0.0, 0.0, 1.0);
bool pica_surface_water_traced = false;

// Flow (world units per second, in the face's tangent frame) of the water's
// texture pattern.
vec2 pica_surface_water_flow = vec2(0.0);
// The pattern's world motion: the scrolling texture coordinate moves the
// pattern the other way, mapped to the surface through the screen-space
// derivatives of the coordinate and of the view position.
vec3 pica_surface_water_flow_world(vec2 uv_dx, vec2 uv_dy, vec3 view_dx, vec3 view_dy) {
    if (pica_surface.water_flow.z <= 0.0) return vec3(0.0);
    vec3 world_dx = vec3(dot(pica_surface.view_to_world_linear[0].xyz, -view_dx),
                         dot(pica_surface.view_to_world_linear[1].xyz, -view_dx),
                         dot(pica_surface.view_to_world_linear[2].xyz, -view_dx));
    vec3 world_dy = vec3(dot(pica_surface.view_to_world_linear[0].xyz, -view_dy),
                         dot(pica_surface.view_to_world_linear[1].xyz, -view_dy),
                         dot(pica_surface.view_to_world_linear[2].xyz, -view_dy));
    mat2 texture_from_screen = mat2(uv_dx, uv_dy);
    if (abs(determinant(texture_from_screen)) < 1.0e-12) return vec3(0.0);
    vec3 flow = -(mat2x3(world_dx, world_dy) * (inverse(texture_from_screen) * pica_surface.water_flow.xy));
    float speed = length(flow);
    return speed > 400.0 ? flow * (400.0 / speed) : flow;
}
// Lying water whose texture flows (a river): its waves and streaks ride the
// texture, in its coordinates (continuous across faces whose mappings
// differ, as the native texture is), turned onto the surface by the face's
// mapping (uv per tangent-frame unit). Faces' own flows would part their
// patterns at every edge, further each second.
bool pica_surface_water_textured = false;
vec2 pica_surface_water_uv = vec2(0.0);
float pica_surface_water_uv_footprint = 0.0;
mat2 pica_surface_water_uv_per_unit = mat2(1.0);
// Flowing water (calm below 10 world units per second, fast above 80): the
// waves ride the current, shorter and steeper as it quickens.
float pica_surface_water_fast() {
    return smoothstep(10.0, 80.0, length(pica_surface_water_flow));
}
// A fragment of water (pica_scene_water.h): a horizontal face at a listed
// body's surface height, inside its footprint; or, in a draw that is all
// water (water_view_translation.w >= 8: a catalog water texture), any face,
// on its own plane (waterfalls, rapids). face: the view-space face normal
// (any length or sign).
bool pica_surface_water(vec3 face) {
    uint mode = uint(pica_surface.water_view_translation.w);
    uint count = mode & 7u;
    bool whole = mode >= 8u;
    if (count == 0u && !whole) return false;
    // Toward the camera (pica_view: from the fragment to the camera).
    if (dot(face, pica_view) < 0.0) face = -face;
    vec3 world_face = vec3(dot(pica_surface.view_to_world_linear[0].xyz, face),
                           dot(pica_surface.view_to_world_linear[1].xyz, face),
                           dot(pica_surface.view_to_world_linear[2].xyz, face));
    float face_length = length(world_face);
    if (face_length <= 0.0) return false;
    vec3 view_position = -pica_view - pica_surface.water_view_translation.xyz;
    pica_surface_water_world = vec3(dot(pica_surface.view_to_world_linear[0].xyz, view_position),
                                    dot(pica_surface.view_to_world_linear[1].xyz, view_position),
                                    dot(pica_surface.view_to_world_linear[2].xyz, view_position));
    vec3 world = pica_surface_water_world;
    // A whole water draw's lying faces (level, or a stream's slope; not its
    // falls) are the reflection trace's (its water map).
    if (whole && world_face.y > 0.75 * face_length) {
        pica_surface_water_traced = true;
        return true;
    }
    if (abs(world_face.y) >= 0.95 * face_length) {
        // A mixed mesh's level faces over a body.
        for (uint slot = 0u; slot < min(count, 4u); ++slot) {
            vec4 footprint = pica_surface.water_footprints[slot];
            if (abs(world.y - pica_surface.water_heights[slot]) <= 4.0 && world.x >= footprint.x &&
                world.z >= footprint.y && world.x <= footprint.z && world.z <= footprint.w) {
                pica_surface_water_traced = true;
                return true;
            }
        }
    }
    if (!whole) return false;
    vec3 up = world_face / face_length;
    // Lying water (a sloped stream, not a fall) shades as level water: its
    // triangles each tilt a little, and their own normals and wave frames
    // would show as facets and as waves changing scale and direction.
    if (abs(up.y) >= 0.75) up = vec3(0.0, sign(up.y), 0.0);
    pica_surface_water_up = up;
    pica_surface_water_tangent = abs(up.y) > 0.95 ? vec3(1.0, 0.0, 0.0)
                                                   : normalize(cross(vec3(0.0, 1.0, 0.0), up));
    pica_surface_water_bitangent = cross(pica_surface_water_tangent, up);
    return true;
}
vec3 pica_surface_unit(vec3 value) {
    float squared = dot(value, value);
    return squared > 1.0e-20 ? value * inversesqrt(squared) : vec3(0.0);
}
vec3 pica_surface_resolved_ambient(vec3 native_ambient) {
    return native_ambient * pica_surface.environment_parameters.y + pica_surface.environment_ambient.rgb;
}
vec3 pica_surface_shadow(int slot);
float pica_point_shadow(float slot, vec3 light_to_fragment, float radius);
void pica_surface_light(int slot, out vec3 direction, out vec3 radiance) {
    PicaSurfaceLight light = pica_surface.lights[slot];
    direction = light.direction_enabled.xyz;
    radiance = light.diffuse.rgb;
    if (light.position_radius.w > 0.0) {
        vec3 delta = light.position_radius.xyz + pica_view;
        vec3 world_delta = vec3(dot(pica_surface.view_to_world_linear[0].xyz, delta),
                                dot(pica_surface.view_to_world_linear[1].xyz, delta),
                                dot(pica_surface.view_to_world_linear[2].xyz, delta));
        float attenuation = max(0.0, 1.0 - dot(world_delta, world_delta) /
                                (light.position_radius.w * light.position_radius.w));
        direction = pica_surface_unit(delta);
        radiance *= attenuation * pica_point_shadow(light.diffuse.w, -world_delta, light.position_radius.w);
    }
}
void pica_surface_scene_point(int slot, out vec3 direction, out vec3 radiance) {
    PicaScenePointLight light = pica_surface.scene_points[slot];
    vec3 delta = light.position_radius.xyz + pica_view;
    vec3 world_delta = vec3(dot(pica_surface.view_to_world_linear[0].xyz, delta),
                            dot(pica_surface.view_to_world_linear[1].xyz, delta),
                            dot(pica_surface.view_to_world_linear[2].xyz, delta));
    float attenuation = max(0.0, 1.0 - dot(world_delta, world_delta) /
                            (light.position_radius.w * light.position_radius.w));
    direction = pica_surface_unit(delta);
    radiance = light.color.rgb * attenuation * pica_point_shadow(light.color.w, -world_delta, light.position_radius.w);
}
uint pica_surface_placed_count() {
    return (pica_surface.control.w >> 8u) & 0xFFu;
}
vec3 pica_surface_to_world(vec3 view_vector) {
    return vec3(dot(pica_surface.view_to_world_linear[0].xyz, view_vector),
                dot(pica_surface.view_to_world_linear[1].xyz, view_vector),
                dot(pica_surface.view_to_world_linear[2].xyz, view_vector));
}
// World to view direction: the transpose of the (rotation and uniform
// scale) view-to-world matrix, normalized.
vec3 pica_surface_view_direction(vec3 world_vector) {
    return pica_surface_unit(pica_surface.view_to_world_linear[0].xyz * world_vector.x +
                             pica_surface.view_to_world_linear[1].xyz * world_vector.y +
                             pica_surface.view_to_world_linear[2].xyz * world_vector.z);
}
// A smooth cut to zero at the reach.
float pica_surface_reach_window(float distance_world, float reach) {
    float ratio = distance_world / max(reach, 1.0e-3);
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window;
}
// Placed light kinds (PicaPlacedLightKind): 0 omni, 1 point, 2 spot,
// 3 rectangle, 4 disc, 5 sun; 8 is added for a two-sided surface.
void pica_surface_placed_light(int slot, out vec3 direction, out vec3 radiance) {
    PicaPlacedLight light = pica_surface.placed_lights[slot];
    bool two_sided = light.axis_kind.w >= 7.5;
    int kind = int(light.axis_kind.w - (two_sided ? 8.0 : 0.0) + 0.5);
    vec3 axis = light.axis_kind.xyz;
    radiance = light.color.rgb;
    if (kind == 5) {
        direction = pica_surface_view_direction(-axis);
        return;
    }
    float reach = light.position_reach.w;
    // World vector from the fragment to the light's position.
    vec3 to_light = pica_surface_to_world(light.position_reach.xyz + pica_view);
    float distance_world = length(to_light);
    if (kind == 3 || kind == 4) {
        // The fragment seen from the surface's centre.
        vec3 from_center = -to_light;
        float side = dot(from_center, axis);
        if (!two_sided && side <= 0.0) {
            direction = vec3(0.0, 0.0, 1.0);
            radiance = vec3(0.0);
            return;
        }
        vec3 closest;
        float area;
        if (kind == 3) {
            float half_width = length(light.tangent_shape.xyz);
            vec3 tangent = light.tangent_shape.xyz / max(half_width, 1.0e-6);
            vec3 bitangent = cross(axis, tangent);
            float half_height = light.tangent_shape.w;
            closest = tangent * clamp(dot(from_center, tangent), -half_width, half_width) +
                      bitangent * clamp(dot(from_center, bitangent), -half_height, half_height);
            area = 4.0 * half_width * half_height;
        } else {
            float radius = length(light.tangent_shape.xyz);
            vec3 planar = from_center - axis * side;
            float planar_length = length(planar);
            closest = planar_length > radius ? planar * (radius / planar_length) : planar;
            area = 3.14159265 * radius * radius;
        }
        // From the fragment to the closest point of the surface.
        vec3 to_surface = closest - from_center;
        float cos_light = abs(side) / max(distance_world, 1.0e-3);
        float form = area * cos_light / (3.14159265 * dot(to_surface, to_surface) + area);
        direction = pica_surface_view_direction(to_surface);
        radiance *= form * pica_surface_reach_window(distance_world, reach);
    } else {
        direction = pica_surface_view_direction(to_light);
        float attenuation;
        if (kind == 0) {
            attenuation = max(0.0, 1.0 - distance_world * distance_world / (reach * reach));
        } else {
            float reference = 100.0;
            attenuation = reference * reference / max(distance_world * distance_world, 100.0) *
                          pica_surface_reach_window(distance_world, reach);
            if (kind == 2) {
                float beam = dot(-to_light / max(distance_world, 1.0e-3), axis);
                attenuation *= smoothstep(light.tangent_shape.x, max(light.tangent_shape.y,
                                          light.tangent_shape.x + 1.0e-4), beam);
            }
        }
        radiance *= attenuation;
    }
    radiance *= pica_point_shadow(light.color.w, -to_light, reach);
}
vec3 pica_surface_scene_point_primary(vec3 normal_view) {
    vec3 color = vec3(0.0);
    for (uint slot = 0u; slot < pica_surface.control.y; ++slot) {
        vec3 direction, radiance;
        pica_surface_scene_point(int(slot), direction, radiance);
        color += max(dot(normal_view, direction), 0.0) * radiance;
    }
    for (uint slot = 0u; slot < pica_surface_placed_count(); ++slot) {
        vec3 direction, radiance;
        pica_surface_placed_light(int(slot), direction, radiance);
        color += max(dot(normal_view, direction), 0.0) * radiance;
    }
    return color;
}
vec3 pica_surface_scene_directional_response(vec3 normal_view, uint capture_mask, out vec3 captured) {
    captured = vec3(0.0);
    vec3 color = vec3(0.0);
    for (uint slot = 0u; slot < pica_surface.control.z; ++slot) {
        vec3 direction = pica_surface_unit(pica_surface.scene_directions[slot].xyz);
        vec3 response = max(dot(normal_view, direction), 0.0) *
            pica_surface.scene_directional_colors[slot].rgb * pica_surface_shadow(10 + int(slot));
        color += response;
        if ((capture_mask & (1u << (10u + slot))) != 0u) captured += response;
    }
    return color;
}
vec3 pica_surface_scene_directional_primary(vec3 normal_view) {
    vec3 unused;
    return pica_surface_scene_directional_response(normal_view, 0u, unused);
}
// Raw radiance and the selected directional contribution, before tint,
// abs/clamp and native colour quantization. A zero capture mask eliminates
// the extra accumulation in ordinary native receiver shaders.
vec3 pica_surface_native_response(vec3 normal_view, uint capture_mask, out vec3 captured) {
    captured = vec3(0.0);
    vec3 direct = vec3(0.0);
    bool native_direct = pica_surface.environment_parameters.w == 0.0 ||
        pica_surface.environment_parameters.x != 0.0;
    for (int slot = 0; pica_surface.control.x != 0u && native_direct && slot < 3; ++slot) {
        if (pica_surface.lights[slot].direction_enabled.w == 0.0) continue;
        if (any(notEqual(pica_surface.lights[slot].diffuse.rgb, vec3(0.0)))) {
        vec3 direction, radiance;
        pica_surface_light(slot, direction, radiance);
        vec3 response = max(dot(normal_view, direction), 0.0) * radiance * pica_surface_shadow(slot);
        direct += response;
        if ((capture_mask & (1u << uint(slot))) != 0u) captured += response;
        }
    }
    vec3 color = direct * pica_surface.material_diffuse.rgb;
    captured *= pica_surface.material_diffuse.rgb;
    if (pica_surface.environment_parameters.w != 0.0) {
        color *= pica_surface.environment_parameters.x;
        captured *= pica_surface.environment_parameters.x;
    }
    color += pica_surface.resolved_ambient.rgb;
    if (pica_surface.environment_parameters.w != 0.0) {
        for (int slot = 0; slot < 2; ++slot) {
            if (pica_surface.environment_directions[slot].w <= 0.0) continue;
            vec3 direction = pica_surface_unit(pica_surface.environment_directions[slot].xyz);
            vec3 response = max(dot(normal_view, direction), 0.0) *
                pica_surface.environment_colors[slot].rgb * pica_surface.environment_directions[slot].w *
                pica_surface_shadow(8 + slot);
            color += response;
            if ((capture_mask & (1u << uint(8 + slot))) != 0u) captured += response;
        }
    }
    color += pica_surface_scene_point_primary(normal_view);
    vec3 scene_captured;
    color += pica_surface_scene_directional_response(normal_view, capture_mask, scene_captured);
    captured += scene_captured;
    return color;
}
vec3 pica_surface_native_primary(vec3 normal_view) {
    vec3 unused;
    vec3 color = pica_surface_native_response(normal_view, 0u, unused);
    return min(abs(color * pica_surface_tint.rgb), vec3(1.0));
}
// Direct light that seeds global illumination: the native scene directionals
// (gi_sources.x), the added key (y) and the dynamic lights (z: native local
// lights, Navi and fairies), shadowed. Ambient and the added fill are never
// sources: OoT bakes its fill light into the native colour.
vec3 pica_surface_gi_direct(vec3 normal_view) {
    // PICA lighting is gamma encoded: a linear weight w is w^(1/2.2) here.
    vec3 weight = pow(max(pica_surface.gi_sources.xyz, vec3(0.0)), vec3(1.0 / 2.2));
    vec3 native_direct = vec3(0.0);
    bool native_enabled = pica_surface.environment_parameters.w == 0.0 ||
        pica_surface.environment_parameters.x != 0.0;
    for (int slot = 0; pica_surface.control.x != 0u && native_enabled && slot < 3; ++slot) {
        if (pica_surface.lights[slot].direction_enabled.w == 0.0) continue;
        vec3 direction, radiance;
        pica_surface_light(slot, direction, radiance);
        float source = pica_surface.lights[slot].position_radius.w > 0.0
            ? weight.z : weight.x;
        native_direct += max(dot(normal_view, direction), 0.0) * radiance * pica_surface_shadow(slot) * source;
    }
    vec3 color = native_direct * pica_surface.material_diffuse.rgb;
    if (pica_surface.environment_parameters.w != 0.0) {
        color *= pica_surface.environment_parameters.x;
        if (pica_surface.environment_directions[0].w > 0.0)
            color += max(dot(normal_view, pica_surface_unit(pica_surface.environment_directions[0].xyz)), 0.0) *
                pica_surface.environment_colors[0].rgb * pica_surface.environment_directions[0].w *
                pica_surface_shadow(8) * weight.y;
    }
    color += pica_surface_scene_point_primary(normal_view) * weight.z;
    color += pica_surface_scene_directional_primary(normal_view) * weight.x;
)glsl") +
        (fragmentOperand ? std::string(R"glsl(    vec3 material = (pica_surface.control.w & 2u) != 0u
        ? pica_primary_color.rgb : pica_surface_tint.rgb;
    return min(abs(color * material), vec3(1.0));
)glsl") : std::string(R"glsl(    return min(abs(color * pica_surface_tint.rgb), vec3(1.0));
)glsl")) +
        std::string(R"glsl(}
vec3 pica_surface_primary(vec3 native_color, vec3 normal_view) {
    if (pica_surface.control.x != 0u ||
        (pica_surface.environment_parameters.w != 0.0 && (pica_surface.control.w & 1u) == 0u))
        return pica_surface_native_primary(normal_view);
    // Authored unlit color is appearance, not an ambient/direct-light gain.
    // The scene reference keeps it intact even when the override is active.
    if (pica_surface.environment_parameters.w != 0.0)
        return min(native_color + pica_surface_native_primary(normal_view), vec3(1.0));
    return min(native_color + (pica_surface_scene_point_primary(normal_view) +
               pica_surface_scene_directional_primary(normal_view)) * pica_surface_tint.rgb, vec3(1.0));
}
// Gameplay water (pica_surface_water): its native colour is its appearance.
// Added optics: Fresnel reflection of the environment (F0 water_optics.x;
// sky: the scene's colour at a distance) and GGX specular of the direct
// lights, shadowed: colour = (1 - F) native + F sky + specular.
bool pica_surface_water_fragment = false;
float pica_surface_water_fresnel(float cosine) {
    float f0 = pica_surface.water_optics.x;
    float grazing = 1.0 - clamp(cosine, 0.0, 1.0);
    float grazing2 = grazing * grazing;
    return f0 + (1.0 - f0) * grazing2 * grazing2 * grazing;
}
vec3 pica_surface_water_specular(vec3 normal, vec3 view_dir, vec3 direction, vec3 radiance) {
    float n_dot_l = dot(normal, direction);
    if (n_dot_l <= 0.0) return vec3(0.0);
    vec3 half_vector = pica_surface_unit(direction + view_dir);
    float n_dot_h = clamp(dot(normal, half_vector), 0.0, 1.0);
    float alpha = mix(pica_surface.water_optics.y, 0.16, pica_surface_water_fast());
    float alpha2 = alpha * alpha;
    float denominator = n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (3.14159265 * denominator * denominator);
    float fresnel = pica_surface_water_fresnel(dot(view_dir, half_vector));
    // Cook-Torrance with a unit visibility term: D F / (4 n.v), faded in at
    // the light's horizon.
    float n_dot_v = max(dot(normal, view_dir), 0.1);
    return radiance * (distribution * fresnel / (4.0 * n_dot_v) * smoothstep(0.0, 0.1, n_dot_l));
}
// The ripples' world slope (d height / dx, dz): a sum of directional waves
// (no lattice, so no tiles), spread around one wind direction with deep-water
// phase speeds (slowed), each faded out where a pixel covers its wavelength.
vec2 pica_surface_water_slope() {
    float time = pica_surface.water_ripples.x;
    const float wind = 0.38;
    vec2 slope = vec2(0.0);
    if (pica_surface_water_textured) {
        // Wavelength (texture repeats), direction (degrees from the wind),
        // slope: the world set's proportions, at the native texture's scale.
        const vec3 texture_waves[8] = vec3[8](vec3(0.50, 0.0, 0.050), vec3(0.36, 37.0, 0.045),
                                              vec3(0.27, -29.0, 0.040), vec3(0.20, 71.0, 0.035),
                                              vec3(0.14, -63.0, 0.030), vec3(0.10, 113.0, 0.025),
                                              vec3(0.075, -101.0, 0.020), vec3(0.055, 151.0, 0.020));
        float texture_scale = max(pica_surface.water_ripples.z, 0.05);
        for (int wave = 0; wave < 8; ++wave) {
            float wavelength = texture_waves[wave].x * texture_scale;
            float fade = clamp(1.5 - 3.0 * pica_surface_water_uv_footprint / wavelength, 0.0, 1.0);
            if (fade <= 0.0) continue;
            float angle = wind + radians(texture_waves[wave].y);
            vec2 direction = vec2(cos(angle), sin(angle));
            float number = 6.2831853 / wavelength;
            // Their own slow motion on the flowing texture.
            float phase = number * dot(direction, pica_surface_water_uv) - 0.9 * sqrt(number) * time +
                          float(wave) * 1.618;
            // The phase's gradient on the surface, as a direction.
            vec2 across = transpose(pica_surface_water_uv_per_unit) * direction;
            float length_across = length(across);
            if (length_across <= 0.0) continue;
            slope += across / length_across * (texture_waves[wave].z * cos(phase) * fade);
        }
        slope *= pica_surface.water_ripples.y;
    } else {
    vec2 position = vec2(dot(pica_surface_water_world, pica_surface_water_tangent),
                         dot(pica_surface_water_world, pica_surface_water_bitangent)) -
                    pica_surface_water_flow * time;
    float fast = pica_surface_water_fast();
    float scale = max(pica_surface.water_ripples.z, 0.05) * mix(1.0, 0.6, fast);
    // Wavelength (world units), direction (degrees from the wind) and slope.
    const vec3 waves[8] = vec3[8](vec3(157.0, 0.0, 0.050), vec3(113.0, 37.0, 0.045), vec3(83.0, -29.0, 0.040),
                                  vec3(61.0, 71.0, 0.035), vec3(43.0, -63.0, 0.030), vec3(31.0, 113.0, 0.025),
                                  vec3(23.0, -101.0, 0.020), vec3(17.0, 151.0, 0.020));
    for (int wave = 0; wave < 8; ++wave) {
        float wavelength = waves[wave].x * scale;
        float fade = clamp(1.5 - 3.0 * pica_surface_water_footprint / wavelength, 0.0, 1.0);
        if (fade <= 0.0) continue;
        float angle = wind + radians(waves[wave].y);
        vec2 direction = vec2(cos(angle), sin(angle));
        float number = 6.2831853 / wavelength;
        // Deep water: c = sqrt(g / k), g = 981 units/s^2, at a third.
        float speed = 0.33 * sqrt(981.0 / number);
        float phase = number * (dot(direction, position) - speed * time) + float(wave) * 1.618;
        slope += direction * (waves[wave].z * cos(phase) * fade);
    }
    slope *= pica_surface.water_ripples.y * mix(1.0, 1.8, fast);
    }
    // Movers' rings on a level surface (water_rings: centre xz, birth,
    // strength): a wave packet spreading from the centre and fading.
    if (pica_surface_water_up.y > 0.95) {
        float fade = clamp(1.5 - 3.0 * pica_surface_water_footprint / 12.0, 0.0, 1.0);
        for (int i = 0; fade > 0.0 && i < 16; ++i) {
            vec4 ring = pica_surface.water_rings[i];
            float age = time - ring.z;
            if (ring.w <= 0.0 || age < 0.0 || age > 3.0) continue;
            vec2 offset = pica_surface_water_world.xz - ring.xy;
            float distance_world = length(offset);
            float front = distance_world - (6.0 + 55.0 * age);
            float width = 6.0 + 5.0 * age;
            float envelope = ring.w * exp(-1.2 * age) * exp(-0.5 * front * front / (width * width));
            slope += offset / max(distance_world, 1.0e-3) * (0.9 * fade * envelope * sin(0.5236 * front));
        }
    }
    return slope;
}
vec3 pica_surface_water_shade(vec3 native_rgb, vec3 sky) {
    if (pica_surface.water_optics.w == -1.0) return vec3(1.0, 0.0, 1.0);
    vec3 view_dir = pica_surface_unit(pica_view);
    vec2 slope = pica_surface_water_slope();
    if (pica_surface.water_optics.w == -2.0) return vec3(0.5 + 2.5 * slope, 0.5).xzy;
    if (pica_surface.water_optics.w == -3.0)
        return pica_surface.water_flow.z <= 0.0 ? vec3(0.2, 1.0, 0.2)
                                                : mix(vec3(0.0, 0.3, 1.0), vec3(1.0, 0.2, 0.0), pica_surface_water_fast());
    vec3 normal = pica_surface_view_direction(pica_surface_water_up - slope.x * pica_surface_water_tangent -
                                              slope.y * pica_surface_water_bitangent);
    // Seen from below (underwater), the surface faces down.
    if (dot(normal, view_dir) < 0.0) normal = -normal;
    float reflectance = pica_surface_water_fresnel(dot(normal, view_dir));
    // The environment: the fog colour, unless the reflection trace owns it
    // (water_flow.w) on a body's plane, where it mixes its hits and the
    // screen's sky over the native colour by the same reflectance.
    bool traced = pica_surface.water_flow.w > 0.0 && pica_surface_water_traced;
    vec3 color = mix(native_rgb, sky, traced ? 0.0 : reflectance * pica_surface.water_optics.z);
    if (pica_surface.water_optics.w == -4.0)
        return vec3(traced ? 1.0 : 0.0, reflectance, pica_surface_water_traced ? 0.0 : 1.0);
    // White water where the flow is fast (waterfalls, rapids): streaks
    // running with the flow (warped sines: no lattice).
    float fast = pica_surface_water_fast();
    if (fast > 0.0) {
        float speed = length(pica_surface_water_flow);
        vec2 along_direction = pica_surface_water_flow / max(speed, 1.0e-3);
        vec2 position = vec2(dot(pica_surface_water_world, pica_surface_water_tangent),
                             dot(pica_surface_water_world, pica_surface_water_bitangent));
        float along = dot(position, along_direction) - speed * pica_surface.water_ripples.x;
        float across = dot(position, vec2(-along_direction.y, along_direction.x));
        if (pica_surface_water_textured) {
            // On a flowing texture: streaks along its scroll, riding it (its
            // coordinates already move), at its scale.
            vec2 scroll = pica_surface.water_flow.xy;
            vec2 along_texture = length(scroll) > 0.0 ? normalize(scroll) : vec2(1.0, 0.0);
            along = 6.0 * dot(pica_surface_water_uv, along_texture);
            across = 20.0 * dot(pica_surface_water_uv, vec2(-along_texture.y, along_texture.x));
        }
        float first = sin(across * 0.13 + 1.7 * sin(along * 0.021 + across * 0.017));
        float second = sin(across * 0.31 - 1.3 * sin(along * 0.047 + 0.7) + along * 0.013);
        float streak = smoothstep(0.35, 0.95, 0.5 + 0.3 * first + 0.2 * second);
        // A waterfall churns white where it lands (the bottom of its sheet).
        float landing = 1.0 - smoothstep(0.0, 60.0, pica_surface_water_world.y - pica_surface.water_ripples.w);
        float light = clamp(dot(native_rgb, vec3(0.299, 0.587, 0.114)) * 1.3 + 0.15, 0.6, 1.0);
        color = mix(color, vec3(light), max(0.45 * fast * streak,
                                            0.7 * landing * smoothstep(0.2, 0.7, 0.5 + 0.3 * first + 0.2 * second)));
    }
    vec3 specular = vec3(0.0);
    for (uint slot = 0u; slot < pica_surface.control.z; ++slot)
        specular += pica_surface_water_specular(normal, view_dir,
            pica_surface_unit(pica_surface.scene_directions[slot].xyz),
            pica_surface.scene_directional_colors[slot].rgb * pica_surface_shadow(10 + int(slot)));
    if (pica_surface.environment_parameters.w != 0.0) {
        for (int slot = 0; slot < 2; ++slot) {
            if (pica_surface.environment_directions[slot].w <= 0.0) continue;
            specular += pica_surface_water_specular(normal, view_dir,
                pica_surface_unit(pica_surface.environment_directions[slot].xyz),
                pica_surface.environment_colors[slot].rgb * pica_surface.environment_directions[slot].w *
                    pica_surface_shadow(8 + slot));
        }
    }
    for (uint slot = 0u; slot < pica_surface.control.y; ++slot) {
        vec3 direction, radiance;
        pica_surface_scene_point(int(slot), direction, radiance);
        specular += pica_surface_water_specular(normal, view_dir, direction, radiance);
    }
    for (uint slot = 0u; slot < pica_surface_placed_count(); ++slot) {
        vec3 direction, radiance;
        pica_surface_placed_light(int(slot), direction, radiance);
        specular += pica_surface_water_specular(normal, view_dir, direction, radiance);
    }
    return color + specular * pica_surface.water_optics.w;
}
)glsl");
}

std::string_view PicaSurfaceLightingWaterShading() noexcept {
    return R"glsl(
    if (pica_surface_water_fragment)
        combiner_output.rgb = pica_surface_water_shade(combiner_output.rgb, fragment_uniforms.fog_color.rgb);
)glsl";
}

std::string_view PicaSurfaceLightingWaterMarker() noexcept {
    return R"glsl(
    if (pica_surface_water_fragment)
        pica_material_guide = vec4(0.0, 0.0, 1.0, pica_material_guide.a);
)glsl";
}

std::string PicaSurfaceLightingBody(bool fragmentOperand) {
    return std::string(R"glsl(
    bool surface_water = pica_surface_water(cross(dFdx(pica_view), dFdy(pica_view)));
    pica_surface_water_fragment = surface_water;
    pica_surface_water_footprint = length(fwidth(pica_view)) * length(pica_surface.view_to_world_linear[0].xyz);
    {
        vec2 water_uv_dx = dFdx(pica_texcoord0);
        vec2 water_uv_dy = dFdy(pica_texcoord0);
        vec3 water_view_dx = dFdx(pica_view);
        vec3 water_view_dy = dFdy(pica_view);
        if (surface_water) {
            vec3 flow = pica_surface_water_flow_world(water_uv_dx, water_uv_dy, water_view_dx, water_view_dy);
            pica_surface_water_flow = vec2(dot(flow, pica_surface_water_tangent),
                                           dot(flow, pica_surface_water_bitangent));
            // Lying water on a flowing texture: its waves in the texture's
            // coordinates (pica_surface_water_textured).
            vec3 world_dx = vec3(dot(pica_surface.view_to_world_linear[0].xyz, -water_view_dx),
                                 dot(pica_surface.view_to_world_linear[1].xyz, -water_view_dx),
                                 dot(pica_surface.view_to_world_linear[2].xyz, -water_view_dx));
            vec3 world_dy = vec3(dot(pica_surface.view_to_world_linear[0].xyz, -water_view_dy),
                                 dot(pica_surface.view_to_world_linear[1].xyz, -water_view_dy),
                                 dot(pica_surface.view_to_world_linear[2].xyz, -water_view_dy));
            mat2 surface_from_screen = mat2(vec2(dot(world_dx, pica_surface_water_tangent),
                                                 dot(world_dx, pica_surface_water_bitangent)),
                                            vec2(dot(world_dy, pica_surface_water_tangent),
                                                 dot(world_dy, pica_surface_water_bitangent)));
            mat2 texture_from_screen = mat2(water_uv_dx, water_uv_dy);
            if (abs(pica_surface_water_up.y) >= 0.75 && pica_surface.water_flow.z > 0.0 &&
                abs(determinant(surface_from_screen)) > 1.0e-12 &&
                abs(determinant(texture_from_screen)) > 1.0e-12) {
                pica_surface_water_textured = true;
                pica_surface_water_uv = pica_texcoord0;
                pica_surface_water_uv_footprint = length(fwidth(pica_texcoord0));
                pica_surface_water_uv_per_unit = texture_from_screen * inverse(surface_from_screen);
            }
            // A steep face falls (a waterfall, a river's drop between its
            // pieces): white water running down it, whatever its texture does.
            if (abs(pica_surface_water_up.y) < 0.75) {
                vec3 down = vec3(0.0, -1.0, 0.0) + pica_surface_water_up * pica_surface_water_up.y;
                vec2 slope_down = vec2(dot(down, pica_surface_water_tangent), dot(down, pica_surface_water_bitangent));
                pica_surface_water_flow = 150.0 * slope_down / max(length(slope_down), 1.0e-3);
            }
        }
    }
)glsl") +
        (fragmentOperand ? std::string(R"glsl(    bool surface_fragment_operand = (pica_surface.control.w & 2u) != 0u && !surface_water;
    // Exact all-native-off endpoint. Canonical inputs were saved before this
    // body, so native TEV alpha replay still uses the original RGB/alpha.
    // Unequal/fractional native ambient/direct gains need decomposition of
    // the native contributions; do not approximate them with one multiplier.
    if (surface_fragment_operand && pica_surface.environment_parameters.w != 0.0 &&
        pica_surface.environment_parameters.x == 0.0 &&
        pica_surface.environment_parameters.y == 0.0) {
        primary_fragment_color.rgb = vec3(0.0);
        secondary_fragment_color.rgb = vec3(0.0);
    }
    if (!surface_fragment_operand &&
        (pica_surface.control.x != 0u || pica_surface.environment_parameters.w != 0.0 ||
)glsl") : std::string(R"glsl(    if ((pica_surface.control.x != 0u || pica_surface.environment_parameters.w != 0.0 ||
)glsl")) +
        std::string(R"glsl(         pica_surface.control.y != 0u || pica_surface.control.z != 0u) && !surface_water) {
        rounded_primary_color.rgb = byteround3(pica_surface_primary(rounded_primary_color.rgb,
            pica_surface_unit(pica_surface_normal)));
    }
    if (pica_surface.control.x == 0u) {
        // Native per-pixel lights a scene shadow covers (receiver slots 0-7
        // are native fragment lights here): their shadowed diffuse share
        // leaves the native result.
        vec3 pica_shadow_normal = pica_surface_unit(pica_surface_normal);
        for (int pica_light = 0; pica_light < 8; ++pica_light) {
            vec3 pica_light_shadow = pica_surface_shadow(pica_light);
            if (all(equal(pica_light_shadow, vec3(1.0)))) continue;
            float pica_light_facing = max(dot(pica_shadow_normal,
                pica_surface_unit(fragment_uniforms.lighting_position[pica_light].xyz)), 0.0);
            primary_fragment_color.rgb = max(primary_fragment_color.rgb -
                fragment_uniforms.lighting_diffuse[pica_light].rgb * pica_light_facing *
                (vec3(1.0) - pica_light_shadow), vec3(0.0));
        }
    }
)glsl") +
        (fragmentOperand ? std::string(R"glsl(    // Shadow subtraction above belongs to native illumination. Add the new
    // light afterwards so a native shadow cannot subtract a placed source.
    if (surface_fragment_operand) {
        vec3 unused;
        vec3 added = pica_surface_native_response(pica_surface_unit(pica_surface_normal), 0u, unused);
        primary_fragment_color.rgb = clamp(primary_fragment_color.rgb + added, vec3(0.0), vec3(1.0));
    }
)glsl") : std::string(R"glsl()glsl"));
}

} // namespace Fast::Renderer3ds
