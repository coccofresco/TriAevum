#ifndef OOT3D_TARGET_NATIVE_PRELUDE_H
#define OOT3D_TARGET_NATIVE_PRELUDE_H

#include "oot3d/types.h"

#include <stdbool.h>
#include <stdint.h>

/* Target image-relative audio work-table base used by reviewed audio bodies. */
extern uint32_t iRam002d3888;

#if defined(_MSC_VER)
#include <intrin.h>
#include <math.h>
#endif

#if !defined(INFINITY) && (defined(__GNUC__) || defined(__clang__))
#define INFINITY (__builtin_inff())
#endif

/*
 * Target-width types retained by bounded native translations until a wider
 * owning structure supplies a stronger field type.  Unlike the bulk recovery
 * lane, users of this header have complete target bodies and reviewed entry
 * boundaries; these aliases only express still-unnamed 3DS data.
 */
typedef u8 undefined1;
typedef u16 undefined2;
typedef u32 undefined3;
typedef u32 undefined4;
typedef u64 undefined6;
typedef u64 undefined8;
typedef u8 byte;
typedef s8 sbyte;
typedef u16 ushort;
typedef u32 uint;
typedef void undefined;
typedef s32 int3;
typedef int64_t int6;
typedef u32 uint3;

/*
 * A target LDREX/STREX loop that replaces a signed word with its two's-
 * complement negation.  The unsigned subtraction preserves ARM wraparound for
 * 0x80000000 while the compare/exchange preserves the retry behavior.
 */
static inline s32 Oot3dAtomic_NegateS32(volatile s32* address) {
    u32 expected = __atomic_load_n((volatile u32*)address, __ATOMIC_ACQUIRE);
    for (;;) {
        const u32 desired = 0u - expected;
        if (__atomic_compare_exchange_n((volatile u32*)address, &expected,
                                        desired, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return (s32)expected;
        }
    }
}

static inline u32 Oot3dAtomic_ExchangeU32(volatile u32* address, u32 value) {
    return __atomic_exchange_n(address, value, __ATOMIC_ACQ_REL);
}

#define Oot3dAtomic_Exchange(address, value) \
    __atomic_exchange_n((address), (value), __ATOMIC_ACQ_REL)
#define Oot3dAtomic_FetchAdd(address, value) \
    __atomic_fetch_add((address), (value), __ATOMIC_ACQ_REL)
#define Oot3dAtomic_CompareExchange(address, expected, desired) \
    __atomic_compare_exchange_n((address), (expected), (desired), 0, \
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)

static inline s32 Oot3dAtomic_FetchAddS32(volatile s32* address, s32 delta) {
    return (s32)__atomic_fetch_add((volatile u32*)address, (u32)delta,
                                   __ATOMIC_ACQ_REL);
}

static inline bool Oot3dAtomic_TryNegatePositiveS32(
    volatile s32* address
) {
    u32 expected = __atomic_load_n((volatile u32*)address, __ATOMIC_ACQUIRE);
    while ((s32)expected > 0) {
        const u32 desired = 0u - expected;
        if (__atomic_compare_exchange_n((volatile u32*)address, &expected,
                                        desired, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return true;
        }
    }
    return false;
}

static inline bool Oot3dAtomic_TryDecrementPositiveS32(
    volatile s32* address
) {
    u32 expected = __atomic_load_n((volatile u32*)address, __ATOMIC_ACQUIRE);
    while ((s32)expected > 0) {
        if (__atomic_compare_exchange_n((volatile u32*)address, &expected,
                                        expected - 1u, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return true;
        }
    }
    return false;
}
typedef u64 uint6;
typedef uint32_t code();

/* Portable spellings for Ghidra's ARM VFP conversion p-code. */
float VectorSignedToFloat(s32 value, u8 roundingMode);
float VectorUnsignedToFloat(u32 value, u8 roundingMode);
s32 VectorFloatToSigned(float value, u8 roundingMode);
u32 VectorFloatToUnsigned(float value, u8 roundingMode);
typedef int64_t longlong;
typedef u64 ulonglong;

/*
 * Three-float homogeneous aggregates returned through s0/s1/s2 by reviewed
 * camera helpers.  The distinct names retain the spherical/Cartesian meaning
 * while expressing the target hard-float ABI as ordinary C.
 */
typedef struct Oot3dNativeSphGeoHfa {
    float radius;
    float pitch;
    float yaw;
} Oot3dNativeSphGeoHfa;

typedef struct Oot3dNativeVec3fHfa {
    float x;
    float y;
    float z;
} Oot3dNativeVec3fHfa;

/*
 * Ghidra's storage-aware signature catalog names the same reviewed ABI shape
 * `float3`.  Keep that tool-facing spelling local to target translations.
 */
typedef Oot3dNativeVec3fHfa float3;

typedef struct Oot3dNativeColorRGBA8 {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} Oot3dNativeColorRGBA8;

/* Portable IEEE-754 word access for reviewed hard-float ABI parameters. */
static inline u32 oot3d_float_to_bits(float value) {
    union {
        float as_float;
        u32 as_u32;
    } bits = { .as_float = value };
    return bits.as_u32;
}

static inline float oot3d_u32_to_float(u32 value) {
    union {
        u32 as_u32;
        float as_float;
    } bits = { .as_u32 = value };
    return bits.as_float;
}

/*
 * FPSCR is target machine state, not a hidden C function argument.  Keep the
 * single architecture-specific read here so promoted bodies can express that
 * dependency explicitly without embedding per-function inline assembly.
 */
static inline u32 oot3d_target_read_fpscr(void) {
#if defined(__arm__) && defined(__GNUC__)
    u32 value;
    __asm__("vmrs %0, fpscr" : "=r"(value));
    return value;
#else
    return 0;
#endif
}

static inline void oot3d_target_write_fpscr(u32 value) {
#if defined(__arm__) && defined(__GNUC__)
    __asm__ volatile("vmsr fpscr, %0" : : "r"(value) : "memory", "cc");
#else
    /* Host floating-point lowering already uses IEEE binary32 operations. */
    (void)value;
#endif
}

/*
 * CP15 c13 holds the current CTR thread-local storage base.  Keep the target
 * instruction behind a typed 32-bit address contract and let host runtimes
 * expose the corresponding guest TLS address explicitly.
 */
#if defined(OOT3D_HOST_TARGET_THREAD_POINTER)
u32 oot3d_host_target_thread_pointer(void);
#endif

static inline u32 oot3d_target_thread_pointer(void) {
#if defined(OOT3D_HOST_TARGET_THREAD_POINTER)
    return oot3d_host_target_thread_pointer();
#elif defined(__arm__) && defined(__GNUC__)
    u32 address;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(address));
    return address;
#else
    static _Thread_local u8 host_thread_marker;
    return (u32)(uint32_t)&host_thread_marker;
#endif
}

static inline void oot3d_target_data_memory_barrier(void) {
#if defined(__arm__) && defined(__GNUC__)
    const u32 zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" : : "r"(zero) : "memory");
#elif defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

static inline void oot3d_target_data_synchronization_barrier(void) {
#if defined(__arm__) && defined(__GNUC__)
    const u32 zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(zero) : "memory");
#elif defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/*
 * SVC 0x28 returns the 3DS system tick in r1:r0.  Host integrations can
 * supply the same monotonically increasing unit through the opt-in hook.
 */
#if defined(OOT3D_HOST_TARGET_SYSTEM_TICK)
u64 oot3d_host_target_system_tick(void);
#endif

static inline u64 oot3d_target_system_tick(void) {
#if defined(OOT3D_HOST_TARGET_SYSTEM_TICK)
    return oot3d_host_target_system_tick();
#elif defined(__arm__) && defined(__GNUC__)
    register u32 low __asm__("r0");
    register u32 high __asm__("r1");
    __asm__ volatile("svc 0x28" : "=r"(low), "=r"(high) : : "memory");
    return ((u64)high << 32) | low;
#else
    static u64 host_tick;
    return host_tick++;
#endif
}

/*
 * SVC 0x14 consumes a result word in r0.  Making that register contract
 * explicit avoids treating the service call as an ordinary no-argument C
 * function.
 */
static inline void oot3d_target_notify_result(u32 result) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 value __asm__("r0") = result;
    __asm__ volatile("svc 0x14" : "+r"(value) : : "memory");
#else
    (void)result;
#endif
}

#if defined(_MSC_VER)
#define NAN(value) (_isnan((double)(value)))
#define LZCOUNT(value) ((u32)__lzcnt((u32)(value)))
#define SQRT(value) (sqrtf((float)(value)))
#else
#define NAN(value) (__builtin_isnan((float)(value)))
#define LZCOUNT(value) ((u32)__builtin_clz((u32)(value)))
#define SQRT(value) (__builtin_sqrtf((float)(value)))
#endif

#define CONCAT11(high, low) ((((u16)(u8)(high)) << 8) | (u8)(low))
#define CONCAT12(high, low) ((((u32)(u8)(high)) << 16) | (u16)(low))
#define CONCAT13(high, low) ((((u32)(u8)(high)) << 24) | ((u32)(low) & 0xffffffu))
#define CONCAT14(high, low) ((((u64)(u8)(high)) << 32) | (u32)(low))
#define CONCAT21(high, low) ((((u32)(u16)(high)) << 8) | (u8)(low))
#define CONCAT22(high, low) ((((u32)(u16)(high)) << 16) | (u16)(low))
#define CONCAT31(high, low) ((((u32)(high) & 0xffffffu) << 8) | (u8)(low))
#define CONCAT44(high, low) ((((u64)(u32)(high)) << 32) | (u32)(low))

#define OOT3D_PIECE_MASK(size) (UINT64_MAX >> (64u - 8u * (size)))
#define OOT3D_SUBPIECE(value, offset, size) \
    (((u64)(value) >> (8u * (offset))) & OOT3D_PIECE_MASK(size))
#define OOT3D_SET_SUBPIECE(value, offset, size, piece)            \
    ((value) = (((u64)(value) &                                  \
                 ~(OOT3D_PIECE_MASK(size) << (8u * (offset)))) | \
                (((u64)(piece) & OOT3D_PIECE_MASK(size))         \
                 << (8u * (offset)))))

#define VectorFloatToUnsigned(value, mode) ((u32)(float)(value))
#define VectorSignedToFloat(value, mode) ((float)(s32)(value))
#define VectorUnsignedToFloat(value, mode) ((float)(u32)(value))
#define UnsignedDoesSaturate(value, width) \
    ((u64)(value) > ((((u64)1) << (width)) - 1))
#define UnsignedSaturate(value, width)                              \
    (UnsignedDoesSaturate((value), (width))                        \
         ? (u32)((((u64)1) << (width)) - 1)                        \
         : (u32)(value))

#endif
