#pragma once

#include "oot3d_native_a32_ctr_host.h"
#include "triaevum/filesystem_service_client.h"

namespace Oot3dNativeGame {

class TriAevumCtrFilesystemClientBridge final : public NativeA32CtrFilesystem {
  public:
    explicit TriAevumCtrFilesystemClientBridge(
        triaevum::module::FilesystemServiceClientV1& client);

    bool EnsureSaveRoot() override;
    bool Open(NativeA32CtrFilesystemRoot root, std::string_view path,
              uint32_t flags, std::shared_ptr<NativeA32CtrFile>* file) override;

  private:
    triaevum::module::FilesystemServiceClientV1& mClient;
};

} // namespace Oot3dNativeGame
