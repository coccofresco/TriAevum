#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace Fast::Renderer3ds {
// std140: two register vectors and 896 native packed LUT words.
struct alignas(16) PicaProcTexProgram {
    std::array<std::array<uint32_t, 4>, 2> Registers{};
    std::array<std::array<uint32_t, 4>, 224> Lut{};
};
static_assert(sizeof(PicaProcTexProgram) == 3616);
inline constexpr std::string_view PicaProcTexDeclaration = R"glsl(
struct PicaProcTexProgram { uvec4 registers[2]; uvec4 lut[224]; };
)glsl";
inline constexpr std::string_view PicaProcTexCode = R"glsl(
#define PT fragment_uniforms.proctex_program
uint pt_word(int i) { return PT.lut[i >> 2][i & 3]; }
// Correctly rounded n/(2^bits-1), bits=8 or 12. The binary fraction repeats
// the native word; normalize that pattern and round its 24-bit significand.
// Unlike a GPU reciprocal multiply, this matches CPU-decoded LUT literals.
float pt_unorm(uint n, uint bits) {
    if(n==0u) return 0.0;
    uint leading=uint(findMSB(n));
    uint pattern=n<<(31u-leading);
    pattern|=pattern>>bits;
    pattern|=pattern>>(2u*bits);
    uint significand=(pattern>>8u)+((pattern>>7u)&1u);
    return uintBitsToFloat(((leading+127u-bits)<<23u)+(significand-8388608u));
}
float pt_float16(uint raw) {
    raw &= 65535u;
    uint e = (raw >> 10u) & 31u;
    uint bits = (raw & 32768u) << 16u;
    if ((raw & 32767u) != 0u)
        bits |= ((e == 31u ? 255u : e + 112u) << 23u) | ((raw & 1023u) << 13u);
    float v = uintBitsToFloat(bits);
    // Match the canonical emitter's treatment of non-finite native constants.
    return isnan(v) || isinf(v) ? (v < 0.0 ? -3.402823466e+38 : 3.402823466e+38) : v;
}
float pt_value(int offset, float coord) {
    coord *= 128.0;
    float index = clamp(floor(coord), 0.0, 127.0);
    uint raw = pt_word(offset + int(index));
    int delta = int((raw >> 12u) & 4095u);
    if (delta >= 2048) delta -= 4096;
    float difference=pt_unorm(uint(abs(delta)),12u)*(delta<0?-1.0:1.0);
    return clamp(pt_unorm(raw & 4095u,12u) + difference * (coord-index), 0.0, 1.0);
}
vec4 pt_color(uint raw) {
    return vec4(pt_unorm(raw & 255u,8u), pt_unorm((raw >> 8u) & 255u,8u),
                pt_unorm((raw >> 16u) & 255u,8u), pt_unorm(raw >> 24u,8u));
}
vec4 pt_difference(uint raw) {
    ivec4 v = ivec4(raw & 255u, (raw >> 8u) & 255u, (raw >> 16u) & 255u, raw >> 24u);
    for (int i=0;i<4;++i) if (v[i]>=128) v[i]-=256;
    return vec4(v) * (2.0 / 255.0);
}
float pt_shift(float coord, uint mode, uint clamp_mode) {
    if (mode == 0u) return 0.0;
    return (clamp_mode == 3u ? 1.0 : 0.5) * float(((int(coord) + (mode == 2u ? 1 : 0)) / 2) % 2);
}
float pt_clamp(float v, uint mode) {
    switch(mode) {
    case 0u: return v > 1.0 ? 0.0 : v;
    case 1u: return min(v,1.0);
    case 2u: return fract(v);
    case 3u: return int(v)%2 == 0 ? fract(v) : 1.0-fract(v);
    case 4u: return v > 0.5 ? 1.0 : 0.0;
    }
    return 0.0;
}
float pt_combine(float u, float v, uint mode) {
    switch(mode) {
    case 0u: return u;
    case 1u: return u*u;
    case 2u: return v;
    case 3u: return v*v;
    case 4u: return (u+v)*0.5;
    case 5u: return (u*u+v*v)*0.5;
    case 6u: return min(sqrt(u*u+v*v),1.0);
    case 7u: return min(u,v);
    case 8u: return max(u,v);
    case 9u: return min(((u+v)*0.5+sqrt(u*u+v*v))*0.5,1.0);
    }
    return 0.0;
}
int pt_rand1(int v) {
    const int table[16]=int[16](0,4,10,8,4,9,7,12,5,15,13,14,11,15,2,11);
    return (((v%9+2)*3)&15)^table[(v/9)&15];
}
float pt_rand2(vec2 p) {
    const int table[16]=int[16](10,2,15,8,0,7,4,5,5,13,2,6,13,9,3,14);
    int u=pt_rand1(int(p.x)), v=pt_rand1(int(p.y));
    v+=((u&3)==1)?4:0; v^=(u&1)*6; v+=10+u; v&=15; v^=table[u];
    return -1.0+float(v)*(2.0/15.0);
}
float pt_noise(vec2 x) {
    uint f=PT.registers[1].x;
    vec2 frequency=vec2(pt_float16(f),pt_float16(f>>16u));
    vec2 phase=vec2(pt_float16(PT.registers[0].z>>16u),pt_float16(PT.registers[0].w>>16u));
    vec2 grid=9.0*frequency*abs(x+phase), point=floor(grid), t=grid-point;
    float g0=pt_rand2(point)*(t.x+t.y);
    float g1=pt_rand2(point+vec2(1,0))*(t.x+t.y-1.0);
    float g2=pt_rand2(point+vec2(0,1))*(t.x+t.y-1.0);
    float g3=pt_rand2(point+vec2(1,1))*(t.x+t.y-2.0);
    return mix(mix(g0,g1,pt_value(0,t.x)),mix(g2,g3,pt_value(0,t.x)),pt_value(0,t.y));
}
vec4 pt_sample_color(float coord, int level) {
    uint cfg=PT.registers[1].y, offsets=PT.registers[1].z;
    const int tail[4]=int[4](240,248,252,254);
    int width=int((cfg>>11u)&255u)>>level;
    int offset=level<4 ? int((offsets>>uint(level*8))&255u) : tail[level-4];
    coord*=float(width-1);
    if ((cfg&1u)!=0u) {
        int i=int(coord)+offset;
        return pt_color(pt_word(384+i))+fract(coord)*pt_difference(pt_word(640+i));
    }
    return pt_color(pt_word(384+int(round(coord+float(offset)))));
}
// Explicit derivatives also allow compute-based equation differential tests.
vec4 pica_proctex_evaluate(vec2 coordinates, vec2 duv) {
    uint cfg=PT.registers[0].y, lut=PT.registers[1].y;
    vec2 uv=abs(coordinates);
    float bias=pt_float16(((cfg>>20u)&255u)|(((lut>>19u)&255u)<<8u));
    float lod=log2(abs(float((lut>>11u)&255u)*bias)*(duv.x+duv.y));
    if (bias==0.0) lod=0.0;
    lod=clamp(lod,float((lut>>3u)&15u),float(min((lut>>7u)&15u,7u)));
    float us=pt_shift(uv.y,(cfg>>16u)&3u,cfg&7u);
    float vs=pt_shift(uv.x,(cfg>>18u)&3u,(cfg>>3u)&7u);
    if ((cfg&32768u)!=0u) {
        ivec2 amp=ivec2(PT.registers[0].z&65535u,PT.registers[0].w&65535u);
        for(int i=0;i<2;++i) if(amp[i]>=32768) amp[i]-=65536;
        uv=abs(uv+vec2(amp)/4095.0*pt_noise(uv));
    }
    float u=pt_clamp(uv.x+us,cfg&7u), v=pt_clamp(uv.y+vs,(cfg>>3u)&7u);
    float coord=pt_value(128,pt_combine(u,v,(cfg>>6u)&15u));
    uint filter_mode=lut&7u;
    vec4 color;
    if (filter_mode<2u) color=pt_sample_color(coord,0);
    else if(filter_mode<4u) color=pt_sample_color(coord,int(round(lod)));
    else {
        int level=int(lod);
        color=pt_sample_color(coord,level);
        // At an exact endpoint no next mip is sampled (level 8 is not a PICA mip).
        if(fract(lod)!=0.0) color=mix(color,pt_sample_color(coord,level+1),fract(lod));
    }
    if((cfg&16384u)!=0u) color.a=pt_value(256,pt_combine(u,v,(cfg>>10u)&15u));
    return color;
}
#undef PT
)glsl";
inline constexpr std::string_view PicaProcTexFragmentSampler = R"glsl(
vec4 pica_sample_proctex() {
    if ((fragment_uniforms.proctex_program.registers[0].x & 1024u)==0u) return vec4(0.0);
    uint coord=(fragment_uniforms.proctex_program.registers[0].x>>8u)&3u;
    vec2 uv=coord==1u?pica_texcoord1:(coord==2u?pica_texcoord2:pica_texcoord0);
    return pica_proctex_evaluate(uv,max(abs(dFdx(abs(uv))),abs(dFdy(abs(uv)))));
}
)glsl";
} // namespace Fast::Renderer3ds
