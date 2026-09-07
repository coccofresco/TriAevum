#ifndef TRIAEVUM_FILESYSTEM_SERVICE_ADAPTER_H
#define TRIAEVUM_FILESYSTEM_SERVICE_ADAPTER_H

#include "triaevum/filesystem_service.h"

#include <cstddef>
#include <cstdint>

namespace triaevum::module {

class FilesystemHostServiceAdapterV1 final {
public:
  explicit FilesystemHostServiceAdapterV1(FilesystemServiceBackendV1 &backend);

  static TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL Invoke(
      void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
      TriAevumMutableBytesV1 response, std::size_t *responseSize);

private:
  TriAevumModuleStatusV1 Dispatch(std::uint32_t operation,
                                  TriAevumReadOnlyBytesV1 request,
                                  TriAevumMutableBytesV1 response,
                                  std::size_t *responseSize);

  FilesystemServiceBackendV1 &mBackend;
};

} // namespace triaevum::module

#endif
