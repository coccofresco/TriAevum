#include "fast/oot3d/cacao_normal_input.h"

namespace Fast::Oot3d {

CacaoNormalInputDecision ResolveCacaoNormalInput(
    CacaoQuality quality, bool picaNormalGuideAvailable) noexcept {
    if (quality == CacaoQuality::Medium && picaNormalGuideAvailable) {
        return {CacaoNormalSource::PicaViewSpaceGuide};
    }
    return {CacaoNormalSource::ReconstructedFromDepth};
}

std::array<float, 16> CacaoViewSpaceNormalTransform(
    const std::array<float, 16>& projection) noexcept {
    // Row-vector projection: clip.w = projection[11] * view.z. The
    // renderer's RH perspective uses -1; CACAO NDCToViewSpace uses +depth.
    const float z = projection[11] < 0.0F ? -1.0F : 1.0F;
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, z, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

} // namespace Fast::Oot3d
