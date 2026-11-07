#pragma once

#include "fast/renderer/pass_shader_artifact.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Fast::Renderer {
// Optional publisher-built title programs. Own the aligned bytes for the full
// lifetime of artifact views. This never compiles a shader or replaces an
// embedded artifact: callers append these entries after their legacy entries.
class TitleProgramPack {
public:
    explicit TitleProgramPack(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto length = file.tellg();
        if (!file || length < 16 || length > 32 * 1024 * 1024 || length % 4 != 0)
            throw std::runtime_error("invalid title program pack extent: " + path.string());
        mWords.resize(static_cast<size_t>(length) / 4);
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(mWords.data()), length))
            throw std::runtime_error("cannot read title program pack: " + path.string());
        std::string error;
        mEntries = ReadSpirvPack({reinterpret_cast<const unsigned char*>(mWords.data()),
                                 mWords.size() * 4}, &error);
        if (!error.empty() || mEntries.empty() || mEntries.size() > 4096)
            throw std::runtime_error("invalid title program pack: " + error);
        for (const auto& entry : mEntries) {
            if ((entry.Kind != SpirvPackKind::PicaVertex && entry.Kind != SpirvPackKind::Pass) ||
                (entry.Kind == SpirvPackKind::Pass &&
                 (entry.Stage != static_cast<uint32_t>(SpirvStage::Vertex) || entry.Source.empty())) ||
                (entry.Kind == SpirvPackKind::PicaVertex && entry.Identity[2] == 0) ||
                entry.Spirv.size() < 5 || entry.Spirv[0] != 0x07230203U)
                throw std::runtime_error("invalid title program entry");
            bool vertex = false;
            for (size_t cursor = 5; cursor < entry.Spirv.size();) {
                const uint32_t instruction = entry.Spirv[cursor];
                const size_t count = instruction >> 16;
                if (!count || count > entry.Spirv.size() - cursor)
                    throw std::runtime_error("invalid title program instruction extent");
                if ((instruction & 65535U) == 15U) {
                    if (count < 4 || entry.Spirv[cursor + 1] != 0)
                        throw std::runtime_error("title program is not a vertex stage");
                    vertex = true;
                }
                cursor += count;
            }
            if (!vertex) throw std::runtime_error("title program has no vertex entry point");
        }
    }
    TitleProgramPack(const TitleProgramPack&) = delete;
    TitleProgramPack& operator=(const TitleProgramPack&) = delete;
    std::span<const SpirvPackEntry> Entries() const { return mEntries; }
private:
    std::vector<uint32_t> mWords;
    std::vector<SpirvPackEntry> mEntries;
};
} // namespace Fast::Renderer
