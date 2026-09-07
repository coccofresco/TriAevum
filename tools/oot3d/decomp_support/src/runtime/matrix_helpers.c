#include "oot3d/runtime_helpers.h"

// Ghidra: FUN_003283a0.
// Scales the first three columns of a 3x4 matrix by a vec3 and preserves the
// translation column.
void oot3d_scale_mtx3x4_axes(float* dst_mtx3x4, const float* src_mtx3x4, const float* scale_vec3) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_mtx3x4;
    register const float* src_reg __asm__("r1") = src_mtx3x4;
    register const float* scale_reg __asm__("r2") = scale_vec3;

    __asm__ volatile(
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "vldmia %2, {s12, s13, s14}\n\t"
        "vmul.f32 s0, s0, s12\n"
        "vmul.f32 s1, s1, s13\n"
        "vmul.f32 s2, s2, s14\n"
        "vmul.f32 s4, s4, s12\n"
        "vmul.f32 s5, s5, s13\n"
        "vmul.f32 s6, s6, s14\n"
        "vmul.f32 s8, s8, s12\n"
        "vmul.f32 s9, s9, s13\n"
        "vmul.f32 s10, s10, s14\n"
        "vstmia %0, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}"
        :
        : "r"(dst_reg), "r"(src_reg), "r"(scale_reg)
        : "memory");
#else
    dst_mtx3x4[0] = src_mtx3x4[0] * scale_vec3[0];
    dst_mtx3x4[1] = src_mtx3x4[1] * scale_vec3[1];
    dst_mtx3x4[2] = src_mtx3x4[2] * scale_vec3[2];
    dst_mtx3x4[3] = src_mtx3x4[3];
    dst_mtx3x4[4] = src_mtx3x4[4] * scale_vec3[0];
    dst_mtx3x4[5] = src_mtx3x4[5] * scale_vec3[1];
    dst_mtx3x4[6] = src_mtx3x4[6] * scale_vec3[2];
    dst_mtx3x4[7] = src_mtx3x4[7];
    dst_mtx3x4[8] = src_mtx3x4[8] * scale_vec3[0];
    dst_mtx3x4[9] = src_mtx3x4[9] * scale_vec3[1];
    dst_mtx3x4[10] = src_mtx3x4[10] * scale_vec3[2];
    dst_mtx3x4[11] = src_mtx3x4[11];
#endif
}

// Ghidra: FUN_003393ac.
// Scales each four-float block of a 3x4 matrix by scale[0], scale[1], and
// scale[2] respectively.
void oot3d_scale_mtx3x4_blocks(float* dst_mtx3x4, const float* scale_vec3, const float* src_mtx3x4) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_mtx3x4;
    register const float* scale_reg __asm__("r1") = scale_vec3;
    register const float* src_reg __asm__("r2") = src_mtx3x4;

    __asm__ volatile(
        "vldmia %2, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "vldmia %1, {s12, s13, s14}\n\t"
        "vmul.f32 s0, s0, s12\n"
        "vmul.f32 s1, s1, s12\n"
        "vmul.f32 s2, s2, s12\n"
        "vmul.f32 s3, s3, s12\n"
        "vmul.f32 s4, s4, s13\n"
        "vmul.f32 s5, s5, s13\n"
        "vmul.f32 s6, s6, s13\n"
        "vmul.f32 s7, s7, s13\n"
        "vmul.f32 s8, s8, s14\n"
        "vmul.f32 s9, s9, s14\n"
        "vmul.f32 s10, s10, s14\n"
        "vmul.f32 s11, s11, s14\n"
        "vstmia %0, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}"
        :
        : "r"(dst_reg), "r"(scale_reg), "r"(src_reg)
        : "memory");
#else
    for (size_t i = 0; i < 4; i++) {
        dst_mtx3x4[i] = src_mtx3x4[i] * scale_vec3[0];
        dst_mtx3x4[i + 4] = src_mtx3x4[i + 4] * scale_vec3[1];
        dst_mtx3x4[i + 8] = src_mtx3x4[i + 8] * scale_vec3[2];
    }
#endif
}

// Ghidra: FUN_0034e0f0.
// Multiplies a row-major 3x3 matrix by a vec3.
void oot3d_mul_mtx3x3_vec3(float* dst_vec3, const float* mtx3x3, const float* vec3) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_vec3;
    register const float* mtx_reg __asm__("r1") = mtx3x3;
    register const float* vec_reg __asm__("r2") = vec3;

    __asm__ volatile(
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8}\n\t"
        "vldmia %2, {s9, s10, s11}\n\t"
        "vmul.f32 s12, s0, s9\n"
        "vmul.f32 s13, s3, s9\n"
        "vmul.f32 s14, s6, s9\n"
        "vmla.f32 s12, s1, s10\n"
        "vmla.f32 s13, s4, s10\n"
        "vmla.f32 s14, s7, s10\n"
        "vmla.f32 s12, s2, s11\n"
        "vmla.f32 s13, s5, s11\n"
        "vmla.f32 s14, s8, s11\n"
        "vstmia %0, {s12, s13, s14}"
        :
        : "r"(dst_reg), "r"(mtx_reg), "r"(vec_reg)
        : "memory");
#else
    dst_vec3[0] = (mtx3x3[0] * vec3[0]) + (mtx3x3[1] * vec3[1]) + (mtx3x3[2] * vec3[2]);
    dst_vec3[1] = (mtx3x3[3] * vec3[0]) + (mtx3x3[4] * vec3[1]) + (mtx3x3[5] * vec3[2]);
    dst_vec3[2] = (mtx3x3[6] * vec3[0]) + (mtx3x3[7] * vec3[1]) + (mtx3x3[8] * vec3[2]);
#endif
}

// Ghidra: FUN_003735ac.
// Transforms a vec3 by a row-major 3x4 matrix, including the translation
// column.
void oot3d_transform_mtx3x4_vec3(float* dst_vec3, const float* mtx3x4, const float* vec3) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_vec3;
    register const float* mtx_reg __asm__("r1") = mtx3x4;
    register const float* vec_reg __asm__("r2") = vec3;

    __asm__ volatile(
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "vldmia %2, {s12, s13, s14}\n\t"
        "vmla.f32 s3, s0, s12\n"
        "vmla.f32 s7, s4, s12\n"
        "vmla.f32 s11, s8, s12\n"
        "vmla.f32 s3, s1, s13\n"
        "vmla.f32 s7, s5, s13\n"
        "vmla.f32 s11, s9, s13\n"
        "vmla.f32 s3, s2, s14\n"
        "vmla.f32 s7, s6, s14\n"
        "vmla.f32 s11, s10, s14\n"
        "vstr.32 s3, [%0]\n\t"
        "vstr.32 s7, [%0, #4]\n\t"
        "vstr.32 s11, [%0, #8]"
        :
        : "r"(dst_reg), "r"(mtx_reg), "r"(vec_reg)
        : "memory");
#else
    dst_vec3[0] = mtx3x4[3] + (mtx3x4[0] * vec3[0]) + (mtx3x4[1] * vec3[1]) + (mtx3x4[2] * vec3[2]);
    dst_vec3[1] = mtx3x4[7] + (mtx3x4[4] * vec3[0]) + (mtx3x4[5] * vec3[1]) + (mtx3x4[6] * vec3[2]);
    dst_vec3[2] = mtx3x4[11] + (mtx3x4[8] * vec3[0]) + (mtx3x4[9] * vec3[1]) + (mtx3x4[10] * vec3[2]);
#endif
}

// Ghidra: FUN_00372070.
// Copies a 3x4 matrix and adds a local-space vec3 into the translation column.
void oot3d_translate_mtx3x4_local(float* dst_mtx3x4, const float* src_mtx3x4, const float* local_vec3) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_mtx3x4;
    register const float* src_reg __asm__("r1") = src_mtx3x4;
    register const float* local_reg __asm__("r2") = local_vec3;

    __asm__ volatile(
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "vldmia %2, {s12, s13, s14}\n\t"
        "vmla.f32 s3, s0, s12\n"
        "vmla.f32 s7, s4, s12\n"
        "vmla.f32 s11, s8, s12\n"
        "vmla.f32 s3, s1, s13\n"
        "vmla.f32 s7, s5, s13\n"
        "vmla.f32 s11, s9, s13\n"
        "vmla.f32 s3, s2, s14\n"
        "vmla.f32 s7, s6, s14\n"
        "vmla.f32 s11, s10, s14\n"
        "vstmia %0, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}"
        :
        : "r"(dst_reg), "r"(src_reg), "r"(local_reg)
        : "memory");
#else
    dst_mtx3x4[0] = src_mtx3x4[0];
    dst_mtx3x4[1] = src_mtx3x4[1];
    dst_mtx3x4[2] = src_mtx3x4[2];
    dst_mtx3x4[3] = src_mtx3x4[3] + (src_mtx3x4[0] * local_vec3[0]) + (src_mtx3x4[1] * local_vec3[1]) + (src_mtx3x4[2] * local_vec3[2]);
    dst_mtx3x4[4] = src_mtx3x4[4];
    dst_mtx3x4[5] = src_mtx3x4[5];
    dst_mtx3x4[6] = src_mtx3x4[6];
    dst_mtx3x4[7] = src_mtx3x4[7] + (src_mtx3x4[4] * local_vec3[0]) + (src_mtx3x4[5] * local_vec3[1]) + (src_mtx3x4[6] * local_vec3[2]);
    dst_mtx3x4[8] = src_mtx3x4[8];
    dst_mtx3x4[9] = src_mtx3x4[9];
    dst_mtx3x4[10] = src_mtx3x4[10];
    dst_mtx3x4[11] = src_mtx3x4[11] + (src_mtx3x4[8] * local_vec3[0]) + (src_mtx3x4[9] * local_vec3[1]) + (src_mtx3x4[10] * local_vec3[2]);
#endif
}

// Ghidra: FUN_0032c78c.
// Copies a 3x4 matrix and adds a world-space vec3 into the translation column.
void oot3d_translate_mtx3x4_world(float* dst_mtx3x4, const float* world_vec3, const float* src_mtx3x4) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_mtx3x4;
    register const float* world_reg __asm__("r1") = world_vec3;
    register const float* src_reg __asm__("r2") = src_mtx3x4;

    __asm__ volatile(
        "vldmia %2, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "vldmia %1, {s12, s13, s14}\n\t"
        "vadd.f32 s3, s3, s12\n"
        "vadd.f32 s7, s7, s13\n"
        "vadd.f32 s11, s11, s14\n"
        "vstmia %0, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}"
        :
        : "r"(dst_reg), "r"(world_reg), "r"(src_reg)
        : "memory");
#else
    dst_mtx3x4[0] = src_mtx3x4[0];
    dst_mtx3x4[1] = src_mtx3x4[1];
    dst_mtx3x4[2] = src_mtx3x4[2];
    dst_mtx3x4[3] = src_mtx3x4[3] + world_vec3[0];
    dst_mtx3x4[4] = src_mtx3x4[4];
    dst_mtx3x4[5] = src_mtx3x4[5];
    dst_mtx3x4[6] = src_mtx3x4[6];
    dst_mtx3x4[7] = src_mtx3x4[7] + world_vec3[1];
    dst_mtx3x4[8] = src_mtx3x4[8];
    dst_mtx3x4[9] = src_mtx3x4[9];
    dst_mtx3x4[10] = src_mtx3x4[10];
    dst_mtx3x4[11] = src_mtx3x4[11] + world_vec3[2];
#endif
}

// Ghidra: FUN_0036ac0c.
// Converts a signed 16-bit vec3 into a float vec3.
void oot3d_vec3s_to_vec3f(float* dst_vec3, const s16* src_vec3s) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_vec3;
    register const s16* src_reg __asm__("r1") = src_vec3s;

    __asm__ volatile(
        "ldrsh r2, [%1, #0]\n"
        "vmov s0, r2\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0]\n"
        "ldrsh r2, [%1, #2]\n"
        "vmov s0, r2\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0, #4]\n"
        "ldrsh %1, [%1, #4]\n"
        "vmov s0, %1\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0, #8]"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "memory");
#else
    dst_vec3[0] = (float)src_vec3s[0];
    dst_vec3[1] = (float)src_vec3s[1];
    dst_vec3[2] = (float)src_vec3s[2];
#endif
}

// Ghidra: FUN_0031459c.
// Converts a signed 32-bit vec3 into floats and writes it into object field
// offsets 0x28, 0x2c, and 0x30.
void oot3d_vec3s32_to_field_28(float* dst_object, const s32* src_vec3) {
#if defined(__arm__)
    register float* dst_reg __asm__("r0") = dst_object;
    register const s32* src_reg __asm__("r1") = src_vec3;

    __asm__ volatile(
        "ldr r2, [%1, #0]\n"
        "vmov s0, r2\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0, #0x28]\n"
        "ldr r2, [%1, #4]\n"
        "vmov s0, r2\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0, #0x2c]\n"
        "ldr %1, [%1, #8]\n"
        "vmov s0, %1\n"
        "vcvt.f32.s32 s0, s0\n"
        "vstr.32 s0, [%0, #0x30]"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "memory");
#else
    dst_object[10] = (float)src_vec3[0];
    dst_object[11] = (float)src_vec3[1];
    dst_object[12] = (float)src_vec3[2];
#endif
}
