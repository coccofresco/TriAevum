#pragma once

// Developer builds: an agent drives a running session through files
// (TRIAEVUM_DEV_LAB=<dir>). Commands are appended to <dir>/commands.jsonl, one
// JSON object per line with an "id" and an "op"; each is answered, in order,
// by a line of <dir>/acks.jsonl. With TRIAEVUM_ENVIRONMENT_HOT_RELOAD the
// scene profiles reload from disk meanwhile, so a look is iterated (edit,
// capture, compare) without restarting the game.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

namespace Oot3dNativeGame {

class DevLabChannel {
public:
  explicit DevLabChannel(std::filesystem::path directory);
  ~DevLabChannel();
  DevLabChannel(const DevLabChannel &) = delete;
  DevLabChannel &operator=(const DevLabChannel &) = delete;

  // The next complete command line (a line still being written waits). A
  // line that is not a JSON object with an "op" is answered as rejected and
  // skipped.
  [[nodiscard]] std::optional<nlohmann::json> Next();
  // One acks.jsonl line: the command's id and op, the status ("ok",
  // "failed", "timeout", "rejected") and the detail's members.
  void Answer(const nlohmann::json &command, std::string_view status,
              const nlohmann::json &detail = nlohmann::json::object());
  [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return mDirectory; }

private:
  std::filesystem::path mDirectory;
  uint64_t mOffset = 0;
  std::FILE *mAcks = nullptr;
};

} // namespace Oot3dNativeGame
