#pragma once

#include "oot3d_demo_host_types.h"

#include <string>

namespace Fast {
class Fast3dWindow;
class GfxRenderingAPI;
} // namespace Fast

void InitContextForDemo(const Args& args);
void DestroyContextForDemo() noexcept;
// The session reached a clean end: the next launch is not a recovery.
void ClearLaunchMarkerForDemo() noexcept;
// Records an error that ends the runtime in its log file as well (release logs
// keep warnings and errors), when the host context has created its logger;
// whether it did.
bool LogFatalErrorForDemo(const char* message) noexcept;
// The runtime's log folder, absolute (call it before DestroyContextForDemo).
std::string FatalLogDirectoryForDemo();
// Shows the error that ends the runtime in a native dialog naming the log
// folder, unless a crash handler already showed one (desktop only).
void ShowFatalErrorForDemo(const std::string& title, const std::string& text) noexcept;
Fast::Fast3dWindow& GetActiveFast3dWindowForDemo();
Fast::GfxRenderingAPI& GetActiveRenderingApiForDemo(Fast::Fast3dWindow& window);
