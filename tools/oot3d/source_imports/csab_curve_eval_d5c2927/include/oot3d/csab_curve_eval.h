#pragma once

#include <cstdint>

namespace Oot3dSourceCsabCurve {

using u8 = std::uint8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;

struct Oot3dAnimationCurveState {
    const void* curve;
    u8 loop;
};

struct Oot3dAnimationCurveHeader {
    u8 interpolationType;
    u8 unk_01[3];
    s32 keyCount;
    u32 unk_08;
    s32 duration;
};

struct Oot3dAnimationCurveF32Key {
    s32 frame;
    float value;
    float tangentIn;
    float tangentOut;
};

struct Oot3dAnimationCurveF32LinearKey {
    s32 frame;
    float value;
};

struct Oot3dAnimationCurveS16Key {
    s16 frame;
    s16 value;
    s16 tangentIn;
    s16 tangentOut;
};

static_assert(sizeof(Oot3dAnimationCurveHeader) == 0x10);
static_assert(sizeof(Oot3dAnimationCurveF32Key) == 0x10);
static_assert(sizeof(Oot3dAnimationCurveF32LinearKey) == 0x08);
static_assert(sizeof(Oot3dAnimationCurveS16Key) == 0x08);

float Oot3d_EvalAnimationCurveS16(
    const Oot3dAnimationCurveState* state, float frame);
float Oot3d_EvalAnimationCurveF32(
    const Oot3dAnimationCurveState* state, float frame);

} // namespace Oot3dSourceCsabCurve
