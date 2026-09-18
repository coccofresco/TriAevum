#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct Oot3dPicaTextureCopySpan {
    uint32_t InputAddress = 0;
    uint32_t OutputAddress = 0;
    uint32_t Size = 0;
};

struct Oot3dPicaTextureCopyPlan {
    uint32_t Size = 0;
    std::vector<Oot3dPicaTextureCopySpan> Spans;
};

// Raw GSP TextureCopy: widths and gaps are measured in 16-byte blocks,
// independently of the format or dimensions of any GPU surface involved.
bool BuildOot3dPicaTextureCopyPlan(
    const Oot3dGspCommandPacket& command, Oot3dPicaTextureCopyPlan& plan,
    std::string* error = nullptr);

// The caller must first flush GPU-owned source/destination ranges and then
// invalidate destination aliases. This function alone does not complete GSP.
bool ExecuteOot3dPicaTextureCopy(
    const Oot3dPicaTextureCopyPlan& plan, NativeA32Memory& memory,
    std::string* error = nullptr);

bool ExecuteOot3dPicaDisplayTransfer(
    const Oot3dPicaDisplayTransfer& transfer, NativeA32Memory& memory,
    std::string* error = nullptr);

bool ExecuteOot3dPicaMemoryFill(const Oot3dPicaMemoryFill& fill,
                                NativeA32Memory& memory,
                                std::string* error = nullptr);

} // namespace Oot3dNativeGame
