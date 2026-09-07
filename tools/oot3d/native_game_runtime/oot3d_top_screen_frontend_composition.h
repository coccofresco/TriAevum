#pragma once

#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_top_screen_mod_profile.h"

#include <algorithm>

namespace Oot3dNativeGame {

// The mod swaps the two native targets. The desktop frontend instead retains
// the upper scene and draws the relocated menu over it, in native draw order.
inline TopScreenPauseTargetPlan ResolveTopScreenFrontendTargetCommand(
    const TopScreenPauseTargetCommand& command, bool frontendActive) noexcept {
    TopScreenPauseTargetPlan plan;
    if (!frontendActive || !command.RendererEnabled ||
        (command.NativeCommand != 0x400U && command.NativeCommand != 0x401U)) {
        return plan;
    }
    plan.Handled = true;
    plan.BindTopTarget = true;
    plan.StoredCommand = command.NativeCommand;
    plan.FramebufferBindingOffset = 0x38U;
    plan.ViewportWidth = command.HalfHeightMode ? 240U : 480U;
    plan.ViewportY = command.NativeCommand == 0x401U ? 40U : 0U;
    plan.ViewportHeight = command.NativeCommand == 0x401U ? 320U : 400U;
    return plan;
}

struct TopScreenFrontendCanvas {
    uint32_t ColorAddress = 0;
    Oot3dPicaViewportState Viewport;

    bool Matches(const Oot3dPicaDecodedDrawState& state) const noexcept {
        // A shared target does not imply a shared canvas: the upper scene's
        // clear must survive removal of the relocated lower menu's clear.
        return ColorAddress == state.Framebuffer.ColorPhysicalAddress &&
               Viewport.CornerX == state.Viewport.CornerX &&
               Viewport.CornerY == state.Viewport.CornerY &&
               Viewport.HalfWidth == state.Viewport.HalfWidth &&
               Viewport.HalfHeight == state.Viewport.HalfHeight;
    }
};

inline bool ComposeTopScreenFrontendFrame(Oot3dPicaVisualFrame& frame,
                                         bool frontendActive) {
    if (!frontendActive || frame.Draws.empty()) {
        return false;
    }
    uint64_t lastTopDraw = frame.TopTransfer.AfterDrawSubmissionId;
    for (const auto& draw : frame.Draws) {
        if (draw.State.Framebuffer.ColorPhysicalAddress ==
            frame.TopTransfer.InputPhysicalAddress) {
            lastTopDraw = std::max(lastTopDraw, draw.SubmissionId);
        }
    }
    if (lastTopDraw == frame.TopTransfer.AfterDrawSubmissionId) {
        return false;
    }
    // Only the host scanout is deferred until the relocated menu is complete.
    // Guest GSP commands, interrupts, memory and the other transfer stay native.
    for (auto& transfer : frame.DisplayTransfers) {
        if (transfer.CompletionId == frame.TopTransfer.CompletionId &&
            transfer.OutputPhysicalAddress == frame.TopTransfer.OutputPhysicalAddress &&
            transfer.InputPhysicalAddress == frame.TopTransfer.InputPhysicalAddress) {
            transfer.AfterDrawSubmissionId = lastTopDraw;
        }
    }
    frame.TopTransfer.AfterDrawSubmissionId = lastTopDraw;
    std::stable_sort(frame.DisplayTransfers.begin(), frame.DisplayTransfers.end(),
                     [](const auto& a, const auto& b) {
                         return a.AfterDrawSubmissionId < b.AfterDrawSubmissionId;
                     });
    return true;
}

} // namespace Oot3dNativeGame
