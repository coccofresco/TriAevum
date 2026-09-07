#ifndef TRIAEVUM_INPUT_SERVICE_CLIENT_H
#define TRIAEVUM_INPUT_SERVICE_CLIENT_H

#include "triaevum/input_service.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace triaevum::module {

class InputServiceClientV1 final {
public:
  explicit InputServiceClientV1(const TriAevumHostApiV1 *host);

  [[nodiscard]] bool IsAvailable() const noexcept;
  TriAevumModuleStatusV1 ReadState(std::uint32_t playerIndex,
                                   InputStateV1 *state) const;

private:
  TriAevumModuleStatusV1 Invoke(std::uint32_t operation,
                                std::span<const std::uint8_t> request,
                                std::span<std::uint8_t> response,
                                std::size_t *responseSize) const;

  TriAevumHostApiV1 mHost{};
  bool mAvailable = false;
};

} // namespace triaevum::module

#endif
