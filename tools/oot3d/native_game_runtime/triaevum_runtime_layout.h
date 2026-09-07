#pragma once

#include <filesystem>

namespace Oot3dNativeGame {

struct TriAevumRuntimePathOverrides {
  std::filesystem::path ExecutablePath;
  std::filesystem::path DataRoot;
  std::filesystem::path ActiveTitleState;
  std::filesystem::path TitleDirectory;
  std::filesystem::path ModulePath;
  std::filesystem::path CacheDirectory;
  std::filesystem::path ContentIndexPath;
  std::filesystem::path ResourceRoot;
  std::filesystem::path ControlConfigPath;
  std::filesystem::path ConfigurationPath;
};

struct TriAevumRuntimeLayout {
  std::filesystem::path DataRoot;
  std::filesystem::path TitleDirectory;
  std::filesystem::path ModulePath;
  std::filesystem::path CacheDirectory;
  std::filesystem::path ContentIndexPath;
  std::filesystem::path ResourceRoot;
  std::filesystem::path ControlConfigPath;
  std::filesystem::path ConfigurationPath;
};

std::filesystem::path DefaultTriAevumDataRoot();
std::filesystem::path DefaultTriAevumActiveTitleState();

TriAevumRuntimeLayout ResolveTriAevumRuntimeLayout(
    const TriAevumRuntimePathOverrides &overrides);

} // namespace Oot3dNativeGame
