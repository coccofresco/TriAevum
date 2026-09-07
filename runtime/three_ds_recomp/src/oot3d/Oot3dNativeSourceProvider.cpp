#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

#include <stdexcept>

namespace ThreeDsRecomp::Oot3d {

NativeSourceProvider::NativeSourceProvider(FileLoader loader) : mLoader(std::move(loader)) {
    if (!mLoader) {
        throw std::invalid_argument("OOT3D native source provider requires a file loader");
    }
}

std::shared_ptr<const NativeSource> NativeSourceProvider::Load(std::string_view resourcePath) {
    const std::string path(resourcePath);
    std::scoped_lock lock(mMutex);
    const auto cached = mCache.find(path);
    if (cached != mCache.end()) {
        return cached->second;
    }
    const auto bytes = mLoader(path);
    if (bytes == nullptr) {
        return nullptr;
    }
    auto source = std::make_shared<NativeSource>(NativeSource{ path, bytes });
    mCache.emplace(path, source);
    return source;
}

std::vector<ZsiEmbeddedCmb> NativeSourceProvider::LoadZsiEmbeddedCmbs(std::string_view resourcePath) {
    const auto source = Load(resourcePath);
    if (source == nullptr) {
        return {};
    }
    return ParseZsiEmbeddedCmbsBytes(*source->Bytes, source->ResourcePath);
}

void NativeSourceProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mCache.clear();
}

} // namespace ThreeDsRecomp::Oot3d
