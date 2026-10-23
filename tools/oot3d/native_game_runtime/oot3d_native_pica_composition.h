#pragma once

#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_a32_lighting.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
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
inline constexpr uint32_t kOot3dActorDrawEntry = 0x002D5F68U;
inline constexpr uint32_t kOot3dActorDrawCallbackReturn = 0x002D6314U;
// ACTOR_EN_MAG: the title logo composited over the opening. The full
// per-pixel model lights it by its own native lights, apart from the scene.
inline constexpr uint16_t kOot3dTitleLogoActorId = 0x0171U;
// Actor draw identities are (actor id << 32) | actor address.
[[nodiscard]] constexpr bool IsTitleLogoActor(uint64_t actorIdentity) noexcept {
    return actorIdentity != 0U && (actorIdentity >> 32U) == kOot3dTitleLogoActorId;
}
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
inline constexpr uint32_t kOot3dGameplayDrawEntry = 0x002E25F0U;
inline constexpr uint32_t kOot3dModelQueueSubmitEntry = 0x00372170U;
inline constexpr uint32_t kOot3dModelQueueSetSlotEntry = 0x0031487CU;

std::span<const uint32_t> Oot3dNativePicaCompositionHookPcs() noexcept;

struct Oot3dNativePicaCompositionStats {
    uint64_t UiScopeEntries = 0;
    uint64_t UiScopeExits = 0;
    uint64_t UiCommandSpans = 0;
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
    void SetRoomShadowShellsEnabled(bool enabled) { mLighting.SetRoomShadowShellsEnabled(enabled); }
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
        Fast::Renderer3ds::PicaNativeLightBinding LightBinding;
    };

    bool RecordPendingSpan(Oot3dPicaCommandListCompositionSpan span);

    static constexpr size_t MaximumPendingSpans = 16384U;
    struct QueuedComposition {
        uint32_t SlotAddress = 0;
        Oot3dPicaCompositionAttribution Attribution;
    };
    std::vector<QueuedComposition> mQueuedComposition;
    Oot3dPicaCompositionAttribution mActiveCmbAttribution;
    std::vector<uint64_t> mActorDrawStack;


    std::optional<uint8_t> mActiveCmbPass;
    uint32_t mActiveCmbRenderer = 0;
    // Native actor lifetimes are independent: recycling one actor must not
    // invalidate a surviving instance. Non-actor CMBs use the tracker lifetime
    // together with the renderer's continuity epoch.
    uint64_t mNextTemporalLifetime = 1;
    uint64_t mWorldTemporalLifetime = 1;
    std::unordered_map<uint32_t, uint64_t> mActorLifetimes;
    [[nodiscard]] uint64_t ActorTemporalLifetime(uint64_t owner) const {
        const auto found = mActorLifetimes.find(static_cast<uint32_t>(owner));
        return found == mActorLifetimes.end() ? 0 : found->second;
    }
    std::vector<uint32_t> mProjectedShadowRenderers;
    NativeA32LightingProbe mLighting;
    std::optional<uint32_t> mActiveCmbReturnPc;
    std::optional<ActiveAtmosphereScope> mActiveAtmosphereScope;
    std::optional<ActiveAtmosphereScope> mActiveUiScope;
    std::optional<ActivePrimitivePacket> mActivePrimitivePacket;
    std::deque<Oot3dPicaCommandListCompositionSpan> mPendingSpans;
    Oot3dNativePicaCompositionStats mStats;
};

} // namespace Oot3dNativeGame
