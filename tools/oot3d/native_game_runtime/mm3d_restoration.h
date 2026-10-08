#pragma once

#include "mm3d_guest_patches.h"

#include <memory>

namespace Mm3dRestoration {
class Input;
}

namespace Oot3dNativeGame {

// Project Restoration (leoetlino/project-restoration 8bb2a7b8), the base of
// mm3d-dekui, ported as source onto the vanilla v1.1 code.bin
// (tools/mm3d/restoration). Each hook of its hooks/*.hks becomes a guest
// patch site at the translated v1.1 address, guarded by the v1.1 word:
// - softbranch (Magikoopa saves r0-r12/lr, calls the function, then runs the
//   displaced instruction): the site runs the function and declines, so the
//   guest's own instruction follows;
// - branch to a trampoline: the site does what the trampoline leaves in the
//   registers or flags and continues after the instruction;
// - code patch: the patched word is executed in place of the guest's;
// - data patch: written once the image is mounted.
class Mm3dRestorationPort {
  public:
    Mm3dRestorationPort();
    ~Mm3dRestorationPort();
    Mm3dRestorationPort(const Mm3dRestorationPort&) = delete;
    Mm3dRestorationPort& operator=(const Mm3dRestorationPort&) = delete;

    void Register(Mm3dGuestPatches& patches);
    // The data patches, after the image is mounted; returns how many applied
    // (one whose original byte differs is left alone).
    std::size_t ApplyDataPatches(oot3d::recomp::a32::MemoryBus& memory);

  private:
    std::unique_ptr<Mm3dRestoration::Input> mInput;
    // rst::Context::is_swimming (set by the Zora swimming port; false until then).
    bool mIsSwimming = false;
};

} // namespace Oot3dNativeGame
