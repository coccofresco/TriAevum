#pragma once

#include "oot3d_ui/ui_frontend_resources.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// Resource ownership follows the original OoT3D loaders. It is deliberately
// separate from presentation so a future backend can replace content without
// changing the native loading contract by accident.
enum class UiLocalizedMenuResourceOwner : std::uint8_t {
    PauseShared = 0,
    PauseMap,
    HintMovie,
    FileSelect,
    NameEntry,
    NoReviewedLocalizedLoader,
    Count,
};

// Exact native order used by PauseUi_LoadSharedTextureAssets. The ordinal is
// part of the original 16-pointer resource array contract.
enum class UiPauseSharedTextureSlot : std::uint8_t {
    CommonBackground00 = 0,
    HudAtlas,
    PauseTopPage,
    ItemPage,
    EquipmentPage,
    OcarinaPage,
    MapPage00,
    MapPage01,
    FieldMapClouds,
    NumberGlyphs,
    ItemIcons,
    HudMenuTitle,
    OptionsPage,
    Cursor,
    ItemPageAux,
    CameraInterface,
    Count,
};

// The first sixteen values intentionally match UiPauseSharedTextureSlot.
enum class UiLocalizedMenuTextureKind : std::uint8_t {
    CommonBackground00 = 0,
    HudAtlas,
    PauseTopPage,
    ItemPage,
    EquipmentPage,
    OcarinaPage,
    MapPage00,
    MapPage01,
    FieldMapClouds,
    NumberGlyphs,
    ItemIcons,
    HudMenuTitle,
    OptionsPage,
    Cursor,
    ItemPageAux,
    CameraInterface,
    PauseTopMap,
    HintMovie,
    FileSelectParts00,
    FileSelectParts01,
    NameEntryParts00,
    NameEntryParts01,
    CommonBackground01,
    Count,
};

inline constexpr std::size_t kOot3dPauseSharedTextureCount = 16;
inline constexpr std::size_t kOot3dLocalizedMenuTextureKindCount = 23;
inline constexpr std::size_t kOot3dLocalizedMenuTextureSlotCount = 230;
inline constexpr std::size_t kOot3dLocalizedMenuNativeDeclaredSlotCount = 220;
inline constexpr std::size_t kOot3dLocalizedMenuUsaRev0PresentCount = 207;
inline constexpr std::size_t kOot3dLocalizedMenuUsaRev0NativePresentCount = 198;
inline constexpr std::size_t kOot3dLocalizedMenuUsaRev0Bytes = 32'602'680;
inline constexpr std::size_t kOot3dLocalizedMenuUsaRev0UniquePayloadCount = 104;
inline constexpr std::size_t kOot3dCtxbHeaderBytes = 0x48;

struct UiLocalizedMenuTextureDescriptor {
    UiLocalizedMenuTextureKind kind = UiLocalizedMenuTextureKind::Count;
    UiLocalizedMenuResourceOwner owner =
        UiLocalizedMenuResourceOwner::NoReviewedLocalizedLoader;
    UiPauseSharedTextureSlot native_pause_shared_slot =
        UiPauseSharedTextureSlot::Count;
    const char* semantic_name = nullptr;
    const char* semantic_role = nullptr;
    const char* filename = nullptr;
    const char* native_loader = nullptr;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t mip_levels = 0;
    std::uint8_t bits_per_pixel = 0;
    std::uint8_t unique_payloads_in_usa_rev0 = 0;
    std::uint8_t unique_header_variants_in_usa_rev0 = 0;
    bool tiled = false;
    bool declared_by_native_code = false;

    constexpr bool IsPauseShared() const noexcept {
        return native_pause_shared_slot != UiPauseSharedTextureSlot::Count;
    }
};

struct UiLocalizedMenuTextureResourceView {
    Oot3dUiLanguage language = Oot3dUiLanguage::Count;
    UiLocalizedMenuTextureKind kind = UiLocalizedMenuTextureKind::Count;
    const UiLocalizedMenuTextureDescriptor* descriptor = nullptr;
    const char* rom_path = nullptr;
    std::size_t byte_count = 0;
    std::size_t payload_byte_count = 0;
    std::uint16_t raw_format = 0;
    std::uint16_t raw_type = 0;
    bool present_in_usa_rev0 = false;

    constexpr bool Declared() const noexcept { return rom_path != nullptr; }
    constexpr bool NativeDeclared() const noexcept {
        return descriptor != nullptr && descriptor->declared_by_native_code;
    }
    constexpr bool Present() const noexcept {
        return Declared() && present_in_usa_rev0;
    }
};

const std::array<UiLocalizedMenuTextureDescriptor,
                 kOot3dLocalizedMenuTextureKindCount>&
Oot3dLocalizedMenuTextureDescriptors() noexcept;
const UiLocalizedMenuTextureDescriptor* Oot3dLocalizedMenuTextureDescriptorFor(
    UiLocalizedMenuTextureKind kind) noexcept;

UiLocalizedMenuTextureResourceView Oot3dLocalizedMenuTextureResource(
    Oot3dUiLanguage language, UiLocalizedMenuTextureKind kind) noexcept;

UiLocalizedMenuTextureKind Oot3dPauseSharedTextureKind(
    UiPauseSharedTextureSlot slot) noexcept;
UiLocalizedMenuTextureResourceView Oot3dPauseSharedTextureResource(
    Oot3dUiLanguage language, UiPauseSharedTextureSlot slot) noexcept;

UiLocalizedMenuTextureKind Oot3dLocalizedMenuTextureKindForFrontendAtlas(
    UiFrontendAtlasKind kind) noexcept;
UiFrontendAtlasKind Oot3dFrontendAtlasKindForLocalizedMenuTexture(
    UiLocalizedMenuTextureKind kind) noexcept;
UiLocalizedMenuTextureResourceView
Oot3dLocalizedMenuTextureResourceForFrontendAtlas(
    Oot3dUiLanguage language, UiFrontendAtlasKind kind) noexcept;

static_assert(static_cast<std::size_t>(UiPauseSharedTextureSlot::Count) ==
              kOot3dPauseSharedTextureCount);
static_assert(static_cast<std::size_t>(UiLocalizedMenuTextureKind::Count) ==
              kOot3dLocalizedMenuTextureKindCount);
static_assert(kOot3dLocalizedMenuTextureSlotCount ==
              kOot3dUiLanguageCount * kOot3dLocalizedMenuTextureKindCount);

} // namespace oot3d::ui
