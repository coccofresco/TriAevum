#include "oot3d_native_a32_registry.h"

#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
#include "oot3d_a32_generated.h"
#endif

namespace Oot3dNativeGame {

const oot3d::recomp::a32::Registry& Oot3dNativeA32Registry() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
    return oot3d::recomp::GetA32GeneratedRegistry();
#else
    static constexpr oot3d::recomp::a32::Registry emptyRegistry{};
    return emptyRegistry;
#endif
}

void ConfigureOot3dNativeA32Candidates(
    std::span<const uint32_t> entryPoints) noexcept {
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
    oot3d::recomp::ConfigureA32GeneratedNativeCandidates(entryPoints);
#else
    static_cast<void>(entryPoints);
#endif
}

} // namespace Oot3dNativeGame
