#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace Fast::Oot3d {

struct TextureCatalogEntry {
    uint64_t ContentHash = 0;
    uint32_t PhysicalAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t NativeFormat = 0;
    uint64_t Observations = 0;
};

class TextureCatalogRuntime final {
  public:
    static TextureCatalogRuntime& Instance();
    void Observe(uint64_t contentHash, uint32_t physicalAddress,
                 uint16_t width, uint16_t height, uint8_t nativeFormat);
    [[nodiscard]] std::vector<TextureCatalogEntry> Snapshot() const;
    void Clear();

  private:
    mutable std::mutex mMutex;
    std::vector<TextureCatalogEntry> mEntries;
};

} // namespace Fast::Oot3d
