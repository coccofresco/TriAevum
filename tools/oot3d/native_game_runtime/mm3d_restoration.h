#pragma once

#include "mm3d_guest_calls.h"
#include "mm3d_guest_patches.h"
#include "session.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    // Restoration's hooks that call guest functions run through calls,
    // which must outlive this.
    explicit Mm3dRestorationPort(Mm3dGuestCalls& calls);
    ~Mm3dRestorationPort();
    Mm3dRestorationPort(const Mm3dRestorationPort&) = delete;
    Mm3dRestorationPort& operator=(const Mm3dRestorationPort&) = delete;

    void Register(Mm3dGuestPatches& patches);
    // The data patches, after the image is mounted; returns how many applied
    // (one whose original byte differs is left alone).
    std::size_t ApplyDataPatches(oot3d::recomp::a32::MemoryBus& memory);
    void SetDekuiBridge(Mm3dRestoration::DekuiBridge bridge);
    bool SetMessageEdits(std::vector<Mm3dRestoration::MessageEdit> edits);
    // Message edits as tools/mm3d/dekui/setup/prepare_message_edits.py
    // writes them (triaevum_mm3d_message_edits_v1: the user's own text and
    // dekui's wording, kept on the user's machine); nullopt and error set if
    // the document is malformed. SetMessageEdits validates their hashes.
    static std::optional<std::vector<Mm3dRestoration::MessageEdit>> ReadMessageEdits(
        std::string_view json, std::string* error = nullptr);
    Mm3dRestoration::PortCapabilities Capabilities(oot3d::recomp::a32::MemoryBus& memory) const;
    static std::vector<std::uint32_t> RegistryBoundaries();
    // Dynamic equivalent of donor s_hud.heart_group, zero before HUD Init.
    std::uint32_t HudHeartWidget() const noexcept;

  private:
    Mm3dGuestTask Run(Mm3dGuestCallContext& context, std::string_view action);

    Mm3dGuestCalls& mCalls;
    Mm3dGuestPatches* mPatches = nullptr; // Register's registry must outlive this.
    std::unique_ptr<Mm3dRestoration::Session> mSession;
};

} // namespace Oot3dNativeGame
