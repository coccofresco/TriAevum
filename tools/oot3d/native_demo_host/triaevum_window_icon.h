#pragma once

#include "fast/WindowIcon.h"

// TriAevum's game icon for the window (tools/triaevum_release/branding):
// 64x64 pixels compiled in, so no file lookup can fail.
const Fast::WindowIconImage& TriAevumWindowIcon();

// The window icon of the active title: its own image (TitleSession::WindowIcon,
// MM3D's from titles/mm3d/branding) or, for a title without one (OoT3D), the
// game icon above.
const Fast::WindowIconImage& ResolveWindowIcon(const Fast::WindowIconImage* titleIcon);
