#pragma once

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_environment_lighting.h"
#include "fast/renderer3ds/pica_surface_appearance.h"

namespace Fast::Oot3d {

// The title already attributes actor geometry independently of material and
// skinning. Keep that policy here; the shared evaluator only receives gains.
[[nodiscard]] inline bool PicaDrawReceivesRimLight(
    const Renderer3ds::PicaEnvironmentLighting& environment,
    Renderer3ds::PicaCompositionDomain domain,
    const Renderer3ds::PicaCompositionAttribution& composition,
    const Renderer3ds::NativeBlendState& blend,
    Renderer3ds::PicaSurfaceAppearance appearance,
    bool depthTest, bool depthWrite, uint8_t fragmentOperation) noexcept {
    using namespace Renderer3ds;
    if (!environment.Enabled || !environment.Rim.Enabled ||
        domain != PicaCompositionDomain::Scene || composition.Layer != PicaCompositionLayer::OpaqueWorld ||
        composition.IsolatedLighting || composition.NativeProjectedShadow ||
        appearance != PicaSurfaceAppearance::Relightable || !depthTest || !depthWrite || fragmentOperation != 0U)
        return false;
    // A transparent blend remains transparent even when it writes depth or
    // was attributed to the opaque native pass. Identity RGB blending is OK.
    if (blend.Enabled && (blend.EquationRgb != NativeBlendEquation::Add ||
                          blend.SourceRgb != NativeBlendFactor::One || blend.DestRgb != NativeBlendFactor::Zero))
        return false;
    return environment.Rim.Scope == PicaRimLightScope::ActorsAndWorld ||
           (environment.Rim.Scope == PicaRimLightScope::Actors && composition.NativeActorGeometry);
}

} // namespace Fast::Oot3d
