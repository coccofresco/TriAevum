#pragma once

#include <cstddef>
#include <cstdint>

namespace oot3d::recomp::a32 {
struct PackedOp;
}

namespace Oot3dNativeGame {

struct Oot3dWholeAotFlow;
struct Oot3dWholeAotFrame;
struct Oot3dWholeAotContext;

inline constexpr uint32_t kOot3dDirectAotPluginAbiV1 = 1U;

using Oot3dDirectAotBlockExecutorV1 = void (*)(
    Oot3dWholeAotFlow *output, Oot3dWholeAotFrame *frame,
    Oot3dWholeAotContext *context, uint32_t pc,
    const oot3d::recomp::a32::PackedOp *operations, uint32_t operationCount);

using Oot3dDirectAotFunctionV1 =
    void (*)(Oot3dWholeAotFlow *output, Oot3dWholeAotFrame *frame,
             Oot3dWholeAotContext *context, uint32_t entryPc,
             Oot3dDirectAotBlockExecutorV1 executeBlock);

struct Oot3dDirectAotProgramV1 final {
  uint32_t AbiVersion = 0U;
  uint32_t StructSize = 0U;
  const uint32_t *DispatchPcs = nullptr;
  const uint32_t *DispatchFunctionIndices = nullptr;
  size_t DispatchCount = 0U;
  const Oot3dDirectAotFunctionV1 *Functions = nullptr;
  size_t FunctionCount = 0U;
};

static_assert(sizeof(Oot3dDirectAotProgramV1) == 48U);

using Oot3dQueryDirectAotProgramV1 =
    const Oot3dDirectAotProgramV1 *(*)(uint32_t requestedAbi);

extern "C" const Oot3dDirectAotProgramV1 *
triaevum_title_aot_query(uint32_t requestedAbi) noexcept;

} // namespace Oot3dNativeGame
