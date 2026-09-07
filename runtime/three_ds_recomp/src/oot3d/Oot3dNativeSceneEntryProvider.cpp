#include "three_ds_recomp/oot3d/Oot3dNativeSceneEntryProvider.h"

#include <algorithm>
#include <stdexcept>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr size_t kFirstSetupOffset = 0x18;
constexpr size_t kCommandSize = 8;
constexpr size_t kNativeSectionPrefixSize = 0x10;
constexpr size_t kActorEntrySize = 0x10;
constexpr size_t kEntranceEntrySize = 2;
constexpr uint8_t kSpawnListCommand = 0x00;
constexpr uint8_t kEntranceListCommand = 0x06;
constexpr uint8_t kEndCommand = 0x14;
constexpr uint8_t kFirstCommand = 0x15;
constexpr int32_t kPlayerActorId = 0;

struct SceneCommand {
    uint8_t Id = 0;
    uint8_t Count = 0;
    uint32_t Argument = 0;
};

bool CanRead(std::span<const uint8_t> bytes, size_t offset, size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

uint16_t ReadLeU16(std::span<const uint8_t> bytes, size_t offset) {
    if (!CanRead(bytes, offset, 2)) {
        throw std::runtime_error("truncated native ZSI u16");
    }
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

int16_t ReadLeS16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<int16_t>(ReadLeU16(bytes, offset));
}

uint32_t ReadLeU32(std::span<const uint8_t> bytes, size_t offset) {
    if (!CanRead(bytes, offset, 4)) {
        throw std::runtime_error("truncated native ZSI u32");
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

SceneCommand ReadCommand(std::span<const uint8_t> bytes, size_t offset) {
    const auto word = ReadLeU32(bytes, offset);
    return {
        static_cast<uint8_t>(word & 0xFF),
        static_cast<uint8_t>((word >> 8) & 0xFF),
        ReadLeU32(bytes, offset + 4),
    };
}

size_t FindSetupOffset(std::span<const uint8_t> bytes, int32_t requestedSetup) {
    if (requestedSetup < 0) {
        throw std::runtime_error("native ZSI setup index is negative");
    }
    size_t setupOffset = kFirstSetupOffset;
    for (int32_t setupIndex = 0; setupIndex <= requestedSetup; ++setupIndex) {
        if (!CanRead(bytes, setupOffset, kCommandSize) ||
            ReadCommand(bytes, setupOffset).Id != kFirstCommand) {
            throw std::runtime_error("native ZSI setup is absent");
        }
        if (setupIndex == requestedSetup) {
            return setupOffset;
        }
        bool ended = false;
        for (size_t commandOffset = setupOffset; CanRead(bytes, commandOffset, kCommandSize);
             commandOffset += kCommandSize) {
            if (ReadCommand(bytes, commandOffset).Id == kEndCommand) {
                setupOffset = commandOffset + kCommandSize;
                ended = true;
                break;
            }
        }
        if (!ended) {
            throw std::runtime_error("native ZSI setup has no end command");
        }
    }
    throw std::runtime_error("native ZSI setup is absent");
}

SceneCommand FindUniqueCommand(std::span<const uint8_t> bytes, size_t setupOffset,
                               uint8_t requestedId) {
    bool found = false;
    bool ended = false;
    SceneCommand result;
    for (size_t offset = setupOffset; CanRead(bytes, offset, kCommandSize);
         offset += kCommandSize) {
        const auto command = ReadCommand(bytes, offset);
        if (command.Id == requestedId) {
            if (found) {
                throw std::runtime_error("native ZSI setup has duplicate required commands");
            }
            result = command;
            found = true;
        }
        if (command.Id == kEndCommand) {
            ended = true;
            break;
        }
    }
    if (!ended) {
        throw std::runtime_error("native ZSI setup has no end command");
    }
    if (!found) {
        throw std::runtime_error("native ZSI setup lacks a required command");
    }
    return result;
}

size_t NativeListOffset(const SceneCommand& command, size_t entrySize) {
    const size_t offset = static_cast<size_t>(command.Argument) + kNativeSectionPrefixSize;
    if (offset < command.Argument || entrySize > SIZE_MAX - offset) {
        throw std::runtime_error("native ZSI list offset overflows");
    }
    return offset;
}

} // namespace

bool SceneEntrySelection::Ready() const {
    return Status == "ready" && Route != nullptr && NativeScene != nullptr && Source != nullptr;
}

NativeSceneEntry ParseNativeZsiSceneEntry(std::span<const uint8_t> bytes, int32_t setupIndex,
                                          int32_t localEntranceIndex, std::string_view sourceName) {
    if (bytes.size() < 4 || bytes[0] != 'Z' || bytes[1] != 'S' || bytes[2] != 'I' || bytes[3] != 0x01) {
        throw std::runtime_error("expected native OOT3D ZSI source: " + std::string(sourceName));
    }
    if (localEntranceIndex < 0) {
        throw std::runtime_error("native ZSI local entrance index is negative");
    }

    const auto setupOffset = FindSetupOffset(bytes, setupIndex);
    const auto spawnCommand = FindUniqueCommand(bytes, setupOffset, kSpawnListCommand);
    const auto entranceCommand = FindUniqueCommand(bytes, setupOffset, kEntranceListCommand);
    if (spawnCommand.Count == 0 || localEntranceIndex >= entranceCommand.Count) {
        throw std::runtime_error("native ZSI entrance does not address a spawn");
    }

    const auto entranceListOffset = NativeListOffset(entranceCommand, kEntranceEntrySize);
    const auto entranceOffset = entranceListOffset +
                                static_cast<size_t>(localEntranceIndex) * kEntranceEntrySize;
    if (!CanRead(bytes, entranceOffset, kEntranceEntrySize)) {
        throw std::runtime_error("native ZSI entrance list is truncated");
    }
    const int32_t spawnIndex = bytes[entranceOffset];
    const int32_t room = static_cast<int8_t>(bytes[entranceOffset + 1]);
    if (spawnIndex >= spawnCommand.Count) {
        throw std::runtime_error("native ZSI entrance references an invalid spawn");
    }

    const auto spawnListOffset = NativeListOffset(spawnCommand, kActorEntrySize);
    const auto spawnOffset = spawnListOffset + static_cast<size_t>(spawnIndex) * kActorEntrySize;
    if (!CanRead(bytes, spawnOffset, kActorEntrySize)) {
        throw std::runtime_error("native ZSI spawn list is truncated");
    }

    NativeSceneEntry entry;
    entry.SetupIndex = setupIndex;
    entry.LocalEntranceIndex = localEntranceIndex;
    entry.SpawnIndex = spawnIndex;
    entry.Room = room;
    entry.ActorId = ReadLeS16(bytes, spawnOffset + 0x00);
    entry.PositionX = ReadLeS16(bytes, spawnOffset + 0x02);
    entry.PositionY = ReadLeS16(bytes, spawnOffset + 0x04);
    entry.PositionZ = ReadLeS16(bytes, spawnOffset + 0x06);
    entry.RotationX = ReadLeS16(bytes, spawnOffset + 0x08);
    entry.RotationY = ReadLeS16(bytes, spawnOffset + 0x0A);
    entry.RotationZ = ReadLeS16(bytes, spawnOffset + 0x0C);
    entry.Params = ReadLeU16(bytes, spawnOffset + 0x0E);
    if (entry.ActorId != kPlayerActorId) {
        throw std::runtime_error("native ZSI entrance does not resolve to ACTOR_PLAYER");
    }
    entry.CameraDataIndex = static_cast<uint8_t>(entry.Params & 0xFF);
    entry.PlayerStartMode = static_cast<uint8_t>(entry.Params >> 8);
    entry.UsesExplicitCameraData = entry.CameraDataIndex != 0xFF;
    return entry;
}

NativeSceneEntryProvider::NativeSceneEntryProvider(const AssetCatalog& catalog,
                                                   NativeSourceProvider& sources)
    : mCatalog(catalog), mSources(sources) {
}

SceneEntrySelection NativeSceneEntryProvider::ResolveSemanticEntry(
    const SemanticRouteCatalog& routes, int32_t scaffoldEntranceIndex, std::string_view variantKey,
    int32_t nativeSetupIndex) {
    return ResolveRoute(routes.FindSceneEntryRequest(scaffoldEntranceIndex, variantKey),
                        nativeSetupIndex);
}

SceneEntrySelection NativeSceneEntryProvider::ResolveSemanticEntry(
    const SemanticRouteCatalog& routes, int32_t scaffoldEntranceIndex,
    std::span<const SemanticGameplayFact> facts, int32_t nativeSetupIndex) {
    const auto match = routes.MatchSceneEntryRequest(scaffoldEntranceIndex, facts);
    if (!match.Ready()) {
        SceneEntrySelection selection;
        selection.Status = match.Status;
        return selection;
    }
    return ResolveRoute(match.Route, nativeSetupIndex);
}

SceneEntrySelection NativeSceneEntryProvider::ResolveRoute(const SemanticRouteRecord* route,
                                                           int32_t nativeSetupIndex) {
    SceneEntrySelection selection;
    selection.Route = route;
    if (selection.Route == nullptr) {
        selection.Status = "semantic_scene_entry_route_missing";
        return selection;
    }
    selection.NativeScene = mCatalog.Find(selection.Route->Native.AssetId);
    if (selection.NativeScene == nullptr || selection.NativeScene->Family != "scene_profile") {
        selection.Status = "native_scene_asset_missing";
        return selection;
    }
    if (nativeSetupIndex < 0) {
        if (selection.Route->Native.SetupIndices.size() != 1) {
            selection.Status = "native_scene_setup_selection_required";
            return selection;
        }
        nativeSetupIndex = selection.Route->Native.SetupIndices.front();
    }
    if (std::find(selection.Route->Native.SetupIndices.begin(),
                  selection.Route->Native.SetupIndices.end(), nativeSetupIndex) ==
        selection.Route->Native.SetupIndices.end()) {
        selection.Status = "native_scene_setup_not_routed";
        return selection;
    }
    if (selection.NativeScene->CanonicalResources.size() != 1) {
        selection.Status = "native_scene_resource_not_unique";
        return selection;
    }
    selection.Source = mSources.Load(selection.NativeScene->CanonicalResources.front());
    if (selection.Source == nullptr) {
        selection.Status = "native_scene_source_missing";
        return selection;
    }
    try {
        selection.Entry = ParseNativeZsiSceneEntry(
            *selection.Source->Bytes, nativeSetupIndex, selection.Route->Native.LocalEntranceIndex,
            selection.Source->ResourcePath);
        if (std::none_of(selection.Route->Native.RoomBindings.begin(),
                         selection.Route->Native.RoomBindings.end(),
                         [&selection](const auto& binding) {
                             return binding.NativeRoomIndex == selection.Entry.Room;
                         })) {
            selection.Status = "native_scene_entry_room_not_routed";
            return selection;
        }
        selection.Entry.GlobalEntranceIndex = selection.Route->Native.GlobalEntranceIndex;
        selection.Status = "ready";
    } catch (const std::exception& error) {
        selection.Status = "native_scene_entry_parse_error: " + std::string(error.what());
    }
    return selection;
}

} // namespace ThreeDsRecomp::Oot3d
