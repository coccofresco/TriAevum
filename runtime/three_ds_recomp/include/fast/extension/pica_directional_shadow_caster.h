#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Extension {

inline constexpr const char* kPicaDirectionalShadowFragmentShader = "#version 450\nvoid main(){}\n";

// Caster transform block. It sits outside the PICA binding contract (0-28),
// so an alpha-tested caster keeps the native texture and fragment uniform
// bindings of its own program.
inline constexpr uint32_t kPicaShadowCasterTransformBinding = 30U;

enum class PicaDirectionalShadowCasterShaderStatus : uint8_t {
    Applied,
    EmptySource,
    InvalidHookContract,
    MissingViewPosition,
    MissingAlphaTest,
    SamplerContract,
    OutputContract,
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

// Point-light cube-face variant: the face's world-to-clip transform arrives
// as a 64-byte push constant; the caster block supplies view-to-world.
[[nodiscard]] PicaDirectionalShadowCasterShader
BuildPicaPointShadowCasterShader(
    std::string_view source, uint64_t vertexKey,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks);

// Alpha-tested caster fragment program: the canonical program up to its
// BeforeDepth hook, which follows the native TEV and alpha test, so the map
// discards exactly the fragments the native draw discards. Nothing after the
// hook runs (no depth, fog or colour output); the rasterized light depth is
// written. The native colour output (published at location 0) is no longer
// exported, and combined samplers are split to the NRI binding contract.
[[nodiscard]] PicaDirectionalShadowCasterShader
BuildPicaShadowCasterAlphaFragmentShader(
    std::string_view source, uint64_t fragmentKey,
    const ::Fast::Renderer3ds::PicaShaderHookLayout& hooks);

} // namespace Fast::Extension
