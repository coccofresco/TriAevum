#include "oot3d_top_screen_frontend_composition.h"

#include <cassert>
#include <iostream>

using namespace Oot3dNativeGame;

int main() {
    for (const bool half : {false, true}) {
        const auto scene = ResolveTopScreenFrontendTargetCommand({true, 0x400, half}, true);
        const auto menu = ResolveTopScreenFrontendTargetCommand({true, 0x401, half}, true);
        assert(scene.BindTopTarget && menu.BindTopTarget);
        assert(scene.FramebufferBindingOffset == 0x38 && menu.FramebufferBindingOffset == 0x38);
        assert(scene.ViewportWidth == (half ? 240U : 480U));
        assert(scene.ViewportY == 0 && scene.ViewportHeight == 400);
        assert(menu.ViewportWidth == scene.ViewportWidth);
        assert(menu.ViewportY == 40 && menu.ViewportHeight == 320);
        assert(!ResolveTopScreenFrontendTargetCommand({true, 0x400, half}, false).Handled);
        assert(!ResolveTopScreenFrontendTargetCommand({false, 0x401, half}, true).Handled);
        assert(!ResolveTopScreenFrontendTargetCommand({true, 0x410, half}, true).Handled);
    }
    Oot3dPicaDecodedDrawState state{};
    state.Framebuffer.ColorPhysicalAddress = 100;
    state.Viewport.HalfWidth = 240;
    state.Viewport.HalfHeight = 160;
    state.Viewport.CornerY = 40;
    const TopScreenFrontendCanvas menuCanvas{100, state.Viewport};
    assert(menuCanvas.Matches(state));
    state.Viewport.CornerY = 0;
    state.Viewport.HalfHeight = 200;
    assert(!menuCanvas.Matches(state));
    state.Viewport = menuCanvas.Viewport;
    state.Framebuffer.ColorPhysicalAddress = 200;
    assert(!menuCanvas.Matches(state));

    Oot3dPicaVisualFrame frame;
    frame.TopTransfer.CompletionId = 7;
    frame.TopTransfer.InputPhysicalAddress = 100;
    frame.TopTransfer.OutputPhysicalAddress = 300;
    frame.TopTransfer.AfterDrawSubmissionId = 10;
    frame.DisplayTransfers.push_back(frame.TopTransfer);
    auto other = frame.TopTransfer;
    other.CompletionId = 8;
    other.InputPhysicalAddress = 200;
    other.OutputPhysicalAddress = 400;
    other.AfterDrawSubmissionId = 15;
    frame.DisplayTransfers.push_back(other);
    for (auto [id, address] : {std::pair{10U, 100U}, {20U, 100U}, {30U, 200U}}) {
        Oot3dPicaVulkanDrawPlan draw;
        draw.SubmissionId = id;
        draw.State.Framebuffer.ColorPhysicalAddress = address;
        frame.Draws.push_back(std::move(draw));
    }
    assert(!ComposeTopScreenFrontendFrame(frame, false));
    assert(frame.TopTransfer.AfterDrawSubmissionId == 10);
    assert(ComposeTopScreenFrontendFrame(frame, true));
    assert(frame.TopTransfer.AfterDrawSubmissionId == 20);
    assert(frame.DisplayTransfers[0].CompletionId == 8);
    assert(frame.DisplayTransfers[0].AfterDrawSubmissionId == 15);
    assert(frame.DisplayTransfers[1].CompletionId == 7);
    assert(frame.DisplayTransfers[1].AfterDrawSubmissionId == 20);
    assert(frame.Draws[0].SubmissionId == 10 && frame.Draws[2].SubmissionId == 30);
    assert(!ComposeTopScreenFrontendFrame(frame, true));
    frame.DisplayTransfers.clear();
    frame.TopTransfer.AfterDrawSubmissionId = 10;
    assert(ComposeTopScreenFrontendFrame(frame, true));
    assert(frame.TopTransfer.AfterDrawSubmissionId == 20);
    frame.Draws.clear();
    assert(!ComposeTopScreenFrontendFrame(frame, true));
    std::cout << "TopScreen frontend composition tests passed\n";
}
