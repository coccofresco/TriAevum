#pragma once

namespace Fast {

enum class ApplicationMenu { Closed, Standard, Advanced };

// Event-driven (not frame-polled): a short host press cannot be lost between
// guest refreshes. OS key repeat never toggles the menu a second time.
constexpr ApplicationMenu RouteApplicationMenuKey(ApplicationMenu current,
                                                 ApplicationMenu requested,
                                                 bool pressed, bool repeat) {
    if (!pressed || repeat || requested == ApplicationMenu::Closed) return current;
    return current == requested ? ApplicationMenu::Closed : requested;
}

} // namespace Fast
