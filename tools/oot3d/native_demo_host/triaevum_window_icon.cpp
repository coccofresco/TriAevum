#include "triaevum_window_icon.h"

#include <cstdint>

namespace {

// generate_branding.py writes the pixels from the game icon master.
constexpr uint8_t kWindowIconRgba[] = {
#include "triaevum_window_icon_rgba.inc"
};
constexpr uint32_t kWindowIconSize = 64;
static_assert(sizeof(kWindowIconRgba) == kWindowIconSize * kWindowIconSize * 4,
              "triaevum_window_icon_rgba.inc must hold 64x64 RGBA pixels");

} // namespace

const Fast::WindowIconImage& TriAevumWindowIcon() {
    static const Fast::WindowIconImage icon{kWindowIconSize, kWindowIconSize, kWindowIconRgba};
    return icon;
}
