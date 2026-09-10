#pragma once

#include "oot3d/renderer/pica_shader_source_identity.h"

#include <cstdint>
#include <compare>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kPicaAotShaderPackVersion = 1U;

enum class PicaAotShaderStage : uint32_t {
    Vertex = 1U,
    Fragment = 2U,
    NriFragment = 3U,
};

struct PicaAotShaderSourceIdentity {
    uint64_t Id = 0;
    uint64_t SecondaryHash = 0;
    uint64_t Size = 0;

    auto operator<=>(const PicaAotShaderSourceIdentity&) const = default;
};

struct PicaAotShaderBinary {
    PicaAotShaderStage Stage = PicaAotShaderStage::Vertex;
    PicaAotShaderSourceIdentity Source;
    std::vector<uint32_t> Spirv;
};

[[nodiscard]] PicaAotShaderSourceIdentity
IdentifyPicaAotShaderSource(std::string_view source) noexcept;
[[nodiscard]] std::string FormatPicaAotShaderId(uint64_t id);
[[nodiscard]] std::string_view PicaAotShaderStageName(
    PicaAotShaderStage stage) noexcept;
[[nodiscard]] bool ParsePicaAotShaderStage(
    std::string_view name, PicaAotShaderStage& stage) noexcept;

bool WritePicaAotShaderPack(
    const std::filesystem::path& path,
    uint32_t descriptorSchemaVersion,
    std::span<const PicaAotShaderBinary> shaders,
    std::string* error = nullptr);

class PicaAotShaderPack final {
  public:
    bool Load(const std::filesystem::path& path,
              std::string* error = nullptr);
    void Clear();

    [[nodiscard]] std::span<const uint32_t> Find(
        PicaAotShaderStage stage, std::string_view source) const noexcept;
    [[nodiscard]] std::span<const uint32_t> Find(
        PicaAotShaderStage stage,
        const PicaAotShaderSourceIdentity& source) const noexcept;
    [[nodiscard]] bool Loaded() const noexcept;
    [[nodiscard]] uint32_t DescriptorSchemaVersion() const noexcept;
    [[nodiscard]] size_t EntryCount() const noexcept;
    // Copies every module; for merging into a rewritten pack at shutdown.
    [[nodiscard]] std::vector<PicaAotShaderBinary> Binaries() const;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

  private:
    struct Key {
        PicaAotShaderStage Stage = PicaAotShaderStage::Vertex;
        PicaAotShaderSourceIdentity Source;

        auto operator<=>(const Key&) const = default;
    };
    struct Range {
        size_t WordOffset = 0;
        size_t WordCount = 0;
    };

    std::filesystem::path mPath;
    uint32_t mDescriptorSchemaVersion = 0;
    std::vector<uint32_t> mWords;
    std::map<Key, Range> mEntries;
};

// Diagnostic collector used only to discover the finite set of post-variant
// GLSL modules that must be compiled into a release pack.
class PicaEffectiveShaderInventory final {
  public:
    bool Configure(const std::filesystem::path& path,
                   std::string* error = nullptr);
    void Observe(PicaAotShaderStage stage, std::string_view source,
                 uint32_t descriptorSchemaVersion,
                 uint64_t canonicalPipelineId,
                 uint64_t effectiveShaderKey,
                 uint64_t settingsRevision);
    bool Finish(std::string* error = nullptr);
    void Clear();

    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] size_t EntryCount() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

  private:
    struct Entry {
        PicaAotShaderStage Stage = PicaAotShaderStage::Vertex;
        PicaAotShaderSourceIdentity Identity;
        std::string Source;
        uint64_t ObservationCount = 0;
        std::set<uint64_t> CanonicalPipelineIds;
        std::set<uint64_t> EffectiveShaderKeys;
        std::set<uint64_t> SettingsRevisions;
    };

    std::filesystem::path mPath;
    std::map<std::pair<PicaAotShaderStage, uint64_t>, Entry> mEntries;
    uint32_t mDescriptorSchemaVersion = 0;
    bool mDescriptorSchemaMismatch = false;
    bool mFinished = false;
};

} // namespace Fast::Oot3d
