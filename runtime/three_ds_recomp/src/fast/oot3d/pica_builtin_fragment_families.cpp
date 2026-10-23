#include "fast/oot3d/pica_builtin_fragment_families.h"
#include "fast/oot3d/pica_temporal_fragment_binaries.h"
#include "fast/oot3d/pica_toon_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"

namespace Fast::Oot3d {
std::span<const uint32_t> FindPicaBuiltInFragmentSpirv(
    const Renderer3ds::PicaShaderSourceIdentity& source, bool separateSamplers) {
    if (!source.Available()) return {};
    for (const auto family : {Renderer3ds::NativeFragmentArtifacts(), TemporalFragmentArtifacts(),
                              ToonFragmentArtifacts()})
        for (const auto& artifact : family)
            if (artifact.SeparateSamplers == separateSamplers && artifact.Source == source &&
                artifact.Spirv.size() >= 5 && artifact.Spirv[0] == 0x07230203U)
                return artifact.Spirv;
    return {};
}
} // namespace Fast::Oot3d
