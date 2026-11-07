#pragma once
#include "fast/renderer3ds/pica_vertex_geometry.h"

namespace Fast::Renderer3ds {
enum class PicaAffineUvStatus {
    Applied,
    UnsupportedProgram,
    MissingUniforms,
    Disabled,
    Procedural,
    InvalidSource,
    InvalidTransform
};
struct PicaAffineUv {
    PicaAffineUvStatus Status = PicaAffineUvStatus::UnsupportedProgram;
    uint8_t InputRegister = 0xff;
    std::array<float, 6> Rows{};
    std::array<float, 2> Apply(const std::array<float, 4>& raw) const {
        return { Rows[0] * raw[0] + Rows[1] * raw[1] + Rows[2], Rows[3] * raw[0] + Rows[4] * raw[1] + Rows[5] };
    }
};
inline PicaAffineUv DecodePicaAffineUv(const PicaVertexTextureCoordinateLayout* layout, uint32_t booleans,
                                       std::span<const uint8_t> uniforms) {
    PicaAffineUv result;
    if (!layout || !layout->Available() || layout->Operation != PicaVertexTextureCoordinateOperation::CmbAffine)
        return result;
    if (uniforms.size() < 96 * 16) {
        result.Status = PicaAffineUvStatus::MissingUniforms;
        return result;
    }
    auto scalar = [&](uint8_t u, uint8_t c) { return VertexGeometry::Scalar(uniforms, { u, c }); };
    auto equal = [](float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 1e-5F; };
    if (!(booleans & (1U << layout->EnableBooleanUniform))) {
        result.Status = PicaAffineUvStatus::Disabled;
        return result;
    }
    const float mode = scalar(layout->CoordinateModeUniform, layout->CoordinateModeComponent);
    if (equal(mode, scalar(layout->CoordinateModeConstantUniform, layout->CoordinateModeConstantComponents[0])) ||
        equal(mode, scalar(layout->CoordinateModeConstantUniform, layout->CoordinateModeConstantComponents[1]))) {
        result.Status = PicaAffineUvStatus::Procedural;
        return result;
    }
    const auto& guards = layout->AffineGuards;
    if (guards.Version == 1) {
        const auto value = [&](const auto& r) { return scalar(r[0], r[1]); };
        if (value(guards.Zero) != 0.0F || value(guards.RootMode) != 0.0F ||
            !std::isfinite(value(guards.Stride)) || scalar(layout->HomogeneousUniform, layout->HomogeneousComponent) != 1.0F) {
            result.Status = PicaAffineUvStatus::UnsupportedProgram;
            return result;
        }
        if (!std::isfinite(mode) || mode < 1.0F ||
            (mode != value(guards.DirectModes[0]) && mode != value(guards.DirectModes[1]))) {
            result.Status = PicaAffineUvStatus::Procedural;
            return result;
        }
    }
    const float selector = scalar(layout->SourceSelectorUniform, layout->SourceSelectorComponent);
    const auto sourceEqual = [&](float a, float b) { return guards.Version == 1 ? a == b : equal(a, b); };
    const bool first =
        sourceEqual(selector, scalar(layout->SourceSelectorConstantUniform, layout->SourceSelectorConstantComponents[0]));
    const bool second =
        sourceEqual(selector, scalar(layout->SourceSelectorConstantUniform, layout->SourceSelectorConstantComponents[1]));
    const size_t index = first && !second ? 0 : !first && second ? 1 : !first && !second ? 2 : 3;
    if (index >= layout->SourceCount) {
        result.Status = PicaAffineUvStatus::InvalidSource;
        return result;
    }
    const auto& source = layout->Sources[index];
    if (!(booleans & (1U << source.EnableBooleanUniform))) {
        result.Status = PicaAffineUvStatus::InvalidSource;
        return result;
    }
    result.InputRegister = source.InputRegister;
    const float scale = scalar(source.ScaleUniform, source.ScaleComponent);
    const float homogeneous = scalar(layout->HomogeneousUniform, layout->HomogeneousComponent);
    const auto u = layout->MatrixRowUUniform, v = layout->MatrixRowVUniform;
    result.Rows = { scalar(u, 0) * scale,  scalar(u, 1) * scale,  (scalar(u, 2) + scalar(u, 3)) * homogeneous,
                    -scalar(v, 0) * scale, -scalar(v, 1) * scale, 1 - (scalar(v, 2) + scalar(v, 3)) * homogeneous };
    result.Status =
        std::isfinite(scale) && scale != 0 &&
                std::all_of(result.Rows.begin(), result.Rows.end(), [](float f) { return std::isfinite(f); })
            ? PicaAffineUvStatus::Applied
            : PicaAffineUvStatus::InvalidTransform;
    return result;
}
} // namespace Fast::Renderer3ds
