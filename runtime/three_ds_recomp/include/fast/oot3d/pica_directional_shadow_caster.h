#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

inline constexpr const char* kPicaDirectionalShadowFragmentShader = "#version 450\nvoid main(){}\n";

enum class PicaDirectionalShadowCasterShaderStatus : uint8_t {
    Applied,
    EmptySource,
    InvalidHookContract,
    MissingViewPosition,
};

struct PicaDirectionalShadowCasterShader {
    std::string Source;
    uint64_t Key = 0U;
    PicaDirectionalShadowCasterShaderStatus Status =
        PicaDirectionalShadowCasterShaderStatus::EmptySource;

    [[nodiscard]] bool Applied() const noexcept {
        return Status == PicaDirectionalShadowCasterShaderStatus::Applied;
    }
};

// Builds a depth-caster variant only from frontend-published PICA hook
// offsets and semantics. It never discovers or rewrites a GLSL main function.
[[nodiscard]] PicaDirectionalShadowCasterShader
BuildPicaDirectionalShadowCasterShader(
    std::string_view source, uint64_t vertexKey,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks);

} // namespace Fast::Oot3d
