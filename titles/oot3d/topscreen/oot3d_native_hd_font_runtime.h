#pragma once

#include "oot3d_native_font_coverage.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_a32_memory.h"

#include <atomic>
#include <filesystem>
#include <optional>

namespace Oot3dNativeGame {

// The native A4 glyph blitter is shared by static and dynamic QBF atlases.
inline constexpr std::uint32_t kNativeQbfA4Blit = 0x002d2504;

// Which coverage the native text receives (TopScreenUiConfig::DialogueFont):
// the pack's HD coverage from 480 output lines (Automatic), the native glyphs
// always (Native), or the alternate pack's coverage at every output height
// (Alternate; Automatic while no alternate pack is loaded).
enum class NativeHdFontMode : std::uint8_t { Automatic, Native, Alternate };

class NativeHdFontRuntime {
  public:
    bool Load(const std::filesystem::path& path, std::string* error);
    // A second QBF coverage pack (the N64-style font) paired with the same
    // native fonts as the loaded pack: each of its native fonts must be one of
    // the loaded pack's, byte for byte, so the one blit observer serves both.
    bool LoadAlternate(const std::filesystem::path& path, std::string* error);
    bool HasAlternate() const noexcept { return mAlternateLoaded; }
    void SetMode(NativeHdFontMode mode) noexcept { mMode.store(mode, std::memory_order_relaxed); }
    NativeHdFontMode Mode() const noexcept { return mMode.load(std::memory_order_relaxed); }
    void ObserveBlit(const oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory);
    void Reset();
    bool NeedsBlitObserver() const noexcept { return !mFonts.empty(); }
    void SetOutputHeight(std::uint32_t height) { mOutputHeight = height; }
    void Resolve(const Oot3dPicaPhysicalMemoryView& memory,
                 const Oot3dPicaTextureState& texture, Oot3dPicaResourceSnapshot& resource);
    std::uint64_t Blits = 0, Rebuilds = 0, Replacements = 0;

  private:
    struct Font {
        std::vector<std::uint8_t> Native;
        NativeFontCoverage Coverage;
    };
    struct Placement {
        std::size_t FontIndex;
        std::uint32_t Offset, X, Y;
    };
    struct Surface {
        std::uint32_t Width = 0, Height = 0;
        std::vector<Placement> Placements;
        std::vector<std::uint8_t> LastNative;
        std::shared_ptr<const std::vector<std::uint8_t>> Encoded;
        std::uint32_t Density = 0;
        std::uint64_t ContentHash = 0;
        bool Dirty = true;
        bool Alternate = false; // Encoded from the alternate coverage
    };
    struct StaticAtlas {
        std::uint32_t Width = 0, Height = 0, Density = 0;
        std::vector<std::uint8_t> Native;
        std::shared_ptr<const std::vector<std::uint8_t>> Coverage;
        std::uint64_t Hash = 0;
    };
    std::vector<StaticAtlas> mStaticAtlases;
    std::vector<Font> mFonts;
    // The alternate coverage of each of mFonts (by index), when loaded.
    std::vector<std::optional<NativeFontCoverage>> mAlternate;
    bool mAlternateLoaded = false;
    std::atomic<NativeHdFontMode> mMode{NativeHdFontMode::Automatic};
    std::unordered_map<std::uint32_t, Surface> mSurfaces;
    std::uint32_t mOutputHeight = 0;
    const NativeFontCoverage& CoverageFor(std::size_t index, bool alternate) const {
        return alternate && mAlternate[index] ? *mAlternate[index] : mFonts[index].Coverage;
    }
};
} // namespace Oot3dNativeGame
