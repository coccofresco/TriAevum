#include "triaevum_runtime_layout.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    mPath = std::filesystem::temp_directory_path() /
            ("triaevum-runtime-layout-" + std::to_string(nonce));
    std::filesystem::create_directories(mPath);
  }
  ~TemporaryDirectory() { std::filesystem::remove_all(mPath); }
  const std::filesystem::path &Path() const { return mPath; }

private:
  std::filesystem::path mPath;
};

void Write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
  if (!stream) {
    throw std::runtime_error("cannot write test fixture");
  }
}

void TestInstalledLayout() {
  TemporaryDirectory temporary;
  const auto install = temporary.Path() / "install";
  const auto data = temporary.Path() / "data";
  const auto title = data / "titles" / "oot3d" / "012345";
  Write(install / "TriAevum.exe", "runtime");
  Write(title / "content.tap", "content");
  Write(title / "modules" / "game.tam", "module");
  Write(title / "forge-state.json",
        R"({"format":"triaevum_forge_state_v1","content_key":"012345","content":{"status":"ready","index":"content.tap"},"module":{"status":"ready","container":"modules/game.tam"}})");
  Write(data / "active-title.json",
        std::string("{\"format\":\"triaevum_active_title_v1\",\"directory\":\"") +
            title.generic_string() + "\"}");

  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = install / "TriAevum.exe";
  overrides.DataRoot = data;
  const auto result = Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides);
  Expect(result.ModulePath == title / "modules" / "game.tam",
         "module was not resolved from Forge state");
  Expect(result.ContentIndexPath == title / "content.tap",
         "content index was not resolved from Forge state");
  Expect(result.ResourceRoot == install / "resources",
         "runtime resources are not install-relative");
  Expect(result.CacheDirectory == data / "module-cache" / "012345",
         "module cache is not content-addressed");
  Expect(result.ControlConfigPath.empty(),
         "an absent default controls file must remain optional");
}

void TestDirectDevelopmentLayout() {
  TemporaryDirectory temporary;
  const auto module = temporary.Path() / "private" / "game.tam";
  const auto content = temporary.Path() / "private" / "content.tap";
  Write(module, "module");
  Write(content, "content");
  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = temporary.Path() / "TriAevum.exe";
  overrides.DataRoot = temporary.Path() / "data";
  overrides.ModulePath = module;
  overrides.ContentIndexPath = content;
  overrides.ResourceRoot = temporary.Path() / "resources";
  const auto result = Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides);
  Expect(result.TitleDirectory == content.parent_path(),
         "direct development inputs did not infer a title directory");
}

void TestPortableForgeLayout() {
  TemporaryDirectory temporary;
  const auto install = temporary.Path() / "install";
  const auto data = install / "data";
  const auto title = data / "titles" / "oot3d" / "portable";
  Write(install / "TriAevum.exe", "runtime");
  Write(title / "content.tap", "content");
  Write(title / "modules" / "game.tam", "module");
  Write(
      title / "forge-state.json",
      R"({"format":"triaevum_forge_state_v1","content_key":"portable","content":{"status":"ready","index":"content.tap"},"module":{"status":"ready","container":"modules/game.tam"}})");
  Write(
      data / "active-title.json",
      std::string("{\"format\":\"triaevum_active_title_v1\",\"directory\":\"") +
          title.generic_string() + "\"}");

  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = install / "TriAevum.exe";
  const auto result = Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides);
  Expect(result.DataRoot == data,
         "runtime did not discover Forge data beside the executable");
  Expect(result.ModulePath == title / "modules" / "game.tam",
         "portable Forge module was not resolved");
}

void TestPortableDefaultsAndRelocation() {
  TemporaryDirectory temporary;
  const auto original = temporary.Path() / "original package";
  const auto moved = temporary.Path() / "moved package";
  const auto executable = original / "TriAevum.exe";
  Expect(Oot3dNativeGame::DefaultTriAevumDataRoot(executable) == original / "data",
         "first-run data default must not fall back to a user profile");
  Expect(Oot3dNativeGame::DefaultTriAevumActiveTitleState(executable) ==
             original / "data" / "active-title.json",
         "active title must use the same portable root");
  const auto title = original / "data" / "titles" / "portable";
  Write(title / "forge-state.json",
        R"({"format":"triaevum_forge_state_v1","content":{"status":"ready","index":"content.tap"},"module":{"status":"ready","container":"modules/game.tam"}})");
  Write(original / "data" / "active-title.json",
        R"({"format":"triaevum_active_title_v1","directory":"titles/portable"})");
  Write(original / "data" / "config" / "controls.json", "{}");
  std::filesystem::rename(original, moved);
  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = moved / "TriAevum.exe";
  const auto result = Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides);
  Expect(result.TitleDirectory == moved / "data" / "titles" / "portable",
         "active-title relative path was resolved against the working directory");
  Expect(result.ControlConfigPath == moved / "data" / "config" / "controls.json",
         "controls did not follow the moved package");
  Expect(result.ConfigurationPath == moved / "data" / "config" / "TriAevum.json",
         "configuration did not follow the moved package");
}

void TestEscapingModuleIsRejected() {
  TemporaryDirectory temporary;
  const auto title = temporary.Path() / "title";
  Write(title / "content.tap", "content");
  Write(title / "forge-state.json",
        R"({"format":"triaevum_forge_state_v1","content_key":"bad","content":{"status":"ready","index":"content.tap"},"module":{"status":"ready","container":"../outside.tam"}})");
  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = temporary.Path() / "TriAevum.exe";
  overrides.DataRoot = temporary.Path() / "data";
  overrides.TitleDirectory = title;
  try {
    static_cast<void>(
        Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides));
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error("escaping private module path was accepted");
}

} // namespace

int main() {
  try {
    TestInstalledLayout();
    TestDirectDevelopmentLayout();
    TestPortableForgeLayout();
    TestPortableDefaultsAndRelocation();
    TestEscapingModuleIsRejected();
    std::cout << "TriAevum runtime layout tests passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
