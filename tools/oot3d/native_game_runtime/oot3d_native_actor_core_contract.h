#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Oot3dNativeGame {

struct ActorCoreFunctionContract {
    uint32_t Address = 0;
    uint32_t Size = 0;
    std::string Family;
};

class ActorCoreContract {
  public:
    static ActorCoreContract LoadFile(const std::filesystem::path& path);

    uint32_t ActorEntrySize() const;
    uint32_t ActorContextSize() const;
    uint32_t CategoryListCount() const;
    uint32_t SpawnTotalGuardValue() const;
    const ActorCoreFunctionContract& Function(std::string_view name) const;
    const std::string& SnapshotId() const;
    const std::string& SourceRevision() const;
    const std::string& CodeBinSha256() const;

  private:
    uint32_t mActorEntrySize = 0;
    uint32_t mActorContextSize = 0;
    uint32_t mCategoryListCount = 0;
    uint32_t mSpawnTotalGuardValue = 0;
    std::string mSnapshotId;
    std::string mSourceRevision;
    std::string mCodeBinSha256;
    std::unordered_map<std::string, ActorCoreFunctionContract> mFunctions;
};

} // namespace Oot3dNativeGame
