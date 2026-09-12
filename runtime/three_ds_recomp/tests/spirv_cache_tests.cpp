#include "fast/renderer/spirv_cache.h"
#include "fast/renderer/cache_file.h"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace Fast::Renderer;
namespace {
void Check(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("SPIR-V cache assertion failed at line " + std::to_string(at.line()));
}
const std::vector<uint32_t> module{0x07230203, 0x00010300, 0, 1, 0};
auto Compile() { return module; }
auto MustNotCompile() -> std::vector<uint32_t> { throw std::runtime_error("unexpected compilation"); }
struct Directory {
    std::filesystem::path Path = std::filesystem::temp_directory_path() /
        ("triaevum-spirv-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { Check(std::filesystem::create_directory(Path)); }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(Path, ignored); }
};
std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void Put(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(bool(output));
}
void CheckInvalidated(SpirvCache& cache) {
    auto before = cache.Stats();
    Check(cache.Resolve("source", SpirvStage::Vertex, Compile) == module);
    Check(cache.Stats().Rejected == before.Rejected + 1 &&
          cache.Stats().Compilations == before.Compilations + 1);
    Check(cache.Resolve("source", SpirvStage::Vertex, MustNotCompile) == module);
}
}
int main() {
    const auto started = std::chrono::steady_clock::now();
    try {
        Directory root;
        SpirvCache cache;
        const auto directory = root.Path / "cache";
        cache.Configure(directory, "compiler/flags-v1");
        Check(cache.Resolve("source", SpirvStage::Vertex, Compile) == module);
        Check(cache.Stats().Compilations == 1 && cache.Stats().Writes == 1);
        // Immediate publication: a second reader does not need the first's shutdown.
        SpirvCache reader;
        reader.Configure(directory, "compiler/flags-v1");
        Check(reader.Resolve("source", SpirvStage::Vertex, MustNotCompile) == module);
        Check(reader.Stats().Hits == 1 && reader.Stats().Compilations == 0);
        auto path = std::filesystem::directory_iterator(directory / "spirv-v2")->path();
        const auto original = ReadFile(path);
        for (size_t length = 0; length < original.size(); ++length) {
            Put(path, std::span(original).first(length));
            CheckInvalidated(reader);
        }
        auto corrupt = original;
        corrupt.back() ^= 1;
        Put(path, corrupt);
        CheckInvalidated(reader);
        corrupt = original;
        for (size_t i = 20; i < 24; ++i) corrupt[i] = 255; // untrusted word count
        Put(path, corrupt);
        CheckInvalidated(reader);
        std::filesystem::resize_file(path, SpirvCache::MaximumEntryBytes + 1);
        CheckInvalidated(reader);
        corrupt = original;
        corrupt.push_back(0); // trailing bytes are not accepted
        Put(path, corrupt);
        CheckInvalidated(reader);
        // Even an internally consistent payload must match the exact source.
        corrupt = original;
        corrupt[40 + std::string("compiler/flags-v1").size()] ^= 1;
        uint64_t checksum = 14695981039346656037ULL;
        for (size_t i = 40; i < corrupt.size(); ++i) checksum = (checksum ^ corrupt[i]) * 1099511628211ULL;
        for (size_t i = 0; i < 8; ++i) corrupt[32 + i] = static_cast<uint8_t>(checksum >> (8 * i));
        Put(path, corrupt);
        CheckInvalidated(reader);
        Put(path.string() + ".interrupted.tmp", std::span(original).first(10));
        Check(reader.Resolve("source", SpirvStage::Vertex, MustNotCompile) == module);
        Check(reader.Resolve("changed source", SpirvStage::Vertex, Compile) == module);
        Check(reader.Resolve("source", SpirvStage::Fragment, Compile) == module);
        reader.Configure(directory, "compiler/flags-v2");
        Check(reader.Resolve("source", SpirvStage::Vertex, Compile) == module);
        Check(reader.Stats().Hits == 0 && reader.Stats().Compilations == 1);
        reader.Configure({}, "compiler/flags-v2");
        Check(!reader.Enabled());
        reader.Resolve("source", SpirvStage::Vertex, Compile);
        reader.Resolve("source", SpirvStage::Vertex, Compile);
        Check(reader.Stats().Compilations == 2 && reader.Stats().Writes == 0);
        reader.Configure(directory, "");
        Check(!reader.Enabled());
        const auto blocked = root.Path / "not-a-directory";
        Put(blocked, std::array<uint8_t, 1>{1});
        reader.Configure(blocked, "compiler");
        Check(reader.Resolve("source", SpirvStage::Vertex, Compile) == module);
        Check(reader.Stats().WriteFailures == 1 && !reader.LastWriteError().empty());
        reader.Configure(root.Path / "failed-compilation", "compiler");
        bool failed = false;
        try { reader.Resolve("source", SpirvStage::Vertex, MustNotCompile); }
        catch (const std::runtime_error&) { failed = true; }
        Check(failed && reader.Stats().CompilationFailures == 1 && reader.Stats().Writes == 0);
        failed = false;
        try { reader.Resolve("source", SpirvStage::Vertex, [] { return std::vector<uint32_t>{0x07230203}; }); }
        catch (const std::runtime_error&) { failed = true; }
        Check(failed && reader.Stats().CompilationFailures == 2);
        // Shared publication with concurrent writers/readers: no partial final file.
        const auto concurrent = root.Path / "concurrent.bin";
        const std::vector<uint8_t> first(65536, 17), second(65536, 29);
        Check(WriteCacheFileAtomically(concurrent, {}, first));
        std::atomic<bool> good = true;
        std::atomic<unsigned> writes = 0, busyWrites = 0;
        auto writer = [&](const auto& bytes) {
            for (int i = 0; i < 50; ++i) {
                if (WriteCacheFileAtomically(concurrent, {}, bytes)) ++writes;
                else ++busyWrites;
            }
        };
        std::thread a([&] { writer(first); }), b([&] { writer(second); });
        unsigned busyReads = 0;
        for (int i = 0; i < 100; ++i) {
            std::ifstream input(concurrent, std::ios::binary);
            if (!input) { ++busyReads; continue; }
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
            if (bytes != first && bytes != second) good = false;
        }
        a.join(); b.join();
        // Windows readers may deny replacement. Publication must not wait or
        // remove the previous entry; a busy writer is allowed to skip its cache.
        Check(good && writes + busyWrites == 100);
        Check(WriteCacheFileAtomically(concurrent, {}, second));
        Check(ReadFile(concurrent) == second);
        std::cout << "Concurrent publication: " << writes << " writes, " << busyWrites
                  << " busy/skipped writes, " << busyReads << " busy reads\n";
        std::cout << "SPIR-V cache: reuse, invalidation, bounds, interrupted writes and concurrent publication pass\n";
        std::cout << "Test body ms: " << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
