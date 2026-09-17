#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

// Title-owned coverage only: no message parsing, layout or PICA state mutation.
// A blit observer must supply a glyph offset within the validated native QBF,
// not a character guessed from pixels or a recycled guest pointer.
class NativeFontCoverage {
  public:
    bool Load(std::span<const std::uint8_t> native,
              std::span<const std::uint8_t> replacement, std::string* error);
    std::span<const std::uint8_t> Glyph(std::uint32_t nativeByteOffset) const;
    std::uint32_t LogicalWidth() const { return mWidth; }
    std::uint32_t LogicalHeight() const { return mHeight; }
    std::uint32_t Density() const { return mDensity; }

  private:
    std::uint32_t mWidth = 0, mHeight = 0, mDensity = 0;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> mGlyphs;
};

struct NativeFontAtlasSnapshot {
    std::uint64_t Epoch = 0;
    std::uint64_t Generation = 0;
    std::uint32_t Width = 0, Height = 0;
    // Linear alpha coverage. Publication chooses the canonical native texture
    // channel contract; this is not implicitly RGBA or a material override.
    std::shared_ptr<const std::vector<std::uint8_t>> Coverage;
};

class NativeFontCoverageAtlas {
  public:
    // The owner supplies a fresh epoch on allocation reuse, language change or
    // savestate restore. Old immutable snapshots remain valid for queued draws.
    bool Reset(std::uint64_t epoch, std::uint32_t logicalWidth,
               std::uint32_t logicalHeight, std::uint32_t density);
    bool Blit(const NativeFontCoverage& font, std::uint32_t nativeGlyphOffset,
              std::uint32_t x, std::uint32_t y);
    bool Clear(std::uint32_t x, std::uint32_t y,
               std::uint32_t width, std::uint32_t height);
    NativeFontAtlasSnapshot Snapshot() const;

  private:
    bool Fits(std::uint32_t x, std::uint32_t y,
              std::uint32_t width, std::uint32_t height) const;
    void MakeWritable();
    std::uint64_t mEpoch = 0, mGeneration = 0;
    std::uint32_t mWidth = 0, mHeight = 0, mDensity = 0;
    std::shared_ptr<std::vector<std::uint8_t>> mCoverage;
};

} // namespace Oot3dNativeGame
