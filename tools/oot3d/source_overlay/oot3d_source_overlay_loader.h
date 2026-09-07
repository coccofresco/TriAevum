#pragma once

#include "oot3d_source_overlay_abi.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

class Oot3dSourceOverlayModule final {
  public:
    Oot3dSourceOverlayModule() = default;
    ~Oot3dSourceOverlayModule();

    Oot3dSourceOverlayModule(const Oot3dSourceOverlayModule&) = delete;
    Oot3dSourceOverlayModule& operator=(const Oot3dSourceOverlayModule&) = delete;

    bool Load(const std::filesystem::path& path,
              const Oot3dSourceOverlayHostApi& hostApi,
              std::string* error = nullptr);
    void Unload() noexcept;

    bool IsLoaded() const noexcept;
    std::string_view BuildId() const noexcept;
    std::span<const uint32_t> EntryPoints() const noexcept;
    bool Contains(uint32_t entry) const noexcept;
    int Execute(uint32_t entry, Oot3dSourceOverlayGuestState* state,
                Oot3dSourceOverlayExecutionResult* result,
                uint32_t blockBudget) const;

  private:
    static void SetError(std::string* error, std::string message);

    void* mLibrary = nullptr;
    const Oot3dSourceOverlayPluginApi* mApi = nullptr;
    std::string mBuildId;
    std::vector<uint32_t> mEntryPoints;
};

} // namespace Oot3dNativeGame

