#include "oot3d_native_actor_runtime.h"
#include "oot3d_a32_provenance.h"
#include "oot3d_native_object_bank_runtime.h"
#include "oot3d_native_room_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

namespace {

std::filesystem::path WriteContract() {
    nlohmann::json functions = nlohmann::json::object();
    uint32_t address = 0x1000;
    for (const char* name : {
             "Actor_Noop", "Actor_Destroy", "Actor_Delete", "Actor_Spawn",
             "Actor_Kill", "Actor_ChangeType", "Actor_InitContext",
             "Actor_UpdateAll", "Actor_Init",
         }) {
        functions[name] = {
            { "address", address }, { "size", 4 }, { "family", "test" },
            { "confidence", "high" },
        };
        address += 4;
    }
    const nlohmann::json document = {
        { "format", "oot3d_actor_core_native_contract_v1" },
        { "status", "ready" },
        { "source", {
            { "snapshot_id", "test-snapshot" },
            { "source_base_revision", std::string(oot3d::recomp::kA32SourceRevision) },
            { "code_bin_sha256", std::string(64, 'b') },
        } },
        { "structures", {
            { "actor_entry", { { "size", 0x10 } } },
            { "actor_context", { { "size", 0x20C }, { "category_list_count", 12 } } },
        } },
        { "native_invariants", {
            { "spawn_total_guard_operator", "less_than" },
            { "spawn_total_guard_value", 3 },
            { "category_insertion", "prepend_head" },
            { "category_iteration", "ascending_index_then_head_to_tail" },
        } },
        { "functions", std::move(functions) },
    };
    const auto path = std::filesystem::temp_directory_path() / "oot3d_actor_runtime_test.json";
    std::ofstream(path) << document.dump(2);
    return path;
}

std::filesystem::path WriteEnRiverSoundClosureCatalog() {
    const auto path = std::filesystem::temp_directory_path() /
                      "oot3d_enriver_sound_closure_test.json";
    const auto function = [](const char* address, const char* name) {
        return nlohmann::json{
            {"address", address}, {"name", name}, {"module", "game/actors"},
            {"body_status", "extracted"}, {"compile_readiness", "test"},
            {"hazards", nlohmann::json::array()},
        };
    };
    const nlohmann::json document = {
        {"format", "oot3d_native_corpus_promotion_v1"},
        {"payload_sha256", "test-corpus"},
        {"source", {{"revision", std::string(oot3d::recomp::kA32SourceRevision)}}},
        {"room_compilation_unit", {
            {"unit_id", "test-enriver-unit"},
            {"payload_sha256", "test-enriver-payload"},
        }},
        {"counts", {
            {"extracted_bodies", 3},
            {"functions_without_listed_hazards", 3},
        }},
        {"functions", {
            function("0x00283870", "EnRiverSound_Init"),
            function("0x00283924", "EnRiverSound_Destroy"),
            function("0x002A5AB4", "EnRiverSound_Update"),
        }},
    };
    std::ofstream(path) << document.dump(2);
    return path;
}

Oot3d::RoomCompilationUnit EnRiverSoundRoomUnit() {
    Oot3d::RoomCompilationUnit unit;
    unit.unitId = "test-enriver-unit";
    unit.payloadSha256 = "test-enriver-payload";
    unit.sceneId = 0x55;
    Oot3d::RoomCompilationActorProfile profile;
    profile.profileKey = "actor:0x003B";
    profile.actorId = 59;
    profile.actorName = "ACTOR_EN_RIVER_SOUND";
    for (const auto& [address, name] : {
             std::pair{0x00283870u, "EnRiverSound_Init"},
             std::pair{0x00283924u, "EnRiverSound_Destroy"},
             std::pair{0x002A5AB4u, "EnRiverSound_Update"},
         }) {
        Oot3d::RoomCompilationActorBehaviorFunction function;
        function.runtimeAddress = address;
        function.name = name;
        profile.behaviorGraph.functions.push_back(std::move(function));
    }
    unit.actorProfiles.push_back(std::move(profile));
    Oot3d::RoomCompilationActorEntry instance;
    instance.instanceKey = "room-0-actor-0";
    instance.profileKey = "actor:0x003B";
    instance.actorId = 59;
    instance.actorName = "ACTOR_EN_RIVER_SOUND";
    instance.params = 1;
    unit.actorInstances.push_back(std::move(instance));
    return unit;
}

ThreeDsRecomp::Oot3d::NativeActorProfile Profile(uint16_t actorId, uint8_t category,
                                        uint32_t init, uint32_t update,
                                        uint32_t destroy) {
    ThreeDsRecomp::Oot3d::NativeActorProfile profile;
    profile.Valid = true;
    profile.ActorId = actorId;
    profile.Category = category;
    profile.InstanceSize = 0x1A4;
    profile.InitFunctionAddress = init;
    profile.UpdateFunctionAddress = update;
    profile.DestroyFunctionAddress = destroy;
    return profile;
}

Oot3dNativeGame::NativeActorSpawnEntry Entry(int16_t actorId, int index) {
    Oot3dNativeGame::NativeActorSpawnEntry entry;
    entry.ActorId = actorId;
    entry.EntryIndex = index;
    entry.Position = { static_cast<double>(index), 2.0, 3.0 };
    return entry;
}

Oot3d::RoomCompilationUnit RoomUnit() {
    Oot3d::RoomCompilationUnit unit;
    unit.initialRoomIndex = 0;
    unit.behaviorGraphProfileCount = 1;
    unit.behaviorFunctionCount = 1;
    unit.behaviorActionTransitionCount = 0;
    unit.behaviorStructureFieldCount = 1;
    for (int32_t roomIndex = 0; roomIndex < 3; ++roomIndex) {
        Oot3d::RoomCompilationRoom room;
        room.roomIndex = roomIndex;
        room.initiallyActive = roomIndex == unit.initialRoomIndex;
        unit.rooms.push_back(std::move(room));
    }
    unit.roomLifecycle.available = true;
    unit.roomLifecycle.idleState = 0;
    unit.roomLifecycle.loadingState = 1;
    unit.roomLifecycle.terminalState = 2;
    unit.roomLifecycle.maxResidentRoomCount = 2;
    unit.roomLifecycle.transitionActorIdSpawnMask = 0x1FFF;
    unit.roomLifecycle.transitionActorParamsIndexStride = 0x400;
    unit.roomLifecycle.cleanupDelayTicks = 1;
    Oot3d::RoomCompilationActorProfile profile;
    profile.profileKey = "actor:0x0000";
    profile.actorId = 0;
    profile.actorName = "ACTOR_PLAYER";
    profile.instanceSize = 0x1A4;
    profile.behaviorGraph.available = true;
    profile.behaviorGraph.status = "workflow_graph_recovered";
    profile.behaviorGraph.owner = "Player";
    profile.behaviorGraph.structure = "Player";
    profile.behaviorGraph.structureSize = profile.instanceSize;
    profile.behaviorGraph.runtimeBindingStatus =
        "consumer_must_bind_native_behavior_graph";
    Oot3d::RoomCompilationActorBehaviorFunction function;
    function.name = "Actor_Noop";
    function.family = "test";
    function.runtimeAddress = 0x1000;
    function.byteLength = 4;
    profile.behaviorGraph.functions.push_back(std::move(function));
    Oot3d::RoomCompilationActorStructureField field;
    field.name = "actor";
    field.type = "Actor";
    profile.behaviorGraph.structureFields.push_back(std::move(field));
    unit.actorProfiles.push_back(std::move(profile));
    return unit;
}

Oot3d::RoomCompilationMaterialTevAlphaOperation RoomTevAlphaOperation() {
    Oot3d::RoomCompilationMaterialTevAlphaOperation operation;
    operation.roomIndex = 1;
    operation.resourceIndex = 0;
    operation.materialIndices = {2, 3};
    operation.constantIndex = 0;
    operation.nativeOperation = 2;
    operation.alphaBaseF32Bits = 0x3F800000;
    operation.alphaSelectorScaleF32Bits = 0x3A1DEB07;
    operation.selector.defaultValue = 0;
    operation.selector.excludedSetupIndex = 4;
    operation.selector.drawParamSetupIndex = 6;
    operation.selector.drawParamIndex = 0;
    operation.selector.progressionValue = 1660;
    operation.selector.setupLessThan = 4;
    operation.selector.alternateFact = {"player.age", "adult"};
    operation.selector.requiredFact = {"quest.kokiri_emerald", "true"};
    operation.random.functionAddress = 0x00368B68;
    operation.random.functionSize = 48;
    operation.random.functionSha256 = std::string(64, 'c');
    operation.random.stateAddress = 0x0050C0C4;
    operation.random.offset = 0;
    operation.random.range = 100;
    operation.random.initialState = 1;
    operation.random.multiplier = 0x0019660D;
    operation.random.increment = 0x3C6EF35F;
    operation.random.scaleF32Bits = 0x3B03126F;
    operation.random.middleTestAddU32 = 0xC2333332;
    operation.random.middleTestLessThanU32 = 0x01A66665;
    operation.random.lowCompareF32Bits = 0x3DCCCCCD;
    return operation;
}

std::unique_ptr<Oot3dNativeGame::NativeA32ExecutionRuntime>
MakeNativeRandomExecution() {
    const auto operation = RoomTevAlphaOperation();
    constexpr uint32_t codeBaseAddress = 0x00100000;
    constexpr uint32_t atanTableAddress = 0x0050D000;
    constexpr uint32_t trackingPresetAddress = 0x0050CBA4;
    constexpr uint32_t globalContextPointerAddress = 0x0051B2F4;
    std::vector<uint8_t> codeImage(
        static_cast<size_t>(globalContextPointerAddress + 4 - codeBaseAddress),
        0);
    const auto writeCodeWord = [&](uint32_t address, uint32_t value) {
        const size_t offset = static_cast<size_t>(address - codeBaseAddress);
        for (size_t byte = 0; byte < sizeof(value); ++byte) {
            codeImage.at(offset + byte) =
                static_cast<uint8_t>(value >> (byte * 8));
        }
    };
    const auto writeCodeHalf = [&](uint32_t address, uint16_t value) {
        const size_t offset = static_cast<size_t>(address - codeBaseAddress);
        codeImage.at(offset) = static_cast<uint8_t>(value);
        codeImage.at(offset + 1) = static_cast<uint8_t>(value >> 8);
    };
    writeCodeWord(0x00375A08, operation.random.stateAddress);
    writeCodeWord(0x00375A0C, operation.random.multiplier);
    writeCodeWord(0x00375A10, operation.random.increment);
    writeCodeWord(0x00375A14, 0x3F800000);
    writeCodeWord(0x002BC75C, atanTableAddress);
    writeCodeWord(0x0034C924, trackingPresetAddress);
    writeCodeWord(0x0034C928, 0x0000FFFE);
    writeCodeWord(0x00375B64, globalContextPointerAddress);
    writeCodeHalf(trackingPresetAddress + 0x00, 12000);
    writeCodeHalf(trackingPresetAddress + 0x02,
                  static_cast<uint16_t>(-8000));
    writeCodeHalf(trackingPresetAddress + 0x04, 8000);
    writeCodeHalf(trackingPresetAddress + 0x06, 12000);
    writeCodeHalf(trackingPresetAddress + 0x08,
                  static_cast<uint16_t>(-8000));
    writeCodeHalf(trackingPresetAddress + 0x0A, 8000);
    codeImage.at(static_cast<size_t>(trackingPresetAddress + 0x0C -
                                    codeBaseAddress)) = 1;
    writeCodeWord(trackingPresetAddress + 0x10,
                  std::bit_cast<uint32_t>(200.0f));
    writeCodeHalf(trackingPresetAddress + 0x14, 4000);
    return std::make_unique<Oot3dNativeGame::NativeA32ExecutionRuntime>(
        codeImage, codeBaseAddress);
}

void Expect(bool condition, const char* message);

void TestNativeRoomPrepareDrawOperation() {
    const auto operation = RoomTevAlphaOperation();
    auto nativeExecution = MakeNativeRandomExecution();
    const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> initialChildFacts = {
        {"player.age", "child"},
        {"quest.kokiri_emerald", "false"},
    };
    uint32_t randomState = operation.random.initialState;
    const auto initial =
        Oot3dNativeGame::EvaluateNativeRoomMaterialTevAlphaOperation(
            operation, 0, initialChildFacts, std::span<const uint16_t>{},
            randomState, nativeExecution.get());
    Expect(initial.Evaluated && initial.Selector == 0 && initial.AlphaU8 == 255 &&
               initial.RandomValue >= 0 && initial.RandomValue < 100 &&
               randomState != operation.random.initialState,
           "native Kokiri prepare-draw initial-child alpha evaluation");

    const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> progressedFacts = {
        {"player.age", "child"},
        {"quest.kokiri_emerald", "true"},
    };
    randomState = operation.random.initialState;
    const auto progressed =
        Oot3dNativeGame::EvaluateNativeRoomMaterialTevAlphaOperation(
            operation, 0, progressedFacts, std::span<const uint16_t>{},
            randomState, nativeExecution.get());
    Expect(progressed.Evaluated && progressed.Selector == 1660 &&
               progressed.Alpha < 0.1f,
           "native Kokiri prepare-draw progression alpha evaluation");

    randomState = operation.random.initialState;
    const auto missingDrawParam =
        Oot3dNativeGame::EvaluateNativeRoomMaterialTevAlphaOperation(
            operation, 6, initialChildFacts, std::span<const uint16_t>{},
            randomState, nativeExecution.get());
    Expect(!missingDrawParam.Evaluated &&
               missingDrawParam.Status ==
                   "native_room_callback_draw_param_unavailable" &&
               randomState == operation.random.initialState,
           "native setup-six draw parameter remains an explicit gap");

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
    for (const int32_t materialIndex : {2, 3, 4}) {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
        batch.MaterialIndex = materialIndex;
        batch.Material.ConstantColors[0] = {
            static_cast<uint8_t>(materialIndex), 17, 29, 211};
        model.Batches.push_back(std::move(batch));
    }
    const size_t applied =
        ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialAlphaOverride(
            model, 2, 0, 73,
            "oot3d_code_bin_room_prepare_draw_material_tev_alpha_op2");
    Expect(applied == 1 && model.Batches[0].Material.ConstantColors[0].R == 2 &&
               model.Batches[0].Material.ConstantColors[0].G == 17 &&
               model.Batches[0].Material.ConstantColors[0].B == 29 &&
               model.Batches[0].Material.ConstantColors[0].A == 73 &&
               model.Batches[1].Material.ConstantColors[0].A == 211 &&
               model.Batches[2].Material.ConstantColors[0].A == 211,
           "native material-indexed TEV alpha override preserves RGB and peers");
}

void WriteU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value & 0xFF);
    bytes.at(offset + 1) = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t byte = 0; byte < 4; ++byte) {
        bytes.at(offset + byte) =
            static_cast<uint8_t>((value >> (byte * 8)) & 0xFF);
    }
}

std::vector<uint8_t> SingleFileZar(std::string_view payload) {
    constexpr size_t kTypeSection = 0x20;
    constexpr size_t kTypeIndexList = 0x30;
    constexpr size_t kTypeNameOffset = 0x34;
    constexpr size_t kMetaSection = 0x40;
    constexpr size_t kFileNameOffset = 0x50;
    constexpr size_t kDataSection = 0x70;
    constexpr size_t kPayloadOffset = 0x80;
    constexpr std::string_view kTypeName = "cmb";
    constexpr std::string_view kFileName = "Model/test.cmb";

    std::vector<uint8_t> bytes(kPayloadOffset + payload.size());
    std::copy_n("ZAR\x01", 4, bytes.begin());
    WriteU32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    WriteU16(bytes, 0x08, 1);
    WriteU16(bytes, 0x0A, 1);
    WriteU32(bytes, 0x0C, kTypeSection);
    WriteU32(bytes, 0x10, kMetaSection);
    WriteU32(bytes, 0x14, kDataSection);
    WriteU32(bytes, kTypeSection, 1);
    WriteU32(bytes, kTypeSection + 4, kTypeIndexList);
    WriteU32(bytes, kTypeSection + 8, kTypeNameOffset);
    WriteU32(bytes, kTypeIndexList, 0);
    std::copy(kTypeName.begin(), kTypeName.end(), bytes.begin() + kTypeNameOffset);
    WriteU32(bytes, kMetaSection, static_cast<uint32_t>(payload.size()));
    WriteU32(bytes, kMetaSection + 4, kFileNameOffset);
    std::copy(kFileName.begin(), kFileName.end(), bytes.begin() + kFileNameOffset);
    WriteU32(bytes, kDataSection, kPayloadOffset);
    std::copy(payload.begin(), payload.end(), bytes.begin() + kPayloadOffset);
    return bytes;
}

std::shared_ptr<const std::vector<uint8_t>> NativeFile(
    const std::vector<uint8_t>& bytes) {
    return std::make_shared<const std::vector<uint8_t>>(bytes);
}

void AddObjectDependency(Oot3d::RoomCompilationUnit& unit, int32_t objectId,
                         std::string logicalPath, std::string payloadStatus) {
    Oot3d::RoomCompilationObjectDependency dependency;
    dependency.objectId = objectId;
    dependency.logicalPath = std::move(logicalPath);
    dependency.payloadStatus = std::move(payloadStatus);
    unit.objectDependencies.push_back(std::move(dependency));
}

const nlohmann::json& FindObjectBank(const nlohmann::json& diagnostics,
                                    int32_t objectId) {
    const auto& banks = diagnostics.at("banks");
    const auto found = std::find_if(
        banks.begin(), banks.end(),
        [objectId](const auto& bank) {
            return bank.value("object_id", -1) == objectId;
        });
    if (found == banks.end()) {
        throw std::runtime_error("object bank missing from diagnostics");
    }
    return *found;
}

void Expect(bool condition, const char* message);

void TestNativeObjectBankResidency() {
    auto unit = RoomUnit();
    unit.unitId = "scene_entry:test:setup-0";
    unit.rooms[0].objectIds = {10, 11, 13};
    unit.rooms[1].objectIds = {10, 12};
    unit.rooms[2].objectIds = {15};
    AddObjectDependency(unit, 10, "actor/shared.zar", "native_zar_materialized");
    AddObjectDependency(unit, 11, "actor/room0.zar", "native_zar_materialized");
    AddObjectDependency(unit, 12, "actor/room1.zar", "native_zar_materialized");
    AddObjectDependency(unit, 13, "actor/zero.zar",
                        "native_unmaterialized_object_bank_reference");
    AddObjectDependency(unit, 14, "actor/global.zar", "native_zar_materialized");
    AddObjectDependency(unit, 15, "actor/missing.zar", "native_zar_materialized");

    const auto zar = SingleFileZar("native-cmb");
    nlohmann::json records = nlohmann::json::array();
    nlohmann::json bindings = nlohmann::json::array();
    const auto addMaterialized = [&](int32_t objectId, std::string_view name) {
        const std::string source = "actor/" + std::string(name) + ".zar";
        const std::string resource = "oot3d/native/actor/" + std::string(name) + ".zar";
        records.push_back({
            {"source_container", source},
            {"resource", resource},
            {"byte_length", zar.size()},
            {"file_count", 1},
            {"files", {{{"index", 0},
                         {"name", "Model/test.cmb"},
                         {"type", "cmb"},
                         {"type_local_index", 0},
                         {"offset", 0x80},
                         {"size", 10}}}},
        });
        bindings.push_back({
            {"unit_id", unit.unitId},
            {"object_id", objectId},
            {"payload_status", "native_zar_materialized"},
            {"source_container", source},
            {"resource", resource},
        });
    };
    addMaterialized(10, "shared");
    addMaterialized(11, "room0");
    addMaterialized(12, "room1");
    addMaterialized(14, "global");
    addMaterialized(15, "missing");
    bindings.push_back({
        {"unit_id", unit.unitId},
        {"object_id", 13},
        {"payload_status", "native_unmaterialized_object_bank_reference"},
        {"source_container", "actor/zero.zar"},
        {"resource", nullptr},
    });
    const nlohmann::json manifest = {
        {"format", "oot3d_native_actor_shard_v1"},
        {"status", "complete"},
        {"records", std::move(records)},
        {"object_bank_bindings", std::move(bindings)},
    };

    std::unordered_map<std::string, std::vector<uint8_t>> resources;
    const std::string manifestResource =
        "oot3d/catalog/shards/oot3d-actors-native.json";
    const std::string manifestText = manifest.dump();
    resources.emplace(manifestResource,
                      std::vector<uint8_t>(manifestText.begin(), manifestText.end()));
    for (const char* name : {"shared", "room0", "room1", "global"}) {
        resources.emplace("oot3d/native/actor/" + std::string(name) + ".zar", zar);
    }
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources(
        [&resources](const std::string& path)
            -> std::shared_ptr<const std::vector<uint8_t>> {
            const auto found = resources.find(path);
            return found == resources.end() ? nullptr : NativeFile(found->second);
        });
    Oot3dNativeGame::NativeObjectBankRuntime runtime(unit, sources);

    Expect(runtime.SetResidentRooms({0}), "activate room-zero native object banks");
    Expect(runtime.IsReady(10) && runtime.IsReady(11) && runtime.IsReady(13) &&
               runtime.IsReady(14) && !runtime.IsReady(12),
           "room-zero, zero-size, and global object-bank readiness");
    auto diagnostics = runtime.Diagnostics();
    Expect(FindObjectBank(diagnostics, 10).at("ref_count") == 1 &&
               FindObjectBank(diagnostics, 13).at("archive_file_count") == 0,
           "native shared and zero-size object-bank state");

    Expect(runtime.SetResidentRooms({0, 1}), "activate adjacent room object banks");
    diagnostics = runtime.Diagnostics();
    Expect(FindObjectBank(diagnostics, 10).at("ref_count") == 2 &&
               FindObjectBank(diagnostics, 12).at("load_count") == 1,
           "shared bank refcount and adjacent-room load");

    Expect(runtime.SetResidentRooms({1}), "release previous room object banks");
    diagnostics = runtime.Diagnostics();
    Expect(!runtime.IsReady(11) && !runtime.IsReady(13) &&
               FindObjectBank(diagnostics, 10).at("ref_count") == 1 &&
               FindObjectBank(diagnostics, 11).at("release_count") == 1,
           "room-exclusive release preserves shared bank");

    Expect(runtime.SetResidentRooms({0}), "reactivate room-zero object banks");
    diagnostics = runtime.Diagnostics();
    Expect(FindObjectBank(diagnostics, 11).at("load_count") == 2 &&
               FindObjectBank(diagnostics, 12).at("release_count") == 1,
           "native object-bank logical reload and release");

    Expect(!runtime.SetResidentRooms({2}),
           "reject transition with unavailable native object bank");
    diagnostics = runtime.Diagnostics();
    Expect(runtime.ResidentRooms() == std::vector<int32_t>{0} &&
               runtime.IsReady(11) &&
               FindObjectBank(diagnostics, 11).at("ref_count") == 1 &&
               diagnostics.at("failed_transition_count") == 1,
           "failed object-bank transition preserves prior residency");
}

nlohmann::json NativeNpcTrackingContractDocument() {
    const nlohmann::json preset = {
        {"index", 0},
        {"head_yaw_limit", 12000},
        {"head_pitch_min", -8000},
        {"head_pitch_max", 8000},
        {"torso_yaw_limit", 12000},
        {"torso_pitch_min", -8000},
        {"torso_pitch_max", 8000},
        {"rotate_actor", true},
        {"auto_turn_distance", 200.0},
        {"auto_turn_yaw_threshold", 4000},
    };
    return {
        {"actor_profile", {{"instance_size", 0xCA8}}},
        {"draw_callback",
         {{"address", 0x002335B4},
          {"size", 0x60},
          {"sha256", std::string(64, 'c')},
          {"torso_limb_index", 9},
          {"head_limb_index", 10},
          {"torso_rotation_u16x3_offset", 0x29A},
          {"head_rotation_u16x3_offset", 0x294},
          {"native_abi",
           {{"name", "EnKo_OverrideLimbDraw"},
            {"address", 0x002335B4},
            {"closure_kind", "maintained_abi"},
            {"return_type", "s32"},
            {"parameter_types",
             {"Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*"}}}}}},
        {"tracking_runtime",
         {{"status", "initial_child_start_routes_recovered"},
          {"service",
           {{"address", 0x0034C664},
            {"size", 0x2C0},
            {"sha256", std::string(64, 'c')}}},
          {"actor_layout",
           {{"minimum_size", 0xC0},
            {"world_position_f32x3_offset", 0x28},
            {"shape_yaw_s16_offset", 0xBE}}},
          {"interact_info",
           {{"size", 0x28},
            {"talk_state_s16_offset", 0x00},
            {"tracking_mode_offset", 0x02},
            {"auto_turn_timer_s16_offset", 0x04},
            {"auto_turn_state_s16_offset", 0x06},
            {"head_pitch_s16_offset", 0x08},
            {"head_yaw_s16_offset", 0x0A},
            {"torso_pitch_s16_offset", 0x0E},
            {"torso_yaw_s16_offset", 0x10},
            {"target_height_f32_offset", 0x14},
            {"target_position_f32x3_offset", 0x18}}},
          {"global_context",
           {{"pointer_address", 0x0051B2F4},
            {"minimum_size", 0x112},
            {"update_rate_s16_offset", 0x110},
            {"update_rate", 2}}},
          {"smoothing", {{"scale", 6}, {"max_step", 2000}, {"min_step", 1}}},
          {"selector",
           {{"address", 0x0035CF48},
            {"size", 0x174},
            {"sha256", std::string(64, 'c')},
            {"random_state_address", 0x0050C0C4},
            {"facing_threshold", 0x3FFC},
            {"forced_mode_path", "nonzero_argument_returned_unchanged"}}},
          {"presets", {preset}},
          {"mode_semantics",
           {{{"mode", 1}, {"head", false}, {"torso", false}, {"rotate_actor", false}},
            {{"mode", 2}, {"head", true}, {"torso", true}, {"rotate_actor", false}},
            {{"mode", 3}, {"head", true}, {"torso", false}, {"rotate_actor", false}},
            {{"mode", 4}, {"head", true}, {"torso", true}, {"rotate_actor", true}}}},
          {"target_heights_by_subtype_and_quest_state",
           {{0.0}, {0.0}, {0.0}, {0.0}}},
          {"initial_routes",
           {{{"quest_state_index", 0},
             {"subtype", 0},
             {"preset_index", 0},
             {"mode_policy", "fixed"},
             {"initial_forced_mode", 4},
             {"evidence_call_site", 0x1000}},
            {{"quest_state_index", 0},
             {"subtype", 1},
             {"preset_index", 0},
             {"mode_policy", "fixed"},
             {"initial_forced_mode", 2},
             {"evidence_call_site", 0x1004}},
            {{"quest_state_index", 0},
             {"subtype", 2},
             {"preset_index", 0},
             {"mode_policy", "talk_state_zero"},
             {"initial_forced_mode", 1},
             {"engaged_mode_policy", "fixed"},
             {"engaged_forced_mode", 2},
             {"evidence_call_site", 0x1008}},
            {{"quest_state_index", 0},
             {"subtype", 3},
             {"preset_index", 0},
             {"mode_policy", "facing_threshold"},
             {"initial_forced_mode", 1},
             {"facing_mode", 2},
             {"not_facing_mode", 1},
             {"evidence_call_site", 0x100C}}}}}},
    };
}

void TestNativeNpcTracking() {
    const auto document = NativeNpcTrackingContractDocument();
    const auto contract =
        Oot3dNativeGame::ResolveNativeNpcTrackingContract(document);
    Expect(contract.Available && contract.Presets.size() == 1 &&
               contract.Modes.size() == 4 && contract.InitialRoutes.size() == 4,
           "resolve native NPC tracking contract");
    Expect(!contract.Modes[0].RotateActor && !contract.Modes[2].RotateActor &&
               contract.Modes[3].RotateActor,
           "decode native NPC tracking mode rotation masks");

    Oot3dNativeGame::NativeNpcTrackingTickInput input;
    input.TargetX = 100.0;
    auto nativeExecution = MakeNativeRandomExecution();
    uint32_t randomState = 1;
    Oot3dNativeGame::NativeNpcTrackingState modeFour;
    const bool modeFourUpdated = Oot3dNativeGame::UpdateNativeNpcTrackingTick(
        contract, input, modeFour, randomState, *nativeExecution);
    if (!modeFourUpdated) {
        std::cerr << "native NPC tracking test status: " << modeFour.Status
                  << ':' << modeFour.Error << '\n';
    }
    Expect(modeFourUpdated &&
               modeFour.TrackingMode == 4,
           "native mode four executes tracking service");

    input.Subtype = 1;
    Oot3dNativeGame::NativeNpcTrackingState modeTwo;
    Expect(Oot3dNativeGame::UpdateNativeNpcTrackingTick(
                   contract, input, modeTwo, randomState, *nativeExecution) &&
               modeTwo.TrackingMode == 2 && modeTwo.ShapeYaw == 0,
           "native mode two executes without rotating actor");

    input.Subtype = 2;
    Oot3dNativeGame::NativeNpcTrackingState talkState;
    Expect(Oot3dNativeGame::UpdateNativeNpcTrackingTick(
                   contract, input, talkState, randomState, *nativeExecution) &&
               talkState.TrackingMode == 1 && talkState.HeadYaw == 0 &&
               talkState.TorsoYaw == 0 && talkState.ShapeYaw == 0,
           "native idle talk route disables tracking");
    input.TalkState = 1;
    Expect(Oot3dNativeGame::UpdateNativeNpcTrackingTick(
                   contract, input, talkState, randomState, *nativeExecution) &&
               talkState.TrackingMode == 2,
           "native engaged talk route enables tracking");

    input.Subtype = 3;
    input.TalkState = 0;
    input.TargetX = 0.0;
    input.TargetZ = 100.0;
    Oot3dNativeGame::NativeNpcTrackingState facingState;
    Expect(Oot3dNativeGame::UpdateNativeNpcTrackingTick(
                   contract, input, facingState, randomState,
                   *nativeExecution) &&
               facingState.TrackingMode == 2,
           "native facing threshold selects tracking mode");
    input.TargetX = 300.0;
    input.TargetZ = 0.0;
    Expect(Oot3dNativeGame::UpdateNativeNpcTrackingTick(
                   contract, input, facingState, randomState,
                   *nativeExecution) &&
               facingState.TrackingMode == 1,
           "native facing threshold selects idle mode");

    const auto malformed = Oot3dNativeGame::ResolveNativeNpcTrackingContract(
        nlohmann::json{{"tracking_runtime",
                        {{"status", "initial_child_start_routes_recovered"}}}});
    Expect(!malformed.Available &&
               malformed.Status == "native_npc_tracking_contract_invalid",
           "malformed native NPC tracking contract fails closed");

    const auto limbContract =
        Oot3dNativeGame::ResolveNativeEnKoLimbCallbackContract(document);
    Expect(limbContract.Available && limbContract.TorsoLimbIndex == 9 &&
               limbContract.HeadLimbIndex == 10 &&
               limbContract.TorsoRotationOffset == 0x29A &&
               limbContract.HeadRotationOffset == 0x294,
           "resolve native EnKo limb callback contract");
    Oot3dNativeGame::NativeEnKoLimbCallbackState limbState;
    Oot3dNativeGame::NativeEnKoLimbCallbackInput limbInput;
    Expect(Oot3dNativeGame::UpdateNativeEnKoLimbCallback(
                   limbContract, limbInput, limbState, *nativeExecution) &&
               limbState.UpdateCount == 1 &&
               limbState.TorsoTransform[0] == 1.0f &&
               limbState.TorsoTransform[5] == 1.0f &&
               limbState.HeadTransform[10] == 1.0f,
           "execute original EnKo limb callback through A32");
}

nlohmann::json EnHollContract() {
    return {
        {"format", "oot3d_enholl_native_runtime_contract_v1"},
        {"status", "room_request_modes_4_6_complete"},
        {"actor_profile",
         {{"actor_id", 0x23},
          {"category", 10},
          {"instance_size", 0x1B4},
          {"init_address", 0x001B3CEC},
          {"destroy_address", 0x001B3D60},
          {"update_address", 0x001F692C},
          {"draw_address", 0x001F6614}}},
        {"parameter_layout",
         {{"action_selector_shift", 6},
          {"action_selector_mask", 7},
          {"transition_index_shift", 10}}},
        {"action_table",
         {{"entry_count", 7},
          {"handlers",
           {0x003F1D7C, 0x00239934, 0x00275264, 0x001EAAB0,
            0x001EAC68, 0x0021DF78, 0x001EAC68}}}},
        {"room_request_trigger",
         {{"handler_address", 0x001EAC68},
          {"next_action_address", 0x001CE6B8},
          {"narrow_mode", 6},
          {"supported_modes", {4, 6}},
          {"bounds",
           {{"vertical_min", -50.0},
            {"vertical_max", 200.0},
            {"absolute_depth_min", 50.0},
            {"absolute_depth_max", 100.0},
            {"default_half_width", 200.0},
            {"narrow_half_width", 100.0},
            {"strict_comparisons", true}}}}},
    };
}

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        TestNativeObjectBankResidency();
        TestNativeRoomPrepareDrawOperation();
        TestNativeNpcTracking();
        const auto roomUnit = RoomUnit();
        Oot3dNativeGame::NativeRoomRuntime roomRuntime(roomUnit);
        Expect(roomRuntime.CurrentRoom() == 0 && roomRuntime.PreviousRoom() == -1 &&
                   roomRuntime.IsRoomLoaded(0),
               "native initial room residency");
        Oot3d::RoomCompilationActorEntry transition;
        transition.transition = true;
        transition.actorId = 35;
        transition.sourceIndex = 1;
        transition.frontRoomIndex = 0;
        transition.backRoomIndex = 2;
        transition.params = 319;
        Expect(roomRuntime.ShouldSpawnTransitionActor(transition),
               "initial adjacent transition actor residency");
        Expect(roomRuntime.TransitionSpawnActorId(transition) == 35 &&
                   roomRuntime.TransitionSpawnParams(transition) == 1343,
               "native transition actor spawn derivation");

        int32_t droppedRoom = -2;
        Expect(roomRuntime.RequestRoom(1, &droppedRoom) && droppedRoom == -1 &&
                   roomRuntime.CurrentRoom() == 1 && roomRuntime.PreviousRoom() == 0 &&
                   !roomRuntime.IsRoomLoaded(1),
               "native room request current/previous ownership transfer");
        Expect(!roomRuntime.RequestRoom(2), "reject overlapping native room request");
        Expect(roomRuntime.CompleteRoomRequest() && roomRuntime.IsRoomLoaded(1),
               "native room request completion");
        Expect(roomRuntime.RequestRoom(2, &droppedRoom) && droppedRoom == 0 &&
                   roomRuntime.ResourceCleanupPending() &&
                   roomRuntime.CleanupTicksRemaining() == 1,
               "native previous room cleanup queue");
        Expect(!roomRuntime.AdvanceResourceCleanup() &&
                   roomRuntime.CleanupTicksRemaining() == 0 &&
                   roomRuntime.ShouldSpawnTransitionActor(transition) &&
                   !roomRuntime.ShouldRetainRoomActor(droppedRoom),
               "native delayed cleanup tick and transition adjacency");
        Expect(roomRuntime.AdvanceResourceCleanup() &&
                   !roomRuntime.ResourceCleanupPending() &&
                   roomRuntime.ResourceCleanupCompletionCount() == 1,
               "native delayed cleanup completion");
        Expect(roomRuntime.CompleteRoomRequest() && roomRuntime.ResidentRooms().size() == 2,
               "native two-room residency bound");

        const auto enHoll =
            Oot3dNativeGame::ResolveNativeEnHollTriggerContract(EnHollContract());
        Expect(enHoll.Available &&
                   Oot3dNativeGame::ResolveNativeEnHollActionMode(enHoll, 0x053F) == 4,
               "native EnHoll action selector");
        Oot3dNativeGame::NativeEnHollTriggerInput enHollInput;
        enHollInput.ActorPosition = {-785.0, 120.0, 1219.0};
        enHollInput.FocusPosition = {-785.0, 120.0, 1150.0};
        enHollInput.FrontRoom = 0;
        enHollInput.BackRoom = 2;
        enHollInput.CurrentRoom = 2;
        enHollInput.Mode = 4;
        auto enHollEvaluation =
            Oot3dNativeGame::EvaluateNativeEnHollRoomRequestTrigger(enHoll,
                                                                    enHollInput);
        Expect(enHollEvaluation.Supported && enHollEvaluation.InsideTrigger &&
                   enHollEvaluation.RoomRequestRequired &&
                   enHollEvaluation.Side == 0 && enHollEvaluation.TargetRoom == 0 &&
                   enHollEvaluation.LocalPosition.Z == -69.0,
               "native EnHoll front-side room request");
        enHollInput.FocusPosition = {-635.0, 120.0, 1288.0};
        enHollInput.CurrentRoom = 0;
        enHollInput.Mode = 6;
        enHollEvaluation =
            Oot3dNativeGame::EvaluateNativeEnHollRoomRequestTrigger(enHoll,
                                                                    enHollInput);
        Expect(enHollEvaluation.Supported && !enHollEvaluation.InsideTrigger &&
                   enHollEvaluation.HalfWidth == 100.0,
               "native EnHoll narrow mode width");
        enHollInput.Mode = 4;
        enHollEvaluation =
            Oot3dNativeGame::EvaluateNativeEnHollRoomRequestTrigger(enHoll,
                                                                    enHollInput);
        Expect(enHollEvaluation.InsideTrigger && enHollEvaluation.Side == 1 &&
                   enHollEvaluation.TargetRoom == 2,
               "native EnHoll back-side room request");
        enHollInput.FocusPosition = {-785.0, 120.0, 1269.0};
        enHollEvaluation =
            Oot3dNativeGame::EvaluateNativeEnHollRoomRequestTrigger(enHoll,
                                                                    enHollInput);
        Expect(!enHollEvaluation.InsideTrigger,
               "native EnHoll strict depth boundary");
        enHollInput.ActorYaw = 0x4000;
        enHollInput.FocusPosition = {-716.0, 120.0, 1219.0};
        enHollEvaluation =
            Oot3dNativeGame::EvaluateNativeEnHollRoomRequestTrigger(enHoll,
                                                                    enHollInput);
        Expect(enHollEvaluation.InsideTrigger && enHollEvaluation.Side == 1 &&
                   std::abs(enHollEvaluation.LocalPosition.Z - 69.0) < 0.0001,
               "native EnHoll actor-local yaw transform");

        const nlohmann::json semanticStates = {
            { { "index", 0 }, { "semantic", "child_start" },
              { "conditions", { { "player.age", "child" },
                                  { "quest.kokiri_emerald", "false" } } } },
            { { "index", 1 }, { "semantic", "child_stone" },
              { "conditions", { { "player.age", "child" },
                                  { "quest.kokiri_emerald", "true" } } } },
        };
        const auto semanticState = Oot3dNativeGame::ResolveNativeActorSemanticState(
            semanticStates,
            { { "player.age", "child" }, { "quest.kokiri_emerald", "false" } });
        Expect(semanticState.Available && semanticState.Index == 0 &&
                   semanticState.Semantic == "child_start",
               "source-backed actor semantic state selection");

        const nlohmann::json blinkContract = {
            { "face_runtime", {
                { "blink_sequence", { 0, 1, 2, 1 } },
                { "blink_timer_base", 30 },
                { "blink_timer_range", 30 },
                { "random", {
                    { "algorithm", "oot3d_code_bin_rand_zero_one" },
                    { "function", "Rand_ZeroOne" },
                    { "function_address", 0x003759D0 },
                    { "function_size", 0x38 },
                    { "function_sha256", std::string(64, 'c') },
                    { "return_abi", "aapcs_vfp_s0_f32" },
                    { "state_address", 0x0050C0C4 },
                    { "initial_state", 1 },
                    { "multiplier", 0x0019660D },
                    { "increment", 0x3C6EF35F },
                } },
            } },
        };
        const auto blinkProfile =
            Oot3dNativeGame::ResolveNativeActorBlinkProfile(blinkContract);
        Expect(blinkProfile.Available && blinkProfile.Sequence.size() == 4,
               "source-backed blink profile");
        Oot3dNativeGame::NativeActorBlinkState blinkState;
        uint32_t randomState = blinkProfile.RandomInitialState;
        auto blinkExecution = MakeNativeRandomExecution();
        Expect(Oot3dNativeGame::AdvanceNativeActorBlinkTick(
                   blinkProfile, blinkState, randomState, *blinkExecution),
               "blink half-close native tick execution");
        Expect(blinkState.SequenceIndex == 1, "blink half-close frame");
        Expect(Oot3dNativeGame::AdvanceNativeActorBlinkTick(
                   blinkProfile, blinkState, randomState, *blinkExecution),
               "blink close native tick execution");
        Expect(blinkState.SequenceIndex == 2, "blink closed frame");
        Expect(Oot3dNativeGame::AdvanceNativeActorBlinkTick(
                   blinkProfile, blinkState, randomState, *blinkExecution),
               "blink reopen native tick execution");
        Expect(Oot3dNativeGame::AdvanceNativeActorBlinkTick(
                   blinkProfile, blinkState, randomState, *blinkExecution),
               "blink hold native random execution");
        Expect(blinkState.SequenceIndex == 0 && blinkState.Timer >= 30 &&
                   blinkState.Timer < 60,
               "blink native random hold interval");

        auto contract = Oot3dNativeGame::ActorCoreContract::LoadFile(WriteContract());
        const uint32_t noop = contract.Function("Actor_Noop").Address;
        Oot3dNativeGame::NativeActorRuntimeConfig runtimeConfig;
        runtimeConfig.CompilationUnit =
            std::make_shared<const Oot3d::RoomCompilationUnit>(RoomUnit());
        Oot3dNativeGame::NativeActorRuntime runtime(std::move(contract), runtimeConfig);
        const auto behaviorDiagnostics =
            runtime.Diagnostics().at("room_compilation_unit");
        Expect(behaviorDiagnostics.at("behavior_graph_profile_count") == 1 &&
                   behaviorDiagnostics.at("behavior_function_count") == 1 &&
                   behaviorDiagnostics.at("bound_behavior_function_count") == 1 &&
                   behaviorDiagnostics.at("unbound_behavior_function_count") == 0 &&
                   behaviorDiagnostics.at("behavior_structure_field_count") == 1 &&
                   behaviorDiagnostics.at("behavior_profiles").size() == 1,
               "room compilation behavior graph consumer diagnostics");

        auto enRiverContract =
            Oot3dNativeGame::ActorCoreContract::LoadFile(WriteContract());
        Oot3dNativeGame::NativeActorRuntimeConfig enRiverConfig;
        enRiverConfig.CompilationUnit =
            std::make_shared<const Oot3d::RoomCompilationUnit>(
                EnRiverSoundRoomUnit());
        enRiverConfig.NativeClosureManifestPath =
            WriteEnRiverSoundClosureCatalog();
        Oot3dNativeGame::NativeActorRuntime enRiverRuntime(
            std::move(enRiverContract), enRiverConfig);
        auto enRiverEntry = Entry(59, 0);
        enRiverEntry.Params = 1;
        auto* enRiverActor = enRiverRuntime.Spawn(
            enRiverEntry,
            Profile(59, 1, 0x00283870, 0x002A5AB4, 0x00283924));
        Expect(enRiverActor != nullptr &&
                   enRiverActor->State ==
                       Oot3dNativeGame::NativeActorLifecycleState::Active,
               "native EnRiverSound supported init branch");
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene enRiverScene;
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene enRiverRenderScene;
        enRiverRuntime.Update(enRiverScene, enRiverRenderScene, 1.0 / 30.0);
        const auto enRiverDiagnostics =
            enRiverRuntime.Diagnostics().at("enriver_sound_runtime");
        Expect(enRiverDiagnostics.at("binding_status") ==
                   "native_params_branch_lifecycle_bound_audio_service_unavailable" &&
                   enRiverDiagnostics.at("active_instance_count") == 1 &&
                   enRiverDiagnostics.at("instances").at(0).at("sound_id") == 1 &&
                   enRiverDiagnostics.at("instances").at(0).at("update_count") == 1,
               "native EnRiverSound lifecycle diagnostics");

        auto* first = runtime.Spawn(Entry(10, 0), Profile(10, 4, noop, noop, noop));
        auto* second = runtime.Spawn(Entry(11, 1), Profile(11, 4, noop, noop, noop));
        Expect(first != nullptr && second != nullptr, "spawn native actors");
        Expect(runtime.LiveCount() == 2, "live count after spawn");
        Expect(runtime.CategoryList(4).front() == second->RuntimeId, "native prepend order");
        Expect(runtime.Find(10, 4) == first, "find by id and category");

        constexpr uint32_t pendingInit = 0x9000;
        constexpr uint32_t customUpdate = 0x9004;
        auto* third = runtime.Spawn(Entry(12, 2), Profile(12, 7, pendingInit, customUpdate, noop));
        Expect(third != nullptr, "third actor within native guard");
        Expect(third->State == Oot3dNativeGame::NativeActorLifecycleState::PendingNativeInit,
               "unknown init remains pending");
        Expect(runtime.Spawn(Entry(13, 3), Profile(13, 7, noop, noop, noop)) == nullptr,
               "native total guard");

        int initCalls = 0;
        int updateCalls = 0;
        runtime.RegisterCallback(pendingInit, [&](auto&) { ++initCalls; });
        runtime.RegisterCallback(customUpdate, [&](auto&) { ++updateCalls; });
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
        runtime.Update(scene, renderScene, 1.0 / 30.0);
        Expect(initCalls == 1 && updateCalls == 0, "init frame does not update actor");
        runtime.Update(scene, renderScene, 1.0 / 30.0);
        Expect(updateCalls == 1 && third->UpdateCount == 1, "active update callback");

        Expect(runtime.ChangeCategory(first->RuntimeId, 6), "change category");
        Expect(runtime.CategoryList(6).front() == first->RuntimeId, "category change prepends");
        Expect(runtime.Kill(first->RuntimeId), "kill actor");
        runtime.Update(scene, renderScene, 1.0 / 30.0);
        Expect(first->State == Oot3dNativeGame::NativeActorLifecycleState::Deleted,
               "destroy and delete actor");
        Expect(runtime.LiveCount() == 2, "live count after delete");

        std::cout << "oot3d_native_actor_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_actor_runtime_tests: " << ex.what() << '\n';
        return 1;
    }
}
