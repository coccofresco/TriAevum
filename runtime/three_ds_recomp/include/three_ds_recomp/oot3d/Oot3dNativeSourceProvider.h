#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"

namespace ThreeDsRecomp::Oot3d {

struct NativeSource {
    std::string ResourcePath;
    std::shared_ptr<const std::vector<uint8_t>> Bytes;
};

class NativeSourceProvider {
  public:
    using FileLoader = std::function<
        std::shared_ptr<const std::vector<uint8_t>>(const std::string&)>;

    explicit NativeSourceProvider(FileLoader loader);

    std::shared_ptr<const NativeSource> Load(std::string_view resourcePath);
    std::vector<ZsiEmbeddedCmb> LoadZsiEmbeddedCmbs(std::string_view resourcePath);
    void Clear();

  private:
    FileLoader mLoader;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeSource>> mCache;
};

} // namespace ThreeDsRecomp::Oot3d
