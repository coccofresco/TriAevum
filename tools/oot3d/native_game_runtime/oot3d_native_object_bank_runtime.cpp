#include "oot3d_native_object_bank_runtime.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"

namespace Oot3dNativeGame {
namespace {

constexpr std::string_view kNativeActorShardManifestResource =
    "oot3d/catalog/shards/oot3d-actors-native.json";

uint64_t ReadNonNegativeInteger(const nlohmann::json& value,
                                std::string_view label) {
    if (value.is_number_unsigned()) {
        return value.get<uint64_t>();
    }
    if (value.is_number_integer()) {
        const int64_t result = value.get<int64_t>();
        if (result >= 0) {
            return static_cast<uint64_t>(result);
        }
    }
    throw std::runtime_error(std::string(label) + " is not a non-negative integer");
}

uint32_t ReadUint32(const nlohmann::json& value, std::string_view label) {
    const uint64_t result = ReadNonNegativeInteger(value, label);
    if (result > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string(label) + " exceeds uint32");
    }
    return static_cast<uint32_t>(result);
}

const nlohmann::json* FindUniqueRecord(const nlohmann::json& rows,
                                       std::string_view key,
                                       std::string_view value) {
    const nlohmann::json* result = nullptr;
    for (const auto& row : rows) {
        if (!row.is_object() || row.value(std::string(key), "") != value) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error("native actor shard contains duplicate records");
        }
        result = &row;
    }
    return result;
}

} // namespace

struct NativeObjectBankRuntime::Impl {
    struct Bank {
        int32_t ObjectId = -1;
        std::string LogicalPath;
        std::string PayloadStatus;
        std::string ResourcePath;
        std::vector<int32_t> OwnerRooms;
        std::vector<int32_t> ResidentRooms;
        bool GlobalOwner = false;
        bool SourceMaterialized = false;
        bool NativeZeroSizeReference = false;
        bool Packaged = false;
        uint64_t ExpectedByteLength = 0;
        uint32_t ExpectedFileCount = 0;
        nlohmann::json ManifestRecord;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeSource> Source;
        std::optional<ThreeDsRecomp::Oot3d::ZarArchive> Archive;
        uint32_t RefCount = 0;
        uint64_t LoadCount = 0;
        uint64_t ReleaseCount = 0;
        std::string Status;
        std::string Error;
    };

    struct LoadedBank {
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeSource> Source;
        ThreeDsRecomp::Oot3d::ZarArchive Archive;
    };

    const Oot3d::RoomCompilationUnit& Unit;
    ThreeDsRecomp::Oot3d::NativeSourceProvider& Sources;
    std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeSource> ManifestSource;
    std::vector<Bank> Banks;
    std::vector<int32_t> ResidentRoomIndices;
    uint64_t TransitionCount = 0;
    uint64_t LoadCount = 0;
    uint64_t ReleaseCount = 0;
    uint64_t FailedTransitionCount = 0;
    std::string Status = "native_object_bank_manifest_ready";
    std::string Error;

    Impl(const Oot3d::RoomCompilationUnit& unit,
         ThreeDsRecomp::Oot3d::NativeSourceProvider& sources)
        : Unit(unit), Sources(sources) {
        ManifestSource = Sources.Load(kNativeActorShardManifestResource);
        if (ManifestSource == nullptr) {
            throw std::runtime_error("native actor shard manifest is unavailable");
        }
        const auto manifest = nlohmann::json::parse(
            ManifestSource->Bytes->begin(), ManifestSource->Bytes->end());
        if (manifest.value("format", "") != "oot3d_native_actor_shard_v1" ||
            manifest.value("status", "") != "complete" ||
            !manifest.contains("records") || !manifest.at("records").is_array() ||
            !manifest.contains("object_bank_bindings") ||
            !manifest.at("object_bank_bindings").is_array()) {
            throw std::runtime_error(
                "native actor shard has no supported object-bank contract");
        }

        for (const auto& dependency : Unit.objectDependencies) {
            Bank bank;
            bank.ObjectId = dependency.objectId;
            bank.LogicalPath = dependency.logicalPath;
            bank.PayloadStatus = dependency.payloadStatus;
            bank.SourceMaterialized =
                dependency.payloadStatus == "native_zar_materialized";
            bank.NativeZeroSizeReference = dependency.payloadStatus ==
                "native_unmaterialized_object_bank_reference";
            if (!bank.SourceMaterialized && !bank.NativeZeroSizeReference) {
                throw std::runtime_error(
                    "room compilation unit has an unsupported object-bank payload status");
            }

            const nlohmann::json* binding = nullptr;
            for (const auto& candidate : manifest.at("object_bank_bindings")) {
                if (!candidate.is_object() ||
                    candidate.value("unit_id", "") != Unit.unitId ||
                    candidate.value("object_id", -1) != dependency.objectId) {
                    continue;
                }
                if (binding != nullptr) {
                    throw std::runtime_error(
                        "native actor shard contains duplicate object-bank bindings");
                }
                binding = &candidate;
            }
            if (binding == nullptr ||
                binding->value("payload_status", "") != dependency.payloadStatus ||
                binding->value("source_container", "") != dependency.logicalPath) {
                throw std::runtime_error(
                    "native actor shard object-bank binding differs from its RCU");
            }

            for (const auto& room : Unit.rooms) {
                if (std::find(room.objectIds.begin(), room.objectIds.end(),
                              dependency.objectId) != room.objectIds.end()) {
                    bank.OwnerRooms.push_back(room.roomIndex);
                }
            }
            std::sort(bank.OwnerRooms.begin(), bank.OwnerRooms.end());
            bank.OwnerRooms.erase(
                std::unique(bank.OwnerRooms.begin(), bank.OwnerRooms.end()),
                bank.OwnerRooms.end());
            bank.GlobalOwner = bank.OwnerRooms.empty();

            if (bank.SourceMaterialized) {
                if (!binding->contains("resource") ||
                    !binding->at("resource").is_string()) {
                    throw std::runtime_error(
                        "materialized object-bank binding has no packaged resource");
                }
                bank.ResourcePath = binding->at("resource").get<std::string>();
                const auto record = FindUniqueRecord(
                    manifest.at("records"), "source_container", bank.LogicalPath);
                if (record != nullptr) {
                    if (record->value("resource", "") != bank.ResourcePath ||
                        !record->contains("byte_length") ||
                        !record->contains("file_count") ||
                        !record->contains("files") ||
                        !record->at("files").is_array()) {
                        throw std::runtime_error(
                            "native actor shard object-bank record is incomplete");
                    }
                    bank.ExpectedByteLength = ReadNonNegativeInteger(
                        record->at("byte_length"), "object-bank byte length");
                    bank.ExpectedFileCount = ReadUint32(
                        record->at("file_count"), "object-bank file count");
                    bank.ManifestRecord = *record;
                    bank.Packaged = true;
                }
                bank.Status = bank.Packaged
                                  ? "native_object_bank_packaged_not_resident"
                                  : "native_object_bank_not_packaged";
            } else {
                if (binding->contains("resource") &&
                    !binding->at("resource").is_null()) {
                    throw std::runtime_error(
                        "zero-size object-bank binding unexpectedly has a resource");
                }
                bank.Status = "native_zero_size_object_bank_not_resident";
            }
            Banks.push_back(std::move(bank));
        }
    }

    LoadedBank Load(const Bank& bank) {
        if (!bank.Packaged) {
            throw std::runtime_error("native object-bank ZAR is not packaged");
        }
        LoadedBank loaded;
        loaded.Source = Sources.Load(bank.ResourcePath);
        if (loaded.Source == nullptr ||
            loaded.Source->Bytes->size() != bank.ExpectedByteLength) {
            throw std::runtime_error(
                "native object-bank resource is absent or has the wrong length");
        }
        loaded.Archive = ThreeDsRecomp::Oot3d::ParseZarArchiveBytes(
            *loaded.Source->Bytes, bank.ResourcePath);
        if (loaded.Archive.Files.size() != bank.ExpectedFileCount ||
            bank.ManifestRecord.at("files").size() != bank.ExpectedFileCount) {
            throw std::runtime_error(
                "native object-bank archive file count differs from its manifest");
        }
        for (const auto& expected : bank.ManifestRecord.at("files")) {
            if (!expected.is_object() || !expected.contains("index") ||
                !expected.contains("name") || !expected.at("name").is_string() ||
                !expected.contains("type") || !expected.at("type").is_string() ||
                !expected.contains("type_local_index") ||
                !expected.contains("offset") || !expected.contains("size")) {
                throw std::runtime_error(
                    "native object-bank member contract is incomplete");
            }
            const uint32_t index = ReadUint32(
                expected.at("index"), "object-bank member index");
            const auto found = std::find_if(
                loaded.Archive.Files.begin(), loaded.Archive.Files.end(),
                [index](const auto& file) { return file.Index == index; });
            if (found == loaded.Archive.Files.end() ||
                found->Name != expected.at("name").get<std::string>() ||
                found->TypeName != expected.at("type").get<std::string>() ||
                found->TypeLocalIndex != ReadUint32(
                    expected.at("type_local_index"),
                    "object-bank member type-local index") ||
                found->Offset != ReadUint32(
                    expected.at("offset"), "object-bank member offset") ||
                found->Size != ReadUint32(
                    expected.at("size"), "object-bank member size")) {
                throw std::runtime_error(
                    "native object-bank member differs from its manifest");
            }
        }
        return loaded;
    }

    bool SetResidentRooms(std::vector<int32_t> roomIndices) {
        std::sort(roomIndices.begin(), roomIndices.end());
        roomIndices.erase(std::unique(roomIndices.begin(), roomIndices.end()),
                          roomIndices.end());
        for (const int32_t roomIndex : roomIndices) {
            if (Unit.FindRoom(roomIndex) == nullptr) {
                Status = "native_object_bank_room_unknown";
                Error = "object-bank residency requested an unknown room";
                ++FailedTransitionCount;
                return false;
            }
        }

        std::vector<uint32_t> desiredRefCounts;
        desiredRefCounts.reserve(Banks.size());
        std::map<size_t, LoadedBank> staged;
        try {
            for (size_t index = 0; index < Banks.size(); ++index) {
                const auto& bank = Banks[index];
                uint32_t refCount = bank.GlobalOwner ? 1u : 0u;
                for (const int32_t owner : bank.OwnerRooms) {
                    if (std::binary_search(roomIndices.begin(), roomIndices.end(), owner)) {
                        ++refCount;
                    }
                }
                desiredRefCounts.push_back(refCount);
                if (refCount != 0 && bank.RefCount == 0 &&
                    bank.SourceMaterialized) {
                    staged.emplace(index, Load(bank));
                }
            }
        } catch (const std::exception& error) {
            Status = "native_object_bank_residency_prepare_failed";
            Error = error.what();
            ++FailedTransitionCount;
            return false;
        }

        for (size_t index = 0; index < Banks.size(); ++index) {
            auto& bank = Banks[index];
            const uint32_t previousRefCount = bank.RefCount;
            bank.RefCount = desiredRefCounts[index];
            bank.ResidentRooms.clear();
            for (const int32_t owner : bank.OwnerRooms) {
                if (std::binary_search(roomIndices.begin(), roomIndices.end(), owner)) {
                    bank.ResidentRooms.push_back(owner);
                }
            }
            if (previousRefCount == 0 && bank.RefCount != 0 &&
                bank.SourceMaterialized) {
                auto loaded = std::move(staged.at(index));
                bank.Source = std::move(loaded.Source);
                bank.Archive = std::move(loaded.Archive);
                ++bank.LoadCount;
                ++LoadCount;
            } else if (previousRefCount != 0 && bank.RefCount == 0 &&
                       bank.SourceMaterialized) {
                bank.Source.reset();
                bank.Archive.reset();
                ++bank.ReleaseCount;
                ++ReleaseCount;
            }
            bank.Error.clear();
            if (bank.RefCount == 0) {
                bank.Status = bank.NativeZeroSizeReference
                                  ? "native_zero_size_object_bank_not_resident"
                                  : "native_object_bank_packaged_not_resident";
            } else if (bank.NativeZeroSizeReference) {
                bank.Status = "native_zero_size_object_bank_resident";
            } else {
                bank.Status = "native_object_bank_archive_resident";
            }
        }
        ResidentRoomIndices = std::move(roomIndices);
        ++TransitionCount;
        Status = "native_object_bank_residency_ready";
        Error.clear();
        return true;
    }

    bool IsReady(int32_t objectId) const {
        const auto bank = std::find_if(
            Banks.begin(), Banks.end(),
            [objectId](const auto& candidate) {
                return candidate.ObjectId == objectId;
            });
        if (bank == Banks.end() || bank->RefCount == 0) {
            return false;
        }
        if (bank->NativeZeroSizeReference) {
            return true;
        }
        return bank->SourceMaterialized && bank->Packaged &&
               bank->Source != nullptr && bank->Archive.has_value();
    }
};

NativeObjectBankRuntime::NativeObjectBankRuntime(
    const Oot3d::RoomCompilationUnit& unit,
    ThreeDsRecomp::Oot3d::NativeSourceProvider& sources)
    : mImpl(std::make_unique<Impl>(unit, sources)) {
}

NativeObjectBankRuntime::~NativeObjectBankRuntime() = default;

bool NativeObjectBankRuntime::SetResidentRooms(
    std::vector<int32_t> roomIndices) {
    return mImpl->SetResidentRooms(std::move(roomIndices));
}

bool NativeObjectBankRuntime::IsReady(int32_t objectId) const {
    return mImpl->IsReady(objectId);
}

const std::vector<int32_t>& NativeObjectBankRuntime::ResidentRooms() const {
    return mImpl->ResidentRoomIndices;
}

const std::string& NativeObjectBankRuntime::Status() const {
    return mImpl->Status;
}

const std::string& NativeObjectBankRuntime::Error() const {
    return mImpl->Error;
}

nlohmann::json NativeObjectBankRuntime::Diagnostics() const {
    nlohmann::json banks = nlohmann::json::array();
    for (const auto& bank : mImpl->Banks) {
        banks.push_back({
            {"object_id", bank.ObjectId},
            {"logical_path", bank.LogicalPath},
            {"payload_status", bank.PayloadStatus},
            {"resource", bank.ResourcePath},
            {"owner_rooms", bank.OwnerRooms},
            {"resident_rooms", bank.ResidentRooms},
            {"global_owner", bank.GlobalOwner},
            {"source_materialized", bank.SourceMaterialized},
            {"native_zero_size_reference", bank.NativeZeroSizeReference},
            {"packaged", bank.Packaged},
            {"resident", bank.RefCount != 0},
            {"ref_count", bank.RefCount},
            {"expected_byte_length", bank.ExpectedByteLength},
            {"expected_file_count", bank.ExpectedFileCount},
            {"archive_file_count",
             bank.Archive.has_value() ? bank.Archive->Files.size() : 0},
            {"load_count", bank.LoadCount},
            {"release_count", bank.ReleaseCount},
            {"status", bank.Status},
            {"error", bank.Error},
        });
    }
    return {
        {"available", true},
        {"manifest_resource", kNativeActorShardManifestResource},
        {"unit_id", mImpl->Unit.unitId},
        {"resident_rooms", mImpl->ResidentRoomIndices},
        {"bank_count", mImpl->Banks.size()},
        {"resident_bank_count",
         std::count_if(mImpl->Banks.begin(), mImpl->Banks.end(),
                       [](const auto& bank) { return bank.RefCount != 0; })},
        {"transition_count", mImpl->TransitionCount},
        {"load_count", mImpl->LoadCount},
        {"release_count", mImpl->ReleaseCount},
        {"failed_transition_count", mImpl->FailedTransitionCount},
        {"status", mImpl->Status},
        {"error", mImpl->Error},
        {"banks", std::move(banks)},
    };
}

} // namespace Oot3dNativeGame
