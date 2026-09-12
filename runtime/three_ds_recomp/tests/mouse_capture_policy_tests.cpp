#include "../include/fast/MouseCapturePolicy.h"
#include <stdexcept>

int main() {
    const auto require = [](bool value) { if (!value) throw std::runtime_error("mouse capture policy regression"); };
    Fast::MouseCapturePolicy policy;
    require(policy.Request(true));
    policy.Release();
    require(policy.Released() && !policy.Request(true));
    require(!policy.Request(true)); // Later polls cannot undo Escape.
    require(!policy.ResumeClick(true)); // F1 interaction cannot recapture.
    require(!policy.Request(false));
    require(!policy.ResumeClick(false)); // Native touch/menu owns the pointer.
    require(!policy.Request(true));
    require(policy.ResumeClick(false));
    require(!policy.Released() && policy.Request(true));
    require(policy.ResumeClickHeld()); // Do not fire a guest action on recapture.
    require(policy.ConsumeClickRelease() && !policy.ResumeClickHeld());
    require(!policy.ConsumeClickRelease());
    policy.Release();
    policy.Release(); // Key repeat remains a release, never a toggle.
    require(!policy.Request(true));
}
