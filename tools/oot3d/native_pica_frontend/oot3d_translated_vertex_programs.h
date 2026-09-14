// Generated translated OOT3D title shader source. Do not classify as title-neutral.
// Translator: TriAevum / Citra-Azahar GLSL decompiler (GPL-2.0-or-later).
#pragma once
#include "fast/renderer3ds/pica_vertex_program.h"
namespace Oot3dNativeGame {
inline const Fast::Renderer3ds::PicaTranslatedVertexProgram kTranslatedVertexPrograms[] = {
{0U,true,{305449005829121044ULL,5924619242408287756ULL,1240ULL},{12282991529206199656ULL,3035664946662145040ULL,216ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
    vec4 product = lhs * rhs;
    return mix(product, mix(mix(vec4(0.0), product, isnan(rhs)), product, isnan(lhs)), isnan(product));
}

vec4 get_offset_register(int base_index, int offset) {
    int fixed_offset = offset >= -128 && offset <= 127 ? offset : 0;
    uint index = uint((base_index + fixed_offset) & 0x7F);
    return index < 96u ? uniforms.f[index] : vec4(1.0);
}

bvec2 conditional_code = bvec2(false);
ivec3 address_registers = ivec3(0);
vec4 reg_tmp0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp1 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp2 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp3 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp4 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp5 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp6 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp7 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp8 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp9 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp10 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp11 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp12 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp13 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp14 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp15 = vec4(0.0, 0.0, 0.0, 1.0);

bool sub_0_4096();
bool sub_3_5();
bool sub_5_11();
bool sub_6_8();
bool sub_8_10();
bool sub_14_213();
bool sub_15_44();
bool sub_20_39();
bool sub_29_33();
bool sub_34_38();
bool sub_39_43();
bool sub_44_51();
bool sub_64_75();
bool sub_71_73();
bool sub_73_74();
bool sub_75_76();
bool sub_77_112();
bool sub_89_94();
bool sub_95_100();
bool sub_102_107();
bool sub_109_111();
bool sub_112_116();
bool sub_114_115();
bool sub_122_203();
bool sub_125_130();
bool sub_130_145();
bool sub_131_135();
bool sub_135_144();
bool sub_148_152();
bool sub_152_179();
bool sub_153_156();
bool sub_156_178();
bool sub_169_171();
bool sub_171_176();
bool sub_173_175();
bool sub_183_187();
bool sub_187_202();
bool sub_188_191();
bool sub_191_201();
bool sub_203_206();
bool sub_214_265();
bool sub_223_230();
bool sub_232_234();
bool sub_243_255();
bool sub_255_258();
bool sub_266_275();
bool sub_276_294();
bool sub_279_283();
bool sub_280_282();
bool sub_283_293();
bool sub_284_288();
bool sub_285_287();
bool sub_288_292();
bool sub_289_291();
bool sub_295_299();
bool sub_300_309();
bool sub_302_303();
bool sub_303_304();
bool sub_306_307();
bool sub_307_308();

bool exec_shader() {
    sub_0_4096();
    return true;
}

bool sub_0_4096() {
    // 0: mov
    reg_tmp0.xy = (uniforms.f[89].wwww).xy;
    // 1: cmp
    conditional_code = equal(vec2(uniforms.f[93].xyyy), vec2(reg_tmp0.xyyy));
    // 2: ifc
    if (conditional_code.x) {
        sub_3_5();
    } else {
        sub_5_11();
    }
    // 11: nop
    // 12: end
    return true;
}

bool sub_3_5() {
    // 3: call
    {
        sub_14_213();
    }
    // 4: nop
    return false;
}

bool sub_5_11() {
    // 5: ifc
    if (conditional_code.y) {
        sub_6_8();
    } else {
        sub_8_10();
    }
    // 10: nop
    return false;
}

bool sub_6_8() {
    // 6: call
    {
        sub_14_213();
    }
    // 7: nop
    return false;
}

bool sub_8_10() {
    // 8: call
    {
        sub_214_265();
    }
    // 9: nop
    return false;
}

bool sub_14_213() {
    // 14: ifu
    if ((uniforms.b & 4u) != 0u) {
        sub_15_44();
    } else {
        sub_44_51();
    }
    // 51: mov
    reg_tmp8.w = (uniforms.f[93].yyyy).w;
    // 52: dp3
    reg_tmp0.w = dot(vec3(sanitize_mul(reg_tmp11.xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 53: rsq
    reg_tmp0.w = inversesqrt(reg_tmp0.wwww.x);
    // 54: mul
    reg_tmp11.xyzw = sanitize_mul(reg_tmp11.xyzw, reg_tmp0.wwww);
    // 55: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 56: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 57: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 58: mov
    reg_tmp15.w = (uniforms.f[93].yyyy).w;
    // 59: dp3
    reg_tmp14.x = dot(vec3(sanitize_mul(uniforms.f[4].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 60: dp3
    reg_tmp14.y = dot(vec3(sanitize_mul(uniforms.f[5].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 61: dp3
    reg_tmp14.z = dot(vec3(sanitize_mul(uniforms.f[6].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 62: mov
    pica_output6.xyzw = -reg_tmp15.xyzw;
    // 63: ifu
    if ((uniforms.b & 1024u) != 0u) {
        sub_64_75();
    } else {
        sub_75_76();
    }
    // 76: ifu
    if ((uniforms.b & 512u) != 0u) {
        sub_77_112();
    } else {
        sub_112_116();
    }
    // 116: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 117: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 118: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 119: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 120: mov
    pica_output1.xyzw = reg_tmp10.xyzw;
    // 121: ifu
    if ((uniforms.b & 2u) != 0u) {
        sub_122_203();
    } else {
        sub_203_206();
    }
    // 206: mov
    pica_output2.xy = (reg_tmp3.xyyy).xy;
    // 207: mov
    pica_output2.z = (reg_tmp3.zzzz).z;
    // 208: mov
    pica_output2.w = (uniforms.f[93].yyyy).w;
    // 209: mov
    pica_output3.xy = (reg_tmp4.xyyy).xy;
    // 210: mov
    pica_output3.zw = (uniforms.f[93].yyyy).zw;
    // 211: mov
    pica_output4.xy = (reg_tmp5.xyyy).xy;
    // 212: mov
    pica_output4.zw = (uniforms.f[93].yyyy).zw;
    return false;
}

bool sub_15_44() {
    // 15: mov
    reg_tmp8.xyzw = uniforms.f[93].xxxx;
    // 16: mov
    reg_tmp11.xyzw = uniforms.f[93].xxxx;
    // 17: mul
    reg_tmp15.xyzw = sanitize_mul(uniforms.f[90].xxxx, pica_input0.xyzw);
    // 18: mul
    reg_tmp14.xyzw = sanitize_mul(uniforms.f[90].yyyy, pica_input1.xyzw);
    // 19: ifu
    if ((uniforms.b & 8u) != 0u) {
        sub_20_39();
    } else {
        sub_39_43();
    }
    // 43: nop
    return false;
}

bool sub_20_39() {
    // 20: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input6.xxxx)).xy;
    // 21: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input7.xxxx)).w;
    // 22: call
    {
        sub_266_275();
    }
    // 23: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input6.yyyy)).xy;
    // 24: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input7.yyyy)).w;
    // 25: call
    {
        sub_266_275();
    }
    // 26: mov
    reg_tmp0.xy = (uniforms.f[92].wwww).xy;
    // 27: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 28: ifc
    if (conditional_code.x) {
        sub_29_33();
    }
    // 33: ifc
    if (conditional_code.y) {
        sub_34_38();
    }
    // 38: nop
    return false;
}

bool sub_29_33() {
    // 29: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input6.zzzz)).xy;
    // 30: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input7.zzzz)).w;
    // 31: call
    {
        sub_266_275();
    }
    // 32: nop
    return false;
}

bool sub_34_38() {
    // 34: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input6.wwww)).xy;
    // 35: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input7.wwww)).w;
    // 36: call
    {
        sub_266_275();
    }
    // 37: nop
    return false;
}

bool sub_39_43() {
    // 39: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input6.xxxx)).xy;
    // 40: mov
    reg_tmp1.w = (uniforms.f[93].yyyy).w;
    // 41: call
    {
        sub_266_275();
    }
    // 42: nop
    return false;
}

bool sub_44_51() {
    // 44: dp3
    reg_tmp11.x = dot(vec3(sanitize_mul(uniforms.f[20].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 45: dp3
    reg_tmp11.y = dot(vec3(sanitize_mul(uniforms.f[21].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 46: dp3
    reg_tmp11.z = dot(vec3(sanitize_mul(uniforms.f[22].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 47: dp4
    reg_tmp8.x = dot(sanitize_mul(uniforms.f[20].xyzw, pica_input0.xyzw), vec4(1.0));
    // 48: dp4
    reg_tmp8.y = dot(sanitize_mul(uniforms.f[21].xyzw, pica_input0.xyzw), vec4(1.0));
    // 49: dp4
    reg_tmp8.z = dot(sanitize_mul(uniforms.f[22].xyzw, pica_input0.xyzw), vec4(1.0));
    // 50: nop
    return false;
}

bool sub_64_75() {
    // 64: add
    reg_tmp4.xyzw = uniforms.f[93].yyyy + reg_tmp14.zzzz;
    // 65: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp4.xyzw);
    // 66: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xxxx), vec2(reg_tmp4.xxxx));
    // 67: mov
    pica_output5.w = (uniforms.f[93].xxxx).w;
    // 68: rsq
    reg_tmp4.xyzw = vec4(inversesqrt(reg_tmp4.xxxx.x));
    // 69: mul
    reg_tmp5.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp14.xyzw);
    // 70: ifc
    if (!conditional_code.x) {
        sub_71_73();
    } else {
        sub_73_74();
    }
    // 74: nop
    return false;
}

bool sub_71_73() {
    // 71: rcp
    pica_output5.z = (1.0 / reg_tmp4.xxxx.x);
    // 72: mul
    pica_output5.xy = (sanitize_mul(reg_tmp5.xyzw, reg_tmp4.xyzw)).xy;
    return false;
}

bool sub_73_74() {
    // 73: mov
    pica_output5.xyz = (uniforms.f[93].yxxx).xyz;
    return false;
}

bool sub_75_76() {
    // 75: mov
    pica_output5.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_77_112() {
    // 77: mov
    reg_tmp0.xyzw = uniforms.f[8].xyzw;
    // 78: mov
    reg_tmp1.xyzw = uniforms.f[9].xyzw;
    // 79: dp3
    reg_tmp3.x = dot(vec3(sanitize_mul(-uniforms.f[80].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 80: dp3
    reg_tmp3.y = dot(vec3(sanitize_mul(-uniforms.f[83].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 81: dp3
    reg_tmp3.z = dot(vec3(sanitize_mul(-uniforms.f[86].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 82: mov
    reg_tmp9.xyzw = uniforms.f[93].xxxx;
    // 83: mov
    reg_tmp10.xyzw = uniforms.f[93].xxxx;
    // 84: mov
    reg_tmp2.x = (uniforms.f[80].wwww).x;
    // 85: mov
    reg_tmp2.y = (uniforms.f[83].wwww).y;
    // 86: mov
    reg_tmp2.z = (uniforms.f[86].wwww).z;
    // 87: cmp
    conditional_code = equal(vec2(uniforms.f[93].yyyy), vec2(reg_tmp2.xyyy));
    // 88: ifc
    if (conditional_code.x) {
        sub_89_94();
    }
    // 94: ifc
    if (conditional_code.y) {
        sub_95_100();
    }
    // 100: cmp
    conditional_code = equal(vec2(uniforms.f[93].yyyy), vec2(reg_tmp2.zzzz));
    // 101: ifc
    if (conditional_code.x) {
        sub_102_107();
    }
    // 107: add
    reg_tmp10.xyz = (reg_tmp10.xyzz + reg_tmp9.xyzz).xyz;
    // 108: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_109_111();
    }
    // 111: nop
    return false;
}

bool sub_89_94() {
    // 89: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[81].xyzw, reg_tmp0.xyzw);
    // 90: max
    reg_tmp3.x = (mix(reg_tmp3.xxxx, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.xxxx))).x;
    // 91: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[82].xyzz) + reg_tmp9.xyzz).xyz;
    // 92: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.xxxx, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 93: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_95_100() {
    // 95: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[84].xyzw, reg_tmp0.xyzw);
    // 96: max
    reg_tmp3.y = (mix(reg_tmp3.yyyy, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.yyyy))).y;
    // 97: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[85].xyzz) + reg_tmp9.xyzz).xyz;
    // 98: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.yyyy, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 99: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_102_107() {
    // 102: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[87].xyzw, reg_tmp0.xyzw);
    // 103: max
    reg_tmp3.z = (mix(reg_tmp3.zzzz, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.zzzz))).z;
    // 104: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[88].xyzz) + reg_tmp9.xyzz).xyz;
    // 105: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.zzzz, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 106: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_109_111() {
    // 109: mul
    reg_tmp9.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input2.xyzw);
    // 110: mul
    reg_tmp10.xyzw = sanitize_mul(reg_tmp10.xyzw, reg_tmp9.xyzw);
    return false;
}

bool sub_112_116() {
    // 112: mov
    reg_tmp10.xyzw = uniforms.f[8].xyzw;
    // 113: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_114_115();
    }
    // 115: nop
    return false;
}

bool sub_114_115() {
    // 114: mul
    reg_tmp10.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input2.xyzw);
    return false;
}

bool sub_122_203() {
    // 122: mov
    reg_tmp0.xy = (uniforms.f[92].xxxx).xy;
    // 123: cmp
    conditional_code = equal(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 124: ifc
    if (all(not(conditional_code))) {
        sub_125_130();
    } else {
        sub_130_145();
    }
    // 145: mov
    reg_tmp0.xy = (uniforms.f[92].yyyy).xy;
    // 146: cmp
    conditional_code = equal(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 147: ifc
    if (all(not(conditional_code))) {
        sub_148_152();
    } else {
        sub_152_179();
    }
    // 179: nop
    // 180: mov
    reg_tmp0.xy = (uniforms.f[92].zzzz).xy;
    // 181: cmp
    conditional_code = equal(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 182: ifc
    if (all(not(conditional_code))) {
        sub_183_187();
    } else {
        sub_187_202();
    }
    // 202: nop
    return false;
}

bool sub_125_130() {
    // 125: mov
    reg_tmp1.xy = (uniforms.f[89].xxxx).xy;
    // 126: call
    {
        sub_276_294();
    }
    // 127: dp4
    reg_tmp3.x = dot(sanitize_mul(uniforms.f[10].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 128: dp4
    reg_tmp3.y = dot(sanitize_mul(uniforms.f[11].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 129: mov
    reg_tmp3.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_130_145() {
    // 130: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_131_135();
    } else {
        sub_135_144();
    }
    // 144: nop
    return false;
}

bool sub_131_135() {
    // 131: call
    {
        sub_295_299();
    }
    // 132: dp4
    reg_tmp3.x = dot(sanitize_mul(uniforms.f[10].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 133: dp4
    reg_tmp3.y = dot(sanitize_mul(uniforms.f[11].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 134: mov
    reg_tmp3.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_135_144() {
    // 135: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[76].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 136: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[77].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 137: dp4
    reg_tmp10.z = dot(sanitize_mul(uniforms.f[78].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 138: mov
    reg_tmp10.w = (uniforms.f[93].yyyy).w;
    // 139: dp4
    reg_tmp3.x = dot(sanitize_mul(uniforms.f[10].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 140: dp4
    reg_tmp3.y = dot(sanitize_mul(uniforms.f[11].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 141: dp4
    reg_tmp3.z = dot(sanitize_mul(uniforms.f[12].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 142: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[94].zzzz, reg_tmp3.zzzz)).xy;
    // 143: add
    reg_tmp3.xy = (reg_tmp3.xyyy + reg_tmp1.xyyy).xy;
    return false;
}

bool sub_148_152() {
    // 148: mov
    reg_tmp1.xy = (uniforms.f[89].yyyy).xy;
    // 149: call
    {
        sub_276_294();
    }
    // 150: dp4
    reg_tmp4.x = dot(sanitize_mul(uniforms.f[14].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 151: dp4
    reg_tmp4.y = dot(sanitize_mul(uniforms.f[15].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_152_179() {
    // 152: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_153_156();
    } else {
        sub_156_178();
    }
    // 178: nop
    return false;
}

bool sub_153_156() {
    // 153: call
    {
        sub_295_299();
    }
    // 154: dp4
    reg_tmp4.x = dot(sanitize_mul(uniforms.f[14].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 155: dp4
    reg_tmp4.y = dot(sanitize_mul(uniforms.f[15].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_156_178() {
    // 156: dp3
    reg_tmp10.x = dot(vec3(sanitize_mul(uniforms.f[76].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 157: dp3
    reg_tmp10.y = dot(vec3(sanitize_mul(uniforms.f[77].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 158: dp3
    reg_tmp10.z = dot(vec3(sanitize_mul(uniforms.f[78].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 159: dp3
    reg_tmp4.z = dot(vec3(sanitize_mul(uniforms.f[16].xyzw, reg_tmp10.xyzw)), vec3(1.0));
    // 160: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xyyy), vec2(reg_tmp4.zzzz));
    // 161: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[76].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 162: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[77].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 163: dp4
    reg_tmp10.z = dot(sanitize_mul(uniforms.f[78].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 164: mov
    reg_tmp10.w = (uniforms.f[93].yyyy).w;
    // 165: dp4
    reg_tmp4.x = dot(sanitize_mul(uniforms.f[14].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 166: dp4
    reg_tmp4.y = dot(sanitize_mul(uniforms.f[15].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 167: dp4
    reg_tmp4.z = dot(sanitize_mul(uniforms.f[16].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 168: ifc
    if (conditional_code.x) {
        sub_169_171();
    } else {
        sub_171_176();
    }
    // 176: nop
    // 177: add
    reg_tmp4.xy = (uniforms.f[94].zzzz + reg_tmp4.xyyy).xy;
    return false;
}

bool sub_169_171() {
    // 169: call
    {
        sub_300_309();
    }
    // 170: nop
    return false;
}

bool sub_171_176() {
    // 171: cmp
    conditional_code = lessThan(vec2(uniforms.f[93].xyyy), vec2(reg_tmp4.zzzz));
    // 172: ifc
    if (conditional_code.x) {
        sub_173_175();
    }
    // 175: nop
    return false;
}

bool sub_173_175() {
    // 173: call
    {
        sub_300_309();
    }
    // 174: nop
    return false;
}

bool sub_183_187() {
    // 183: mov
    reg_tmp1.xy = (uniforms.f[89].zzzz).xy;
    // 184: call
    {
        sub_276_294();
    }
    // 185: dp4
    reg_tmp5.x = dot(sanitize_mul(uniforms.f[17].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 186: dp4
    reg_tmp5.y = dot(sanitize_mul(uniforms.f[18].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_187_202() {
    // 187: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_188_191();
    } else {
        sub_191_201();
    }
    // 201: nop
    return false;
}

bool sub_188_191() {
    // 188: call
    {
        sub_295_299();
    }
    // 189: dp4
    reg_tmp5.x = dot(sanitize_mul(uniforms.f[17].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 190: dp4
    reg_tmp5.y = dot(sanitize_mul(uniforms.f[18].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_191_201() {
    // 191: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[76].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 192: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[77].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 193: dp4
    reg_tmp10.z = dot(sanitize_mul(uniforms.f[78].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 194: mov
    reg_tmp10.w = (uniforms.f[93].yyyy).w;
    // 195: dp4
    reg_tmp5.x = dot(sanitize_mul(uniforms.f[17].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 196: dp4
    reg_tmp5.y = dot(sanitize_mul(uniforms.f[18].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 197: dp4
    reg_tmp5.z = dot(sanitize_mul(uniforms.f[19].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 198: rcp
    reg_tmp10.w = (1.0 / reg_tmp5.zzzz.x);
    // 199: mul
    reg_tmp5.xy = (sanitize_mul(reg_tmp5.xyyy, -reg_tmp10.wwww)).xy;
    // 200: add
    reg_tmp5.xy = (uniforms.f[94].zzzz + reg_tmp5.xyyy).xy;
    return false;
}

bool sub_203_206() {
    // 203: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    // 204: mov
    reg_tmp4.xyzw = uniforms.f[93].xxxx;
    // 205: mov
    reg_tmp5.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_214_265() {
    // 214: dp4
    reg_tmp8.x = dot(sanitize_mul(uniforms.f[20].xyzw, pica_input0.xyzw), vec4(1.0));
    // 215: dp4
    reg_tmp8.y = dot(sanitize_mul(uniforms.f[21].xyzw, pica_input0.xyzw), vec4(1.0));
    // 216: dp4
    reg_tmp8.z = dot(sanitize_mul(uniforms.f[22].xyzw, pica_input0.xyzw), vec4(1.0));
    // 217: mov
    reg_tmp8.w = (uniforms.f[93].yyyy).w;
    // 218: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 219: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 220: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 221: mov
    reg_tmp15.w = (uniforms.f[93].yyyy).w;
    // 222: ifu
    if ((uniforms.b & 16u) != 0u) {
        sub_223_230();
    }
    // 230: mov
    reg_tmp10.xyzw = uniforms.f[8].xyzw;
    // 231: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_232_234();
    }
    // 234: nop
    // 235: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 236: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 237: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 238: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 239: mov
    pica_output1.xyzw = reg_tmp10.xyzw;
    // 240: mov
    pica_output6.xyzw = -reg_tmp15.xyzw;
    // 241: mov
    pica_output5.xyzw = uniforms.f[93].xxxx;
    // 242: ifu
    if ((uniforms.b & 2u) != 0u) {
        sub_243_255();
    } else {
        sub_255_258();
    }
    // 258: mov
    pica_output2.xy = (reg_tmp3.xyyy).xy;
    // 259: mov
    pica_output2.z = (reg_tmp3.zzzz).z;
    // 260: mov
    pica_output2.w = (uniforms.f[93].xxxx).w;
    // 261: mov
    pica_output3.xy = (reg_tmp4.xyyy).xy;
    // 262: mov
    pica_output3.zw = (uniforms.f[93].xxxx).zw;
    // 263: mov
    pica_output4.xy = (reg_tmp5.xyyy).xy;
    // 264: mov
    pica_output4.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_223_230() {
    // 223: mov
    reg_tmp0.z = (reg_tmp15.zzzz).z;
    // 224: max
    reg_tmp0.z = (mix(-reg_tmp0.zzzz, reg_tmp0.zzzz, greaterThan(reg_tmp0.zzzz, -reg_tmp0.zzzz))).z;
    // 225: mov
    reg_tmp0.x = (uniforms.f[23].xxxx).x;
    // 226: add
    reg_tmp0.y = (-uniforms.f[23].yyyy + reg_tmp0.zzzz).y;
    // 227: rcp
    reg_tmp0.z = (1.0 / reg_tmp0.zzzz.x);
    // 228: mul
    reg_tmp0.z = (sanitize_mul(reg_tmp0.yyyy, reg_tmp0.zzzz)).z;
    // 229: mad
    reg_tmp15.x = (sanitize_mul(reg_tmp0.xxxx, reg_tmp0.zzzz) + reg_tmp15.xxxx).x;
    return false;
}

bool sub_232_234() {
    // 232: mul
    reg_tmp9.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input2.xyzw);
    // 233: mul
    reg_tmp10.xyzw = sanitize_mul(reg_tmp10.xyzw, reg_tmp9.xyzw);
    return false;
}

bool sub_243_255() {
    // 243: mov
    reg_tmp1.xy = (uniforms.f[89].xxxx).xy;
    // 244: call
    {
        sub_276_294();
    }
    // 245: dp4
    reg_tmp3.x = dot(sanitize_mul(uniforms.f[10].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 246: dp4
    reg_tmp3.y = dot(sanitize_mul(uniforms.f[11].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 247: mov
    reg_tmp1.xy = (uniforms.f[89].yyyy).xy;
    // 248: call
    {
        sub_276_294();
    }
    // 249: dp4
    reg_tmp4.x = dot(sanitize_mul(uniforms.f[14].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 250: dp4
    reg_tmp4.y = dot(sanitize_mul(uniforms.f[15].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 251: mov
    reg_tmp1.xy = (uniforms.f[89].zzzz).xy;
    // 252: call
    {
        sub_276_294();
    }
    // 253: dp4
    reg_tmp5.x = dot(sanitize_mul(uniforms.f[17].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 254: dp4
    reg_tmp5.y = dot(sanitize_mul(uniforms.f[18].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_255_258() {
    // 255: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    // 256: mov
    reg_tmp4.xyzw = uniforms.f[93].xxxx;
    // 257: mov
    reg_tmp5.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_266_275() {
    // 266: mova
    address_registers.xy = ivec2(reg_tmp1.xyyy);
    // 267: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(20, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 268: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(21, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 269: dp4
    reg_tmp3.z = dot(sanitize_mul(get_offset_register(22, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 270: dp3
    reg_tmp4.x = dot(vec3(sanitize_mul(get_offset_register(20, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 271: dp3
    reg_tmp4.y = dot(vec3(sanitize_mul(get_offset_register(21, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 272: dp3
    reg_tmp4.z = dot(vec3(sanitize_mul(get_offset_register(22, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 273: mad
    reg_tmp8.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp3.xyzw) + reg_tmp8.xyzw;
    // 274: mad
    reg_tmp11.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp4.xyzw) + reg_tmp11.xyzw;
    return false;
}

bool sub_276_294() {
    // 276: cmp
    conditional_code = equal(vec2(uniforms.f[93].xyyy), vec2(reg_tmp1.xyyy));
    // 277: mov
    reg_tmp10.xyzw = reg_tmp15.xyzw;
    // 278: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_279_283();
    } else {
        sub_283_293();
    }
    // 293: nop
    return false;
}

bool sub_279_283() {
    // 279: ifu
    if ((uniforms.b & 64u) != 0u) {
        sub_280_282();
    }
    // 282: nop
    return false;
}

bool sub_280_282() {
    // 280: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].xxxx, pica_input3.xyyy)).xy;
    // 281: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_283_293() {
    // 283: ifc
    if (all(bvec2(!conditional_code.x, conditional_code.y))) {
        sub_284_288();
    } else {
        sub_288_292();
    }
    // 292: nop
    return false;
}

bool sub_284_288() {
    // 284: ifu
    if ((uniforms.b & 128u) != 0u) {
        sub_285_287();
    }
    // 287: nop
    return false;
}

bool sub_285_287() {
    // 285: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].yyyy, pica_input4.xyzw)).xy;
    // 286: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_288_292() {
    // 288: ifu
    if ((uniforms.b & 256u) != 0u) {
        sub_289_291();
    }
    // 291: nop
    return false;
}

bool sub_289_291() {
    // 289: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].zzzz, pica_input5.xyzw)).xy;
    // 290: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_295_299() {
    // 295: mov
    reg_tmp1.xy = (uniforms.f[94].zzzz).xy;
    // 296: mov
    reg_tmp1.zw = (uniforms.f[93].xxxx).zw;
    // 297: mad
    reg_tmp10.xyzw = sanitize_mul(reg_tmp14.xyzw, reg_tmp1.xyzw) + reg_tmp1.xyzw;
    // 298: mov
    reg_tmp10.zw = (uniforms.f[93].yyyy).zw;
    return false;
}

bool sub_300_309() {
    // 300: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xxxx), vec2(reg_tmp4.xyyy));
    // 301: ifc
    if (conditional_code.x) {
        sub_302_303();
    } else {
        sub_303_304();
    }
    // 304: nop
    // 305: ifc
    if (conditional_code.y) {
        sub_306_307();
    } else {
        sub_307_308();
    }
    // 308: nop
    return false;
}

bool sub_302_303() {
    // 302: add
    reg_tmp4.x = (-uniforms.f[95].wwww + reg_tmp4.xxxx).x;
    return false;
}

bool sub_303_304() {
    // 303: add
    reg_tmp4.x = (uniforms.f[95].wwww + reg_tmp4.xxxx).x;
    return false;
}

bool sub_306_307() {
    // 306: add
    reg_tmp4.y = (-uniforms.f[95].wwww + reg_tmp4.yyyy).y;
    return false;
}

bool sub_307_308() {
    // 307: add
    reg_tmp4.y = (uniforms.f[95].wwww + reg_tmp4.yyyy).y;
    return false;
}

)PICA_TITLE"},
{0U,true,{14735393177411741859ULL,8658856199077325975ULL,100ULL},{2732082405918681046ULL,13126140387055097978ULL,20ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
    vec4 product = lhs * rhs;
    return mix(product, mix(mix(vec4(0.0), product, isnan(rhs)), product, isnan(lhs)), isnan(product));
}

vec4 get_offset_register(int base_index, int offset) {
    int fixed_offset = offset >= -128 && offset <= 127 ? offset : 0;
    uint index = uint((base_index + fixed_offset) & 0x7F);
    return index < 96u ? uniforms.f[index] : vec4(1.0);
}

bvec2 conditional_code = bvec2(false);
ivec3 address_registers = ivec3(0);
vec4 reg_tmp0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp1 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp2 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp3 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp4 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp5 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp6 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp7 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp8 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp9 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp10 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp11 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp12 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp13 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp14 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp15 = vec4(0.0, 0.0, 0.0, 1.0);

bool sub_0_4096();

bool exec_shader() {
    sub_0_4096();
    return true;
}

bool sub_0_4096() {
    // 0: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, pica_input0.xyzw), vec4(1.0));
    // 1: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, pica_input0.xyzw), vec4(1.0));
    // 2: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, pica_input0.xyzw), vec4(1.0));
    // 3: dp4
    reg_tmp15.w = dot(sanitize_mul(uniforms.f[7].xyzw, pica_input0.xyzw), vec4(1.0));
    // 4: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 5: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 6: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 7: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 8: mov
    pica_output1.xyzw = pica_input1.xyzw;
    // 9: mov
    pica_output2.xyzw = pica_input2.xyzw;
    // 10: nop
    // 11: end
    return true;
}

)PICA_TITLE"},
{13U,true,{14735393177411741859ULL,8658856199077325975ULL,100ULL},{2732082405918681046ULL,13126140387055097978ULL,20ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
    vec4 product = lhs * rhs;
    return mix(product, mix(mix(vec4(0.0), product, isnan(rhs)), product, isnan(lhs)), isnan(product));
}

vec4 get_offset_register(int base_index, int offset) {
    int fixed_offset = offset >= -128 && offset <= 127 ? offset : 0;
    uint index = uint((base_index + fixed_offset) & 0x7F);
    return index < 96u ? uniforms.f[index] : vec4(1.0);
}

bvec2 conditional_code = bvec2(false);
ivec3 address_registers = ivec3(0);
vec4 reg_tmp0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp1 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp2 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp3 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp4 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp5 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp6 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp7 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp8 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp9 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp10 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp11 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp12 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp13 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp14 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 reg_tmp15 = vec4(0.0, 0.0, 0.0, 1.0);

bool sub_13_4096();

bool exec_shader() {
    sub_13_4096();
    return true;
}

bool sub_13_4096() {
    // 13: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, pica_input0.xyzw), vec4(1.0));
    // 14: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, pica_input0.xyzw), vec4(1.0));
    // 15: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, pica_input0.xyzw), vec4(1.0));
    // 16: dp4
    reg_tmp15.w = dot(sanitize_mul(uniforms.f[7].xyzw, pica_input0.xyzw), vec4(1.0));
    // 17: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 18: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 19: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 20: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 21: mov
    pica_output1.xyzw = pica_input1.xyzw;
    // 22: nop
    // 23: end
    return true;
}

)PICA_TITLE"},
};
} // namespace Oot3dNativeGame
