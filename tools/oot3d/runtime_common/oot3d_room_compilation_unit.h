#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3d {

inline constexpr std::string_view kRoomCompilationUnitCatalogResource =
    "oot3d/catalog/oot3d_room_compilation_units.json";
inline constexpr std::string_view kRoomCompilationUnitResourceRoot = "oot3d/room_units/";

struct RoomCompilationVec3s {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
};

struct RoomCompilationActorEntry {
    std::string instanceKey;
    std::string profileKey;
    std::string sourceKind;
    std::string actorName;
    bool transition = false;
    int32_t roomIndex = -1;
    int32_t frontRoomIndex = -1;
    int32_t backRoomIndex = -1;
    int32_t frontEffect = 0;
    int32_t backEffect = 0;
    int32_t actorId = -1;
    int32_t sourceIndex = -1;
    int64_t sourceOffset = -1;
    RoomCompilationVec3s position;
    RoomCompilationVec3s rotation;
    int16_t params = 0;
};

struct RoomCompilationActorCallback {
    bool present = false;
    uint32_t runtimeAddress = 0;
    std::string slot;
    std::string name;
    std::string address;
    std::string evidenceStatus;
    std::string runtimeBindingStatus;
};

struct RoomCompilationActorBehaviorFunction {
    uint32_t runtimeAddress = 0;
    uint32_t byteLength = 0;
    std::string name;
    std::string family;
    std::string confidence;
    std::string returnType;
    std::string parameterTypes;
    std::string parameterNames;
    std::string source;
    std::string evidenceFile;
    std::string signatureEvidenceFile;
    std::string discoveryKind;
    std::string closureKind;
    std::string ownerResolution;
    std::string consumerRole;
    std::string bodyStatus;
    uint32_t sourceTranche = 0;
    uint32_t consumerDepth = 0;
};

struct RoomCompilationActorConsumerRoot {
    uint32_t runtimeAddress = 0;
    std::string slot;
    std::string name;
    std::string bodyStatus;
};

struct RoomCompilationActorConsumerCallEdge {
    uint32_t callerAddress = 0;
    uint32_t calleeAddress = 0;
    std::string callerName;
    std::string calleeName;
    std::string relation;
};

struct RoomCompilationActorBehaviorTransition {
    uint32_t literalAddress = 0;
    uint32_t targetAddress = 0;
    std::string sourceFunction;
    std::string targetName;
    std::string evidence;
    std::string status;
    std::string evidenceFile;
};

struct RoomCompilationActorStructureField {
    uint32_t offset = 0;
    std::string type;
    std::string name;
    std::string confidence;
    std::string source;
    std::string evidenceFile;
};

struct RoomCompilationActorBehaviorGraph {
    bool available = false;
    std::string status;
    std::string owner;
    std::string structure;
    std::string runtimeBindingStatus;
    std::string consumerStatus;
    std::string consumerRuntimeBindingStatus;
    uint32_t structureSize = 0;
    uint32_t indirectRootCount = 0;
    uint32_t nativeAbiFunctionCount = 0;
    uint32_t consumerRootCount = 0;
    uint32_t consumerFunctionCount = 0;
    uint32_t consumerCallEdgeCount = 0;
    uint32_t actorLocalConsumerFunctionCount = 0;
    uint32_t nativeServiceDependencyCount = 0;
    std::vector<std::string> initialActionCandidates;
    std::vector<std::string> unresolvedConsumerDependencies;
    std::vector<RoomCompilationActorBehaviorFunction> functions;
    std::vector<RoomCompilationActorBehaviorTransition> actionTransitions;
    std::vector<RoomCompilationActorStructureField> structureFields;
    std::vector<RoomCompilationActorConsumerRoot> consumerRoots;
    std::vector<RoomCompilationActorConsumerCallEdge> consumerCallEdges;

    const RoomCompilationActorBehaviorFunction* FindFunction(
        std::string_view name) const;
};

struct RoomCompilationIndirectRootCatalog {
    bool available = false;
    std::string status;
    std::string sourceSnapshotId;
    std::string ownershipPolicy;
    std::string runtimeBindingStatus;
    uint32_t rootCount = 0;
    uint32_t actorCandidateCount = 0;
    uint32_t profileBoundRootCount = 0;
    uint32_t unboundActorCandidateCount = 0;
    std::unordered_map<std::string, uint32_t> familyCounts;
    std::vector<std::string> sourceCatalogs;
};

struct RoomCompilationNativeAbiCatalogSummary {
    bool available = false;
    std::string status;
    std::string sourceSnapshotId;
    std::string sourceBaseRevision;
    std::string codeBinSha256;
    std::string payloadSha256;
    std::string ownershipPolicy;
    std::string runtimeBindingStatus;
    uint32_t functionCount = 0;
    uint32_t nativePointerFunctionCount = 0;
    uint32_t profileBoundFunctionCount = 0;
    std::unordered_map<std::string, uint32_t> closureKindCounts;
};

struct RoomCompilationActorProfile {
    std::string profileKey;
    std::string actorName;
    std::string objectRomPath;
    std::string profileAddress;
    std::string overlayEntryAddress;
    std::string overlayTableAddress;
    std::string actorInitEvidenceStatus;
    std::string runtimeBindingStatus;
    int32_t actorId = -1;
    int32_t objectId = -1;
    int32_t category = -1;
    uint32_t flags = 0;
    uint32_t instanceSize = 0;
    uint32_t profileRuntimeAddress = 0;
    uint32_t overlayEntryRuntimeAddress = 0;
    uint32_t overlayTableRuntimeAddress = 0;
    std::unordered_map<std::string, uint32_t> callbackEvidenceCounts;
    std::vector<RoomCompilationActorCallback> callbacks;
    RoomCompilationActorBehaviorGraph behaviorGraph;

    const RoomCompilationActorCallback* FindCallback(std::string_view slot) const;
};

struct RoomCompilationObjectDependency {
    int32_t objectId = -1;
    std::string logicalPath;
    std::string payloadStatus;
    std::vector<std::string> requiredBy;
};

struct RoomCompilationSceneCommand {
    uint8_t commandId = 0;
    uint8_t parameter = 0;
    uint32_t commandWord = 0;
    uint32_t argument = 0;
};

struct RoomCompilationSceneAudioSettings {
    bool available = false;
    uint8_t soundSpecId = 0;
    uint8_t natureAmbienceId = 0;
    uint32_t bgmSoundId = 0;
    uint32_t commandWord = 0;
};

struct RoomCompilationRoom {
    int32_t roomIndex = -1;
    int32_t setupIndex = -1;
    bool initiallyActive = false;
    std::string sourcePath;
    std::vector<RoomCompilationSceneCommand> commands;
    std::vector<std::string> meshNames;
    std::vector<int32_t> objectIds;
    std::vector<std::string> actorInstanceKeys;
};

struct RoomCompilationEntrypoint {
    int32_t globalEntranceIndex = -1;
    int32_t localEntranceIndex = -1;
    int32_t spawnIndex = -1;
    int32_t roomIndex = -1;
    int32_t actorId = -1;
    RoomCompilationVec3s position;
    RoomCompilationVec3s rotation;
    uint16_t params = 0;
};

struct RoomCompilationLifecycleFunction {
    std::string role;
    std::string name;
    std::string returnType;
    std::string parameterTypes;
    std::string confidence;
    std::string source;
    std::string notes;
    uint32_t runtimeAddress = 0;
};

struct RoomCompilationLifecycleContract {
    bool available = false;
    std::string format;
    std::string status;
    std::string evidenceSource;
    std::string sourceSnapshotId;
    uint8_t idleState = 0;
    uint8_t loadingState = 0;
    uint8_t terminalState = 0;
    uint32_t maxResidentRoomCount = 0;
    uint32_t initialBufferCount = 0;
    uint32_t bufferCountWithoutTransitionActors = 0;
    uint32_t bufferCountWithTransitionActors = 0;
    std::string roomActorRetentionPolicy;
    std::string transitionActorResidencyPolicy;
    uint32_t transitionActorIdSpawnMask = 0;
    uint32_t transitionActorParamsIndexStride = 0;
    std::string transitionActorSpawnMarker;
    bool previousRoomCleanupBeforeNextRequest = false;
    uint32_t cleanupDelayTicks = 0;
    bool roomCommandsInstallOnRequestCompletion = false;
    std::vector<RoomCompilationLifecycleFunction> functions;

    const RoomCompilationLifecycleFunction* FindFunction(std::string_view role) const;
};

struct RoomCompilationFactPredicate {
    std::string key;
    std::string equals;
};

struct RoomCompilationTevAlphaSelector {
    uint32_t defaultValue = 0;
    int32_t excludedSetupIndex = -1;
    int32_t drawParamSetupIndex = -1;
    uint32_t drawParamIndex = 0;
    uint32_t progressionValue = 0;
    int32_t setupLessThan = 0;
    RoomCompilationFactPredicate alternateFact;
    RoomCompilationFactPredicate requiredFact;
};

struct RoomCompilationTevAlphaRandom {
    uint32_t functionAddress = 0;
    uint32_t functionSize = 0;
    std::string functionSha256;
    uint32_t stateAddress = 0;
    int32_t offset = 0;
    int32_t range = 0;
    uint32_t initialState = 0;
    uint32_t multiplier = 0;
    uint32_t increment = 0;
    uint32_t scaleF32Bits = 0;
    uint32_t middleTestAddU32 = 0;
    uint32_t middleTestLessThanU32 = 0;
    uint32_t lowCompareF32Bits = 0;
};

struct RoomCompilationMaterialTevAlphaOperation {
    int32_t roomIndex = -1;
    uint32_t resourceIndex = 0;
    std::vector<int32_t> materialIndices;
    uint32_t constantIndex = 0;
    uint32_t nativeOperation = 0;
    std::array<uint32_t, 3> colorRgbF32Bits = {};
    uint32_t alphaBaseF32Bits = 0;
    uint32_t alphaSelectorScaleF32Bits = 0;
    RoomCompilationTevAlphaSelector selector;
    RoomCompilationTevAlphaRandom random;
};

struct RoomCompilationSceneCallback {
    uint32_t runtimeAddress = 0;
    std::string role;
    std::string name;
    std::string evidenceStatus;
};

struct RoomCompilationSceneCallbackContract {
    bool available = false;
    bool executable = false;
    int32_t configIndex = -1;
    std::string configName;
    std::string status;
    std::string sourceRegistry;
    std::string functionSha256;
    uint32_t functionSize = 0;
    uint32_t literalVerificationCount = 0;
    std::vector<RoomCompilationSceneCallback> callbacks;
    std::vector<RoomCompilationMaterialTevAlphaOperation> materialTevAlphaOperations;
};

struct RoomCompilationUnit {
    std::string routeId;
    std::string unitId;
    std::string scenePath;
    std::string status;
    std::string behaviorEvidenceStatus;
    std::string payloadSha256;
    std::string resourcePath;
    std::string sourceCodeBinPath;
    std::string sourceCodeBinSha256;
    int32_t sceneId = -1;
    int32_t setupIndex = -1;
    int32_t initialRoomIndex = -1;
    uint32_t unresolvedCount = 0;
    uint32_t behaviorGraphProfileCount = 0;
    uint32_t behaviorFunctionCount = 0;
    uint32_t behaviorActionTransitionCount = 0;
    uint32_t behaviorStructureFieldCount = 0;
    uint32_t behaviorIndirectRootCount = 0;
    uint32_t behaviorNativeAbiFunctionCount = 0;
    uint32_t behaviorConsumerRootCount = 0;
    uint32_t behaviorConsumerFunctionCount = 0;
    uint32_t behaviorConsumerCallEdgeCount = 0;
    uint32_t behaviorActorLocalConsumerFunctionCount = 0;
    uint32_t behaviorNativeServiceDependencyCount = 0;
    std::vector<int32_t> nativeRoomIndices;
    std::vector<RoomCompilationSceneCommand> sceneCommands;
    RoomCompilationSceneAudioSettings sceneAudio;
    RoomCompilationEntrypoint entrypoint;
    std::vector<RoomCompilationRoom> rooms;
    std::vector<RoomCompilationActorEntry> actorInstances;
    std::vector<RoomCompilationActorProfile> actorProfiles;
    std::vector<RoomCompilationObjectDependency> objectDependencies;
    RoomCompilationLifecycleContract roomLifecycle;
    RoomCompilationSceneCallbackContract roomCallbacks;
    RoomCompilationIndirectRootCatalog indirectRootCatalog;
    RoomCompilationNativeAbiCatalogSummary nativeAbiCatalog;

    const RoomCompilationRoom* FindRoom(int32_t roomIndex) const;
    const RoomCompilationActorProfile* FindActorProfile(std::string_view profileKey) const;
    const RoomCompilationObjectDependency* FindObjectDependency(int32_t objectId) const;
};

struct RoomCompilationUnitCatalog {
    uint32_t excludedUnitCount = 0;
    std::vector<RoomCompilationUnit> units;

    const RoomCompilationUnit* Find(std::string_view routeId, int32_t setupIndex) const;
};

using RoomCompilationPayloadLoader = std::function<std::string(std::string_view resourcePath)>;

RoomCompilationUnit ParseRoomCompilationUnit(std::string_view payload);
RoomCompilationUnit LoadRoomCompilationUnitFile(const std::filesystem::path& path);
RoomCompilationUnitCatalog ParseRoomCompilationUnitCatalog(
    std::string_view catalogPayload, const RoomCompilationPayloadLoader& payloadLoader);

} // namespace Oot3d
