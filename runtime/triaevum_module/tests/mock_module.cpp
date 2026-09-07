#include "triaevum/module_abi.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

const TriAevumHostApiV1 *gHost = nullptr;
std::uint32_t gState = 0;
std::array<std::uint8_t, 256> gGuestMemory{};

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
Initialize(const TriAevumHostApiV1 *host, const char *privateContentIndex,
           std::size_t privateContentIndexSize) {
  if (host == nullptr || privateContentIndex == nullptr ||
      privateContentIndexSize == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  gHost = host;
  gState = 0;
  std::fill(gGuestMemory.begin(), gGuestMemory.end(), std::uint8_t{0});
  std::size_t responseSize = 0;
  return host->invoke_service(host->host_context, 0x54455354U, 1U,
                              {nullptr, 0U}, {nullptr, 0U}, &responseSize);
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
RunFrame(const TriAevumFrameInputV1 *input) {
  if (gHost == nullptr || input == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  ++gState;
  return TRIAEVUM_MODULE_OK_V1;
}

void TRIAEVUM_ABI_CALL Shutdown() { gHost = nullptr; }

std::size_t TRIAEVUM_ABI_CALL StateSize() { return sizeof(gState); }

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
SaveState(TriAevumMutableBytesV1 destination, std::size_t *writtenSize) {
  if (destination.data == nullptr || destination.size < sizeof(gState) ||
      writtenSize == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::memcpy(destination.data, &gState, sizeof(gState));
  *writtenSize = sizeof(gState);
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
LoadState(TriAevumReadOnlyBytesV1 source) {
  if (source.data == nullptr || source.size != sizeof(gState)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  std::memcpy(&gState, source.data, sizeof(gState));
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
MapGuestMemory(const TriAevumGuestMemoryMapRequestV1 *request,
               TriAevumGuestMemoryViewV1 *view) {
  constexpr std::uint32_t guestBase = 0x1000U;
  constexpr std::uint32_t guestSize =
      static_cast<std::uint32_t>(gGuestMemory.size());
  if (request == nullptr || view == nullptr ||
      request->struct_size < sizeof(TriAevumGuestMemoryMapRequestV1) ||
      request->guest_address < guestBase) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const std::uint32_t offset = request->guest_address - guestBase;
  if (offset > guestSize || request->byte_count > guestSize - offset) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  *view = {
      sizeof(TriAevumGuestMemoryViewV1),
      TRIAEVUM_GUEST_MEMORY_READ_V1 | TRIAEVUM_GUEST_MEMORY_WRITE_V1,
      gGuestMemory.data() + offset,
      request->byte_count,
      gState + 1U,
      static_cast<std::uint64_t>(offset) + 1U,
  };
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL
UnmapGuestMemory(std::uint64_t token, std::uint32_t flags) {
  if (token == 0U ||
      (flags & ~TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) != 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if ((flags & TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) != 0U) {
    ++gState;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

const TriAevumModuleApiV1 kApi = {
    sizeof(TriAevumModuleApiV1),
    TRIAEVUM_RUNTIME_ABI_V1,
    {0x42U},
    Initialize,
    RunFrame,
    Shutdown,
    StateSize,
    SaveState,
    LoadState,
    MapGuestMemory,
    UnmapGuestMemory,
};

} // namespace

extern "C" TRIAEVUM_MODULE_EXPORT const TriAevumModuleApiV1 *TRIAEVUM_ABI_CALL
TriAevumQueryModuleV1(std::uint32_t queryAbi, std::uint32_t runtimeAbi) {
  if (queryAbi != TRIAEVUM_MODULE_QUERY_ABI_V1 ||
      runtimeAbi != TRIAEVUM_RUNTIME_ABI_V1) {
    return nullptr;
  }
  return &kApi;
}
