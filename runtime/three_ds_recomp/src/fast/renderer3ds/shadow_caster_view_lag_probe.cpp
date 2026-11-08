// Developer diagnostic, F11 read timing (shadow_caster_view_lag_probe.h).
// Kept out of the decision-policy inputs of the shader corpus qualification:
// it only reads draw inputs and writes stderr lines.
#include "fast/renderer3ds/shadow_caster_view_lag_probe.h"
#include "fast/renderer3ds/shadow_caster_stability_trace.h"

#include <cstdlib>

namespace Fast::Renderer3ds {

bool ShadowCasterViewLagRequested() noexcept {
    static const bool requested = [] {
        const char* value = std::getenv("TRIAEVUM_SHADOW_CASTER_VIEW_LAG");
        return value != nullptr && value[0] != 0 && value[0] != '0';
    }();
    return requested;
}

bool ObserveShadowCasterViewLagDraw(ShadowCasterViewLagProbe& probe, const ShadowCasterViewLagDraw& draw) {
    ShadowMatrix postView, model;
    if (!VertexGeometry::UniformMatrix(draw.FloatUniforms, draw.ViewFirstUniform, postView) ||
        !VertexGeometry::UniformMatrix(draw.FloatUniforms, draw.ModelFirstUniform, model))
        return false;
    probe.BeginFrame(draw.Frame, draw.ViewToWorld);
    uint64_t key = draw.GeometryIdentity;
    for (const uint64_t value : draw.TemporalIdentity)
        key = ShadowCasterStabilityTrace::Mix(key ^ value);
    probe.Observe(key, postView, model);
    return true;
}

} // namespace Fast::Renderer3ds
