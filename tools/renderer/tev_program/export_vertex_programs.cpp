// Developer-only SHBIN -> translated title source. Not an end-user Forge step.
#include "fast/renderer3ds/pica_vertex_program.h"
#include "video_core/shader/generator/glsl_shader_decompiler.h"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <iterator>
#include <vector>

using namespace Fast::Renderer3ds;

static std::string Translate(const Pica::ProgramCode& code, const Pica::SwizzleData& swizzles,
                             uint32_t entry) {
    return Pica::Shader::Generator::GLSL::DecompileProgram(code, swizzles, entry,
        [](uint32_t r) { return "pica_input" + std::to_string(r); },
        [](uint32_t r) { return "pica_output" + std::to_string(r); }, true);
}

static void Identity(std::ostream& out, PicaShaderSourceIdentity id) {
    out << '{' << id.Id << "ULL," << id.SecondaryHash << "ULL," << id.Size << "ULL}";
}

int main(int argc, char** argv) try {
    if (argc < 3) throw std::runtime_error("usage: export_vertex_programs output.h input.shbin [...]");
    std::ostringstream output;
    output << "// Generated translated OOT3D title shader source. Do not classify as title-neutral.\n"
              "// Translator: TriAevum / Citra-Azahar GLSL decompiler (GPL-2.0-or-later).\n"
              "#pragma once\n#include \"fast/renderer3ds/pica_vertex_program.h\"\n"
              "namespace Oot3dNativeGame {\ninline const Fast::Renderer3ds::PicaTranslatedVertexProgram kTranslatedVertexPrograms[] = {\n";
    size_t entries = 0;
    for (int arg = 2; arg < argc; ++arg) {
        std::ifstream input(argv[arg], std::ios::binary);
        if (!input) throw std::runtime_error("cannot open SHBIN");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto word = [&](size_t p) {
            if (p > bytes.size() || bytes.size() - p < 4) throw std::runtime_error("truncated SHBIN");
            return uint32_t(bytes[p]) | uint32_t(bytes[p+1]) << 8 |
                   uint32_t(bytes[p+2]) << 16 | uint32_t(bytes[p+3]) << 24;
        };
        if (word(0) != 0x424c5644) throw std::runtime_error("not DVLB");
        const size_t count = word(4);
        if (count > (bytes.size() - 8) / 4) throw std::runtime_error("invalid entry table");
        const size_t p = 8 + count * 4;
        if (word(p) != 0x504c5644) throw std::runtime_error("not DVLP");
        const size_t nc = word(p+12), ns = word(p+20);
        Pica::ProgramCode code{};
        Pica::SwizzleData swizzles{};
        if (!nc || !ns || nc > code.size() || ns > swizzles.size())
            throw std::runtime_error("invalid program capacity");
        for (size_t i = 0; i < nc; ++i) code[i] = word(p + word(p+8) + i*4);
        for (size_t i = 0; i < ns; ++i) swizzles[i] = word(p + word(p+16) + i*8);
        for (size_t i = 0; i < count; ++i) {
            const size_t d = word(8+i*4);
            if (word(d) != 0x454c5644 || ((word(d+4) >> 16) & 255) != 0)
                throw std::runtime_error("unsupported non-vertex DVLE");
            const auto entry = word(d+8);
            if (entry >= nc) throw std::runtime_error("invalid entry point");
            const auto body = Translate(code, swizzles, entry);
            if (body.empty() || body.find(")PICA_TITLE\"") != std::string::npos)
                throw std::runtime_error("translation failed");
            auto tailCode = code;
            auto tailSwizzles = swizzles;
            std::fill(tailCode.begin()+nc, tailCode.end(), 0x88000000U);
            std::fill(tailSwizzles.begin()+ns, tailSwizzles.end(), 0xffffffffU);
            if (Translate(tailCode, tailSwizzles, entry) != body)
                throw std::runtime_error("translation depends on data outside SHBIN");
            output << '{' << entry << "U,true,";
            Identity(output, IdentifyPicaProgramWords(std::span(code).first(nc)));
            output << ',';
            Identity(output, IdentifyPicaProgramWords(std::span(swizzles).first(ns)));
            output << ",R\"PICA_TITLE(" << body << ")PICA_TITLE\"},\n";
            ++entries;
        }
    }
    output << "};\n} // namespace Oot3dNativeGame\n";
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
    if (!file || !(file << output.str())) throw std::runtime_error("cannot write translated source");
    std::cout << "translated_vertex_entries=" << entries << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
