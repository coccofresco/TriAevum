#pragma once

#include "oot3d_top_screen_mod_profile.h"

namespace Oot3dNativeGame {

struct TopScreenDpadPresentationState {
  bool Child = false;
  std::uint16_t OwnedEquipment = 0;
  std::uint16_t EquippedEquipment = 0;
  std::uint8_t ItemZr = 0xFF;
  std::uint8_t ItemZl = 0xFF;
  std::uint8_t Ocarina = 0xFF;
  bool Boomerang = false;
  bool Slingshot = false;
  TopScreenNativeItemOpacity ItemOpacity;
};

bool ReadTopScreenDpadPresentationState(NativeA32Memory &memory,
                                        TopScreenDpadPresentationState *state);

// Emits mapped action icons and their native cycle marks. The cross, face buttons and global
// HUD scale remain owned by the existing HUD compositor.
std::size_t AppendTopScreenDpadPresentation(
    const TopScreenUiConfig &config,
    const TopScreenDpadPresentationState &state,
    const TopScreenAuxiliaryTouchInputs &viewState, float alpha,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

} // namespace Oot3dNativeGame
