#pragma once

#include "oot3d_demo_host_types.h"

namespace Fast {
class Fast3dWindow;
class GfxRenderingAPI;
} // namespace Fast

void InitContextForDemo(const Args& args);
void DestroyContextForDemo() noexcept;
// The session reached a clean end: the next launch is not a recovery.
void ClearLaunchMarkerForDemo() noexcept;
// Records an error that ends the runtime in its log file as well (release logs
// keep warnings and errors), when the host context has created its logger.
void LogFatalErrorForDemo(const char* message) noexcept;
Fast::Fast3dWindow& GetActiveFast3dWindowForDemo();
Fast::GfxRenderingAPI& GetActiveRenderingApiForDemo(Fast::Fast3dWindow& window);
