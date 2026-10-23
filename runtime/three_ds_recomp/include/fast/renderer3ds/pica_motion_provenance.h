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

// The guide writers live with the title adapter that declares the draw push
// constants and clip varyings they read (the adapter's pica_rigid_motion.h).

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
