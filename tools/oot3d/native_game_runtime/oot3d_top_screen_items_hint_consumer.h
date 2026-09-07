#pragma once

#include "oot3d_top_screen_items_hint.h"

#include <string>

namespace Oot3dNativeGame {

class NativeA32Memory;

// Application-side adapter for the original OoT3D Items owner. It reproduces
// the selected-model lookup and four-quad bounds scan used by the 2.1.1
// payload, then exposes only typed cursor geometry to the UI policy.
bool ReadTopScreenItemsHintCursorBounds(const NativeA32Memory &memory,
                                        TopScreenItemsHintCursorBounds *bounds,
                                        std::string *error = nullptr);

} // namespace Oot3dNativeGame
