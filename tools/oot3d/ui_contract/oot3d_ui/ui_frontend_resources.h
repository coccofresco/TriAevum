#pragma once

#include "oot3d_ui/ui_name_entry_tables.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// Native language indices shared by the OoT3D file-select and name-entry
// resource loaders. Their order is part of the original resource contract.
enum class Oot3dUiLanguage : std::uint8_t {
    Japanese = 0,
    UsEnglish,
    EuEnglish,
    EuGerman,
    EuFrench,
    UsFrench,
    EuSpanish,
    UsSpanish,
    EuItalian,
    EuDutch,
    Count,
};

enum class UiFrontendAtlasKind : std::uint8_t {
    NameEntryParts00 = 0,
    NameEntryParts01,
    FileSelectParts00,
    FileSelectParts01,
    Count,
};

enum class UiNameEntryLocalizedAlphabet : std::uint8_t {
    Lowercase = 0,
    Uppercase,
    Symbols,
    European,
    Count,
};

enum class UiFileSelectAtlasVariant : std::uint8_t {
    Parts00 = 0,
    Parts01,
};

inline constexpr std::size_t kOot3dUiLanguageCount = 10;
inline constexpr std::size_t kOot3dFrontendAtlasKindCount = 4;
inline constexpr std::size_t kOot3dFrontendDeclaredAtlasCount = 40;
inline constexpr std::size_t kOot3dFrontendUsaRev0AtlasCount = 36;
inline constexpr std::size_t kOot3dFrontendUsaRev0AtlasBytes = 14'158'368;
inline constexpr std::size_t kOot3dFrontendUsaRev0UniqueAtlasCount = 24;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetCount = 4;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetFileBytes = 410;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetCodeUnits = 201;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetBomBytes = 2;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetUnwrittenBytes =
    kOot3dNameEntryLocalizedAlphabetStorageBytes -
    kOot3dNameEntryLocalizedAlphabetFileBytes;

struct UiFrontendLanguageResource {
    Oot3dUiLanguage language = Oot3dUiLanguage::Count;
    std::uint8_t native_index = 0;
    const char* semantic_name = nullptr;
    const char* rom_directory = nullptr;
    bool atlases_present_in_usa_rev0 = false;
    bool name_entry_loads_localized_alphabets = false;
};

struct UiFrontendAtlasResourceView {
    Oot3dUiLanguage language = Oot3dUiLanguage::Count;
    UiFrontendAtlasKind kind = UiFrontendAtlasKind::Count;
    const char* rom_path = nullptr;
    std::size_t byte_count = 0;
    bool present_in_usa_rev0 = false;

    constexpr bool Declared() const noexcept { return rom_path != nullptr; }
    constexpr bool Present() const noexcept {
        return Declared() && present_in_usa_rev0;
    }
};

struct UiNameEntryAlphabetResourceView {
    UiNameEntryLocalizedAlphabet alphabet =
        UiNameEntryLocalizedAlphabet::Count;
    const char* rom_path = nullptr;
    const std::uint16_t* code_units = nullptr;
    std::size_t code_unit_count = 0;
    std::size_t file_byte_count = 0;
    std::uint32_t destination_address = 0;
    std::uint32_t consumer_address = 0;
    std::size_t lane_byte_count = 0;
    bool has_utf16le_bom = false;

    constexpr bool Empty() const noexcept {
        return rom_path == nullptr || code_units == nullptr;
    }
    constexpr const std::uint16_t* begin() const noexcept {
        return code_units;
    }
    constexpr const std::uint16_t* end() const noexcept {
        return code_units == nullptr ? nullptr : code_units + code_unit_count;
    }
    constexpr const std::uint16_t& operator[](
        std::size_t index) const noexcept {
        return code_units[index];
    }
};

const std::array<UiFrontendLanguageResource, kOot3dUiLanguageCount>&
Oot3dFrontendLanguageResources() noexcept;
const UiFrontendLanguageResource* Oot3dFrontendLanguageResourceFor(
    Oot3dUiLanguage language) noexcept;

UiFrontendAtlasResourceView Oot3dFrontendAtlasResource(
    Oot3dUiLanguage language, UiFrontendAtlasKind kind) noexcept;
std::array<UiFrontendAtlasResourceView, 2> Oot3dNameEntryAtlasResources(
    Oot3dUiLanguage language) noexcept;

// The native file-select loader chooses parts01 only when its observed raw
// variant byte equals one. No unverified gameplay meaning is assigned to it.
UiFileSelectAtlasVariant Oot3dFileSelectAtlasVariantFromNativeFlag(
    std::uint8_t native_flag) noexcept;
UiFrontendAtlasResourceView Oot3dFileSelectAtlasResource(
    Oot3dUiLanguage language, std::uint8_t native_variant_flag) noexcept;

const std::array<UiNameEntryAlphabetResourceView,
                 kOot3dNameEntryLocalizedAlphabetCount>&
Oot3dNameEntryLocalizedAlphabetResources() noexcept;
UiNameEntryAlphabetResourceView Oot3dNameEntryLocalizedAlphabetResource(
    UiNameEntryLocalizedAlphabet alphabet) noexcept;
bool Oot3dNameEntryLoadsLocalizedAlphabets(
    Oot3dUiLanguage language) noexcept;

static_assert(static_cast<std::size_t>(Oot3dUiLanguage::Count) ==
              kOot3dUiLanguageCount);
static_assert(static_cast<std::size_t>(UiFrontendAtlasKind::Count) ==
              kOot3dFrontendAtlasKindCount);
static_assert(static_cast<std::size_t>(
                  UiNameEntryLocalizedAlphabet::Count) ==
              kOot3dNameEntryLocalizedAlphabetCount);
static_assert(kOot3dFrontendDeclaredAtlasCount ==
              kOot3dUiLanguageCount * kOot3dFrontendAtlasKindCount);
static_assert(kOot3dNameEntryLocalizedAlphabetUnwrittenBytes == 1638);

} // namespace oot3d::ui
