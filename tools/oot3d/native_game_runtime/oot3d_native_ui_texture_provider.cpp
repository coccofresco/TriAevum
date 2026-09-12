#include "oot3d_native_ui_texture_provider.h"

#include "oot3d/renderer/pica_texture_decode.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace Oot3dNativeGame {
namespace {

constexpr std::string_view kPauseSharedSemanticPrefix =
    "oot3d/native/pause_shared/";
constexpr std::string_view kLocalizedSemanticPrefix =
    "oot3d/native/localized/";
constexpr std::string_view kCameraOptionGlyphSemantic =
    "oot3d/topscreen/camera_option_glyphs";

constexpr std::uint16_t kPicaUnsignedByte = 0x1401U;
constexpr std::uint16_t kPicaUnsignedByte44 = 0x6760U;
constexpr std::uint16_t kPicaUnsigned4Bits = 0x6761U;
constexpr std::uint16_t kPicaUnsignedShort4444 = 0x8033U;
constexpr std::uint16_t kPicaUnsignedShort5551 = 0x8034U;
constexpr std::uint16_t kPicaUnsignedShort565 = 0x8363U;
constexpr std::uint16_t kPicaTextureRgba = 0x6752U;
constexpr std::uint16_t kPicaTextureRgb = 0x6754U;
constexpr std::uint16_t kPicaTextureAlpha = 0x6756U;
constexpr std::uint16_t kPicaTextureLuminance = 0x6757U;
constexpr std::uint16_t kPicaTextureLuminanceAlpha = 0x6758U;
constexpr std::uint16_t kPicaTextureEtc1 = 0x675AU;
constexpr std::uint16_t kPicaTextureEtc1A4 = 0x675BU;
constexpr std::uint32_t kCtxbSourceMetadataOffset = 0x2CU;
constexpr std::size_t kCtxbSourceMetadataByteCount = 0x24U;
constexpr std::size_t kCtxbSourceWidthOffset = 0x00U;
constexpr std::size_t kCtxbSourceHeightOffset = 0x02U;
constexpr std::size_t kCtxbSourceRawFormatOffset = 0x04U;
constexpr std::size_t kCtxbSourceRawTypeOffset = 0x06U;
constexpr std::size_t kCtxbSourceSurfaceOffset = 0x20U;

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool ResolveCameraOptionGlyphAtlas(
    const oot3d::ui::UiTextureIdentity& identity,
    oot3d::ui::UiTexturePixels& pixels) {
    if (identity.semantic_name != kCameraOptionGlyphSemantic) {
        return false;
    }

    struct GlyphRows {
        std::uint8_t character;
        std::array<std::uint8_t, 10> rows;
    };
    constexpr std::array kGlyphs{
        GlyphRows{' ', {0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00}},
        GlyphRows{'.', {0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x18, 0x18, 0x00, 0x00}},
        GlyphRows{'B', {0x00, 0x78, 0x24, 0x24, 0x38,
                        0x24, 0x24, 0x78, 0x00, 0x00}},
        GlyphRows{'I', {0x00, 0x38, 0x10, 0x10, 0x10,
                        0x10, 0x10, 0x38, 0x00, 0x00}},
        GlyphRows{'N', {0x00, 0x44, 0x64, 0x54, 0x4C,
                        0x44, 0x44, 0x44, 0x00, 0x00}},
        GlyphRows{'X', {0x00, 0x44, 0x44, 0x28, 0x10,
                        0x28, 0x44, 0x44, 0x00, 0x00}},
        GlyphRows{'Y', {0x00, 0x44, 0x44, 0x44, 0x28,
                        0x10, 0x10, 0x10, 0x00, 0x00}},
        GlyphRows{'a', {0x00, 0x00, 0x00, 0x38, 0x04,
                        0x3C, 0x44, 0x3C, 0x00, 0x00}},
        GlyphRows{'d', {0x00, 0x04, 0x04, 0x34, 0x4C,
                        0x44, 0x4C, 0x34, 0x00, 0x00}},
        GlyphRows{'e', {0x00, 0x00, 0x00, 0x38, 0x44,
                        0x7C, 0x40, 0x3C, 0x00, 0x00}},
        GlyphRows{'h', {0x00, 0x40, 0x40, 0x78, 0x44,
                        0x44, 0x44, 0x44, 0x00, 0x00}},
        GlyphRows{'l', {0x00, 0x30, 0x10, 0x10, 0x10,
                        0x10, 0x10, 0x38, 0x00, 0x00}},
        GlyphRows{'m', {0x00, 0x00, 0x00, 0x68, 0x54,
                        0x54, 0x54, 0x54, 0x00, 0x00}},
        GlyphRows{'n', {0x00, 0x00, 0x00, 0x58, 0x64,
                        0x44, 0x44, 0x44, 0x00, 0x00}},
        GlyphRows{'o', {0x00, 0x00, 0x00, 0x38, 0x44,
                        0x44, 0x44, 0x38, 0x00, 0x00}},
        GlyphRows{'r', {0x00, 0x00, 0x00, 0x58, 0x64,
                        0x40, 0x40, 0x40, 0x00, 0x00}},
        GlyphRows{'t', {0x00, 0x10, 0x10, 0x38, 0x10,
                        0x10, 0x10, 0x0C, 0x00, 0x00}},
        GlyphRows{'v', {0x00, 0x00, 0x00, 0x44, 0x44,
                        0x44, 0x28, 0x10, 0x00, 0x00}},
        GlyphRows{'|', {0x10, 0x10, 0x10, 0x10, 0x00,
                        0x10, 0x10, 0x10, 0x10, 0x00}},
    };
    constexpr std::uint16_t kWidth = 128U;
    constexpr std::uint16_t kHeight = 256U;
    constexpr std::size_t kCellWidth = 8U;
    constexpr std::size_t kCellHeight = 16U;
    pixels.width = kWidth;
    pixels.height = kHeight;
    pixels.rgba8.assign(static_cast<std::size_t>(kWidth) * kHeight * 4U, 0U);
    for (const auto& glyph : kGlyphs) {
        const std::size_t originX = (glyph.character & 0x0FU) * kCellWidth;
        const std::size_t originY = (glyph.character >> 4U) * kCellHeight;
        for (std::size_t y = 0; y < glyph.rows.size(); ++y) {
            for (std::size_t x = 0; x < 6U; ++x) {
                const std::size_t pixel =
                    ((originY + y) * kWidth + originX + x) * 4U;
                const std::uint8_t value =
                    (glyph.rows[y] & (0x40U >> x)) != 0U ? 0xFFU : 0x00U;
                pixels.rgba8[pixel] = value;
                pixels.rgba8[pixel + 1U] = value;
                pixels.rgba8[pixel + 2U] = value;
                pixels.rgba8[pixel + 3U] = 0xFFU;
            }
        }
    }
    return true;
}

std::uint16_t LoadU16(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1U] << 8U);
}

std::uint32_t LoadU32(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::optional<std::uint8_t> NativePicaFormat(std::uint16_t rawFormat,
                                             std::uint16_t rawType) noexcept {
    if (rawFormat == kPicaTextureRgba && rawType == kPicaUnsignedByte) {
        return 0U;
    }
    if (rawFormat == kPicaTextureRgb && rawType == kPicaUnsignedByte) {
        return 1U;
    }
    if (rawFormat == kPicaTextureRgba &&
        rawType == kPicaUnsignedShort5551) {
        return 2U;
    }
    if (rawFormat == kPicaTextureRgb && rawType == kPicaUnsignedShort565) {
        return 3U;
    }
    if (rawFormat == kPicaTextureRgba &&
        rawType == kPicaUnsignedShort4444) {
        return 4U;
    }
    if (rawFormat == kPicaTextureLuminanceAlpha &&
        rawType == kPicaUnsignedByte) {
        return 5U;
    }
    if (rawFormat == kPicaTextureLuminance &&
        rawType == kPicaUnsignedByte) {
        return 7U;
    }
    if (rawFormat == kPicaTextureAlpha && rawType == kPicaUnsignedByte) {
        return 8U;
    }
    if (rawFormat == kPicaTextureLuminanceAlpha &&
        rawType == kPicaUnsignedByte44) {
        return 9U;
    }
    if (rawFormat == kPicaTextureLuminance &&
        rawType == kPicaUnsigned4Bits) {
        return 10U;
    }
    if (rawFormat == kPicaTextureAlpha &&
        rawType == kPicaUnsigned4Bits) {
        return 11U;
    }
    if (rawFormat == kPicaTextureEtc1 && rawType == 0U) {
        return 12U;
    }
    if (rawFormat == kPicaTextureEtc1A4 && rawType == 0U) {
        return 13U;
    }
    return std::nullopt;
}

const oot3d::ui::UiLocalizedMenuTextureDescriptor*
LocalizedDescriptorForSemantic(std::string_view semanticName) noexcept {
    if (semanticName ==
        std::string(kLocalizedSemanticPrefix) + "file_select_active") {
        return oot3d::ui::Oot3dLocalizedMenuTextureDescriptorFor(
            oot3d::ui::UiLocalizedMenuTextureKind::FileSelectParts00);
    }
    const bool pauseShared =
        semanticName.starts_with(kPauseSharedSemanticPrefix);
    const bool localized = semanticName.starts_with(kLocalizedSemanticPrefix);
    if (!pauseShared && !localized) {
        return nullptr;
    }
    const std::string_view suffix = semanticName.substr(
        pauseShared ? kPauseSharedSemanticPrefix.size()
                    : kLocalizedSemanticPrefix.size());
    for (const auto& descriptor :
         oot3d::ui::Oot3dLocalizedMenuTextureDescriptors()) {
        if ((!pauseShared || descriptor.IsPauseShared()) &&
            descriptor.semantic_name != nullptr &&
            suffix == descriptor.semantic_name) {
            return &descriptor;
        }
    }
    return nullptr;
}

} // namespace

const Oot3dNativeUiTextureEncoding*
Oot3dNativeUiTextureDescriptor::FindEncoding(
    std::uint8_t nativePicaFormat) const noexcept {
    const auto found = std::find_if(
        encodings.begin(), encodings.end(),
        [nativePicaFormat](const Oot3dNativeUiTextureEncoding& encoding) {
            return encoding.native_pica_format == nativePicaFormat;
        });
    return found != encodings.end() ? &*found : nullptr;
}

std::optional<Oot3dNativeUiTextureDescriptor>
ResolveOot3dNativeUiTextureDescriptor(std::string_view semanticName,
                                      std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    const auto* descriptor = LocalizedDescriptorForSemantic(semanticName);
    if (descriptor == nullptr) {
        SetError(error, "OoT3D UI semantic is not a native localized texture");
        return std::nullopt;
    }

    Oot3dNativeUiTextureDescriptor resolved{
        descriptor->native_pause_shared_slot,
        descriptor->width,
        descriptor->height,
        {},
    };
    for (std::size_t languageIndex = 0;
         languageIndex < oot3d::ui::kOot3dUiLanguageCount;
         ++languageIndex) {
        const auto resource = oot3d::ui::Oot3dLocalizedMenuTextureResource(
            static_cast<oot3d::ui::Oot3dUiLanguage>(languageIndex),
            descriptor->kind);
        if (!resource.Present()) {
            continue;
        }
        const auto mapped = NativePicaFormat(resource.raw_format,
                                             resource.raw_type);
        if (!mapped.has_value()) {
            SetError(error,
                     "OoT3D pause-shared CTXB format is not supported");
            return std::nullopt;
        }
        const auto existing = std::find_if(
            resolved.encodings.begin(), resolved.encodings.end(),
            [mapped](const Oot3dNativeUiTextureEncoding& encoding) {
                return encoding.native_pica_format == *mapped;
            });
        if (existing != resolved.encodings.end()) {
            if (existing->encoded_byte_count != resource.payload_byte_count) {
                SetError(error,
                         "OoT3D CTXB payload size diverges for one PICA format");
                return std::nullopt;
            }
            continue;
        }
        resolved.encodings.push_back(
            {*mapped, resource.payload_byte_count});
    }
    if (resolved.encodings.empty()) {
        SetError(error, "OoT3D localized CTXB has no verified payload");
        return std::nullopt;
    }
    std::sort(resolved.encodings.begin(), resolved.encodings.end(),
              [](const Oot3dNativeUiTextureEncoding& left,
                 const Oot3dNativeUiTextureEncoding& right) {
                  return left.native_pica_format < right.native_pica_format;
              });

    return resolved;
}

Oot3dNativeA32UiTextureProvider::Oot3dNativeA32UiTextureProvider(
    const Oot3dPicaPhysicalMemoryView& memory,
    const TopScreenTextureOverridePack* textureOverrides) noexcept
    : mMemory(memory), mTextureOverrides(textureOverrides) {}

bool Oot3dNativeA32UiTextureProvider::Resolve(
    const oot3d::ui::UiTextureIdentity& identity,
    oot3d::ui::UiTexturePixels& pixels, std::string* error) const {
    ++mStats.resolve_calls;
    pixels = {};
    if (error != nullptr) {
        error->clear();
    }
    if (mTextureOverrides != nullptr) {
        const auto* profileTexture =
            mTextureOverrides->FindProfileTexture(identity.semantic_name);
        if (profileTexture != nullptr) {
            pixels.width = profileTexture->Width;
            pixels.height = profileTexture->Height;
            if (!Oot3d::Renderer::DecodePicaTextureRgba8(
                    profileTexture->NativePicaFormat,
                    profileTexture->Width, profileTexture->Height,
                    profileTexture->EncodedPayload, pixels.rgba8, error)) {
                ++mStats.profile_asset_decode_failures;
                pixels = {};
                return false;
            }
            ++mStats.profile_asset_resolves;
            return true;
        }
    }
    if (ResolveCameraOptionGlyphAtlas(identity, pixels)) {
        return true;
    }
    const bool generatedText = identity.semantic_name == "oot3d/native/generated_text";
    auto descriptor = generatedText ? std::optional<Oot3dNativeUiTextureDescriptor>{}
                                   : ResolveOot3dNativeUiTextureDescriptor(identity.semantic_name, error);
    if (!generatedText && !descriptor.has_value()) {
        return false;
    }
    if (identity.guest_resource_address == 0U) {
        ++mStats.runtime_metadata_failures;
        SetError(error, "OoT3D UI texture has no native CTXB source object");
        return false;
    }

    std::array<std::uint8_t, kCtxbSourceMetadataByteCount> metadata{};
    if (!mMemory.ReadGuest(
            identity.guest_resource_address + kCtxbSourceMetadataOffset,
            metadata)) {
        ++mStats.runtime_metadata_failures;
        SetError(error, "OoT3D UI CTXB source metadata is outside guest memory");
        return false;
    }
    ++mStats.runtime_metadata_reads;

    const std::uint16_t runtimeWidth =
        LoadU16(metadata, kCtxbSourceWidthOffset);
    const std::uint16_t runtimeHeight =
        LoadU16(metadata, kCtxbSourceHeightOffset);
    const std::uint16_t runtimeRawFormat =
        LoadU16(metadata, kCtxbSourceRawFormatOffset);
    const std::uint16_t runtimeRawType =
        LoadU16(metadata, kCtxbSourceRawTypeOffset);
    const std::uint32_t runtimeSurface =
        LoadU32(metadata, kCtxbSourceSurfaceOffset);
    const auto runtimeFormat = NativePicaFormat(runtimeRawFormat,
                                                runtimeRawType);
    if (generatedText) {
        constexpr std::array<std::uint8_t, 14> bitsPerPixel{
            32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8};
        if (!runtimeFormat || !runtimeWidth || !runtimeHeight ||
            runtimeWidth > 1024 || runtimeHeight > 1024 ||
            (runtimeWidth & (runtimeWidth - 1)) || (runtimeHeight & (runtimeHeight - 1)) ||
            runtimeWidth < 8 || runtimeHeight < 8) {
            ++mStats.contract_mismatches;
            SetError(error, "invalid native generated-text texture descriptor");
            return false;
        }
        const auto bytes = std::size_t(runtimeWidth) * runtimeHeight * bitsPerPixel[*runtimeFormat] / 8;
        std::array<std::uint8_t, 4> size{};
        if (!mMemory.ReadGuest(identity.guest_resource_address + 0x24U, size) ||
            LoadU32(size, 0) != bytes) {
            ++mStats.contract_mismatches;
            SetError(error, "native generated-text texture size mismatch");
            return false;
        }
        descriptor = Oot3dNativeUiTextureDescriptor{
            oot3d::ui::UiPauseSharedTextureSlot::Count, runtimeWidth, runtimeHeight,
            {{*runtimeFormat, bytes}}};
    }
    if (runtimeWidth != descriptor->width ||
        runtimeHeight != descriptor->height) {
        ++mStats.contract_mismatches;
        SetError(error, "OoT3D UI CTXB dimensions do not match its semantic");
        return false;
    }
    if (!runtimeFormat.has_value()) {
        ++mStats.contract_mismatches;
        SetError(error, "OoT3D UI CTXB runtime PICA format is unsupported");
        return false;
    }
    const auto* encoding = descriptor->FindEncoding(*runtimeFormat);
    if (encoding == nullptr) {
        ++mStats.contract_mismatches;
        SetError(error,
                 "OoT3D UI CTXB runtime encoding does not match its semantic");
        return false;
    }
    if (runtimeSurface == 0U || identity.guest_surface_address == 0U ||
        runtimeSurface != identity.guest_surface_address) {
        ++mStats.contract_mismatches;
        SetError(error, "OoT3D UI CTXB source/surface identity is stale");
        return false;
    }

    std::vector<std::uint8_t> encoded(encoding->encoded_byte_count);
    bool read = mMemory.Read(runtimeSurface, encoded);
    if (read) {
        ++mStats.physical_reads;
    } else {
        const auto physicalAddress = mMemory.TranslateGuest(
            runtimeSurface, encoded.size());
        read = physicalAddress.has_value() &&
               mMemory.Read(*physicalAddress, encoded);
        if (read) {
            ++mStats.translated_guest_reads;
        }
    }
    if (!read) {
        SetError(error,
                 "OoT3D UI texture surface is outside mapped PICA memory");
        return false;
    }
    mStats.bytes_read += encoded.size();

    if (mTextureOverrides != nullptr && !mTextureOverrides->Empty()) {
        ++mStats.override_checks;
        switch (mTextureOverrides->Apply(encoded)) {
        case TopScreenTextureOverrideResult::Applied:
            ++mStats.overrides_applied;
            break;
        case TopScreenTextureOverrideResult::AlreadyApplied:
            ++mStats.overrides_already_applied;
            break;
        case TopScreenTextureOverrideResult::NoMatch:
            ++mStats.override_no_matches;
            break;
        }
    }

    pixels.width = descriptor->width;
    pixels.height = descriptor->height;
    if (!Oot3d::Renderer::DecodePicaTextureRgba8(
            encoding->native_pica_format, descriptor->width,
            descriptor->height, encoded, pixels.rgba8, error)) {
        ++mStats.decode_failures;
        pixels = {};
        return false;
    }
    if (generatedText && (*runtimeFormat == 8U || *runtimeFormat == 11U)) {
        // Native text uses primary RGB and the A8/A4 font as coverage. The
        // host UI multiplies RGBA, so lower that mask to neutral RGB here.
        // Canonical PICA decoding (whose absent RGB is zero) stays untouched.
        for (std::size_t pixel = 0; pixel < pixels.rgba8.size(); pixel += 4) {
            pixels.rgba8[pixel] = pixels.rgba8[pixel + 1] = pixels.rgba8[pixel + 2] = 255;
        }
    }
    return true;
}

const Oot3dNativeUiTextureProviderStats&
Oot3dNativeA32UiTextureProvider::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
