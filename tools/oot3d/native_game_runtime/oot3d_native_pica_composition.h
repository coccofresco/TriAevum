#pragma once

#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
struct GuestState;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

inline constexpr uint32_t kOot3dCmbRendererSubmitDrawHandleEntry =
    0x0030F4D0U;
inline constexpr uint32_t kOot3dObjectKankyoDrawEntry = 0x00297508U;
inline constexpr uint32_t kOot3dActorDrawCallbackReturn = 0x002D6314U;
inline constexpr uint32_t kOot3dSceneRendererSubmitConfiguredModelPassEntry =
    0x002D9664U;
inline constexpr uint32_t kOot3dEnvironmentRendererUpdateGameplayFadeLayersEntry =
    0x0046423CU;
inline constexpr uint32_t kOot3dEnvironmentRendererUpdateRandomizedOverlayEntry =
    0x0045C25CU;
inline constexpr uint32_t kOot3dGameplayDrawRandomizedTextureStageModelsEntry =
    0x0045FEB0U;
inline constexpr uint32_t kOot3dGameplayDrawPrecipitationEffectsEntry =
    0x00463544U;
inline constexpr uint32_t kOot3dGameplayDrawCosineScaledOverlayEntry =
    0x0045945CU;
inline constexpr uint32_t kOot3dGameplayDrawConfiguredProjectedOverlayEntry =
    0x0045FE28U;
inline constexpr uint32_t kOot3dEnvironmentRendererSubmitNormalizedColor4Entry =
    0x002D9490U;
inline constexpr uint32_t kOot3dRendererCommandListExecuteEntry = 0x0035BF50U;
inline constexpr uint32_t kOot3dKankyoEffectPrimitiveDrawEntry = 0x003FB5ECU;
inline constexpr uint32_t kOot3dPicaPrimitivePacketBuilderEntry = 0x00313444U;

std::span<const uint32_t> Oot3dNativePicaCompositionHookPcs() noexcept;

struct Oot3dNativePicaCompositionStats {
    uint64_t CmbOpaquePassEntries = 0;
    uint64_t CmbTransparentPassEntries = 0;
    uint64_t CmbPassExits = 0;
    uint64_t InvalidCmbPassEntries = 0;
    uint64_t InvalidCmbReturnAddresses = 0;
    uint64_t AtmosphereScopeEntries = 0;
    uint64_t AtmosphereScopeExits = 0;
    uint64_t AtmospherePacketSpans = 0;
    uint64_t DirectAtmosphereSpans = 0;
    uint64_t DirectAtmosphereBytes = 0;
    uint64_t NestedAtmosphereScopeEntries = 0;
    uint64_t InvalidAtmosphereCommandRanges = 0;
    uint64_t RecoveredAtmospherePacketSpans = 0;
    uint64_t InvalidAtmosphereReturnAddresses = 0;
    uint64_t IgnoredUnrelatedCommandListExecutions = 0;
    uint64_t AtmosphereSelectorReadFailures = 0;
    uint64_t PrimitivePacketEntries = 0;
    uint64_t PrimitivePacketExits = 0;
    uint64_t PrimitivePacketSpans = 0;
    uint64_t PrimitivePacketBytes = 0;
    uint64_t NestedPrimitivePacketEntries = 0;
    uint64_t InvalidPrimitivePacketReturnAddresses = 0;
    uint64_t InvalidPrimitivePacketRanges = 0;
    uint64_t UnmatchedPrimitivePacketExits = 0;
    uint64_t MeshPacketEntries = 0;
    uint64_t RecordedPacketSpans = 0;
    uint64_t CoalescedPacketSpans = 0;
    uint64_t UnattributedPacketSpans = 0;
    uint64_t InvalidPacketSpans = 0;
    uint64_t OverlappingPacketSpans = 0;
    uint64_t ConsumedPacketSpans = 0;
    uint64_t PartiallyOverlappingSubmissions = 0;
    uint64_t EvictedPacketSpans = 0;
    uint64_t ReadFailures = 0;
};

// Observes native draw-control boundaries and exact mesh packet appends. It
// records byte provenance only; no PICA register, material, texture or scene
// identity participates in classification.
class Oot3dNativePicaCompositionTracker final
    : public Oot3dPicaCommandListCompositionProvider {
  public:
    void ObserveBlockEntry(uint32_t pc,
                           const oot3d::recomp::a32::GuestState& state,
                           oot3d::recomp::a32::MemoryBus& memory);

    bool TakeCommandListCompositionSpans(
        uint32_t commandListAddress, uint32_t commandListSize,
        std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
        std::string* error = nullptr) override;

    void Reset() noexcept;
    [[nodiscard]] const Oot3dNativePicaCompositionStats& Stats() const
        noexcept;

  private:
    struct ActiveAtmosphereScope {
        uint32_t SourcePc = 0;
        uint32_t ReturnPc = 0;
        uint32_t BeginAddress = 0;
        uint32_t NativeValue = 0;
        bool BeginAddressValid = false;
        std::vector<Oot3dPicaCommandListCompositionSpan> PacketSpans;
    };

    struct ActivePrimitivePacket {
        uint32_t ContextAddress = 0;
        uint32_t BeginAddress = 0;
        uint32_t ReturnPc = 0;
        bool BeginAddressValid = false;
        Oot3dPicaCompositionAttribution Attribution;
    };

    bool RecordPendingSpan(Oot3dPicaCommandListCompositionSpan span);

    static constexpr size_t MaximumPendingSpans = 16384U;

    std::optional<uint8_t> mActiveCmbPass;
    std::optional<uint32_t> mActiveCmbReturnPc;
    std::optional<ActiveAtmosphereScope> mActiveAtmosphereScope;
    std::optional<ActivePrimitivePacket> mActivePrimitivePacket;
    std::deque<Oot3dPicaCommandListCompositionSpan> mPendingSpans;
    Oot3dNativePicaCompositionStats mStats;
};

} // namespace Oot3dNativeGame
