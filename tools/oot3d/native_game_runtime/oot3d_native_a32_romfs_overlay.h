#pragma once

// RomFS overlay: a Level-3 RomFS service view composed from the user's
// unmodified image plus a Forge-built O3RO file.
//
// The overlay never moves base bytes. It replaces a few small base ranges (the
// file-metadata fields of replaced files: payload offset and size) and appends
// a tail after the base view that holds the replacement payloads. Metadata the
// guest cached from the base view therefore stays valid. Each replaced base
// range carries its expected original bytes, so an overlay built for another
// image is rejected when it is loaded, before the guest reads anything.
//
// O3RO v1, little-endian:
//   0  char[4] "O3RO"            4  u32 version (1)
//   8  u64 base view size       16  u64 view size (>= base view size)
//  24  u32 patch count          28  u32 reserved (0)
//  32  u64 tail file offset     40  u64[3] reserved (0)
//  64  patch records, 32 bytes each, sorted and disjoint, inside the base view:
//        u64 view offset, u32 length, u32 reserved (0),
//        u64 expected-bytes file offset, u64 replacement-bytes file offset
//  The tail holds view bytes [base view size, view size) and ends the file.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32RomFsOverlay {
  public:
    // Reads base view bytes [offset, offset + bytes.size()); false on failure.
    using BaseReader = std::function<bool(uint64_t offset, std::span<uint8_t> bytes)>;

    static constexpr uint32_t kVersion = 1U;
    static constexpr uint32_t kMaximumPatchCount = 65536U;
    static constexpr uint32_t kMaximumPatchLength = 64U * 1024U;

    // Validates the file against the base view (size and expected bytes).
    static std::shared_ptr<const NativeA32RomFsOverlay> Load(
        const std::filesystem::path &path, uint64_t baseViewSize,
        const BaseReader &base, std::string *error);

    uint64_t BaseViewSize() const noexcept { return mBaseViewSize; }
    uint64_t ViewSize() const noexcept { return mViewSize; }
    size_t PatchCount() const noexcept { return mPatches.size(); }

    // Composes view bytes [offset, offset + bytes.size()), which must lie in
    // the view. Base ranges come from `base`, then the patches apply.
    bool Read(uint64_t offset, std::span<uint8_t> bytes, const BaseReader &base) const;

  private:
    struct Patch {
        uint64_t Offset = 0U;
        std::vector<uint8_t> Bytes;
    };

    NativeA32RomFsOverlay() = default;
    bool ReadTail(uint64_t offset, std::span<uint8_t> bytes) const;

    std::filesystem::path mPath;
    uint64_t mBaseViewSize = 0U;
    uint64_t mViewSize = 0U;
    uint64_t mTailFileOffset = 0U;
    std::vector<Patch> mPatches;
    mutable std::ifstream mTail;
    mutable std::mutex mTailMutex;
};

} // namespace Oot3dNativeGame
