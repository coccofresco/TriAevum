#pragma once

#include "oot3d_native_pica_submission.h"
#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_top_screen_texture_overrides.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

struct TopScreenTextureOverrideRuntimeStats {
    std::uint64_t observe_calls = 0;
    std::uint64_t identities_observed = 0;
    std::uint64_t target_bindings = 0;
    std::uint64_t payload_checks = 0;
    std::uint64_t applied = 0;
    std::uint64_t already_applied = 0;
    std::uint64_t no_match = 0;
};

struct TopScreenTextureOverrideTargetStats {
    std::uint32_t physical_address = 0;
    std::uint32_t guest_surface_address = 0;
    std::size_t byte_count = 0;
    std::vector<std::string> semantic_names;
    std::uint32_t last_width = 0;
    std::uint32_t last_height = 0;
    std::uint32_t last_format = 0;
    std::uint64_t last_payload_hash = 0;
    std::uint64_t payload_checks = 0;
    std::uint64_t applied = 0;
    std::uint64_t already_applied = 0;
    std::uint64_t no_match = 0;
};

// Connects native pause-shared CTXB identities to transient PICA draw
// snapshots. Guest memory remains untouched so profile switches and savestates
// cannot retain replacement texture bytes.
class TopScreenTextureOverrideRuntime {
  public:
    TopScreenTextureOverrideRuntime(
        const TopScreenTextureOverridePack& pack,
        const Oot3dPicaPhysicalMemoryView& memory) noexcept;

    void Observe(const Oot3dNativeUiLifecycleBridge& bridge);
    void Transform(const Oot3dPicaTextureState& texture,
                   std::span<std::uint8_t> payload);

    const TopScreenTextureOverrideRuntimeStats& Stats() const noexcept;
    std::vector<TopScreenTextureOverrideTargetStats> TargetStats() const;

  private:
    void Bind(std::uint32_t physicalAddress, std::uint32_t guestSurfaceAddress,
              std::size_t byteCount, std::string_view semanticName);

    const TopScreenTextureOverridePack& mPack;
    const Oot3dPicaPhysicalMemoryView& mMemory;
    std::unordered_map<std::uint32_t, TopScreenTextureOverrideTargetStats>
        mTargets;
    TopScreenTextureOverrideRuntimeStats mStats;
};

} // namespace Oot3dNativeGame
