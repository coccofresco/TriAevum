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

// What the host supplies (UX plan WP-05C): the installed game's program ID
// (Azahar names texture folders by it; a USA installation is 0004000000033500)
// and the managed default folders, D/textures/{load,dump}/<title id>. They
// rank below OOT3D_AZAHAR_TITLE_ID / _LOAD_DIRECTORY / _DUMP_DIRECTORY, the
// F1 folders and a named Azahar user folder (UserDirectory or
// OOT3D_AZAHAR_USER_DIRECTORY). An existing pack in Azahar's own folder keeps
// loading from there only while the managed load folder holds no regular file
// (recursively; empty subfolders count as empty). Once the managed folder
// holds a pack (Forge > Enhancements or F1's Import... put one there), the
// managed folder wins (UX plan WP-07). Azahar's folder is never modified.
struct AzaharTextureHostDefaults {
    std::optional<uint64_t> TitleId;
    std::filesystem::path LoadDirectory;
    std::filesystem::path DumpDirectory;
    // The game settings file's folder (issue #124). A relative F1 folder is
    // read from it, and F1's Browse... stores a folder inside the portable
    // installation relative to it (fast/extension/portable_path_setting.h).
    // Empty: a relative F1 folder is read from the working folder, as before.
    std::filesystem::path SettingsDirectory;

    bool operator==(const AzaharTextureHostDefaults&) const = default;
};

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
    // Owned by the host (SetHostDefaults), kept across Configure calls.
    AzaharTextureHostDefaults HostDefaults;

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
    // Levels 1.. of the replacement's mip chain, tightly packed (each half
    // the previous, at least 1x1), built on the loader thread: a picture
    // larger than the native texture it replaces is sampled minified, and
    // without levels it shimmers and its cut-out edges (grass) break up
    // (issue #122). MipLevels counts the base level; 1 means none.
    std::shared_ptr<const std::vector<uint8_t>> MipsRgba8;
    uint32_t MipLevels = 1;
    std::filesystem::path SourcePath;
};

// The levels below a w x h RGBA8 image (gamma-correct, alpha-weighted 2x2
// box filter). A cut-out image (alpha nearly all 0 or 255) keeps its
// alpha-tested coverage on every level. `levels` receives the level count
// including the base.
[[nodiscard]] std::vector<uint8_t> BuildAzaharReplacementMipChain(
    std::span<const uint8_t> rgba8, uint32_t width, uint32_t height, uint32_t& levels);

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
    // The title the folders are named by (OOT3D_AZAHAR_TITLE_ID, then the
    // installed title the host names, then the configuration's).
    uint64_t TitleId = kOot3dEuropeanTitleId;
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
    // Owned by the window host, kept across Configure calls from settings.
    void SetHostDefaults(AzaharTextureHostDefaults defaults);
    // The window host's defaults as last set (folders lexically normalized);
    // F1's Import... copies into HostDefaults().LoadDirectory and is
    // unavailable while it is empty (a launch without Forge's profile).
    [[nodiscard]] AzaharTextureHostDefaults HostDefaults() const;
    // Resolves the folders again (a pack imported into the managed folder
    // takes over from Azahar's) and re-indexes the pack.
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
