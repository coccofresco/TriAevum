#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

bool ExecuteOot3dPicaDisplayTransfer(
    const Oot3dPicaDisplayTransfer& transfer, NativeA32Memory& memory,
    std::string* error = nullptr);

bool ExecuteOot3dPicaMemoryFill(const Oot3dPicaMemoryFill& fill,
                                NativeA32Memory& memory,
                                std::string* error = nullptr);

} // namespace Oot3dNativeGame
