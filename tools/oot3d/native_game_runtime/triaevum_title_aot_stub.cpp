#include "triaevum_title_aot_abi.h"
#include "triaevum_title_whole_aot_abi.h"

#if defined(_WIN32)
#define TRIAEVUM_TITLE_AOT_EXPORT __declspec(dllexport)
#else
#define TRIAEVUM_TITLE_AOT_EXPORT __attribute__((visibility("default")))
#endif

namespace Oot3dNativeGame {

extern "C" TRIAEVUM_TITLE_AOT_EXPORT const Oot3dDirectAotProgramV1 *
triaevum_title_aot_query(uint32_t) noexcept {
  return nullptr;
}

extern "C" TRIAEVUM_TITLE_AOT_EXPORT const Oot3dWholeAotProgramV2 *
triaevum_title_whole_aot_query(uint32_t) noexcept {
  return nullptr;
}

} // namespace Oot3dNativeGame
