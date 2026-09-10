#include "fast/renderer/shaderc_compiler.h"
#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif
#endif

namespace Fast::Renderer {
namespace {
bool ShaderTool(const std::filesystem::path& path) {
    const auto utf8 = path.filename().u8string();
    std::string name(utf8.begin(), utf8.end());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name.starts_with("libshaderc") || name.starts_with("shaderc") ||
           name.starts_with("libglslang") || name.starts_with("glslang") ||
           name.starts_with("libspirv") || name.starts_with("spirv");
}
std::filesystem::path DiskPath(std::filesystem::path path) {
#ifdef __ANDROID__
    // Android can map a native library directly from its installed APK.
    const auto text = path.generic_string();
    if (const auto zip = text.find("!/"); zip != std::string::npos) return text.substr(0, zip);
#endif
    return path;
}
std::string Fingerprint(const std::filesystem::path& path) {
    std::ifstream input(DiskPath(path), std::ios::binary);
    if (!input) return {};
    uint64_t first = 14695981039346656037ULL, second = 1099511628211ULL, count = 0;
    std::array<char, 65536> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        for (std::streamsize i = 0; i < input.gcount(); ++i) {
            const auto byte = static_cast<unsigned char>(buffer[i]);
            first = (first ^ byte) * 1099511628211ULL;
            second = (second + byte) * 0x9e3779b185ebca87ULL;
        }
        count += input.gcount();
    }
    if (!input.eof() || count == 0) return {};
    std::ostringstream out;
    out << std::hex << first << ':' << second << ':' << count;
    return out.str();
}
std::string IdentifyCompiler() {
    std::set<std::filesystem::path> paths;
#ifdef _WIN32
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&shaderc_compile_into_spv), &owner)) return {};
    const auto pathFor = [](HMODULE module) -> std::filesystem::path {
        std::array<wchar_t, 32768> path{};
        const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count == path.size()) return {};
        return std::wstring(path.data(), count);
    };
    paths.insert(pathFor(owner));
    std::vector<HMODULE> modules(1024);
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
            static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed) ||
        needed > modules.size() * sizeof(HMODULE)) return {};
    for (size_t i = 0; i < needed / sizeof(HMODULE); ++i) {
        auto path = pathFor(modules[i]);
        if (ShaderTool(path)) paths.insert(std::move(path));
    }
#else
    Dl_info owner{};
    if (!dladdr(reinterpret_cast<const void*>(&shaderc_compile_into_spv), &owner) || !owner.dli_fname) return {};
    paths.insert(owner.dli_fname);
#ifdef __APPLE__
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        if (const auto* name = _dyld_get_image_name(i); name && ShaderTool(name)) paths.insert(name);
    }
#else
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* context) {
        if (info->dlpi_name && ShaderTool(info->dlpi_name))
            static_cast<std::set<std::filesystem::path>*>(context)->insert(info->dlpi_name);
        return 0;
    }, &paths);
#endif
#endif
    std::set<std::string> fingerprints;
    std::set<std::filesystem::path> files;
    for (const auto& path : paths) files.insert(DiskPath(path));
    for (const auto& path : files) {
        auto fingerprint = Fingerprint(path);
        if (fingerprint.empty()) return {};
        fingerprints.insert(std::move(fingerprint));
    }
    // Keep this option contract beside the only compiler invocation below.
    std::string contract = "shaderc/vulkan1.1/performance/main/v1";
    for (const auto& fingerprint : fingerprints) contract += "/" + fingerprint;
    return contract;
}
}

const std::string& ShadercCompilerContract() {
    static const std::string contract = [] {
        try { return IdentifyCompiler(); }
        catch (const std::exception&) { return std::string{}; }
    }();
    return contract;
}

std::vector<uint32_t> CompileShadercSpirv(std::string_view source, SpirvStage stage, const char* name) {
    shaderc_shader_kind kind;
    switch (stage) {
        case SpirvStage::Vertex: kind = shaderc_vertex_shader; break;
        case SpirvStage::Fragment: kind = shaderc_fragment_shader; break;
        case SpirvStage::Compute: kind = shaderc_compute_shader; break;
        default: throw std::invalid_argument("unsupported shader stage");
    }
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(source.data(), source.size(), kind, name, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(std::string("shaderc failed for ") + name + ": " + result.GetErrorMessage());
    return {result.cbegin(), result.cend()};
}
}
