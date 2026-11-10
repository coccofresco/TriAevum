// Generated translated MM3D title shader source. Do not classify as title-neutral.
// Translator: TriAevum / Citra-Azahar GLSL decompiler (GPL-2.0-or-later).
#pragma once
#include "fast/renderer3ds/pica_vertex_program.h"
namespace Oot3dNativeGame {
inline const Fast::Renderer3ds::PicaTranslatedVertexProgram kTranslatedVertexPrograms[] = {
{0U,true,{279267132898765555ULL,13836317292759408199ULL,100ULL},{11885327606956728864ULL,3968505935597988748ULL,20ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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
    // 8: mul
    pica_output1.xyzw = sanitize_mul(uniforms.f[8].xyzw, pica_input1.xyzw);
    // 9: mov
    pica_output2.xyzw = pica_input2.xyzw;
    // 10: nop
    // 11: end
    return true;
}

)PICA_TITLE"},
{13U,true,{279267132898765555ULL,13836317292759408199ULL,100ULL},{11885327606956728864ULL,3968505935597988748ULL,20ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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
    // 21: mul
    pica_output1.xyzw = sanitize_mul(uniforms.f[8].xyzw, pica_input1.xyzw);
    // 22: nop
    // 23: end
    return true;
}

)PICA_TITLE"},
{0U,true,{674750701693317135ULL,7075814500449206703ULL,16ULL},{16798926939177180717ULL,5036626779893845465ULL,4ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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
    // 0: mov
    pica_output0.xyzw = pica_input0.xyzw;
    // 1: nop
    // 2: end
    return true;
}

)PICA_TITLE"},
{0U,true,{16409897063030600279ULL,10771426951278110499ULL,2028ULL},{16118985823656470646ULL,983198199795142074ULL,300ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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
bool sub_14_175();
bool sub_15_25();
bool sub_18_21();
bool sub_21_24();
bool sub_25_44();
bool sub_36_41();
bool sub_41_43();
bool sub_60_78();
bool sub_61_66();
bool sub_66_77();
bool sub_73_75();
bool sub_75_76();
bool sub_78_79();
bool sub_80_122();
bool sub_92_97();
bool sub_98_103();
bool sub_105_110();
bool sub_111_116();
bool sub_119_121();
bool sub_122_126();
bool sub_124_125();
bool sub_132_166();
bool sub_135_139();
bool sub_139_140();
bool sub_147_151();
bool sub_151_152();
bool sub_158_162();
bool sub_162_163();
bool sub_166_174();
bool sub_176_228();
bool sub_185_192();
bool sub_194_196();
bool sub_205_218();
bool sub_218_221();
bool sub_229_270();
bool sub_260_262();
bool sub_262_263();
bool sub_271_298();
bool sub_274_293();
bool sub_283_287();
bool sub_288_292();
bool sub_293_297();
bool sub_299_327();
bool sub_303_322();
bool sub_312_316();
bool sub_317_321();
bool sub_322_326();
bool sub_328_340();
bool sub_341_358();
bool sub_359_407();
bool sub_364_379();
bool sub_366_374();
bool sub_374_378();
bool sub_379_406();
bool sub_380_384();
bool sub_384_405();
bool sub_397_399();
bool sub_399_403();
bool sub_400_402();
bool sub_408_426();
bool sub_411_415();
bool sub_412_414();
bool sub_415_425();
bool sub_416_420();
bool sub_417_419();
bool sub_420_424();
bool sub_421_423();
bool sub_427_431();
bool sub_432_441();
bool sub_434_435();
bool sub_435_436();
bool sub_438_439();
bool sub_439_440();
bool sub_442_507();
bool sub_470_490();
bool sub_471_476();
bool sub_476_489();
bool sub_480_483();
bool sub_483_488();
bool sub_490_503();
bool sub_491_496();
bool sub_496_502();

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
        sub_14_175();
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
        sub_229_270();
    }
    // 7: nop
    return false;
}

bool sub_8_10() {
    // 8: call
    {
        sub_176_228();
    }
    // 9: nop
    return false;
}

bool sub_14_175() {
    // 14: ifu
    if ((uniforms.b & 4u) != 0u) {
        sub_15_25();
    } else {
        sub_25_44();
    }
    // 44: mov
    reg_tmp8.w = (uniforms.f[93].yyyy).w;
    // 45: dp3
    reg_tmp0.w = dot(vec3(sanitize_mul(reg_tmp11.xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 46: dp3
    reg_tmp1.w = dot(vec3(sanitize_mul(reg_tmp12.xyzw, reg_tmp12.xyzw)), vec3(1.0));
    // 47: rsq
    reg_tmp0.w = inversesqrt(reg_tmp0.wwww.x);
    // 48: rsq
    reg_tmp1.w = inversesqrt(reg_tmp1.wwww.x);
    // 49: mul
    reg_tmp11.xyzw = sanitize_mul(reg_tmp11.xyzw, reg_tmp0.wwww);
    // 50: mul
    reg_tmp12.xyzw = sanitize_mul(reg_tmp12.xyzw, reg_tmp1.wwww);
    // 51: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 52: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 53: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 54: mov
    reg_tmp15.w = (uniforms.f[93].yyyy).w;
    // 55: dp3
    reg_tmp14.x = dot(vec3(sanitize_mul(uniforms.f[4].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 56: dp3
    reg_tmp14.y = dot(vec3(sanitize_mul(uniforms.f[5].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 57: dp3
    reg_tmp14.z = dot(vec3(sanitize_mul(uniforms.f[6].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 58: mov
    pica_output6.xyzw = -reg_tmp15.xyzw;
    // 59: ifu
    if ((uniforms.b & 1024u) != 0u) {
        sub_60_78();
    } else {
        sub_78_79();
    }
    // 79: ifu
    if ((uniforms.b & 512u) != 0u) {
        sub_80_122();
    } else {
        sub_122_126();
    }
    // 126: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 127: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 128: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 129: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 130: mov
    pica_output1.xyzw = reg_tmp10.xyzw;
    // 131: ifu
    if ((uniforms.b & 2u) != 0u) {
        sub_132_166();
    } else {
        sub_166_174();
    }
    // 174: nop
    return false;
}

bool sub_15_25() {
    // 15: mov
    reg_tmp8.xyzw = uniforms.f[93].xxxx;
    // 16: mov
    reg_tmp11.xyzw = uniforms.f[93].xxxx;
    // 17: ifu
    if ((uniforms.b & 1u) != 0u) {
        sub_18_21();
    } else {
        sub_21_24();
    }
    // 24: nop
    return false;
}

bool sub_18_21() {
    // 18: mov
    reg_tmp12.xyzw = uniforms.f[93].xxxx;
    // 19: call
    {
        sub_299_327();
    }
    // 20: nop
    return false;
}

bool sub_21_24() {
    // 21: mov
    reg_tmp12.xyzw = uniforms.f[93].yxxx;
    // 22: call
    {
        sub_271_298();
    }
    // 23: nop
    return false;
}

bool sub_25_44() {
    // 25: mul
    reg_tmp15.xyzw = sanitize_mul(uniforms.f[90].xxxx, pica_input0.xyzw);
    // 26: mul
    reg_tmp14.xyzw = sanitize_mul(uniforms.f[90].yyyy, pica_input1.xyzw);
    // 27: mul
    reg_tmp13.xyzw = sanitize_mul(uniforms.f[90].wwww, pica_input2.xyzw);
    // 28: dp3
    reg_tmp11.x = dot(vec3(sanitize_mul(uniforms.f[20].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 29: dp3
    reg_tmp11.y = dot(vec3(sanitize_mul(uniforms.f[21].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 30: dp3
    reg_tmp11.z = dot(vec3(sanitize_mul(uniforms.f[22].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 31: mov
    reg_tmp11.w = (uniforms.f[93].xxxx).w;
    // 32: dp4
    reg_tmp8.x = dot(sanitize_mul(uniforms.f[20].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 33: dp4
    reg_tmp8.y = dot(sanitize_mul(uniforms.f[21].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 34: dp4
    reg_tmp8.z = dot(sanitize_mul(uniforms.f[22].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 35: ifu
    if ((uniforms.b & 1u) != 0u) {
        sub_36_41();
    } else {
        sub_41_43();
    }
    // 43: nop
    return false;
}

bool sub_36_41() {
    // 36: dp3
    reg_tmp12.x = dot(vec3(sanitize_mul(uniforms.f[20].xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 37: dp3
    reg_tmp12.y = dot(vec3(sanitize_mul(uniforms.f[21].xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 38: dp3
    reg_tmp12.z = dot(vec3(sanitize_mul(uniforms.f[22].xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 39: mov
    reg_tmp12.w = (uniforms.f[93].xxxx).w;
    // 40: nop
    return false;
}

bool sub_41_43() {
    // 41: mov
    reg_tmp12.xyzw = uniforms.f[93].yxxx;
    // 42: nop
    return false;
}

bool sub_60_78() {
    // 60: ifu
    if ((uniforms.b & 1u) != 0u) {
        sub_61_66();
    } else {
        sub_66_77();
    }
    // 77: nop
    return false;
}

bool sub_61_66() {
    // 61: dp3
    reg_tmp13.x = dot(vec3(sanitize_mul(uniforms.f[4].xyzw, reg_tmp12.xyzw)), vec3(1.0));
    // 62: dp3
    reg_tmp13.y = dot(vec3(sanitize_mul(uniforms.f[5].xyzw, reg_tmp12.xyzw)), vec3(1.0));
    // 63: dp3
    reg_tmp13.z = dot(vec3(sanitize_mul(uniforms.f[6].xyzw, reg_tmp12.xyzw)), vec3(1.0));
    // 64: call
    {
        sub_442_507();
    }
    // 65: nop
    return false;
}

bool sub_66_77() {
    // 66: add
    reg_tmp4.xyzw = uniforms.f[93].yyyy + reg_tmp14.zzzz;
    // 67: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp4.xyzw);
    // 68: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xxxx), vec2(reg_tmp4.xxxx));
    // 69: mov
    pica_output5.w = (uniforms.f[93].xxxx).w;
    // 70: rsq
    reg_tmp4.xyzw = vec4(inversesqrt(reg_tmp4.xxxx.x));
    // 71: mul
    reg_tmp5.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp14.xyzw);
    // 72: ifc
    if (!conditional_code.x) {
        sub_73_75();
    } else {
        sub_75_76();
    }
    // 76: nop
    return false;
}

bool sub_73_75() {
    // 73: rcp
    pica_output5.z = (1.0 / reg_tmp4.xxxx.x);
    // 74: mul
    pica_output5.xy = (sanitize_mul(reg_tmp5.xyzw, reg_tmp4.xyzw)).xy;
    return false;
}

bool sub_75_76() {
    // 75: mov
    pica_output5.xyz = (uniforms.f[93].yxxx).xyz;
    return false;
}

bool sub_78_79() {
    // 78: mov
    pica_output5.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_80_122() {
    // 80: mov
    reg_tmp0.xyzw = uniforms.f[8].xyzw;
    // 81: mov
    reg_tmp1.xyzw = uniforms.f[9].xyzw;
    // 82: dp3
    reg_tmp3.x = dot(vec3(sanitize_mul(uniforms.f[77].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 83: dp3
    reg_tmp3.y = dot(vec3(sanitize_mul(uniforms.f[80].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 84: dp3
    reg_tmp3.z = dot(vec3(sanitize_mul(uniforms.f[83].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 85: dp3
    reg_tmp3.w = dot(vec3(sanitize_mul(uniforms.f[86].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 86: mul
    reg_tmp5.xyzw = sanitize_mul(uniforms.f[74].xyzw, reg_tmp1.xyzw);
    // 87: mov
    reg_tmp9.xyzw = uniforms.f[93].xxxx;
    // 88: mov
    reg_tmp10.xyzw = uniforms.f[93].xxxx;
    // 89: mov
    reg_tmp2.xyzw = uniforms.f[75].xyzw;
    // 90: cmp
    conditional_code = equal(vec2(uniforms.f[93].yyyy), vec2(reg_tmp2.xyyy));
    // 91: ifc
    if (conditional_code.x) {
        sub_92_97();
    }
    // 97: ifc
    if (conditional_code.y) {
        sub_98_103();
    }
    // 103: cmp
    conditional_code = equal(vec2(uniforms.f[93].yyyy), vec2(reg_tmp2.zwww));
    // 104: ifc
    if (conditional_code.x) {
        sub_105_110();
    }
    // 110: ifc
    if (conditional_code.y) {
        sub_111_116();
    }
    // 116: add
    reg_tmp10.xyz = (reg_tmp10.xyzz + reg_tmp9.xyzz).xyz;
    // 117: add
    reg_tmp10.xyz = (reg_tmp10.xyzz + reg_tmp5.xyzz).xyz;
    // 118: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_119_121();
    }
    // 121: nop
    return false;
}

bool sub_92_97() {
    // 92: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[78].xyzw, reg_tmp0.xyzw);
    // 93: max
    reg_tmp3.x = (mix(reg_tmp3.xxxx, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.xxxx))).x;
    // 94: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[79].xyzz) + reg_tmp9.xyzz).xyz;
    // 95: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.xxxx, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 96: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_98_103() {
    // 98: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[81].xyzw, reg_tmp0.xyzw);
    // 99: max
    reg_tmp3.y = (mix(reg_tmp3.yyyy, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.yyyy))).y;
    // 100: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[82].xyzz) + reg_tmp9.xyzz).xyz;
    // 101: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.yyyy, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 102: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_105_110() {
    // 105: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[84].xyzw, reg_tmp0.xyzw);
    // 106: max
    reg_tmp3.z = (mix(reg_tmp3.zzzz, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.zzzz))).z;
    // 107: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[85].xyzz) + reg_tmp9.xyzz).xyz;
    // 108: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.zzzz, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 109: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_111_116() {
    // 111: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[87].xyzw, reg_tmp0.xyzw);
    // 112: max
    reg_tmp3.w = (mix(reg_tmp3.wwww, uniforms.f[93].xxxx, greaterThan(uniforms.f[93].xxxx, reg_tmp3.wwww))).w;
    // 113: mad
    reg_tmp9.xyz = (sanitize_mul(reg_tmp1.xyzz, uniforms.f[88].xyzz) + reg_tmp9.xyzz).xyz;
    // 114: mad
    reg_tmp10.xyz = (sanitize_mul(reg_tmp3.wwww, reg_tmp4.xyzz) + reg_tmp10.xyzz).xyz;
    // 115: add
    reg_tmp10.w = (reg_tmp10.wwww + reg_tmp4.wwww).w;
    return false;
}

bool sub_119_121() {
    // 119: mul
    reg_tmp9.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input3.xyzw);
    // 120: mul
    reg_tmp10.xyzw = sanitize_mul(reg_tmp10.xyzw, reg_tmp9.xyzw);
    return false;
}

bool sub_122_126() {
    // 122: mov
    reg_tmp10.xyzw = uniforms.f[8].xyzw;
    // 123: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_124_125();
    }
    // 125: nop
    return false;
}

bool sub_124_125() {
    // 124: mul
    reg_tmp10.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input3.xyzw);
    return false;
}

bool sub_132_166() {
    // 132: mov
    reg_tmp0.xy = (uniforms.f[92].xxxx).xy;
    // 133: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[93].yyyy), vec2(reg_tmp0.xyyy));
    // 134: ifc
    if (conditional_code.x) {
        sub_135_139();
    } else {
        sub_139_140();
    }
    // 140: nop
    // 141: mov
    pica_output2.xy = (reg_tmp3.xyyy).xy;
    // 142: mov
    pica_output2.z = (reg_tmp3.zzzz).z;
    // 143: mov
    pica_output2.w = (uniforms.f[93].yyyy).w;
    // 144: mov
    reg_tmp0.xy = (uniforms.f[92].yyyy).xy;
    // 145: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[93].yyyy), vec2(reg_tmp0.xyyy));
    // 146: ifc
    if (conditional_code.x) {
        sub_147_151();
    } else {
        sub_151_152();
    }
    // 152: nop
    // 153: mov
    pica_output3.xy = (reg_tmp3.xyyy).xy;
    // 154: mov
    pica_output3.zw = (uniforms.f[93].yyyy).zw;
    // 155: mov
    reg_tmp0.xy = (uniforms.f[92].zzzz).xy;
    // 156: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[93].yyyy), vec2(reg_tmp0.xyyy));
    // 157: ifc
    if (conditional_code.x) {
        sub_158_162();
    } else {
        sub_162_163();
    }
    // 163: nop
    // 164: mov
    pica_output4.xy = (reg_tmp3.xyyy).xy;
    // 165: mov
    pica_output4.zw = (uniforms.f[93].yyyy).zw;
    return false;
}

bool sub_135_139() {
    // 135: mov
    reg_tmp1.xy = (uniforms.f[89].xxxx).xy;
    // 136: mov
    reg_tmp2.xy = (uniforms.f[93].xxxx).xy;
    // 137: call
    {
        sub_359_407();
    }
    // 138: nop
    return false;
}

bool sub_139_140() {
    // 139: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_147_151() {
    // 147: mov
    reg_tmp1.xy = (uniforms.f[89].yyyy).xy;
    // 148: mov
    reg_tmp2.xy = (uniforms.f[93].yyyy).xy;
    // 149: call
    {
        sub_359_407();
    }
    // 150: nop
    return false;
}

bool sub_151_152() {
    // 151: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_158_162() {
    // 158: mov
    reg_tmp1.xy = (uniforms.f[89].zzzz).xy;
    // 159: mov
    reg_tmp2.xy = (uniforms.f[93].zyyy).xy;
    // 160: call
    {
        sub_359_407();
    }
    // 161: nop
    return false;
}

bool sub_162_163() {
    // 162: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_166_174() {
    // 166: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    // 167: mov
    pica_output2.xy = (reg_tmp3.xyyy).xy;
    // 168: mov
    pica_output2.z = (reg_tmp3.zzzz).z;
    // 169: mov
    pica_output2.w = (uniforms.f[93].yyyy).w;
    // 170: mov
    pica_output3.xy = (reg_tmp3.xyyy).xy;
    // 171: mov
    pica_output3.zw = (uniforms.f[93].yyyy).zw;
    // 172: mov
    pica_output4.xy = (reg_tmp3.xyyy).xy;
    // 173: mov
    pica_output4.zw = (uniforms.f[93].yyyy).zw;
    return false;
}

bool sub_176_228() {
    // 176: dp4
    reg_tmp8.x = dot(sanitize_mul(uniforms.f[20].xyzw, pica_input0.xyzw), vec4(1.0));
    // 177: dp4
    reg_tmp8.y = dot(sanitize_mul(uniforms.f[21].xyzw, pica_input0.xyzw), vec4(1.0));
    // 178: dp4
    reg_tmp8.z = dot(sanitize_mul(uniforms.f[22].xyzw, pica_input0.xyzw), vec4(1.0));
    // 179: mov
    reg_tmp8.w = (uniforms.f[93].yyyy).w;
    // 180: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 181: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 182: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 183: mov
    reg_tmp15.w = (uniforms.f[93].yyyy).w;
    // 184: ifu
    if ((uniforms.b & 16u) != 0u) {
        sub_185_192();
    }
    // 192: mov
    reg_tmp10.xyzw = uniforms.f[8].xyzw;
    // 193: ifu
    if ((uniforms.b & 32u) != 0u) {
        sub_194_196();
    }
    // 196: nop
    // 197: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 198: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 199: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 200: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 201: mov
    pica_output1.xyzw = reg_tmp10.xyzw;
    // 202: mov
    pica_output6.xyzw = -reg_tmp15.xyzw;
    // 203: mov
    pica_output5.xyzw = uniforms.f[93].xxxx;
    // 204: ifu
    if ((uniforms.b & 2u) != 0u) {
        sub_205_218();
    } else {
        sub_218_221();
    }
    // 221: mov
    pica_output2.xy = (reg_tmp3.xyyy).xy;
    // 222: mov
    pica_output2.z = (reg_tmp3.zzzz).z;
    // 223: mov
    pica_output2.w = (uniforms.f[93].xxxx).w;
    // 224: mov
    pica_output3.xy = (reg_tmp4.xyyy).xy;
    // 225: mov
    pica_output3.zw = (uniforms.f[93].xxxx).zw;
    // 226: mov
    pica_output4.xy = (reg_tmp5.xyyy).xy;
    // 227: mov
    pica_output4.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_185_192() {
    // 185: mov
    reg_tmp0.z = (reg_tmp15.zzzz).z;
    // 186: max
    reg_tmp0.z = (mix(-reg_tmp0.zzzz, reg_tmp0.zzzz, greaterThan(reg_tmp0.zzzz, -reg_tmp0.zzzz))).z;
    // 187: mov
    reg_tmp0.x = (uniforms.f[23].xxxx).x;
    // 188: add
    reg_tmp0.y = (-uniforms.f[23].yyyy + reg_tmp0.zzzz).y;
    // 189: rcp
    reg_tmp0.z = (1.0 / reg_tmp0.zzzz.x);
    // 190: mul
    reg_tmp0.z = (sanitize_mul(reg_tmp0.yyyy, reg_tmp0.zzzz)).z;
    // 191: mad
    reg_tmp15.x = (sanitize_mul(reg_tmp0.xxxx, reg_tmp0.zzzz) + reg_tmp15.xxxx).x;
    return false;
}

bool sub_194_196() {
    // 194: mul
    reg_tmp9.xyzw = sanitize_mul(uniforms.f[90].zzzz, pica_input3.xyzw);
    // 195: mul
    reg_tmp10.xyzw = sanitize_mul(reg_tmp10.xyzw, reg_tmp9.xyzw);
    return false;
}

bool sub_205_218() {
    // 205: mov
    reg_tmp1.xy = (uniforms.f[89].xxxx).xy;
    // 206: call
    {
        sub_408_426();
    }
    // 207: dp4
    reg_tmp3.x = dot(sanitize_mul(uniforms.f[10].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 208: dp4
    reg_tmp3.y = dot(sanitize_mul(uniforms.f[11].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 209: mov
    reg_tmp3.z = (uniforms.f[93].xxxx).z;
    // 210: mov
    reg_tmp1.xy = (uniforms.f[89].yyyy).xy;
    // 211: call
    {
        sub_408_426();
    }
    // 212: dp4
    reg_tmp4.x = dot(sanitize_mul(uniforms.f[14].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 213: dp4
    reg_tmp4.y = dot(sanitize_mul(uniforms.f[15].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 214: mov
    reg_tmp1.xy = (uniforms.f[89].zzzz).xy;
    // 215: call
    {
        sub_408_426();
    }
    // 216: dp4
    reg_tmp5.x = dot(sanitize_mul(uniforms.f[17].xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 217: dp4
    reg_tmp5.y = dot(sanitize_mul(uniforms.f[18].xyzw, reg_tmp10.xyzw), vec4(1.0));
    return false;
}

bool sub_218_221() {
    // 218: mov
    reg_tmp3.xyzw = uniforms.f[93].xxxx;
    // 219: mov
    reg_tmp4.xyzw = uniforms.f[93].xxxx;
    // 220: mov
    reg_tmp5.xyzw = uniforms.f[93].xxxx;
    return false;
}

bool sub_229_270() {
    // 229: dp3
    reg_tmp11.x = dot(vec3(sanitize_mul(uniforms.f[20].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 230: dp3
    reg_tmp11.y = dot(vec3(sanitize_mul(uniforms.f[21].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 231: dp3
    reg_tmp11.z = dot(vec3(sanitize_mul(uniforms.f[22].xyzw, pica_input1.xyzw)), vec3(1.0));
    // 232: mov
    reg_tmp11.w = (uniforms.f[93].xxxx).w;
    // 233: dp4
    reg_tmp8.x = dot(sanitize_mul(uniforms.f[20].xyzw, pica_input0.xyzw), vec4(1.0));
    // 234: dp4
    reg_tmp8.y = dot(sanitize_mul(uniforms.f[21].xyzw, pica_input0.xyzw), vec4(1.0));
    // 235: dp4
    reg_tmp8.z = dot(sanitize_mul(uniforms.f[22].xyzw, pica_input0.xyzw), vec4(1.0));
    // 236: mov
    reg_tmp8.w = (uniforms.f[93].yyyy).w;
    // 237: dp3
    reg_tmp0.w = dot(vec3(sanitize_mul(reg_tmp11.xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 238: rsq
    reg_tmp0.w = inversesqrt(reg_tmp0.wwww.x);
    // 239: mul
    reg_tmp11.xyzw = sanitize_mul(reg_tmp11.xyzw, reg_tmp0.wwww);
    // 240: dp4
    reg_tmp15.x = dot(sanitize_mul(uniforms.f[4].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 241: dp4
    reg_tmp15.y = dot(sanitize_mul(uniforms.f[5].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 242: dp4
    reg_tmp15.z = dot(sanitize_mul(uniforms.f[6].xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 243: mov
    reg_tmp15.w = (uniforms.f[93].yyyy).w;
    // 244: dp3
    reg_tmp14.x = dot(vec3(sanitize_mul(uniforms.f[4].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 245: dp3
    reg_tmp14.y = dot(vec3(sanitize_mul(uniforms.f[5].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 246: dp3
    reg_tmp14.z = dot(vec3(sanitize_mul(uniforms.f[6].xyzw, reg_tmp11.xyzw)), vec3(1.0));
    // 247: mov
    reg_tmp14.w = (uniforms.f[93].xxxx).w;
    // 248: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 249: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 250: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 251: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 252: mov
    pica_output6.xyzw = -reg_tmp15.xyzw;
    // 253: add
    reg_tmp4.xyzw = uniforms.f[93].yyyy + reg_tmp14.zzzz;
    // 254: mul
    reg_tmp4.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp4.xyzw);
    // 255: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xxxx), vec2(reg_tmp4.xxxx));
    // 256: mov
    pica_output5.w = (uniforms.f[93].xxxx).w;
    // 257: rsq
    reg_tmp4.xyzw = vec4(inversesqrt(reg_tmp4.xxxx.x));
    // 258: mul
    reg_tmp5.xyzw = sanitize_mul(uniforms.f[94].zzzz, reg_tmp14.xyzw);
    // 259: ifc
    if (!conditional_code.x) {
        sub_260_262();
    } else {
        sub_262_263();
    }
    // 263: nop
    // 264: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[10].xyzw, pica_input4.xyzw), vec4(1.0));
    // 265: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[11].xyzw, pica_input4.xyzw), vec4(1.0));
    // 266: mov
    pica_output2.xyzw = reg_tmp10.xyxy;
    // 267: mov
    pica_output3.xyzw = uniforms.f[93].xxxx;
    // 268: mov
    pica_output4.xyzw = uniforms.f[93].xxxx;
    // 269: mov
    pica_output1.xyzw = uniforms.f[93].yyyy;
    return false;
}

bool sub_260_262() {
    // 260: rcp
    pica_output5.z = (1.0 / reg_tmp4.xxxx.x);
    // 261: mul
    pica_output5.xy = (sanitize_mul(reg_tmp5.xyzw, reg_tmp4.xyzw)).xy;
    return false;
}

bool sub_262_263() {
    // 262: mov
    pica_output5.xyz = (uniforms.f[93].yxxx).xyz;
    return false;
}

bool sub_271_298() {
    // 271: mul
    reg_tmp15.xyzw = sanitize_mul(uniforms.f[90].xxxx, pica_input0.xyzw);
    // 272: mul
    reg_tmp14.xyzw = sanitize_mul(uniforms.f[90].yyyy, pica_input1.xyzw);
    // 273: ifu
    if ((uniforms.b & 8u) != 0u) {
        sub_274_293();
    } else {
        sub_293_297();
    }
    // 297: nop
    return false;
}

bool sub_274_293() {
    // 274: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.xxxx)).xy;
    // 275: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.xxxx)).w;
    // 276: call
    {
        sub_328_340();
    }
    // 277: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.yyyy)).xy;
    // 278: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.yyyy)).w;
    // 279: call
    {
        sub_328_340();
    }
    // 280: mov
    reg_tmp0.xy = (uniforms.f[91].wwww).xy;
    // 281: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 282: ifc
    if (conditional_code.x) {
        sub_283_287();
    }
    // 287: ifc
    if (conditional_code.y) {
        sub_288_292();
    }
    // 292: nop
    return false;
}

bool sub_283_287() {
    // 283: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.zzzz)).xy;
    // 284: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.zzzz)).w;
    // 285: call
    {
        sub_328_340();
    }
    // 286: nop
    return false;
}

bool sub_288_292() {
    // 288: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.wwww)).xy;
    // 289: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.wwww)).w;
    // 290: call
    {
        sub_328_340();
    }
    // 291: nop
    return false;
}

bool sub_293_297() {
    // 293: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.xxxx)).xy;
    // 294: mov
    reg_tmp1.w = (uniforms.f[93].yyyy).w;
    // 295: call
    {
        sub_328_340();
    }
    // 296: nop
    return false;
}

bool sub_299_327() {
    // 299: mul
    reg_tmp15.xyzw = sanitize_mul(uniforms.f[90].xxxx, pica_input0.xyzw);
    // 300: mul
    reg_tmp14.xyzw = sanitize_mul(uniforms.f[90].yyyy, pica_input1.xyzw);
    // 301: mul
    reg_tmp13.xyzw = sanitize_mul(uniforms.f[90].wwww, pica_input2.xyzw);
    // 302: ifu
    if ((uniforms.b & 8u) != 0u) {
        sub_303_322();
    } else {
        sub_322_326();
    }
    // 326: nop
    return false;
}

bool sub_303_322() {
    // 303: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.xxxx)).xy;
    // 304: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.xxxx)).w;
    // 305: call
    {
        sub_341_358();
    }
    // 306: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.yyyy)).xy;
    // 307: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.yyyy)).w;
    // 308: call
    {
        sub_341_358();
    }
    // 309: mov
    reg_tmp0.xy = (uniforms.f[91].wwww).xy;
    // 310: cmp
    conditional_code = lessThanEqual(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 311: ifc
    if (conditional_code.x) {
        sub_312_316();
    }
    // 316: ifc
    if (conditional_code.y) {
        sub_317_321();
    }
    // 321: nop
    return false;
}

bool sub_312_316() {
    // 312: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.zzzz)).xy;
    // 313: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.zzzz)).w;
    // 314: call
    {
        sub_341_358();
    }
    // 315: nop
    return false;
}

bool sub_317_321() {
    // 317: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.wwww)).xy;
    // 318: mul
    reg_tmp1.w = (sanitize_mul(uniforms.f[94].wwww, pica_input8.wwww)).w;
    // 319: call
    {
        sub_341_358();
    }
    // 320: nop
    return false;
}

bool sub_322_326() {
    // 322: mul
    reg_tmp1.xy = (sanitize_mul(uniforms.f[93].wwww, pica_input7.xxxx)).xy;
    // 323: mov
    reg_tmp1.w = (uniforms.f[93].yyyy).w;
    // 324: call
    {
        sub_341_358();
    }
    // 325: nop
    return false;
}

bool sub_328_340() {
    // 328: mova
    address_registers.xy = ivec2(reg_tmp1.xyyy);
    // 329: nop
    // 330: mov
    reg_tmp3.w = (uniforms.f[93].yyyy).w;
    // 331: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(20, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 332: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(21, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 333: dp4
    reg_tmp3.z = dot(sanitize_mul(get_offset_register(22, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 334: dp3
    reg_tmp4.x = dot(vec3(sanitize_mul(get_offset_register(20, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 335: dp3
    reg_tmp4.y = dot(vec3(sanitize_mul(get_offset_register(21, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 336: dp3
    reg_tmp4.z = dot(vec3(sanitize_mul(get_offset_register(22, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 337: mov
    reg_tmp4.w = (uniforms.f[93].xxxx).w;
    // 338: mad
    reg_tmp8.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp3.xyzw) + reg_tmp8.xyzw;
    // 339: mad
    reg_tmp11.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp4.xyzw) + reg_tmp11.xyzw;
    return false;
}

bool sub_341_358() {
    // 341: mova
    address_registers.xy = ivec2(reg_tmp1.xyyy);
    // 342: nop
    // 343: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(20, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 344: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(21, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 345: dp4
    reg_tmp3.z = dot(sanitize_mul(get_offset_register(22, address_registers.x).xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 346: mov
    reg_tmp3.w = (uniforms.f[93].yyyy).w;
    // 347: dp3
    reg_tmp4.x = dot(vec3(sanitize_mul(get_offset_register(20, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 348: dp3
    reg_tmp4.y = dot(vec3(sanitize_mul(get_offset_register(21, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 349: dp3
    reg_tmp4.z = dot(vec3(sanitize_mul(get_offset_register(22, address_registers.y).xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 350: mov
    reg_tmp4.w = (uniforms.f[93].xxxx).w;
    // 351: dp3
    reg_tmp5.x = dot(vec3(sanitize_mul(get_offset_register(20, address_registers.y).xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 352: dp3
    reg_tmp5.y = dot(vec3(sanitize_mul(get_offset_register(21, address_registers.y).xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 353: dp3
    reg_tmp5.z = dot(vec3(sanitize_mul(get_offset_register(22, address_registers.y).xyzw, reg_tmp13.xyzw)), vec3(1.0));
    // 354: mov
    reg_tmp5.w = (uniforms.f[93].xxxx).w;
    // 355: mad
    reg_tmp8.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp3.xyzw) + reg_tmp8.xyzw;
    // 356: mad
    reg_tmp11.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp4.xyzw) + reg_tmp11.xyzw;
    // 357: mad
    reg_tmp12.xyzw = sanitize_mul(reg_tmp1.wwww, reg_tmp5.xyzw) + reg_tmp12.xyzw;
    return false;
}

bool sub_359_407() {
    // 359: cmp
    conditional_code = equal(vec2(uniforms.f[95].xyyy), vec2(reg_tmp0.xyyy));
    // 360: mad
    reg_tmp2.x = (sanitize_mul(reg_tmp2.xxxx, uniforms.f[93].wwww) + reg_tmp2.yyyy).x;
    // 361: mova
    address_registers.x = (ivec2(reg_tmp2.xxxx)).x;
    // 362: nop
    // 363: ifc
    if (all(not(conditional_code))) {
        sub_364_379();
    } else {
        sub_379_406();
    }
    // 406: nop
    return false;
}

bool sub_364_379() {
    // 364: cmp
    conditional_code = equal(vec2(uniforms.f[93].zyyy), vec2(reg_tmp0.xyyy));
    // 365: ifc
    if (all(not(conditional_code))) {
        sub_366_374();
    } else {
        sub_374_378();
    }
    // 378: nop
    return false;
}

bool sub_366_374() {
    // 366: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[70].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 367: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[71].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 368: dp4
    reg_tmp10.z = dot(sanitize_mul(uniforms.f[72].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 369: mov
    reg_tmp10.w = (uniforms.f[93].yyyy).w;
    // 370: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(10, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 371: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(11, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 372: dp4
    reg_tmp3.z = dot(sanitize_mul(get_offset_register(12, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 373: mov
    reg_tmp3.w = (uniforms.f[93].yyyy).w;
    return false;
}

bool sub_374_378() {
    // 374: call
    {
        sub_408_426();
    }
    // 375: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(10, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 376: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(11, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 377: mov
    reg_tmp3.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_379_406() {
    // 379: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_380_384();
    } else {
        sub_384_405();
    }
    // 405: nop
    return false;
}

bool sub_380_384() {
    // 380: call
    {
        sub_427_431();
    }
    // 381: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(10, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 382: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(11, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 383: mov
    reg_tmp3.zw = (uniforms.f[93].xxxx).zw;
    return false;
}

bool sub_384_405() {
    // 384: dp3
    reg_tmp10.x = dot(vec3(sanitize_mul(uniforms.f[70].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 385: dp3
    reg_tmp10.y = dot(vec3(sanitize_mul(uniforms.f[71].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 386: dp3
    reg_tmp10.z = dot(vec3(sanitize_mul(uniforms.f[72].xyzw, reg_tmp14.xyzw)), vec3(1.0));
    // 387: dp3
    reg_tmp4.z = dot(vec3(sanitize_mul(get_offset_register(12, address_registers.x).xyzw, reg_tmp10.xyzw)), vec3(1.0));
    // 388: dp4
    reg_tmp10.x = dot(sanitize_mul(uniforms.f[70].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 389: dp4
    reg_tmp10.y = dot(sanitize_mul(uniforms.f[71].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 390: dp4
    reg_tmp10.z = dot(sanitize_mul(uniforms.f[72].xyzw, reg_tmp15.xyzw), vec4(1.0));
    // 391: mov
    reg_tmp10.w = (uniforms.f[93].yyyy).w;
    // 392: dp4
    reg_tmp3.x = dot(sanitize_mul(get_offset_register(10, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 393: dp4
    reg_tmp3.y = dot(sanitize_mul(get_offset_register(11, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 394: dp4
    reg_tmp3.z = dot(sanitize_mul(get_offset_register(12, address_registers.x).xyzw, reg_tmp10.xyzw), vec4(1.0));
    // 395: cmp
    conditional_code.x = uniforms.f[93].xyyy.x >= reg_tmp4.zzzz.x;
    conditional_code.y = uniforms.f[93].xyyy.y < reg_tmp4.zzzz.y;
    // 396: ifc
    if (conditional_code.x) {
        sub_397_399();
    } else {
        sub_399_403();
    }
    // 403: nop
    // 404: add
    reg_tmp3.xy = (uniforms.f[94].zzzz + reg_tmp3.xyyy).xy;
    return false;
}

bool sub_397_399() {
    // 397: call
    {
        sub_432_441();
    }
    // 398: nop
    return false;
}

bool sub_399_403() {
    // 399: ifc
    if (conditional_code.y) {
        sub_400_402();
    }
    // 402: nop
    return false;
}

bool sub_400_402() {
    // 400: call
    {
        sub_432_441();
    }
    // 401: nop
    return false;
}

bool sub_408_426() {
    // 408: cmp
    conditional_code = equal(vec2(uniforms.f[93].xyyy), vec2(reg_tmp1.xyyy));
    // 409: mov
    reg_tmp10.xyzw = reg_tmp15.xyzw;
    // 410: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_411_415();
    } else {
        sub_415_425();
    }
    // 425: nop
    return false;
}

bool sub_411_415() {
    // 411: ifu
    if ((uniforms.b & 64u) != 0u) {
        sub_412_414();
    }
    // 414: nop
    return false;
}

bool sub_412_414() {
    // 412: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].xxxx, pica_input4.xyyy)).xy;
    // 413: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_415_425() {
    // 415: ifc
    if (all(bvec2(!conditional_code.x, conditional_code.y))) {
        sub_416_420();
    } else {
        sub_420_424();
    }
    // 424: nop
    return false;
}

bool sub_416_420() {
    // 416: ifu
    if ((uniforms.b & 128u) != 0u) {
        sub_417_419();
    }
    // 419: nop
    return false;
}

bool sub_417_419() {
    // 417: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].yyyy, pica_input5.xyzw)).xy;
    // 418: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_420_424() {
    // 420: ifu
    if ((uniforms.b & 256u) != 0u) {
        sub_421_423();
    }
    // 423: nop
    return false;
}

bool sub_421_423() {
    // 421: mul
    reg_tmp10.xy = (sanitize_mul(uniforms.f[91].zzzz, pica_input6.xyzw)).xy;
    // 422: mov
    reg_tmp10.zw = (uniforms.f[93].xyyy).zw;
    return false;
}

bool sub_427_431() {
    // 427: mov
    reg_tmp1.xy = (uniforms.f[94].zzzz).xy;
    // 428: mov
    reg_tmp1.zw = (uniforms.f[93].xxxx).zw;
    // 429: mad
    reg_tmp10.xyzw = sanitize_mul(reg_tmp14.xyzw, reg_tmp1.xyzw) + reg_tmp1.xyzw;
    // 430: mov
    reg_tmp10.zw = (uniforms.f[93].yyyy).zw;
    return false;
}

bool sub_432_441() {
    // 432: cmp
    conditional_code = greaterThanEqual(vec2(uniforms.f[93].xxxx), vec2(reg_tmp3.xyyy));
    // 433: ifc
    if (conditional_code.x) {
        sub_434_435();
    } else {
        sub_435_436();
    }
    // 436: nop
    // 437: ifc
    if (conditional_code.y) {
        sub_438_439();
    } else {
        sub_439_440();
    }
    // 440: nop
    return false;
}

bool sub_434_435() {
    // 434: add
    reg_tmp3.x = (-uniforms.f[95].wwww + reg_tmp3.xxxx).x;
    return false;
}

bool sub_435_436() {
    // 435: add
    reg_tmp3.x = (uniforms.f[95].wwww + reg_tmp3.xxxx).x;
    return false;
}

bool sub_438_439() {
    // 438: add
    reg_tmp3.y = (-uniforms.f[95].wwww + reg_tmp3.yyyy).y;
    return false;
}

bool sub_439_440() {
    // 439: add
    reg_tmp3.y = (uniforms.f[95].wwww + reg_tmp3.yyyy).y;
    return false;
}

bool sub_442_507() {
    uint jmp_to = 442u;
    while (true) {
        switch (jmp_to) {
        case 442u: {
            // 442: mov
            reg_tmp10.xyzw = uniforms.f[93].yxxx;
            // 443: mul
            reg_tmp5.xyzw = sanitize_mul(reg_tmp14.yzxx, reg_tmp13.zxyy);
            // 444: mad
            reg_tmp5.xyzw = sanitize_mul(-reg_tmp13.yzxx, reg_tmp14.zxyy) + reg_tmp5.xyzw;
            // 445: dp3
            reg_tmp5.w = dot(vec3(sanitize_mul(reg_tmp5.xyzw, reg_tmp5.xyzw)), vec3(1.0));
            // 446: rsq
            reg_tmp5.w = inversesqrt(reg_tmp5.wwww.x);
            // 447: mul
            reg_tmp5.xyzw = sanitize_mul(reg_tmp5.xyzw, reg_tmp5.wwww);
            // 448: add
            reg_tmp6.w = (reg_tmp14.zzzz + reg_tmp5.yyyy).w;
            // 449: mul
            reg_tmp13.xyzw = sanitize_mul(reg_tmp5.yzxx, reg_tmp14.zxyy);
            // 450: mad
            reg_tmp13.xyzw = sanitize_mul(-reg_tmp14.yzxx, reg_tmp5.zxyy) + reg_tmp13.xyzw;
            // 451: add
            reg_tmp6.w = (reg_tmp13.xxxx + reg_tmp6.wwww).w;
            // 452: mov
            reg_tmp13.w = (reg_tmp5.zzzz).w;
            // 453: mov
            reg_tmp5.z = (reg_tmp13.xxxx).z;
            // 454: add
            reg_tmp6.w = (uniforms.f[93].yyyy + reg_tmp6.wwww).w;
            // 455: mov
            reg_tmp14.w = (reg_tmp5.xxxx).w;
            // 456: mov
            reg_tmp5.x = (reg_tmp14.zzzz).x;
            // 457: cmp
            conditional_code = lessThan(vec2(uniforms.f[94].yyyy), vec2(reg_tmp6.wwww));
            // 458: mov
            reg_tmp6.x = (uniforms.f[93].yyyy).x;
            // 459: mov
            reg_tmp6.y = (-uniforms.f[93].yyyy).y;
            // 460: jmpc
            if (!conditional_code.x) {
                { jmp_to = 468u; break; }
            }
            // 461: add
            reg_tmp7.xz = (reg_tmp13.wwyy + -reg_tmp14.yyww).xz;
            // 462: add
            reg_tmp7.y = (reg_tmp14.xxxx + -reg_tmp13.zzzz).y;
            // 463: mov
            reg_tmp7.w = (reg_tmp6.wwww).w;
            // 464: dp4
            reg_tmp6.xyzw = vec4(dot(sanitize_mul(reg_tmp7.xyzw, reg_tmp7.xyzw), vec4(1.0)));
            // 465: rsq
            reg_tmp6.xyzw = vec4(inversesqrt(reg_tmp6.xxxx.x));
            // 466: mul
            reg_tmp10.xyzw = sanitize_mul(reg_tmp7.xyzw, reg_tmp6.xyzw);
            // 467: jmpu
            if ((uniforms.b & 1u) != 0u) {
                { jmp_to = 506u; break; }
            }
        }
        case 468u: {
            // 468: cmp
            conditional_code = greaterThan(vec2(reg_tmp5.zyyy), vec2(reg_tmp5.yxxx));
            // 469: ifc
            if (conditional_code.x) {
                sub_470_490();
            } else {
                sub_490_503();
            }
            // 503: dp4
            reg_tmp6.xyzw = vec4(dot(sanitize_mul(reg_tmp8.xyzw, reg_tmp8.xyzw), vec4(1.0)));
            // 504: rsq
            reg_tmp6.xyzw = vec4(inversesqrt(reg_tmp6.xxxx.x));
            // 505: mul
            reg_tmp10.xyzw = sanitize_mul(reg_tmp8.xyzw, reg_tmp6.xyzw);
        }
        case 506u: {
            // 506: mov
            pica_output5.xyzw = reg_tmp10.xyzw;
        }
        default: return false;
        }
    }
    return false;
}

bool sub_470_490() {
    // 470: ifc
    if (conditional_code.y) {
        sub_471_476();
    } else {
        sub_476_489();
    }
    // 489: mov
    reg_tmp8.w = (-reg_tmp8.xyzw).w;
    return false;
}

bool sub_471_476() {
    // 471: mul
    reg_tmp8.xyzw = sanitize_mul(reg_tmp13.yyzw, reg_tmp6.xxxy);
    // 472: add
    reg_tmp8.x = (uniforms.f[93].yyyy + -reg_tmp5.yyyy).x;
    // 473: add
    reg_tmp9.xyzw = reg_tmp5.zzzz + -reg_tmp5.xxxx;
    // 474: add
    reg_tmp8.yzw = (reg_tmp8.xyzw + reg_tmp14.wwxy).yzw;
    // 475: add
    reg_tmp8.x = (reg_tmp9.xyzw + reg_tmp8.xyzw).x;
    return false;
}

bool sub_476_489() {
    // 476: cmp
    conditional_code = greaterThan(vec2(reg_tmp5.zzzz), vec2(reg_tmp5.xxxx));
    // 477: mul
    reg_tmp8.xyzw = sanitize_mul(reg_tmp13.yyzw, reg_tmp6.xxxy);
    // 478: add
    reg_tmp8.x = (uniforms.f[93].yyyy + -reg_tmp5.yyyy).x;
    // 479: ifc
    if (conditional_code.x) {
        sub_480_483();
    } else {
        sub_483_488();
    }
    // 488: nop
    return false;
}

bool sub_480_483() {
    // 480: add
    reg_tmp9.xyzw = reg_tmp5.zzzz + -reg_tmp5.xxxx;
    // 481: add
    reg_tmp8.yzw = (reg_tmp8.xyzw + reg_tmp14.wwxy).yzw;
    // 482: add
    reg_tmp8.x = (reg_tmp9.xyzw + reg_tmp8.xyzw).x;
    return false;
}

bool sub_483_488() {
    // 483: mul
    reg_tmp8.xyzw = sanitize_mul(reg_tmp13.zwwy, reg_tmp6.xxxy);
    // 484: add
    reg_tmp8.z = (uniforms.f[93].yyyy + -reg_tmp5.zzzz).z;
    // 485: add
    reg_tmp9.xyzw = reg_tmp5.xxxx + -reg_tmp5.yyyy;
    // 486: add
    reg_tmp8.xyw = (reg_tmp8.xyzw + reg_tmp14.xyyw).xyw;
    // 487: add
    reg_tmp8.z = (reg_tmp9.xyzw + reg_tmp8.xyzw).z;
    return false;
}

bool sub_490_503() {
    // 490: ifc
    if (conditional_code.y) {
        sub_491_496();
    } else {
        sub_496_502();
    }
    // 502: nop
    return false;
}

bool sub_491_496() {
    // 491: mul
    reg_tmp8.xyzw = sanitize_mul(reg_tmp13.yywz, reg_tmp6.xxxy);
    // 492: add
    reg_tmp8.y = (uniforms.f[93].yyyy + -reg_tmp5.zzzz).y;
    // 493: add
    reg_tmp9.xyzw = reg_tmp5.yyyy + -reg_tmp5.xxxx;
    // 494: add
    reg_tmp8.xzw = (reg_tmp8.xyzw + reg_tmp14.wwyx).xzw;
    // 495: add
    reg_tmp8.y = (reg_tmp9.xyzw + reg_tmp8.xyzw).y;
    return false;
}

bool sub_496_502() {
    // 496: mul
    reg_tmp8.xyzw = sanitize_mul(reg_tmp13.zwwy, reg_tmp6.xxxy);
    // 497: add
    reg_tmp8.z = (uniforms.f[93].yyyy + -reg_tmp5.zzzz).z;
    // 498: add
    reg_tmp9.xyzw = reg_tmp5.xxxx + -reg_tmp5.yyyy;
    // 499: add
    reg_tmp8.xyw = (reg_tmp8.xyzw + reg_tmp14.xyyw).xyw;
    // 500: add
    reg_tmp8.z = (reg_tmp9.xyzw + reg_tmp8.xyzw).z;
    // 501: mov
    reg_tmp8.w = (-reg_tmp8.xyzw).w;
    return false;
}

)PICA_TITLE"},
{0U,true,{8666064895965427392ULL,15011713385858978596ULL,268ULL},{2919690346405498153ULL,12553437616660230073ULL,128ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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
bool sub_7_10();
bool sub_10_65();
bool sub_31_34();
bool sub_36_37();
bool sub_43_45();
bool sub_45_59();
bool sub_47_48();
bool sub_48_58();
bool sub_50_51();
bool sub_52_53();
bool sub_54_55();
bool sub_56_57();
bool sub_61_64();

bool exec_shader() {
    sub_0_4096();
    return true;
}

bool sub_0_4096() {
    // 0: mova
    address_registers.x = (ivec2(pica_input0.xyzw)).x;
    // 1: mov
    reg_tmp0.xyzw = get_offset_register(10, address_registers.x).wzyx;
    // 2: mul
    reg_tmp10.x = (sanitize_mul(uniforms.f[7].xxxx, reg_tmp0.zzzz)).x;
    // 3: add
    reg_tmp13.x = (uniforms.f[9].wwww + reg_tmp10.xxxx).x;
    // 4: flr
    reg_tmp10.x = (floor(reg_tmp13.xxxx)).x;
    // 5: cmp
    conditional_code = equal(vec2(uniforms.f[9].xzzz), vec2(reg_tmp10.xxxx));
    // 6: ifc
    if (any(conditional_code)) {
        sub_7_10();
    } else {
        sub_10_65();
    }
    // 65: end
    return true;
}

bool sub_7_10() {
    // 7: mov
    pica_output0.xyzw = uniforms.f[9].xxxy;
    // 8: mov
    pica_output1.xyzw = uniforms.f[9].yyyx;
    // 9: mov
    pica_output2.xyzw = uniforms.f[9].xxyy;
    return false;
}

bool sub_10_65() {
    // 10: mov
    reg_tmp7.xyzw = get_offset_register(11, address_registers.x).wzyx;
    // 11: mov
    reg_tmp8.xyzw = get_offset_register(12, address_registers.x).wzyx;
    // 12: mov
    reg_tmp9.xyzw = get_offset_register(13, address_registers.x).wzyx;
    // 13: slti
    reg_tmp12.x = (vec4(lessThan(reg_tmp0.xxxx, uniforms.f[9].xxxx))).x;
    // 14: max
    reg_tmp0.x = (mix(-reg_tmp0.xxxx, reg_tmp0.xxxx, greaterThan(reg_tmp0.xxxx, -reg_tmp0.xxxx))).x;
    // 15: mova
    address_registers.xy = ivec2(reg_tmp0.xyyy);
    // 16: mov
    reg_tmp1.xy = (pica_input0.zwzw).xy;
    // 17: mov
    reg_tmp1.zw = (uniforms.f[9].xyxy).zw;
    // 18: mov
    reg_tmp3.w = (uniforms.f[9].yyyy).w;
    // 19: dp4
    reg_tmp3.x = dot(sanitize_mul(reg_tmp1.xyzw, reg_tmp7.xyzw), vec4(1.0));
    // 20: dp4
    reg_tmp3.y = dot(sanitize_mul(reg_tmp1.xyzw, reg_tmp8.xyzw), vec4(1.0));
    // 21: dp4
    reg_tmp3.z = dot(sanitize_mul(reg_tmp1.xyzw, reg_tmp9.xyzw), vec4(1.0));
    // 22: mov
    reg_tmp2.w = (uniforms.f[9].yyyy).w;
    // 23: dp4
    reg_tmp2.x = dot(sanitize_mul(uniforms.f[4].wzyx, reg_tmp3.xyzw), vec4(1.0));
    // 24: dp4
    reg_tmp2.y = dot(sanitize_mul(uniforms.f[5].wzyx, reg_tmp3.xyzw), vec4(1.0));
    // 25: dp4
    reg_tmp2.z = dot(sanitize_mul(uniforms.f[6].wzyx, reg_tmp3.xyzw), vec4(1.0));
    // 26: mov
    reg_tmp3.z = (-reg_tmp3.zzzz).z;
    // 27: add
    reg_tmp3.z = (uniforms.f[7].yyyy + reg_tmp3.zzzz).z;
    // 28: mov
    reg_tmp3.x = (uniforms.f[7].wwww).x;
    // 29: cmp
    conditional_code = notEqual(vec2(uniforms.f[9].xxxx), vec2(reg_tmp3.xzzz));
    // 30: ifc
    if (all(conditional_code)) {
        sub_31_34();
    }
    // 34: cmp
    conditional_code = notEqual(vec2(uniforms.f[9].xxxx), vec2(reg_tmp0.wwww));
    // 35: ifc
    if (all(conditional_code)) {
        sub_36_37();
    }
    // 37: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].wzyx, reg_tmp2.xyzw), vec4(1.0));
    // 38: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].wzyx, reg_tmp2.xyzw), vec4(1.0));
    // 39: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].wzyx, reg_tmp2.xyzw), vec4(1.0));
    // 40: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].wzyx, reg_tmp2.xyzw), vec4(1.0));
    // 41: cmp
    conditional_code.x = uniforms.f[9].xyyy.x <= reg_tmp0.xxxx.x;
    conditional_code.y = uniforms.f[9].xyyy.y >= reg_tmp0.xxxx.y;
    // 42: ifc
    if (all(conditional_code)) {
        sub_43_45();
    } else {
        sub_45_59();
    }
    // 59: mov
    reg_tmp4.xyzw = uniforms.f[9].xxyy;
    // 60: ifu
    if ((uniforms.b & 1u) != 0u) {
        sub_61_64();
    }
    // 64: mov
    pica_output2.xyzw = reg_tmp4.xyzw;
    return false;
}

bool sub_31_34() {
    // 31: rcp
    reg_tmp3.y = (1.0 / -reg_tmp2.zzzz.x);
    // 32: mul
    reg_tmp3.z = (sanitize_mul(reg_tmp3.yyyy, reg_tmp3.zzzz)).z;
    // 33: mad
    reg_tmp2.x = (sanitize_mul(reg_tmp3.xxxx, reg_tmp3.zzzz) + reg_tmp2.xxxx).x;
    return false;
}

bool sub_36_37() {
    // 36: flr
    reg_tmp2.xyz = (floor(reg_tmp2.xyzz)).xyz;
    return false;
}

bool sub_43_45() {
    // 43: mov
    pica_output1.xyz = (uniforms.f[9].yyyy).xyz;
    // 44: mov
    pica_output1.w = (reg_tmp0.xxxx).w;
    return false;
}

bool sub_45_59() {
    // 45: cmp
    conditional_code = equal(vec2(uniforms.f[9].yyyy), vec2(reg_tmp12.xxxx));
    // 46: ifc
    if (all(conditional_code)) {
        sub_47_48();
    } else {
        sub_48_58();
    }
    // 58: nop
    return false;
}

bool sub_47_48() {
    // 47: mov
    pica_output1.xyzw = get_offset_register(0, address_registers.x).wzyx;
    return false;
}

bool sub_48_58() {
    // 48: cmp
    conditional_code = notEqual(vec2(uniforms.f[9].xxxx), vec2(reg_tmp1.xyyy));
    // 49: ifc
    if (all(not(conditional_code))) {
        sub_50_51();
    }
    // 51: ifc
    if (all(bvec2(conditional_code.x, !conditional_code.y))) {
        sub_52_53();
    }
    // 53: ifc
    if (all(bvec2(!conditional_code.x, conditional_code.y))) {
        sub_54_55();
    }
    // 55: ifc
    if (all(conditional_code)) {
        sub_56_57();
    }
    // 57: nop
    return false;
}

bool sub_50_51() {
    // 50: mov
    pica_output1.xyzw = get_offset_register(0, address_registers.x).wzyx;
    return false;
}

bool sub_52_53() {
    // 52: mov
    pica_output1.xyzw = get_offset_register(1, address_registers.x).wzyx;
    return false;
}

bool sub_54_55() {
    // 54: mov
    pica_output1.xyzw = get_offset_register(2, address_registers.x).wzyx;
    return false;
}

bool sub_56_57() {
    // 56: mov
    pica_output1.xyzw = get_offset_register(3, address_registers.x).wzyx;
    return false;
}

bool sub_61_64() {
    // 61: dp3
    reg_tmp4.x = dot(vec3(sanitize_mul(get_offset_register(0, address_registers.y).wzyy, reg_tmp1.xyww)), vec3(1.0));
    // 62: dp3
    reg_tmp4.y = dot(vec3(sanitize_mul(get_offset_register(1, address_registers.y).wzyy, reg_tmp1.xyww)), vec3(1.0));
    // 63: add
    reg_tmp4.y = (uniforms.f[9].yyyy + -reg_tmp4.yyyy).y;
    return false;
}

)PICA_TITLE"},
{9U,true,{5858288631121555060ULL,9930116078652300608ULL,676ULL},{14096147928651149917ULL,11795952949675581ULL,144ULL},R"PICA_TITLE(vec4 sanitize_mul(vec4 lhs, vec4 rhs) {
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

bool sub_0_8();
bool sub_9_4096();

bool exec_shader() {
    sub_9_4096();
    return true;
}

bool sub_0_8() {
    // 0: dp4
    reg_tmp12.x = dot(sanitize_mul(uniforms.f[4].xyzw, pica_input0.xyzw), vec4(1.0));
    // 1: dp4
    reg_tmp12.y = dot(sanitize_mul(uniforms.f[5].xyzw, pica_input0.xyzw), vec4(1.0));
    // 2: dp4
    reg_tmp12.z = dot(sanitize_mul(uniforms.f[6].xyzw, pica_input0.xyzw), vec4(1.0));
    // 3: mov
    reg_tmp12.w = (uniforms.f[95].zzzz).w;
    // 4: dp4
    pica_output0.x = dot(sanitize_mul(uniforms.f[0].xyzw, reg_tmp12.xyzw), vec4(1.0));
    // 5: dp4
    pica_output0.y = dot(sanitize_mul(uniforms.f[1].xyzw, reg_tmp12.xyzw), vec4(1.0));
    // 6: dp4
    pica_output0.z = dot(sanitize_mul(uniforms.f[2].xyzw, reg_tmp12.xyzw), vec4(1.0));
    // 7: dp4
    pica_output0.w = dot(sanitize_mul(uniforms.f[3].xyzw, reg_tmp12.xyzw), vec4(1.0));
    return false;
}

bool sub_9_4096() {
    // 9: call
    {
        sub_0_8();
    }
    // 10: mov
    pica_output1.xyzw = pica_input1.xyzw;
    // 11: end
    return true;
}

)PICA_TITLE"},
};
} // namespace Oot3dNativeGame
