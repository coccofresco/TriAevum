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

struct TopScreenOcarinaRelocationStats {
    size_t DrawsRelocated = 0;
    size_t OpaqueDrawsSkipped = 0;
    size_t UntexturedDrawsSkipped = 0;
};

// The ocarina performance UI (staff, notes, song banner) is drawn only to the
// lower screen, which the single-screen layout never shows. While a
// performance is active, re-emit the lower target's alpha-blended, textured
// draws over the upper scene and defer the upper scanout past them. The lower
// canvas is 320 lines tall against the upper 400, so its viewport is shifted
// by 40 to center it, the same placement ResolveTopScreenFrontendTargetCommand
// gives the relocated frontend. The lower background fills opaquely
// (One/Zero) and is left behind; copying it would paint over the scene.
inline bool RelocateTopScreenOcarinaDraws(
    Oot3dPicaVisualFrame& frame, uint32_t lowerColorAddress,
    TopScreenOcarinaRelocationStats* stats = nullptr) {
    constexpr int16_t kLowerCanvasCenteringOffset = 40;
    const uint32_t upperColorAddress = frame.TopTransfer.InputPhysicalAddress;
    if (lowerColorAddress == 0U || upperColorAddress == 0U ||
        lowerColorAddress == upperColorAddress) {
        return false;
    }
    std::optional<Oot3dPicaFramebufferState> upperTarget;
    uint64_t lastSubmission = 0;
    for (const auto& draw : frame.Draws) {
        lastSubmission = std::max(lastSubmission, draw.SubmissionId);
        if (!upperTarget.has_value() &&
            draw.State.Framebuffer.ColorPhysicalAddress == upperColorAddress) {
            upperTarget = draw.State.Framebuffer;
        }
    }
    if (!upperTarget.has_value()) {
        return false;
    }
    TopScreenOcarinaRelocationStats result;
    const size_t originalCount = frame.Draws.size();
    for (size_t index = 0; index < originalCount; ++index) {
        const auto& draw = frame.Draws[index];
        if (draw.State.Framebuffer.ColorPhysicalAddress != lowerColorAddress) {
            continue;
        }
        const auto& blend = draw.State.OutputMerger.Blend;
        if (!blend.Enabled ||
            blend.DestinationColor == Oot3dPicaBlendFactor::Zero) {
            ++result.OpaqueDrawsSkipped;
            continue;
        }
        const bool textured = std::any_of(
            draw.Textures.begin(), draw.Textures.end(),
            [](const Oot3dPicaVulkanTextureBinding& texture) {
                return texture.State.Enabled;
            });
        if (!textured) {
            ++result.UntexturedDrawsSkipped;
            continue;
        }
        Oot3dPicaVulkanDrawPlan copy = draw;
        copy.SubmissionId = ++lastSubmission;
        copy.State.Framebuffer = *upperTarget;
        copy.State.Viewport.CornerY = static_cast<int16_t>(
            copy.State.Viewport.CornerY + kLowerCanvasCenteringOffset);
        // An overlay must not be occluded by the scene's depth buffer.
        copy.State.OutputMerger.Depth.TestEnabled = false;
        copy.State.OutputMerger.Depth.WriteEnabled = false;
        if (frame.StrictDrawIdentities.size() == frame.Draws.size()) {
            frame.StrictDrawIdentities.push_back(
                ComputeOot3dPicaVisualDrawIdentity(copy));
        }
        frame.Draws.push_back(std::move(copy));
        ++result.DrawsRelocated;
    }
    if (stats != nullptr) {
        *stats = result;
    }
    if (result.DrawsRelocated == 0U) {
        return false;
    }
    return ComposeTopScreenFrontendFrame(frame, true);
}

} // namespace Oot3dNativeGame
