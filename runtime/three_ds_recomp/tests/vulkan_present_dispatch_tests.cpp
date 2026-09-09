#include "fast/oot3d/vulkan_adapter_policy.h"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace Fast::Oot3d;
    using Mode = VulkanPresentDispatchMode;
    try {
        Require(ParseVulkanPresentDispatchMode("") == Mode::Automatic, "default parser");
        Require(ParseVulkanPresentDispatchMode("auto") == Mode::Automatic, "auto parser");
        Require(ParseVulkanPresentDispatchMode("async") == Mode::Asynchronous, "async parser");
        Require(ParseVulkanPresentDispatchMode("inline") == Mode::Inline, "inline parser");
        Require(ParseVulkanPresentDispatchMode("graphics") == Mode::GraphicsQueue, "graphics parser");
        bool rejected = false;
        try { (void)ParseVulkanPresentDispatchMode("unsupported"); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "invalid mode must not silently change presentation");

        const auto automatic = ResolveVulkanQueueTopology(3, 3, 2);
        Require(automatic.AsynchronousPresent && automatic.PresentQueueIndex == 1 &&
                    automatic.RequestedGraphicsQueueCount == 2, "default behavior changed");
        const auto inlinePresent = ResolveVulkanQueueTopology(3, 3, 2, Mode::Inline);
        Require(!inlinePresent.AsynchronousPresent && inlinePresent.PresentQueueIndex == 1 &&
                    inlinePresent.RequestedGraphicsQueueCount == 2, "inline must only change dispatch");
        const auto graphics = ResolveVulkanQueueTopology(3, 3, 2, Mode::GraphicsQueue);
        Require(!graphics.AsynchronousPresent && graphics.PresentQueueIndex == 0 &&
                    graphics.RequestedGraphicsQueueCount == 1, "shared queue selection");

        const auto wayland = ResolveVulkanQueueTopology(3, 3, 2, Mode::Automatic, "wayland");
        Require(!wayland.AsynchronousPresent && wayland.RequestedGraphicsQueueCount == 1 &&
                    wayland.PresentQueueIndex == 0, "Wayland must default to the shared queue");
        for (const auto driver : {"x11", "windows", "cocoa", "android", ""}) {
            const auto other = ResolveVulkanQueueTopology(3, 3, 2, Mode::Automatic, driver);
            Require(other.AsynchronousPresent && other.RequestedGraphicsQueueCount == 2,
                    "unrelated surface policy changed");
        }
        const auto waylandAsync = ResolveVulkanQueueTopology(3, 3, 2, Mode::Asynchronous, "wayland");
        Require(waylandAsync.AsynchronousPresent && waylandAsync.PresentQueueIndex == 1,
                "explicit diagnostic async override");
        const auto waylandInline = ResolveVulkanQueueTopology(3, 3, 2, Mode::Inline, "wayland");
        Require(!waylandInline.AsynchronousPresent && waylandInline.PresentQueueIndex == 1,
                "explicit diagnostic inline override");
        const auto waylandSeparate = ResolveVulkanQueueTopology(1, 4, 8, Mode::Automatic, "wayland");
        Require(!waylandSeparate.SharedFamily && !waylandSeparate.AsynchronousPresent &&
                    waylandSeparate.PresentFamily == 4 && !waylandSeparate.NriSwapchainEligible,
                "Wayland must retain a required separate present family");

        for (const auto mode : {Mode::Automatic, Mode::Asynchronous, Mode::Inline, Mode::GraphicsQueue}) {
            const auto single = ResolveVulkanQueueTopology(2, 2, 1, mode);
            Require(!single.AsynchronousPresent && single.PresentQueueIndex == 0 &&
                        single.RequestedGraphicsQueueCount == 1, "single queue device");
            const auto separate = ResolveVulkanQueueTopology(1, 4, 8, mode);
            Require(separate.GraphicsFamily == 1 && separate.PresentFamily == 4 &&
                        !separate.SharedFamily && !separate.NriSwapchainEligible &&
                        separate.RequestedGraphicsQueueCount == 1 && separate.PresentQueueIndex == 0,
                    "must preserve required separate presentation family");
            Require(separate.AsynchronousPresent == (mode == Mode::Automatic || mode == Mode::Asynchronous),
                    "separate family dispatch");
        }
        std::cout << "Vulkan presentation dispatch tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
