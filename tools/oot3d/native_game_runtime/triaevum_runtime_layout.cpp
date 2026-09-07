#include "triaevum_runtime_layout.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

using Json = nlohmann::json;

std::filesystem::path AbsoluteNormalized(const std::filesystem::path &path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    throw std::runtime_error("cannot resolve path: " + path.string());
  }
  return absolute.lexically_normal();
}

Json ReadJsonObject(const std::filesystem::path &path, const char *label) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error(std::string("cannot open ") + label + ": " +
                             path.string());
  }
  Json value;
  try {
    stream >> value;
  } catch (const Json::exception &exception) {
    throw std::runtime_error(std::string("invalid ") + label + ": " +
                             exception.what());
  }
  if (!value.is_object()) {
    throw std::runtime_error(std::string(label) + " is not an object");
  }
  return value;
}

bool IsContained(const std::filesystem::path &root,
                 const std::filesystem::path &candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  while (rootPart != root.end() && candidatePart != candidate.end()) {
    if (*rootPart != *candidatePart) {
      return false;
    }
    ++rootPart;
    ++candidatePart;
  }
  return rootPart == root.end();
}

std::filesystem::path ResolvePrivateChild(const std::filesystem::path &root,
                                          const std::string &relative,
                                          const char *label) {
  const std::filesystem::path input(relative);
  if (input.empty() || input.is_absolute()) {
    throw std::runtime_error(std::string(label) +
                             " must be relative to the prepared title");
  }
  for (const auto &part : input) {
    if (part == "..") {
      throw std::runtime_error(std::string(label) +
                               " escapes the prepared title");
    }
  }
  const auto normalizedRoot = AbsoluteNormalized(root);
  const auto resolved = AbsoluteNormalized(normalizedRoot / input);
  if (!IsContained(normalizedRoot, resolved)) {
    throw std::runtime_error(std::string(label) +
                             " escapes the prepared title");
  }
  return resolved;
}

std::filesystem::path ResolveExecutableRoot(
    const std::filesystem::path &executablePath) {
  if (executablePath.empty()) {
    throw std::runtime_error("runtime executable path is empty");
  }
  return AbsoluteNormalized(executablePath).parent_path();
}

std::string RequiredString(const Json &object, const char *field,
                           const char *label) {
  const auto iterator = object.find(field);
  if (iterator == object.end() || !iterator->is_string() ||
      iterator->get_ref<const std::string &>().empty()) {
    throw std::runtime_error(std::string(label) + " has no valid " + field);
  }
  return iterator->get<std::string>();
}

std::filesystem::path ResolveActiveTitle(
    const std::filesystem::path &activeTitleState) {
  const Json active = ReadJsonObject(activeTitleState, "active-title state");
  if (active.value("format", "") != "triaevum_active_title_v1") {
    throw std::runtime_error("unsupported active-title state format");
  }
  return AbsoluteNormalized(
      RequiredString(active, "directory", "active-title state"));
}

} // namespace

std::filesystem::path DefaultTriAevumDataRoot() {
  if (const char *localAppData = std::getenv("LOCALAPPDATA");
      localAppData != nullptr && *localAppData != '\0') {
    return AbsoluteNormalized(std::filesystem::path(localAppData) / "TriAevum");
  }
  if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return AbsoluteNormalized(std::filesystem::path(home) / ".local" / "share" /
                              "TriAevum");
  }
  throw std::runtime_error(
      "cannot locate the TriAevum user-data directory (LOCALAPPDATA/HOME unset)");
}

std::filesystem::path DefaultTriAevumActiveTitleState() {
  return DefaultTriAevumDataRoot() / "active-title.json";
}

TriAevumRuntimeLayout ResolveTriAevumRuntimeLayout(
    const TriAevumRuntimePathOverrides &overrides) {
  TriAevumRuntimeLayout result;
  const auto executableRoot = ResolveExecutableRoot(overrides.ExecutablePath);
  if (!overrides.DataRoot.empty()) {
    result.DataRoot = AbsoluteNormalized(overrides.DataRoot);
  } else {
    const auto portableDataRoot = executableRoot / "data";
    std::error_code error;
    const bool hasPortableTitle = std::filesystem::is_regular_file(
        portableDataRoot / "active-title.json", error);
    result.DataRoot = hasPortableTitle ? AbsoluteNormalized(portableDataRoot)
                                       : DefaultTriAevumDataRoot();
  }

  const bool hasDirectPair = !overrides.ModulePath.empty() &&
                             !overrides.ContentIndexPath.empty();
  if (!overrides.TitleDirectory.empty()) {
    result.TitleDirectory = AbsoluteNormalized(overrides.TitleDirectory);
  } else if (hasDirectPair) {
    result.TitleDirectory =
        AbsoluteNormalized(overrides.ContentIndexPath).parent_path();
  } else {
    const auto activeState = overrides.ActiveTitleState.empty()
                                 ? result.DataRoot / "active-title.json"
                                 : AbsoluteNormalized(overrides.ActiveTitleState);
    result.TitleDirectory = ResolveActiveTitle(activeState);
  }

  Json forgeState;
  const bool needsForgeState = overrides.ModulePath.empty() ||
                               overrides.ContentIndexPath.empty();
  std::string contentKey = result.TitleDirectory.filename().string();
  if (needsForgeState) {
    forgeState = ReadJsonObject(result.TitleDirectory / "forge-state.json",
                                "Forge state");
    if (forgeState.value("format", "") != "triaevum_forge_state_v1") {
      throw std::runtime_error("unsupported Forge state format");
    }
    contentKey = forgeState.value("content_key", contentKey);
  }

  if (!overrides.ModulePath.empty()) {
    result.ModulePath = AbsoluteNormalized(overrides.ModulePath);
  } else {
    const auto module = forgeState.find("module");
    if (module == forgeState.end() || !module->is_object() ||
        module->value("status", "") != "ready") {
      throw std::runtime_error(
          "the active title has no verified private module; run TriAevum Forge");
    }
    result.ModulePath = ResolvePrivateChild(
        result.TitleDirectory,
        RequiredString(*module, "container", "Forge module state"),
        "private module path");
  }

  if (!overrides.ContentIndexPath.empty()) {
    result.ContentIndexPath = AbsoluteNormalized(overrides.ContentIndexPath);
  } else {
    const auto content = forgeState.find("content");
    if (content == forgeState.end() || !content->is_object() ||
        content->value("status", "") != "ready") {
      throw std::runtime_error("the active title has no verified content index");
    }
    result.ContentIndexPath = ResolvePrivateChild(
        result.TitleDirectory,
        RequiredString(*content, "index", "Forge content state"),
        "private content index");
  }

  result.CacheDirectory = overrides.CacheDirectory.empty()
                              ? result.DataRoot / "module-cache" / contentKey
                              : AbsoluteNormalized(overrides.CacheDirectory);
  result.ResourceRoot = overrides.ResourceRoot.empty()
                            ? executableRoot / "resources"
                            : AbsoluteNormalized(overrides.ResourceRoot);
  result.ConfigurationPath = overrides.ConfigurationPath.empty()
                                 ? result.DataRoot / "config" / "TriAevum.json"
                                 : AbsoluteNormalized(overrides.ConfigurationPath);
  result.ControlConfigPath = overrides.ControlConfigPath.empty()
                                 ? result.DataRoot / "config" / "controls.json"
                                 : AbsoluteNormalized(overrides.ControlConfigPath);
  if (overrides.ControlConfigPath.empty() &&
      !std::filesystem::is_regular_file(result.ControlConfigPath)) {
    result.ControlConfigPath.clear();
  }
  return result;
}

} // namespace Oot3dNativeGame
