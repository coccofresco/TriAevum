#ifndef TRIAEVUM_INPUT_SERVICE_ADAPTER_H
#define TRIAEVUM_INPUT_SERVICE_ADAPTER_H

#include "triaevum/input_service.h"

#include <cstddef>
#include <cstdint>

namespace triaevum::module {

class InputServiceBackendV1 {
public:
  virtual ~InputServiceBackendV1() = default;

  virtual TriAevumModuleStatusV1 ReadState(std::uint32_t playerIndex,
                                           InputStateV1 *state) = 0;
};

class InputHostServiceAdapterV1 final {
public:
  explicit InputHostServiceAdapterV1(InputServiceBackendV1 &backend);

  static TriAevumModuleStatusV1 TRIAEVUM_ABI_CALL Invoke(
      void *context, std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
      TriAevumMutableBytesV1 response, std::size_t *responseSize);

private:
  TriAevumModuleStatusV1 Dispatch(std::uint32_t operation,
                                  TriAevumReadOnlyBytesV1 request,
                                  TriAevumMutableBytesV1 response,
                                  std::size_t *responseSize);

  InputServiceBackendV1 &mBackend;
};

} // namespace triaevum::module

#endif
