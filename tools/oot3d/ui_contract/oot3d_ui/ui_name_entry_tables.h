#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class UiNameEntryKanaScript : std::uint8_t {
    Hiragana = 0,
    Katakana,
    Count,
};

enum class UiNameEntryKanaVariant : std::uint8_t {
    Base = 0,
    Dakuten,
    Handakuten,
    Small,
    Count,
};

enum class UiNameEntryBuiltinLatinPage : std::uint8_t {
    Lowercase = 0,
    Uppercase,
    Symbols,
    Count,
};

enum class UiNameEntryKanaTransform : std::uint8_t {
    Dakuten = 0,
    Handakuten,
    Small,
    Count,
};

enum class UiNameEntryKanaMechanicTable : std::uint8_t {
    TransformTarget = 0,
    Lookup,
    Classification,
    Cycle,
    Count,
};

inline constexpr std::size_t kOot3dNameEntryKanaScriptCount = 2;
inline constexpr std::size_t kOot3dNameEntryKanaVariantCount = 4;
inline constexpr std::size_t kOot3dNameEntryKanaTransformCount = 3;
inline constexpr std::size_t kOot3dNameEntryKanaMechanicTableCount = 4;
inline constexpr std::size_t kOot3dNameEntryKanaKeyCount = 51;
inline constexpr std::size_t kOot3dNameEntryKanaPageCodeUnitCount = 52;
inline constexpr std::size_t kOot3dNameEntryBuiltinLatinPageCount = 3;
inline constexpr std::size_t kOot3dNameEntryBuiltinLatinPageCodeUnitCount = 47;
inline constexpr std::size_t kOot3dNameEntryStaticSymbolCount = 10;
inline constexpr std::size_t kOot3dNameEntryStaticSubtableCount = 46;
inline constexpr std::size_t kOot3dNameEntryStaticValueCount = 2374;
inline constexpr std::size_t kOot3dNameEntryStaticSourceBytes = 5054;

// This storage is populated asynchronously by the native localized resource
// loader.  It is deliberately not copied into the immutable static model.
inline constexpr std::uint32_t kOot3dNameEntryLocalizedAlphabetAddress =
    0x0055B64C;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetPageCount = 4;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetPageBytes = 0x200;
inline constexpr std::size_t kOot3dNameEntryLocalizedAlphabetStorageBytes =
    kOot3dNameEntryLocalizedAlphabetPageCount *
    kOot3dNameEntryLocalizedAlphabetPageBytes;

using UiNameEntryKanaAvailabilityTable =
    std::array<std::int32_t, kOot3dNameEntryKanaKeyCount>;
using UiNameEntryKanaKeyboardTables = std::array<
    std::uint16_t,
    kOot3dNameEntryKanaVariantCount *
        kOot3dNameEntryKanaPageCodeUnitCount>;
using UiNameEntryBuiltinLatinTables = std::array<
    std::uint16_t,
    kOot3dNameEntryBuiltinLatinPageCount *
        kOot3dNameEntryBuiltinLatinPageCodeUnitCount>;
using UiNameEntryKanaMechanicTables = std::array<
    std::uint16_t,
    kOot3dNameEntryKanaScriptCount * kOot3dNameEntryKanaVariantCount *
        kOot3dNameEntryKanaPageCodeUnitCount>;

struct UiNameEntryCodeUnitPageView {
    const std::uint16_t* data = nullptr;
    std::size_t size = 0;

    constexpr bool Empty() const noexcept { return size == 0; }
    constexpr const std::uint16_t* begin() const noexcept { return data; }
    constexpr const std::uint16_t* end() const noexcept {
        return data == nullptr ? nullptr : data + size;
    }
    constexpr const std::uint16_t& operator[](std::size_t index) const noexcept {
        return data[index];
    }
};

// The fields retain the ten distinct original data owners even where two
// owners currently contain identical bytes.  Their consumers and replacement
// roles are not interchangeable.
struct UiNameEntryStaticTables {
    UiNameEntryKanaAvailabilityTable dakuten_availability;
    UiNameEntryKanaAvailabilityTable handakuten_availability;
    UiNameEntryKanaAvailabilityTable small_kana_availability;
    UiNameEntryKanaKeyboardTables hiragana_tables;
    UiNameEntryKanaKeyboardTables katakana_tables;
    UiNameEntryBuiltinLatinTables builtin_latin_tables;
    UiNameEntryKanaMechanicTables kana_transform_targets;
    UiNameEntryKanaMechanicTables kana_lookup_tables;
    UiNameEntryKanaMechanicTables kana_classification_tables;
    UiNameEntryKanaMechanicTables kana_cycle_tables;
};

// Immutable host-side transcription of the original OoT3D USA rev0 tables.
// Accessors neither read nor write guest memory.
const UiNameEntryStaticTables& Oot3dNameEntryStaticTables() noexcept;

UiNameEntryCodeUnitPageView Oot3dNameEntryKanaKeyboardPage(
    UiNameEntryKanaScript script, UiNameEntryKanaVariant variant) noexcept;
UiNameEntryCodeUnitPageView Oot3dNameEntryBuiltinLatinKeyboardPage(
    UiNameEntryBuiltinLatinPage page) noexcept;
UiNameEntryCodeUnitPageView Oot3dNameEntryKanaMechanicPage(
    UiNameEntryKanaMechanicTable table, UiNameEntryKanaScript script,
    UiNameEntryKanaVariant variant) noexcept;
bool Oot3dNameEntryKanaKeySupports(
    UiNameEntryKanaTransform transform, std::size_t key_index) noexcept;

static_assert(static_cast<std::size_t>(UiNameEntryKanaScript::Count) ==
              kOot3dNameEntryKanaScriptCount);
static_assert(static_cast<std::size_t>(UiNameEntryKanaVariant::Count) ==
              kOot3dNameEntryKanaVariantCount);
static_assert(static_cast<std::size_t>(UiNameEntryBuiltinLatinPage::Count) ==
              kOot3dNameEntryBuiltinLatinPageCount);
static_assert(static_cast<std::size_t>(UiNameEntryKanaTransform::Count) ==
              kOot3dNameEntryKanaTransformCount);
static_assert(static_cast<std::size_t>(UiNameEntryKanaMechanicTable::Count) ==
              kOot3dNameEntryKanaMechanicTableCount);
static_assert(sizeof(UiNameEntryKanaAvailabilityTable) * 3 +
                  sizeof(UiNameEntryKanaKeyboardTables) * 2 +
                  sizeof(UiNameEntryBuiltinLatinTables) +
                  sizeof(UiNameEntryKanaMechanicTables) * 4 ==
              kOot3dNameEntryStaticSourceBytes);
static_assert(kOot3dNameEntryLocalizedAlphabetStorageBytes == 2048);

} // namespace oot3d::ui
