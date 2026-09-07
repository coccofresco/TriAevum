#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

namespace ThreeDsRecomp::Oot3d {

enum class NativeQdbCommandCategory {
    NoOp,
    CameraList,
    SingleCameraFixed,
    PackedThreeWordPairs,
    CountedThreeWordEntries,
    Fixed16,
    Blob16BitCount,
    BlobU32Size,
    CountedTwelveWordEntries,
};

struct NativeQdbCommand {
    uint32_t Index = 0;
    size_t Offset = 0;
    int32_t CommandId = 0;
    NativeQdbCommandCategory Category = NativeQdbCommandCategory::NoOp;
    int32_t EntryCount = -1;
    uint32_t CameraPointCount = 0;
    int64_t BlobSize = -1;
    size_t PayloadSize = 0;
    size_t TotalSize = 0;
};

struct NativeQdbTimeline {
    uint32_t VersionOrFlags = 0;
    int32_t CommandCount = 0;
    int32_t EndFrame = 0;
    size_t CommandStreamOffset = 0x10;
    size_t DecodedSize = 0;
    size_t TrailerSize = 0;
    std::vector<NativeQdbCommand> Commands;
};

struct NativeQdbAsset {
    const AssetCatalogRecord* CatalogRecord = nullptr;
    std::shared_ptr<const NativeSource> Source;
    NativeQdbTimeline Timeline;
};

NativeQdbTimeline ParseNativeQdb(std::span<const uint8_t> bytes, std::string_view sourceName);
std::string_view NativeQdbCommandCategoryName(NativeQdbCommandCategory category);

class NativeQdbProvider {
  public:
    explicit NativeQdbProvider(NativeSourceProvider::FileLoader loader);

    std::shared_ptr<const NativeQdbAsset> Load(const AssetCatalogRecord& record);
    void Clear();

  private:
    NativeSourceProvider mSources;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeQdbAsset>> mCache;
};

} // namespace ThreeDsRecomp::Oot3d
