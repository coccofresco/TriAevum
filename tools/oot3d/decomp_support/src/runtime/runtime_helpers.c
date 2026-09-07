#include "oot3d/runtime_helpers.h"

// Ghidra: FUN_0010004c.
// The source words are read from offsets 0,1,2,4,5,6,8,9,10 and packed
// contiguously into a 36-byte destination. The skipped source words look
// intentional, so this is not treated as a plain memcpy.
void oot3d_copy_sparse_words_36(Oot3dPackedCopy36* dst, const u32* src) {
#if defined(__arm__)
    register Oot3dPackedCopy36* dst_reg __asm__("r0") = dst;
    register const u32* src_reg __asm__("r1") = src;

    __asm__ volatile(
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11}\n\t"
        "cpy r2, %0\n\t"
        "vstmia r2!, {s0, s1, s2}\n\t"
        "vstmia r2!, {s4, s5, s6}\n\t"
        "vstmia r2, {s8, s9, s10}"
        :
        : "r"(dst_reg), "r"(src_reg)
        : "r2", "memory");
#else
    dst->word0 = src[0];
    dst->word1 = src[1];
    dst->word2 = src[2];
    dst->word3 = src[4];
    dst->word4 = src[5];
    dst->word5 = src[6];
    dst->word6 = src[8];
    dst->word7 = src[9];
    dst->word8 = src[10];
#endif
}

// Ghidra: FUN_00363f20.
void oot3d_copy_u8x4(u8* dst, const u8* src) {
#if defined(__arm__)
    register u8* dst_reg __asm__("r0") = dst;
    register const u8* src_reg __asm__("r1") = src;

    __asm__ volatile(
        "ldrb r2, [%1, #0]\n\t"
        "strb r2, [%0, #0]\n\t"
        "ldrb r2, [%1, #1]\n\t"
        "strb r2, [%0, #1]\n\t"
        "ldrb r2, [%1, #2]\n\t"
        "strb r2, [%0, #2]\n\t"
        "ldrb %1, [%1, #3]\n\t"
        "strb %1, [%0, #3]"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "memory");
#else
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
#endif
}

// Ghidra: FUN_00304380.
void oot3d_copy_u8x5(u8* dst, const u8* src) {
#if defined(__arm__)
    __asm__ volatile(
        "ldrb r2, [%1, #0]\n\t"
        "strb r2, [%0, #0]\n\t"
        "ldrb r3, [%1, #1]\n\t"
        "strb r3, [%0, #1]\n\t"
        "ldrb r12, [%1, #2]\n\t"
        "strb r12, [%0, #2]\n\t"
        "ldrb r2, [%1, #3]\n\t"
        "strb r2, [%0, #3]\n\t"
        "ldrb r3, [%1, #4]\n\t"
        "strb r3, [%0, #4]"
        :
        : "r"(dst), "r"(src)
        : "r2", "r3", "r12", "memory");
#else
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
#endif
}

// Ghidra: FUN_0035fb94.
void oot3d_copy_u16x3(u16* dst, const u16* src) {
#if defined(__arm__)
    __asm__ volatile(
        "ldrh r2, [%1, #0]\n\t"
        "strh r2, [%0, #0]\n\t"
        "ldrh r12, [%1, #2]\n\t"
        "strh r12, [%0, #2]\n\t"
        "ldrh r3, [%1, #4]\n\t"
        "strh r3, [%0, #4]"
        :
        : "r"(dst), "r"(src)
        : "r2", "r3", "r12", "memory");
#else
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
#endif
}

// Ghidra: FUN_002f48d4.
void oot3d_copy_u16x4(u16* dst, const u16* src) {
#if defined(__arm__)
    __asm__ volatile(
        "ldrh r2, [%1, #0]\n\t"
        "strh r2, [%0, #0]\n\t"
        "ldrh r12, [%1, #2]\n\t"
        "strh r12, [%0, #2]\n\t"
        "ldrh r3, [%1, #4]\n\t"
        "strh r3, [%0, #4]\n\t"
        "ldrh r2, [%1, #6]\n\t"
        "strh r2, [%0, #6]"
        :
        : "r"(dst), "r"(src)
        : "r2", "r3", "r12", "memory");
#else
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
#endif
}

// Ghidra: FUN_0036df4c.
void oot3d_copy_u32x3(u32* dst, const u32* src) {
#if defined(__arm__)
    __asm__ volatile(
        "ldmia %1, {r2, r3, r12}\n\t"
        "stmia %0, {r2, r3, r12}"
        :
        : "r"(dst), "r"(src)
        : "r2", "r3", "r12", "memory");
#else
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
#endif
}

// Ghidra: FUN_0033ddbc.
void oot3d_copy_u32x9(u32* dst, const u32* src) {
#if defined(__arm__)
    register u32* dst_reg __asm__("r0") = dst;
    register const u32* src_reg __asm__("r1") = src;

    __asm__ volatile(
        "vldmia %1!, {s0, s1, s2, s3, s4}\n\t"
        "cpy r2, %0\n\t"
        "vldmia %1, {s5, s6, s7, s8}\n\t"
        "vstmia r2!, {s0, s1, s2, s3, s4}\n\t"
        "vstmia r2, {s5, s6, s7, s8}"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "memory");
#else
    for (size_t i = 0; i < 9; i++) {
        dst[i] = src[i];
    }
#endif
}

// Ghidra: FUN_003721e0.
// Copies a 12-word payload into an object field block starting at byte offset
// 0x7c. The owning object type is not recovered yet.
void oot3d_copy_u32x12_to_field_7c(void* dst_object, const u32* src) {
#if defined(__arm__)
    register void* dst_reg __asm__("r0") = dst_object;
    register const u32* src_reg __asm__("r1") = src;

    __asm__ volatile(
        "str r4, [sp, #-4]!\n\t"
        "cpy r4, %1\n\t"
        "add %0, %0, #0x7c\n\t"
        "ldmia r4!, {%1, r2, r3, r12}\n\t"
        "stmia %0!, {%1, r2, r3, r12}\n\t"
        "ldmia r4!, {%1, r2, r3, r12}\n\t"
        "stmia %0!, {%1, r2, r3, r12}\n\t"
        "ldmia r4, {%1, r2, r3, r12}\n\t"
        "stmia %0, {%1, r2, r3, r12}\n\t"
        "ldr r4, [sp], #4"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "r3", "r12", "memory");
#else
    u32* dst = (u32*)((u8*)dst_object + 0x7c);
    for (size_t i = 0; i < 12; i++) {
        dst[i] = src[i];
    }
#endif
}

// Ghidra: FUN_0037571c.
// Reads a byte flag from an object field at byte offset 0x22a0.
u32 oot3d_get_flag_22a0(const void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "add %0, %0, #0x2000\n\t"
        "add %0, %0, #0x298\n\t"
        "ldrb %0, [%0, #8]"
        : "+r"(result)
        :
        : "memory");
    return result;
#else
    return *((const u8*)object + 0x22a0);
#endif
}

// Ghidra: FUN_0037632c.
// Copies a three-word field from source offset 0x28 to destination offset 0x4c.
void oot3d_copy_u32x3_field_28_to_field_4c(const void* src_object, void* dst_object) {
#if defined(__arm__)
    register const void* src_reg __asm__("r0") = src_object;
    register void* dst_reg __asm__("r1") = dst_object;

    __asm__ volatile(
        "add r2, %0, #0x28\n\t"
        "add %0, %1, #0x4c\n\t"
        "ldmia r2, {%1, r3, r12}\n\t"
        "stmia %0, {%1, r3, r12}"
        : "+r"(src_reg), "+r"(dst_reg)
        :
        : "r2", "r3", "r12", "memory");
#else
    const u32* src = (const u32*)((const u8*)src_object + 0x28);
    u32* dst = (u32*)((u8*)dst_object + 0x4c);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
#endif
}

// Ghidra: FUN_0036bc98.
// Tests bit 0x100 in word field 4, clears it when set, and returns whether it
// had been set.
u32 oot3d_test_and_clear_flag_100(void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "ldr r1, [%0, #4]\n\t"
        "tst r1, #0x100\n\t"
        "bicne r1, r1, #0x100\n\t"
        "strne r1, [%0, #4]\n\t"
        "moveq %0, #0\n\t"
        "movne %0, #1"
        : "+r"(result)
        :
        : "r1", "cc", "memory");
    return result;
#else
    u32* flags = (u32*)((u8*)object + 4);
    u32 had_flag = (*flags & 0x100) != 0;
    if (had_flag) {
        *flags &= ~0x100u;
    }
    return had_flag;
#endif
}

// Ghidra: FUN_00373264.
// Clears two flag groups from word field 4, then stores a value at field 0x24.
void oot3d_clear_flags_and_set_field_24(void* object, u32 value) {
#if defined(__arm__)
    __asm__ volatile(
        "ldr r2, [%0, #4]\n\t"
        "bic r2, r2, #0x10000000\n\t"
        "bic r2, r2, #0x380000\n\t"
        "str r2, [%0, #4]!\n\t"
        "str %1, [%0, #0x20]"
        : "+r"(object)
        : "r"(value)
        : "r2", "memory");
#else
    u32* flags = (u32*)((u8*)object + 4);
    *flags &= ~0x10000000u;
    *flags &= ~0x00380000u;
    *(u32*)((u8*)object + 0x24) = value;
#endif
}

// Ghidra: FUN_00371e40.
// Returns whether the word at object offset 0x124 is nonzero.
u32 oot3d_has_field_124(const void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "ldr %0, [%0, #0x124]\n\t"
        "cmp %0, #0\n\t"
        "movne %0, #1"
        : "+r"(result)
        :
        : "cc", "memory");
    return result;
#else
    return *(const u32*)((const u8*)object + 0x124) != 0;
#endif
}

// Ghidra: FUN_00373074.
// Checks whether the signed halfword at object + index * 0x80 + 4 is positive.
u32 oot3d_field_80x4_positive(const void* object, s32 index) {
    return *(const s16*)((const u8*)object + (index * 0x80) + 4) > 0;
}

// Ghidra: FUN_002fc3e4.
void* oot3d_offset_from_field_18_stride_40(const void* object, s32 index) {
    return (u8*)(uintptr_t)(*(const u32*)((const u8*)object + 0x18)) + (index * 0x40);
}

// Ghidra: FUN_002fc3f0.
void* oot3d_offset_from_field_14_stride_20(const void* object, s32 index) {
    return (u8*)(uintptr_t)(*(const u32*)((const u8*)object + 0x14)) + (index * 0x20);
}

// Ghidra: FUN_002fc3fc.
void* oot3d_offset_from_field_c_stride_30(const void* object, s32 index) {
    return (u8*)(uintptr_t)(*(const u32*)((const u8*)object + 0xc)) + (index * 0x30);
}

// Ghidra: FUN_003518cc.
u32 oot3d_get_flag_1710_mask_10(const void* object) {
    return *(const u32*)((const u8*)object + 0x1710) & 0x10;
}

// Ghidra: FUN_00303ea8.
u32 oot3d_get_field_10_if_byte_8_is_1(const void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "ldrb r1, [%0, #8]\n\t"
        "cmp r1, #1\n\t"
        "ldreq %0, [%0, #0x10]\n\t"
        "movne %0, #0"
        : "+r"(result)
        :
        : "r1", "cc", "memory");
    return result;
#else
    return *((const u8*)object + 8) == 1 ? *(const u32*)((const u8*)object + 0x10) : 0;
#endif
}

// Ghidra: FUN_00372f0c.
u32 oot3d_get_indexed_field_58_entry(const void* object, u32 index) {
#if defined(__arm__)
    u32 result = (u32)object;
    register u32 index_reg __asm__("r1") = index;

    __asm__ volatile(
        "ldr r2, [%0, #0x30]\n\t"
        "mov r3, #0\n\t"
        "cmn r2, #1\n\t"
        "ldrne r12, [%0, #0xc]\n\t"
        "ldrne r2, [r12, r2, lsl #4]\n\t"
        "moveq r2, #0\n\t"
        "cmp r2, %1\n\t"
        "ldrhi %0, [%0, #0x58]\n\t"
        "ldrhi r3, [%0, %1, lsl #2]\n\t"
        "cpy %0, r3"
        : "+r"(result)
        : "r"(index_reg)
        : "r2", "r3", "r12", "cc", "memory");
    return result;
#else
    u32 count = 0;
    s32 slot = *(const s32*)((const u8*)object + 0x30);
    if (slot != -1) {
        count = *(const u32*)(uintptr_t)(*(const u32*)((const u8*)object + 0xc) + (slot * 0x10));
    }
    if (index < count) {
        return *(const u32*)(uintptr_t)(*(const u32*)((const u8*)object + 0x58) + (index * 4));
    }
    return 0;
#endif
}

// Ghidra: FUN_00375750.
u32 oot3d_get_indexed_field_60_entry(const void* object, u32 index) {
#if defined(__arm__)
    u32 result = (u32)object;
    register u32 index_reg __asm__("r1") = index;

    __asm__ volatile(
        "ldr r2, [%0, #0x34]\n\t"
        "mov r3, #0\n\t"
        "cmn r2, #1\n\t"
        "ldrne r12, [%0, #0xc]\n\t"
        "ldrne r2, [r12, r2, lsl #4]\n\t"
        "moveq r2, #0\n\t"
        "cmp r2, %1\n\t"
        "ldrhi %0, [%0, #0x60]\n\t"
        "ldrhi r3, [%0, %1, lsl #2]\n\t"
        "cpy %0, r3"
        : "+r"(result)
        : "r"(index_reg)
        : "r2", "r3", "r12", "cc", "memory");
    return result;
#else
    u32 count = 0;
    s32 slot = *(const s32*)((const u8*)object + 0x34);
    if (slot != -1) {
        count = *(const u32*)(uintptr_t)(*(const u32*)((const u8*)object + 0xc) + (slot * 0x10));
    }
    if (index < count) {
        return *(const u32*)(uintptr_t)(*(const u32*)((const u8*)object + 0x60) + (index * 4));
    }
    return 0;
#endif
}

// Ghidra: FUN_00375eb8.
u32 oot3d_decrement_byte_b7_by_b8(void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "ldrb r1, [%0, #0xb7]\n\t"
        "ldrb r2, [%0, #0xb8]\n\t"
        "cmp r1, r2\n\t"
        "subhi r1, r1, r2\n\t"
        "movls r1, #0\n\t"
        "strb r1, [%0, #0xb7]\n\t"
        "and %0, r1, #0xff"
        : "+r"(result)
        :
        : "r1", "r2", "cc", "memory");
    return result;
#else
    u8* field = (u8*)object + 0xb7;
    u8 decrement = *((u8*)object + 0xb8);
    *field = *field > decrement ? (u8)(*field - decrement) : 0;
    return *field;
#endif
}

// Ghidra: FUN_0037573c.
// Stores a word in the large field block at 0x229c and clears 0x22ac.
void oot3d_set_field_229c_clear_22ac(void* object, u32 value) {
#if defined(__arm__)
    register u32 value_r1 __asm__("r1") = value;

    __asm__ volatile(
        "add %0, %0, #0x2000\n\t"
        "str %1, [%0, #0x29c]\n\t"
        "mov r1, #0\n\t"
        "str r1, [%0, #0x2ac]"
        : "+r"(object)
        : "r"(value_r1)
        : "memory");
#else
    *(u32*)((u8*)object + 0x229c) = value;
    *(u32*)((u8*)object + 0x22ac) = 0;
#endif
}

// Ghidra: FUN_0036adf4.
u32 oot3d_get_byte_1b8_mask_2_bool(const void* object) {
#if defined(__arm__)
    u32 result = (u32)object;

    __asm__ volatile(
        "ldrb %0, [%0, #0x1b8]\n\t"
        "ands %0, %0, #2\n\t"
        "movne %0, #1"
        : "+r"(result)
        :
        : "cc", "memory");
    return result;
#else
    return (*((const u8*)object + 0x1b8) & 2) != 0;
#endif
}

// Ghidra: FUN_00367c48.
void oot3d_set_byte_1b6(void* object) {
#if defined(__arm__)
    __asm__ volatile(
        "mov r1, #1\n\t"
        "strb r1, [%0, #0x1b6]"
        :
        : "r"(object)
        : "r1", "memory");
#else
    *((u8*)object + 0x1b6) = 1;
#endif
}

// Ghidra: FUN_00367c54.
void oot3d_clear_byte_1b6(void* object) {
#if defined(__arm__)
    __asm__ volatile(
        "mov r1, #0\n\t"
        "strb r1, [%0, #0x1b6]"
        :
        : "r"(object)
        : "r1", "memory");
#else
    *((u8*)object + 0x1b6) = 0;
#endif
}

// Ghidra: FUN_002f99f4.
u32 oot3d_first_word_lsl_6(const u32* value) {
    return value[0] << 6;
}

// Ghidra: FUN_002f9a00.
u32 oot3d_first_word_lsl_5(const u32* value) {
    return value[0] << 5;
}

// Ghidra: FUN_002f9a0c.
u32 oot3d_first_word_mul_48(const u32* value) {
    return value[0] * 48;
}

// Ghidra: FUN_0047d568.
void oot3d_store_child14_byte16(void* object, u8 value) {
#if defined(__arm__)
    __asm__ volatile(
        "ldr %0, [%0, #0x14]\n\t"
        "cpy %0, %0\n\t"
        "strb %1, [%0, #0x16]"
        : "+r"(object)
        : "r"(value)
        : "memory");
#else
    *(u8*)(uintptr_t)(*(u32*)((u8*)object + 0x14) + 0x16) = value;
#endif
}

// Ghidra: FUN_003589cc.
void oot3d_set_byte_index_plus_4(void* object, s32 index) {
#if defined(__arm__)
    __asm__ volatile(
        "add %0, %0, %1\n\t"
        "mov r2, #1\n\t"
        "strb r2, [%0, #4]"
        : "+r"(object)
        : "r"(index)
        : "r2", "memory");
#else
    *((u8*)object + index + 4) = 1;
#endif
}

// Ghidra: FUN_00372224.
void oot3d_copy_u32x12_if_distinct(u32* dst, const u32* src) {
#if defined(__arm__)
    register u32* dst_reg __asm__("r0") = dst;
    register const u32* src_reg __asm__("r1") = src;

    __asm__ volatile(
        "cmp %1, %0\n\t"
        "bxeq lr\n\t"
        "vldmia %1!, {s0, s1, s2, s3, s4, s5}\n\t"
        "cpy r2, %0\n\t"
        "vldmia %1, {s6, s7, s8, s9, s10, s11}\n\t"
        "vstmia r2!, {s0, s1, s2, s3, s4, s5}\n\t"
        "vstmia r2, {s6, s7, s8, s9, s10, s11}"
        : "+r"(dst_reg), "+r"(src_reg)
        :
        : "r2", "cc", "memory");
#else
    if (dst == src) {
        return;
    }
    for (size_t i = 0; i < 12; i++) {
        dst[i] = src[i];
    }
#endif
}

// Ghidra: FUN_00324744.
void oot3d_copy_u32x16_if_distinct(u32* dst, const u32* src) {
#if defined(__arm__)
    __asm__ volatile(
        "cmp %1, %0\n\t"
        "bxeq lr\n\t"
        "vldmia %1, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15}\n\t"
        "vstmia %0, {s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15}"
        :
        : "r"(dst), "r"(src)
        : "cc", "memory");
#else
    if (dst == src) {
        return;
    }
    for (size_t i = 0; i < 16; i++) {
        dst[i] = src[i];
    }
#endif
}

// Ghidra: FUN_001000ec.
// This follows the observed behavior, including early termination on NUL and
// the byte-wise result when a mismatch is found.
int oot3d_strncmp(const char* lhs, const char* rhs, size_t limit) {
#if defined(__arm__)
    __asm__ volatile(
        ".syntax unified\n"
        "orr r3, r0, r1\n"
        "push {r4, r5, lr}\n"
        "tst r3, #3\n"
        "bne 2f\n"
        "ldr r5, 5f\n"
        "1:\n"
        "cmp r2, #4\n"
        "bcc 2f\n"
        "ldr r3, [r0], #4\n"
        "ldr r12, [r1], #4\n"
        "sub r2, r2, #4\n"
        "sub r4, r3, r5\n"
        "bic r4, r4, r3\n"
        "ands r4, r4, r5, lsl #7\n"
        "cmpeq r3, r12\n"
        "beq 1b\n"
        "and r0, r3, #0xff\n"
        "and r1, r12, #0xff\n"
        "sub r0, r0, r1\n"
        "orrs r1, r0, r4, lsl #24\n"
        "moveq r0, r3, lsl #16\n"
        "moveq r1, r12, lsl #16\n"
        "moveq r0, r0, lsr #16\n"
        "moveq r1, r1, lsr #16\n"
        "subeq r0, r0, r1\n"
        "orrseq r1, r0, r4, lsl #16\n"
        "biceq r0, r3, #0xff000000\n"
        "biceq r1, r12, #0xff000000\n"
        "subeq r0, r0, r1\n"
        "orrseq r1, r0, r4, lsl #8\n"
        "popne {r4, r5, pc}\n"
        "mov r0, r3, lsr #24\n"
        "sub r0, r0, r12, lsr #24\n"
        "pop {r4, r5, pc}\n"
        "2:\n"
        "cmp r2, #0\n"
        "moveq r0, #0\n"
        "popeq {r4, r5, pc}\n"
        "3:\n"
        "ldrb r3, [r0], #1\n"
        "ldrb r12, [r1], #1\n"
        "cmp r3, #0\n"
        "beq 4f\n"
        "cmp r3, r12\n"
        "bne 4f\n"
        "subs r2, r2, #1\n"
        "ldrbne r3, [r0], #1\n"
        "ldrbne r12, [r1], #1\n"
        "cmpne r3, #0\n"
        "beq 4f\n"
        "cmp r3, r12\n"
        "bne 4f\n"
        "subs r2, r2, #1\n"
        "bne 3b\n"
        "4:\n"
        "sub r0, r3, r12\n"
        "pop {r4, r5, pc}\n"
        "5:\n"
        ".word 0xfefefeff\n"
    );
    __builtin_unreachable();
#else
    const u8* a = (const u8*)lhs;
    const u8* b = (const u8*)rhs;

    if (limit == 0) {
        return 0;
    }

    while (limit != 0 && *a != '\0' && *a == *b) {
        a++;
        b++;
        limit--;
    }

    if (limit == 0) {
        return 0;
    }

    return (int)*a - (int)*b;
#endif
}

// Ghidra: FUN_00482520.
// Clears the sign bit of a 64-bit floating-point bit pattern.
u64 oot3d_abs_f64_bits(u64 value) {
#if defined(__arm__)
    register u32 lo __asm__("r0") = (u32)value;
    register u32 hi __asm__("r1") = (u32)(value >> 32);

    __asm__ volatile(
        "vmov d0, %0, %1\n\t"
        "sub sp, sp, #8\n\t"
        "vstr.64 d0, [sp]\n\t"
        "ldr %0, [sp, #4]\n\t"
        "bic %0, %0, #0x80000000\n\t"
        "str %0, [sp, #4]\n\t"
        "vldr.64 d0, [sp]\n\t"
        "add sp, sp, #8\n\t"
        "vmov %0, %1, d0"
        : "+r"(lo), "+r"(hi)
        :
        : "memory");
    return ((u64)hi << 32) | lo;
#else
    return value & UINT64_C(0x7fffffffffffffff);
#endif
}

// Ghidra: FUN_002cfca0.
// Runtime evidence shows this is paired with oot3d_cos_idx8 by
// oot3d_make_transform_mtx_s16. Ghidra reduces the implementation to a VFP
// conversion using angle & 0xff, so the backing lookup table is not recovered
// in maintained source yet.
float oot3d_sin_idx8(u32 angle);

// Ghidra: FUN_00338f60.
float oot3d_cos_idx8(u32 angle);

// Ghidra: FUN_003679d0.
// Builds a 3x4 transform matrix from three signed 16-bit angle fields. Ghidra
// does not recover the float return registers cleanly, but the matrix layout
// matches sin/cos composition around the three axes.
void oot3d_make_transform_mtx_s16(float* dst_mtx3x4, const s16* angles);
