#include "triaevum_ctr_filesystem_client_bridge.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Oot3dNativeGame {
namespace {

class ServiceBackedCtrFile final : public NativeA32CtrFile {
  public:
    ServiceBackedCtrFile(triaevum::module::FilesystemServiceClientV1& client,
                         uint64_t handle, uint64_t size)
        : mClient(client), mHandle(handle), mSize(size) {}

    ~ServiceBackedCtrFile() override {
        if (mHandle != 0U) {
            mClient.Close(mHandle);
        }
    }

    bool Read(uint64_t offset, std::span<uint8_t> destination,
              uint32_t* returnedSize) override {
        if (returnedSize == nullptr) {
            return false;
        }
        triaevum::module::FilesystemReadResultV1 result;
        const auto status = mClient.Read(mHandle, offset, destination, &result);
        if (status != TRIAEVUM_MODULE_OK_V1) {
            return false;
        }
        *returnedSize = result.returnedSize;
        return true;
    }

    bool Write(uint64_t offset, std::span<const uint8_t> data,
               uint32_t* writtenSize) override {
        if (writtenSize == nullptr ||
            offset > std::numeric_limits<uint64_t>::max() - data.size()) {
            return false;
        }
        const auto status = mClient.Write(mHandle, offset, data, writtenSize);
        if (status != TRIAEVUM_MODULE_OK_V1) {
            return false;
        }
        mSize = std::max(mSize, offset + *writtenSize);
        return true;
    }

    bool Resize(uint64_t size) override {
        if (mClient.Resize(mHandle, size) != TRIAEVUM_MODULE_OK_V1) {
            return false;
        }
        mSize = size;
        return true;
    }

    uint64_t Size() const noexcept override { return mSize; }

  private:
    triaevum::module::FilesystemServiceClientV1& mClient;
    uint64_t mHandle = 0U;
    uint64_t mSize = 0U;
};

} // namespace

TriAevumCtrFilesystemClientBridge::TriAevumCtrFilesystemClientBridge(
    triaevum::module::FilesystemServiceClientV1& client)
    : mClient(client) {}

bool TriAevumCtrFilesystemClientBridge::EnsureSaveRoot() {
    return mClient.IsAvailable();
}

bool TriAevumCtrFilesystemClientBridge::RemoveFile(std::string_view path, bool* removed) {
    return mClient.RemoveFile(TRIAEVUM_FILESYSTEM_SAVE_V1, path, removed) == TRIAEVUM_MODULE_OK_V1;
}

bool TriAevumCtrFilesystemClientBridge::Open(
    NativeA32CtrFilesystemRoot root, std::string_view path, uint32_t flags,
    std::shared_ptr<NativeA32CtrFile>* file) {
    if (file == nullptr || path.empty()) {
        return false;
    }
    TriAevumFilesystemOpenFlagsV1 serviceFlags = 0U;
    serviceFlags |= (flags & NativeA32CtrFileOpenRead) != 0U
                        ? TRIAEVUM_FILESYSTEM_OPEN_READ_V1
                        : 0U;
    serviceFlags |= (flags & NativeA32CtrFileOpenWrite) != 0U
                        ? TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1
                        : 0U;
    serviceFlags |= (flags & NativeA32CtrFileOpenCreate) != 0U
                        ? TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1
                        : 0U;
    serviceFlags |= (flags & NativeA32CtrFileOpenTruncate) != 0U
                        ? TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1
                        : 0U;
    triaevum::module::FilesystemOpenResultV1 opened;
    const auto status = mClient.Open(root == NativeA32CtrFilesystemRoot::Content
                                         ? TRIAEVUM_FILESYSTEM_CONTENT_V1
                                         : TRIAEVUM_FILESYSTEM_SAVE_V1,
                                     path, serviceFlags, &opened);
    if (status != TRIAEVUM_MODULE_OK_V1) {
        return false;
    }
    *file = std::make_shared<ServiceBackedCtrFile>(mClient, opened.handle,
                                                   opened.size);
    return true;
}

} // namespace Oot3dNativeGame
