#include "oot3d_native_a32_romfs_overlay.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFFU;
constexpr uint32_t kHeaderSize = 0x28U;
constexpr uint32_t kDirectoryEntrySize = 0x18U;
constexpr uint32_t kFileEntrySize = 0x20U;
constexpr uint64_t kFileDataAlignment = 0x10U;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1U) / alignment * alignment;
}

class HostRangeFile final : public NativeA32CtrFile {
  public:
    HostRangeFile(std::ifstream stream, uint64_t offset, uint64_t size)
        : mStream(std::move(stream)), mOffset(offset), mSize(size) {}

    bool Read(uint64_t offset, std::span<uint8_t> destination, uint32_t* returnedSize) override {
        if (offset > mSize || destination.size() > mSize - offset) {
            return false;
        }
        std::scoped_lock lock(mMutex);
        mStream.clear();
        mStream.seekg(static_cast<std::streamoff>(mOffset + offset), std::ios::beg);
        if (!destination.empty() &&
            !mStream.read(reinterpret_cast<char*>(destination.data()),
                          static_cast<std::streamsize>(destination.size()))) {
            return false;
        }
        if (returnedSize != nullptr) {
            *returnedSize = static_cast<uint32_t>(destination.size());
        }
        return true;
    }
    bool Write(uint64_t, std::span<const uint8_t>, uint32_t*) override { return false; }
    bool Resize(uint64_t) override { return false; }
    uint64_t Size() const noexcept override { return mSize; }

  private:
    std::mutex mMutex;
    std::ifstream mStream;
    uint64_t mOffset;
    uint64_t mSize;
};

// The merged level 3: its metadata in memory, then each file's data read
// from where it lives; the bytes between files are zero.
class OverlayFile final : public NativeA32CtrFile {
  public:
    struct Segment {
        uint64_t Start = 0;
        uint64_t Size = 0;
        std::shared_ptr<NativeA32CtrFile> Source;
        uint64_t SourceOffset = 0;
    };

    OverlayFile(std::vector<uint8_t> metadata, std::vector<Segment> segments, uint64_t size)
        : mMetadata(std::move(metadata)), mSegments(std::move(segments)), mSize(size) {}

    bool Read(uint64_t offset, std::span<uint8_t> destination, uint32_t* returnedSize) override {
        if (offset > mSize || destination.size() > mSize - offset) {
            return false;
        }
        std::fill(destination.begin(), destination.end(), uint8_t{0});
        const uint64_t end = offset + destination.size();
        if (offset < mMetadata.size()) {
            const uint64_t count = std::min<uint64_t>(end, mMetadata.size()) - offset;
            std::memcpy(destination.data(), mMetadata.data() + offset, count);
        }
        auto segment = std::upper_bound(mSegments.begin(), mSegments.end(), offset,
                                        [](uint64_t value, const Segment& candidate) {
                                            return value < candidate.Start;
                                        });
        if (segment != mSegments.begin()) {
            --segment;
        }
        for (; segment != mSegments.end() && segment->Start < end; ++segment) {
            const uint64_t from = std::max(offset, segment->Start);
            const uint64_t to = std::min(end, segment->Start + segment->Size);
            if (from >= to) {
                continue;
            }
            uint32_t read = 0;
            const auto part = destination.subspan(from - offset, to - from);
            if (!segment->Source->Read(segment->SourceOffset + (from - segment->Start), part, &read) ||
                read != part.size()) {
                return false;
            }
        }
        if (returnedSize != nullptr) {
            *returnedSize = static_cast<uint32_t>(destination.size());
        }
        return true;
    }
    bool Write(uint64_t, std::span<const uint8_t>, uint32_t*) override { return false; }
    bool Resize(uint64_t) override { return false; }
    uint64_t Size() const noexcept override { return mSize; }

  private:
    std::vector<uint8_t> mMetadata;
    std::vector<Segment> mSegments;
    uint64_t mSize;
};

struct FileNode {
    std::u16string Name;
    std::shared_ptr<NativeA32CtrFile> Source;
    uint64_t SourceOffset = 0;
    uint64_t Size = 0;
};

struct DirectoryNode {
    std::u16string Name;
    std::vector<std::unique_ptr<DirectoryNode>> Directories;
    std::vector<FileNode> Files;
};

uint32_t Read32(const std::vector<uint8_t>& bytes, uint64_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
uint64_t Read64(const std::vector<uint8_t>& bytes, uint64_t offset) {
    uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
void Write32(std::vector<uint8_t>& bytes, uint64_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
void Write64(std::vector<uint8_t>& bytes, uint64_t offset, uint64_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool ReadName(const std::vector<uint8_t>& table, uint64_t offset, uint32_t length,
              std::u16string* name) {
    if ((length & 1U) != 0U || offset > table.size() || length > table.size() - offset) {
        return false;
    }
    name->resize(length / 2U);
    std::memcpy(name->data(), table.data() + offset, length);
    return true;
}

class BaseReader {
  public:
    BaseReader(std::shared_ptr<NativeA32CtrFile> base, std::string* error)
        : mBase(std::move(base)), mError(error) {}

    bool Read(std::unique_ptr<DirectoryNode>* root) {
        std::vector<uint8_t> header(kHeaderSize);
        if (!ReadBytes(0, header)) {
            return false;
        }
        if (Read32(header, 0) != kHeaderSize) {
            SetError(mError, "RomFS level 3 header has an unexpected size");
            return false;
        }
        mFileDataOffset = Read32(header, 0x24);
        mDirectories.resize(Read32(header, 0x10));
        mFiles.resize(Read32(header, 0x20));
        if (!ReadBytes(Read32(header, 0x0C), mDirectories) ||
            !ReadBytes(Read32(header, 0x1C), mFiles)) {
            return false;
        }
        *root = std::make_unique<DirectoryNode>();
        return ReadDirectory(0, **root);
    }

  private:
    bool ReadBytes(uint64_t offset, std::vector<uint8_t>& bytes) {
        uint32_t read = 0;
        if (offset > mBase->Size() || bytes.size() > mBase->Size() - offset ||
            !mBase->Read(offset, bytes, &read) || read != bytes.size()) {
            SetError(mError, "RomFS level 3 is shorter than its header says");
            return false;
        }
        return true;
    }

    bool ReadDirectory(uint32_t offset, DirectoryNode& node) {
        if (++mVisited > (mDirectories.size() + mFiles.size()) ||
            offset > mDirectories.size() || mDirectories.size() - offset < kDirectoryEntrySize) {
            SetError(mError, "RomFS directory table is malformed");
            return false;
        }
        if (!ReadName(mDirectories, offset + kDirectoryEntrySize, Read32(mDirectories, offset + 0x14),
                      &node.Name)) {
            SetError(mError, "RomFS directory name is malformed");
            return false;
        }
        for (uint32_t file = Read32(mDirectories, offset + 0x0C); file != kNone;
             file = Read32(mFiles, file + 0x04)) {
            if (++mVisited > (mDirectories.size() + mFiles.size()) || file > mFiles.size() ||
                mFiles.size() - file < kFileEntrySize) {
                SetError(mError, "RomFS file table is malformed");
                return false;
            }
            FileNode entry;
            if (!ReadName(mFiles, file + kFileEntrySize, Read32(mFiles, file + 0x1C), &entry.Name)) {
                SetError(mError, "RomFS file name is malformed");
                return false;
            }
            entry.Source = mBase;
            entry.SourceOffset = mFileDataOffset + Read64(mFiles, file + 0x08);
            entry.Size = Read64(mFiles, file + 0x10);
            if (entry.SourceOffset > mBase->Size() || entry.Size > mBase->Size() - entry.SourceOffset) {
                SetError(mError, "RomFS file data lies outside the image");
                return false;
            }
            node.Files.push_back(std::move(entry));
        }
        for (uint32_t child = Read32(mDirectories, offset + 0x08); child != kNone;
             child = Read32(mDirectories, child + 0x04)) {
            auto directory = std::make_unique<DirectoryNode>();
            if (!ReadDirectory(child, *directory)) {
                return false;
            }
            node.Directories.push_back(std::move(directory));
        }
        return true;
    }

    std::shared_ptr<NativeA32CtrFile> mBase;
    std::string* mError;
    std::vector<uint8_t> mDirectories;
    std::vector<uint8_t> mFiles;
    uint64_t mFileDataOffset = 0;
    size_t mVisited = 0;
};

uint32_t HashTableCount(uint32_t entries) {
    uint32_t count = entries;
    if (count < 3U) {
        return 3U;
    }
    if (count < 19U) {
        return count | 1U;
    }
    while (count % 2U == 0U || count % 3U == 0U || count % 5U == 0U || count % 7U == 0U ||
           count % 11U == 0U || count % 13U == 0U || count % 17U == 0U) {
        ++count;
    }
    return count;
}

uint32_t NameHash(uint32_t parent, const std::u16string& name) {
    uint32_t hash = parent ^ 123456789U;
    for (const char16_t character : name) {
        hash = ((hash >> 5U) | (hash << 27U)) ^ static_cast<uint16_t>(character);
    }
    return hash;
}

uint32_t NameBytes(const std::u16string& name) {
    return static_cast<uint32_t>(AlignUp(name.size() * 2U, 4U));
}

struct Layout {
    struct Directory {
        DirectoryNode* Node = nullptr;
        uint32_t Offset = 0;
        uint32_t Parent = 0;
        uint32_t Sibling = kNone;
        uint32_t FirstChild = kNone;
        uint32_t FirstFile = kNone;
    };
    struct File {
        const FileNode* Node = nullptr;
        uint32_t Offset = 0;
        uint32_t Parent = 0;
        uint32_t Sibling = kNone;
        uint64_t DataOffset = 0;
    };
    std::vector<Directory> Directories;
    std::vector<File> Files;
    uint32_t DirectoryBytes = 0;
    uint32_t FileBytes = 0;
    uint64_t DataBytes = 0;
};

// Directory entries in the order the title's own image has them: a
// directory's children take consecutive entries, then each child's subtree
// follows in turn (depth-first over sibling groups). Each directory's files
// follow the previous directory's in that order, and their data likewise.
void PlaceChildren(size_t index, Layout& layout) {
    DirectoryNode& node = *layout.Directories[index].Node;
    const uint32_t self = layout.Directories[index].Offset;
    const size_t first = layout.Directories.size();
    for (size_t child = 0; child < node.Directories.size(); ++child) {
        const uint32_t offset = layout.DirectoryBytes;
        if (child == 0U) {
            layout.Directories[index].FirstChild = offset;
        } else {
            layout.Directories.back().Sibling = offset;
        }
        layout.Directories.push_back({node.Directories[child].get(), offset, self});
        layout.DirectoryBytes += kDirectoryEntrySize + NameBytes(node.Directories[child]->Name);
    }
    const size_t last = layout.Directories.size();
    for (size_t child = first; child < last; ++child) {
        PlaceChildren(child, layout);
    }
}

// File entries in depth-first pre-order: a directory's files, then its
// subdirectories' in turn.
void PlaceFiles(const DirectoryNode& node, const std::map<const DirectoryNode*, size_t>& indices,
                Layout& layout) {
    auto& directory = layout.Directories[indices.at(&node)];
    for (size_t file = 0; file < node.Files.size(); ++file) {
        const uint32_t offset = layout.FileBytes;
        if (file == 0U) {
            directory.FirstFile = offset;
        } else {
            layout.Files.back().Sibling = offset;
        }
        layout.DataBytes = AlignUp(layout.DataBytes, kFileDataAlignment);
        layout.Files.push_back({&node.Files[file], offset, directory.Offset, kNone, layout.DataBytes});
        layout.DataBytes += node.Files[file].Size;
        layout.FileBytes += kFileEntrySize + NameBytes(node.Files[file].Name);
    }
    for (const auto& child : node.Directories) {
        PlaceFiles(*child, indices, layout);
    }
}

void PlaceDirectories(DirectoryNode& root, Layout& layout) {
    layout.Directories.push_back({&root, 0U, 0U});
    layout.DirectoryBytes = kDirectoryEntrySize + NameBytes(root.Name);
    PlaceChildren(0U, layout);
    std::sort(layout.Directories.begin(), layout.Directories.end(),
              [](const Layout::Directory& left, const Layout::Directory& right) {
                  return left.Offset < right.Offset;
              });
    std::map<const DirectoryNode*, size_t> indices;
    for (size_t index = 0; index < layout.Directories.size(); ++index) {
        indices.emplace(layout.Directories[index].Node, index);
    }
    PlaceFiles(root, indices, layout);
}

std::vector<uint8_t> Metadata(const Layout& layout, uint64_t* fileDataOffset) {
    const uint32_t directoryBuckets = HashTableCount(static_cast<uint32_t>(layout.Directories.size()));
    const uint32_t fileBuckets = HashTableCount(static_cast<uint32_t>(layout.Files.size()));
    const uint32_t directoryHashOffset = kHeaderSize;
    const uint32_t directoryHashSize = directoryBuckets * 4U;
    const uint32_t directoryMetaOffset = directoryHashOffset + directoryHashSize;
    const uint32_t fileHashOffset = directoryMetaOffset + layout.DirectoryBytes;
    const uint32_t fileHashSize = fileBuckets * 4U;
    const uint32_t fileMetaOffset = fileHashOffset + fileHashSize;
    *fileDataOffset = AlignUp(fileMetaOffset + layout.FileBytes, kFileDataAlignment);
    std::vector<uint8_t> bytes(fileMetaOffset + layout.FileBytes, 0U);
    const uint32_t header[] = {kHeaderSize,         directoryHashOffset, directoryHashSize,
                               directoryMetaOffset, layout.DirectoryBytes, fileHashOffset,
                               fileHashSize,        fileMetaOffset,      layout.FileBytes,
                               static_cast<uint32_t>(*fileDataOffset)};
    std::memcpy(bytes.data(), header, sizeof(header));
    std::fill(bytes.begin() + directoryHashOffset, bytes.begin() + directoryMetaOffset, uint8_t{0xFF});
    std::fill(bytes.begin() + fileHashOffset, bytes.begin() + fileMetaOffset, uint8_t{0xFF});
    for (const auto& directory : layout.Directories) {
        const uint64_t at = directoryMetaOffset + directory.Offset;
        const uint32_t bucket = NameHash(directory.Parent, directory.Node->Name) % directoryBuckets;
        const uint64_t head = directoryHashOffset + bucket * 4U;
        Write32(bytes, at + 0x00, directory.Parent);
        Write32(bytes, at + 0x04, directory.Sibling);
        Write32(bytes, at + 0x08, directory.FirstChild);
        Write32(bytes, at + 0x0C, directory.FirstFile);
        Write32(bytes, at + 0x10, Read32(bytes, head));
        Write32(bytes, at + 0x14, static_cast<uint32_t>(directory.Node->Name.size() * 2U));
        std::memcpy(bytes.data() + at + kDirectoryEntrySize, directory.Node->Name.data(),
                    directory.Node->Name.size() * 2U);
        Write32(bytes, head, directory.Offset);
    }
    for (const auto& file : layout.Files) {
        const uint64_t at = fileMetaOffset + file.Offset;
        const uint32_t bucket = NameHash(file.Parent, file.Node->Name) % fileBuckets;
        const uint64_t head = fileHashOffset + bucket * 4U;
        Write32(bytes, at + 0x00, file.Parent);
        Write32(bytes, at + 0x04, file.Sibling);
        Write64(bytes, at + 0x08, file.DataOffset);
        Write64(bytes, at + 0x10, file.Node->Size);
        Write32(bytes, at + 0x18, Read32(bytes, head));
        Write32(bytes, at + 0x1C, static_cast<uint32_t>(file.Node->Name.size() * 2U));
        std::memcpy(bytes.data() + at + kFileEntrySize, file.Node->Name.data(),
                    file.Node->Name.size() * 2U);
        Write32(bytes, head, file.Offset);
    }
    return bytes;
}

bool ApplyLayer(DirectoryNode& root, const std::filesystem::path& layer,
                NativeA32RomFsOverlay& overlay, std::string* error) {
    std::error_code code;
    if (!std::filesystem::is_directory(layer, code)) {
        SetError(error, "RomFS overlay layer is not a directory: " + layer.string());
        return false;
    }
    // A fixed order, whatever the host's directory enumeration.
    std::vector<std::filesystem::path> files;
    for (auto entry = std::filesystem::recursive_directory_iterator(layer, code);
         !code && entry != std::filesystem::recursive_directory_iterator(); entry.increment(code)) {
        if (entry->is_regular_file(code)) {
            files.push_back(entry->path());
        }
    }
    if (code) {
        SetError(error, "cannot list RomFS overlay layer " + layer.string() + ": " + code.message());
        return false;
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        const auto relative = path.lexically_relative(layer);
        std::vector<std::u16string> components;
        for (const auto& component : relative) {
            components.push_back(component.u16string());
        }
        DirectoryNode* directory = &root;
        for (size_t index = 0; index + 1U < components.size(); ++index) {
            auto found = std::find_if(directory->Directories.begin(), directory->Directories.end(),
                                      [&](const auto& child) { return child->Name == components[index]; });
            if (found == directory->Directories.end()) {
                auto created = std::make_unique<DirectoryNode>();
                created->Name = components[index];
                directory->Directories.push_back(std::move(created));
                found = std::prev(directory->Directories.end());
                ++overlay.AddedDirectories;
            }
            directory = found->get();
        }
        const uint64_t size = std::filesystem::file_size(path, code);
        std::ifstream stream(path, std::ios::binary);
        if (code || !stream) {
            SetError(error, "cannot read RomFS overlay file " + path.string());
            return false;
        }
        FileNode file{components.back(), std::make_shared<HostRangeFile>(std::move(stream), 0U, size), 0U,
                      size};
        auto existing = std::find_if(directory->Files.begin(), directory->Files.end(),
                                     [&](const FileNode& candidate) { return candidate.Name == file.Name; });
        if (existing != directory->Files.end()) {
            *existing = std::move(file);
            ++overlay.ReplacedFiles;
        } else {
            directory->Files.push_back(std::move(file));
            ++overlay.AddedFiles;
        }
    }
    return true;
}

} // namespace

std::shared_ptr<NativeA32CtrFile> OpenNativeA32HostFileRange(const std::filesystem::path& path,
                                                             uint64_t offset, uint64_t size,
                                                             std::string* error) {
    std::error_code code;
    const uint64_t fileSize = std::filesystem::file_size(path, code);
    std::ifstream stream(path, std::ios::binary);
    if (code || !stream || offset > fileSize || size > fileSize - offset) {
        SetError(error, "cannot open " + path.string() + " for the requested range");
        return nullptr;
    }
    return std::make_shared<HostRangeFile>(std::move(stream), offset, size);
}

std::optional<NativeA32RomFsOverlay> BuildNativeA32RomFsOverlay(
    std::shared_ptr<NativeA32CtrFile> base, std::span<const std::filesystem::path> layers,
    std::string* error) {
    if (base == nullptr) {
        SetError(error, "RomFS overlay has no base image");
        return std::nullopt;
    }
    std::unique_ptr<DirectoryNode> root;
    if (!BaseReader(base, error).Read(&root)) {
        return std::nullopt;
    }
    NativeA32RomFsOverlay overlay;
    for (const auto& layer : layers) {
        if (!ApplyLayer(*root, layer, overlay, error)) {
            return std::nullopt;
        }
    }
    Layout layout;
    PlaceDirectories(*root, layout);
    uint64_t fileDataOffset = 0;
    auto metadata = Metadata(layout, &fileDataOffset);
    std::vector<OverlayFile::Segment> segments;
    segments.reserve(layout.Files.size());
    for (const auto& file : layout.Files) {
        if (file.Node->Size != 0U) {
            segments.push_back({fileDataOffset + file.DataOffset, file.Node->Size, file.Node->Source,
                                file.Node->SourceOffset});
        }
    }
    overlay.File = std::make_shared<OverlayFile>(std::move(metadata), std::move(segments),
                                                 fileDataOffset + layout.DataBytes);
    return overlay;
}

} // namespace Oot3dNativeGame
