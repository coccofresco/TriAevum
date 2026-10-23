#pragma once

#include <string_view>

namespace Fast::Renderer3ds {

// Rigid-motion attachment: RG = previous-current jittered UV, B = provenance.
// A remains the transparent-depth guide used by outlines. Cleared B=0 rejects
// history; camera fallback requires explicit stationary-surface evidence.
enum class PicaMotionProvenance { Invalid = 0, Camera = 1, Rigid = 2, Exact = 3 };

[[nodiscard]] constexpr PicaMotionProvenance ResolvePicaMotionProvenance(
    bool exact, bool rigid, bool cameraOnly) {
    return exact ? PicaMotionProvenance::Exact : rigid ? PicaMotionProvenance::Rigid
        : cameraOnly ? PicaMotionProvenance::Camera : PicaMotionProvenance::Invalid;
}

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

// Shared consumer policy; a missing object history must override even a valid
// camera reconstruction and reach both TAA's disocclusion and vendor masks.
inline constexpr std::string_view PicaMotionGuideResolve = R"glsl(
    if (rigid.b == 2.0 || rigid.b == 3.0) {
        if (!any(isnan(rigid.rg)) && !any(isinf(rigid.rg))) {
            motion = rigid.rg;
            disocclusion = 0.0;
        } else {
            motion = vec2(0.0); disocclusion = 1.0; reactive = 1.0;
        }
    } else if (rigid.b != 1.0) {
        motion = vec2(0.0); disocclusion = 1.0; reactive = 1.0;
    }
)glsl";

} // namespace Fast::Renderer3ds
