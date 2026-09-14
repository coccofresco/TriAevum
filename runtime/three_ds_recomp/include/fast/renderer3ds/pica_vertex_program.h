#pragma once

#include "pica_shader_source_identity.h"
#include <span>
#include <string>

namespace Fast::Renderer3ds {

inline PicaShaderSourceIdentity IdentifyPicaProgramWords(std::span<const uint32_t> words) {
    std::string bytes;
    bytes.reserve(words.size() * 4);
    for (uint32_t word : words)
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<char>(word >> shift));
    return IdentifyPicaShaderSource(bytes);
}

// Title-owned translated code; no uniforms, output mapping or host policy.
struct PicaTranslatedVertexProgram {
    uint32_t Entry;
    bool AccurateMultiplication;
    PicaShaderSourceIdentity Instructions;
    PicaShaderSourceIdentity Swizzles;
    std::string_view Body;

    bool Matches(std::span<const uint32_t> instructions,
                 std::span<const uint32_t> swizzles, uint32_t entry,
                 bool accurateMultiplication) const {
        if (entry != Entry || accurateMultiplication != AccurateMultiplication ||
            Body.empty() || !Instructions.Available() || !Swizzles.Available() ||
            Instructions.Size % 4 != 0 || Swizzles.Size % 4 != 0 ||
            Instructions.Size / 4 > instructions.size() || Swizzles.Size / 4 > swizzles.size())
            return false;
        // Exporters must prove the translated entry does not read the unused tail.
        return IdentifyPicaProgramWords(instructions.first(Instructions.Size / 4)) == Instructions &&
               IdentifyPicaProgramWords(swizzles.first(Swizzles.Size / 4)) == Swizzles;
    }
};

} // namespace Fast::Renderer3ds
