#pragma once

#include "oot3d_demo_host_types.h"

namespace Fast {
class Fast3dWindow;
class GfxRenderingAPI;
} // namespace Fast

void InitContextForDemo(const Args& args);
void DestroyContextForDemo() noexcept;
Fast::Fast3dWindow& GetActiveFast3dWindowForDemo();
Fast::GfxRenderingAPI& GetActiveRenderingApiForDemo(Fast::Fast3dWindow& window);
