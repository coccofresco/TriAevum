#pragma once

#include <filesystem>
#include <string>

#include "oot3d_link_runtime_types.h"

namespace Oot3dNativeGame {

class PlayerCollisionActionContract {
  public:
    static PlayerCollisionActionContract LoadFile(const std::filesystem::path& path);

    const std::filesystem::path& CodeBinPath() const;
    const std::string& CodeBinSha256() const;
    const LinkNativeCollisionActionConfig& Config() const;
    const LinkNativePlayerActionConfig& ActionConfig() const;

  private:
    std::filesystem::path mCodeBinPath;
    std::string mCodeBinSha256;
    LinkNativeCollisionActionConfig mConfig;
    LinkNativePlayerActionConfig mActionConfig;
};

} // namespace Oot3dNativeGame
