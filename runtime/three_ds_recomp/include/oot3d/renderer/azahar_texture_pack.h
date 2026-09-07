#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
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

class AzaharTexturePackRuntime final {
  public:
    AzaharTexturePackRuntime();
    ~AzaharTexturePackRuntime();

    AzaharTexturePackRuntime(const AzaharTexturePackRuntime&) = delete;
    AzaharTexturePackRuntime& operator=(const AzaharTexturePackRuntime&) = delete;

    static AzaharTexturePackRuntime& Instance();

    // Reconfiguration and reload invalidate only this module's decoded cache.
    // Generation lets a backend invalidate its own texture identity lazily.
    void Configure(AzaharTexturePackConfiguration configuration);
    void Reload();

    [[nodiscard]] std::shared_ptr<const AzaharTextureReplacement>
    ResolveAndMaybeDump(const AzaharTextureRequest& request);
    // The renderer uses the non-blocking path: native OOT3D pixels remain
    // valid while a matching replacement is decoded by the pack worker.
    [[nodiscard]] AzaharTextureResolveResult
    ResolveOrQueue(const AzaharTextureRequest& request);
    [[nodiscard]] AzaharTextureResolveResult
    PollQueued(uint64_t nativeHash) const;
    [[nodiscard]] AzaharTexturePackStatus Snapshot() const;
    [[nodiscard]] uint64_t Generation() const;

    // Intended for deterministic tests and orderly shutdown, not frame code.
    void WaitForPendingDumps();
    void WaitForPendingLoads();

  private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3d::Renderer
