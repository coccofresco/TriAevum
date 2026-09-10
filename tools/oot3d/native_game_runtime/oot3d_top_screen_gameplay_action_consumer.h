#pragma once

#include "oot3d_top_screen_controls.h"
#include "oot3d_top_screen_gameplay_actions.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Memory;
class NativeA32Process;

struct TopScreenGameplayActionConsumerStats {
  std::uint64_t Attempts = 0U;
  std::uint64_t Eligible = 0U;
  std::uint64_t EquipmentChanges = 0U;
  std::uint64_t PlayerRefreshes = 0U;
  std::uint64_t DirectItemAssignments = 0U;
  std::uint64_t DirectItemTriggers = 0U;
  std::uint64_t DirectItemClears = 0U;
};

struct TopScreenGameplayActionConsumerRuntime {
  TopScreenDirectItemRuntime DirectItem;
};

// Decodes the native save-context age field for host-side TopScreen routing.
// Guest addresses remain confined to this application adapter.
bool ReadTopScreenChildLink(const NativeA32Memory &memory, bool *childLink,
                            std::string *error = nullptr);

bool PlayTopScreenUiSound(NativeA32Process &process, std::uint32_t sound,
                         std::uint32_t returnAddress, std::string *error);

// Application-side sink for TopScreen gameplay intents. It is the only new
// 2.1.1 layer that may invoke original guest actions; the UI planner and the
// renderer remain independent of guest addresses and A32 execution.
bool ConsumeTopScreenGameplayActions(
    NativeA32Process &process, const TopScreenDpadActionState &actions,
    bool ordinaryItemActivated, std::uint32_t directCallReturnAddress,
    TopScreenGameplayActionConsumerRuntime *runtime,
    TopScreenGameplayActionConsumerStats *stats = nullptr,
    std::string *error = nullptr);

} // namespace Oot3dNativeGame
