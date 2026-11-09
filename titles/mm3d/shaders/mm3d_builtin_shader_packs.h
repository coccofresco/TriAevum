#pragma once
// MM3D's built-in shader packs (PARITY_PLAN P7.1), the same mechanism as
// OoT3D's: a translated vertex-program family and a fragment-family pack
// compiled into the runtime. They are produced by the CATALOGS lane at
//   titles/mm3d/shaders/mm3d_native_vertex.spvpack
//   titles/mm3d/shaders/mm3d_native_fragment.spvpack
// and embedded exactly when BOTH exist at configure time
// (CMake/oot3d/game.cmake, TRIAEVUM_MM3D_BUILTIN_SHADER_PACKS). There is no
// product/developer switch and no runtime pack-identity switch (OoT3D has
// none): a build without the packs prepares no shaders for MM3D.
#include <span>

namespace Oot3dNativeGame {
// Empty when this build has no MM3D packs.
std::span<const unsigned char> Mm3dBuiltInVertexPack();
std::span<const unsigned char> Mm3dBuiltInFragmentPack();
// The startup uber module list (mm3d_startup_uber_modules.json, embedded when
// it exists at configure time; see mm3d_startup_uber_modules.h). Empty: none.
std::span<const unsigned char> Mm3dStartupUberModulesData();
// Both packs are built in and non-empty: MM3D has prepared shaders.
[[nodiscard]] inline bool Mm3dBuiltInShaderPacksPresent() {
  return !Mm3dBuiltInVertexPack().empty() && !Mm3dBuiltInFragmentPack().empty();
}
} // namespace Oot3dNativeGame
