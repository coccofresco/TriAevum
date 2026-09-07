#pragma once

#undef _DLL

#include "ship/resource/archive/FolderArchive.h"

#include "ship/Context.h"
#include "spdlog/spdlog.h"
#include "ship/utils/filesystemtools/FileHelper.h"
#include "ship/resource/ResourceManager.h"

#include <filesystem>

namespace Ship {
FolderArchive::FolderArchive(const std::string& archivePath) : Archive(archivePath) {
    mArchiveBasePath = std::filesystem::path(archivePath).lexically_normal().generic_string();
    if (!mArchiveBasePath.ends_with('/')) {
        mArchiveBasePath += "/";
    }
}

Ship::FolderArchive::~FolderArchive() {
    SPDLOG_TRACE("destruct folderarchive: {}", GetPath());
}

bool FolderArchive::Open() {

    auto fileEntries = Directory::ListFiles(mArchiveBasePath);
    const auto basePath = std::filesystem::path(mArchiveBasePath);

    for (const auto& fileEntry : fileEntries) {
        std::error_code ec;
        auto relativePath = std::filesystem::relative(std::filesystem::path(fileEntry), basePath, ec);
        if (ec) {
            relativePath = std::filesystem::path(fileEntry).lexically_relative(basePath);
        }
        if (relativePath.empty() || relativePath.is_absolute()) {
            SPDLOG_WARN("Skipping folder archive entry outside archive root: {}", fileEntry);
            continue;
        }

        auto filePath = relativePath.generic_string();
        if (filePath == "." || filePath == ".." || filePath.starts_with("../")) {
            SPDLOG_WARN("Skipping folder archive entry outside archive root: {}", fileEntry);
            continue;
        }
        IndexFile(filePath);
    }

    return true;
}

bool FolderArchive::Close() {
    return true;
}

bool FolderArchive::WriteFile(const std::string& filename, const std::vector<uint8_t>& data) {
    Ship::FileHelper::WriteAllBytes(mArchiveBasePath + filename, data);
    return true;
}

std::shared_ptr<File> Ship::FolderArchive::LoadFile(const std::string& filePath) {
    return LoadFileRaw(filePath);
}

std::shared_ptr<File> Ship::FolderArchive::LoadFile(uint64_t hash) {
    const std::string& filePath =
        *Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);

    return LoadFileRaw(filePath);
}

std::shared_ptr<File> FolderArchive::LoadFileRaw(const std::string& filePath) {
    if (Ship::FileHelper::Exists(mArchiveBasePath + filePath)) {
        auto data = Ship::FileHelper::ReadAllBytes(mArchiveBasePath + filePath);
        auto fileToLoad = std::make_shared<File>();

        fileToLoad->Buffer = std::make_shared<std::vector<char>>(data.size());
        memcpy(fileToLoad->Buffer->data(), data.data(), data.size());

        fileToLoad->IsLoaded = true;

        return fileToLoad;
    } else {
        return nullptr;
    }
}

std::shared_ptr<File> FolderArchive::LoadFileRaw(uint64_t hash) {
    const std::string& filePath =
        *Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);

    return LoadFileRaw(filePath);
}
} // namespace Ship
