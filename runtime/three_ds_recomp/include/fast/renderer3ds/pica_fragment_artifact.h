#pragma once

#include "pica_shader_source_identity.h"
#include <span>

namespace Fast::Renderer3ds {

struct PicaFragmentArtifact {
    PicaShaderSourceIdentity Source;
    bool SeparateSamplers;
    std::span<const uint32_t> Spirv;
};

inline std::span<const uint32_t> FindPicaFragmentArtifact(
    std::span<const PicaFragmentArtifact> artifacts, std::string_view source,
    bool separateSamplers) {
    const auto identity = IdentifyPicaShaderSource(source);
    if (!identity.Available()) return {};
    for (const auto& artifact : artifacts) {
        if (artifact.SeparateSamplers == separateSamplers && artifact.Source == identity &&
            artifact.Spirv.size() >= 5 && artifact.Spirv[0] == 0x07230203U)
            return artifact.Spirv;
    }
    return {};
}

} // namespace Fast::Renderer3ds
