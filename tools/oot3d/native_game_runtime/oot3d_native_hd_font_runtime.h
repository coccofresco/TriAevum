#pragma once

#include "oot3d_native_font_coverage.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_a32_memory.h"

#include <filesystem>

namespace Oot3dNativeGame {

// The native A4 glyph blitter is shared by static and dynamic QBF atlases.
inline constexpr std::uint32_t kNativeQbfA4Blit = 0x002d2504;

class NativeHdFontRuntime {
  public:
    bool Load(const std::filesystem::path& path, std::string* error);
    void ObserveBlit(const oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory);
    void Reset();
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
    };
    std::vector<Font> mFonts;
    std::unordered_map<std::uint32_t, Surface> mSurfaces;
    std::uint32_t mOutputHeight = 0;
};
} // namespace Oot3dNativeGame
