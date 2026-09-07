#include "oot3d_top_screen_texture_runtime.h"

#include "oot3d_native_ui_texture_provider.h"

#include <algorithm>

namespace Oot3dNativeGame {
namespace {

std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t value = 0xCBF29CE484222325ULL;
    for (const auto byte : bytes) {
        value ^= byte;
        value *= 0x100000001B3ULL;
    }
    return value;
}

} // namespace

TopScreenTextureOverrideRuntime::TopScreenTextureOverrideRuntime(
    const TopScreenTextureOverridePack& pack,
    const Oot3dPicaPhysicalMemoryView& memory) noexcept
    : mPack(pack), mMemory(memory) {}

void TopScreenTextureOverrideRuntime::Observe(
    const Oot3dNativeUiLifecycleBridge& bridge) {
    ++mStats.observe_calls;
    for (std::size_t index = 0;
         index < oot3d::ui::kOot3dPauseSharedTextureCount; ++index) {
        const auto slot =
            static_cast<oot3d::ui::UiPauseSharedTextureSlot>(index);
        const auto identity = bridge.NativePauseSharedTextureIdentity(slot);
        if (!identity.has_value()) {
            continue;
        }
        const auto descriptor = ResolveOot3dNativeUiTextureDescriptor(
            identity->semantic_name);
        if (!descriptor.has_value()) {
            continue;
        }
        ++mStats.identities_observed;
        for (const auto& encoding : descriptor->encodings) {
            const auto byteCount = encoding.encoded_byte_count;
            const auto surface = identity->guest_surface_address;
            if (const auto physical =
                    mMemory.TranslateGuest(surface, byteCount)) {
                Bind(*physical, surface, byteCount, identity->semantic_name);
            }
            if (mMemory.Translate(surface, byteCount).has_value()) {
                Bind(surface, surface, byteCount, identity->semantic_name);
            }
        }
    }
}

void TopScreenTextureOverrideRuntime::Transform(
    const Oot3dPicaTextureState& texture,
    std::span<std::uint8_t> payload) {
    const auto target = mTargets.find(texture.PhysicalAddress);
    if (target == mTargets.end() || target->second.byte_count != payload.size()) {
        return;
    }
    auto& targetStats = target->second;
    targetStats.last_width = texture.Width;
    targetStats.last_height = texture.Height;
    targetStats.last_format = texture.Format;
    targetStats.last_payload_hash = Fnv1a64(payload);
    ++targetStats.payload_checks;
    ++mStats.payload_checks;
    switch (mPack.Apply(payload)) {
    case TopScreenTextureOverrideResult::Applied:
        ++targetStats.applied;
        ++mStats.applied;
        break;
    case TopScreenTextureOverrideResult::AlreadyApplied:
        ++targetStats.already_applied;
        ++mStats.already_applied;
        break;
    case TopScreenTextureOverrideResult::NoMatch:
        ++targetStats.no_match;
        ++mStats.no_match;
        break;
    }
}

const TopScreenTextureOverrideRuntimeStats&
TopScreenTextureOverrideRuntime::Stats() const noexcept {
    return mStats;
}

std::vector<TopScreenTextureOverrideTargetStats>
TopScreenTextureOverrideRuntime::TargetStats() const {
    std::vector<TopScreenTextureOverrideTargetStats> result;
    result.reserve(mTargets.size());
    for (const auto& [physicalAddress, target] : mTargets) {
        static_cast<void>(physicalAddress);
        result.push_back(target);
    }
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) {
                  return left.physical_address < right.physical_address;
              });
    return result;
}

void TopScreenTextureOverrideRuntime::Bind(std::uint32_t physicalAddress,
                                           std::uint32_t guestSurfaceAddress,
                                           std::size_t byteCount,
                                           std::string_view semanticName) {
    auto [position, inserted] = mTargets.try_emplace(physicalAddress);
    auto& target = position->second;
    if (inserted) {
        target.physical_address = physicalAddress;
        target.guest_surface_address = guestSurfaceAddress;
        target.byte_count = byteCount;
        ++mStats.target_bindings;
    } else {
        target.guest_surface_address = guestSurfaceAddress;
        target.byte_count = byteCount;
    }
    if (std::find(target.semantic_names.begin(), target.semantic_names.end(),
                  semanticName) == target.semantic_names.end()) {
        target.semantic_names.emplace_back(semanticName);
    }
}

} // namespace Oot3dNativeGame
