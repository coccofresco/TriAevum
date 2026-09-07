#pragma once

#include "fast/renderer3ds/pica_render_backend.h"

#include <cstdio>
#include <cstdlib>

namespace Fast::Oot3d {

// Bounded, opt-in trace of the effective backend draws, including visual replays.
inline void TraceOutlineDraw(uint64_t frame, const Renderer3ds::PicaDrawView& draw,
                             bool perspective, bool writesGuide) {
    static const uint64_t first = [] {
        const char* value = std::getenv("TRIAEVUM_OUTLINE_TRACE_FIRST");
        return value ? std::strtoull(value, nullptr, 10) : UINT64_MAX;
    }();
    static const uint64_t last = [] {
        const char* value = std::getenv("TRIAEVUM_OUTLINE_TRACE_LAST");
        return value ? std::strtoull(value, nullptr, 10) : 0;
    }();
    if (frame < first || frame > last) return;
    std::fprintf(stderr,
        "OUTLINE_DRAW frame=%llu id=%llu target=%08x/%08x domain=%u layer=%u "
        "depth=%u/%u/%u blend=%u/%u/%u alpha=%u/%u/%u rgb=%u perspective=%u guide=%u "
        "vertices=%u viewport=%.1f,%.1f,%.1f,%.1f scissor=%u,%u,%u,%u shader=%llx\n",
        static_cast<unsigned long long>(frame), static_cast<unsigned long long>(draw.SubmissionId),
        draw.FramebufferColorPhysicalAddress, draw.FramebufferDepthPhysicalAddress,
        static_cast<unsigned>(draw.CompositionDomain), static_cast<unsigned>(draw.Composition.Layer),
        draw.DepthTestEnabled, draw.DepthWriteEnabled, static_cast<unsigned>(draw.DepthCompare),
        draw.Blend.Enabled, static_cast<unsigned>(draw.Blend.SourceRgb), static_cast<unsigned>(draw.Blend.DestRgb),
        draw.AlphaTestEnabled, static_cast<unsigned>(draw.AlphaCompare), draw.AlphaReference, draw.ColorWriteMask,
        perspective, writesGuide, draw.VertexCount, draw.ViewportX, draw.ViewportY,
        draw.ViewportWidth, draw.ViewportHeight, draw.ScissorX1, draw.ScissorY1, draw.ScissorX2, draw.ScissorY2,
        static_cast<unsigned long long>(draw.FragmentShaderKey));
}

} // namespace Fast::Oot3d
