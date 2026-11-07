#pragma once
namespace Oot3dNativeGame {
// Resolved renderer/window settings, never independent title preferences.
struct TitlePresentationSettings {
  float FovMultiplier=1.0F;
  float HorizontalExpansion=1.0F;
  float VerticalExpansion=1.0F;
  bool ExtendedDrawDistance=false;
};
}
