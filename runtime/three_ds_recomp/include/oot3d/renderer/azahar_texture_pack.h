#pragma once

#include "fast/renderer3ds/pica_pbr_material.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3d::Renderer {

inline constexpr uint64_t kOot3dEuropeanTitleId = 0x0004000000033600ULL;

struct AzaharTexturePackConfiguration {
    bool DumpTextures = false;
    bool LoadCustomTextures = false;
    uint64_t TitleId = kOot3dEuropeanTitleId;
    std::filesystem::path UserDirectory;
    std::filesystem::path LoadDirectory;
    std::filesystem::path DumpDirectory;
    // Packs a title adapter installs beneath the user's pack, whatever
    // LoadCustomTextures says: the user's pack wins, then the earlier entry.
    std::vector<std::filesystem::path> BaseDirectories;

    bool operator==(const AzaharTexturePackConfiguration&) const = default;
};

struct AzaharTextureRequest {
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t NativeFormat = 0;
    uint32_t MipLevel = 0;
    std::span<const uint8_t> NativeBytes;
    std::span<const uint8_t> NativeRgba8;
};

struct AzaharTextureReplacement {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint64_t NativeHash = 0;
    uint64_t PixelContentHash = 0;
    uint64_t Generation = 0;
    std::shared_ptr<const std::vector<uint8_t>> Rgba8;
    std::filesystem::path SourcePath;
};

enum class AzaharTextureResolveState : uint8_t {
    Disabled = 0,
    Missing,
    Pending,
    Ready,
    Failed,
};

struct AzaharTextureResolveResult {
    AzaharTextureResolveState State = AzaharTextureResolveState::Disabled;
    uint64_t NativeHash = 0;
    std::shared_ptr<const AzaharTextureReplacement> Replacement;
    bool NativeHashAvailable = false;
    bool UseNewHash = true;
};

struct AzaharMaterialIdentity {
    uint64_t TitleId = 0;
    uint64_t NativeHash = 0;
    bool UseNewHash = true;

    bool operator==(const AzaharMaterialIdentity&) const = default;
};

struct AzaharPbrMaterial {
    AzaharMaterialIdentity Identity;
    uint64_t Generation = 0;
    Fast::Renderer3ds::PicaPbrMaterialParameters Parameters;
    std::array<std::shared_ptr<const AzaharTextureReplacement>, Fast::Renderer3ds::kPicaPbrTextureCount> Textures;
    std::shared_ptr<const AzaharTextureReplacement> ColorReplacement;
};
struct IndexedAzaharPbrMaterial {
    Fast::Renderer3ds::PicaPbrMaterialParameters Parameters;
    std::array<std::filesystem::path, Fast::Renderer3ds::kPicaPbrTextureCount> Textures;
    std::filesystem::path ConfigurationPath;
    std::filesystem::path BaseColorTexture;
    std::string Error;
};

struct AzaharMaterialCatalogEntry {
    uint64_t NativeHash = 0;
    IndexedAzaharPbrMaterial Definition;
};

struct AzaharImageLoadResult {
    std::shared_ptr<const AzaharTextureReplacement> Image;
    std::string Error;
};

struct AzaharMaterialParameterPreview {
    uint64_t NativeHash = 0;
    Fast::Renderer3ds::PicaPbrMaterialParameters Parameters;
    bool operator==(const AzaharMaterialParameterPreview&) const = default;
};

struct AzaharMaterialResolveResult {
    AzaharTextureResolveState State = AzaharTextureResolveState::Disabled;
    std::shared_ptr<const AzaharPbrMaterial> Material;
    std::optional<Fast::Renderer3ds::PicaPbrMaterialParameters> Preview;
};

struct AzaharTexturePackStatus {
    bool DumpTextures = false;
    bool LoadCustomTextures = false;
    bool PackConfigurationFound = false;
    bool PackUsesNewHash = true;
    uint64_t Generation = 1;
    size_t IndexedTextures = 0;
    size_t LoadedTextures = 0;
    size_t DumpedTextures = 0;
    size_t PendingDumps = 0;
    size_t PendingLoads = 0;
    size_t FailedLoads = 0;
    size_t UnsupportedTextureFiles = 0;
    bool IndexPending = false;
    size_t IndexedMaterials = 0;
    size_t LoadedMaterials = 0;
    size_t FailedMaterials = 0;
    std::filesystem::path LoadDirectory;
    std::filesystem::path DumpDirectory;
    std::string LastError;
};

// Azahar's modern custom-texture identity is CityHash64 over the untouched
// native PICA upload bytes.
[[nodiscard]] uint64_t AzaharCityHash64(std::span<const uint8_t> bytes) noexcept;

// Computes either Azahar's modern native-byte hash or its legacy detiled hash.
[[nodiscard]] uint64_t ComputeAzaharTextureHash(const AzaharTextureRequest& request, bool useNewHash,
                                                std::string* error = nullptr);

[[nodiscard]] std::string MakeAzaharTextureFilename(uint16_t width, uint16_t height, uint64_t hash,
                                                    uint8_t nativeFormat, uint32_t mipLevel);

// Uses the same lossless PNG encoder as native texture dumps.
void WriteAzaharTexturePng(const std::filesystem::path& path, uint32_t width,
                          uint32_t height, std::span<const uint8_t> rgba8);

class AzaharTexturePackRuntime final {
  public:
    AzaharTexturePackRuntime();
    ~AzaharTexturePackRuntime();

    AzaharTexturePackRuntime(const AzaharTexturePackRuntime&) = delete;
    AzaharTexturePackRuntime& operator=(const AzaharTexturePackRuntime&) = delete;

    static AzaharTexturePackRuntime& Instance();

    // Indexing and decoding run on the pack worker. Reload retains valid
    // resources until replacements are ready; generation changes are atomic.
    void Configure(AzaharTexturePackConfiguration configuration);
    // Owned by the title adapter, kept across Configure calls from settings.
    void SetBaseDirectories(std::vector<std::filesystem::path> directories);
    void Reload();

    [[nodiscard]] std::shared_ptr<const AzaharTextureReplacement>
    ResolveAndMaybeDump(const AzaharTextureRequest& request);
    // The renderer uses the non-blocking path: native OOT3D pixels remain
    // valid while a matching replacement is decoded by the pack worker.
    [[nodiscard]] AzaharTextureResolveResult
    ResolveOrQueue(const AzaharTextureRequest& request);
    [[nodiscard]] AzaharTextureResolveResult
    PollQueued(uint64_t nativeHash) const;
    [[nodiscard]] AzaharMaterialResolveResult
    ResolveMaterialOrQueue(uint64_t nativeHash, uint64_t expectedGeneration);
    void ReportMaterialUploadFailure(uint64_t nativeHash, uint64_t generation, std::string error);
    [[nodiscard]] AzaharTexturePackStatus Snapshot() const;
    [[nodiscard]] uint64_t Generation() const;
    [[nodiscard]] std::vector<AzaharMaterialCatalogEntry> MaterialCatalog() const;
    // Image imports share the existing pack worker; no file decoding in UI/frame code.
    [[nodiscard]] std::future<AzaharImageLoadResult> LoadEditorImage(std::filesystem::path path);
    void SetMaterialPreview(std::optional<AzaharMaterialParameterPreview> preview);
    [[nodiscard]] uint64_t MaterialRevision() const;
    // Diagnostics only for the selected live material preview; no saved state.
    void ReportMaterialSeams(uint64_t nativeHash, uint64_t generation, std::string_view status);
    [[nodiscard]] std::string MaterialSeamStatus(uint64_t nativeHash) const;

    // Intended for deterministic tests and orderly shutdown, not frame code.
    void WaitForPendingDumps();
    void WaitForPendingLoads();

  private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3d::Renderer
