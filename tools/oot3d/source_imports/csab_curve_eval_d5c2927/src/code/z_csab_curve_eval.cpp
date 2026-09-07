#include "oot3d/csab_curve_eval.h"

#include <cmath>
#include <cstddef>

namespace Oot3dSourceCsabCurve {

static const float OOT3D_CSAB_ZERO = 0.0f;
static const float OOT3D_CSAB_ONE = 1.0f;
static const float OOT3D_CSAB_TWO = 2.0f;
static const float OOT3D_CSAB_THREE = 3.0f;
static const float OOT3D_CSAB_PI = 3.1415927410125732f;
static const float OOT3D_CSAB_TWO_PI = 6.2831854820251465f;
static const float OOT3D_CSAB_S16_ANGLE_SCALE =
    0.00009587380156106316f;
static const float OOT3D_CSAB_S16_TANGENT_SCALE =
    0.00004793690078053158f;
static const float OOT3D_CSAB_ANGLE_TO_TABLE =
    40.7436637878418f;
static const float OOT3D_CSAB_TABLE_STEP_RADIANS =
    0.02454369328916073f;

enum {
    OOT3D_CURVE_LINEAR = 1,
    OOT3D_CURVE_HERMITE = 2,
    OOT3D_CURVE_STEP = 3,
    OOT3D_CSAB_TRANSLATION_CHANNEL = 1,
    OOT3D_CSAB_ROTATION_CHANNEL = 2,
    OOT3D_CSAB_SCALE_CHANNEL = 4,
};

#if defined(__GNUC__) || defined(__clang__)
#define OOT3D_CSAB_INLINE static inline __attribute__((always_inline))
#else
#define OOT3D_CSAB_INLINE static inline
#endif

OOT3D_CSAB_INLINE float Oot3d_EvalHermiteSegment(
    float frame,
    float leftFrame,
    float rightFrame,
    float leftValue,
    float rightValue,
    float leftTangentOut,
    float rightTangentIn
) {
    const float elapsed = frame - leftFrame;
    const float interval = rightFrame - leftFrame;
    const float t = elapsed * (OOT3D_CSAB_ONE / interval);
    const float tMinusOne = t - OOT3D_CSAB_ONE;
    const float valueTerm =
        ((leftValue - rightValue) *
         (OOT3D_CSAB_TWO * t - OOT3D_CSAB_THREE) *
         t);
    const float tangentTerm =
        elapsed * tMinusOne *
        (tMinusOne * leftTangentOut + t * rightTangentIn);

    return leftValue + valueTerm * t + tangentTerm;
}

OOT3D_CSAB_INLINE s32 Oot3d_FindF32LinearUpperKey(
    const Oot3dAnimationCurveF32LinearKey* keys,
    s32 keyCount,
    float frame
) {
    s32 index = 0;

    /*
     * The negated comparison deliberately mirrors the VFP `bgt` loop. An
     * unordered frame therefore consumes all keys and returns the last value.
     */
    while (index < keyCount &&
           !((float)keys[index].frame > frame)) {
        index++;
    }
    return index;
}

OOT3D_CSAB_INLINE s32 Oot3d_FindF32UpperKey(
    const Oot3dAnimationCurveF32Key* keys,
    s32 keyCount,
    float frame
) {
    s32 index = 0;

    while (index < keyCount &&
           !((float)keys[index].frame > frame)) {
        index++;
    }
    return index;
}

OOT3D_CSAB_INLINE s32 Oot3d_FindS16UpperKey(
    const Oot3dAnimationCurveS16Key* keys,
    s32 keyCount,
    float frame
) {
    s32 index = 0;

    while (index < keyCount &&
           !((float)keys[index].frame > frame)) {
        index++;
    }
    return index;
}

OOT3D_CSAB_INLINE float Oot3d_S16AngleToRadians(s16 value) {
    return (float)value * OOT3D_CSAB_S16_ANGLE_SCALE;
}

OOT3D_CSAB_INLINE float Oot3d_S16TangentToSlope(s16 value) {
    float angle = (float)value * OOT3D_CSAB_S16_TANGENT_SCALE;

    /*
     * Keep the target's three separately rounded products. Their net scale
     * is pi/65536, but the intermediate VFP rounding is observable.
     */
    angle *= OOT3D_CSAB_ANGLE_TO_TABLE;
    angle *= OOT3D_CSAB_TABLE_STEP_RADIANS;
    return tanf(angle);
}

/*
 * Ghidra: FUN_003084e8 @ 0x003084E8.
 *
 * CSAB rotation curves store signed 16-bit angles. Endpoint values use
 * 2*pi/65536 radians per unit; Hermite tangents use
 * tan(raw * pi/65536), exactly as the native instruction sequence does.
 */
float Oot3d_EvalAnimationCurveS16(
    const Oot3dAnimationCurveState* state,
    float frame
) {
    const Oot3dAnimationCurveHeader* header =
        (const Oot3dAnimationCurveHeader*)state->curve;
    const Oot3dAnimationCurveS16Key* keys =
        (const Oot3dAnimationCurveS16Key*)(header + 1);
    const s32 keyCount = header->keyCount;
    const u8 interpolationType = header->interpolationType;
    const Oot3dAnimationCurveS16Key* left = NULL;
    const Oot3dAnimationCurveS16Key* right = NULL;
    float leftFrame = OOT3D_CSAB_ZERO;
    float rightFrame = OOT3D_CSAB_ZERO;

    if (interpolationType == OOT3D_CURVE_LINEAR) {
        if (keyCount == 1) {
            return Oot3d_S16AngleToRadians(keys[0].value);
        }
        return OOT3D_CSAB_ZERO;
    }
    if (interpolationType != OOT3D_CURVE_HERMITE) {
        return OOT3D_CSAB_ZERO;
    }
    if (keyCount == 1) {
        return Oot3d_S16AngleToRadians(keys[0].value);
    }

    if (state->loop != 0 &&
        (frame < OOT3D_CSAB_ZERO ||
         frame > (float)header->duration)) {
        left = &keys[keyCount - 1];
        right = &keys[0];
        leftFrame = (float)left->frame;
        rightFrame =
            (float)((s32)right->frame + header->duration + 1);
        if (frame < OOT3D_CSAB_ZERO) {
            frame += (float)(header->duration + 1);
        }
    } else {
        const s32 upperIndex =
            Oot3d_FindS16UpperKey(keys, keyCount, frame);

        if (upperIndex == 0) {
            return OOT3D_CSAB_ZERO;
        }
        if (upperIndex == keyCount) {
            return Oot3d_S16AngleToRadians(
                keys[upperIndex - 1].value
            );
        }
        left = &keys[upperIndex - 1];
        right = &keys[upperIndex];
        leftFrame = (float)left->frame;
        rightFrame = (float)right->frame;
    }

    if (left != NULL && right != NULL) {
        const float leftValue =
            Oot3d_S16AngleToRadians(left->value);
        float rightValue =
            Oot3d_S16AngleToRadians(right->value);
        const float difference = rightValue - leftValue;

        if (difference < -OOT3D_CSAB_PI) {
            rightValue += OOT3D_CSAB_TWO_PI;
        } else if (difference > OOT3D_CSAB_PI) {
            rightValue -= OOT3D_CSAB_TWO_PI;
        }

        return Oot3d_EvalHermiteSegment(
            frame,
            leftFrame,
            rightFrame,
            leftValue,
            rightValue,
            Oot3d_S16TangentToSlope(left->tangentOut),
            Oot3d_S16TangentToSlope(right->tangentIn)
        );
    }
    return OOT3D_CSAB_ZERO;
}

/*
 * Ghidra: FUN_003087a4 @ 0x003087A4.
 *
 * Type 1 uses 0x08-byte linear keys, type 2 uses 0x10-byte Hermite keys and
 * type 3 uses the same compact keys as a hold/step curve.
 */
float Oot3d_EvalAnimationCurveF32(
    const Oot3dAnimationCurveState* state,
    float frame
) {
    const Oot3dAnimationCurveHeader* header =
        (const Oot3dAnimationCurveHeader*)state->curve;
    const s32 keyCount = header->keyCount;
    const u8 interpolationType = header->interpolationType;

    if (interpolationType == OOT3D_CURVE_LINEAR) {
        const Oot3dAnimationCurveF32LinearKey* keys =
            (const Oot3dAnimationCurveF32LinearKey*)(header + 1);
        s32 upperIndex;

        if (keyCount == 1) {
            return keys[0].value;
        }
        upperIndex =
            Oot3d_FindF32LinearUpperKey(keys, keyCount, frame);
        if (upperIndex == 0) {
            return keys[0].value;
        }
        if (upperIndex == keyCount) {
            return keys[upperIndex - 1].value;
        }
        {
            const Oot3dAnimationCurveF32LinearKey* left =
                &keys[upperIndex - 1];
            const Oot3dAnimationCurveF32LinearKey* right =
                &keys[upperIndex];
            const float t =
                (frame - (float)left->frame) /
                (float)(right->frame - left->frame);

            return left->value +
                   (right->value - left->value) * t;
        }
    }

    if (interpolationType == OOT3D_CURVE_HERMITE) {
        const Oot3dAnimationCurveF32Key* keys =
            (const Oot3dAnimationCurveF32Key*)(header + 1);
        const Oot3dAnimationCurveF32Key* left = NULL;
        const Oot3dAnimationCurveF32Key* right = NULL;
        float leftFrame = OOT3D_CSAB_ZERO;
        float rightFrame = OOT3D_CSAB_ZERO;

        if (keyCount == 1) {
            return keys[0].value;
        }
        if (state->loop != 0 &&
            (frame < OOT3D_CSAB_ZERO ||
             frame > (float)header->duration)) {
            left = &keys[keyCount - 1];
            right = &keys[0];
            leftFrame = (float)left->frame;
            rightFrame =
                (float)(right->frame + header->duration + 1);
            if (frame < OOT3D_CSAB_ZERO) {
                frame += (float)(header->duration + 1);
            }
        } else {
            const s32 upperIndex =
                Oot3d_FindF32UpperKey(keys, keyCount, frame);

            if (upperIndex == 0) {
                return OOT3D_CSAB_ZERO;
            }
            if (upperIndex == keyCount) {
                return keys[upperIndex - 1].value;
            }
            left = &keys[upperIndex - 1];
            right = &keys[upperIndex];
            leftFrame = (float)left->frame;
            rightFrame = (float)right->frame;
        }

        if (left != NULL && right != NULL) {
            return Oot3d_EvalHermiteSegment(
                frame,
                leftFrame,
                rightFrame,
                left->value,
                right->value,
                left->tangentOut,
                right->tangentIn
            );
        }
        return OOT3D_CSAB_ZERO;
    }

    if (interpolationType == OOT3D_CURVE_STEP) {
        const Oot3dAnimationCurveF32LinearKey* keys =
            (const Oot3dAnimationCurveF32LinearKey*)(header + 1);
        s32 upperIndex;

        if (keyCount == 1) {
            return keys[0].value;
        }
        upperIndex =
            Oot3d_FindF32LinearUpperKey(keys, keyCount, frame);
        if (upperIndex == 0) {
            return keys[0].value;
        }
        return keys[upperIndex - 1].value;
    }

    return OOT3D_CSAB_ZERO;
}

#undef OOT3D_CSAB_INLINE

} // namespace Oot3dSourceCsabCurve
