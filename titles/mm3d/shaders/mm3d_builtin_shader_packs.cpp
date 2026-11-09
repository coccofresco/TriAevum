// MM3D built-in shader packs: see mm3d_builtin_shader_packs.h.
#include "mm3d_builtin_shader_packs.h"
#include "fast/renderer/spirv_pack.h"

namespace Oot3dNativeGame {
#if TRIAEVUM_MM3D_BUILTIN_SHADER_PACKS
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
alignas(16) const unsigned char kVertexPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed TRIAEVUM_MM3D_VERTEX_PACK
#else
#include "mm3d_native_vertex.spvpack.inc"
#endif
};
alignas(16) const unsigned char kFragmentPack[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed TRIAEVUM_MM3D_FRAGMENT_PACK
#else
#include "mm3d_native_fragment.spvpack.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace
std::span<const unsigned char> Mm3dBuiltInVertexPack() { return kVertexPack; }
std::span<const unsigned char> Mm3dBuiltInFragmentPack() { return kFragmentPack; }
#else
std::span<const unsigned char> Mm3dBuiltInVertexPack() { return {}; }
std::span<const unsigned char> Mm3dBuiltInFragmentPack() { return {}; }
#endif
#if TRIAEVUM_MM3D_STARTUP_UBER_MODULES
namespace {
TRIAEVUM_SPIRV_PACK_EMBED_BEGIN
const unsigned char kStartupUberModules[] = {
#if TRIAEVUM_SPIRV_PACK_EMBED
#embed TRIAEVUM_MM3D_STARTUP_UBER_MODULES_FILE
#else
#include "mm3d_startup_uber_modules.json.inc"
#endif
};
TRIAEVUM_SPIRV_PACK_EMBED_END
} // namespace
std::span<const unsigned char> Mm3dStartupUberModulesData() { return kStartupUberModules; }
#else
std::span<const unsigned char> Mm3dStartupUberModulesData() { return {}; }
#endif
} // namespace Oot3dNativeGame
