#include "fast/renderer/shaderc_compiler.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer;
int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("triaevum-shaderc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory)) return 1;
    int result = 0;
    try {
        const auto& contract = ShadercCompilerContract();
        if (contract.empty()) throw std::runtime_error("compiler identity unavailable");
        SpirvCache cache;
        cache.Configure(directory, contract);
        const std::string source = "#version 450\nlayout(location=0) out vec4 color; void main(){color=vec4(0.5);}";
        const auto compile = [&] { return CompileShadercSpirv(source, SpirvStage::Fragment, "cache-test.frag"); };
        const auto expected = compile();
        if (cache.Resolve(source, SpirvStage::Fragment, compile) != expected)
            throw std::runtime_error("cold compiler output changed");
        cache.Configure(directory, contract);
        const auto actual = cache.Resolve(source, SpirvStage::Fragment, []() -> std::vector<uint32_t> {
            throw std::runtime_error("warm cache unexpectedly compiled");
        });
        if (actual != expected || cache.Stats().Hits != 1 || cache.Stats().Compilations != 0)
            throw std::runtime_error("warm compiler output changed");
        std::cout << "Real shaderc cold/warm parity: " << expected.size() << " words; " << contract << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(directory);
    return result;
}
