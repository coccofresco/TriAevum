#include "triaevum_title_aot_abi.h"
#include "triaevum_title_whole_aot_abi.h"

#include "oot3d_whole_aot_generated.h"

#if defined(_WIN32)
#define TRIAEVUM_TITLE_AOT_EXPORT __declspec(dllexport)
#else
#define TRIAEVUM_TITLE_AOT_EXPORT __attribute__((visibility("default")))
#endif

namespace Oot3dNativeGame {

namespace {

bool PluginExecutionActive() noexcept {
  return Oot3dWholeAotExecutionActive();
}

void PluginObservableExit(
    uint32_t pc, const oot3d::recomp::a32::GuestState *state) {
  if (state == nullptr) {
    Oot3dWholeAotExitAt(pc);
  }
  Oot3dWholeAotExitAt(pc, *state);
}

} // namespace

extern "C" TRIAEVUM_TITLE_AOT_EXPORT const Oot3dDirectAotProgramV1 *
triaevum_title_aot_query(uint32_t) noexcept {
  return nullptr;
}

extern "C" TRIAEVUM_TITLE_AOT_EXPORT const Oot3dWholeAotProgramV2 *
triaevum_title_whole_aot_query(uint32_t requestedAbi) noexcept {
  if (requestedAbi != kOot3dWholeAotPluginAbiV2) {
    return nullptr;
  }
  static const Oot3dWholeAotProgramV2 program = [] {
    const auto entries = Oot3dWholeAotEntryPoints();
    return Oot3dWholeAotProgramV2{
        kOot3dWholeAotPluginAbiV2, sizeof(Oot3dWholeAotProgramV2),
        entries.data(), entries.size(), ExecuteOot3dWholeAotFunction,
        PluginExecutionActive, PluginObservableExit};
  }();
  return &program;
}

} // namespace Oot3dNativeGame
