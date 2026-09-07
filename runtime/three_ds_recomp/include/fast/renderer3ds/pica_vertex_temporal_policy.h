#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"

namespace Fast::Renderer3ds {

[[nodiscard]] inline bool IsPicaVertexInputContinuousForPresentation(const PicaVertexShaderHookLayout& hooks,
                                                                     uint8_t inputRegister) noexcept {
    if (hooks.Skeleton.Available() && inputRegister == hooks.Skeleton.BoneIndexInputRegister)
        return false;
    // UVs may select discrete atlas frames. Interpolating them samples unrelated
    // texels between cells. Continuous texture-matrix animation is a separate track.
    for (const auto& coordinate : hooks.TextureCoordinates) {
        if (coordinate.Operation == PicaVertexTextureCoordinateOperation::Unavailable)
            continue;
        for (size_t i = 0; i < coordinate.SourceCount && i < coordinate.Sources.size(); ++i) {
            if (coordinate.Sources[i].InputRegister == inputRegister)
                return false;
        }
    }
    return true;
}

} // namespace Fast::Renderer3ds
