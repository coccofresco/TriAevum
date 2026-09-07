#ifndef TRIAEVUM_SHA256_H
#define TRIAEVUM_SHA256_H

#include <array>
#include <cstdint>
#include <span>

namespace triaevum::module::detail {

[[nodiscard]] std::array<std::uint8_t, 32>
Sha256(std::span<const std::uint8_t> input);

} // namespace triaevum::module::detail

#endif
