#pragma once

#include <stddef.h>
#include <stdint.h>

#if !defined(OOT3D_SOURCE_OVERLAY_BUILD_PLUGIN)
#define OOT3D_SOURCE_OVERLAY_EXPORT
#elif defined(_WIN32)
#define OOT3D_SOURCE_OVERLAY_EXPORT __declspec(dllexport)
#else
#define OOT3D_SOURCE_OVERLAY_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OOT3D_SOURCE_OVERLAY_ABI_VERSION 1U
#define OOT3D_SOURCE_OVERLAY_QUERY_SYMBOL "Oot3dSourceOverlayQuery"

typedef enum Oot3dSourceOverlayFlowKind {
    OOT3D_SOURCE_OVERLAY_NOT_HANDLED = 0,
    OOT3D_SOURCE_OVERLAY_BRANCH = 1,
    OOT3D_SOURCE_OVERLAY_SVC = 2,
    OOT3D_SOURCE_OVERLAY_BLOCK_LIMIT = 3,
    OOT3D_SOURCE_OVERLAY_MEMORY_FAULT = 4,
} Oot3dSourceOverlayFlowKind;

typedef struct Oot3dSourceOverlayGuestState {
    uint32_t Registers[16];
    uint32_t Cpsr;
    uint32_t Fpscr;
    uint32_t ThreadPointer;
    uint32_t Vfp[32];
    uint32_t ExclusiveAddress;
    uint64_t ExclusiveToken;
    uint8_t ExclusiveSize;
    uint8_t ExclusiveValid;
    uint8_t Reserved[6];
} Oot3dSourceOverlayGuestState;

typedef struct Oot3dSourceOverlayExecutionResult {
    uint32_t StructSize;
    uint32_t Kind;
    uint32_t Pc;
    uint32_t Detail;
    uint32_t BlocksConsumed;
} Oot3dSourceOverlayExecutionResult;

enum {
    OOT3D_SOURCE_OVERLAY_MEMORY_READ = 1U << 0U,
    OOT3D_SOURCE_OVERLAY_MEMORY_WRITE = 1U << 1U,
};

typedef struct Oot3dSourceOverlayHostApi {
    uint32_t StructSize;
    uint32_t AbiVersion;
    void* Context;
    uint32_t (*ProbeMemory)(void* context, uint32_t address, size_t size);
    int (*ReadMemory)(void* context, uint32_t address, void* bytes, size_t size);
    int (*WriteMemory)(void* context, uint32_t address, const void* bytes,
                       size_t size);
    const void* (*ResolveRead)(void* context, uint32_t address, size_t size);
    void* (*ResolveWrite)(void* context, uint32_t address, size_t size);
    int (*CallGuest)(void* context, uint32_t entry,
                     Oot3dSourceOverlayGuestState* state,
                     Oot3dSourceOverlayExecutionResult* result,
                     uint32_t blockBudget);
    void (*Log)(void* context, uint32_t level, const char* message);
} Oot3dSourceOverlayHostApi;

typedef struct Oot3dSourceOverlayEntry {
    uint32_t Address;
    uint32_t Flags;
    const char* Owner;
    const char* Name;
} Oot3dSourceOverlayEntry;

typedef struct Oot3dSourceOverlayPluginApi {
    uint32_t StructSize;
    uint32_t AbiVersion;
    const char* BuildId;
    void* Context;
    const Oot3dSourceOverlayEntry* Entries;
    size_t EntryCount;
    int (*Execute)(void* context, uint32_t entry,
                   Oot3dSourceOverlayGuestState* state,
                   Oot3dSourceOverlayExecutionResult* result,
                   uint32_t blockBudget);
    void (*Shutdown)(void* context);
} Oot3dSourceOverlayPluginApi;

typedef const Oot3dSourceOverlayPluginApi* (*Oot3dSourceOverlayQueryFn)(
    uint32_t hostAbiVersion, const Oot3dSourceOverlayHostApi* hostApi);

OOT3D_SOURCE_OVERLAY_EXPORT const Oot3dSourceOverlayPluginApi*
Oot3dSourceOverlayQuery(uint32_t hostAbiVersion,
                        const Oot3dSourceOverlayHostApi* hostApi);

#ifdef __cplusplus
}
#endif
