// Developer-only Linux LD_PRELOAD audit. Counts shaderc calls even when a pass
// bypasses the renderer cache; never ships with Forge or the game.
#include <shaderc/shaderc.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {
std::atomic<unsigned> calls{0};
std::atomic<unsigned long long> nanos{0};
__attribute__((destructor)) void Report() {
    std::fprintf(stderr, "TRIAEVUM_SHADERC_AUDIT calls=%u compile_ms=%.3f\n",
                 calls.load(), nanos.load() / 1e6);
}
}

extern "C" shaderc_compilation_result_t shaderc_compile_into_spv(
    const shaderc_compiler_t compiler, const char* source, size_t size,
    shaderc_shader_kind kind, const char* name, const char* entry,
    const shaderc_compile_options_t options) {
    using Function = decltype(&shaderc_compile_into_spv);
    static const auto real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "shaderc_compile_into_spv"));
    if (!real) std::abort();
    const auto start = std::chrono::steady_clock::now();
    const auto result = real(compiler, source, size, kind, name, entry, options);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    ++calls;
    nanos += static_cast<unsigned long long>(elapsed);
    std::fprintf(stderr, "TRIAEVUM_SHADERC_CALL name=%s stage=%d source_bytes=%zu compile_ms=%.3f\n",
                 name ? name : "(null)", static_cast<int>(kind), size, elapsed / 1e6);
    return result;
}
