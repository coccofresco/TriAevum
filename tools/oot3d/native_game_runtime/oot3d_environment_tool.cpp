// triaevum_environment_tool: scene environment profiles for agents and
// scripts, without starting the game.
//
//   validate <file>...           one JSON line per file: valid, errors (JSON pointers)
//   format [--check] <file>...   rewrite in the runtime's canonical form, or
//                                only report the files that are not
//   explain <file>               the authored profile, the renderer inputs,
//                                run-time dependencies and notes (JSON)
//
// A file named scene-XXXX[.night].environment.json must name that scene and
// time of day. Exit status: 0 success, 1 invalid or not canonical, 2 usage.
#include "oot3d_scene_environment_codec.h"
#include "ship/config/ConfigPersistence.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace {
using Json = nlohmann::json;
using namespace Oot3dNativeGame;

struct Loaded {
  std::string Text;
  SceneEnvironmentDocument Document;
};

// As the runtime reads it (64 KiB at most); a file named
// scene-XXXX[.night].environment.json must name that scene and time of day.
Loaded Load(const std::filesystem::path &path) {
  std::optional<uint16_t> scene;
  std::optional<bool> night;
  std::smatch match;
  const auto name = path.filename().string();
  static const std::regex kName(R"(scene-([0-9A-Fa-f]{4})(\.night)?\.environment\.json)");
  if (std::regex_match(name, match, kName)) {
    scene = static_cast<uint16_t>(std::stoul(match[1].str(), nullptr, 16));
    night = match[2].matched;
  }
  auto file = LoadSceneEnvironmentFile(path, scene, night);
  return {std::move(file.Text), std::move(file.Document)};
}

Json Report(const std::filesystem::path &path, const SceneEnvironmentDocument &document) {
  Json errors = Json::array();
  for (const auto &error : document.Errors) errors.push_back({{"pointer", error.Pointer}, {"message", error.Message}});
  Json report = {{"file", path.string()}, {"valid", document.Profile.has_value()}, {"errors", std::move(errors)}};
  if (document.SceneId) report["sceneId"] = *document.SceneId;
  report["timeOfDay"] = document.Night ? "night" : "day";
  return report;
}

int Usage() {
  std::fputs("usage: triaevum_environment_tool validate <file>...\n"
             "       triaevum_environment_tool format [--check] <file>...\n"
             "       triaevum_environment_tool explain <file>\n",
             stderr);
  return 2;
}
} // namespace

int main(int argc, char **argv) try {
  if (argc < 3) return Usage();
  const std::string command = argv[1];
  if (command == "validate") {
    int status = 0;
    for (int i = 2; i < argc; ++i) {
      const auto loaded = Load(argv[i]);
      std::cout << Report(argv[i], loaded.Document).dump() << "\n";
      if (!loaded.Document.Profile) status = 1;
    }
    return status;
  }
  if (command == "format") {
    const bool check = std::string(argv[2]) == "--check";
    int status = 0;
    for (int i = check ? 3 : 2; i < argc; ++i) {
      const std::filesystem::path path = argv[i];
      const auto loaded = Load(path);
      if (!loaded.Document.Profile) {
        std::cout << Report(path, loaded.Document).dump() << "\n";
        status = 1;
        continue;
      }
      const auto canonical = FormatSceneEnvironment(
          EncodeSceneEnvironment(*loaded.Document.Profile, *loaded.Document.SceneId, loaded.Document.Night));
      const bool same = canonical == loaded.Text;
      if (!same && !check) {
        // Atomically, as the runtime saves: a failed write keeps the original.
        const auto written = Ship::WriteConfigFileAtomically(path, canonical);
        if (!written.Success) {
          std::cout << Json{{"file", path.string()}, {"formatted", false}, {"error", written.Error}}.dump() << "\n";
          status = 1;
          continue;
        }
      }
      std::cout << Json{{"file", path.string()}, {"canonical", same}, {"formatted", !same && !check}}.dump() << "\n";
      if (!same && check) status = 1;
    }
    return status;
  }
  if (command == "explain" && argc == 3) {
    const auto loaded = Load(argv[2]);
    if (!loaded.Document.Profile) {
      std::cout << Report(argv[2], loaded.Document).dump(2) << "\n";
      return 1;
    }
    std::cout << ExplainSceneEnvironment(*loaded.Document.Profile, *loaded.Document.SceneId, loaded.Document.Night)
                     .dump(2)
              << "\n";
    return 0;
  }
  return Usage();
} catch (const std::exception &error) {
  std::fprintf(stderr, "triaevum_environment_tool: %s\n", error.what());
  return 2;
}
