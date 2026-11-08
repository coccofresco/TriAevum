#include "mm3d_window_icon.h"

#include <cstdint>

namespace Oot3dNativeGame {
namespace {

// generate_branding.py --title mm3d writes the pixels from the MM3D game icon master.
constexpr uint8_t kMm3dWindowIconRgba[] = {
#include "../branding/mm3d_window_icon_rgba.inc"
};
constexpr uint32_t kMm3dWindowIconSize = 64;
static_assert(sizeof(kMm3dWindowIconRgba) == kMm3dWindowIconSize * kMm3dWindowIconSize * 4,
              "mm3d_window_icon_rgba.inc must hold 64x64 RGBA pixels");

} // namespace

const Fast::WindowIconImage& Mm3dWindowIcon() {
    static const Fast::WindowIconImage icon{kMm3dWindowIconSize, kMm3dWindowIconSize, kMm3dWindowIconRgba};
    return icon;
}

} // namespace Oot3dNativeGame
