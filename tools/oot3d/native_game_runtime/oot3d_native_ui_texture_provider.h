#pragma once

#include "oot3d_n64_ui_renderer.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_ui/ui_localized_menu_resources.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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
};

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

    const Oot3dNativeUiTextureProviderStats& Stats() const noexcept;

  private:
    const Oot3dPicaPhysicalMemoryView& mMemory;
    const TopScreenTextureOverridePack* mTextureOverrides = nullptr;
    mutable Oot3dNativeUiTextureProviderStats mStats;
};

} // namespace Oot3dNativeGame
