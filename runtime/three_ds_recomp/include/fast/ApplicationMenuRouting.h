#pragma once

namespace Fast {

enum class ApplicationMenu { Closed, Standard, Advanced };

class ApplicationInputReleaseGate {
  public:
    bool Update(bool captured, bool neutral) {
        if (captured) mBlocked = true;
        else if (neutral) mBlocked = false;
        return mBlocked;
    }
    bool Blocked() const { return mBlocked; }
  private:
    bool mBlocked = false;
};

// Event-driven (not frame-polled): a short host press cannot be lost between
// guest refreshes. OS key repeat never toggles the menu a second time.
constexpr ApplicationMenu RouteApplicationMenuKey(ApplicationMenu current,
                                                 ApplicationMenu requested,
                                                 bool pressed, bool repeat) {
    if (!pressed || repeat || requested == ApplicationMenu::Closed) return current;
    return current == requested ? ApplicationMenu::Closed : requested;
}

} // namespace Fast
