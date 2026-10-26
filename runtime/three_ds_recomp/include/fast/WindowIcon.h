#pragma once

#include <cstdint>

namespace Fast {

// The window's icon, supplied by the product before the window opens
// (GfxWindowBackend::ConfigureWindowIcon). 8-bit RGBA, rows top to bottom,
// straight alpha; the pixels outlive the window (static product data). The
// window host shows it where the platform takes a window icon from the
// window itself (X11, macOS); a Windows executable's icon resource already
// gives its windows every icon size. It never reaches the framebuffer.
struct WindowIconImage {
    uint32_t Width = 0;
    uint32_t Height = 0;
    const uint8_t* Rgba = nullptr;
};

} // namespace Fast
