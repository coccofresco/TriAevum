#include "oot3d_native_a32_romfs_overlay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace Oot3dNativeGame;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class MemoryFile final : public NativeA32CtrFile {
  public:
    explicit MemoryFile(std::vector<uint8_t> bytes) : mBytes(std::move(bytes)) {}
    bool Read(uint64_t offset, std::span<uint8_t> destination, uint32_t* returned) override {
        if (offset > mBytes.size() || destination.size() > mBytes.size() - offset) {
            return false;
        }
        std::memcpy(destination.data(), mBytes.data() + offset, destination.size());
        *returned = static_cast<uint32_t>(destination.size());
        return true;
    }
    bool Write(uint64_t, std::span<const uint8_t>, uint32_t*) override { return false; }
    bool Resize(uint64_t) override { return false; }
    uint64_t Size() const noexcept override { return mBytes.size(); }

  private:
    std::vector<uint8_t> mBytes;
};

std::vector<uint8_t> ReadAll(NativeA32CtrFile& file, uint64_t offset, uint64_t size) {
    std::vector<uint8_t> bytes(size);
    uint32_t returned = 0;
    Require(file.Read(offset, bytes, &returned) && returned == size, "short read");
    return bytes;
}

uint32_t U32(NativeA32CtrFile& file, uint64_t offset) {
    const auto bytes = ReadAll(file, offset, 4);
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), 4);
    return value;
}

// The title's lookup: from the root, each component through the hash table
// of its kind, matching parent and name along the bucket chain.
std::optional<std::vector<uint8_t>> Lookup(NativeA32CtrFile& romfs, const std::u16string& path) {
    const uint32_t directoryHash = U32(romfs, 0x04), directoryBuckets = U32(romfs, 0x08) / 4U;
    const uint32_t directoryMeta = U32(romfs, 0x0C);
    const uint32_t fileHash = U32(romfs, 0x14), fileBuckets = U32(romfs, 0x18) / 4U;
    const uint32_t fileMeta = U32(romfs, 0x1C), fileData = U32(romfs, 0x24);
    const auto hash = [](uint32_t parent, const std::u16string& name) {
        uint32_t value = parent ^ 123456789U;
        for (const char16_t c : name) {
            value = ((value >> 5U) | (value << 27U)) ^ static_cast<uint16_t>(c);
        }
        return value;
    };
    const auto nameAt = [&](uint64_t at, uint32_t length) {
        const auto bytes = ReadAll(romfs, at, length);
        std::u16string name(length / 2U, u'\0');
        std::memcpy(name.data(), bytes.data(), length);
        return name;
    };
    uint32_t directory = 0;
    size_t start = 1;
    while (true) {
        const size_t slash = path.find(u'/', start);
        const std::u16string name = path.substr(start, slash == std::u16string::npos ? slash : slash - start);
        if (slash == std::u16string::npos) {
            for (uint32_t file = U32(romfs, fileHash + hash(directory, name) % fileBuckets * 4U);
                 file != 0xFFFFFFFFU; file = U32(romfs, fileMeta + file + 0x18)) {
                const uint64_t at = fileMeta + file;
                if (U32(romfs, at) == directory && nameAt(at + 0x20, U32(romfs, at + 0x1C)) == name) {
                    const auto offset = ReadAll(romfs, at + 0x08, 8), size = ReadAll(romfs, at + 0x10, 8);
                    uint64_t dataOffset = 0, dataSize = 0;
                    std::memcpy(&dataOffset, offset.data(), 8);
                    std::memcpy(&dataSize, size.data(), 8);
                    return ReadAll(romfs, fileData + dataOffset, dataSize);
                }
            }
            return std::nullopt;
        }
        bool found = false;
        for (uint32_t child = U32(romfs, directoryHash + hash(directory, name) % directoryBuckets * 4U);
             child != 0xFFFFFFFFU; child = U32(romfs, directoryMeta + child + 0x10)) {
            const uint64_t at = directoryMeta + child;
            if (U32(romfs, at) == directory && nameAt(at + 0x18, U32(romfs, at + 0x14)) == name) {
                directory = child;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
        start = slash + 1U;
    }
}

// A level 3 with only its root directory.
std::vector<uint8_t> EmptyRomFs() {
    std::vector<uint8_t> bytes(0x60, 0U);
    const uint32_t header[] = {0x28, 0x28, 0x0C, 0x34, 0x18, 0x4C, 0x0C, 0x58, 0x00, 0x60};
    std::memcpy(bytes.data(), header, sizeof(header));
    const uint32_t buckets[] = {0, 0xFFFFFFFFU, 0xFFFFFFFFU};
    // The root (parent 0, empty name) hashes to bucket 123456789 % 3 == 0.
    std::memcpy(bytes.data() + 0x28, buckets, sizeof(buckets));
    const uint32_t root[] = {0, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0};
    std::memcpy(bytes.data() + 0x34, root, sizeof(root));
    std::memset(bytes.data() + 0x4C, 0xFF, 0x0C);
    return bytes;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

std::vector<uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }

void TestLayers(const std::filesystem::path& scratch) {
    const auto first = scratch / "first", second = scratch / "second";
    WriteFile(first / "layout" / "Joker.Main.New.gar", "hud layout");
    WriteFile(first / "layout" / "common.gar", "first common");
    WriteFile(first / "top.bin", "top level");
    WriteFile(second / "layout" / "common.gar", "second common, longer");
    WriteFile(second / "a" / "b" / "c" / "deep.bin", "deep");
    const std::filesystem::path layers[] = {first, second};
    std::string error;
    auto overlay = BuildNativeA32RomFsOverlay(std::make_shared<MemoryFile>(EmptyRomFs()), layers, &error);
    Require(overlay.has_value(), "overlay failed: " + error);
    Require(overlay->AddedFiles == 4U && overlay->ReplacedFiles == 1U && overlay->AddedDirectories == 4U,
            "unexpected overlay counts");
    auto& romfs = *overlay->File;
    Require(Lookup(romfs, u"/layout/Joker.Main.New.gar") == Bytes("hud layout"), "added file");
    Require(Lookup(romfs, u"/layout/common.gar") == Bytes("second common, longer"), "later layer wins");
    Require(Lookup(romfs, u"/top.bin") == Bytes("top level"), "root file");
    Require(Lookup(romfs, u"/a/b/c/deep.bin") == Bytes("deep"), "nested directories");
    Require(!Lookup(romfs, u"/layout/missing.gar").has_value(), "missing file found");

    // Layering the result again keeps every file and replaces in place.
    WriteFile(scratch / "third" / "top.bin", "replaced");
    const std::filesystem::path third[] = {scratch / "third"};
    auto again = BuildNativeA32RomFsOverlay(overlay->File, third, &error);
    Require(again.has_value() && again->ReplacedFiles == 1U && again->AddedFiles == 0U, "re-layer: " + error);
    Require(Lookup(*again->File, u"/top.bin") == Bytes("replaced") &&
                Lookup(*again->File, u"/a/b/c/deep.bin") == Bytes("deep"),
            "re-layered image lost a file");

    // A malformed base is refused.
    auto broken = EmptyRomFs();
    broken[0] = 0x30;
    Require(!BuildNativeA32RomFsOverlay(std::make_shared<MemoryFile>(broken), {}, &error).has_value(),
            "malformed base accepted");
}

// With a real image (OOT3D_ROMFS_OVERLAY_IMAGE=path, level 3 at 0x1000): the
// rebuilt metadata serves every file the base serves, byte for byte.
void TestImage(const char* image, const std::filesystem::path& scratch) {
    const auto size = std::filesystem::file_size(image);
    std::string error;
    auto base = OpenNativeA32HostFileRange(image, 0x1000U, size - 0x1000U, &error);
    Require(base != nullptr, error);
    WriteFile(scratch / "image" / "layout" / "Overlay.Test.gar", "overlay test");
    const std::filesystem::path layers[] = {scratch / "image"};
    auto overlay = BuildNativeA32RomFsOverlay(base, layers, &error);
    Require(overlay.has_value(), "image overlay failed: " + error);
    auto empty = BuildNativeA32RomFsOverlay(base, {}, &error);
    Require(empty.has_value(), error);
    Require(Lookup(*overlay->File, u"/layout/Overlay.Test.gar") == Bytes("overlay test"), "image added file");
    // Same tables as the base's own when nothing is layered.
    const uint32_t metaEnd = U32(*base, 0x1C) + U32(*base, 0x20);
    const auto rebuilt = ReadAll(*empty->File, 0, metaEnd), original = ReadAll(*base, 0, metaEnd);
    const auto differs = std::mismatch(rebuilt.begin(), rebuilt.end(), original.begin());
    Require(differs.first == rebuilt.end(),
            "rebuilt tables differ from the image's at 0x" +
                [&] {
                    const size_t at = static_cast<size_t>(differs.first - rebuilt.begin()) & ~size_t{3};
                    char text[64];
                    std::snprintf(text, sizeof(text), "%zx: %08x, image %08x", at, U32(*empty->File, at),
                                  U32(*base, at));
                    return std::string(text);
                }());
    // Level 3 only (the image's IVFC hash levels follow it), its file data
    // where the image has it.
    const uint64_t level3 = empty->File->Size(), data = U32(*base, 0x24);
    Require(level3 <= base->Size() && level3 > data, "rebuilt image size is wrong");
    const uint64_t window = std::min<uint64_t>(level3 - data, 1U << 20U);
    Require(ReadAll(*empty->File, data, window) == ReadAll(*base, data, window) &&
                ReadAll(*empty->File, level3 - window, window) == ReadAll(*base, level3 - window, window),
            "rebuilt file data differs from the image's");
    std::cout << "image: " << overlay->File->Size() << " bytes, " << overlay->AddedFiles << " added\n";
}

} // namespace

int main() {
    try {
        const auto scratch = std::filesystem::temp_directory_path() / "oot3d_romfs_overlay_tests";
        std::filesystem::remove_all(scratch);
        TestLayers(scratch);
        if (const char* image = std::getenv("OOT3D_ROMFS_OVERLAY_IMAGE")) {
            TestImage(image, scratch);
        }
        std::filesystem::remove_all(scratch);
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_a32_romfs_overlay_tests: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "oot3d_native_a32_romfs_overlay_tests: ok\n";
    return 0;
}
