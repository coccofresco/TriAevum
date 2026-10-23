#pragma once

#include <cstdint>

namespace Fast::Renderer3ds {

// Each group contains disjoint three-row affine matrices in ONE output space.
// A title adapter must prove the extent and shader/mode contract; float values
// never establish ownership. Sequential transforms must be separate groups.
struct PicaAffineUniformGroup {
    uint8_t FirstUniform = 0xffU;
    uint8_t MatrixCount = 0U;
    bool operator==(const PicaAffineUniformGroup&) const = default;
    [[nodiscard]] bool Valid() const noexcept {
        return MatrixCount != 0U && FirstUniform < 96U &&
               unsigned(FirstUniform) + 3U * MatrixCount <= 96U;
    }
};

struct PicaAffineUniformLayout {
    PicaAffineUniformGroup ModelView;
    PicaAffineUniformGroup Palette;
    bool operator==(const PicaAffineUniformLayout&) const = default;
};

} // namespace Fast::Renderer3ds
