#include "oot3d_native_a32_romfs_overlay.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace Oot3dNativeGame {
namespace {

constexpr size_t kHeaderBytes = 64U;
constexpr size_t kPatchRecordBytes = 32U;

uint32_t Le32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

uint64_t Le64(const uint8_t *bytes) {
    return static_cast<uint64_t>(Le32(bytes)) | (static_cast<uint64_t>(Le32(bytes + 4)) << 32U);
}

bool ReadAt(std::ifstream &stream, uint64_t offset, std::span<uint8_t> bytes) {
    if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) return false;
    if (bytes.empty()) return true;
    stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

bool Fail(std::string *error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

} // namespace

std::shared_ptr<const NativeA32RomFsOverlay> NativeA32RomFsOverlay::Load(
    const std::filesystem::path &path, uint64_t baseViewSize, const BaseReader &base,
    std::string *error) {
    const std::string prefix = "RomFS overlay " + path.string() + ": ";
    std::error_code sizeError;
    const uint64_t fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        Fail(error, prefix + "cannot be read");
        return nullptr;
    }
    std::ifstream stream(path, std::ios::binary);
    std::array<uint8_t, kHeaderBytes> header{};
    if (!stream || fileSize < kHeaderBytes || !ReadAt(stream, 0U, header)) {
        Fail(error, prefix + "is truncated");
        return nullptr;
    }
    if (std::memcmp(header.data(), "O3RO", 4U) != 0 || Le32(header.data() + 4) != kVersion) {
        Fail(error, prefix + "has an unsupported format");
        return nullptr;
    }
    std::shared_ptr<NativeA32RomFsOverlay> overlay(new NativeA32RomFsOverlay());
    overlay->mPath = path;
    overlay->mBaseViewSize = Le64(header.data() + 8);
    overlay->mViewSize = Le64(header.data() + 16);
    const uint32_t patchCount = Le32(header.data() + 24);
    overlay->mTailFileOffset = Le64(header.data() + 32);
    bool reservedClear = Le32(header.data() + 28) == 0U;
    for (size_t offset = 40U; offset < kHeaderBytes; offset += 8U)
        reservedClear = reservedClear && Le64(header.data() + offset) == 0U;
    if (!reservedClear) {
        Fail(error, prefix + "has nonzero reserved fields");
        return nullptr;
    }
    if (overlay->mBaseViewSize != baseViewSize) {
        Fail(error, prefix + "was built for another RomFS image; run Forge again");
        return nullptr;
    }
    if (overlay->mViewSize < overlay->mBaseViewSize || patchCount > kMaximumPatchCount) {
        Fail(error, prefix + "has an invalid layout");
        return nullptr;
    }
    const uint64_t tailBytes = overlay->mViewSize - overlay->mBaseViewSize;
    const uint64_t tableEnd = kHeaderBytes + static_cast<uint64_t>(patchCount) * kPatchRecordBytes;
    if (overlay->mTailFileOffset < tableEnd || overlay->mTailFileOffset > fileSize ||
        fileSize - overlay->mTailFileOffset != tailBytes) {
        Fail(error, prefix + "has an invalid tail");
        return nullptr;
    }
    std::vector<uint8_t> table(static_cast<size_t>(tableEnd - kHeaderBytes));
    if (!ReadAt(stream, kHeaderBytes, table)) {
        Fail(error, prefix + "has a truncated patch table");
        return nullptr;
    }
    uint64_t previousEnd = 0U;
    std::vector<uint8_t> expected;
    std::vector<uint8_t> actual;
    for (uint32_t index = 0U; index < patchCount; ++index) {
        const uint8_t *record = table.data() + static_cast<size_t>(index) * kPatchRecordBytes;
        const uint64_t viewOffset = Le64(record);
        const uint32_t length = Le32(record + 8);
        const uint64_t expectedOffset = Le64(record + 16);
        const uint64_t replacementOffset = Le64(record + 24);
        const auto inData = [&](uint64_t offset) {
            return offset >= tableEnd && offset <= overlay->mTailFileOffset &&
                   length <= overlay->mTailFileOffset - offset;
        };
        if (Le32(record + 12) != 0U || length == 0U || length > kMaximumPatchLength ||
            viewOffset < previousEnd || viewOffset > overlay->mBaseViewSize ||
            length > overlay->mBaseViewSize - viewOffset || !inData(expectedOffset) ||
            !inData(replacementOffset)) {
            Fail(error, prefix + "has an invalid patch record");
            return nullptr;
        }
        previousEnd = viewOffset + length;
        expected.resize(length);
        actual.resize(length);
        Patch patch{viewOffset, std::vector<uint8_t>(length)};
        if (!ReadAt(stream, expectedOffset, expected) || !ReadAt(stream, replacementOffset, patch.Bytes)) {
            Fail(error, prefix + "has truncated patch data");
            return nullptr;
        }
        if (!base(viewOffset, actual)) {
            Fail(error, prefix + "cannot read the RomFS image it patches");
            return nullptr;
        }
        if (actual != expected) {
            Fail(error, prefix + "does not match this RomFS image; run Forge again");
            return nullptr;
        }
        overlay->mPatches.push_back(std::move(patch));
    }
    return overlay;
}

bool NativeA32RomFsOverlay::ReadTail(uint64_t offset, std::span<uint8_t> bytes) const {
    std::scoped_lock lock(mTailMutex);
    if (!mTail.is_open()) {
        mTail.clear();
        mTail.open(mPath, std::ios::binary);
        if (!mTail.is_open()) return false;
    }
    return ReadAt(mTail, mTailFileOffset + offset, bytes);
}

bool NativeA32RomFsOverlay::Read(uint64_t offset, std::span<uint8_t> bytes,
                                 const BaseReader &base) const {
    if (offset > mViewSize || bytes.size() > mViewSize - offset) return false;
    const uint64_t end = offset + bytes.size();
    if (offset < mBaseViewSize) {
        const uint64_t baseEnd = std::min(end, mBaseViewSize);
        const auto baseBytes = bytes.first(static_cast<size_t>(baseEnd - offset));
        if (!base(offset, baseBytes)) return false;
        const auto first = std::upper_bound(
            mPatches.begin(), mPatches.end(), offset,
            [](uint64_t value, const Patch &patch) { return value < patch.Offset + patch.Bytes.size(); });
        for (auto patch = first; patch != mPatches.end() && patch->Offset < baseEnd; ++patch) {
            const uint64_t from = std::max(offset, patch->Offset);
            const uint64_t to = std::min(baseEnd, patch->Offset + patch->Bytes.size());
            std::memcpy(baseBytes.data() + (from - offset), patch->Bytes.data() + (from - patch->Offset),
                        static_cast<size_t>(to - from));
        }
    }
    if (end > mBaseViewSize) {
        const uint64_t tailStart = std::max(offset, mBaseViewSize);
        if (!ReadTail(tailStart - mBaseViewSize, bytes.subspan(static_cast<size_t>(tailStart - offset))))
            return false;
    }
    return true;
}

} // namespace Oot3dNativeGame
