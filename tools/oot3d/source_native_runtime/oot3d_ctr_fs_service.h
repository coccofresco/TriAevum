#pragma once

#include "oot3d_ctr_ipc_router.h"
#include "oot3d_guest_address_space.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace Oot3dSourceRuntime {

struct CtrFsProfile {
    std::filesystem::path RomFsImagePath;
    std::uint64_t RomFsImageOffset = 0;
    std::uint64_t RomFsImageSize = 0;
    std::filesystem::path SaveDataDirectory;
};

class CtrFileSession final : public CtrIpcSession {
  public:
    CtrFileSession(GuestAddressSpace& memory, std::filesystem::path path,
                   std::uint64_t fileOffset, std::uint64_t fileSize);
    CtrFileSession(GuestAddressSpace& memory, std::filesystem::path path,
                   std::uint32_t openMode);
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

  private:
    GuestAddressSpace& mMemory;
    std::filesystem::path mPath;
    std::uint64_t mFileOffset = 0;
    std::uint64_t mFileSize = 0;
    std::uint32_t mOpenMode = 1;
    bool mWritable = false;
};

class CtrFsUserService final : public CtrIpcSession {
  public:
    CtrFsUserService(GuestAddressSpace& memory, CtrIpcRouter& router,
                     CtrFsProfile profile);
    CtrResult Dispatch(std::span<std::uint32_t> commandBuffer) override;

  private:
    GuestAddressSpace& mMemory;
    CtrIpcRouter& mRouter;
    CtrFsProfile mProfile;
    std::uint64_t mNextArchiveHandle = 1;
    std::unordered_map<std::uint64_t, std::filesystem::path> mArchives;
};

} // namespace Oot3dSourceRuntime
