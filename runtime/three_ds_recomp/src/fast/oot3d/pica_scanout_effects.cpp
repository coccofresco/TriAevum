#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/oot3d/pica_surface_coordinates.h"
#include "fast/oot3d/pica_guide_diagnostics.h"
#include "fast/oot3d/toon_outline_shader.h"
#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/oot3d/reflection_debug.h"
#include "fast/oot3d/spatial_aa.h"

#include <algorithm>

namespace Fast::Oot3d {

bool ValidatePicaScanoutPolicyInput(
    const PicaScanoutPolicyInput& input) noexcept {
    return input.Width != 0U && input.Height != 0U &&
           IsKnownSceneColorEncoding(input.InputEncoding);
}

PicaScanoutPushConstants BuildPicaScanoutPushConstants(
    const PicaScanoutPolicyInput& input) {
    PicaScanoutPushConstants output;
    output.FlipY = PicaSurfaceCoordinates::FromTransferFlags(input.TransferFlags).FlipY;
    output.AaMode = input.AaMode;
    const bool inputLinear = SceneColorIsLinear(input.InputEncoding);
    output.InputLinear = static_cast<uint32_t>(inputLinear);
    output.EncodeSrgb = static_cast<uint32_t>(
        inputLinear && !input.TargetSrgb);
    const bool effectsBaked = input.EffectsComposited;
    output.Cacao =
        static_cast<uint32_t>(input.CacaoAvailable && !effectsBaked);
    output.Outline = static_cast<uint32_t>(
        input.OutlineAvailable && !effectsBaked);
    output.InvWidth =
        1.0F / static_cast<float>(std::max(input.Width, 1U));
    output.InvHeight =
        1.0F / static_cast<float>(std::max(input.Height, 1U));

    const auto& outline = input.Effects.ToonStyle;
    output.OutlineWidth = ToonOutlineRenderWidth(
        outline.OutlineWidth, input.Width, input.Height);
    output.OutlineSoftness = outline.OutlineSoftness;
    output.OutlineDepthSensitivity = outline.OutlineDepthSensitivity;
    std::copy(outline.OutlineTint.begin(), outline.OutlineTint.end(),
              output.OutlineColor);
    output.OutlineOpacity = outline.OutlineOpacity;
    output.OutlineNormalSensitivity = outline.OutlineNormalSensitivity;

    output.Reflections =
        static_cast<uint32_t>(input.ReflectionsAvailable && !effectsBaked);
    output.ReflectionStrength = input.Effects.ReflectionStrength;
    output.ReflectionMaxDistance = input.Effects.ReflectionMaxDistance;
    output.ReflectionThickness = input.Effects.ReflectionThickness;
    output.ReflectionEdgeFade = input.Effects.ReflectionEdgeFade;
    output.ReflectionMaxSteps = input.Effects.ReflectionMaxSteps;
    output.HiZMipCount = std::max(input.HiZMipCount, 1U);
    output.ReflectionDebug = input.Effects.ReflectionDebugView;
    output.ReflectionRoughnessBias = input.Effects.ReflectionRoughnessBias;
    if (input.Perspective.has_value()) {
        output.ProjectionScaleX = input.Perspective->Projection[0];
        output.ProjectionScaleY = input.Perspective->Projection[5];
        output.ProjectionOffsetX = input.Perspective->Projection[8];
        output.ProjectionOffsetY = input.Perspective->Projection[9];
        output.NearPlane = input.Perspective->NearPlane;
        output.FarPlane = input.Perspective->FarPlane;
    }
    return output;
}

std::string BuildPicaScanoutVertexShader() {
    return R"glsl(
#version 450
layout(location = 0) out vec2 output_uv;
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    output_uv = position * 0.5 + 0.5;
}
)glsl";
}

std::string BuildPicaScanoutFragmentShader(bool separateSampler,
                                          std::optional<int> diagnosticMode) {
    std::string source = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D physical_scanout;
layout(set = 0, binding = 1) uniform sampler2D cacao_visibility;
layout(set = 0, binding = 2) uniform sampler2D scene_depth;
layout(set = 0, binding = 3) uniform sampler2D normal_guide;
layout(set = 0, binding = 4) uniform sampler2D hiz_depth;
layout(set = 0, binding = 5) uniform sampler2D material_guide;
layout(set = 0, binding = 6) uniform sampler2D filtered_reflection;
layout(set = 0, binding = 7) uniform sampler2D ambient_guide;
layout(set = 0, binding = 8) uniform sampler2D transparent_depth_guide;
layout(set = 0, binding = 9) uniform sampler2D fog_guide;
layout(set = 0, binding = 10) uniform sampler2D outline_geometry_guide;
layout(push_constant) uniform ScanoutState {
    uint flip_y; uint aa_mode; uint cacao; uint outline;
    vec2 inverse_size;
    float outline_width;
    float outline_depth_sensitivity;
    vec3 outline_color;
    float outline_opacity;
    float outline_normal_sensitivity;
    float reflection_strength;
    float reflection_max_distance;
    float reflection_thickness;
    float reflection_edge_fade;
    uint reflections;
    uint reflection_max_steps;
    uint hiz_mip_count;
    uint reflection_debug;
    float projection_scale_x;
    float projection_scale_y;
    float projection_offset_x;
    float projection_offset_y;
    float near_plane;
    float far_plane;
    float reflection_roughness_bias;
    float reflection_normal_reject;
    uint input_linear;
    uint encode_srgb;
    float outline_softness;
} scanout;
layout(location = 0) in vec2 output_uv;
layout(location = 0) out vec4 output_color;

vec3 oot3d_srgb_to_linear(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.04045));
    vec3 lower=value/12.92;
    vec3 higher=pow((value+0.055)/1.055,vec3(2.4));
    return mix(higher,lower,cutoff);
}

vec3 oot3d_linear_to_srgb(vec3 value) {
    value=max(value,vec3(0.0));
    bvec3 cutoff=lessThanEqual(value,vec3(0.0031308));
    vec3 lower=value*12.92;
    vec3 higher=1.055*pow(value,vec3(1.0/2.4))-0.055;
    return mix(higher,lower,cutoff);
}
)glsl";
    source += R"glsl(
float oot3d_outline_sample_scene_depth(vec2 uv) {
    ivec2 size=textureSize(scene_depth,0);
    return texelFetch(scene_depth,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).r;
}
vec4 oot3d_outline_sample_normal(vec2 uv) {
    ivec2 size=textureSize(normal_guide,0);
    return texelFetch(normal_guide,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0);
}
vec4 oot3d_outline_sample_geometry(vec2 uv) {
    ivec2 size=textureSize(outline_geometry_guide,0);
    return texelFetch(outline_geometry_guide,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0);
}
float oot3d_outline_sample_occlusion(vec2 uv) {
    ivec2 size=textureSize(transparent_depth_guide,0);
    return texelFetch(transparent_depth_guide,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).a;
}
)glsl";
    source += ToonOutlineShaderLibrary();
    source += AmbientOcclusionCompositeShaderLibrary();
    source += ReflectionDebugShaderLibrary();
    source += R"glsl(

vec4 oot3d_compose_at(vec2 uv) {
    vec4 color = texture(physical_scanout, uv);
    if (scanout.cacao != 0u) {
        float cacao_value = texture(cacao_visibility, uv).r;
        float scene_coverage = texture(ambient_guide, uv).a;
        color.rgb *= oot3d_scene_ambient_occlusion_visibility(
            cacao_value, scene_coverage);
    }
    if (scanout.reflections != 0u) {
        vec4 reflection = texture(filtered_reflection, uv);
        if (scanout.reflection_debug != 0u) {
            vec4 material = texture(material_guide, uv);
            color.rgb = oot3d_reflection_debug_color(
                scanout.reflection_debug, material, reflection);
        } else
            color.rgb = mix(color.rgb, reflection.rgb,
                clamp(reflection.a * scanout.reflection_strength, 0.0, 1.0));
    }
    if (scanout.outline != 0u) {
    float edge = oot3d_toon_outline_edge(
        uv, scanout.inverse_size, scanout.outline_width,
        scanout.outline_depth_sensitivity, scanout.outline_normal_sensitivity,
        scanout.outline_softness) * scanout.outline_opacity;
    vec4 fog=texture(fog_guide,uv);
    vec3 native_tint=mix(fog.rgb,scanout.outline_color,fog.a);
    vec3 outline_color=scanout.input_linear!=0u
        ? oot3d_srgb_to_linear(native_tint) : native_tint;
    color.rgb = mix(color.rgb, outline_color, edge);
    }
    return color;
}
)glsl";
    source += BuildSpatialAaShaderLibrary();
    source += EffectSurfaceCoordinateShaderLibrary;
    source += PicaGuideDiagnosticShaderLibrary(
        diagnosticMode.value_or(PicaGuideDiagnosticMode()));
    source += R"glsl(

void main() {
    vec2 source_uv = vec2(1.0 - output_uv.y, output_uv.x);
    if (scanout.flip_y != 0u) source_uv.y = 1.0 - source_uv.y;
    if(oot3d_guide_diagnostic_mode!=0 && (scanout.cacao!=0u || scanout.outline!=0u)) {
        output_color=vec4(oot3d_guide_diagnostic(source_uv),1.0);
        return;
    }
    vec4 center = oot3d_compose_at(source_uv);
    if (scanout.aa_mode == 1u) output_color = oot3d_fxaa(source_uv, center);
    else output_color = center;
    if(scanout.encode_srgb!=0u)
        output_color.rgb=oot3d_linear_to_srgb(output_color.rgb);
}
)glsl";
    if (separateSampler) {
        static constexpr std::array<const char*, 11> names{ "physical_scanout",
                                                            "cacao_visibility",
                                                            "scene_depth",
                                                            "normal_guide",
                                                            "hiz_depth",
                                                            "material_guide",
                                                            "filtered_reflection",
                                                            "ambient_guide",
                                                            "transparent_depth_guide",
                                                            "fog_guide",
                                                            "outline_geometry_guide" };
        for (const char* name : names) {
            const std::string combined =
                "uniform sampler2D " + std::string(name) + ";";
            const std::string texture =
                "uniform texture2D " + std::string(name) + ";";
            source.replace(source.find(combined), combined.size(), texture);
            for (const std::string operation : {"texture", "textureSize", "texelFetch"}) {
                const std::string sample = operation + "(" + name + ",";
                const std::string separate = operation + "(sampler2D(" + name + ", scanout_sampler),";
                size_t offset = 0;
                while ((offset = source.find(sample, offset)) != std::string::npos) {
                    source.replace(offset, sample.size(), separate);
                    offset += separate.size();
                }
            }
        }
        const auto declarationEnd =
            source.find("layout(push_constant)");
        source.insert(declarationEnd, "layout(set = 0, binding = 11) uniform sampler "
                                      "scanout_sampler;\n");
    }
    return source;
}

} // namespace Fast::Oot3d
