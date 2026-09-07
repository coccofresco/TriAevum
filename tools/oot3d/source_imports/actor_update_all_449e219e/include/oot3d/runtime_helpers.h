#ifndef OOT3D_RUNTIME_HELPERS_H
#define OOT3D_RUNTIME_HELPERS_H

#include "oot3d/types.h"

typedef struct Oot3dPackedCopy36 {
    u32 word0;
    u32 word1;
    u32 word2;
    u32 word3;
    u32 word4;
    u32 word5;
    u32 word6;
    u32 word7;
    u32 word8;
} Oot3dPackedCopy36;

void oot3d_copy_sparse_words_36(Oot3dPackedCopy36* dst, const u32* src);
void oot3d_copy_u8x4(u8* dst, const u8* src);
void oot3d_copy_u8x5(u8* dst, const u8* src);
void oot3d_copy_u16x3(u16* dst, const u16* src);
void oot3d_copy_u16x4(u16* dst, const u16* src);
void oot3d_copy_u32x3(u32* dst, const u32* src);
void oot3d_copy_u32x9(u32* dst, const u32* src);
void oot3d_copy_u32x12_to_field_7c(void* dst_object, const u32* src);
void oot3d_copy_u32x12_if_distinct(u32* dst, const u32* src);
void oot3d_copy_u32x16_if_distinct(u32* dst, const u32* src);
u32 oot3d_get_flag_22a0(const void* object);
void oot3d_copy_u32x3_field_28_to_field_4c(const void* src_object, void* dst_object);
u32 oot3d_test_and_clear_flag_100(void* object);
void oot3d_clear_flags_and_set_field_24(void* object, u32 value);
u32 oot3d_has_field_124(const void* object);
u32 oot3d_field_80x4_positive(const void* object, s32 index);
void* oot3d_offset_from_field_18_stride_40(const void* object, s32 index);
void* oot3d_offset_from_field_14_stride_20(const void* object, s32 index);
void* oot3d_offset_from_field_c_stride_30(const void* object, s32 index);
u32 oot3d_get_flag_1710_mask_10(const void* object);
void oot3d_set_byte_index_plus_4(void* object, s32 index);
u32 oot3d_get_field_10_if_byte_8_is_1(const void* object);
u32 oot3d_get_indexed_field_58_entry(const void* object, u32 index);
u32 oot3d_get_indexed_field_60_entry(const void* object, u32 index);
void oot3d_set_field_229c_clear_22ac(void* object, u32 value);
u32 oot3d_decrement_byte_b7_by_b8(void* object);
u32 oot3d_get_byte_1b8_mask_2_bool(const void* object);
void oot3d_set_byte_1b6(void* object);
void oot3d_clear_byte_1b6(void* object);
void oot3d_store_child14_byte16(void* object, u8 value);
u32 oot3d_first_word_lsl_6(const u32* value);
u32 oot3d_first_word_lsl_5(const u32* value);
u32 oot3d_first_word_mul_48(const u32* value);
int oot3d_strncmp(const char* lhs, const char* rhs, size_t limit);
u64 oot3d_abs_f64_bits(u64 value);
float Oot3d_SinInterpU16(u32 angle);
float oot3d_sin_idx8(u32 angle);
float Oot3d_CosInterpU16(u32 angle);
float oot3d_cos_idx8(u32 angle);
void oot3d_make_transform_mtx_s16(float* dst_mtx3x4, const s16* angles);
void oot3d_scale_mtx3x4_axes(float* dst_mtx3x4, const float* src_mtx3x4, const float* scale_vec3);
void oot3d_scale_mtx3x4_blocks(float* dst_mtx3x4, const float* scale_vec3, const float* src_mtx3x4);
void oot3d_mul_mtx3x3_vec3(float* dst_vec3, const float* mtx3x3, const float* vec3);
void oot3d_transform_mtx3x4_vec3(float* dst_vec3, const float* mtx3x4, const float* vec3);
void oot3d_translate_mtx3x4_local(float* dst_mtx3x4, const float* src_mtx3x4, const float* local_vec3);
void oot3d_translate_mtx3x4_world(float* dst_mtx3x4, const float* world_vec3, const float* src_mtx3x4);
void oot3d_vec3s_to_vec3f(float* dst_vec3, const s16* src_vec3s);
void oot3d_vec3s32_to_field_28(float* dst_object, const s32* src_vec3);

#endif
