#include "oot3d_dev_lab.h"

#include <fstream>
#include <string>

namespace Oot3dNativeGame {

DevLabChannel::DevLabChannel(std::filesystem::path directory) : mDirectory(std::move(directory)) {
  std::error_code error;
  std::filesystem::create_directories(mDirectory, error);
  // Commands already in the file belong to an earlier session.
  mOffset = std::filesystem::exists(mDirectory / "commands.jsonl", error)
                ? std::filesystem::file_size(mDirectory / "commands.jsonl", error)
                : 0U;
  if (error) mOffset = 0;
  mAcks = std::fopen((mDirectory / "acks.jsonl").string().c_str(), "ab");
  if (mAcks == nullptr) std::fprintf(stderr, "TRIAEVUM_DEV_LAB cannot open %s\n", (mDirectory / "acks.jsonl").string().c_str());
}

DevLabChannel::~DevLabChannel() {
  if (mAcks != nullptr) std::fclose(mAcks);
}

std::optional<nlohmann::json> DevLabChannel::Next() {
  std::ifstream stream(mDirectory / "commands.jsonl", std::ios::binary);
  if (!stream) return std::nullopt;
  stream.seekg(static_cast<std::streamoff>(mOffset));
  std::string line;
  // Only a line its writer finished (newline-terminated).
  if (!std::getline(stream, line) || stream.eof()) return std::nullopt;
  mOffset += line.size() + 1U;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.find_first_not_of(" \t") == std::string::npos) return Next();
  auto command = nlohmann::json::parse(line, nullptr, false);
  if (command.is_discarded() || !command.is_object() || !command.contains("op") || !command["op"].is_string()) {
    Answer(command.is_object() ? command : nlohmann::json::object(), "rejected",
           {{"reason", "not a JSON object with a string \"op\""}, {"line", line.substr(0, 200)}});
    return Next();
  }
  return command;
}

void DevLabChannel::Answer(const nlohmann::json &command, std::string_view status, const nlohmann::json &detail) {
  if (mAcks == nullptr) return;
  nlohmann::json ack = {{"id", command.value("id", nlohmann::json())},
                        {"op", command.value("op", std::string())},
                        {"status", status}};
  if (detail.is_object())
    for (const auto &[key, value] : detail.items()) ack[key] = value;
  const auto text = ack.dump() + "\n";
  std::fwrite(text.data(), 1, text.size(), mAcks);
  std::fflush(mAcks);
}

} // namespace Oot3dNativeGame
