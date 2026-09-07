#include "three_ds_recomp/oot3d/Oot3dNativeQdbProvider.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr uint32_t kNativeQdbMagic = 0x51444220;
constexpr uint32_t kNativeQdbVersion = 3;

void Require(std::span<const uint8_t> bytes, size_t offset, size_t size, std::string_view sourceName) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw std::runtime_error("OOT3D QDB read outside " + std::string(sourceName) + " at " +
                                 std::to_string(offset) + " size " + std::to_string(size));
    }
}

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t offset, std::string_view sourceName) {
    Require(bytes, offset, 2, sourceName);
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(bytes[offset + 1] << 8);
}

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t offset, std::string_view sourceName) {
    Require(bytes, offset, 4, sourceName);
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

int32_t ReadS32(std::span<const uint8_t> bytes, size_t offset, std::string_view sourceName) {
    return static_cast<int32_t>(ReadU32(bytes, offset, sourceName));
}

size_t CheckedAdd(size_t base, size_t count, size_t stride, std::string_view sourceName) {
    if (count > (std::numeric_limits<size_t>::max() - base) / stride) {
        throw std::runtime_error("OOT3D QDB command size overflow in " + std::string(sourceName));
    }
    return base + count * stride;
}

int32_t ReadEntryCount(std::span<const uint8_t> bytes, size_t offset, std::string_view sourceName) {
    const int32_t count = ReadS32(bytes, offset, sourceName);
    if (count < 0 || count > 10000) {
        throw std::runtime_error("Implausible OOT3D QDB entry count in " + std::string(sourceName));
    }
    return count;
}

NativeQdbCommandCategory CategoryFor(int32_t commandId) {
    switch (commandId) {
        case 0:
            return NativeQdbCommandCategory::NoOp;
        case 0x0001:
        case 0x0002:
        case 0x0005:
        case 0x0006:
            return NativeQdbCommandCategory::CameraList;
        case 0x0007:
        case 0x0008:
            return NativeQdbCommandCategory::SingleCameraFixed;
        case 0x0009:
            return NativeQdbCommandCategory::PackedThreeWordPairs;
        case 0x0013:
        case 0x008C:
            return NativeQdbCommandCategory::CountedThreeWordEntries;
        case 0x002D:
        case 0x03E8:
            return NativeQdbCommandCategory::Fixed16;
        case 0x0096:
            return NativeQdbCommandCategory::Blob16BitCount;
        case 0x0097:
            return NativeQdbCommandCategory::BlobU32Size;
        default:
            return NativeQdbCommandCategory::CountedTwelveWordEntries;
    }
}

NativeQdbCommand ParseCommand(std::span<const uint8_t> bytes, size_t offset, uint32_t index,
                              std::string_view sourceName) {
    NativeQdbCommand command;
    command.Index = index;
    command.Offset = offset;
    command.CommandId = ReadS32(bytes, offset, sourceName);
    command.Category = CategoryFor(command.CommandId);
    size_t totalSize = 0;
    switch (command.Category) {
        case NativeQdbCommandCategory::NoOp:
            totalSize = 4;
            break;
        case NativeQdbCommandCategory::CameraList: {
            size_t cursor = offset + 0xC;
            while (true) {
                Require(bytes, cursor, 0x10, sourceName);
                ++command.CameraPointCount;
                const auto continueFlag = static_cast<int8_t>(bytes[cursor]);
                cursor += 0x10;
                if (continueFlag == -1) {
                    break;
                }
                if (command.CameraPointCount > 500) {
                    throw std::runtime_error("Unterminated OOT3D QDB camera list in " +
                                             std::string(sourceName));
                }
            }
            totalSize = cursor - offset;
            break;
        }
        case NativeQdbCommandCategory::SingleCameraFixed:
            command.CameraPointCount = 1;
            totalSize = 0x1C;
            break;
        case NativeQdbCommandCategory::PackedThreeWordPairs: {
            command.EntryCount = ReadEntryCount(bytes, offset + 4, sourceName);
            const size_t count = static_cast<size_t>(command.EntryCount);
            if (count < 1) {
                totalSize = 8;
            } else if ((count & 1U) != 0) {
                totalSize = CheckedAdd(0x14, (count - 1) / 2, 0x18, sourceName);
            } else {
                totalSize = CheckedAdd(8, count / 2, 0x18, sourceName);
            }
            break;
        }
        case NativeQdbCommandCategory::CountedThreeWordEntries:
            command.EntryCount = ReadEntryCount(bytes, offset + 4, sourceName);
            totalSize = CheckedAdd(8, static_cast<size_t>(command.EntryCount), 0xC, sourceName);
            break;
        case NativeQdbCommandCategory::Fixed16:
            totalSize = 0x10;
            break;
        case NativeQdbCommandCategory::Blob16BitCount:
            command.EntryCount = ReadU16(bytes, offset + 0xA, sourceName);
            command.BlobSize = static_cast<int64_t>(command.EntryCount) * 0x20;
            totalSize = CheckedAdd(0xC, static_cast<size_t>(command.EntryCount), 0x20, sourceName);
            break;
        case NativeQdbCommandCategory::BlobU32Size:
            command.BlobSize = ReadU32(bytes, offset + 4, sourceName);
            totalSize = CheckedAdd(8, static_cast<size_t>(command.BlobSize), 1, sourceName);
            break;
        case NativeQdbCommandCategory::CountedTwelveWordEntries:
            command.EntryCount = ReadEntryCount(bytes, offset + 4, sourceName);
            totalSize = CheckedAdd(8, static_cast<size_t>(command.EntryCount), 0x30, sourceName);
            break;
    }
    Require(bytes, offset, totalSize, sourceName);
    command.TotalSize = totalSize;
    command.PayloadSize = totalSize - 4;
    return command;
}

} // namespace

NativeQdbTimeline ParseNativeQdb(std::span<const uint8_t> bytes, std::string_view sourceName) {
    Require(bytes, 0, 0x10, sourceName);
    if (ReadU32(bytes, 0, sourceName) != kNativeQdbMagic) {
        throw std::runtime_error("Invalid native OOT3D QDB magic in " + std::string(sourceName));
    }
    NativeQdbTimeline timeline;
    timeline.VersionOrFlags = ReadU32(bytes, 4, sourceName);
    if (timeline.VersionOrFlags != kNativeQdbVersion) {
        throw std::runtime_error("Unsupported native OOT3D QDB version in " + std::string(sourceName));
    }
    timeline.CommandCount = ReadS32(bytes, 8, sourceName);
    timeline.EndFrame = ReadS32(bytes, 0xC, sourceName);
    if (timeline.CommandCount < 0 || timeline.CommandCount > 1000 ||
        timeline.EndFrame < 0 || timeline.EndFrame > 200000) {
        throw std::runtime_error("Implausible native OOT3D QDB header in " + std::string(sourceName));
    }
    timeline.Commands.reserve(static_cast<size_t>(timeline.CommandCount));
    size_t cursor = timeline.CommandStreamOffset;
    for (int32_t index = 0; index < timeline.CommandCount; ++index) {
        auto command = ParseCommand(bytes, cursor, static_cast<uint32_t>(index), sourceName);
        cursor += command.TotalSize;
        timeline.Commands.push_back(std::move(command));
    }
    Require(bytes, cursor, 4, sourceName);
    if (ReadU32(bytes, cursor, sourceName) != 0xFFFFFFFFU) {
        throw std::runtime_error("Native OOT3D QDB terminator missing in " + std::string(sourceName));
    }
    for (size_t index = cursor + 4; index < bytes.size(); ++index) {
        if (bytes[index] != 0) {
            throw std::runtime_error("Native OOT3D QDB alignment contains data in " +
                                     std::string(sourceName));
        }
    }
    timeline.DecodedSize = cursor;
    timeline.TrailerSize = bytes.size() - cursor;
    return timeline;
}

std::string_view NativeQdbCommandCategoryName(NativeQdbCommandCategory category) {
    switch (category) {
        case NativeQdbCommandCategory::NoOp: return "noop";
        case NativeQdbCommandCategory::CameraList: return "camera_list";
        case NativeQdbCommandCategory::SingleCameraFixed: return "single_camera_fixed";
        case NativeQdbCommandCategory::PackedThreeWordPairs: return "packed_3word_pairs";
        case NativeQdbCommandCategory::CountedThreeWordEntries: return "counted_3word_entries";
        case NativeQdbCommandCategory::Fixed16: return "fixed16";
        case NativeQdbCommandCategory::Blob16BitCount: return "blob_16bit_count";
        case NativeQdbCommandCategory::BlobU32Size: return "blob_u32_size";
        case NativeQdbCommandCategory::CountedTwelveWordEntries: return "counted_12word_entries";
    }
    return "unknown";
}

NativeQdbProvider::NativeQdbProvider(NativeSourceProvider::FileLoader loader)
    : mSources(std::move(loader)) {
}

std::shared_ptr<const NativeQdbAsset> NativeQdbProvider::Load(const AssetCatalogRecord& record) {
    if (record.Family != "cutscene_timeline" || record.CanonicalResources.size() != 1) {
        throw std::invalid_argument("OOT3D QDB provider requires one native cutscene_timeline resource");
    }
    std::scoped_lock lock(mMutex);
    if (const auto cached = mCache.find(record.AssetId); cached != mCache.end()) {
        return cached->second;
    }
    const auto source = mSources.Load(record.CanonicalResources.front());
    if (source == nullptr || source->Bytes == nullptr) {
        return nullptr;
    }
    auto asset = std::make_shared<NativeQdbAsset>();
    asset->CatalogRecord = &record;
    asset->Source = source;
    asset->Timeline = ParseNativeQdb(*source->Bytes, source->ResourcePath);
    mCache.emplace(record.AssetId, asset);
    return asset;
}

void NativeQdbProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mCache.clear();
    mSources.Clear();
}

} // namespace ThreeDsRecomp::Oot3d
