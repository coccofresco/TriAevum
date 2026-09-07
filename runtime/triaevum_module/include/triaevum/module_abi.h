#ifndef TRIAEVUM_MODULE_ABI_H
#define TRIAEVUM_MODULE_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define TRIAEVUM_ABI_CALL __cdecl
#define TRIAEVUM_MODULE_EXPORT __declspec(dllexport)
#else
#define TRIAEVUM_ABI_CALL
#define TRIAEVUM_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  TRIAEVUM_RUNTIME_ABI_V1 = 1,
  TRIAEVUM_MODULE_QUERY_ABI_V1 = 1,
};

typedef uint32_t TriAevumLogLevelV1;
enum {
  TRIAEVUM_LOG_DEBUG_V1 = 0,
  TRIAEVUM_LOG_INFO_V1 = 1,
  TRIAEVUM_LOG_WARNING_V1 = 2,
  TRIAEVUM_LOG_ERROR_V1 = 3,
};

typedef uint32_t TriAevumModuleStatusV1;
enum {
  TRIAEVUM_MODULE_OK_V1 = 0,
  TRIAEVUM_MODULE_INVALID_ARGUMENT_V1 = 1,
  TRIAEVUM_MODULE_INCOMPATIBLE_ABI_V1 = 2,
  TRIAEVUM_MODULE_HOST_ERROR_V1 = 3,
  TRIAEVUM_MODULE_TITLE_ERROR_V1 = 4,
  TRIAEVUM_MODULE_SERVICE_NOT_FOUND_V1 = 5,
  TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1 = 6,
  TRIAEVUM_MODULE_MALFORMED_REQUEST_V1 = 7,
  TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1 = 8,
  TRIAEVUM_MODULE_SERVICE_BUSY_V1 = 9,
};

typedef struct TriAevumReadOnlyBytesV1 {
  const uint8_t *data;
  size_t size;
} TriAevumReadOnlyBytesV1;

typedef struct TriAevumMutableBytesV1 {
  uint8_t *data;
  size_t size;
} TriAevumMutableBytesV1;

typedef struct TriAevumFrameInputV1 {
  uint32_t struct_size;
  uint32_t flags;
  uint64_t presentation_time_ns;
  uint64_t simulation_step_ns;
  TriAevumReadOnlyBytesV1 input_snapshot;
} TriAevumFrameInputV1;

typedef uint32_t TriAevumGuestMemoryAccessV1;
enum {
  TRIAEVUM_GUEST_MEMORY_READ_V1 = 1U << 0U,
  TRIAEVUM_GUEST_MEMORY_WRITE_V1 = 1U << 1U,
};

typedef struct TriAevumGuestMemoryMapRequestV1 {
  uint32_t struct_size;
  TriAevumGuestMemoryAccessV1 access;
  uint32_t guest_address;
  uint32_t byte_count;
} TriAevumGuestMemoryMapRequestV1;

typedef struct TriAevumGuestMemoryViewV1 {
  uint32_t struct_size;
  TriAevumGuestMemoryAccessV1 access;
  uint8_t *data;
  size_t size;
  uint64_t content_version;
  uint64_t token;
} TriAevumGuestMemoryViewV1;

typedef uint32_t TriAevumGuestMemoryUnmapFlagsV1;
enum {
  TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1 = 1U << 0U,
};

typedef void(TRIAEVUM_ABI_CALL *TriAevumLogFnV1)(void *host_context,
                                                 TriAevumLogLevelV1 level,
                                                 const char *message,
                                                 size_t message_size);

typedef uint64_t(TRIAEVUM_ABI_CALL *TriAevumMonotonicTimeFnV1)(
    void *host_context);

typedef TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *TriAevumInvokeServiceFnV1)(
    void *host_context, uint32_t service, uint32_t operation,
    TriAevumReadOnlyBytesV1 request, TriAevumMutableBytesV1 response,
    size_t *response_size);

typedef struct TriAevumHostApiV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *host_context;
  TriAevumLogFnV1 log;
  TriAevumMonotonicTimeFnV1 monotonic_time_ns;
  TriAevumInvokeServiceFnV1 invoke_service;
} TriAevumHostApiV1;

typedef TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *TriAevumInitializeFnV1)(
    const TriAevumHostApiV1 *host, const char *private_content_index,
    size_t private_content_index_size);

typedef TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *TriAevumRunFrameFnV1)(
    const TriAevumFrameInputV1 *input);

typedef void(TRIAEVUM_ABI_CALL *TriAevumShutdownFnV1)(void);

typedef size_t(TRIAEVUM_ABI_CALL *TriAevumStateSizeFnV1)(void);

typedef TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *TriAevumSaveStateFnV1)(
    TriAevumMutableBytesV1 destination, size_t *written_size);

typedef TriAevumModuleStatusV1(TRIAEVUM_ABI_CALL *TriAevumLoadStateFnV1)(
    TriAevumReadOnlyBytesV1 source);

typedef TriAevumModuleStatusV1(
    TRIAEVUM_ABI_CALL *TriAevumMapGuestMemoryFnV1)(
    const TriAevumGuestMemoryMapRequestV1 *request,
    TriAevumGuestMemoryViewV1 *view);

typedef TriAevumModuleStatusV1(
    TRIAEVUM_ABI_CALL *TriAevumUnmapGuestMemoryFnV1)(uint64_t token,
                                                     uint32_t flags);

typedef struct TriAevumModuleApiV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint8_t module_identity_sha256[32];
  TriAevumInitializeFnV1 initialize;
  TriAevumRunFrameFnV1 run_frame;
  TriAevumShutdownFnV1 shutdown;
  TriAevumStateSizeFnV1 state_size;
  TriAevumSaveStateFnV1 save_state;
  TriAevumLoadStateFnV1 load_state;
  TriAevumMapGuestMemoryFnV1 map_guest_memory;
  TriAevumUnmapGuestMemoryFnV1 unmap_guest_memory;
} TriAevumModuleApiV1;

typedef const TriAevumModuleApiV1 *(
    TRIAEVUM_ABI_CALL *TriAevumQueryModuleApiV1)(uint32_t query_abi,
                                                 uint32_t runtime_abi);

#define TRIAEVUM_MODULE_QUERY_SYMBOL_V1 "TriAevumQueryModuleV1"

#ifdef __cplusplus
}
#endif

#endif
