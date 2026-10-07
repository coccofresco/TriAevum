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

std::optional<PicaSurfaceLightingUniforms> BuildPicaSurfaceLightingUniforms(
    const PicaVertexShaderHookLayout& hooks, uint32_t booleanUniforms,
    std::span<const uint8_t> floatUniformBytes,
    const PicaNativeLightBinding& binding,
    const PicaEnvironmentLighting& environment,
    const PicaSurfaceLightingView* cameraView,
    const PicaPointShadowAssignment* pointShadows) noexcept {
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
    const auto& reflectance = hasDiffuse ? result.MaterialDiffuse : result.MaterialAmbient;
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
    const float ambientGain = !environment.Enabled || environment.PreserveSceneBrightness
        ? 1.0F : environment.NativeAmbientStrength;
    result.EnvironmentParameters[1] = ambientGain;
    if (environment.Enabled) {
        result.EnvironmentParameters = {environment.NativeLocalStrength, ambientGain,
                                        environment.FogStrength, 1.0F};
        const bool preserveReference = environment.PreserveSceneBrightness;
        result.Control[3] = preserveReference ? 1U : 0U;
        // The profile replaces illumination, not material reflectance. Native
        // TEV gains still apply, so dropping these coefficients overexposes
        // materials authored with reduced diffuse/ambient response.
        // Ambient-only materials use their authored ambient reflectance for
        // the new direct lights rather than acquiring an arbitrary white one.
        for (size_t channel = 0; channel < 3; ++channel)
            result.EnvironmentAmbient[channel] = environment.AmbientColor[channel] *
                environment.AmbientIntensity * result.MaterialAmbient[channel];
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

std::string_view PicaSurfaceLightingDeclarations() noexcept {
    return R"glsl(
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
} pica_surface;
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
vec3 pica_surface_scene_point_primary(vec3 normal_view) {
    vec3 color = vec3(0.0);
    for (uint slot = 0u; slot < pica_surface.control.y; ++slot) {
        vec3 direction, radiance;
        pica_surface_scene_point(int(slot), direction, radiance);
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
    return min(abs(color * pica_surface_tint.rgb), vec3(1.0));
}
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
)glsl";
}

std::string_view PicaSurfaceLightingBody() noexcept {
    return R"glsl(
    if (pica_surface.control.x != 0u || pica_surface.environment_parameters.w != 0.0 ||
        pica_surface.control.y != 0u || pica_surface.control.z != 0u) {
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
)glsl";
}

} // namespace Fast::Renderer3ds
