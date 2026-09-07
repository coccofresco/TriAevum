#include "fast/oot3d/reflection_ibl.h"
#include "fast/oot3d/pica_surface_coordinates.h"

#include "fast/oot3d/pica_uniform_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fast::Oot3d {
namespace {

std::array<float, 3> ReadColor(std::span<const uint8_t> bytes,
                               size_t offset, bool& valid) {
    std::array<float, 4> value{};
    valid = offset + sizeof(value) <= bytes.size();
    if (!valid) return {};
    std::memcpy(value.data(), bytes.data() + offset, sizeof(value));
    std::array<float, 3> color{};
    for (size_t i = 0; i < color.size(); ++i) {
        valid &= std::isfinite(value[i]);
        color[i] = std::clamp(value[i], 0.0F, 4.0F);
    }
    return valid ? color : std::array<float, 3>{};
}

uint64_t ProfileSignature(const ReflectionEnvironmentProfile& profile) {
    uint64_t hash = 1469598103934665603ULL;
    const auto append = [&](float value) {
        const uint32_t quantized = static_cast<uint32_t>(
            std::lround(std::clamp(value, 0.0F, 4.0F) * 4096.0F));
        hash ^= quantized;
        hash *= 1099511628211ULL;
    };
    for (float value : profile.Sky) append(value);
    for (float value : profile.Horizon) append(value);
    for (float value : profile.Ground) append(value);
    hash ^= static_cast<uint64_t>(profile.PicaDerived);
    hash *= 1099511628211ULL;
    return hash;
}

ReflectionEnvironmentProfile FallbackProfile() {
    ReflectionEnvironmentProfile profile{};
    profile.Sky = {0.0225F, 0.0300F, 0.0450F};
    profile.Horizon = {0.0300F, 0.0400F, 0.0600F};
    profile.Ground = {0.0120F, 0.0160F, 0.0240F};
    profile.Signature = ProfileSignature(profile);
    return profile;
}

float RadicalInverse(uint32_t bits) {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) |
           ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) |
           ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) |
           ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) |
           ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

float GeometrySchlickGgx(float nDotV, float roughness) {
    const float k = roughness * roughness * 0.5F;
    return nDotV / std::max(nDotV * (1.0F - k) + k, 1.0e-6F);
}

} // namespace

void PicaReflectionEnvironmentAccumulator::Observe(
    uint64_t renderedFrameId, std::string_view fragmentShaderSource,
    std::span<const uint8_t> fragmentUniformBytes,
    bool eligibleWorldDraw) {
    if (!eligibleWorldDraw || renderedFrameId == 0U) return;
    if (mFrameId != renderedFrameId) {
        mFrameId = renderedFrameId;
        mFogSum = {};
        mAmbientSum = {};
        mFogCount = 0;
        mAmbientCount = 0;
    }

    if (fragmentShaderSource.find("fog_factor") !=
        std::string_view::npos) {
        bool valid = false;
        const auto fog = ReadColor(
            fragmentUniformBytes, kPicaPackedFragmentFogColorOffset, valid);
        if (valid) {
            for (size_t i = 0; i < fog.size(); ++i) mFogSum[i] += fog[i];
            ++mFogCount;
        }
    }
    if (fragmentShaderSource.find("lighting_global_ambient") !=
        std::string_view::npos) {
        bool valid = false;
        const auto ambient = ReadColor(
            fragmentUniformBytes,
            kPicaPackedFragmentLightingGlobalAmbientOffset, valid);
        if (valid) {
            for (size_t i = 0; i < ambient.size(); ++i)
                mAmbientSum[i] += ambient[i];
            ++mAmbientCount;
        }
    }
}

ReflectionEnvironmentProfile
PicaReflectionEnvironmentAccumulator::Resolve(uint64_t renderedFrameId) {
    if (mFrameId != renderedFrameId ||
        (mFogCount == 0U && mAmbientCount == 0U)) {
        return mHasLastProfile ? mLastProfile : FallbackProfile();
    }

    ReflectionEnvironmentProfile profile{};
    std::array<float, 3> fog{};
    std::array<float, 3> ambient{};
    for (size_t i = 0; i < 3; ++i) {
        fog[i] = mFogCount != 0U
            ? static_cast<float>(mFogSum[i] / mFogCount)
            : 0.0F;
        ambient[i] = mAmbientCount != 0U
            ? static_cast<float>(mAmbientSum[i] / mAmbientCount)
            : fog[i] * 0.25F;
    }
    for (size_t i = 0; i < 3; ++i) {
        profile.Horizon[i] = mFogCount != 0U ? fog[i] : ambient[i];
        profile.Sky[i] =
            profile.Horizon[i] * 0.75F + ambient[i] * 0.25F;
        profile.Ground[i] =
            ambient[i] * 0.65F + profile.Horizon[i] * 0.10F;
    }
    profile.ObservationCount = mFogCount + mAmbientCount;
    profile.PicaDerived = true;
    profile.Signature = ProfileSignature(profile);
    mLastProfile = profile;
    mHasLastProfile = true;
    return profile;
}

void PicaReflectionEnvironmentAccumulator::Reset() {
    *this = {};
}

IntegratedBrdf IntegrateReflectionBrdf(
    float nDotV, float perceptualRoughness, uint32_t sampleCount) {
    nDotV = std::clamp(nDotV, 1.0e-4F, 1.0F);
    const float roughness =
        std::clamp(perceptualRoughness, 0.0F, 1.0F);
    sampleCount = std::max(sampleCount, 1U);
    const std::array<float, 3> view{
        std::sqrt(std::max(1.0F - nDotV * nDotV, 0.0F)),
        0.0F, nDotV};
    IntegratedBrdf result{};
    constexpr float pi = 3.14159265358979323846F;
    for (uint32_t i = 0; i < sampleCount; ++i) {
        const float xi0 =
            (static_cast<float>(i) + 0.5F) /
            static_cast<float>(sampleCount);
        const float xi1 = RadicalInverse(i);
        const float alpha = roughness * roughness;
        const float alpha2 = alpha * alpha;
        const float phi = 2.0F * pi * xi0;
        const float cosTheta = std::sqrt(
            std::max((1.0F - xi1) /
                         (1.0F + (alpha2 - 1.0F) * xi1),
                     0.0F));
        const float sinTheta =
            std::sqrt(std::max(1.0F - cosTheta * cosTheta, 0.0F));
        const std::array<float, 3> halfVector{
            std::cos(phi) * sinTheta,
            std::sin(phi) * sinTheta,
            cosTheta};
        const float vDotH = std::clamp(
            view[0] * halfVector[0] +
                view[1] * halfVector[1] +
                view[2] * halfVector[2],
            0.0F, 1.0F);
        const std::array<float, 3> light{
            2.0F * vDotH * halfVector[0] - view[0],
            2.0F * vDotH * halfVector[1] - view[1],
            2.0F * vDotH * halfVector[2] - view[2]};
        const float nDotL = std::max(light[2], 0.0F);
        const float nDotH = std::max(halfVector[2], 0.0F);
        if (nDotL <= 0.0F || nDotH <= 0.0F) continue;
        const float geometry =
            GeometrySchlickGgx(nDotV, roughness) *
            GeometrySchlickGgx(nDotL, roughness);
        const float visibility =
            geometry * vDotH /
            std::max(nDotH * nDotV, 1.0e-6F);
        const float fresnel = std::pow(1.0F - vDotH, 5.0F);
        result.Scale += (1.0F - fresnel) * visibility;
        result.Bias += fresnel * visibility;
    }
    result.Scale /= static_cast<float>(sampleCount);
    result.Bias /= static_cast<float>(sampleCount);
    return result;
}

std::string BuildReflectionEnvironmentComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,rgba16f) uniform writeonly image2DArray environment_output;
layout(push_constant) uniform EnvironmentState {
    vec4 sky;
    vec4 horizon;
    vec4 ground;
    uint extent;
    uint mip_level;
    uint mip_count;
    uint sample_count;
} state;

const float PI=3.14159265358979323846;

float radical_inverse(uint bits) {
    bits=(bits<<16u)|(bits>>16u);
    bits=((bits&0x55555555u)<<1u)|((bits&0xAAAAAAAAu)>>1u);
    bits=((bits&0x33333333u)<<2u)|((bits&0xCCCCCCCCu)>>2u);
    bits=((bits&0x0F0F0F0Fu)<<4u)|((bits&0xF0F0F0F0u)>>4u);
    bits=((bits&0x00FF00FFu)<<8u)|((bits&0xFF00FF00u)>>8u);
    return float(bits)*2.3283064365386963e-10;
}

vec3 cube_direction(uint face,vec2 uv) {
    vec2 p=uv*2.0-1.0;
    if(face==0u) return normalize(vec3(1.0,-p.y,-p.x));
    if(face==1u) return normalize(vec3(-1.0,-p.y,p.x));
    if(face==2u) return normalize(vec3(p.x,1.0,p.y));
    if(face==3u) return normalize(vec3(p.x,-1.0,-p.y));
    if(face==4u) return normalize(vec3(p.x,-p.y,1.0));
    return normalize(vec3(-p.x,-p.y,-1.0));
}

vec3 sample_oot3d_environment(vec3 direction) {
    float up=clamp(direction.y,0.0,1.0);
    float down=clamp(-direction.y,0.0,1.0);
    vec3 upper=mix(state.horizon.rgb,state.sky.rgb,
                   smoothstep(0.0,1.0,up));
    return mix(upper,state.ground.rgb,smoothstep(0.0,1.0,down));
}

vec3 importance_sample_ggx(vec2 xi,vec3 normal,float roughness) {
    float alpha=roughness*roughness;
    float alpha2=alpha*alpha;
    float phi=2.0*PI*xi.x;
    float cos_theta=sqrt(max((1.0-xi.y)/
        (1.0+(alpha2-1.0)*xi.y),0.0));
    float sin_theta=sqrt(max(1.0-cos_theta*cos_theta,0.0));
    vec3 half_tangent=vec3(cos(phi)*sin_theta,
                           sin(phi)*sin_theta,cos_theta);
    vec3 up=abs(normal.z)<0.999?vec3(0.0,0.0,1.0):vec3(1.0,0.0,0.0);
    vec3 tangent=normalize(cross(up,normal));
    vec3 bitangent=cross(normal,tangent);
    return normalize(tangent*half_tangent.x+
                     bitangent*half_tangent.y+
                     normal*half_tangent.z);
}

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    uint face=gl_GlobalInvocationID.z;
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))||face>=6u) return;
    vec2 uv=(vec2(pixel)+0.5)/float(state.extent);
    vec3 normal=cube_direction(face,uv);
    float roughness=state.mip_count>1u
        ? float(state.mip_level)/float(state.mip_count-1u):0.0;
    vec3 color=sample_oot3d_environment(normal);
    if(roughness>0.0001){
        vec3 sum=vec3(0.0);
        float weight=0.0;
        uint count=max(state.sample_count,1u);
        for(uint i=0u;i<count;++i){
            vec2 xi=vec2((float(i)+0.5)/float(count),radical_inverse(i));
            vec3 half_vector=importance_sample_ggx(xi,normal,roughness);
            vec3 light=normalize(reflect(-normal,half_vector));
            float n_dot_l=max(dot(normal,light),0.0);
            if(n_dot_l>0.0){
                sum+=sample_oot3d_environment(light)*n_dot_l;
                weight+=n_dot_l;
            }
        }
        color=sum/max(weight,0.0001);
    }
    imageStore(environment_output,ivec3(pixel,int(face)),vec4(color,1.0));
}
)glsl";
}

std::string BuildReflectionBrdfComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,rg16f) uniform writeonly image2D brdf_output;
layout(push_constant) uniform BrdfState {
    uvec2 extent;
    uint sample_count;
    uint reserved;
} state;

const float PI=3.14159265358979323846;

float radical_inverse(uint bits) {
    bits=(bits<<16u)|(bits>>16u);
    bits=((bits&0x55555555u)<<1u)|((bits&0xAAAAAAAAu)>>1u);
    bits=((bits&0x33333333u)<<2u)|((bits&0xCCCCCCCCu)>>2u);
    bits=((bits&0x0F0F0F0Fu)<<4u)|((bits&0xF0F0F0F0u)>>4u);
    bits=((bits&0x00FF00FFu)<<8u)|((bits&0xFF00FF00u)>>8u);
    return float(bits)*2.3283064365386963e-10;
}

vec3 importance_sample_ggx(vec2 xi,float roughness) {
    float alpha=roughness*roughness;
    float alpha2=alpha*alpha;
    float phi=2.0*PI*xi.x;
    float cos_theta=sqrt(max((1.0-xi.y)/
        (1.0+(alpha2-1.0)*xi.y),0.0));
    float sin_theta=sqrt(max(1.0-cos_theta*cos_theta,0.0));
    return vec3(cos(phi)*sin_theta,sin(phi)*sin_theta,cos_theta);
}

float geometry_schlick_ggx(float n_dot_v,float roughness) {
    float k=roughness*roughness*0.5;
    return n_dot_v/max(n_dot_v*(1.0-k)+k,0.000001);
}

vec2 integrate_brdf(float n_dot_v,float roughness) {
    vec3 view=vec3(sqrt(max(1.0-n_dot_v*n_dot_v,0.0)),0.0,n_dot_v);
    vec2 result=vec2(0.0);
    uint count=max(state.sample_count,1u);
    for(uint i=0u;i<count;++i){
        vec2 xi=vec2((float(i)+0.5)/float(count),radical_inverse(i));
        vec3 half_vector=importance_sample_ggx(xi,roughness);
        vec3 light=normalize(2.0*dot(view,half_vector)*half_vector-view);
        float n_dot_l=max(light.z,0.0);
        float n_dot_h=max(half_vector.z,0.0);
        float v_dot_h=max(dot(view,half_vector),0.0);
        if(n_dot_l>0.0&&n_dot_h>0.0){
            float geometry=geometry_schlick_ggx(n_dot_v,roughness)*
                           geometry_schlick_ggx(n_dot_l,roughness);
            float visibility=geometry*v_dot_h/
                max(n_dot_h*n_dot_v,0.000001);
            float fresnel=pow(1.0-v_dot_h,5.0);
            result+=vec2((1.0-fresnel)*visibility,
                         fresnel*visibility);
        }
    }
    return result/float(count);
}

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent);
    imageStore(brdf_output,pixel,vec4(integrate_brdf(
        max(uv.x,0.0001),clamp(1.0-uv.y,0.0,1.0)),0.0,1.0));
}
)glsl";
}

std::string BuildReflectionMaterialResolveComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D reflection_radiance;
layout(set=0,binding=1) uniform texture2D normal_guide;
layout(set=0,binding=2) uniform texture2D material_guide;
layout(set=0,binding=3) uniform texture2D brdf_lut;
layout(set=0,binding=4,rgba16f) uniform writeonly image2D resolved_reflection;
layout(set=0,binding=5) uniform sampler resolve_sampler;
layout(push_constant) uniform ResolveState {
    mat4 inverse_projection;
    uvec2 extent;
    float roughness_bias;
    uint surface_orientation;
} state;
)glsl" + std::string(EffectSurfaceCoordinateShaderLibrary) + R"glsl(

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.extent)))) return;
    vec2 uv=(vec2(pixel)+0.5)/vec2(state.extent);
    vec4 radiance=texture(sampler2D(
        reflection_radiance,resolve_sampler),uv);
    vec4 material=texture(sampler2D(material_guide,resolve_sampler),uv);
    vec3 normal=normalize(texture(sampler2D(
        normal_guide,resolve_sampler),uv).xyz*2.0-1.0);
    vec2 ndc=oot3d_surface_view_ndc(uv,state.surface_orientation);
    vec4 view_h=state.inverse_projection*vec4(ndc,1.0,1.0);
    vec3 view_direction=normalize(view_h.xyz/max(abs(view_h.w),0.000001));
    float n_dot_v=clamp(dot(normal,-view_direction),0.0001,1.0);
    float roughness=clamp(material.g+state.roughness_bias,0.0,1.0);
    vec2 brdf=texture(sampler2D(brdf_lut,resolve_sampler),
                      vec2(n_dot_v,1.0-roughness)).rg;
    float coverage=step(0.5,material.b);
    float reflectivity=clamp(material.r,0.0,1.0);
    float weight=coverage*reflectivity*
                 clamp(brdf.x+brdf.y,0.0,1.0);
    imageStore(resolved_reflection,pixel,
               vec4(max(radiance.rgb,vec3(0.0)),weight));
}
)glsl";
}

} // namespace Fast::Oot3d
