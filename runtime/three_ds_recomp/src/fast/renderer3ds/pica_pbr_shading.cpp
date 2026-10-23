#include "fast/renderer3ds/pica_pbr_shading.h"
#include "fast/renderer3ds/pica_pbr_seams.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace Fast::Renderer3ds {

PicaPbrUniforms BuildPicaPbrUniforms(
    const PicaPbrMaterialParameters& parameters, uint8_t textureMask,
    const PicaLightingProgram& nativeLighting) noexcept {
    PicaPbrUniforms result;
    result.BaseColorDielectricF0 = {parameters.BaseColorFactor[0], parameters.BaseColorFactor[1],
        parameters.BaseColorFactor[2], 0.04F * parameters.SpecularLevel};
    result.EmissiveNormalScale = {parameters.EmissiveFactor[0], parameters.EmissiveFactor[1],
        parameters.EmissiveFactor[2], parameters.NormalScale * parameters.NormalYSign};
    result.OrmMinimumRoughness = {parameters.OcclusionStrength, parameters.RoughnessFactor,
        parameters.MetallicFactor, parameters.MinimumRoughness};
    result.Control[0] = textureMask;
    result.Control[1] = std::bit_cast<uint32_t>(parameters.GlossinessStrength);
    result.Control[2] = static_cast<uint32_t>(parameters.DebugView);
    result.Control[3] = std::bit_cast<uint32_t>(parameters.HeightScale);
    result.SpecularReflectionWeight = {parameters.SpecularWeight,
        parameters.SpecularWeight * parameters.ScreenSpaceReflectionWeight, 0.0F, 0.0F};
    const uint32_t loadedChannels = ((textureMask & 1U) ? 1U : 0U) |
        ((textureMask & 2U) ? 14U : 0U) | ((textureMask & 4U) ? 16U : 0U) |
        ((textureMask & 8U) ? 32U : 0U);
    result.SeamSettings = {parameters.SeamSoftening ? parameters.SeamStrength : 0.0F,
        parameters.SeamWidth, std::bit_cast<float>(parameters.SeamChannels & loadedChannels), 0.0F};
    result.NativeLighting = nativeLighting;
    return result;
}

namespace {

// The specialized material program is assembled from these; the superset
// program shares them with draw-time controls (BuildPicaPbrUberShader).
constexpr std::string_view kPbrDeclarations = R"glsl(
layout(set=0,binding=17,std140) uniform PicaPbrMaterial {
    vec4 base_color_dielectric_f0;
    vec4 emissive_normal_scale;
    vec4 orm_minimum_roughness;
    uvec4 control;
    vec4 specular_reflection_weight;
    vec4 seam_settings;
    uvec4 native_control;
    uvec4 native_lights[8];
    vec4 native_luts[7];
} pbr_material;
layout(set=0,binding=18) uniform sampler2D pbr_normal_map;
layout(set=0,binding=19) uniform sampler2D pbr_orm_map;
layout(set=0,binding=20) uniform sampler2D pbr_emissive_map;
layout(set=0,binding=21) uniform sampler2D pbr_height_map;
vec3 pbr_srgb_to_linear(vec3 value) {
    return mix(pow((value + 0.055) / 1.055, vec3(2.4)), value / 12.92,
               lessThanEqual(value, vec3(0.04045)));
}
vec3 pbr_linear_to_srgb(vec3 value) {
    value = max(value, vec3(0.0));
    return mix(1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055, value * 12.92,
               lessThanEqual(value, vec3(0.0031308)));
}
bool pbr_surface_frame(vec2 uv, vec3 geometric, out vec3 tangent, out vec3 bitangent) {
    vec3 dpdx = dFdx(-pica_view), dpdy = dFdy(-pica_view);
    vec2 duvdx = dFdx(uv), duvdy = dFdy(uv);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    tangent = bitangent = vec3(0.0);
    if (abs(determinant) <= 1.0e-12) return false;
    vec3 t = (dpdx * duvdy.y - dpdy * duvdx.y) * sign(determinant);
    vec3 b = (dpdy * duvdx.x - dpdx * duvdy.x) * sign(determinant);
    t -= geometric * dot(geometric, t);
    if (dot(t, t) <= 1.0e-20 || dot(b, b) <= 1.0e-20) return false;
    tangent = normalize(t);
    bitangent = cross(geometric, tangent);
    bitangent *= dot(bitangent, b) < 0.0 ? -1.0 : 1.0;
    return true;
}
vec2 pbr_parallax_uv(vec2 uv, vec3 view_tangent, vec2 gradient_x, vec2 gradient_y,
                     float seam_height, float seam_weight) {
    float scale = uintBitsToFloat(pbr_material.control.w);
    // Fade at grazing/back-facing angles instead of an unbounded UV offset.
    scale *= smoothstep(0.05, 0.2, view_tangent.z);
    if (scale <= 0.0) return uv;
    float layers = mix(32.0, 8.0, clamp(view_tangent.z, 0.0, 1.0));
    float step_depth = 1.0 / layers;
    vec2 step_uv = view_tangent.xy / max(view_tangent.z, 0.2) * scale * step_depth;
    vec2 current_uv = uv;
    float ray_depth = 0.0;
    float surface_depth = 1.0 - textureGrad(pbr_height_map, current_uv, gradient_x, gradient_y).r;
    if (seam_weight > 0.0) surface_depth = mix(surface_depth, 1.0 - seam_height, seam_weight);
    float previous_difference = surface_depth;
    for (int step_index = 0; step_index < 32; ++step_index) {
        if (ray_depth >= surface_depth) break;
        previous_difference = surface_depth - ray_depth;
        current_uv -= step_uv;
        ray_depth += step_depth;
        surface_depth = 1.0 - textureGrad(pbr_height_map, current_uv, gradient_x, gradient_y).r;
        if (seam_weight > 0.0) surface_depth = mix(surface_depth, 1.0 - seam_height, seam_weight);
    }
    float difference = surface_depth - ray_depth;
    float weight = clamp(-difference / max(previous_difference - difference, 1.0e-6), 0.0, 1.0);
    return current_uv + step_uv * weight;
}
vec3 pbr_surface_normal(vec2 uv, vec3 geometric, vec3 tangent, vec3 bitangent, bool valid_frame) {
    if (!valid_frame || (pbr_material.control.x & 1u) == 0u ||
        pbr_material.emissive_normal_scale.w == 0.0) return geometric;
    vec3 mapped = texture(pbr_normal_map, uv).xyz * 2.0 - 1.0;
    mapped.xy *= vec2(abs(pbr_material.emissive_normal_scale.w),
                      pbr_material.emissive_normal_scale.w);
    vec3 transformed = tangent * mapped.x + bitangent * mapped.y + geometric * mapped.z;
    return dot(transformed, transformed) > 1.0e-20 ? normalize(transformed) : geometric;
}
void pbr_ggx(vec3 n, vec3 v, vec3 l, vec3 albedo, vec3 f0, float roughness, float metallic,
             bool two_sided, out vec3 diffuse, out vec3 specular) {
    float nl = two_sided ? abs(dot(n, l)) : max(dot(n, l), 0.0);
    float nv = two_sided ? abs(dot(n, v)) : max(dot(n, v), 0.0);
    vec3 h = pica_surface_unit(v + l);
    float nh = two_sided ? abs(dot(n, h)) : max(dot(n, h), 0.0);
    float vh = max(dot(v, h), 0.0);
    vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - vh, 5.0);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nh * nh * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / max(3.14159265359 * denominator * denominator, 1.0e-8);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2) +
                                 nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2), 1.0e-6);
    // Native light RGB is Lambert irradiance (the native model omits 1/pi).
    // Convert that convention once, rather than adding an artistic light gain.
    diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo * nl;
    specular = fresnel * (3.14159265359 * distribution * visibility * nl) *
        pbr_material.specular_reflection_weight.x;
}
vec3 pbr_baked_brdf_reference(vec3 v, vec3 l, vec3 albedo, vec3 f0, float roughness, float metallic,
                              inout vec3 diffuse, inout vec3 specular) {
    vec3 reference_diffuse, reference_specular;
    pbr_ggx(pica_surface_unit(pica_surface.baked_reference_up.xyz), v, l, albedo, f0, roughness, metallic,
            false, reference_diffuse, reference_specular);
    vec3 reference = reference_diffuse + reference_specular;
    // The same bounded orientation policy as the plain surface evaluator.
    // In particular, grazing sunlight cannot blow out a facing wall.
    vec3 scale = min(vec3(1.0), reference / max(diffuse + specular, vec3(1.0e-6)));
    diffuse *= scale;
    specular *= scale;
    return reference;
}
)glsl";
constexpr std::string_view kPbrNativeSpot = R"glsl(
float pbr_native_spot(int table, bool two_sided, vec3 n, vec3 t, vec3 v,
                      vec3 l, vec3 h, vec3 spot) {
    vec4 config = pbr_material.native_luts[2];
    float x = 0.0;
    switch (int(config.x)) {
    case 0: x = dot(n, pica_surface_unit(h)); break;
    case 1: x = dot(v, pica_surface_unit(h)); break;
    case 2: x = dot(n, v); break;
    case 3: x = dot(l, n); break;
    case 4: x = dot(-l, spot); break;
    case 5:
        if (pbr_material.native_control.y == 8u) {
            vec3 half_unit = pica_surface_unit(h);
            x = dot(half_unit - n * dot(n, half_unit), t);
        }
        break;
    }
    if (config.y != 0.0)
        return config.z * pica_lighting_lut_unsigned(table, two_sided ? abs(x) : max(x, 0.0));
    return config.z * pica_lighting_lut_signed(table, x);
}
)glsl";
constexpr std::string_view kPbrUnlitPrimary = R"glsl(
    if (pica_surface.baked_reference_up.w == 0.0 && (pica_surface.control.x != 0u ||
        (pica_surface.environment_parameters.w != 0.0 && (pica_surface.control.w & 1u) == 0u)))
        rounded_primary_color.rgb = byteround3(min(abs(pica_surface_tint.rgb), vec3(1.0)));
)glsl";
constexpr std::string_view kPbrNativeUnlit = R"glsl(
    primary_fragment_color.rgb = vec3(1.0);
    secondary_fragment_color.rgb = vec3(0.0);
)glsl";
constexpr std::string_view kPbrFrame = R"glsl(
    vec3 pbr_geometric_normal = pica_surface_unit(pica_surface_normal);
    vec3 pbr_view = pica_surface_unit(pica_view);
    vec3 pbr_tangent, pbr_bitangent;
    bool pbr_valid_frame = pbr_surface_frame(pbr_native_uv, pbr_geometric_normal, pbr_tangent, pbr_bitangent);
    vec2 pbr_gradient_x = dFdx(pbr_native_uv), pbr_gradient_y = dFdy(pbr_native_uv);
    vec2 pbr_uv = pbr_native_uv;
    bool pbr_relief = pbr_valid_frame && (pbr_material.control.x & 8u) != 0u &&
        uintBitsToFloat(pbr_material.control.w) > 0.0;
    if (pbr_relief)
        pbr_uv = pbr_parallax_uv(pbr_native_uv,
            vec3(dot(pbr_view, pbr_tangent), dot(pbr_view, pbr_bitangent), dot(pbr_view, pbr_geometric_normal)),
            pbr_gradient_x, pbr_gradient_y, 0.0, 0.0);
)glsl";
constexpr std::string_view kPbrSeamInputs = R"glsl(
    PbrSeamBlend pbr_seam = pbr_seam_blend(uint(gl_PrimitiveID), -pica_view,
        pbr_geometric_normal, dFdx(-pica_view), dFdy(-pica_view));
    uint pbr_seam_channels = floatBitsToUint(pbr_material.seam_settings.z);
    // Native color keeps the original relief coordinates assigned above.
    // Only the auxiliary maps consume the seam-smoothed height field.
    if (pbr_relief && (pbr_seam_channels & 32u) != 0u && pbr_seam.weight > 0.0)
        pbr_uv = pbr_parallax_uv(pbr_native_uv,
            vec3(dot(pbr_view, pbr_tangent), dot(pbr_view, pbr_bitangent), dot(pbr_view, pbr_geometric_normal)),
            pbr_gradient_x, pbr_gradient_y, pbr_seam.value.height, pbr_seam.weight);
)glsl";
constexpr std::string_view kPbrShadingNormal = R"glsl(
    vec3 pbr_normal = pbr_surface_normal(pbr_uv, pbr_geometric_normal, pbr_tangent, pbr_bitangent, pbr_valid_frame);
    vec3 pbr_orm = (pbr_material.control.x & 2u) != 0u ? texture(pbr_orm_map, pbr_uv).rgb : vec3(1.0);
)glsl";
constexpr std::string_view kPbrSeamShading = R"glsl(
    if (pbr_seam.weight > 0.0) {
        if ((pbr_seam_channels & 1u) != 0u) {
            vec3 blended = mix(pbr_normal, pbr_seam.value.normal, pbr_seam.weight);
            pbr_normal = dot(blended, blended) > 1e-20 ? normalize(blended) : pbr_geometric_normal;
        }
        if ((pbr_seam_channels & 2u) != 0u) pbr_orm.r = mix(pbr_orm.r, pbr_seam.value.orm.r, pbr_seam.weight);
        if ((pbr_seam_channels & 4u) != 0u) pbr_orm.g = mix(pbr_orm.g, pbr_seam.value.orm.g, pbr_seam.weight);
        if ((pbr_seam_channels & 8u) != 0u) pbr_orm.b = mix(pbr_orm.b, pbr_seam.value.orm.b, pbr_seam.weight);
    }
)glsl";
constexpr std::string_view kPbrShadingMaterial = R"glsl(
    float pbr_ao = mix(1.0, pbr_orm.r, pbr_material.orm_minimum_roughness.x);
    float pbr_authored_roughness = clamp(pbr_orm.g * pbr_material.orm_minimum_roughness.y, 0.0, 1.0);
    float pbr_roughness = clamp(1.0 - (1.0 - pbr_authored_roughness) *
        uintBitsToFloat(pbr_material.control.y), pbr_material.orm_minimum_roughness.w, 1.0);
    float pbr_metallic = clamp(pbr_orm.b * pbr_material.orm_minimum_roughness.z, 0.0, 1.0);
    vec3 pbr_albedo = pbr_srgb_to_linear(clamp(combiner_output.rgb, 0.0, 1.0)) * pbr_material.base_color_dielectric_f0.rgb;
    vec3 pbr_f0 = mix(vec3(pbr_material.base_color_dielectric_f0.w), pbr_albedo, pbr_metallic);
    vec3 pbr_emission = (pbr_material.control.x & 4u) != 0u ?
        pbr_srgb_to_linear(texture(pbr_emissive_map, pbr_uv).rgb) : vec3(1.0);
)glsl";
constexpr std::string_view kPbrSeamEmission = R"glsl(
    if ((pbr_seam_channels & 16u) != 0u && pbr_seam.weight > 0.0)
        pbr_emission = mix(pbr_emission, pbr_seam.value.emission, pbr_seam.weight);
)glsl";
constexpr std::string_view kPbrShadingAccumulators = R"glsl(
    pbr_emission *= pbr_material.emissive_normal_scale.rgb;
    vec3 pbr_direct = vec3(0.0), pbr_ambient = vec3(0.0);
    // Diffuse direct light seeding global illumination (sources weighted by
    // gi_sources; ambient, fill, emission and specular are not sources).
    vec3 pbr_gi_native = vec3(0.0), pbr_gi_direct = vec3(0.0);
)glsl";
constexpr std::string_view kPbrUnlitReferenceNative =
    "    bool pbr_baked_reference = false;\n    bool pbr_unlit_reference = false;\n";
constexpr std::string_view kPbrUnlitReferenceSurface = R"glsl(
    bool pbr_baked_reference = pica_surface.baked_reference_up.w != 0.0;
    bool pbr_unlit_reference = pbr_baked_reference ||
        ((pica_surface.control.w & 1u) != 0u && pica_surface.control.x == 0u);
)glsl";
constexpr std::string_view kPbrNativeFlags = R"glsl(
    bool pbr_native_direct = pbr_unlit_reference || pica_surface.environment_parameters.w == 0.0 ||
        (pica_surface.control.x != 0u || pbr_material.native_control.x != 0u
            ? pica_surface.environment_parameters.x : pica_surface.environment_parameters.y) != 0.0;
    bool pbr_native_ambient = pica_surface.environment_parameters.w == 0.0 ||
        ((pica_surface.control.w & 1u) == 0u && pica_surface.environment_parameters.y != 0.0);
)glsl";
constexpr std::string_view kPbrNativeLoopBegin = R"glsl(
    for (uint slot = 0u; (pbr_native_direct || pbr_native_ambient) && slot < pbr_material.native_control.x; ++slot) {
        uint i = pbr_material.native_lights[slot].x;
        uint flags = pbr_material.native_lights[slot].y;
        vec3 delta = fragment_uniforms.lighting_position[i].xyz;
        if ((flags & 1u) == 0u) delta += pica_view;
        float light_distance = length(delta);
        vec3 l = pica_surface_unit(delta);
        bool two_sided = (flags & 2u) != 0u;
        float attenuation = 1.0;
)glsl";
constexpr std::string_view kPbrNativeAttenuation = R"glsl(
        if ((flags & 32u) != 0u)
            attenuation *= pbr_native_spot(int(8u + i), two_sided, pbr_normal, pbr_tangent,
                pbr_view, l, pbr_view + l, fragment_uniforms.lighting_spot_direction[i].xyz);
        if ((flags & 64u) != 0u)
            attenuation *= pica_lighting_lut_unsigned(int(16u + i), clamp(
                fragment_uniforms.lighting_attenuation[i].y * light_distance +
                fragment_uniforms.lighting_attenuation[i].x, 0.0, 1.0));
)glsl";
constexpr std::string_view kPbrNativeLoopEnd = R"glsl(
        if (pbr_native_direct) {
        vec3 diffuse, specular;
        pbr_ggx(pbr_normal, pbr_view, l, pbr_albedo, pbr_f0, pbr_roughness, pbr_metallic,
                two_sided, diffuse, specular);
        vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view, l);
        if ((flags & 16u) != 0u) {
            if ((pbr_material.native_control.w & 2u) != 0u) diffuse *= pica_shadow_factor.rgb;
            if ((pbr_material.native_control.w & 4u) != 0u) specular *= pica_shadow_factor.rgb;
            if ((pbr_material.native_control.w & 6u) != 0u) rim *= pica_shadow_factor.rgb;
        }
        vec3 radiance = fragment_uniforms.lighting_diffuse[i].rgb;
        pbr_direct += (diffuse + specular + rim) * radiance * attenuation * pica_surface_shadow(int(i));
        pbr_gi_native += diffuse * radiance * attenuation * pica_surface_shadow(int(i)) *
            ((flags & 1u) != 0u ? pica_surface.gi_sources.x : pica_surface.gi_sources.z);
        }
        if (pbr_native_ambient)
            pbr_ambient += fragment_uniforms.lighting_ambient[i].rgb * attenuation;
    }
    pbr_ambient += fragment_uniforms.lighting_global_ambient.rgb;
    pbr_ambient = pica_surface_resolved_ambient(pbr_ambient);
)glsl";
constexpr std::string_view kPbrSurfaceLoop = R"glsl(
    if (pica_surface.control.x != 0u) {
        for (int slot = 0; pbr_native_direct && slot < 3; ++slot) {
            if (pica_surface.lights[slot].direction_enabled.w == 0.0) continue;
            if (any(notEqual(pica_surface.lights[slot].diffuse.rgb, vec3(0.0)))) {
            vec3 l, radiance;
            pica_surface_light(slot, l, radiance);
            vec3 diffuse, specular;
            pbr_ggx(pbr_normal, pbr_view, pica_surface_unit(l), pbr_albedo, pbr_f0,
                pbr_roughness, pbr_metallic, false, diffuse, specular);
            vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view, l);
            pbr_direct += (diffuse + specular + rim) * radiance * pica_surface.material_diffuse.rgb * pica_surface_shadow(slot);
            pbr_gi_native += diffuse * radiance * pica_surface.material_diffuse.rgb * pica_surface_shadow(slot) *
                (pica_surface.lights[slot].position_radius.w > 0.0 ? pica_surface.gi_sources.z
                                                                   : pica_surface.gi_sources.x);
            }
        }
    } else if (pbr_native_direct) {
        // The native shader explicitly selected an unlit material.
        pbr_direct = pbr_albedo * pica_surface_shadow(-1);
    }
    pbr_ambient = pica_surface.resolved_ambient.rgb;
    if (pbr_baked_reference) {
        pbr_direct = vec3(0.0);
        pbr_ambient = pica_surface_baked_ambient();
    }
)glsl";
constexpr std::string_view kPbrShadingEnd = R"glsl(
    vec3 pbr_indirect = pbr_ambient *
        pbr_albedo * (1.0 - pbr_metallic) * pbr_ao;
    vec3 pbr_reference = pbr_ambient * pbr_albedo * (1.0 - pbr_metallic);
    if (pica_surface.environment_parameters.w != 0.0) {
        if (!pbr_unlit_reference) {
            float pbr_native_scale = pica_surface.control.x != 0u || pbr_material.native_control.x != 0u
                ? pica_surface.environment_parameters.x : pica_surface.environment_parameters.y;
            pbr_direct *= pbr_native_scale;
            pbr_gi_native *= pbr_native_scale;
        }
        for (int slot = 0; slot < 2; ++slot) {
            if (pica_surface.environment_directions[slot].w <= 0.0 && !(pbr_baked_reference && slot == 0)) continue;
            vec3 diffuse, specular;
            pbr_ggx(pbr_normal, pbr_view,
                pica_surface_unit(pica_surface.environment_directions[slot].xyz),
                pbr_albedo, pbr_f0, pbr_roughness, pbr_metallic, false, diffuse, specular);
            if (pbr_baked_reference)
                pbr_reference += pbr_baked_brdf_reference(pbr_view,
                    pica_surface_unit(pica_surface.environment_directions[slot].xyz),
                    pbr_albedo, pbr_f0, pbr_roughness, pbr_metallic, diffuse, specular) *
                    (slot == 0 ? pica_surface.baked_reference_key.rgb :
                        pica_surface.environment_colors[slot].rgb * pica_surface.environment_directions[slot].w);
            vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view,
                pica_surface.environment_directions[slot].xyz);
            pbr_direct += (diffuse + specular + rim) * pica_surface.environment_colors[slot].rgb *
                pica_surface.environment_directions[slot].w * pica_surface_shadow(8 + slot);
            if (slot == 0)
                pbr_gi_direct += diffuse * pica_surface.environment_colors[0].rgb *
                    pica_surface.environment_directions[0].w * pica_surface_shadow(8) * pica_surface.gi_sources.y;
        }
    }
    for (uint slot = 0u; slot < pica_surface.control.z; ++slot) {
        if (all(equal(pica_surface.scene_directional_colors[slot].rgb, vec3(0.0)))) continue;
        vec3 diffuse, specular;
        pbr_ggx(pbr_normal, pbr_view,
            pica_surface_unit(pica_surface.scene_directions[slot].xyz),
            pbr_albedo, pbr_f0, pbr_roughness, pbr_metallic, false, diffuse, specular);
        if (pbr_baked_reference)
            pbr_reference += pbr_baked_brdf_reference(pbr_view,
                pica_surface_unit(pica_surface.scene_directions[slot].xyz),
                pbr_albedo, pbr_f0, pbr_roughness, pbr_metallic, diffuse, specular) *
                pica_surface.scene_directional_colors[slot].rgb;
        vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view,
            pica_surface.scene_directions[slot].xyz);
        pbr_direct += (diffuse + specular + rim) * pica_surface.scene_directional_colors[slot].rgb *
            pica_surface_shadow(10 + int(slot));
        pbr_gi_direct += diffuse * pica_surface.scene_directional_colors[slot].rgb *
            pica_surface_shadow(10 + int(slot)) * pica_surface.gi_sources.x;
    }
    if (pbr_baked_reference) {
        // The native TEV result already includes baked lighting in encoded
        // colour space. Apply the relative (shadowed/oriented versus lit-up)
        // response there, then decode once for local PBR lights/emission.
        // Normalizing the same BRDF includes its Fresnel/pi convention and
        // avoids a diffuse/specular gain on the horizontal reference face.
        vec3 total = max(pbr_direct + pbr_indirect, vec3(0.0));
        vec3 response = total / max(pbr_reference, vec3(1.0e-6));
        vec3 calibrated = pbr_srgb_to_linear(pbr_linear_to_srgb(pbr_albedo) * response);
        vec3 scale = calibrated / max(total, vec3(1.0e-6));
        pbr_direct *= scale;
        pbr_indirect *= scale;
        pbr_gi_native *= scale;
        pbr_gi_direct *= scale;
    }
    for (uint slot = 0u; slot < pica_surface.control.y; ++slot) {
        vec3 direction, radiance, diffuse, specular;
        pica_surface_scene_point(int(slot), direction, radiance);
        if (all(equal(radiance, vec3(0.0)))) continue;
        pbr_ggx(pbr_normal, pbr_view, direction, pbr_albedo, pbr_f0,
            pbr_roughness, pbr_metallic, false, diffuse, specular);
        vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view, direction);
        pbr_direct += (diffuse + specular + rim) * radiance;
        pbr_gi_direct += diffuse * radiance * pica_surface.gi_sources.z;
    }
    for (uint slot = 0u; slot < pica_surface_placed_count(); ++slot) {
        vec3 direction, radiance, diffuse, specular;
        pica_surface_placed_light(int(slot), direction, radiance);
        if (all(equal(radiance, vec3(0.0)))) continue;
        pbr_ggx(pbr_normal, pbr_view, direction, pbr_albedo, pbr_f0,
            pbr_roughness, pbr_metallic, false, diffuse, specular);
        vec3 rim = pbr_albedo * pica_surface_rim_weight(pbr_normal, pbr_view, direction);
        pbr_direct += (diffuse + specular + rim) * radiance;
        pbr_gi_direct += diffuse * radiance * pica_surface.gi_sources.z;
    }
    vec3 pbr_gi_radiance = max(pbr_gi_native + pbr_gi_direct, vec3(0.0));
    // A camera-relative virtual source: no albedo, native gain, AO, scene
    // attenuation or shadow multiplier; never part of the GI source above.
    pbr_direct += pica_surface_rim_independent(pbr_normal, pbr_view);
    vec3 pbr_lit = max(pbr_direct + pbr_indirect + pbr_emission, vec3(0.0));
    vec3 pbr_ao_response = clamp(pbr_indirect / max(pbr_lit, vec3(1.0e-6)), 0.0, 1.0);
    float pbr_reflectivity = clamp(max(max(pbr_f0.r, pbr_f0.g), pbr_f0.b), 0.0, 1.0);
    combiner_output.rgb = clamp(pbr_linear_to_srgb(pbr_lit), 0.0, 1.0);
    if (pbr_material.control.z != 0u) {
        pbr_ao_response = vec3(0.0);
        pbr_reflectivity = 0.0;
    }
)glsl";
constexpr std::string_view kPbrDiagnostic = R"glsl(
    if (pbr_material.control.z == 1u)
        pica_color.rgb = pbr_normal * 0.5 + 0.5;
    else if (pbr_material.control.z == 2u)
        pica_color.rgb = vec3(pbr_roughness);
)glsl";

} // namespace

std::string HoistPicaGlslDeclarations(
    std::string_view body, std::initializer_list<std::string_view> names, std::string& hoisted) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kTypes{{
        {"vec2", " = vec2(0.0)"}, {"vec3", " = vec3(0.0)"}, {"float", " = 0.0"},
        {"bool", " = false"}, {"uint", " = 0u"}, {"PbrSeamBlend", ""}}};
    const auto isName = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    std::string out;
    size_t cursor = 0;
    while (cursor < body.size()) {
        const size_t lineEnd = std::min(body.find('\n', cursor), body.size());
        const size_t first = body.find_first_not_of(' ', cursor);
        bool rewritten = false;
        for (const auto& [type, zero] : kTypes) {
            if (first >= lineEnd || body.compare(first, type.size(), type) != 0 ||
                first + type.size() >= body.size() || body[first + type.size()] != ' ')
                continue;
            const size_t nameBegin = first + type.size() + 1U;
            size_t nameEnd = nameBegin;
            while (nameEnd < body.size() && isName(body[nameEnd])) ++nameEnd;
            const auto name = body.substr(nameBegin, nameEnd - nameBegin);
            if (std::find(names.begin(), names.end(), name) == names.end()) break;
            // The statement: declarators separated by top-level commas.
            std::vector<std::string_view> declarators;
            int depth = 0;
            size_t begin = nameBegin, at = nameBegin;
            for (; at < body.size(); ++at) {
                const char c = body[at];
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (depth == 0 && (c == ',' || c == ';')) {
                    declarators.push_back(body.substr(begin, at - begin));
                    begin = at + 1U;
                    if (c == ';') break;
                }
            }
            if (at == body.size()) throw std::logic_error("unterminated PBR declaration");
            const std::string indent(first - cursor, ' ');
            for (auto declarator : declarators) {
                declarator.remove_prefix(std::min(declarator.find_first_not_of(" \n"), declarator.size()));
                size_t end = 0;
                while (end < declarator.size() && isName(declarator[end])) ++end;
                const auto declared = declarator.substr(0, end);
                if (std::find(names.begin(), names.end(), declared) == names.end())
                    throw std::logic_error("PBR declaration mixes hoisted and local names");
                hoisted += "    " + std::string(type) + " " + std::string(declared) + std::string(zero) + ";\n";
                const size_t equals = declarator.find('=', end);
                if (equals != std::string_view::npos)
                    out += indent + std::string(declared) + " =" + std::string(declarator.substr(equals + 1U)) + ";\n";
            }
            cursor = at + 1U;
            if (cursor < body.size() && body[cursor] == '\n') ++cursor;
            rewritten = true;
            break;
        }
        if (!rewritten) {
            out.append(body.substr(cursor, lineEnd - cursor));
            if (lineEnd < body.size()) out += '\n';
            cursor = lineEnd + 1U;
        }
    }
    return out;
}

PicaPbrUberShader BuildPicaPbrUberShader(
    const PicaPbrUberControls& controls, bool nativeLighting, bool primitiveIds) {
    PicaPbrUberShader result;
    result.Declarations = kPbrDeclarations;
    if (primitiveIds) result.Declarations += PicaPbrSeamShaderLibrary();
    if (nativeLighting) result.Declarations += kPbrNativeSpot;
    const auto& on = controls.Enabled;
    auto& hoisted = result.MainDeclarations;

    std::string unlit = "    if (" + on + ") {\n";
    unlit += kPbrUnlitPrimary;
    if (nativeLighting)
        unlit += "    if (" + controls.NativeLighting + ") {" + std::string(kPbrNativeUnlit) + "    }\n";
    unlit += "    pbr_native_coordinate = " + controls.Coordinate + " == 0u ? pica_texcoord0 : " +
             controls.Coordinate + " == 1u ? pica_texcoord1 : pica_texcoord2;\n"
             "    pbr_native_uv = " + controls.Projected +
             " ? vec2(pica_texcoord0.x / pica_texcoord0_w, 1.0 - pica_texcoord0.y / pica_texcoord0_w)\n"
             "        : vec2(pbr_native_coordinate.x, 1.0 - pbr_native_coordinate.y);\n";
    hoisted += "    vec2 pbr_native_coordinate = vec2(0.0);\n    vec2 pbr_native_uv = vec2(0.0);\n";
    unlit += HoistPicaGlslDeclarations(kPbrFrame, {"pbr_geometric_normal", "pbr_view", "pbr_tangent", "pbr_bitangent",
                               "pbr_valid_frame", "pbr_gradient_x", "pbr_gradient_y", "pbr_uv", "pbr_relief"},
                   hoisted);
    unlit += "    if (pbr_relief && any(notEqual(pbr_uv, pbr_native_uv))) {\n"
             "        vec2 pbr_relief_coordinate = " + controls.Projected +
             " ? vec2(pbr_uv.x, 1.0 - pbr_uv.y) * pica_texcoord0_w : vec2(pbr_uv.x, 1.0 - pbr_uv.y);\n"
             "        if (" + controls.Coordinate + " == 0u) pica_texcoord0 = pbr_relief_coordinate;\n"
             "        else if (" + controls.Coordinate + " == 1u) pica_texcoord1 = pbr_relief_coordinate;\n"
             "        else pica_texcoord2 = pbr_relief_coordinate;\n    }\n";
    if (primitiveIds)
        unlit += "    if (" + controls.Seams + ") {" +
                 HoistPicaGlslDeclarations(kPbrSeamInputs, {"pbr_seam", "pbr_seam_channels"}, hoisted) + "    }\n";
    unlit += "    }\n";
    result.UnlitInputs = std::move(unlit);

    std::string shading = "    if (" + on + ") {";
    shading += HoistPicaGlslDeclarations(kPbrShadingNormal, {"pbr_normal"}, hoisted);
    if (primitiveIds) shading += "    if (" + controls.Seams + ") {" + std::string(kPbrSeamShading) + "    }\n";
    shading += HoistPicaGlslDeclarations(kPbrShadingMaterial, {"pbr_roughness", "pbr_albedo"}, hoisted);
    if (primitiveIds) shading += "    if (" + controls.Seams + ") {" + std::string(kPbrSeamEmission) + "    }\n";
    shading += kPbrShadingAccumulators;
    if (nativeLighting) {
        shading += "    bool pbr_baked_reference = !" + controls.NativeLighting +
                   " && pica_surface.baked_reference_up.w != 0.0;\n"
                   "    bool pbr_unlit_reference = pbr_baked_reference || (!" + controls.NativeLighting +
                   " && (pica_surface.control.w & 1u) != 0u && pica_surface.control.x == 0u);\n";
    } else {
        shading += kPbrUnlitReferenceSurface;
    }
    shading += kPbrNativeFlags;
    if (nativeLighting) {
        shading += "    if (" + controls.NativeLighting + ") {";
        shading += kPbrNativeLoopBegin;
        shading += "        if (" + controls.AttenuationLuts + ") {\n";
        shading += kPbrNativeAttenuation;
        shading += "        }\n";
        shading += kPbrNativeLoopEnd;
        shading += "    } else {";
        shading += kPbrSurfaceLoop;
        shading += "    }\n";
    } else {
        shading += kPbrSurfaceLoop;
    }
    shading += HoistPicaGlslDeclarations(kPbrShadingEnd, {"pbr_gi_radiance", "pbr_ao_response", "pbr_reflectivity"}, hoisted);
    shading += "    }\n";
    result.ShadingBody = std::move(shading);
    result.DiagnosticOutput = kPbrDiagnostic;
    return result;
}

std::optional<PicaPbrShader> BuildPicaPbrShader(
    const PicaShaderHookLayout& hooks, const PicaPbrShaderOptions& options) {
    const auto* sample = hooks.TextureSample(options.TextureSlot);
    if (sample == nullptr || sample->Coordinate > 2U ||
        (sample->Operation != PicaTextureCoordinateOperation::NativeVFlip &&
         sample->Operation != PicaTextureCoordinateOperation::ProjectedNativeVFlip)) return std::nullopt;
    const auto coordinate = std::to_string(sample->Coordinate);
    const std::string uv = sample->Operation == PicaTextureCoordinateOperation::ProjectedNativeVFlip
        ? "vec2(pica_texcoord0.x / pica_texcoord0_w, 1.0 - pica_texcoord0.y / pica_texcoord0_w)"
        : "vec2(pica_texcoord" + coordinate + ".x, 1.0 - pica_texcoord" + coordinate + ".y)";
    PicaPbrShader result;
    result.Declarations = kPbrDeclarations;
    if (options.SeamSoftening) result.Declarations += PicaPbrSeamShaderLibrary();
    if (options.NativeFragmentLighting && options.NativeAttenuationLuts) {
        result.Declarations += kPbrNativeSpot;
    }
    // TEV receives unlit color, not the diffuse material coefficient. Diffuse
    // and ambient coefficients belong to their respective lighting terms;
    // an ambient-only material may legitimately have a black diffuse term.
    result.UnlitInputs = kPbrUnlitPrimary;
    if (options.NativeFragmentLighting) {
        result.UnlitInputs += kPbrNativeUnlit;
    }
    const std::string nativeCoordinate = "pica_texcoord" + coordinate;
    result.UnlitInputs += "\n    vec2 pbr_native_coordinate = " + nativeCoordinate + ";\n"
                          "    vec2 pbr_native_uv = " + uv + ";\n";
    result.UnlitInputs += kPbrFrame;
    // Shadow only the typed coordinate used by this material, never edit the
    // canonical shader. The native-alpha replay restores the original input.
    result.UnlitInputs += "    vec2 " + nativeCoordinate + " = pbr_native_coordinate;\n"
                          "    if (pbr_relief && any(notEqual(pbr_uv, pbr_native_uv))) " +
                          nativeCoordinate + " = vec2(pbr_uv.x, 1.0 - pbr_uv.y)";
    if (sample->Operation == PicaTextureCoordinateOperation::ProjectedNativeVFlip)
        result.UnlitInputs += " * pica_texcoord0_w";
    result.UnlitInputs += ";\n";
    result.NativeCoordinates = "        vec2 " + nativeCoordinate + " = pbr_native_coordinate;\n";
    if (options.SeamSoftening) result.UnlitInputs += kPbrSeamInputs;
    result.ShadingBody = kPbrShadingNormal;
    if (options.SeamSoftening) result.ShadingBody += kPbrSeamShading;
    result.ShadingBody += kPbrShadingMaterial;
    if (options.SeamSoftening) result.ShadingBody += kPbrSeamEmission;
    result.ShadingBody += kPbrShadingAccumulators;
    result.ShadingBody += options.NativeFragmentLighting
        ? kPbrUnlitReferenceNative
        : kPbrUnlitReferenceSurface;
    result.ShadingBody += kPbrNativeFlags;
    if (options.NativeFragmentLighting) {
        result.ShadingBody += kPbrNativeLoopBegin;
        if (options.NativeAttenuationLuts) {
            result.ShadingBody += kPbrNativeAttenuation;
        }
        result.ShadingBody += kPbrNativeLoopEnd;
    } else {
        result.ShadingBody += kPbrSurfaceLoop;
    }
    result.ShadingBody += kPbrShadingEnd;
    result.DiagnosticOutput = kPbrDiagnostic;
    return result;
}

} // namespace Fast::Renderer3ds
