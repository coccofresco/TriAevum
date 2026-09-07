#include "triaevum_forge_content_filesystem.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

using Json = nlohmann::json;

constexpr std::uintmax_t kMaximumContentIndexBytes = 4U * 1024U * 1024U;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::filesystem::path ReadIndexedPath(const Json& descriptor,
                                      std::uint64_t* expectedBytes) {
    if (!descriptor.is_object() || expectedBytes == nullptr ||
        !descriptor.contains("path") || !descriptor.at("path").is_string() ||
        !descriptor.contains("bytes") ||
        !descriptor.at("bytes").is_number_unsigned()) {
        throw std::runtime_error("content.tap file descriptor is invalid");
    }
    *expectedBytes = descriptor.at("bytes").get<std::uint64_t>();
    return descriptor.at("path").get<std::string>();
}

void AddMapping(triaevum::module::RootedFilesystemConfigV1& config,
                std::string logicalPath, const Json& descriptor) {
    std::uint64_t expectedBytes = 0U;
    const auto hostPath = ReadIndexedPath(descriptor, &expectedBytes);
    std::error_code fileError;
    const std::uintmax_t actualBytes =
        std::filesystem::file_size(hostPath, fileError);
    if (fileError || actualBytes != expectedBytes || expectedBytes == 0U ||
        !config.contentFiles.emplace(std::move(logicalPath), hostPath).second) {
        throw std::runtime_error(
            "content.tap references an unavailable or ambiguous file");
    }
}

} // namespace

std::unique_ptr<triaevum::module::RootedFilesystemBackendV1>
CreateTriAevumForgeContentFilesystem(
    const std::filesystem::path& contentIndexPath,
    const std::filesystem::path& saveRootOverride, std::string* error) {
    try {
        std::error_code fileError;
        const std::uintmax_t indexBytes =
            std::filesystem::file_size(contentIndexPath, fileError);
        if (fileError || indexBytes == 0U ||
            indexBytes > kMaximumContentIndexBytes) {
            throw std::runtime_error("private content index has invalid size");
        }
        std::ifstream stream(contentIndexPath, std::ios::binary);
        Json content;
        if (!stream || !(stream >> content) || !content.is_object() ||
            content.value("format", "") != "triaevum_content_index_v1" ||
            !content.value("private_local_artifact", false) ||
            content.value("redistributable", true)) {
            throw std::runtime_error("private content index is invalid");
        }

        triaevum::module::RootedFilesystemConfigV1 config;
        const auto& inputs = content.at("inputs");
        if (!inputs.is_object() || inputs.empty()) {
            throw std::runtime_error("private content index has no inputs");
        }
        for (const auto& [name, descriptor] : inputs.items()) {
            AddMapping(config, "inputs/" + name, descriptor);
        }
        AddMapping(config, "process_manifest", content.at("process_manifest"));
        config.saveRoot = saveRootOverride.empty()
                              ? contentIndexPath.parent_path() / "savedata"
                              : saveRootOverride;
        auto backend = triaevum::module::RootedFilesystemBackendV1::Create(
            std::move(config), error);
        if (backend == nullptr && error != nullptr && error->empty()) {
            *error = "rooted filesystem backend could not be created";
        }
        return backend;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return nullptr;
    }
}

} // namespace Oot3dNativeGame
