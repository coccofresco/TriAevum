#ifndef OOT3D_PLATFORM_ABI_H
#define OOT3D_PLATFORM_ABI_H

#include "oot3d/types.h"

/*
 * Target-width ABI types used at the 3DS service boundary.  These remain
 * 32-bit values even in host tests: an address is resolved explicitly and a
 * handle is an operating-system token, never a native host pointer.
 */
typedef s32 Oot3dCtrResult;
typedef u32 Oot3dCtrHandle;
typedef u32 Oot3dTargetAddress32;

/* Process-wide arbiter created during CTR runtime initialization. */
extern Oot3dCtrHandle gOot3dAddressArbiterHandle
    __asm__("DAT_0054ab54");

typedef enum Oot3dCtrArbitrationType {
    OOT3D_CTR_ARBITRATION_SIGNAL = 0,
    OOT3D_CTR_ARBITRATION_WAIT_IF_LESS_THAN = 1,
    OOT3D_CTR_ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN = 2,
} Oot3dCtrArbitrationType;

/*
 * Host/recompiler implementation of the inline CTR ConnectToPort SVC path.
 * Native ARM builds keep the original SVC 0x2D boundary; host builds define
 * OOT3D_HOST_CTR_CONNECT_TO_PORT and provide this symbol.
 */
#if defined(OOT3D_HOST_CTR_CONNECT_TO_PORT)
Oot3dCtrResult oot3d_host_ctr_connect_to_port(
    Oot3dCtrHandle* outHandle,
    const char* portName
);
#endif

Oot3dCtrResult Oot3dCtr_ConnectToPort(
    Oot3dCtrHandle* outHandle,
    const char* portName
);
Oot3dCtrResult Oot3dCtr_ConnectServiceManager(
    Oot3dCtrHandle* outHandle
);

typedef enum Oot3dCtrResourceLimitType {
    OOT3D_CTR_RESOURCE_LIMIT_COMMIT = 1,
} Oot3dCtrResourceLimitType;

#define OOT3D_CTR_CURRENT_PROCESS_HANDLE ((Oot3dCtrHandle)0xFFFF8001u)

/*
 * SVC 0x3A returns signed 64-bit resource-limit counters.  Keeping this ABI
 * boundary here prevents the decompiler's temporary r1 value from being
 * mistaken for the high half of GetUsingMemorySize's C return value.
 */
static inline int64_t Oot3dCtr_GetResourceLimitCurrentValue(
    Oot3dCtrHandle resourceLimit,
    Oot3dCtrResourceLimitType type
) {
#if defined(OOT3D_HOST_CTR_RESOURCE_LIMIT_QUERY)
    extern int64_t oot3d_host_ctr_resource_limit_current_value(
        Oot3dCtrHandle resourceLimit,
        Oot3dCtrResourceLimitType type
    );
    return oot3d_host_ctr_resource_limit_current_value(resourceLimit, type);
#elif defined(__arm__) && defined(__GNUC__)
    int64_t value = 0;
    register int64_t* r0 __asm__("r0") = &value;
    register Oot3dCtrHandle r1 __asm__("r1") = resourceLimit;
    register Oot3dCtrResourceLimitType* r2 __asm__("r2") = &type;
    register u32 r3 __asm__("r3") = 1;
    __asm__ volatile(
        "svc 0x3a"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        :
        : "memory", "cc"
    );
    return value;
#else
    (void)resourceLimit;
    (void)type;
    return 0;
#endif
}

static inline Oot3dCtrResult Oot3dCtr_CloseHandle(Oot3dCtrHandle handle) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = handle;
    __asm__ volatile("svc 0x23" : "+r"(r0) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult svcCloseHandle(Oot3dCtrHandle handle);
    return svcCloseHandle(handle);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_SendSyncRequest(
    Oot3dCtrHandle handle
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = handle;
    __asm__ volatile("svc 0x32" : "+r"(r0) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult svcSendSyncRequest(Oot3dCtrHandle handle);
    return svcSendSyncRequest(handle);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_SleepThread(int64_t nanoseconds) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = (u32)nanoseconds;
    register u32 r1 __asm__("r1") = (u32)((u64)nanoseconds >> 32);
    __asm__ volatile(
        "svc 0x0a"
        : "+r"(r0), "+r"(r1)
        :
        : "memory", "cc"
    );
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_sleep_thread(
        int64_t nanoseconds
    );
    return oot3d_host_ctr_sleep_thread(nanoseconds);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_SignalEvent(
    Oot3dCtrHandle event
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = event;
    __asm__ volatile("svc 0x18" : "+r"(r0) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_signal_event(
        Oot3dCtrHandle event
    );
    return oot3d_host_ctr_signal_event(event);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_ClearEvent(
    Oot3dCtrHandle event
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = event;
    __asm__ volatile("svc 0x19" : "+r"(r0) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_clear_event(
        Oot3dCtrHandle event
    );
    return oot3d_host_ctr_clear_event(event);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_ReleaseMutex(
    Oot3dCtrHandle mutex
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = mutex;
    __asm__ volatile("svc 0x14" : "+r"(r0) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_release_mutex(
        Oot3dCtrHandle mutex
    );
    return oot3d_host_ctr_release_mutex(mutex);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_CreateEvent(
    Oot3dCtrHandle* outHandle, u32 resetType
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = resetType;
    register Oot3dCtrHandle r1 __asm__("r1");
    __asm__ volatile("svc 0x17" : "+r"(r0), "=r"(r1) : : "memory", "cc");
    *outHandle = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_create_event(
        Oot3dCtrHandle* outHandle, u32 resetType
    );
    return oot3d_host_ctr_create_event(outHandle, resetType);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_CreateAddressArbiter(
    Oot3dCtrHandle* outHandle
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0");
    register Oot3dCtrHandle r1 __asm__("r1");
    __asm__ volatile("svc 0x21" : "=r"(r0), "=r"(r1) : : "memory", "cc");
    *outHandle = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_create_address_arbiter(
        Oot3dCtrHandle* outHandle
    );
    return oot3d_host_ctr_create_address_arbiter(outHandle);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_DuplicateHandle(
    Oot3dCtrHandle* outHandle, Oot3dCtrHandle sourceHandle
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = sourceHandle;
    register Oot3dCtrHandle r1 __asm__("r1");
    __asm__ volatile("svc 0x27" : "+r"(r0), "=r"(r1) : : "memory", "cc");
    *outHandle = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_duplicate_handle(
        Oot3dCtrHandle* outHandle, Oot3dCtrHandle sourceHandle
    );
    return oot3d_host_ctr_duplicate_handle(outHandle, sourceHandle);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_GetResourceLimit(
    Oot3dCtrHandle* outHandle, Oot3dCtrHandle process
) {
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = process;
    register Oot3dCtrHandle r1 __asm__("r1");
    __asm__ volatile("svc 0x38" : "+r"(r0), "=r"(r1) : : "memory", "cc");
    *outHandle = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_get_resource_limit(
        Oot3dCtrHandle* outHandle, Oot3dCtrHandle process
    );
    return oot3d_host_ctr_get_resource_limit(outHandle, process);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_ArbitrateAddress(
    Oot3dCtrHandle arbiter,
    volatile void* address,
    Oot3dCtrArbitrationType type,
    s32 value
) {
    const Oot3dTargetAddress32 targetAddress =
        (Oot3dTargetAddress32)(uint32_t)address;
#if defined(__arm__) && defined(__GNUC__)
    register Oot3dCtrHandle r0 __asm__("r0") = arbiter;
    register Oot3dTargetAddress32 r1 __asm__("r1") = targetAddress;
    register u32 r2 __asm__("r2") = (u32)type;
    register s32 r3 __asm__("r3") = value;
    __asm__ volatile(
        "svc 0x22"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        :
        : "memory", "cc"
    );
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_arbitrate_address(
        Oot3dCtrHandle arbiter,
        Oot3dTargetAddress32 address,
        u32 type,
        s32 value
    );
    return oot3d_host_ctr_arbitrate_address(
        arbiter,targetAddress,(u32)type,value);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_ControlMemory(
    Oot3dTargetAddress32* outAddress,
    Oot3dTargetAddress32 address0,
    Oot3dTargetAddress32 address1,
    u32 size,
    u32 operation,
    u32 permissions
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = operation;
    register u32 r1 __asm__("r1") = address0;
    register u32 r2 __asm__("r2") = address1;
    register u32 r3 __asm__("r3") = size;
    register u32 r4 __asm__("r4") = permissions;
    __asm__ volatile("svc 0x01" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4) : : "memory", "cc");
    *outAddress = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_control_memory(
        Oot3dTargetAddress32*, Oot3dTargetAddress32,
        Oot3dTargetAddress32, u32, u32, u32);
    return oot3d_host_ctr_control_memory(
        outAddress,address0,address1,size,operation,permissions);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_MapMemoryBlock(
    Oot3dCtrHandle memoryBlock,
    Oot3dTargetAddress32 address,
    u32 permissions,
    u32 otherPermissions
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = memoryBlock;
    register u32 r1 __asm__("r1") = address;
    register u32 r2 __asm__("r2") = permissions;
    register u32 r3 __asm__("r3") = otherPermissions;
    __asm__ volatile("svc 0x1f" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_map_memory_block(
        Oot3dCtrHandle, Oot3dTargetAddress32, u32, u32);
    return oot3d_host_ctr_map_memory_block(
        memoryBlock,address,permissions,otherPermissions);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_UnmapMemoryBlock(
    Oot3dCtrHandle memoryBlock,
    Oot3dTargetAddress32 address
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = memoryBlock;
    register u32 r1 __asm__("r1") = address;
    __asm__ volatile("svc 0x20" : "+r"(r0), "+r"(r1) : : "memory", "cc");
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_unmap_memory_block(
        Oot3dCtrHandle, Oot3dTargetAddress32);
    return oot3d_host_ctr_unmap_memory_block(memoryBlock,address);
#endif
}

static inline Oot3dCtrResult Oot3dCtr_CreateThread(
    Oot3dCtrHandle* outHandle,
    Oot3dTargetAddress32 entry,
    Oot3dTargetAddress32 argument,
    Oot3dTargetAddress32 stackTop,
    s32 priority,
    s32 processorId
) {
#if defined(__arm__) && defined(__GNUC__)
    register u32 r0 __asm__("r0") = (u32)priority;
    register u32 r1 __asm__("r1") = entry;
    register u32 r2 __asm__("r2") = argument;
    register u32 r3 __asm__("r3") = stackTop;
    register u32 r4 __asm__("r4") = (u32)processorId;
    __asm__ volatile("svc 0x08" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4) : : "memory", "cc");
    *outHandle = r1;
    return (Oot3dCtrResult)r0;
#else
    extern Oot3dCtrResult oot3d_host_ctr_create_thread(
        Oot3dCtrHandle*, Oot3dTargetAddress32, Oot3dTargetAddress32,
        Oot3dTargetAddress32, s32, s32);
    return oot3d_host_ctr_create_thread(
        outHandle,entry,argument,stackTop,priority,processorId);
#endif
}

static inline void Oot3dCtr_ExitProcess(void) {
#if defined(__arm__) && defined(__GNUC__)
    __asm__ volatile("svc 0x03" : : : "memory", "cc");
#else
    extern void oot3d_host_ctr_exit_process(void);
    oot3d_host_ctr_exit_process();
#endif
}

static inline void Oot3dCtr_ExitThread(void) {
#if defined(__arm__) && defined(__GNUC__)
    __asm__ volatile("svc 0x09" : : : "memory", "cc");
#else
    extern void oot3d_host_ctr_exit_thread(void);
    oot3d_host_ctr_exit_thread();
#endif
}

#endif
