#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {

// Conservative summed-area table of non-interior tiles. One failed texel
// invalidates its tile; coarse tiles can prevent merging, never fill a hole.
class GrassMaskInterior final {
  public:
    template<class IsInterior>
    GrassMaskInterior(uint32_t width, uint32_t height, std::span<const uint8_t> samples, IsInterior interior)
        : Width(width), Height(height), Tile(std::max(1U,(std::max(width,height)+511U)/512U)),
          Columns((width + Tile - 1) / Tile), Rows((height + Tile - 1) / Tile) {
        if (!width || !height || width > 65535 || height > 65535 || uint64_t(width) * height != samples.size())
            throw std::invalid_argument("invalid Grass interior mask");
        std::array<bool,256> accepted{};
        for (uint32_t i = 0; i < accepted.size(); ++i) accepted[i] = interior(static_cast<uint8_t>(i));
        Prefix.resize(size_t(Columns + 1) * (Rows + 1));
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                if (!accepted[samples[size_t(y) * width + x]])
                    Prefix[size_t(y / Tile + 1) * (Columns + 1) + x / Tile + 1] = 1;
        for (uint32_t y = 1; y <= Rows; ++y)
            for (uint32_t x = 1; x <= Columns; ++x) {
                const auto i = size_t(y) * (Columns + 1) + x;
                Prefix[i] += Prefix[i-1] + Prefix[i-Columns-1] - Prefix[i-Columns-2];
            }
    }

    // Caller resolves wrap into a conservative normalized rectangle.
    uint32_t InteriorTiles() const noexcept { return Columns * Rows - Prefix.back(); }
    uint32_t TileCount() const noexcept { return Columns * Rows; }

    bool Contains(std::array<float,2> lo, std::array<float,2> hi) const noexcept {
        for (size_t axis = 0; axis < 2; ++axis)
            if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] > hi[axis]) return false;
        const auto tile = [&](float v, uint32_t extent) {
            return std::min(static_cast<uint32_t>(std::clamp(v,0.0F,1.0F)*extent), extent-1) / Tile;
        };
        const uint32_t x0=tile(lo[0],Width), y0=tile(lo[1],Height);
        const uint32_t x1=tile(hi[0],Width)+1, y1=tile(hi[1],Height)+1;
        const auto at = [&](uint32_t x,uint32_t y) { return Prefix[size_t(y)*(Columns+1)+x]; };
        return at(x1,y1) + at(x0,y0) == at(x0,y1) + at(x1,y0);
    }

  private:
    uint32_t Width, Height, Tile, Columns, Rows;
    std::vector<uint32_t> Prefix;
};

struct GrassUnknownMaskInterior {
    bool operator()(std::array<float,2>,std::array<float,2>) const noexcept { return false; }
};

} // namespace Fast::Oot3d
