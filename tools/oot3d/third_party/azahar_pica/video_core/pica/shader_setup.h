#pragma once

#include "common/common_types.h"

#include <array>

namespace Pica {

constexpr u32 MAX_PROGRAM_CODE_LENGTH = 4096;
constexpr u32 MAX_SWIZZLE_DATA_LENGTH = 4096;

using ProgramCode = std::array<u32, MAX_PROGRAM_CODE_LENGTH>;
using SwizzleData = std::array<u32, MAX_SWIZZLE_DATA_LENGTH>;

} // namespace Pica
