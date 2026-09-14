#pragma once
#include "pica_shader_source_identity.h"
#include <span>

namespace Fast::Renderer3ds {
// Immutable binary published by a title adapter. The owner must outlive every
// submitted view. Source identity prevents reuse after vertex instrumentation.
struct PicaVertexArtifact {
    PicaShaderSourceIdentity Source;
    std::span<const uint32_t> Spirv;
    bool Matches(std::string_view source) const {
        return Source.Available() && Spirv.size() >= 5 && Spirv[0] == 0x07230203U &&
               Source == IdentifyPicaShaderSource(source);
    }
};
inline PicaVertexArtifact FindPicaVertexArtifact(std::span<const PicaVertexArtifact> artifacts,
                                                std::string_view source) {
    const auto id = IdentifyPicaShaderSource(source);
    for (const auto& artifact : artifacts)
        if (artifact.Source.Available() && artifact.Source == id && artifact.Spirv.size() >= 5 &&
            artifact.Spirv[0] == 0x07230203U) return artifact;
    return {};
}
} // namespace Fast::Renderer3ds
