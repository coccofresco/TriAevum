#pragma once

#include "oot3d_n64_ui_renderer.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_ui/ui_localized_menu_resources.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

struct Oot3dNativeUiTextureEncoding {
    std::uint8_t native_pica_format = 0;
    std::size_t encoded_byte_count = 0;
};

struct Oot3dNativeUiTextureDescriptor {
    oot3d::ui::UiPauseSharedTextureSlot slot =
        oot3d::ui::UiPauseSharedTextureSlot::Count;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<Oot3dNativeUiTextureEncoding> encodings;

    const Oot3dNativeUiTextureEncoding* FindEncoding(
        std::uint8_t nativePicaFormat) const noexcept;
};

std::optional<Oot3dNativeUiTextureDescriptor>
ResolveOot3dNativeUiTextureDescriptor(std::string_view semanticName,
                                      std::string* error = nullptr);

struct Oot3dNativeUiTextureProviderStats {
    std::uint64_t resolve_calls = 0;
    std::uint64_t physical_reads = 0;
    std::uint64_t translated_guest_reads = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t runtime_metadata_reads = 0;
    std::uint64_t runtime_metadata_failures = 0;
    std::uint64_t contract_mismatches = 0;
    std::uint64_t decode_failures = 0;
    std::uint64_t override_checks = 0;
    std::uint64_t overrides_applied = 0;
    std::uint64_t overrides_already_applied = 0;
    std::uint64_t override_no_matches = 0;
    std::uint64_t profile_asset_resolves = 0;
    std::uint64_t profile_asset_decode_failures = 0;
    std::uint64_t custom_texture_replacements = 0;
    std::uint64_t custom_texture_pending = 0;
};

// A decoded UI texture offered to a custom texture source (a user texture
// pack). Its identity is the one a native PICA upload of the same bytes has:
// the encoded base level, after any TopScreen CTXB override, as the game with
// that mod installed would upload it.
struct Oot3dNativeUiCustomTextureRequest {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t native_pica_format = 0;
    std::span<const std::uint8_t> encoded_base_level;
    std::span<const std::uint8_t> rgba8;
};

struct Oot3dNativeUiCustomTexture {
    enum class State : std::uint8_t { None, Pending, Ready };
    State state = State::None;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> rgba8;
};

struct Oot3dNativeUiCustomTextureSource {
    std::function<Oot3dNativeUiCustomTexture(
        const Oot3dNativeUiCustomTextureRequest&)> resolve;
    // Changes when replacements may differ from earlier ones.
    std::function<std::uint64_t()> revision;
};

// Bytes of a PICA texture's base level, or 0 for an unknown format.
std::size_t Oot3dNativePicaBaseLevelBytes(std::uint8_t nativePicaFormat,
                                          std::uint16_t width,
                                          std::uint16_t height) noexcept;

// Reads the CTXB payload selected by the original OoT3D resource loader from
// guest/PICA memory, then decodes its native tiled representation for upload.
class Oot3dNativeA32UiTextureProvider final
    : public oot3d::ui::UiTextureProvider {
  public:
    explicit Oot3dNativeA32UiTextureProvider(
        const Oot3dPicaPhysicalMemoryView& memory,
        const TopScreenTextureOverridePack* textureOverrides = nullptr) noexcept;

    bool Resolve(const oot3d::ui::UiTextureIdentity& identity,
                 oot3d::ui::UiTexturePixels& pixels,
                 std::string* error = nullptr) const override;
    std::uint64_t Revision() const noexcept override;

    const Oot3dNativeUiTextureProviderStats& Stats() const noexcept;
    void SetGeneratedTextureTransform(Oot3dNativePicaSubmissionQueue::TextureSnapshotTransform transform) {
        mGeneratedTextureTransform = std::move(transform);
    }
    // Native and TopScreen profile textures (not generated text) are offered
    // to this source; a ready replacement is returned in their place.
    void SetCustomTextureSource(Oot3dNativeUiCustomTextureSource source) {
        mCustomTextures = std::move(source);
    }

  private:
    void ApplyCustomTexture(std::uint8_t nativePicaFormat,
                            std::span<const std::uint8_t> encoded,
                            oot3d::ui::UiTexturePixels& pixels) const;

    const Oot3dPicaPhysicalMemoryView& mMemory;
    const TopScreenTextureOverridePack* mTextureOverrides = nullptr;
    mutable Oot3dNativeUiTextureProviderStats mStats;
    Oot3dNativePicaSubmissionQueue::TextureSnapshotTransform mGeneratedTextureTransform;
    Oot3dNativeUiCustomTextureSource mCustomTextures;
};

} // namespace Oot3dNativeGame
