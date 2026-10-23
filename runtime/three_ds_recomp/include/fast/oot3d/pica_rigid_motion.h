#pragma once

#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Fast::Oot3d {

struct RigidMotionSample {
    std::array<float, 2> MotionUv{};
    bool Valid = false;
};

[[nodiscard]] bool DecodeCommonRigidOriginClip(
    std::span<const uint8_t> vertexUniformBytes, bool framebufferFlipped,
    std::array<float, 4>& originClip);

class PicaRigidMotionTracker final {
  public:
    [[nodiscard]] RigidMotionSample Track(
        uint64_t instanceId, uint64_t frameId,
        const std::array<float, 4>& originClip,
        const std::array<float, 2>& jitterUv);
    void Reset();
    void PruneBeforeFrame(uint64_t frameId);

  private:
    struct Record {
        uint64_t FrameId = 0;
        std::array<float, 2> ScreenUv{};
        bool Valid = false;
    };
    std::unordered_map<uint64_t, Record> mRecords;
};

// Rigid-motion guide writers (RG = previous-current jittered UV, B =
// Renderer3ds::PicaMotionProvenance). They read the adapter's Oot3dDrawState
// push constants and the temporal clip varyings; the shared renderer owns only
// the provenance values and the consumer policy (PicaMotionGuideResolve).
inline constexpr std::string_view PicaMotionFallbackAssignment =
    "    pica_rigid_motion_guide = vec4(oot3d_draw.rigid_motion.xyz, 0.0);\n";

// Shared by standalone, composed and parametric fragment programs. Push Z is
// fallback provenance (0/1/2); push W requests exact evaluation. A bad exact
// clip is INVALID, never a silent rigid/camera substitution.
inline constexpr std::string_view PicaMotionGuideAssignment = R"glsl(
    vec4 oot3d_motion = vec4(oot3d_draw.rigid_motion.xyz, 0.0);
    if (oot3d_draw.rigid_motion.a > 0.5) {
        oot3d_motion = vec4(0.0);
        if (!any(isnan(oot3d_current_clip)) && !any(isinf(oot3d_current_clip)) &&
            !any(isnan(oot3d_previous_clip)) && !any(isinf(oot3d_previous_clip)) &&
            oot3d_current_clip.w > 1.0e-7 && oot3d_previous_clip.w > 1.0e-7) {
            vec2 oot3d_current_uv = oot3d_current_clip.xy / oot3d_current_clip.w * 0.5 + 0.5;
            vec2 oot3d_previous_uv = oot3d_previous_clip.xy / oot3d_previous_clip.w * 0.5 + 0.5;
            vec2 oot3d_delta = oot3d_previous_uv - oot3d_current_uv;
            if (!any(isnan(oot3d_delta)) && !any(isinf(oot3d_delta)) &&
                all(greaterThanEqual(oot3d_previous_uv, vec2(0.0))) &&
                all(lessThanEqual(oot3d_previous_uv, vec2(1.0))))
                oot3d_motion = vec4(oot3d_delta, 3.0, 0.0);
        }
    }
    pica_rigid_motion_guide = oot3d_motion;
)glsl";

struct PicaRigidMotionShaderVariant {
    std::string Source;
    uint64_t FragmentKey = 0;
    bool Applied = false;
    bool UsedProvidedProgram = false;
    bool TemporalApplied = false;
    bool SurfaceApplied = false;
};

[[nodiscard]] PicaRigidMotionShaderVariant
BuildPicaVertexInstrumentation(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView& program,
    bool temporal, bool surface);

// Legacy compatibility/test API. Production instrumentation must use the
// typed frontend program overload above and may not analyze generated GLSL.
[[nodiscard]] PicaRigidMotionShaderVariant BuildPicaTemporalVertexVariant(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView* program =
        nullptr);

// Legacy compatibility/test API retained for source-equivalence coverage.
[[nodiscard]] PicaRigidMotionShaderVariant BuildPicaRigidMotionShaderVariant(
    std::string_view source, uint64_t fragmentKey);

} // namespace Fast::Oot3d
