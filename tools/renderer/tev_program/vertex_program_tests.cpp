#include "fast/renderer3ds/pica_vertex_program.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_vertex_artifact.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;
static void Check(bool ok) { if (!ok) throw std::runtime_error("vertex program contract"); }
int main() try {
    std::array<uint32_t, 5> binary{0x07230203U, 0x00010300, 0, 1, 0};
    std::array<PicaFragmentArtifact, 1> artifacts{{{IdentifyPicaShaderSource("fragment"), true, binary}}};
    Check(FindPicaFragmentArtifact(artifacts, "fragment", true).size() == 5);
    Check(FindPicaFragmentArtifact(artifacts, "fragment", false).empty());
    Check(FindPicaFragmentArtifact(artifacts, "fragment ", true).empty());
    Check(FindPicaFragmentArtifact(artifacts, "", true).empty());
    binary[0] = 0;
    Check(FindPicaFragmentArtifact(artifacts, "fragment", true).empty());
    binary[0] = 0x07230203U;
    artifacts[0].Spirv = std::span(binary).first(4);
    Check(FindPicaFragmentArtifact(artifacts, "fragment", true).empty());
    const std::array<PicaVertexArtifact,1> vertexArtifacts{{{IdentifyPicaShaderSource("vertex"), binary}}};
    const auto vertexArtifact = FindPicaVertexArtifact(vertexArtifacts,"vertex");
    Check(vertexArtifact.Matches("vertex"));
    Check(!vertexArtifact.Matches("instrumented vertex"));
    Check(!FindPicaVertexArtifact(vertexArtifacts,"different outputs").Matches("different outputs"));
    Check(!PicaVertexArtifact{}.Matches("vertex"));
    std::array<uint32_t, 4> code{0x12345678, 0x88000000, 0, 0};
    std::array<uint32_t, 2> swizzles{0x1b1b1b1b, 0};
    PicaTranslatedVertexProgram p{0, true, IdentifyPicaProgramWords(std::span(code).first(2)),
                                 IdentifyPicaProgramWords(std::span(swizzles).first(1)), "body"};
    Check(p.Matches(code, swizzles, 0, true));
    Check(!p.Matches(code, swizzles, 1, true));
    Check(!p.Matches(code, swizzles, 0, false));
    Check(!p.Matches(std::span(code).first(1), swizzles, 0, true));
    Check(!p.Matches(code, {}, 0, true));
    for (unsigned bit = 0; bit < 32; ++bit) {
        code[0] ^= 1U << bit;
        Check(!p.Matches(code, swizzles, 0, true));
        code[0] ^= 1U << bit;
        swizzles[0] ^= 1U << bit;
        Check(!p.Matches(code, swizzles, 0, true));
        swizzles[0] ^= 1U << bit;
    }
    code[3] = 0xffffffff;
    swizzles[1] = 0xffffffff;
    Check(p.Matches(code, swizzles, 0, true));
    const char expected[] = {0x78, 0x56, 0x34, 0x12};
    Check(IdentifyPicaProgramWords(std::span(code).first(1)) ==
          IdentifyPicaShaderSource(std::string_view(expected, 4)));
    p.Instructions.Size += 1;
    Check(!p.Matches(code, swizzles, 0, true));
    p.Instructions.Size -= 1;
    p.Body = {};
    Check(!p.Matches(code, swizzles, 0, true));
    std::cout << "vertex program contract: passed\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
