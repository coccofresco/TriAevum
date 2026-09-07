#pragma once

#include <cstdint>
#include <filesystem>

#include "fast/backends/gfx_rendering_api.h"
#include "oot3d_demo_host_types.h"

namespace Fast::Renderer3ds { struct PicaFrameTemporalSample; }

struct FramebufferScreenshotState {
    bool Written = false;
    uint32_t WrittenCount = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    int LastGrassDiagnosticStatus = -1;
    uint64_t LastTemporalDiagnosticEpoch = 0;
    uint32_t TemporalDiagnosticFrames = 0;
};

bool ShouldWriteFramebufferScreenshot(const Args& args, uint32_t frameCount,
                                      const FramebufferScreenshotState& state);
void MaybeWriteFramebufferScreenshot(const Args& args, Fast::GfxRenderingAPI& api, uint32_t width, uint32_t height,
                                     uint32_t frameCount, FramebufferScreenshotState& state,
                                     const Fast::Renderer3ds::PicaFrameTemporalSample* temporalSample = nullptr);
