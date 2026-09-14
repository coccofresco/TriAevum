#pragma once
#include "fast/renderer3ds/pica_shader_source_identity.h"
#include <array>
#include <compare>
#include <stdexcept>

namespace Fast::Renderer3ds {
// GPU program ownership follows effective source and interface, not material
// aliases. Canonical/instrumented domains remain in separate owning stores.
template<class OutputLayout>
struct PicaShaderModuleIdentity {
    std::array<uint64_t, 3> Vertex{}, Fragment{};
    uint32_t DescriptorSchema = 0;
    bool NriInterface = false;
    OutputLayout Outputs{};
    auto operator<=>(const PicaShaderModuleIdentity&) const = default;
};

template<class OutputLayout>
PicaShaderModuleIdentity<OutputLayout> BuildPicaShaderModuleIdentity(
    PicaShaderSourceIdentity vertex, PicaShaderSourceIdentity fragment,
    uint32_t descriptorSchema, bool nriInterface, OutputLayout outputs) {
    if (!vertex.Available() || !fragment.Available())
        throw std::invalid_argument("shader modules require effective source identities");
    return {{vertex.Id, vertex.SecondaryHash, vertex.Size},
            {fragment.Id, fragment.SecondaryHash, fragment.Size},
            descriptorSchema, nriInterface, outputs};
}
}
