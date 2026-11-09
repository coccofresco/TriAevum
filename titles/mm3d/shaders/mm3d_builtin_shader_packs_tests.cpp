// The Q-CAT-3 rule (parity with OoT3D): MM3D has prepared shaders exactly when
// both of its built-in packs are compiled into the build. Built twice: with
// fixture packs (EXPECT_PACKS=1) and with none (EXPECT_PACKS=0).
// `--negative` demands the opposite outcome and must exit 1.
#include "mm3d_builtin_shader_packs.h"
#include "mm3d_shader_preparation.h"
#include "fast/renderer/spirv_pack.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace Oot3dNativeGame;

static void Check(bool ok, const char* what) {
    if (!ok) { std::cerr << "mm3d_builtin_shader_packs_tests: " << what << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
    const bool negative = argc > 1 && std::strcmp(argv[1], "--negative") == 0;
    const bool expect = (EXPECT_PACKS != 0) != negative;
    Check(Mm3dBuiltInShaderPacksPresent() == expect, "prepared-shaders rule does not follow the built-in packs");
    if (expect) {
        const auto vertex = Fast::Renderer::ReadSpirvPack(Mm3dBuiltInVertexPack());
        const auto fragment = Fast::Renderer::ReadSpirvPack(Mm3dBuiltInFragmentPack());
        Check(!vertex.empty() && !fragment.empty(), "embedded packs do not parse");
        std::cout << "packs present: vertex entries=" << vertex.size() << " fragment entries=" << fragment.size() << '\n';
    } else {
        Check(Mm3dBuiltInVertexPack().empty() && Mm3dBuiltInFragmentPack().empty(), "absent build exposes pack bytes");
        std::cout << "packs absent: no prepared shaders\n";
    }
    // The pipeline cache namespace is one title fact: the session's constant
    // is the product fact the MM3D Forge passes to the device pipeline helper
    // (titles/mm3d/product.json, forge-catalog title.rs). Two spellings would
    // prepare a cache file the runtime never opens.
#if defined(TRIAEVUM_MM3D_PRODUCT_FACTS)
    {
        std::ifstream file(TRIAEVUM_MM3D_PRODUCT_FACTS, std::ios::binary);
        const std::string facts((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string fact = "\"pipeline_cache_namespace\": \"";
        fact += kMm3dPipelineCacheNamespace;
        fact += '"';
        Check(!facts.empty(), "titles/mm3d/product.json is unreadable");
        Check(facts.find(fact) != std::string::npos,
              "the session's pipeline cache namespace is not the product fact");
    }
#endif
    std::cout << "mm3d_builtin_shader_packs_tests: OK\n";
    return 0;
}
