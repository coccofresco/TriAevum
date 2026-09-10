#pragma once

#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_top_screen_mod_profile.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

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
    bool BrowserVisible = false;
    int CursorTile = -1; // selected song tile while the browser is visible
};

// The ocarina performance UI is drawn only to the lower screen, which the
// single-screen layout never shows. While a performance is active, re-emit
// its overlay quads over the upper scene the way the TopScreen mod lays them
// out: the note staff along the bottom edge and the song-name banner along
// the top edge, nothing else.
//
// Everything is authored on the 320x240 lower canvas through a fixed ortho
// projection in the vertex-shader constants: clip.x = c0.w - y/120 and
// clip.y = 1 - x/160. A band of quads is moved vertically by re-emitting the
// draw with a filtered index buffer and a different c0.w; the vertex streams
// stay shared with the source draw, which matters because those buffers are
// refreshed in place until the draw executes. The copy takes the upper
// target's full viewport, shifted 40 columns to center the 320-column canvas
// in 400.
//
// The 512x256 ocarina-page batch has fixed quad slots: 0-2 the browser's
// staff strip, 35-37 its song-name banner, 39-44 the note glyphs on the
// staff, 51-54 the free-play page's message/notes bar. The remaining slots
// (song grid, cursor, note buttons, ocarina art, BACK and list icons) stay
// hidden; the opaque backdrop (One/Zero blend) and other page art are not
// copied. Song-name and message text arrive in separate glyph draws and
// follow their band. Canvas-top content (y < 120) moves to the screen bottom
// and canvas-bottom content to the screen top.
inline bool RelocateTopScreenOcarinaDraws(
    Oot3dPicaVisualFrame& frame, uint32_t lowerColorAddress,
    TopScreenOcarinaRelocationStats* stats = nullptr) {
    constexpr int16_t kLowerCanvasCenteringOffset = 40;
    constexpr float kCanvasHalfHeight = 120.0F;
    constexpr float kStaffShift = 168.0F;   // y 4..68 -> 172..236
    constexpr float kBannerShift = -196.0F; // y 200..240 -> 4..44
    constexpr uint32_t kOcarinaPageWidth = 512;
    constexpr uint32_t kOcarinaPageHeight = 256;
    constexpr size_t kOcarinaPageQuads = 108;
    constexpr size_t kBannerQuad = 35;
    constexpr size_t kCursorQuad = 18;
    constexpr uint32_t kGlyphAtlasSize = 256;
    constexpr float kGlyphMaxSize = 32.0F;
    constexpr std::array<size_t, 13> kStaffQuads{0,  1,  2,  39, 40, 41, 42,
                                                 43, 44, 51, 52, 53, 54};
    constexpr std::array<size_t, 3> kBannerQuads{35, 36, 37};
    const uint32_t upperColorAddress = frame.TopTransfer.InputPhysicalAddress;
    if (lowerColorAddress == 0U || upperColorAddress == 0U ||
        lowerColorAddress == upperColorAddress) {
        return false;
    }
    std::optional<Oot3dPicaFramebufferState> upperTarget;
    std::optional<Oot3dPicaViewportState> upperViewport;
    uint64_t lastSubmission = 0;
    for (const auto& draw : frame.Draws) {
        lastSubmission = std::max(lastSubmission, draw.SubmissionId);
        if (!upperTarget.has_value() &&
            draw.State.Framebuffer.ColorPhysicalAddress == upperColorAddress) {
            upperTarget = draw.State.Framebuffer;
            upperViewport = draw.State.Viewport;
        }
    }
    if (!upperTarget.has_value()) {
        return false;
    }
    TopScreenOcarinaRelocationStats result;
    const auto emit = [&](const Oot3dPicaVulkanDrawPlan& draw,
                          std::span<const size_t> quads, float shift,
                          uint64_t salt) {
        const auto indices = draw.ResolvedIndexBytes();
        Oot3dPicaVulkanDrawPlan copy = draw;
        if (!quads.empty()) {
            copy.IndexBytes.clear();
            copy.IndexBytes.reserve(quads.size() * 12U);
            for (const size_t quad : quads) {
                copy.IndexBytes.insert(copy.IndexBytes.end(),
                                       indices.begin() + quad * 12U,
                                       indices.begin() + quad * 12U + 12U);
            }
            copy.SharedIndexBytes.reset();
            copy.VertexCount = static_cast<uint32_t>(copy.IndexBytes.size() / 2U);
        }
        copy.VertexShader.Uniforms.Floats[0][3] -= shift / kCanvasHalfHeight;
        // A distinct identity keeps the relocated copy out of the source
        // draw's geometry cache entry while still tracking its content.
        copy.GeometryIdentity = draw.GeometryIdentity ^ salt;
        copy.SubmissionId = ++lastSubmission;
        copy.State.Framebuffer = *upperTarget;
        if (upperViewport.has_value()) {
            copy.State.Viewport.HalfWidth = upperViewport->HalfWidth;
            copy.State.Viewport.CornerX = upperViewport->CornerX;
        }
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
    };
    const size_t originalCount = frame.Draws.size();
    for (size_t index = 0; index < originalCount; ++index) {
        // Copies are appended, which may reallocate; re-resolve each pass.
        const Oot3dPicaVulkanDrawPlan draw = frame.Draws[index];
        if (draw.State.Framebuffer.ColorPhysicalAddress != lowerColorAddress) {
            continue;
        }
        const auto& blend = draw.State.OutputMerger.Blend;
        const bool opaqueFill =
            !blend.Enabled ||
            blend.DestinationColor == Oot3dPicaBlendFactor::Zero;
        const bool textured = std::any_of(
            draw.Textures.begin(), draw.Textures.end(),
            [](const Oot3dPicaVulkanTextureBinding& texture) {
                return texture.State.Enabled;
            });
        if (!textured || !draw.Indexed || !draw.IndicesAre16Bit ||
            draw.VertexBindings.empty() ||
            draw.VertexBindings[0].ByteStride < 8U) {
            ++result.UntexturedDrawsSkipped;
            continue;
        }
        const auto indices = draw.ResolvedIndexBytes();
        const auto positions = draw.VertexBindings[0].ResolvedBytes();
        const size_t stride = draw.VertexBindings[0].ByteStride;
        const bool ocarinaPage =
            draw.Textures[0].State.Width == kOcarinaPageWidth &&
            draw.Textures[0].State.Height == kOcarinaPageHeight &&
            indices.size() == kOcarinaPageQuads * 12U;
        if (ocarinaPage) {
            const auto quadOrigin = [&](size_t quad, float* x, float* y) {
                const uint16_t vertex = static_cast<uint16_t>(
                    indices[quad * 12U] | (indices[quad * 12U + 1U] << 8));
                float xy[2];
                std::memcpy(xy, positions.data() + vertex * stride, sizeof(xy));
                *x = xy[0];
                *y = xy[1];
            };
            float bannerX = 0.0F;
            float bannerY = 0.0F;
            quadOrigin(kBannerQuad, &bannerX, &bannerY);
            result.BrowserVisible = result.BrowserVisible || bannerX < 320.0F;
            // The cursor quad sits on the selected song tile: four columns by
            // three rows of 72x44 cells from (20,72).
            float cursorX = 0.0F;
            float cursorY = 0.0F;
            quadOrigin(kCursorQuad, &cursorX, &cursorY);
            if (result.BrowserVisible && cursorX >= 0.0F && cursorX < 320.0F) {
                const int column = static_cast<int>((cursorX - 20.0F + 36.0F) / 72.0F);
                const int row = static_cast<int>((cursorY - 72.0F + 22.0F) / 44.0F);
                if (column >= 0 && column < 4 && row >= 0 && row < 3) {
                    result.CursorTile = row * 4 + column;
                }
            }
            emit(draw, kStaffQuads, kStaffShift, 0x4F43415249'4E4131ULL);
            emit(draw, kBannerQuads, kBannerShift, 0x4F43415249'4E4132ULL);
            continue;
        }
        // Song-name and message text: small glyph quads from the font atlas.
        // Anything larger is page art (backdrop, grid trim).
        bool glyphs = draw.Textures[0].State.Width == kGlyphAtlasSize &&
                      draw.Textures[0].State.Height == kGlyphAtlasSize;
        float textMinY = 1e9F;
        for (size_t quad = 0; glyphs && quad * 12U + 12U <= indices.size(); ++quad) {
            float minX = 1e9F, maxX = -1e9F, minY = 1e9F, maxY = -1e9F;
            for (size_t k = 0; k < 6U; ++k) {
                const uint16_t vertex = static_cast<uint16_t>(
                    indices[quad * 12U + k * 2U] |
                    (indices[quad * 12U + k * 2U + 1U] << 8));
                float xy[2];
                std::memcpy(xy, positions.data() + vertex * stride, sizeof(xy));
                minX = std::min(minX, xy[0]);
                maxX = std::max(maxX, xy[0]);
                minY = std::min(minY, xy[1]);
                maxY = std::max(maxY, xy[1]);
            }
            glyphs = maxX - minX <= kGlyphMaxSize && maxY - minY <= kGlyphMaxSize;
            textMinY = std::min(textMinY, minY);
        }
        if (!glyphs) {
            if (opaqueFill) {
                ++result.OpaqueDrawsSkipped;
            } else {
                ++result.UntexturedDrawsSkipped;
            }
            continue;
        }
        emit(draw, {}, textMinY < kCanvasHalfHeight ? kStaffShift : kBannerShift,
             0x4F43415249'4E4133ULL);
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
