#include "oot3d_native_actor_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

#include "oot3d_native_skel_anime.h"

namespace Oot3dNativeGame {
namespace {

std::vector<uint8_t> ReadNativeBinary(const std::filesystem::path& path,
                                      const char* role) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(std::string("could not open native ") + role + ": " +
                                 path.string());
    }
    const auto length = input.tellg();
    if (length < 0) {
        throw std::runtime_error(std::string("could not size native ") + role);
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw std::runtime_error(std::string("could not read native ") + role);
    }
    return bytes;
}

constexpr std::string_view kEnKoRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-enko-native-runtime.json";
constexpr std::string_view kEnSaRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-ensa-native-runtime.json";
constexpr std::string_view kEnMdRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-enmd-native-runtime.json";
constexpr std::string_view kEnKusaRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-enkusa-native-runtime.json";
constexpr std::string_view kObjHanaRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-objhana-native-runtime.json";
constexpr std::string_view kRigidActorRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-rigid-actors-native-runtime.json";
constexpr std::string_view kItemActorRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-item-actors-native-runtime.json";
constexpr std::string_view kEnHollRuntimeContractResource =
    "oot3d/catalog/contracts/oot3d-enholl-native-runtime.json";

const ThreeDsRecomp::Oot3d::SemanticGameplayFact* FindGameplayFact(
    const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact>& facts, std::string_view key) {
    const auto found = std::find_if(facts.begin(), facts.end(),
                                    [key](const auto& fact) { return fact.Key == key; });
    return found == facts.end() ? nullptr : &*found;
}

bool ReadJsonU32(const nlohmann::json& value, uint32_t& result) {
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        return false;
    }
    const auto candidate = value.get<int64_t>();
    if (candidate < 0 || candidate > UINT32_MAX) {
        return false;
    }
    result = static_cast<uint32_t>(candidate);
    return true;
}

bool ReadJsonFiniteDouble(const nlohmann::json& value, double& result) {
    if (!value.is_number()) {
        return false;
    }
    result = value.get<double>();
    return std::isfinite(result);
}

bool NativeRandomProfilesMatch(const NativeActorBlinkProfile& left,
                               const NativeActorBlinkProfile& right) {
    return left.RandomFunctionAddress == right.RandomFunctionAddress &&
           left.RandomFunctionSize == right.RandomFunctionSize &&
           left.RandomFunctionSha256 == right.RandomFunctionSha256 &&
           left.RandomStateAddress == right.RandomStateAddress &&
           left.RandomInitialState == right.RandomInitialState &&
           left.RandomMultiplier == right.RandomMultiplier &&
           left.RandomIncrement == right.RandomIncrement;
}

bool IsSha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

bool NativeActorContractMatchesCodeBin(const nlohmann::json& contract,
                                       std::string_view codeBinSha256) {
    const auto source = contract.find("source");
    return source != contract.end() && source->is_object() &&
           source->value("code_bin_sha256", "") == codeBinSha256;
}

NativeActorSpawnEntry SpawnEntryFromRoomActor(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRoomActorEntry& actor, bool renderBound) {
    NativeActorSpawnEntry entry;
    entry.EntryIndex = actor.Index;
    entry.ActorId = static_cast<int16_t>(actor.ActorId);
    entry.Position = actor.Position;
    entry.Rotation = actor.Rotation;
    entry.Params = static_cast<int16_t>(actor.Params);
    entry.RenderBound = renderBound;
    return entry;
}

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 CompilationVec(const Oot3d::RoomCompilationVec3s& value) {
    return {static_cast<double>(value.x), static_cast<double>(value.y),
            static_cast<double>(value.z)};
}

ThreeDsRecomp::Oot3d::Matrix4f BuildEnvironmentActorTransform(
    double scale, double shapeYOffset,
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& modelLocalTranslation,
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& rotation,
    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& worldPosition) {
    auto drawPosition = worldPosition;
    drawPosition.Y += shapeYOffset * scale;
    auto transform = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(
        scale, rotation, drawPosition);
    transform.M[0][3] += transform.M[0][0] * modelLocalTranslation.X +
                         transform.M[0][1] * modelLocalTranslation.Y +
                         transform.M[0][2] * modelLocalTranslation.Z;
    transform.M[1][3] += transform.M[1][0] * modelLocalTranslation.X +
                         transform.M[1][1] * modelLocalTranslation.Y +
                         transform.M[1][2] * modelLocalTranslation.Z;
    transform.M[2][3] += transform.M[2][0] * modelLocalTranslation.X +
                         transform.M[2][1] * modelLocalTranslation.Y +
                         transform.M[2][2] * modelLocalTranslation.Z;
    return transform;
}

ThreeDsRecomp::Oot3d::Matrix4f NativeMtx3x4ToPoseMatrix(
    const std::array<float, 12>& nativeMatrix) {
    ThreeDsRecomp::Oot3d::Matrix4f result{};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            result.M[row][column] = nativeMatrix[row * 4 + column];
        }
    }
    result.M[3][3] = 1.0f;
    return result;
}

NativeActorSpawnEntry SpawnEntryFromCompilationActor(const Oot3d::RoomCompilationActorEntry& actor,
                                                     bool renderBound) {
    NativeActorSpawnEntry entry;
    entry.InstanceKey = actor.instanceKey;
    entry.ProfileKey = actor.profileKey;
    entry.EntryIndex = actor.sourceIndex;
    entry.RoomIndex = actor.roomIndex;
    entry.ActorId = static_cast<int16_t>(actor.actorId);
    entry.Position = CompilationVec(actor.position);
    entry.Rotation = CompilationVec(actor.rotation);
    entry.Params = actor.params;
    entry.RenderBound = renderBound;
    return entry;
}

ThreeDsRecomp::Oot3d::NativeActorProfile ProfileFromCompilation(
    const Oot3d::RoomCompilationActorProfile& source) {
    ThreeDsRecomp::Oot3d::NativeActorProfile profile;
    profile.Valid = true;
    profile.SourceKind = "oot3d_room_compilation_unit_actor_init";
    profile.ActorId = static_cast<uint16_t>(source.actorId);
    profile.Category = static_cast<uint8_t>(source.category);
    profile.Flags = source.flags;
    profile.ObjectId = static_cast<uint16_t>(source.objectId);
    profile.InstanceSize = source.instanceSize;
    profile.ProfileAddress = source.profileRuntimeAddress;
    profile.OverlayEntryAddress = source.overlayEntryRuntimeAddress;
    profile.OverlayTableAddress = source.overlayTableRuntimeAddress;
    for (const auto& callback : source.callbacks) {
        if (callback.slot == "init") {
            profile.InitFunctionAddress = callback.runtimeAddress;
        } else if (callback.slot == "destroy") {
            profile.DestroyFunctionAddress = callback.runtimeAddress;
        } else if (callback.slot == "update") {
            profile.UpdateFunctionAddress = callback.runtimeAddress;
        } else if (callback.slot == "draw") {
            profile.DrawFunctionAddress = callback.runtimeAddress;
        }
    }
    return profile;
}

bool CompilationVecMatches(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value,
                           const Oot3d::RoomCompilationVec3s& expected) {
    return value.X == expected.x && value.Y == expected.y && value.Z == expected.z;
}

} // namespace

NativeActorSemanticStateSelection ResolveNativeActorSemanticState(
    const nlohmann::json& states, const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact>& facts) {
    NativeActorSemanticStateSelection result;
    if (!states.is_array()) {
        result.Status = "semantic_state_contract_unavailable";
        return result;
    }
    for (const auto& state : states) {
        if (!state.is_object() || !state.contains("conditions") ||
            !state.at("conditions").is_object()) {
            result.Status = "semantic_state_contract_invalid";
            return result;
        }
        bool matches = true;
        for (const auto& [key, expected] : state.at("conditions").items()) {
            const auto* fact = FindGameplayFact(facts, key);
            if (!expected.is_string() || fact == nullptr ||
                fact->Value != expected.get<std::string>()) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            continue;
        }
        uint32_t index = 0;
        if (!state.contains("index") || !ReadJsonU32(state.at("index"), index) ||
            !state.contains("semantic") || !state.at("semantic").is_string()) {
            result.Status = "semantic_state_contract_invalid";
            return result;
        }
        if (result.Available) {
            result.Available = false;
            result.Status = "semantic_state_ambiguous";
            return result;
        }
        result.Available = true;
        result.Index = index;
        result.Semantic = state.at("semantic").get<std::string>();
    }
    result.Status = result.Available ? "ready" : "semantic_state_facts_unresolved";
    return result;
}

NativeActorBlinkProfile ResolveNativeActorBlinkProfile(const nlohmann::json& contract) {
    NativeActorBlinkProfile result;
    const auto faceIt = contract.find("face_runtime");
    if (faceIt == contract.end() || !faceIt->is_object()) {
        result.Status = "native_blink_contract_unavailable";
        return result;
    }
    const auto& face = *faceIt;
    const auto randomIt = face.find("random");
    if (!face.contains("blink_sequence") || !face.at("blink_sequence").is_array() ||
        randomIt == face.end() || !randomIt->is_object() ||
        randomIt->value("algorithm", "") != "oot3d_code_bin_rand_zero_one" ||
        randomIt->value("function", "") != "Rand_ZeroOne" ||
        randomIt->value("return_abi", "") != "aapcs_vfp_s0_f32") {
        result.Status = "native_blink_contract_invalid";
        return result;
    }
    uint32_t timerBase = 0;
    uint32_t timerRange = 0;
    if (!face.contains("blink_timer_base") ||
        !ReadJsonU32(face.at("blink_timer_base"), timerBase) ||
        !face.contains("blink_timer_range") ||
        !ReadJsonU32(face.at("blink_timer_range"), timerRange) || timerBase > INT16_MAX ||
        timerRange > INT16_MAX || !randomIt->contains("function_address") ||
        !ReadJsonU32(randomIt->at("function_address"),
                     result.RandomFunctionAddress) ||
        !randomIt->contains("function_size") ||
        !ReadJsonU32(randomIt->at("function_size"), result.RandomFunctionSize) ||
        !randomIt->contains("function_sha256") ||
        !randomIt->at("function_sha256").is_string() ||
        !randomIt->contains("state_address") ||
        !ReadJsonU32(randomIt->at("state_address"), result.RandomStateAddress) ||
        !randomIt->contains("initial_state") ||
        !ReadJsonU32(randomIt->at("initial_state"), result.RandomInitialState) ||
        !randomIt->contains("multiplier") ||
        !ReadJsonU32(randomIt->at("multiplier"), result.RandomMultiplier) ||
        !randomIt->contains("increment") ||
        !ReadJsonU32(randomIt->at("increment"), result.RandomIncrement) ||
        result.RandomFunctionAddress == 0 || result.RandomFunctionSize == 0 ||
        result.RandomStateAddress == 0 || result.RandomMultiplier == 0) {
        result.Status = "native_blink_contract_invalid";
        return result;
    }
    result.RandomFunctionSha256 =
        randomIt->at("function_sha256").get<std::string>();
    if (!IsSha256(result.RandomFunctionSha256)) {
        result.Status = "native_blink_contract_invalid";
        return result;
    }
    for (const auto& value : face.at("blink_sequence")) {
        uint32_t frame = 0;
        if (!ReadJsonU32(value, frame) || frame > UINT8_MAX) {
            result.Status = "native_blink_contract_invalid";
            return result;
        }
        result.Sequence.push_back(static_cast<uint8_t>(frame));
    }
    if (result.Sequence.empty()) {
        result.Status = "native_blink_contract_invalid";
        return result;
    }
    result.TimerBase = static_cast<int16_t>(timerBase);
    result.TimerRange = static_cast<int16_t>(timerRange);
    result.Available = true;
    result.Status = "native_blink_contract_ready";
    return result;
}

std::optional<float> NextNativeActorRandom(
    const NativeActorBlinkProfile& profile, uint32_t& state,
    NativeA32ExecutionRuntime& nativeExecution) {
    if (!nativeExecution.Write32(profile.RandomStateAddress, state)) {
        return std::nullopt;
    }
    oot3d::recomp::a32::GuestState guestState;
    const auto call =
        nativeExecution.Call(profile.RandomFunctionAddress, guestState);
    uint32_t nextState = 0;
    if (!call.Completed ||
        !nativeExecution.Read32(profile.RandomStateAddress, &nextState)) {
        return std::nullopt;
    }
    const float value = std::bit_cast<float>(guestState.vfp[0]);
    if (!std::isfinite(value) || value < 0.0f || value >= 1.0f) {
        return std::nullopt;
    }
    state = nextState;
    return value;
}

bool AdvanceNativeActorBlinkTick(const NativeActorBlinkProfile& profile,
                                 NativeActorBlinkState& state,
                                 uint32_t& randomState,
                                 NativeA32ExecutionRuntime& nativeExecution) {
    if (!profile.Available || profile.Sequence.empty()) {
        return false;
    }
    if (state.Timer != 0) {
        --state.Timer;
        if (state.Timer != 0) {
            return true;
        }
    }
    ++state.SequenceIndex;
    if (state.SequenceIndex == profile.Sequence.size()) {
        state.SequenceIndex = 0;
        const auto random =
            NextNativeActorRandom(profile, randomState, nativeExecution);
        if (!random.has_value()) {
            return false;
        }
        state.Timer = static_cast<int16_t>(
            profile.TimerBase +
            static_cast<int16_t>(*random * profile.TimerRange));
    }
    return true;
}

const char* NativeActorLifecycleStateName(NativeActorLifecycleState state) {
    switch (state) {
    case NativeActorLifecycleState::PendingNativeInit:
        return "pending_native_init";
    case NativeActorLifecycleState::Active:
        return "active";
    case NativeActorLifecycleState::ActiveExternalOwner:
        return "active_external_owner";
    case NativeActorLifecycleState::PendingNativeDestroy:
        return "pending_native_destroy";
    case NativeActorLifecycleState::Deleted:
        return "deleted";
    }
    return "unknown";
}

NativeActorRuntime::NativeActorRuntime(ActorCoreContract contract, NativeActorRuntimeConfig config)
    : mContract(std::move(contract)), mConfig(std::move(config)),
      mCategoryLists(mContract.CategoryListCount()) {
    if (mConfig.NativeAudioOutputSampleRate == 0) {
        throw std::runtime_error("native actor runtime audio sample rate is zero");
    }
    mNativeAudioOutputSampleRate = mConfig.NativeAudioOutputSampleRate;
    if (!mConfig.NativeClosureManifestPath.empty()) {
        mNativeClosureCatalog =
            NativeClosureCatalog::LoadFile(mConfig.NativeClosureManifestPath);
        if (!mConfig.CompilationUnit ||
            mNativeClosureCatalog->RoomCompilationUnitId() !=
                mConfig.CompilationUnit->unitId ||
            mNativeClosureCatalog->RoomCompilationPayloadSha256() !=
                mConfig.CompilationUnit->payloadSha256) {
            throw std::runtime_error(
                "native closure corpus and Room Compilation Unit provenance differ");
        }
    }
    if (!mConfig.NativeCodeBinPath.empty()) {
        mNativeCodeBin = ReadNativeBinary(mConfig.NativeCodeBinPath, "code.bin");
        if (mConfig.NativeCodeBaseAddress == 0) {
            throw std::runtime_error(
                "native code.bin is configured without a guest base address");
        }
        mNativeA32Execution = std::make_unique<NativeA32ExecutionRuntime>(
            mNativeCodeBin, mConfig.NativeCodeBaseAddress);
    }
    if (!mConfig.NativeAudioArchivePath.empty()) {
        std::optional<std::filesystem::path> streamArchive;
        if (!mConfig.NativeStreamArchivePath.empty()) {
            streamArchive = mConfig.NativeStreamArchivePath;
        }
        mNativeAudioService = std::make_unique<NativeAudioService>(
            mConfig.NativeAudioArchivePath, std::move(streamArchive),
            mNativeAudioOutputSampleRate);
        if (!mConfig.NativeCodeBinPath.empty()) {
            mNativeAudioService->MountResamplerProfile(
                mConfig.NativeCodeBinPath);
        }
        if (mConfig.NativeAudioBehavior.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio behavior profile requires code.bin");
            }
            mNativeAudioService->MountBehaviorCatalog(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioBehavior);
        }
        if (mConfig.NativeAudioEnvelope.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio envelope profile requires code.bin");
            }
            mNativeAudioService->MountEnvelopeProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioEnvelope);
        }
        if (mConfig.NativeAudioModulation.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio modulation profile requires code.bin");
            }
            mNativeAudioService->MountModulationProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioModulation);
        }
        if (mConfig.NativeAudioSpatial.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio spatial profile requires code.bin");
            }
            mNativeAudioService->MountSpatialProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioSpatial);
        }
        if (mConfig.NativeAudioMix.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio mix profile requires code.bin");
            }
            mNativeAudioService->MountMixProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioMix);
        }
        if (mConfig.NativeAudioFilter.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio filter profile requires code.bin");
            }
            mNativeAudioService->MountFilterProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioFilter);
        }
        if (mConfig.NativeAudioReverb.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio reverb profile requires code.bin");
            }
            mNativeAudioService->MountReverbProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioReverb);
        }
        if (mConfig.NativeAudioScene.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio scene profile requires code.bin");
            }
            mNativeAudioService->MountSceneProfile(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioScene);
        }
        if (mConfig.NativeAudioSoundSpec.has_value()) {
            if (mConfig.NativeCodeBinPath.empty()) {
                throw std::runtime_error(
                    "native audio sound-spec catalog requires code.bin");
            }
            mNativeAudioService->MountSoundSpecCatalog(
                mConfig.NativeCodeBinPath, *mConfig.NativeAudioSoundSpec);
        }
    }
    RegisterEnRiverSoundCallbacks();
    RegisterCallback(mContract.Function("Actor_Noop").Address, [](NativeActorInstance&) {});
    mInitialized = true;
}

void NativeActorRuntime::Initialize(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    mCurrentScene = &scene;
    mCurrentRenderScene = &renderScene;
    InitializeNativeRenderServices();
    mActors.clear();
    mCategoryLists.assign(mContract.CategoryListCount(), {});
    mUnsupportedCallbacks.clear();
    mUnsupportedCallbackKeys.clear();
    mProfileGaps.clear();
    mObjectBankRuntime.reset();
    mObjectBankCleanupRooms.clear();
    mDeferredCompilationInstances.clear();
    mCompilationRuntimeIds.clear();
    mRoomRuntime.reset();
    mNextRuntimeId = 1;
    mFrameCount = 0;
    mLiveCount = 0;
    mSourceRoomActorCount = scene.AssetGraph.RoomActors.size();
    mSpawnedRoomActorCount = 0;
    mCompiledActorInstanceCount = 0;
    mCompiledActorProfileCount = 0;
    mCompiledRoomCount = 0;
    mCompiledObjectDependencyCount = 0;
    mArchiveSelectedVisualReplacementCount = 0;
    mPopulationFromCompilationUnit = false;
    mEnKoVisuals.clear();
    mNamedActorVisuals.clear();
    mEnvironmentActorVisuals.clear();
    mEnHollStates.clear();
    mEnRiverSoundStates.clear();
    if (mNativeAudioService) {
        mNativeAudioService->StopAll();
    }
    mFrameContext = {};

    if (mConfig.CompilationUnit) {
        InitializeFromCompilationUnit(scene);
    } else {
        InitializeLegacyScenePopulation(scene);
    }
    InitializeSceneAudio();
    for (const int32_t roomIndex : mConfig.RoomRequestSmokeSequence) {
        if (!RequestCompiledRoom(roomIndex)) {
            throw std::runtime_error("native Actor runtime room-request smoke failed");
        }
    }
    mInitialized = true;
}

void NativeActorRuntime::InitializeSceneAudio() {
    mSceneBgmStarted = false;
    if (!mConfig.CompilationUnit || !mConfig.CompilationUnit->sceneAudio.available) {
        mSceneAudioStatus = "native_scene_sound_settings_unavailable";
        return;
    }
    if (!mNativeAudioService) {
        mSceneAudioStatus = "native_scene_sound_settings_ready_audio_service_unavailable";
        return;
    }

    const auto& settings = mConfig.CompilationUnit->sceneAudio;
    if (!mNativeAudioService->ApplySceneAudio(
            settings.soundSpecId, settings.natureAmbienceId,
            settings.bgmSoundId)) {
        throw std::runtime_error(
            "native scene audio settings could not be applied");
    }
    mSceneBgmStarted = settings.bgmSoundId != 0;
    mSceneAudioStatus = settings.bgmSoundId == 0
        ? "native_scene_bgm_disabled"
        : settings.bgmSoundId == 0x7f
            ? "native_scene_nature_ambience_started"
            : "native_scene_bgm_started_from_zsi_sound_settings";
}

void NativeActorRuntime::SetFrameContext(const Oot3dDemoHostActorFrameContext& context) {
    mFrameContext = context;
    if (mNativeAudioService && context.ViewEyeValid) {
        mNativeAudioService->SetListener(
            {static_cast<float>(context.ViewEyeX),
             static_cast<float>(context.ViewEyeY),
             static_cast<float>(context.ViewEyeZ)},
            {static_cast<float>(context.ViewTargetX),
             static_cast<float>(context.ViewTargetY),
             static_cast<float>(context.ViewTargetZ)},
            {static_cast<float>(context.ViewUpX),
             static_cast<float>(context.ViewUpY),
             static_cast<float>(context.ViewUpZ)});
    }
}

void NativeActorRuntime::PrepareCompilationObjectBanks() {
    if (!mConfig.CompilationUnit) {
        return;
    }
    if (!mSources) {
        throw std::runtime_error(
            "native object-bank runtime requires mounted OOT3D resources");
    }
    mObjectBankRuntime = std::make_unique<NativeObjectBankRuntime>(
        *mConfig.CompilationUnit, *mSources);
    if (!mObjectBankRuntime->SetResidentRooms(
            {mConfig.CompilationUnit->initialRoomIndex})) {
        throw std::runtime_error(
            "initial native object-bank residency failed: " +
            mObjectBankRuntime->Error());
    }
}

bool NativeActorRuntime::CompilationObjectBankReady(int32_t objectId) const {
    return mObjectBankRuntime && mObjectBankRuntime->IsReady(objectId);
}

bool NativeActorRuntime::SetCompilationObjectBankResidentRooms(
    std::vector<int32_t> roomIndices) {
    return !mObjectBankRuntime ||
           mObjectBankRuntime->SetResidentRooms(std::move(roomIndices));
}

std::vector<int32_t> NativeActorRuntime::CompilationObjectBankRoomsWithCleanup() const {
    std::vector<int32_t> rooms =
        mRoomRuntime ? mRoomRuntime->ResidentRooms() : std::vector<int32_t>{};
    rooms.insert(rooms.end(), mObjectBankCleanupRooms.begin(),
                 mObjectBankCleanupRooms.end());
    std::sort(rooms.begin(), rooms.end());
    rooms.erase(std::unique(rooms.begin(), rooms.end()), rooms.end());
    return rooms;
}

void NativeActorRuntime::InitializeFromCompilationUnit(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    const auto& unit = *mConfig.CompilationUnit;
    if (!scene.PlayerStart.Valid || scene.ActiveSceneSetupIndex != unit.setupIndex ||
        scene.PlayerStart.SetupIndex != unit.setupIndex ||
        scene.PlayerStart.RequestedGlobalEntranceIndex != unit.entrypoint.globalEntranceIndex ||
        scene.PlayerStart.EntranceIndex != unit.entrypoint.localEntranceIndex ||
        scene.PlayerStart.SpawnIndex != unit.entrypoint.spawnIndex ||
        scene.PlayerStart.Room != unit.entrypoint.roomIndex ||
        scene.PlayerStart.ActorId != unit.entrypoint.actorId ||
        !CompilationVecMatches(scene.PlayerStart.Position, unit.entrypoint.position) ||
        !CompilationVecMatches(scene.PlayerStart.Rotation, unit.entrypoint.rotation) ||
        static_cast<uint16_t>(scene.PlayerStart.Params) != unit.entrypoint.params) {
        throw std::runtime_error("native scene state does not match its room "
                                 "compilation unit entrypoint");
    }

    mPopulationFromCompilationUnit = true;
    mCompiledRoomCount = unit.rooms.size();
    mCompiledActorInstanceCount = unit.actorInstances.size();
    mCompiledActorProfileCount = unit.actorProfiles.size();
    mCompiledObjectDependencyCount = unit.objectDependencies.size();
    mSourceRoomActorCount = 0;
    mRoomRuntime = std::make_unique<NativeRoomRuntime>(unit);
    PrepareCompilationObjectBanks();
    if (mRoomRenderRuntime) {
        mRoomRenderRuntime->CaptureInitialRoom(unit.initialRoomIndex,
                                               *mCurrentRenderScene);
    }

    for (const auto& actor : unit.actorInstances) {
        if (actor.sourceKind == "room_actor_list" && actor.roomIndex == unit.initialRoomIndex) {
            ++mSourceRoomActorCount;
        }
        if (actor.sourceKind != "scene_spawn") {
            continue;
        }
        const auto* profileSource = unit.FindActorProfile(actor.profileKey);
        if (profileSource == nullptr) {
            throw std::runtime_error("compiled scene spawn has no native profile");
        }
        auto entry = SpawnEntryFromCompilationActor(actor, false);
        entry.ExternalOwner = actor.sourceKind == "scene_spawn";
        if (!CompilationObjectBankReady(profileSource->objectId)) {
            RecordProfileGap(entry, "native_object_bank_not_prepared");
            SetDeferredCompilationInstance(actor, "native_object_bank_not_prepared");
            continue;
        }
        if (Spawn(entry, ProfileFromCompilation(*profileSource)) == nullptr) {
            SetDeferredCompilationInstance(actor, "native_actor_spawn_rejected");
        }
    }
    ReconcileCompilationResidency(scene);
}

void NativeActorRuntime::SetDeferredCompilationInstance(
    const Oot3d::RoomCompilationActorEntry& actor, std::string reason) {
    const auto existing =
        std::find_if(mDeferredCompilationInstances.begin(), mDeferredCompilationInstances.end(),
                     [&](const auto& deferred) {
                         return deferred.InstanceKey == actor.instanceKey;
                     });
    if (existing != mDeferredCompilationInstances.end()) {
        existing->Reason = std::move(reason);
        existing->RoomIndex = actor.transition ? -1 : actor.roomIndex;
        return;
    }
    mDeferredCompilationInstances.push_back({actor.instanceKey, actor.profileKey,
                                             std::move(reason), actor.actorId,
                                             actor.transition ? -1 : actor.roomIndex});
}

void NativeActorRuntime::ClearDeferredCompilationInstance(std::string_view instanceKey) {
    std::erase_if(mDeferredCompilationInstances, [&](const auto& deferred) {
        return deferred.InstanceKey == instanceKey;
    });
}

bool NativeActorRuntime::SpawnCompilationInstance(
    const Oot3d::RoomCompilationActorEntry& actor,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    if (!mConfig.CompilationUnit || !mRoomRuntime || actor.sourceKind == "scene_spawn") {
        return false;
    }
    const auto* profileSource = mConfig.CompilationUnit->FindActorProfile(actor.profileKey);
    if (profileSource == nullptr) {
        throw std::runtime_error("compiled actor instance has no native profile");
    }
    if (!CompilationObjectBankReady(profileSource->objectId)) {
        SetDeferredCompilationInstance(actor, "native_object_bank_not_prepared");
        return false;
    }

    const bool renderBound = !actor.transition &&
                             actor.roomIndex == mConfig.CompilationUnit->initialRoomIndex &&
                             HasRenderBinding(scene, actor.sourceIndex, actor.actorId);
    auto entry = SpawnEntryFromCompilationActor(actor, renderBound);
    if (actor.transition) {
        entry.ActorId = mRoomRuntime->TransitionSpawnActorId(actor);
        entry.Params = mRoomRuntime->TransitionSpawnParams(actor);
        entry.RoomIndex = mRoomRuntime->CurrentRoom();
        entry.RenderBound = false;
    }
    auto* spawned = Spawn(entry, ProfileFromCompilation(*profileSource));
    if (spawned == nullptr) {
        SetDeferredCompilationInstance(actor, "native_actor_spawn_rejected");
        return false;
    }
    mCompilationRuntimeIds[actor.instanceKey] = spawned->RuntimeId;
    ClearDeferredCompilationInstance(actor.instanceKey);
    if (actor.sourceKind == "room_actor_list") {
        ++mSpawnedRoomActorCount;
    }
    return true;
}

void NativeActorRuntime::ReconcileCompilationResidency(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    if (!mConfig.CompilationUnit || !mRoomRuntime) {
        return;
    }
    const auto& unit = *mConfig.CompilationUnit;
    for (auto iterator = mCompilationRuntimeIds.begin();
         iterator != mCompilationRuntimeIds.end();) {
        const auto source =
            std::find_if(unit.actorInstances.begin(), unit.actorInstances.end(),
                         [&](const auto& actor) { return actor.instanceKey == iterator->first; });
        if (source == unit.actorInstances.end()) {
            throw std::runtime_error("resident actor is absent from its room compilation unit");
        }
        auto* instance = FindByRuntimeId(iterator->second);
        if (instance == nullptr || instance->State == NativeActorLifecycleState::Deleted) {
            iterator = mCompilationRuntimeIds.erase(iterator);
            continue;
        }
        const bool desired =
            mRoomRuntime->ShouldRetainRoomActor(instance->Entry.RoomIndex);
        if (!desired) {
            if (instance->State != NativeActorLifecycleState::PendingNativeDestroy) {
                Kill(instance->RuntimeId);
            }
            SetDeferredCompilationInstance(
                *source, source->transition ? "native_transition_owner_not_resident"
                                            : "native_room_not_resident");
        } else if (instance->State == NativeActorLifecycleState::PendingNativeDestroy) {
            SetDeferredCompilationInstance(*source, "native_actor_destroy_pending");
        } else {
            ClearDeferredCompilationInstance(source->instanceKey);
        }
        ++iterator;
    }

    for (const auto& actor : unit.actorInstances) {
        if (actor.sourceKind == "scene_spawn" ||
            mCompilationRuntimeIds.contains(actor.instanceKey)) {
            continue;
        }
        bool desired = false;
        std::string deferredReason;
        if (actor.transition) {
            desired = mRoomRuntime->LoadState() == unit.roomLifecycle.idleState &&
                      mRoomRuntime->ShouldSpawnTransitionActor(actor);
            deferredReason = mRoomRuntime->LoadState() == unit.roomLifecycle.idleState
                                 ? "native_transition_not_adjacent"
                                 : "native_transition_room_load_pending";
        } else {
            desired = mRoomRuntime->IsRoomLoaded(actor.roomIndex);
            deferredReason = mRoomRuntime->IsRoomResident(actor.roomIndex)
                                 ? "native_room_load_pending"
                                 : "native_room_not_resident";
        }
        if (desired) {
            SpawnCompilationInstance(actor, scene);
        } else {
            SetDeferredCompilationInstance(actor, std::move(deferredReason));
        }
    }
}

bool NativeActorRuntime::RequestCompiledRoom(int32_t roomIndex) {
    if (!mRoomRuntime || mCurrentScene == nullptr) {
        return false;
    }
    const std::vector<int32_t> previousObjectBankRooms =
        mObjectBankRuntime ? mObjectBankRuntime->ResidentRooms()
                           : std::vector<int32_t>{};
    std::vector<int32_t> preparedObjectBankRooms = previousObjectBankRooms;
    preparedObjectBankRooms.push_back(roomIndex);
    const auto restoreObjectBanks = [&]() {
        if (!SetCompilationObjectBankResidentRooms(previousObjectBankRooms)) {
            throw std::runtime_error(
                "native object-bank residency rollback failed");
        }
    };
    if (!mRoomRenderRuntime) {
        if (!SetCompilationObjectBankResidentRooms(preparedObjectBankRooms)) {
            return false;
        }
        int32_t droppedPreviousRoom = -1;
        if (!mRoomRuntime->RequestRoom(roomIndex, &droppedPreviousRoom)) {
            restoreObjectBanks();
            return false;
        }
        if (droppedPreviousRoom >= 0) {
            mObjectBankCleanupRooms.insert(droppedPreviousRoom);
        }
        if (!SetCompilationObjectBankResidentRooms(
                CompilationObjectBankRoomsWithCleanup())) {
            throw std::runtime_error(
                "native room request produced invalid object-bank ownership");
        }
        ReconcileCompilationResidency(*mCurrentScene);
        return true;
    }
    if (mCurrentRenderScene == nullptr ||
        !mRoomRenderRuntime->PrepareRoomRequest(roomIndex,
                                                *mCurrentRenderScene)) {
        return false;
    }
    if (!SetCompilationObjectBankResidentRooms(preparedObjectBankRooms)) {
        mRoomRenderRuntime->DiscardPreparedRoom();
        return false;
    }
    int32_t droppedPreviousRoom = -1;
    if (!mRoomRuntime->RequestRoom(roomIndex, &droppedPreviousRoom)) {
        restoreObjectBanks();
        mRoomRenderRuntime->DiscardPreparedRoom();
        return false;
    }
    if (droppedPreviousRoom >= 0) {
        mObjectBankCleanupRooms.insert(droppedPreviousRoom);
    }
    if (!SetCompilationObjectBankResidentRooms(
            CompilationObjectBankRoomsWithCleanup())) {
        throw std::runtime_error(
            "native room request produced invalid object-bank ownership");
    }
    if (!mRoomRenderRuntime->CommitPreparedRoom(roomIndex, droppedPreviousRoom)) {
        throw std::runtime_error(
            "native room lifecycle accepted a mismatched prepared render resource");
    }
    mRoomRenderRuntime->ComposeResidentRooms(*mRoomRuntime, *mCurrentRenderScene);
    ReconcileCompilationResidency(*mCurrentScene);
    if (!mRoomRuntime->CompleteRoomRequest()) {
        throw std::runtime_error(
            "native room render resource was ready but lifecycle completion failed");
    }
    mRoomRenderRuntime->ComposeResidentRooms(*mRoomRuntime, *mCurrentRenderScene);
    ReconcileCompilationResidency(*mCurrentScene);
    return true;
}

bool NativeActorRuntime::CompleteCompiledRoomRequest() {
    if (!mRoomRuntime || mCurrentScene == nullptr ||
        !mRoomRuntime->CompleteRoomRequest()) {
        return false;
    }
    if (mRoomRenderRuntime && mCurrentRenderScene) {
        mRoomRenderRuntime->ComposeResidentRooms(*mRoomRuntime,
                                                 *mCurrentRenderScene);
    }
    if (!SetCompilationObjectBankResidentRooms(
            CompilationObjectBankRoomsWithCleanup())) {
        throw std::runtime_error(
            "native room completion produced invalid object-bank ownership");
    }
    ReconcileCompilationResidency(*mCurrentScene);
    return true;
}

bool NativeActorRuntime::PlayActorSound2(uint64_t runtimeId, uint32_t soundId) {
    if (!mNativeAudioService) {
        return false;
    }
    const auto* actor = FindByRuntimeId(runtimeId);
    if (actor == nullptr || actor->State == NativeActorLifecycleState::Deleted) {
        return false;
    }
    return mNativeAudioService->PlayActorSound2(
        runtimeId, soundId,
        {static_cast<float>(actor->WorldPosition.X),
         static_cast<float>(actor->WorldPosition.Y),
         static_cast<float>(actor->WorldPosition.Z)});
}

bool NativeActorRuntime::PlaySoundGeneral(const NativeAudioRequest& request) {
    return mNativeAudioService && mNativeAudioService->PlaySoundGeneral(request);
}

bool NativeActorRuntime::QueueAudioSequence(
    uint8_t playerIndex, uint32_t soundId, uint32_t fadeInFrames) {
    return mNativeAudioService && mNativeAudioService->QueueSequencePlayer(
        playerIndex, soundId, fadeInFrames);
}

bool NativeActorRuntime::QueueAudioStream(uint8_t playerIndex, uint32_t soundId,
                                          uint8_t volume, uint8_t pan) {
    return mNativeAudioService && mNativeAudioService->QueueStreamPlayer(
        playerIndex, soundId, volume, pan);
}

bool NativeActorRuntime::SetAudioSequenceVolume(
    uint8_t playerIndex, uint8_t layer, uint8_t volume,
    uint32_t fadeFrames) {
    return mNativeAudioService &&
           mNativeAudioService->SetSequencePlayerVolume(
               playerIndex, layer, volume, fadeFrames);
}

void NativeActorRuntime::StopAudioSequence(
    uint8_t playerIndex, uint32_t fadeOutFrames) {
    if (mNativeAudioService) {
        mNativeAudioService->StopSequencePlayer(playerIndex, fadeOutFrames);
    }
}

void NativeActorRuntime::StopAudioStream(
    uint8_t playerIndex, uint32_t fadeOutFrames) {
    if (mNativeAudioService) {
        mNativeAudioService->StopStreamPlayer(playerIndex, fadeOutFrames);
    }
}

void NativeActorRuntime::InitializeLegacyScenePopulation(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    const auto& codeBinPath = scene.AssetGraph.NativeActorProfileCodeBinPath;
    if (!scene.AssetGraph.NativeActorProfileCodeBinAvailable || codeBinPath.empty()) {
        throw std::runtime_error("native Actor runtime requires an RCU or the "
                                 "OOT3D code.bin profile table");
    }

    if (scene.PlayerStart.Valid && scene.PlayerStart.ActorId >= 0 &&
        scene.PlayerStart.ActorId <= UINT16_MAX) {
        NativeActorSpawnEntry playerEntry;
        playerEntry.EntryIndex = -1;
        playerEntry.ActorId = static_cast<int16_t>(scene.PlayerStart.ActorId);
        playerEntry.Position = scene.PlayerStart.Position;
        playerEntry.Rotation = scene.PlayerStart.Rotation;
        playerEntry.Params = static_cast<int16_t>(scene.PlayerStart.Params);
        playerEntry.ExternalOwner = true;
        try {
            const auto profile = ThreeDsRecomp::Oot3d::ParseNativeActorProfileFromCodeBinFile(
                codeBinPath, static_cast<uint16_t>(scene.PlayerStart.ActorId));
            if (profile.Valid) {
                Spawn(playerEntry, profile);
            } else {
                RecordProfileGap(playerEntry, "native_player_profile_invalid");
            }
        } catch (const std::exception& ex) {
            RecordProfileGap(playerEntry,
                             std::string("native_player_profile_decode_failed:") + ex.what());
        }
    }

    for (const auto& actor : scene.AssetGraph.RoomActors) {
        if (!actor.Plausible || actor.ActorId < 0 || actor.ActorId > UINT16_MAX) {
            continue;
        }
        const auto entry =
            SpawnEntryFromRoomActor(actor, HasRenderBinding(scene, actor.Index, actor.ActorId));
        try {
            const auto profile = ThreeDsRecomp::Oot3d::ParseNativeActorProfileFromCodeBinFile(
                codeBinPath, static_cast<uint16_t>(actor.ActorId));
            if (!profile.Valid) {
                RecordProfileGap(entry, "native_actor_profile_invalid");
                continue;
            }
            if (Spawn(entry, profile) != nullptr) {
                ++mSpawnedRoomActorCount;
            }
        } catch (const std::exception& ex) {
            RecordProfileGap(entry, std::string("native_actor_profile_decode_failed:") + ex.what());
        }
    }
}

void NativeActorRuntime::Update(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                                double deltaSeconds) {
    if (!mInitialized) {
        throw std::runtime_error("native Actor runtime was updated before initialization");
    }
    mCurrentScene = &scene;
    mCurrentRenderScene = &renderScene;
    mCurrentDeltaSeconds = std::max(0.0, deltaSeconds);
    ++mFrameCount;
    auto phaseStart = std::chrono::steady_clock::now();
    for (uint32_t category = 0; category < mCategoryLists.size(); ++category) {
        const std::vector<uint64_t> traversal(mCategoryLists[category].begin(),
                                              mCategoryLists[category].end());
        for (const uint64_t runtimeId : traversal) {
            auto* actor = FindByRuntimeId(runtimeId);
            if (actor == nullptr || actor->State == NativeActorLifecycleState::Deleted) {
                continue;
            }
            if (actor->State == NativeActorLifecycleState::PendingNativeInit) {
                TryInitialize(*actor);
                continue;
            }
            if (actor->State == NativeActorLifecycleState::PendingNativeDestroy) {
                TryDestroy(*actor);
                continue;
            }
            if (actor->State == NativeActorLifecycleState::ActiveExternalOwner) {
                continue;
            }
            // Actor_UpdateAll clears Actor+0x24 before the actor callback writes
            // the request consumed by Gameplay_Draw in the same native frame.
            actor->SfxRequest = 0;
            actor->PreviousPosition = actor->WorldPosition;
            if (actor->Profile.UpdateFunctionAddress == 0) {
                actor->State = NativeActorLifecycleState::PendingNativeDestroy;
                TryDestroy(*actor);
                continue;
            }
            if (DispatchCallback(*actor, actor->Profile.UpdateFunctionAddress, "update")) {
                ++actor->UpdateCount;
            }
        }
    }
    mActorCallbackSeconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
    if (mRoomRuntime) {
        const bool cleanupCompleted = mRoomRuntime->AdvanceResourceCleanup();
        if (cleanupCompleted) {
            if (mRoomRenderRuntime) {
                mRoomRenderRuntime->ReleaseCompletedCleanup();
            }
            mObjectBankCleanupRooms.clear();
            if (!SetCompilationObjectBankResidentRooms(
                    mRoomRuntime->ResidentRooms())) {
                throw std::runtime_error(
                    "native object-bank delayed cleanup failed");
            }
        }
        ReconcileCompilationResidency(scene);
    }
    mRoomLifecycleSeconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
    if (mRoomRenderRuntime && mCurrentRenderScene) {
        const uint32_t nativeRenderTickCount = mRoomRenderRuntime->AdvanceMaterialAnimations(
            deltaSeconds, mConfig.MaterialAnimationTicksPerSecond,
            *mCurrentRenderScene);
        if (mConfig.CompilationUnit && nativeRenderTickCount > 0) {
            for (uint32_t tick = 0; tick < nativeRenderTickCount; ++tick) {
                mRoomRenderRuntime->ApplySceneCallbacks(
                    mConfig.CompilationUnit->roomCallbacks, mConfig.GameplayFacts,
                    {}, mNativeRandomState, *mCurrentRenderScene);
            }
        }
    }
    mRoomRenderSeconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
    if (mNativeAudioService) {
        for (const auto& actor : mActors) {
            if (actor->State == NativeActorLifecycleState::Deleted) {
                continue;
            }
            const std::array<float, 3> position{
                static_cast<float>(actor->WorldPosition.X),
                static_cast<float>(actor->WorldPosition.Y),
                static_cast<float>(actor->WorldPosition.Z)};
            mNativeAudioService->UpdateOwnerPosition(actor->RuntimeId, position);
            if (actor->SfxRequest == 0) {
                continue;
            }
            mNativeAudioService->ConsumeActorSfxRequest(
                actor->RuntimeId, actor->SfxRequest, position);
        }
        for (auto& [runtimeId, state] : mEnRiverSoundStates) {
            if (!state.DrawInitialized) {
                state.DrawInitialized = true;
                state.Status = "native_draw_audio_initialized";
                continue;
            }
            auto* actor = FindByRuntimeId(runtimeId);
            if (actor == nullptr) {
                continue;
            }
            NativeAudioRequest request;
            request.SoundId = state.NativeSoundId;
            request.OwnerId = runtimeId;
            request.PositionIdentity = runtimeId;
            request.HasPosition = true;
            request.Position = {static_cast<float>(actor->WorldPosition.X),
                                static_cast<float>(actor->WorldPosition.Y),
                                static_cast<float>(actor->WorldPosition.Z)};
            mNativeAudioService->PlaySoundGeneral(request);
            state.Status = "native_draw_audio_sequence_active";
        }
    }
    mAudioStateSeconds += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
}

bool NativeActorRuntime::MixAudio(uint32_t sampleRate, size_t frameCount,
                                  std::vector<int16_t>& stereoSamples) {
    if (!mNativeAudioService) {
        stereoSamples.clear();
        return false;
    }
    if (sampleRate != mNativeAudioOutputSampleRate) {
        mNativeAudioOutputSampleRate = sampleRate;
        mNativeAudioService->SetOutputSampleRate(sampleRate);
    }
    stereoSamples = mNativeAudioService->MixStereo(frameCount);
    return true;
}

void NativeActorRuntime::InitializeNativeRenderServices() {
    mObjectBankRuntime.reset();
    mObjectBankCleanupRooms.clear();
    mRoomRenderRuntime.reset();
    mRenderProvider.reset();
    mSources.reset();
    mNativeAbiCatalog.reset();
    mRigidActorDefinitions.clear();
    if (!mConfig.Assets) {
        return;
    }
    if (!mConfig.NativeSourceLoaderFactory) {
        throw std::runtime_error(
            "native Actor render services require a source loader factory");
    }
    mSources = std::make_unique<ThreeDsRecomp::Oot3d::NativeSourceProvider>(
        mConfig.NativeSourceLoaderFactory());
    const auto nativeAbiSource = mSources->Load(Oot3d::kNativeAbiCatalogResource);
    if (nativeAbiSource == nullptr) {
        throw std::runtime_error("native core archive has no reviewed ABI catalog");
    }
    mNativeAbiCatalog = Oot3d::ParseNativeAbiCatalog(std::string_view(
        reinterpret_cast<const char*>(nativeAbiSource->Bytes->data()),
        nativeAbiSource->Bytes->size()));
    if (mConfig.CompilationUnit &&
        (!mConfig.CompilationUnit->nativeAbiCatalog.available ||
         mNativeAbiCatalog->sourceSnapshotId !=
             mConfig.CompilationUnit->nativeAbiCatalog.sourceSnapshotId ||
         mNativeAbiCatalog->sourceBaseRevision !=
             mConfig.CompilationUnit->nativeAbiCatalog.sourceBaseRevision ||
         mNativeAbiCatalog->codeBinSha256 !=
             mConfig.CompilationUnit->sourceCodeBinSha256 ||
         mNativeAbiCatalog->payloadSha256 !=
             mConfig.CompilationUnit->nativeAbiCatalog.payloadSha256 ||
         mNativeAbiCatalog->functions.size() !=
             mConfig.CompilationUnit->nativeAbiCatalog.functionCount)) {
        throw std::runtime_error(
            "native ABI catalog and Room Compilation Unit provenance differ");
    }
    if (mConfig.CompilationUnit) {
        mRoomRenderRuntime = std::make_unique<NativeRoomRenderRuntime>(
            *mConfig.Assets, *mSources, mConfig.CompilationUnit->sceneId,
            mConfig.CompilationUnit->setupIndex, mNativeA32Execution.get());
    }
    const auto contractSource = mSources->Load(kEnKoRuntimeContractResource);
    if (contractSource == nullptr) {
        throw std::runtime_error("native Actor shard has no EnKo runtime contract");
    }
    mEnKoRuntimeContract =
        nlohmann::json::parse(contractSource->Bytes->begin(), contractSource->Bytes->end());
    if (mEnKoRuntimeContract.value("format", "") != "oot3d_enko_native_runtime_contract_v1" ||
        mEnKoRuntimeContract.value("status", "") != "complete") {
        throw std::runtime_error("native EnKo runtime contract is unsupported");
    }
    if (mConfig.CompilationUnit &&
        !NativeActorContractMatchesCodeBin(
            mEnKoRuntimeContract,
            mConfig.CompilationUnit->sourceCodeBinSha256)) {
        throw std::runtime_error(
            "native EnKo runtime contract and Room Compilation Unit code.bin differ");
    }
    if (mConfig.CompilationUnit) {
        const auto enkoProfile = std::find_if(
            mConfig.CompilationUnit->actorProfiles.begin(),
            mConfig.CompilationUnit->actorProfiles.end(),
            [](const auto& profile) { return profile.actorName == "ACTOR_EN_KO"; });
        if (enkoProfile != mConfig.CompilationUnit->actorProfiles.end() &&
            mConfig.CompilationUnit->nativeAbiCatalog.available) {
            const auto& drawCallback = mEnKoRuntimeContract.at("draw_callback");
            if (!drawCallback.is_object() || !drawCallback.contains("native_abi") ||
                !drawCallback.at("native_abi").is_object()) {
                throw std::runtime_error(
                    "native EnKo contract lacks reviewed OverrideLimbDraw ABI");
            }
            const auto& abi = drawCallback.at("native_abi");
            uint32_t callbackAddress = 0;
            uint32_t torsoRotationOffset = 0;
            uint32_t headRotationOffset = 0;
            if (abi.value("name", "") != "EnKo_OverrideLimbDraw" ||
                abi.value("closure_kind", "") != "maintained_abi" ||
                !abi.contains("address") ||
                !ReadJsonU32(abi.at("address"), callbackAddress) ||
                !drawCallback.contains("torso_rotation_u16x3_offset") ||
                !ReadJsonU32(drawCallback.at("torso_rotation_u16x3_offset"),
                             torsoRotationOffset) ||
                !drawCallback.contains("head_rotation_u16x3_offset") ||
                !ReadJsonU32(drawCallback.at("head_rotation_u16x3_offset"),
                             headRotationOffset) ||
                torsoRotationOffset + 6 > enkoProfile->instanceSize ||
                headRotationOffset + 6 > enkoProfile->instanceSize) {
                throw std::runtime_error(
                    "native EnKo OverrideLimbDraw contract is inconsistent");
            }
            const auto* function =
                enkoProfile->behaviorGraph.FindFunction("EnKo_OverrideLimbDraw");
            const auto* catalogFunction =
                mNativeAbiCatalog->FindByName("EnKo_OverrideLimbDraw");
            if (function == nullptr || function->runtimeAddress != callbackAddress ||
                function->closureKind != "maintained_abi" ||
                function->ownerResolution !=
                    "exact_maintained_actor_symbol_prefix_and_native_context_abi" ||
                catalogFunction == nullptr ||
                catalogFunction->runtimeAddress != callbackAddress ||
                catalogFunction->sourceTranche != function->sourceTranche) {
                throw std::runtime_error(
                    "EnKo contract and Room Compilation Unit ABI binding differ");
            }
        }
    }
    mRenderProvider =
        std::make_unique<ThreeDsRecomp::Oot3d::NativeActorRenderProvider>(*mConfig.Assets, *mSources);
    mEnKoQuestState = ResolveNativeActorSemanticState(
        mEnKoRuntimeContract.value("quest_state_semantics", nlohmann::json::array()),
        mConfig.GameplayFacts);
    mEnKoBlinkProfile = ResolveNativeActorBlinkProfile(mEnKoRuntimeContract);
    if (!mEnKoBlinkProfile.Available) {
        throw std::runtime_error("native EnKo blink contract is unsupported: " +
                                 mEnKoBlinkProfile.Status);
    }
    if (!mNativeA32Execution || !mNativeA32Execution->Available()) {
        throw std::runtime_error(
            "native actor blink requires the A32 code.bin execution runtime");
    }
    mEnKoTrackingContract = ResolveNativeNpcTrackingContract(mEnKoRuntimeContract);
    if (!mEnKoTrackingContract.Available) {
        throw std::runtime_error("native EnKo tracking contract is unsupported: " +
                                 mEnKoTrackingContract.Status + ":" +
                                 mEnKoTrackingContract.Error);
    }
    mEnKoLimbCallbackContract =
        ResolveNativeEnKoLimbCallbackContract(mEnKoRuntimeContract);
    if (!mEnKoLimbCallbackContract.Available) {
        throw std::runtime_error(
            "native EnKo limb callback contract is unsupported: " +
            mEnKoLimbCallbackContract.Status + ":" +
            mEnKoLimbCallbackContract.Error);
    }
    if (mConfig.CompilationUnit) {
        for (const auto& operation :
             mConfig.CompilationUnit->roomCallbacks.materialTevAlphaOperations) {
            if (operation.random.initialState !=
                    mEnKoBlinkProfile.RandomInitialState ||
                operation.random.stateAddress !=
                    mEnKoBlinkProfile.RandomStateAddress ||
                operation.random.multiplier !=
                    mEnKoBlinkProfile.RandomMultiplier ||
                operation.random.increment !=
                    mEnKoBlinkProfile.RandomIncrement) {
                throw std::runtime_error(
                    "native room callback and actor RNG contracts differ");
            }
        }
    }
    mNativeRandomState = mEnKoBlinkProfile.RandomInitialState;
    mNativeRandomStatus = "native_a32_rand_zero_one_ready";
    RegisterEnKoCallbacks();
    mNamedActorDefinitions.clear();
    LoadNamedActorContract(kEnSaRuntimeContractResource, "oot3d_ensa_native_runtime_contract_v1",
                           "EnSa");
    LoadNamedActorContract(kEnMdRuntimeContractResource, "oot3d_enmd_native_runtime_contract_v1",
                           "EnMd");
    LoadEnKusaContract();
    LoadObjHanaContract();
    LoadRigidActorContracts();
    LoadEnHollContract();
}

void NativeActorRuntime::RegisterEnRiverSoundCallbacks() {
    mEnRiverSoundBindingStatus = "native_closure_or_rcu_unavailable";
    if (!mConfig.CompilationUnit || !mNativeClosureCatalog) {
        return;
    }
    const auto profile = std::find_if(
        mConfig.CompilationUnit->actorProfiles.begin(),
        mConfig.CompilationUnit->actorProfiles.end(),
        [](const auto& candidate) {
            return candidate.actorName == "ACTOR_EN_RIVER_SOUND";
        });
    if (profile == mConfig.CompilationUnit->actorProfiles.end()) {
        mEnRiverSoundBindingStatus = "actor_absent_from_rcu";
        return;
    }
    const auto* init = profile->behaviorGraph.FindFunction("EnRiverSound_Init");
    const auto* update = profile->behaviorGraph.FindFunction("EnRiverSound_Update");
    const auto* destroy = profile->behaviorGraph.FindFunction("EnRiverSound_Destroy");
    const auto closureMatches = [this](const auto* function) {
        if (function == nullptr) {
            return false;
        }
        const auto* closure = mNativeClosureCatalog->Find(function->runtimeAddress);
        return closure != nullptr && closure->Name == function->name &&
               closure->BodyStatus == "extracted";
    };
    if (!closureMatches(init) || !closureMatches(update) ||
        !closureMatches(destroy)) {
        mEnRiverSoundBindingStatus = "native_lifecycle_body_mismatch";
        return;
    }

    size_t supportedInstances = 0;
    for (const auto& instance : mConfig.CompilationUnit->actorInstances) {
        if (instance.actorId != profile->actorId) {
            continue;
        }
        const auto soundId = static_cast<uint8_t>(instance.params & 0xff);
        const bool initNeedsUnboundService =
            soundId >= 0xf7 || soundId == 0x0c;
        const bool updateNeedsUnboundService =
            soundId == 0 || soundId == 4 || soundId == 5 ||
            soundId == 0x0d || soundId == 0x13 ||
            mConfig.CompilationUnit->sceneId == 0x12;
        if (initNeedsUnboundService || updateNeedsUnboundService) {
            mEnRiverSoundBindingStatus =
                "rcu_instance_requires_unbound_native_audio_or_path_service";
            return;
        }
        ++supportedInstances;
    }
    if (supportedInstances == 0) {
        mEnRiverSoundBindingStatus = "actor_absent_from_selected_setup";
        return;
    }

    RegisterCallback(init->runtimeAddress, [this](NativeActorInstance& actor) {
        InitializeEnRiverSound(actor);
    });
    RegisterCallback(update->runtimeAddress, [this](NativeActorInstance& actor) {
        UpdateEnRiverSound(actor);
    });
    RegisterCallback(destroy->runtimeAddress, [this](NativeActorInstance& actor) {
        DestroyEnRiverSound(actor);
    });
    mEnRiverSoundBindingStatus = mNativeAudioService
        ? "native_params_branch_lifecycle_and_draw_audio_bound"
        : "native_params_branch_lifecycle_bound_audio_service_unavailable";
}

void NativeActorRuntime::InitializeEnRiverSound(NativeActorInstance& actor) {
    EnRiverSoundRuntimeState state;
    const auto params = static_cast<uint16_t>(actor.Entry.Params);
    state.PathIndex = static_cast<uint8_t>(params >> 8);
    state.SoundId = static_cast<uint8_t>(params & 0xff);
    if (mNativeAudioService) {
        state.NativeSoundId = ResolveEnRiverSoundNativeId(state.SoundId);
        state.Status = "native_init_branch_complete_with_audio_binding";
    } else {
        state.Status = "native_init_branch_complete_audio_service_unavailable";
    }
    mEnRiverSoundStates[actor.RuntimeId] = std::move(state);
}

void NativeActorRuntime::UpdateEnRiverSound(NativeActorInstance& actor) {
    const auto found = mEnRiverSoundStates.find(actor.RuntimeId);
    if (found == mEnRiverSoundStates.end()) {
        throw std::runtime_error("native EnRiverSound update has no initialized state");
    }
    ++found->second.UpdateCount;
    found->second.Status = "native_non_water_path_update_return";
}

void NativeActorRuntime::DestroyEnRiverSound(NativeActorInstance& actor) {
    if (mNativeAudioService) {
        mNativeAudioService->StopOwner(actor.RuntimeId);
    }
    mEnRiverSoundStates.erase(actor.RuntimeId);
}

uint32_t NativeActorRuntime::ResolveEnRiverSoundNativeId(uint8_t selector) const {
    if (mNativeCodeBin.empty()) {
        throw std::runtime_error("native EnRiverSound sound-ID table is unavailable");
    }
    return ResolveNativeCodeU32TableEntry(
        mNativeCodeBin, mConfig.NativeCodeBaseAddress,
        mConfig.EnRiverSoundIdTableAddress, selector);
}

void NativeActorRuntime::LoadEnHollContract() {
    const auto source = mSources->Load(kEnHollRuntimeContractResource);
    if (source == nullptr) {
        throw std::runtime_error("native Actor shard has no EnHoll runtime contract");
    }
    const auto document =
        nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
    mEnHollContract = ResolveNativeEnHollTriggerContract(document);
    if (!mEnHollContract.Available) {
        throw std::runtime_error("native EnHoll runtime contract is unsupported: " +
                                 mEnHollContract.Status);
    }
    RegisterEnHollCallbacks();
}

void NativeActorRuntime::RegisterEnHollCallbacks() {
    RegisterCallback(mEnHollContract.InitAddress,
                     [this](NativeActorInstance& actor) { InitializeEnHoll(actor); });
    RegisterCallback(mEnHollContract.UpdateAddress,
                     [this](NativeActorInstance& actor) { UpdateEnHoll(actor); });
    RegisterCallback(mEnHollContract.DestroyAddress,
                     [this](NativeActorInstance& actor) { DestroyEnHoll(actor); });
}

void NativeActorRuntime::InitializeEnHoll(NativeActorInstance& actor) {
    const auto matchesProfile =
        actor.Profile.ActorId == mEnHollContract.ActorId &&
        actor.Profile.Category == mEnHollContract.Category &&
        actor.Profile.InstanceSize == mEnHollContract.InstanceSize &&
        actor.Profile.InitFunctionAddress == mEnHollContract.InitAddress &&
        actor.Profile.DestroyFunctionAddress == mEnHollContract.DestroyAddress &&
        actor.Profile.UpdateFunctionAddress == mEnHollContract.UpdateAddress &&
        actor.Profile.DrawFunctionAddress == mEnHollContract.DrawAddress;
    if (!matchesProfile || !mConfig.CompilationUnit || !mRoomRuntime) {
        throw std::runtime_error(
            "native EnHoll spawn does not match its RCU/code.bin contract");
    }
    const auto source = std::find_if(
        mConfig.CompilationUnit->actorInstances.begin(),
        mConfig.CompilationUnit->actorInstances.end(),
        [&](const auto& candidate) {
            return candidate.instanceKey == actor.Entry.InstanceKey;
        });
    if (source == mConfig.CompilationUnit->actorInstances.end() || !source->transition ||
        source->sourceIndex < 0) {
        throw std::runtime_error("native EnHoll has no transition entry in its RCU");
    }

    EnHollRuntimeState state;
    state.ActionMode = ResolveNativeEnHollActionMode(mEnHollContract, actor.Entry.Params);
    state.TransitionIndex = static_cast<uint16_t>(actor.Entry.Params) >>
                            mEnHollContract.TransitionIndexShift;
    state.FrontRoom = source->frontRoomIndex;
    state.BackRoom = source->backRoomIndex;
    state.Status = std::find(mEnHollContract.SupportedModes.begin(),
                             mEnHollContract.SupportedModes.end(), state.ActionMode) !=
                           mEnHollContract.SupportedModes.end()
                       ? "native_enholl_room_request_mode_active"
                       : "native_enholl_action_mode_not_implemented";
    if (state.TransitionIndex != static_cast<uint16_t>(source->sourceIndex)) {
        throw std::runtime_error("native EnHoll transition index does not match its RCU entry");
    }
    mEnHollStates[actor.RuntimeId] = std::move(state);
}

void NativeActorRuntime::UpdateEnHoll(NativeActorInstance& actor) {
    const auto found = mEnHollStates.find(actor.RuntimeId);
    if (found == mEnHollStates.end() || !mRoomRuntime) {
        return;
    }
    auto& state = found->second;
    if (state.WaitingForRoom) {
        if (mRoomRuntime->LoadState() ==
            mConfig.CompilationUnit->roomLifecycle.idleState) {
            state.WaitingForRoom = false;
            state.Status = "native_enholl_room_request_committed";
        }
        return;
    }

    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 focus;
    if (mFrameContext.UseViewEyeForTransitionActors && mFrameContext.ViewEyeValid) {
        focus = {mFrameContext.ViewEyeX, mFrameContext.ViewEyeY,
                 mFrameContext.ViewEyeZ};
    } else if (mFrameContext.PlayerActorPositionValid) {
        focus = {mFrameContext.PlayerActorX, mFrameContext.PlayerActorY,
                 mFrameContext.PlayerActorZ};
    } else {
        state.Status = "native_enholl_focus_position_unavailable";
        return;
    }

    NativeEnHollTriggerInput input;
    input.ActorPosition = actor.WorldPosition;
    input.ActorYaw = static_cast<int16_t>(actor.ShapeRotation.Y);
    input.FocusPosition = focus;
    input.FrontRoom = state.FrontRoom;
    input.BackRoom = state.BackRoom;
    input.CurrentRoom = mRoomRuntime->CurrentRoom();
    input.Mode = state.ActionMode;
    input.SpecialBypass = mFrameContext.NativeSpecialTransitionBypass;
    state.LastEvaluation = EvaluateNativeEnHollRoomRequestTrigger(mEnHollContract, input);
    state.Status = state.LastEvaluation.Status;
    if (state.LastEvaluation.RoomRequestRequired &&
        RequestCompiledRoom(state.LastEvaluation.TargetRoom)) {
        state.WaitingForRoom = true;
        ++state.RequestCount;
        state.Status = "native_enholl_room_request_started";
    }
}

void NativeActorRuntime::DestroyEnHoll(NativeActorInstance& actor) {
    mEnHollStates.erase(actor.RuntimeId);
}

void NativeActorRuntime::RegisterEnKoCallbacks() {
    if (!mEnKoQuestState.has_value() || !mEnKoQuestState->Available ||
        !mEnKoRuntimeContract.contains("actor_profile") ||
        !mEnKoRuntimeContract.at("actor_profile").is_object()) {
        return;
    }
    const auto& profile = mEnKoRuntimeContract.at("actor_profile");
    const auto registerRole = [&](const char* field, Callback callback) {
        uint32_t address = 0;
        if (!profile.contains(field) || !ReadJsonU32(profile.at(field), address) || address == 0) {
            throw std::runtime_error(std::string("native EnKo profile has invalid ") + field);
        }
        RegisterCallback(address, std::move(callback));
    };
    registerRole("init_address", [this](NativeActorInstance& actor) { InitializeEnKo(actor); });
    registerRole("update_address", [this](NativeActorInstance& actor) { UpdateEnKo(actor); });
    registerRole("destroy_address", [this](NativeActorInstance& actor) { DestroyEnKo(actor); });
    registerRole("draw_address", [](NativeActorInstance&) {});
}

void NativeActorRuntime::LoadNamedActorContract(std::string_view resource, std::string_view format,
                                                std::string_view label) {
    const auto source = mSources->Load(resource);
    if (source == nullptr) {
        throw std::runtime_error("native Actor shard has no " + std::string(label) +
                                 " runtime contract");
    }
    NamedActorDefinition definition;
    definition.Label = label;
    definition.Contract = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
    if (definition.Contract.value("format", "") != format ||
        definition.Contract.value("status", "") != "initial_route_complete" ||
        !definition.Contract.contains("actor_profile") ||
        !definition.Contract.at("actor_profile").is_object()) {
        throw std::runtime_error("native " + std::string(label) +
                                 " runtime contract is unsupported");
    }
    if (mConfig.CompilationUnit &&
        !NativeActorContractMatchesCodeBin(
            definition.Contract,
            mConfig.CompilationUnit->sourceCodeBinSha256)) {
        throw std::runtime_error("native " + std::string(label) +
                                 " runtime contract and Room Compilation Unit code.bin differ");
    }
    uint32_t actorId = 0;
    const auto& profile = definition.Contract.at("actor_profile");
    if (!profile.contains("actor_id") || !ReadJsonU32(profile.at("actor_id"), actorId) ||
        actorId > UINT16_MAX) {
        throw std::runtime_error("native " + std::string(label) + " actor identity is invalid");
    }
    definition.SpawnState = ResolveNativeActorSemanticState(
        definition.Contract.value("spawn_state_semantics", nlohmann::json::array()),
        mConfig.GameplayFacts);
    definition.BlinkProfile = ResolveNativeActorBlinkProfile(definition.Contract);
    if (!definition.BlinkProfile.Available ||
        !NativeRandomProfilesMatch(mEnKoBlinkProfile, definition.BlinkProfile)) {
        throw std::runtime_error("native " + std::string(label) +
                                 " face runtime does not share the decoded OOT3D RNG contract");
    }
    if (!mNamedActorDefinitions.emplace(static_cast<uint16_t>(actorId), std::move(definition))
             .second) {
        throw std::runtime_error("duplicate native named-actor contract identity");
    }
    RegisterNamedActorCallbacks(static_cast<uint16_t>(actorId));
}

void NativeActorRuntime::RegisterNamedActorCallbacks(uint16_t actorId) {
    const auto definition = mNamedActorDefinitions.find(actorId);
    if (definition == mNamedActorDefinitions.end() || !definition->second.SpawnState.Available) {
        return;
    }
    const auto& profile = definition->second.Contract.at("actor_profile");
    const auto registerRole = [&](const char* field, Callback callback) {
        uint32_t address = 0;
        if (!profile.contains(field) || !ReadJsonU32(profile.at(field), address) || address == 0) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " profile has invalid " + field);
        }
        RegisterCallback(address, std::move(callback));
    };
    registerRole("init_address",
                 [this](NativeActorInstance& actor) { InitializeNamedActor(actor); });
    registerRole("update_address", [this](NativeActorInstance& actor) { UpdateNamedActor(actor); });
    registerRole("destroy_address",
                 [this](NativeActorInstance& actor) { DestroyNamedActor(actor); });
    registerRole("draw_address", [](NativeActorInstance&) {});
}

void NativeActorRuntime::LoadEnKusaContract() {
    const auto source = mSources->Load(kEnKusaRuntimeContractResource);
    if (source == nullptr) {
        throw std::runtime_error("native Actor shard has no EnKusa runtime contract");
    }
    mEnKusaRuntimeContract = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
    if (mEnKusaRuntimeContract.value("format", "") != "oot3d_enkusa_native_runtime_contract_v1" ||
        mEnKusaRuntimeContract.value("status", "") != "initial_visual_complete" ||
        !mEnKusaRuntimeContract.contains("actor_profile") ||
        !mEnKusaRuntimeContract.at("actor_profile").is_object() ||
        !mEnKusaRuntimeContract.contains("visual_states") ||
        !mEnKusaRuntimeContract.at("visual_states").is_array()) {
        throw std::runtime_error("native EnKusa runtime contract is unsupported");
    }
    RegisterEnKusaCallbacks();
}

void NativeActorRuntime::RegisterEnKusaCallbacks() {
    const auto& profile = mEnKusaRuntimeContract.at("actor_profile");
    const auto registerRole = [&](const char* field, Callback callback) {
        uint32_t address = 0;
        if (!profile.contains(field) || !ReadJsonU32(profile.at(field), address) || address == 0) {
            throw std::runtime_error(std::string("native EnKusa profile has invalid ") + field);
        }
        RegisterCallback(address, std::move(callback));
    };
    registerRole("init_address", [this](NativeActorInstance& actor) { InitializeEnKusa(actor); });
    registerRole("update_address", [this](NativeActorInstance& actor) { UpdateEnKusa(actor); });
    registerRole("destroy_address", [this](NativeActorInstance& actor) { DestroyEnKusa(actor); });
}

void NativeActorRuntime::LoadObjHanaContract() {
    const auto source = mSources->Load(kObjHanaRuntimeContractResource);
    if (source == nullptr) {
        throw std::runtime_error("native Actor shard has no ObjHana runtime contract");
    }
    mObjHanaRuntimeContract = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
    if (mObjHanaRuntimeContract.value("format", "") != "oot3d_objhana_native_runtime_contract_v1" ||
        mObjHanaRuntimeContract.value("status", "") != "initial_visual_complete" ||
        !mObjHanaRuntimeContract.contains("actor_profile") ||
        !mObjHanaRuntimeContract.at("actor_profile").is_object() ||
        !mObjHanaRuntimeContract.contains("visual_states") ||
        !mObjHanaRuntimeContract.at("visual_states").is_array()) {
        throw std::runtime_error("native ObjHana runtime contract is unsupported");
    }
    RegisterObjHanaCallbacks();
}

void NativeActorRuntime::RegisterObjHanaCallbacks() {
    const auto& profile = mObjHanaRuntimeContract.at("actor_profile");
    const auto registerRole = [&](const char* field, Callback callback) {
        uint32_t address = 0;
        if (!profile.contains(field) || !ReadJsonU32(profile.at(field), address) || address == 0) {
            throw std::runtime_error(std::string("native ObjHana profile has invalid ") + field);
        }
        RegisterCallback(address, std::move(callback));
    };
    registerRole("init_address", [this](NativeActorInstance& actor) { InitializeObjHana(actor); });
    registerRole("update_address", [this](NativeActorInstance& actor) { UpdateObjHana(actor); });
    registerRole("destroy_address",
                 [this](NativeActorInstance& actor) { DestroyEnvironmentActor(actor); });
    registerRole("draw_address", [](NativeActorInstance&) {});
}

void NativeActorRuntime::LoadRigidActorContracts() {
    const std::array contractResources = {
        std::pair{kRigidActorRuntimeContractResource,
                  std::string_view{"oot3d_rigid_actor_native_runtime_contract_v1"}},
        std::pair{kItemActorRuntimeContractResource,
                  std::string_view{"oot3d_item_actor_native_runtime_contract_v1"}},
    };
    for (const auto& [resource, expectedFormat] : contractResources) {
        const auto source = mSources->Load(resource);
        if (source == nullptr) {
            throw std::runtime_error("native Actor shard has no runtime contract " +
                                     std::string(resource));
        }
        const auto document =
            nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
        if (document.value("format", "") != expectedFormat ||
            document.value("status", "") != "initial_visual_complete" ||
            !document.contains("actors") || !document.at("actors").is_array()) {
            throw std::runtime_error("native rigid-actor runtime contract is unsupported");
        }

        for (const auto& contract : document.at("actors")) {
            if (!contract.is_object() ||
                contract.value("family", "") != "rigid_single_model" ||
                contract.value("status", "") != "initial_visual_complete" ||
                !contract.contains("actor_profile") ||
                !contract.at("actor_profile").is_object() ||
                !contract.contains("selector_contract") ||
                !contract.at("selector_contract").is_object() ||
                !contract.contains("visual_states") ||
                !contract.at("visual_states").is_array()) {
                throw std::runtime_error("native rigid-actor definition is unsupported");
            }
            const auto& profile = contract.at("actor_profile");
            uint32_t actorId = 0;
            if (!profile.contains("actor_id") ||
                !ReadJsonU32(profile.at("actor_id"), actorId) || actorId > UINT16_MAX) {
                throw std::runtime_error("native rigid-actor identity is invalid");
            }
            RigidActorDefinition definition;
            definition.Label = contract.value(
                "actor_name", "ACTOR_0x" + std::to_string(actorId));
            definition.Contract = contract;
            if (!mRigidActorDefinitions
                     .emplace(static_cast<uint16_t>(actorId), std::move(definition))
                     .second) {
                throw std::runtime_error("duplicate native rigid-actor contract identity");
            }
        }
    }
    for (const auto& [actorId, definition] : mRigidActorDefinitions) {
        (void)definition;
        RegisterRigidActorCallbacks(actorId);
    }
}

void NativeActorRuntime::RegisterRigidActorCallbacks(uint16_t actorId) {
    const auto definition = mRigidActorDefinitions.find(actorId);
    if (definition == mRigidActorDefinitions.end()) {
        throw std::runtime_error("native rigid-actor definition is unavailable");
    }
    const auto& profile = definition->second.Contract.at("actor_profile");
    const auto registerRole = [&](const char* field, Callback callback) {
        uint32_t address = 0;
        if (!profile.contains(field) || !ReadJsonU32(profile.at(field), address) ||
            address == 0) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " profile has invalid " + field);
        }
        RegisterCallback(address, std::move(callback));
    };
    registerRole("init_address",
                 [this](NativeActorInstance& actor) { InitializeRigidActor(actor); });
    registerRole("update_address",
                 [this](NativeActorInstance& actor) { UpdateRigidActor(actor); });
    registerRole("destroy_address",
                 [this](NativeActorInstance& actor) { DestroyEnvironmentActor(actor); });
    registerRole("draw_address", [](NativeActorInstance&) {});
}

void NativeActorRuntime::InitializeRigidActor(NativeActorInstance& actor) {
    const auto definition =
        mRigidActorDefinitions.find(static_cast<uint16_t>(actor.Entry.ActorId));
    if (definition == mRigidActorDefinitions.end()) {
        throw std::runtime_error("native rigid-actor spawn has no definition");
    }
    const auto& contract = definition->second.Contract;
    const auto& profile = contract.at("actor_profile");
    const auto profileMatches = [&](const char* field, uint32_t value) {
        uint32_t expected = 0;
        return profile.contains(field) && ReadJsonU32(profile.at(field), expected) &&
               expected == value;
    };
    if (!profileMatches("actor_id", static_cast<uint16_t>(actor.Entry.ActorId)) ||
        !profileMatches("category", actor.Profile.Category) ||
        !profileMatches("flags", actor.Profile.Flags) ||
        !profileMatches("object_id", actor.Profile.ObjectId) ||
        !profileMatches("instance_size", actor.Profile.InstanceSize) ||
        !profileMatches("init_address", actor.Profile.InitFunctionAddress) ||
        !profileMatches("destroy_address", actor.Profile.DestroyFunctionAddress) ||
        !profileMatches("update_address", actor.Profile.UpdateFunctionAddress) ||
        !profileMatches("draw_address", actor.Profile.DrawFunctionAddress)) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " spawn profile does not match its code.bin contract");
    }

    const auto& selectorContract = contract.at("selector_contract");
    uint32_t selector = 0;
    const auto selectorSource = selectorContract.value("source", "");
    if (selectorSource == "actor.params") {
        uint32_t selectorMask = 0;
        if (!selectorContract.contains("mask") ||
            !ReadJsonU32(selectorContract.at("mask"), selectorMask)) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " selector contract is invalid");
        }
        selector = static_cast<uint16_t>(actor.Entry.Params) & selectorMask;
    } else if (selectorSource == "constant") {
        if (!selectorContract.contains("value") ||
            !ReadJsonU32(selectorContract.at("value"), selector)) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " constant selector is invalid");
        }
    } else {
        throw std::runtime_error("native " + definition->second.Label +
                                 " selector contract is invalid");
    }
    const auto& states = contract.at("visual_states");
    const auto stateIt =
        std::find_if(states.begin(), states.end(), [selector](const auto& value) {
            uint32_t candidate = 0;
            return value.is_object() && value.contains("selector") &&
                   ReadJsonU32(value.at("selector"), candidate) &&
                   candidate == selector;
        });
    if (stateIt == states.end()) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " params select no verified visual state");
    }
    if (!stateIt->contains("model_asset_id") ||
        !stateIt->at("model_asset_id").is_string()) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " visual state has no selected model");
    }
    double modelScale = 0.0;
    if (!stateIt->contains("model_scale") ||
        !ReadJsonFiniteDouble(stateIt->at("model_scale"), modelScale) ||
        modelScale <= 0.0) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " visual state has invalid scale");
    }
    double shapeYOffset = 0.0;
    if (stateIt->contains("shape_y_offset") &&
        !ReadJsonFiniteDouble(stateIt->at("shape_y_offset"), shapeYOffset)) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " visual state has invalid shape y-offset");
    }
    double shapeYawStep = 0.0;
    if (stateIt->contains("shape_yaw_step_per_native_tick") &&
        !ReadJsonFiniteDouble(stateIt->at("shape_yaw_step_per_native_tick"),
                              shapeYawStep)) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " visual state has invalid yaw step");
    }
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 modelLocalTranslation;
    if (stateIt->contains("model_local_translation")) {
        const auto& translation = stateIt->at("model_local_translation");
        if (!translation.is_array() || translation.size() != 3 ||
            !ReadJsonFiniteDouble(translation.at(0), modelLocalTranslation.X) ||
            !ReadJsonFiniteDouble(translation.at(1), modelLocalTranslation.Y) ||
            !ReadJsonFiniteDouble(translation.at(2), modelLocalTranslation.Z)) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " visual state has invalid local translation");
        }
    }
    std::vector<uint32_t> visibleMeshIndices;
    if (stateIt->contains("visible_mesh_indices")) {
        if (!stateIt->at("visible_mesh_indices").is_array()) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " visual state has invalid mesh visibility");
        }
        for (const auto& meshIndexValue : stateIt->at("visible_mesh_indices")) {
            uint32_t meshIndex = 0;
            if (!ReadJsonU32(meshIndexValue, meshIndex)) {
                throw std::runtime_error("native " + definition->second.Label +
                                         " visual state has invalid mesh index");
            }
            visibleMeshIndices.push_back(meshIndex);
        }
        std::sort(visibleMeshIndices.begin(), visibleMeshIndices.end());
        visibleMeshIndices.erase(
            std::unique(visibleMeshIndices.begin(), visibleMeshIndices.end()),
            visibleMeshIndices.end());
    }

    uint32_t drawSuppressionMask = 0;
    bool initialDrawSuppressed = false;
    if (contract.contains("initial_draw_gate")) {
        const auto& gate = contract.at("initial_draw_gate");
        if (!gate.is_object() || gate.value("source", "") != "actor.params" ||
            !gate.contains("suppressed_when_mask_nonzero") ||
            !ReadJsonU32(gate.at("suppressed_when_mask_nonzero"),
                         drawSuppressionMask)) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " initial draw gate is invalid");
        }
        initialDrawSuppressed =
            (static_cast<uint16_t>(actor.Entry.Params) & drawSuppressionMask) != 0;
    }
    if (shapeYawStep != 0.0 && contract.contains("spawn_mode_contract")) {
        const auto& spawnMode = contract.at("spawn_mode_contract");
        uint32_t thrownMask = 0;
        uint32_t worldSpawnValue = 0;
        if (!spawnMode.is_object() || spawnMode.value("source", "") != "actor.params" ||
            !spawnMode.contains("thrown_spawn_mask") ||
            !ReadJsonU32(spawnMode.at("thrown_spawn_mask"), thrownMask) ||
            !spawnMode.contains("world_spawn_value") ||
            !ReadJsonU32(spawnMode.at("world_spawn_value"), worldSpawnValue)) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " spawn-mode contract is invalid");
        }
        if ((static_cast<uint16_t>(actor.Entry.Params) & thrownMask) != worldSpawnValue) {
            shapeYawStep = 0.0;
        }
    }

    EnvironmentActorVisualState state;
    state.ActorId = static_cast<uint16_t>(actor.Entry.ActorId);
    state.Selector = selector;
    state.ActorLabel = definition->second.Label;
    state.ContractFamily = contract.value("family", "");
    state.ModelScale = modelScale;
    state.ShapeYOffset = shapeYOffset;
    state.ShapeYawStepPerNativeTick = shapeYawStep;
    state.ShapeYawAccumulator = actor.ShapeRotation.Y;
    state.ModelLocalTranslation = modelLocalTranslation;
    state.InitialDrawSuppressed = initialDrawSuppressed;
    state.InitialDrawSuppressionMask = drawSuppressionMask;
    state.VisibleMeshIndices = std::move(visibleMeshIndices);
    state.ModelAssetId = stateIt->at("model_asset_id").get<std::string>();
    state.BindingKey = "native_rigid_actor_" + std::to_string(state.ActorId) + "_" +
                       std::to_string(actor.RuntimeId);
    actor.UniformScale = modelScale;
    if (initialDrawSuppressed) {
        RemoveArchiveSelectedVisualBinding(actor);
        state.Status = "native_rigid_actor_initial_draw_suppressed_by_code_bin_contract";
        actor.Entry.RenderBound = false;
        mEnvironmentActorVisuals[actor.RuntimeId] = std::move(state);
        return;
    }
    state.Source = mRenderProvider->Resolve(state.ModelAssetId);
    if (!state.Source || !state.Source->Ready()) {
        throw std::runtime_error(
            "native " + definition->second.Label + " model failed: " +
            (state.Source ? state.Source->Status : std::string("source_missing")));
    }
    auto model = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(state.Source->Model->Model);
    if (!state.VisibleMeshIndices.empty()) {
        std::erase_if(model.Batches, [&state](const auto& batch) {
            return !std::binary_search(state.VisibleMeshIndices.begin(),
                                       state.VisibleMeshIndices.end(), batch.MeshIndex);
        });
    }
    if (model.Batches.empty()) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " render model is empty");
    }
    model.Name = state.BindingKey;
    model.Source = state.ModelAssetId;
    model.ModelToWorld = BuildEnvironmentActorTransform(
        modelScale, shapeYOffset, modelLocalTranslation, actor.ShapeRotation,
        actor.WorldPosition);
    model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
    model.Diagnostics["native_rigid_actor_runtime"] = {
        {"actor_id", state.ActorId},
        {"actor_name", state.ActorLabel},
        {"runtime_id", actor.RuntimeId},
        {"entry_index", actor.Entry.EntryIndex},
        {"selector", selector},
        {"contract_family", state.ContractFamily},
        {"shape_y_offset", shapeYOffset},
        {"shape_yaw_step_per_native_tick", shapeYawStep},
        {"model_local_translation", {modelLocalTranslation.X,
                                     modelLocalTranslation.Y,
                                     modelLocalTranslation.Z}},
        {"visible_mesh_indices", state.VisibleMeshIndices},
        {"placement", "native_room_entry_pending_floor_raycast_bridge"},
    };
    RemoveArchiveSelectedVisualBinding(actor);
    mCurrentRenderScene->ActorVisuals.push_back(std::move(model));
    const size_t visualIndex = FindActorVisual(state.BindingKey);
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(
        *mCurrentRenderScene, visualIndex, 1);
    state.Status = "native_rigid_actor_initial_visual_from_code_bin_contract";
    actor.Entry.RenderBound = true;
    mEnvironmentActorVisuals[actor.RuntimeId] = std::move(state);
}

void NativeActorRuntime::UpdateRigidActor(NativeActorInstance& actor) {
    const auto found = mEnvironmentActorVisuals.find(actor.RuntimeId);
    if (found == mEnvironmentActorVisuals.end()) {
        return;
    }
    auto& state = found->second;
    if (state.InitialDrawSuppressed) {
        state.Status = "native_rigid_actor_initial_draw_suppressed_by_code_bin_contract";
        return;
    }
    if (state.ShapeYawStepPerNativeTick != 0.0) {
        state.ShapeYawAccumulator += state.ShapeYawStepPerNativeTick *
                                     mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
        state.ShapeYawAccumulator = std::fmod(state.ShapeYawAccumulator, 65536.0);
        if (state.ShapeYawAccumulator >= 32768.0) {
            state.ShapeYawAccumulator -= 65536.0;
        } else if (state.ShapeYawAccumulator < -32768.0) {
            state.ShapeYawAccumulator += 65536.0;
        }
        actor.ShapeRotation.Y = state.ShapeYawAccumulator;
        const size_t visualIndex = FindActorVisual(state.BindingKey);
        if (mCurrentRenderScene != nullptr &&
            visualIndex < mCurrentRenderScene->ActorVisuals.size()) {
            auto& model = mCurrentRenderScene->ActorVisuals[visualIndex];
            model.ModelToWorld = BuildEnvironmentActorTransform(
                state.ModelScale, state.ShapeYOffset, state.ModelLocalTranslation,
                actor.ShapeRotation, actor.WorldPosition);
            model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
        }
    }
    state.Status = "native_rigid_actor_visual_active_code_bin_transform_update";
}

void NativeActorRuntime::InitializeEnKusa(NativeActorInstance& actor) {
    const auto& profile = mEnKusaRuntimeContract.at("actor_profile");
    const auto profileMatches = [&](const char* field, uint32_t value) {
        uint32_t expected = 0;
        return profile.contains(field) && ReadJsonU32(profile.at(field), expected) &&
               expected == value;
    };
    if (!profileMatches("actor_id", static_cast<uint16_t>(actor.Entry.ActorId)) ||
        !profileMatches("category", actor.Profile.Category) ||
        !profileMatches("flags", actor.Profile.Flags) ||
        !profileMatches("object_id", actor.Profile.ObjectId) ||
        !profileMatches("instance_size", actor.Profile.InstanceSize) ||
        !profileMatches("init_address", actor.Profile.InitFunctionAddress) ||
        !profileMatches("destroy_address", actor.Profile.DestroyFunctionAddress) ||
        !profileMatches("update_address", actor.Profile.UpdateFunctionAddress) ||
        !profileMatches("draw_address", actor.Profile.DrawFunctionAddress)) {
        throw std::runtime_error(
            "native EnKusa spawn profile does not match its code.bin contract");
    }
    uint32_t selectorMask = 0;
    if (!mEnKusaRuntimeContract.contains("selector_mask") ||
        !ReadJsonU32(mEnKusaRuntimeContract.at("selector_mask"), selectorMask)) {
        throw std::runtime_error("native EnKusa selector mask is invalid");
    }
    const uint32_t selector = static_cast<uint16_t>(actor.Entry.Params) & selectorMask;
    const auto& states = mEnKusaRuntimeContract.at("visual_states");
    const auto stateIt = std::find_if(states.begin(), states.end(), [selector](const auto& value) {
        uint32_t candidate = 0;
        return value.is_object() && value.contains("selector") &&
               ReadJsonU32(value.at("selector"), candidate) && candidate == selector;
    });
    if (stateIt == states.end()) {
        Kill(actor.RuntimeId);
        return;
    }
    const bool destroyed = (actor.Profile.Flags & 0x800u) != 0;
    const char* modelField = destroyed && stateIt->contains("destroyed_model_asset_id")
                                 ? "destroyed_model_asset_id"
                                 : "intact_model_asset_id";
    if (!stateIt->contains(modelField) || !stateIt->at(modelField).is_string()) {
        throw std::runtime_error("native EnKusa visual state has no selected model");
    }
    double modelScale = 0.0;
    const auto& initChain = mEnKusaRuntimeContract.at("init_chain");
    if (!initChain.is_object() || !initChain.contains("model_scale") ||
        !ReadJsonFiniteDouble(initChain.at("model_scale"), modelScale)) {
        throw std::runtime_error("native EnKusa InitChain scale is invalid");
    }

    EnvironmentActorVisualState state;
    state.ActorId = static_cast<uint16_t>(actor.Entry.ActorId);
    state.Selector = selector;
    state.Destroyed = destroyed;
    state.ModelScale = modelScale;
    state.ModelAssetId = stateIt->at(modelField).get<std::string>();
    state.Source = mRenderProvider->Resolve(state.ModelAssetId);
    if (!state.Source || !state.Source->Ready()) {
        throw std::runtime_error(
            "native EnKusa model failed: " +
            (state.Source ? state.Source->Status : std::string("source_missing")));
    }
    state.BindingKey = "native_environment_actor_" + std::to_string(state.ActorId) + "_" +
                       std::to_string(actor.RuntimeId);
    auto model = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(state.Source->Model->Model);
    if (model.Batches.empty()) {
        throw std::runtime_error("native EnKusa render model is empty");
    }
    model.Name = state.BindingKey;
    model.Source = state.ModelAssetId;
    model.ModelToWorld = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(
        modelScale, actor.ShapeRotation, actor.WorldPosition);
    model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
    model.Diagnostics["native_environment_actor_runtime"] = {
        {"actor_id", state.ActorId},
        {"runtime_id", actor.RuntimeId},
        {"entry_index", actor.Entry.EntryIndex},
        {"selector", selector},
        {"destroyed", destroyed},
        {"placement", "native_room_entry_pending_floor_raycast_bridge"},
    };
    RemoveArchiveSelectedVisualBinding(actor);
    mCurrentRenderScene->ActorVisuals.push_back(std::move(model));
    const size_t visualIndex = FindActorVisual(state.BindingKey);
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(*mCurrentRenderScene, visualIndex,
                                                                1, false);
    state.Status = "native_enkusa_initial_visual_from_code_bin_contract";
    actor.UniformScale = modelScale;
    actor.Entry.RenderBound = true;
    mEnvironmentActorVisuals[actor.RuntimeId] = std::move(state);
}

void NativeActorRuntime::UpdateEnKusa(NativeActorInstance& actor) {
    const auto found = mEnvironmentActorVisuals.find(actor.RuntimeId);
    if (found != mEnvironmentActorVisuals.end()) {
        found->second.Status = "native_enkusa_visual_active_behavior_callbacks_pending";
    }
}

void NativeActorRuntime::DestroyEnKusa(NativeActorInstance& actor) {
    DestroyEnvironmentActor(actor);
}

void NativeActorRuntime::InitializeObjHana(NativeActorInstance& actor) {
    const auto& profile = mObjHanaRuntimeContract.at("actor_profile");
    const auto profileMatches = [&](const char* field, uint32_t value) {
        uint32_t expected = 0;
        return profile.contains(field) && ReadJsonU32(profile.at(field), expected) &&
               expected == value;
    };
    if (!profileMatches("actor_id", static_cast<uint16_t>(actor.Entry.ActorId)) ||
        !profileMatches("category", actor.Profile.Category) ||
        !profileMatches("flags", actor.Profile.Flags) ||
        !profileMatches("object_id", actor.Profile.ObjectId) ||
        !profileMatches("instance_size", actor.Profile.InstanceSize) ||
        !profileMatches("init_address", actor.Profile.InitFunctionAddress) ||
        !profileMatches("destroy_address", actor.Profile.DestroyFunctionAddress) ||
        !profileMatches("update_address", actor.Profile.UpdateFunctionAddress) ||
        !profileMatches("draw_address", actor.Profile.DrawFunctionAddress)) {
        throw std::runtime_error(
            "native ObjHana spawn profile does not match its code.bin contract");
    }

    uint32_t selectorMask = 0;
    if (!mObjHanaRuntimeContract.contains("selector_mask") ||
        !ReadJsonU32(mObjHanaRuntimeContract.at("selector_mask"), selectorMask)) {
        throw std::runtime_error("native ObjHana selector mask is invalid");
    }
    const uint32_t selector = static_cast<uint16_t>(actor.Entry.Params) & selectorMask;
    const auto& states = mObjHanaRuntimeContract.at("visual_states");
    const auto stateIt = std::find_if(states.begin(), states.end(), [selector](const auto& value) {
        uint32_t candidate = 0;
        return value.is_object() && value.contains("selector") &&
               ReadJsonU32(value.at("selector"), candidate) && candidate == selector;
    });
    if (stateIt == states.end()) {
        Kill(actor.RuntimeId);
        return;
    }
    if (!stateIt->contains("model_asset_id") || !stateIt->at("model_asset_id").is_string()) {
        throw std::runtime_error("native ObjHana visual state has no selected model");
    }
    double modelScale = 0.0;
    if (!stateIt->contains("model_scale") ||
        !ReadJsonFiniteDouble(stateIt->at("model_scale"), modelScale) || modelScale <= 0.0) {
        throw std::runtime_error("native ObjHana visual state has invalid scale");
    }

    EnvironmentActorVisualState state;
    state.ActorId = static_cast<uint16_t>(actor.Entry.ActorId);
    state.Selector = selector;
    state.ModelScale = modelScale;
    state.ModelAssetId = stateIt->at("model_asset_id").get<std::string>();
    state.Source = mRenderProvider->Resolve(state.ModelAssetId);
    if (!state.Source || !state.Source->Ready()) {
        throw std::runtime_error(
            "native ObjHana model failed: " +
            (state.Source ? state.Source->Status : std::string("source_missing")));
    }
    state.BindingKey = "native_environment_actor_" + std::to_string(state.ActorId) + "_" +
                       std::to_string(actor.RuntimeId);
    auto model = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(state.Source->Model->Model);
    if (model.Batches.empty()) {
        throw std::runtime_error("native ObjHana render model is empty");
    }
    model.Name = state.BindingKey;
    model.Source = state.ModelAssetId;
    model.ModelToWorld = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(
        modelScale, actor.ShapeRotation, actor.WorldPosition);
    model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
    model.Diagnostics["native_environment_actor_runtime"] = {
        {"actor_id", state.ActorId},
        {"runtime_id", actor.RuntimeId},
        {"entry_index", actor.Entry.EntryIndex},
        {"selector", selector},
        {"placement", "native_room_entry"},
    };
    RemoveArchiveSelectedVisualBinding(actor);
    mCurrentRenderScene->ActorVisuals.push_back(std::move(model));
    const size_t visualIndex = FindActorVisual(state.BindingKey);
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(*mCurrentRenderScene, visualIndex,
                                                                1, false);
    state.Status = "native_objhana_initial_visual_from_code_bin_contract";
    actor.UniformScale = modelScale;
    actor.Entry.RenderBound = true;
    mEnvironmentActorVisuals[actor.RuntimeId] = std::move(state);
}

void NativeActorRuntime::UpdateObjHana(NativeActorInstance& actor) {
    const auto found = mEnvironmentActorVisuals.find(actor.RuntimeId);
    if (found != mEnvironmentActorVisuals.end()) {
        found->second.Status = "native_objhana_visual_active_collision_and_save_bridge_pending";
    }
}

void NativeActorRuntime::DestroyEnvironmentActor(NativeActorInstance& actor) {
    const auto found = mEnvironmentActorVisuals.find(actor.RuntimeId);
    if (found == mEnvironmentActorVisuals.end()) {
        return;
    }
    const size_t visualIndex = FindActorVisual(found->second.BindingKey);
    if (mCurrentRenderScene != nullptr && visualIndex < mCurrentRenderScene->ActorVisuals.size()) {
        mCurrentRenderScene->ActorVisuals.erase(mCurrentRenderScene->ActorVisuals.begin() +
                                                static_cast<std::ptrdiff_t>(visualIndex));
    }
    mEnvironmentActorVisuals.erase(found);
}

void NativeActorRuntime::InitializeEnKo(NativeActorInstance& actor) {
    const auto& profile = mEnKoRuntimeContract.at("actor_profile");
    const auto profileMatches = [&](const char* field, uint32_t value) {
        uint32_t expected = 0;
        return profile.contains(field) && ReadJsonU32(profile.at(field), expected) &&
               expected == value;
    };
    if (!profileMatches("actor_id", static_cast<uint16_t>(actor.Entry.ActorId)) ||
        !profileMatches("category", actor.Profile.Category) ||
        !profileMatches("instance_size", actor.Profile.InstanceSize) ||
        !profileMatches("init_address", actor.Profile.InitFunctionAddress) ||
        !profileMatches("destroy_address", actor.Profile.DestroyFunctionAddress) ||
        !profileMatches("update_address", actor.Profile.UpdateFunctionAddress) ||
        !profileMatches("draw_address", actor.Profile.DrawFunctionAddress)) {
        throw std::runtime_error("native EnKo spawn profile does not match its code.bin contract");
    }
    const uint32_t subtype = static_cast<uint16_t>(actor.Entry.Params) & 0xFFu;
    const auto& lookup = mEnKoRuntimeContract.at("quest_animation_lookup");
    if (!lookup.is_array() || subtype >= lookup.size() || !lookup.at(subtype).is_array() ||
        mEnKoQuestState->Index >= lookup.at(subtype).size()) {
        throw std::runtime_error("native EnKo quest animation lookup is out of range");
    }
    uint32_t animationIndex = 0;
    if (!ReadJsonU32(lookup.at(subtype).at(mEnKoQuestState->Index), animationIndex)) {
        throw std::runtime_error("native EnKo quest animation entry is invalid");
    }

    EnKoVisualState state;
    state.Binding = mRenderProvider->ResolveEnKoRuntimeBinding(subtype, animationIndex);
    if (!state.Binding.Available) {
        throw std::runtime_error("native EnKo binding failed: " + state.Binding.Status + ":" +
                                 state.Binding.Error);
    }
    state.Source = mRenderProvider->Resolve(state.Binding.ModelAssetId);
    if (!state.Source || !state.Source->Ready()) {
        throw std::runtime_error(
            "native EnKo model failed: " +
            (state.Source ? state.Source->Status : std::string("source_missing")));
    }
    state.FaceSource = state.Binding.FaceModelAssetId == state.Binding.ModelAssetId
                           ? state.Source
                           : mRenderProvider->Resolve(state.Binding.FaceModelAssetId);
    if (!state.FaceSource || !state.FaceSource->Ready()) {
        throw std::runtime_error("native EnKo face model is unavailable");
    }
    state.Visibility =
        ThreeDsRecomp::Oot3d::ResolveNativeEnKoResourceVisibility(state.Binding, state.Source->Model->Model);
    if (!state.Visibility.Available) {
        throw std::runtime_error("native EnKo resource visibility failed: " +
                                 state.Visibility.Status);
    }
    state.AnimationFrame = state.Binding.StartFrame;
    state.Tracking.ShapeYaw = static_cast<int16_t>(
        std::lround(actor.ShapeRotation.Y));
    state.BindingKey = "native_enko_runtime_" + std::to_string(actor.RuntimeId);
    state.Status = "native_enko_initialized_from_code_bin_contract";
    actor.UniformScale = state.Binding.ModelScale;
    RemoveArchiveSelectedVisualBinding(actor);
    actor.Entry.RenderBound = true;
    mEnKoVisuals[actor.RuntimeId] = std::move(state);
    RefreshEnKoRenderModel(actor);
}

bool NativeActorRuntime::AdvanceActorBlink(
    const NativeActorBlinkProfile& profile, NativeActorBlinkState& state) {
    if (!mNativeA32Execution ||
        !AdvanceNativeActorBlinkTick(profile, state, mNativeRandomState,
                                     *mNativeA32Execution)) {
        ++mNativeRandomFailureCount;
        mNativeRandomStatus = "native_a32_rand_zero_one_failed";
        return false;
    }
    mNativeRandomStatus = "native_a32_rand_zero_one_active";
    return true;
}

void NativeActorRuntime::UpdateEnKo(NativeActorInstance& actor) {
    const auto found = mEnKoVisuals.find(actor.RuntimeId);
    if (found == mEnKoVisuals.end()) {
        return;
    }
    bool visualDirty = false;
    found->second.PendingAnimationTicks +=
        mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
    while (found->second.PendingAnimationTicks >= 1.0) {
        found->second.AnimationFrame += found->second.Binding.PlaybackSpeed;
        found->second.PendingAnimationTicks -= 1.0;
        visualDirty = true;
    }
    found->second.Blink.PendingTicks += mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
    while (found->second.Blink.PendingTicks >= 1.0) {
        const bool advanced =
            AdvanceActorBlink(mEnKoBlinkProfile, found->second.Blink);
        found->second.Blink.PendingTicks -= 1.0;
        if (!advanced) {
            break;
        }
    }
    if (mFrameContext.PlayerActorPositionValid && mEnKoQuestState.has_value() &&
        mEnKoQuestState->Available) {
        found->second.Tracking.PendingTicks +=
            mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
        const NativeNpcTrackingTickInput trackingInput = {
            mEnKoQuestState->Index,
            found->second.Binding.Subtype,
            0,
            actor.WorldPosition.X,
            actor.WorldPosition.Y,
            actor.WorldPosition.Z,
            mFrameContext.PlayerActorX,
            mFrameContext.PlayerActorY,
            mFrameContext.PlayerActorZ,
        };
        while (found->second.Tracking.PendingTicks >= 1.0) {
            if (UpdateNativeNpcTrackingTick(
                    mEnKoTrackingContract, trackingInput,
                    found->second.Tracking, mNativeRandomState,
                    *mNativeA32Execution)) {
                const NativeEnKoLimbCallbackInput limbInput = {
                    found->second.Tracking.HeadPitch,
                    found->second.Tracking.HeadYaw,
                    found->second.Tracking.TorsoPitch,
                    found->second.Tracking.TorsoYaw,
                };
                UpdateNativeEnKoLimbCallback(
                    mEnKoLimbCallbackContract, limbInput,
                    found->second.LimbCallback, *mNativeA32Execution);
            }
            found->second.Tracking.PendingTicks -= 1.0;
            visualDirty = true;
        }
        actor.ShapeRotation.Y = found->second.Tracking.ShapeYaw;
    }
    if (visualDirty) {
        RefreshEnKoRenderModel(actor);
    }
}

void NativeActorRuntime::DestroyEnKo(NativeActorInstance& actor) {
    const auto found = mEnKoVisuals.find(actor.RuntimeId);
    if (found == mEnKoVisuals.end()) {
        return;
    }
    const size_t visualIndex = FindActorVisual(found->second.BindingKey);
    if (mCurrentRenderScene != nullptr && visualIndex < mCurrentRenderScene->ActorVisuals.size()) {
        mCurrentRenderScene->ActorVisuals.erase(mCurrentRenderScene->ActorVisuals.begin() +
                                                static_cast<std::ptrdiff_t>(visualIndex));
    }
    mEnKoVisuals.erase(found);
}

void NativeActorRuntime::InitializeNamedActor(NativeActorInstance& actor) {
    const uint16_t actorId = static_cast<uint16_t>(actor.Entry.ActorId);
    const auto definition = mNamedActorDefinitions.find(actorId);
    if (definition == mNamedActorDefinitions.end()) {
        throw std::runtime_error("native named-actor definition is unavailable");
    }
    const auto& contract = definition->second.Contract;
    const auto& profile = contract.at("actor_profile");
    const auto profileMatches = [&](const char* field, uint32_t value) {
        uint32_t expected = 0;
        return profile.contains(field) && ReadJsonU32(profile.at(field), expected) &&
               expected == value;
    };
    if (!profileMatches("actor_id", static_cast<uint16_t>(actor.Entry.ActorId)) ||
        !profileMatches("category", actor.Profile.Category) ||
        !profileMatches("instance_size", actor.Profile.InstanceSize) ||
        !profileMatches("init_address", actor.Profile.InitFunctionAddress) ||
        !profileMatches("destroy_address", actor.Profile.DestroyFunctionAddress) ||
        !profileMatches("update_address", actor.Profile.UpdateFunctionAddress) ||
        !profileMatches("draw_address", actor.Profile.DrawFunctionAddress)) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " spawn profile does not match its code.bin contract");
    }

    const auto& animations = contract.at("animations");
    const auto animation =
        std::find_if(animations.begin(), animations.end(), [&definition](const auto& candidate) {
            uint32_t selector = 0;
            return candidate.is_object() && candidate.contains("selector_index") &&
                   ReadJsonU32(candidate.at("selector_index"), selector) &&
                   selector == definition->second.SpawnState.Index;
        });
    if (animation == animations.end()) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " spawn animation selector is unavailable");
    }
    const auto& model = contract.at("model");
    NamedActorVisualState state;
    state.ActorId = actorId;
    if (!model.is_object() || !model.contains("model_asset_id") ||
        !model.at("model_asset_id").is_string() || !model.contains("model_scale") ||
        !ReadJsonFiniteDouble(model.at("model_scale"), state.ModelScale) ||
        !animation->contains("animation_asset_id") ||
        !animation->at("animation_asset_id").is_string() || !animation->contains("csab_member") ||
        !animation->at("csab_member").is_string() ||
        !animation->contains("applied_playback_speed") ||
        !ReadJsonFiniteDouble(animation->at("applied_playback_speed"), state.PlaybackSpeed) ||
        !animation->contains("start_frame") ||
        !ReadJsonFiniteDouble(animation->at("start_frame"), state.StartFrame)) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " render binding is invalid");
    }
    uint32_t playbackMode = 0;
    if (!animation->contains("playback_mode") ||
        !ReadJsonU32(animation->at("playback_mode"), playbackMode) || playbackMode > UINT8_MAX) {
        throw std::runtime_error("native " + definition->second.Label +
                                 " playback mode is invalid");
    }
    state.ModelAssetId = model.at("model_asset_id").get<std::string>();
    state.AnimationAssetId = animation->at("animation_asset_id").get<std::string>();
    state.AnimationMember = animation->at("csab_member").get<std::string>();
    state.PlaybackMode = static_cast<uint8_t>(playbackMode);
    state.Source = mRenderProvider->Resolve(state.ModelAssetId);
    if (!state.Source || !state.Source->Ready()) {
        throw std::runtime_error(
            "native " + definition->second.Label + " model failed: " +
            (state.Source ? state.Source->Status : std::string("source_missing")));
    }
    const auto& face = contract.at("face_runtime");
    if (face.contains("mouth_frame_lookup")) {
        uint32_t initialMouthState = 0;
        if (!face.at("mouth_frame_lookup").is_array() || !face.contains("initial_mouth_state") ||
            !ReadJsonU32(face.at("initial_mouth_state"), initialMouthState) ||
            initialMouthState >= face.at("mouth_frame_lookup").size()) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " mouth state contract is invalid");
        }
        uint32_t mouthFrame = 0;
        if (!ReadJsonU32(face.at("mouth_frame_lookup").at(initialMouthState), mouthFrame) ||
            mouthFrame > UINT8_MAX) {
            throw std::runtime_error("native " + definition->second.Label +
                                     " initial mouth frame is invalid");
        }
        state.MouthFrame = static_cast<uint8_t>(mouthFrame);
        state.MouthSelected = true;
    }
    state.AnimationFrame = state.StartFrame;
    state.BindingKey =
        "native_named_actor_" + std::to_string(actorId) + "_" + std::to_string(actor.RuntimeId);
    state.Status = "native_named_actor_initialized_from_code_bin_contract";
    actor.UniformScale = state.ModelScale;
    RemoveArchiveSelectedVisualBinding(actor);
    actor.Entry.RenderBound = true;
    mNamedActorVisuals[actor.RuntimeId] = std::move(state);
    RefreshNamedActorRenderModel(actor);
}

void NativeActorRuntime::UpdateNamedActor(NativeActorInstance& actor) {
    const auto found = mNamedActorVisuals.find(actor.RuntimeId);
    if (found == mNamedActorVisuals.end()) {
        return;
    }
    const auto definition = mNamedActorDefinitions.find(found->second.ActorId);
    if (definition == mNamedActorDefinitions.end()) {
        return;
    }
    bool visualDirty = false;
    found->second.PendingAnimationTicks +=
        mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
    while (found->second.PendingAnimationTicks >= 1.0) {
        found->second.AnimationFrame += found->second.PlaybackSpeed;
        found->second.PendingAnimationTicks -= 1.0;
        visualDirty = true;
    }
    found->second.Blink.PendingTicks += mCurrentDeltaSeconds * kNativeDisplayTicksPerSecond;
    while (found->second.Blink.PendingTicks >= 1.0) {
        const bool advanced = AdvanceActorBlink(
            definition->second.BlinkProfile, found->second.Blink);
        found->second.Blink.PendingTicks -= 1.0;
        if (!advanced) {
            break;
        }
    }
    if (visualDirty) {
        RefreshNamedActorRenderModel(actor);
    }
}

void NativeActorRuntime::DestroyNamedActor(NativeActorInstance& actor) {
    const auto found = mNamedActorVisuals.find(actor.RuntimeId);
    if (found == mNamedActorVisuals.end()) {
        return;
    }
    const size_t visualIndex = FindActorVisual(found->second.BindingKey);
    if (mCurrentRenderScene != nullptr && visualIndex < mCurrentRenderScene->ActorVisuals.size()) {
        mCurrentRenderScene->ActorVisuals.erase(mCurrentRenderScene->ActorVisuals.begin() +
                                                static_cast<std::ptrdiff_t>(visualIndex));
    }
    mNamedActorVisuals.erase(found);
}

size_t NativeActorRuntime::FindActorVisual(std::string_view bindingKey) const {
    if (mCurrentRenderScene == nullptr) {
        return std::numeric_limits<size_t>::max();
    }
    const auto found = std::find_if(
        mCurrentRenderScene->ActorVisuals.begin(), mCurrentRenderScene->ActorVisuals.end(),
        [bindingKey](const auto& model) { return model.Name == bindingKey; });
    return static_cast<size_t>(std::distance(mCurrentRenderScene->ActorVisuals.begin(), found));
}

void NativeActorRuntime::RefreshEnKoRenderModel(NativeActorInstance& actor) {
    if (mCurrentRenderScene == nullptr || mRenderProvider == nullptr) {
        return;
    }
    auto& state = mEnKoVisuals.at(actor.RuntimeId);
    if (state.AnimationFrameCount > 0) {
        const double start = state.Binding.StartFrame;
        const double decodedEnd = state.Binding.EndFrame >= state.Binding.StartFrame
                                      ? state.Binding.EndFrame
                                      : static_cast<double>(state.AnimationFrameCount);
        const double span = std::max(1.0, decodedEnd - start);
        if (state.Binding.PlaybackMode == 0) {
            state.AnimationFrame =
                start + std::fmod(std::max(0.0, state.AnimationFrame - start), span);
        } else {
            state.AnimationFrame = std::clamp(state.AnimationFrame, start, decodedEnd);
        }
    }
    auto sample = mRenderProvider->SampleAnimation(
        *state.Source, state.Binding.AnimationAssetId, static_cast<float>(state.AnimationFrame));
    if (!sample.Ready()) {
        state.Status = sample.Status;
        state.Error = sample.Error;
        return;
    }
    state.AnimationFrameCount = sample.FrameCount;
    if (!ThreeDsRecomp::Oot3d::ApplyNativeActorPoseMatrixOverrides(
            state.Source->Model->Model.Skeleton, sample.Pose,
            {
                { mEnKoLimbCallbackContract.TorsoLimbIndex,
                  NativeMtx3x4ToPoseMatrix(
                      state.LimbCallback.TorsoTransform) },
                { mEnKoLimbCallbackContract.HeadLimbIndex,
                  NativeMtx3x4ToPoseMatrix(
                      state.LimbCallback.HeadTransform) },
            })) {
        state.Status = "native_enko_limb_callback_pose_override_failed";
        return;
    }
    ThreeDsRecomp::Oot3d::NativeActorFaceSample face;
    face.Available = true;
    face.EyeSelected = true;
    face.EyeIndex = mEnKoBlinkProfile.Sequence.at(state.Blink.SequenceIndex);
    face.MaterialAnimationSelectorAvailable = true;
    face.MaterialAnimationTypeLocalIndex = state.Binding.FaceAnimationSelector;
    face.Status = "native_enko_code_bin_blink_state";
    const bool faceMaterialDirty = !state.FaceMaterialApplied ||
                                   state.AppliedEyeIndex != face.EyeIndex;
    size_t visualIndex = FindActorVisual(state.BindingKey);
    if (visualIndex >= mCurrentRenderScene->ActorVisuals.size()) {
        auto model = mRenderProvider->BuildPosedRenderModel(
            *state.Source, sample.Pose, &face, &state.Visibility.ResourceVisibility,
            state.FaceSource.get());
        if (model.Batches.empty()) {
            state.Status = "native_enko_render_model_empty";
            return;
        }
        model.Name = state.BindingKey;
        model.Source = state.Binding.ModelAssetId + "|" + state.Binding.AnimationAssetId;
        mCurrentRenderScene->ActorVisuals.push_back(std::move(model));
        visualIndex = mCurrentRenderScene->ActorVisuals.size() - 1;
        state.FaceMaterialApplied = true;
        state.AppliedEyeIndex = face.EyeIndex;
    } else if (!mRenderProvider->UpdatePosedRenderModel(
                   mCurrentRenderScene->ActorVisuals[visualIndex], *state.Source,
                   sample.Pose, faceMaterialDirty ? &face : nullptr,
                   state.FaceSource.get())) {
        state.Status = "native_enko_render_model_update_failed";
        return;
    } else if (faceMaterialDirty) {
        state.FaceMaterialApplied = true;
        state.AppliedEyeIndex = face.EyeIndex;
    }
    auto& model = mCurrentRenderScene->ActorVisuals[visualIndex];
    model.ModelToWorld = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(
        state.Binding.ModelScale, actor.ShapeRotation, actor.WorldPosition);
    model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
    model.Diagnostics["native_enko_runtime"] = {
        {"runtime_id", actor.RuntimeId},
        {"entry_index", actor.Entry.EntryIndex},
        {"subtype", state.Binding.Subtype},
        {"quest_state", mEnKoQuestState->Semantic},
        {"semantic_animation_index", state.Binding.SemanticAnimationIndex},
        {"sampled_frame", sample.SampledFrame},
        {"eye_index", face.EyeIndex},
        {"blink_sequence_index", state.Blink.SequenceIndex},
        {"blink_timer", state.Blink.Timer},
        {"tunic_color", state.Binding.TunicColor},
        {"boots_color", state.Binding.BootsColor},
        {"tracking_mode", state.Tracking.TrackingMode},
        {"tracking_auto_turn_timer", state.Tracking.AutoTurnTimer},
        {"tracking_auto_turn_state", state.Tracking.AutoTurnState},
        {"tracking_status", state.Tracking.Status},
        {"tracking_error", state.Tracking.Error},
        {"tracking_update_count", state.Tracking.UpdateCount},
        {"limb_callback_status", state.LimbCallback.Status},
        {"limb_callback_error", state.LimbCallback.Error},
        {"limb_callback_update_count", state.LimbCallback.UpdateCount},
        {"limb_callback_torso_transform", state.LimbCallback.TorsoTransform},
        {"limb_callback_head_transform", state.LimbCallback.HeadTransform},
        {"head_pitch", state.Tracking.HeadPitch},
        {"head_yaw", state.Tracking.HeadYaw},
        {"torso_pitch", state.Tracking.TorsoPitch},
        {"torso_yaw", state.Tracking.TorsoYaw},
        {"shape_yaw", state.Tracking.ShapeYaw},
    };
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(*mCurrentRenderScene, visualIndex,
                                                                1, false);
    state.Status = "native_enko_rendered_from_native_contract";
    state.Error.clear();
}

void NativeActorRuntime::RefreshNamedActorRenderModel(NativeActorInstance& actor) {
    if (mCurrentRenderScene == nullptr || mRenderProvider == nullptr) {
        return;
    }
    auto& state = mNamedActorVisuals.at(actor.RuntimeId);
    const auto definition = mNamedActorDefinitions.find(state.ActorId);
    if (definition == mNamedActorDefinitions.end()) {
        return;
    }
    if (state.AnimationFrameCount > 0) {
        const double end = static_cast<double>(state.AnimationFrameCount);
        const double span = std::max(1.0, end - state.StartFrame);
        if (state.PlaybackMode == 0) {
            state.AnimationFrame =
                state.StartFrame +
                std::fmod(std::max(0.0, state.AnimationFrame - state.StartFrame), span);
        } else {
            state.AnimationFrame = std::clamp(state.AnimationFrame, state.StartFrame, end);
        }
    }
    const auto sample = mRenderProvider->SampleAnimation(*state.Source, state.AnimationAssetId,
                                                         static_cast<float>(state.AnimationFrame));
    if (!sample.Ready()) {
        state.Status = sample.Status;
        state.Error = sample.Error;
        return;
    }
    state.AnimationFrameCount = sample.FrameCount;
    ThreeDsRecomp::Oot3d::NativeActorFaceSample face;
    face.Available = true;
    face.EyeSelected = true;
    face.EyeIndex = definition->second.BlinkProfile.Sequence.at(state.Blink.SequenceIndex);
    face.MouthSelected = state.MouthSelected;
    face.MouthIndex = state.MouthFrame;
    face.Status = "native_named_actor_code_bin_face_state";
    const bool faceMaterialDirty = !state.FaceMaterialApplied ||
                                   state.AppliedEyeIndex != face.EyeIndex ||
                                   state.AppliedMouthIndex != face.MouthIndex;
    size_t visualIndex = FindActorVisual(state.BindingKey);
    if (visualIndex >= mCurrentRenderScene->ActorVisuals.size()) {
        auto model = mRenderProvider->BuildPosedRenderModel(*state.Source, sample.Pose, &face);
        if (model.Batches.empty()) {
            state.Status = "native_named_actor_render_model_empty";
            return;
        }
        model.Name = state.BindingKey;
        model.Source = state.ModelAssetId + "|" + state.AnimationAssetId;
        mCurrentRenderScene->ActorVisuals.push_back(std::move(model));
        visualIndex = mCurrentRenderScene->ActorVisuals.size() - 1;
        state.FaceMaterialApplied = true;
        state.AppliedEyeIndex = face.EyeIndex;
        state.AppliedMouthIndex = face.MouthIndex;
    } else if (!mRenderProvider->UpdatePosedRenderModel(
                   mCurrentRenderScene->ActorVisuals[visualIndex], *state.Source,
                   sample.Pose, faceMaterialDirty ? &face : nullptr)) {
        state.Status = "native_named_actor_render_model_update_failed";
        return;
    } else if (faceMaterialDirty) {
        state.FaceMaterialApplied = true;
        state.AppliedEyeIndex = face.EyeIndex;
        state.AppliedMouthIndex = face.MouthIndex;
    }
    auto& model = mCurrentRenderScene->ActorVisuals[visualIndex];
    model.ModelToWorld = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(
        state.ModelScale, actor.ShapeRotation, actor.WorldPosition);
    model.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
    model.Diagnostics["native_named_actor_runtime"] = {
        {"actor_id", state.ActorId},
        {"actor_label", definition->second.Label},
        {"runtime_id", actor.RuntimeId},
        {"entry_index", actor.Entry.EntryIndex},
        {"spawn_state", definition->second.SpawnState.Semantic},
        {"animation_selector_index", definition->second.SpawnState.Index},
        {"animation_member", state.AnimationMember},
        {"sampled_frame", sample.SampledFrame},
        {"eye_index", face.EyeIndex},
        {"mouth_index", face.MouthIndex},
        {"blink_sequence_index", state.Blink.SequenceIndex},
        {"blink_timer", state.Blink.Timer},
    };
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(*mCurrentRenderScene, visualIndex,
                                                                1, false);
    state.Status = "native_named_actor_rendered_from_native_contract";
    state.Error.clear();
}

NativeActorInstance* NativeActorRuntime::Spawn(const NativeActorSpawnEntry& entry,
                                               const ThreeDsRecomp::Oot3d::NativeActorProfile& profile) {
    if (!profile.Valid || profile.ActorId != static_cast<uint16_t>(entry.ActorId) ||
        profile.Category >= mCategoryLists.size() ||
        mLiveCount >= mContract.SpawnTotalGuardValue()) {
        return nullptr;
    }

    auto actor = std::make_unique<NativeActorInstance>();
    actor->RuntimeId = mNextRuntimeId++;
    actor->Entry = entry;
    actor->Profile = profile;
    actor->Category = profile.Category;
    actor->HomePosition = entry.Position;
    actor->WorldPosition = entry.Position;
    actor->PreviousPosition = entry.Position;
    actor->HomeRotation = entry.Rotation;
    actor->WorldRotation = entry.Rotation;
    actor->ShapeRotation = entry.Rotation;
    actor->State = entry.ExternalOwner ? NativeActorLifecycleState::ActiveExternalOwner
                                       : NativeActorLifecycleState::PendingNativeInit;

    const uint64_t runtimeId = actor->RuntimeId;
    auto* result = actor.get();
    mActors.push_back(std::move(actor));
    mCategoryLists[profile.Category].push_front(runtimeId);
    ++mLiveCount;
    if (!entry.ExternalOwner) {
        TryInitialize(*result);
    }
    return result;
}

NativeActorInstance* NativeActorRuntime::Find(int16_t actorId, uint8_t category) {
    if (category >= mCategoryLists.size()) {
        return nullptr;
    }
    for (const uint64_t runtimeId : mCategoryLists[category]) {
        auto* actor = FindByRuntimeId(runtimeId);
        if (actor != nullptr && actor->Entry.ActorId == actorId &&
            actor->State != NativeActorLifecycleState::Deleted) {
            return actor;
        }
    }
    return nullptr;
}

bool NativeActorRuntime::Kill(uint64_t runtimeId) {
    auto* actor = FindByRuntimeId(runtimeId);
    if (actor == nullptr || actor->State == NativeActorLifecycleState::Deleted) {
        return false;
    }
    actor->State = NativeActorLifecycleState::PendingNativeDestroy;
    return true;
}

bool NativeActorRuntime::ChangeCategory(uint64_t runtimeId, uint8_t category) {
    auto* actor = FindByRuntimeId(runtimeId);
    if (actor == nullptr || actor->State == NativeActorLifecycleState::Deleted ||
        category >= mCategoryLists.size() || actor->Category == category) {
        return false;
    }
    mCategoryLists[actor->Category].remove(runtimeId);
    actor->Category = category;
    mCategoryLists[category].push_front(runtimeId);
    return true;
}

void NativeActorRuntime::RegisterCallback(uint32_t address, Callback callback) {
    if (address == 0 || !callback) {
        throw std::invalid_argument("native Actor callback registration is invalid");
    }
    mCallbacks[address] = std::move(callback);
}

size_t NativeActorRuntime::LiveCount() const {
    return mLiveCount;
}

const std::list<uint64_t>& NativeActorRuntime::CategoryList(uint8_t category) const {
    if (category >= mCategoryLists.size()) {
        throw std::out_of_range("native Actor category is outside the decoded context");
    }
    return mCategoryLists[category];
}

NativeActorInstance* NativeActorRuntime::FindByRuntimeId(uint64_t runtimeId) {
    const auto found = std::find_if(mActors.begin(), mActors.end(), [runtimeId](const auto& actor) {
        return actor->RuntimeId == runtimeId;
    });
    return found == mActors.end() ? nullptr : found->get();
}

bool NativeActorRuntime::DispatchCallback(NativeActorInstance& actor, uint32_t address,
                                          const char* role) {
    if (address == 0) {
        return true;
    }
    const auto found = mCallbacks.find(address);
    if (found == mCallbacks.end()) {
        RecordUnsupported(actor, role, address);
        return false;
    }
    found->second(actor);
    return true;
}

void NativeActorRuntime::TryInitialize(NativeActorInstance& actor) {
    if (actor.State != NativeActorLifecycleState::PendingNativeInit) {
        return;
    }
    if (DispatchCallback(actor, actor.Profile.InitFunctionAddress, "init")) {
        actor.State = NativeActorLifecycleState::Active;
    }
}

void NativeActorRuntime::TryDestroy(NativeActorInstance& actor) {
    if (actor.State != NativeActorLifecycleState::PendingNativeDestroy) {
        return;
    }
    if (DispatchCallback(actor, actor.Profile.DestroyFunctionAddress, "destroy")) {
        DeleteActor(actor);
    }
}

void NativeActorRuntime::DeleteActor(NativeActorInstance& actor) {
    if (mNativeAudioService) {
        mNativeAudioService->StopOwner(actor.RuntimeId);
    }
    mCategoryLists[actor.Category].remove(actor.RuntimeId);
    actor.State = NativeActorLifecycleState::Deleted;
    if (mLiveCount > 0) {
        --mLiveCount;
    }
}

void NativeActorRuntime::RecordUnsupported(const NativeActorInstance& actor, const char* role,
                                           uint32_t address) {
    const auto key = std::make_tuple(actor.RuntimeId, std::string(role), address);
    if (!mUnsupportedCallbackKeys.insert(key).second) {
        return;
    }
    mUnsupportedCallbacks.push_back({
        actor.RuntimeId,
        actor.Entry.EntryIndex,
        actor.Entry.ActorId,
        "ACTOR_0x" + std::to_string(static_cast<uint16_t>(actor.Entry.ActorId)),
        role,
        address,
    });
}

void NativeActorRuntime::RecordProfileGap(const NativeActorSpawnEntry& entry, std::string reason) {
    mProfileGaps.emplace_back(entry.EntryIndex, std::move(reason));
}

bool NativeActorRuntime::HasRenderBinding(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                          int actorEntryIndex, int32_t actorId) const {
    return std::any_of(scene.ActorVisualInstances.begin(), scene.ActorVisualInstances.end(),
                       [actorEntryIndex, actorId](const auto& visual) {
                           return visual.ActorEntryIndex == actorEntryIndex &&
                                  visual.ActorId == actorId;
                       });
}

size_t NativeActorRuntime::RemoveArchiveSelectedVisualBinding(
    NativeActorInstance& actor) {
    if (!actor.Entry.RenderBound || mCurrentScene == nullptr ||
        mCurrentRenderScene == nullptr) {
        return 0;
    }

    std::vector<std::string> namePrefixes;
    for (const auto& visual : mCurrentScene->ActorVisualInstances) {
        if (visual.ActorEntryIndex == actor.Entry.EntryIndex &&
            visual.ActorId == actor.Entry.ActorId) {
            namePrefixes.push_back(visual.ActorName + "#" +
                                   std::to_string(visual.ActorEntryIndex) + ":");
        }
    }
    const auto before = mCurrentRenderScene->ActorVisuals.size();
    std::erase_if(mCurrentRenderScene->ActorVisuals, [&namePrefixes](const auto& model) {
        return std::any_of(namePrefixes.begin(), namePrefixes.end(),
                           [&model](const auto& prefix) {
                               return model.Name.starts_with(prefix);
                           });
    });
    const size_t removed = before - mCurrentRenderScene->ActorVisuals.size();
    mArchiveSelectedVisualReplacementCount += removed;
    actor.Entry.RenderBound = false;
    return removed;
}

nlohmann::json NativeActorRuntime::Diagnostics() const {
    nlohmann::json categoryLists = nlohmann::json::array();
    for (size_t category = 0; category < mCategoryLists.size(); ++category) {
        nlohmann::json runtimeIds = nlohmann::json::array();
        for (const auto runtimeId : mCategoryLists[category]) {
            runtimeIds.push_back(runtimeId);
        }
        categoryLists.push_back({
            {"category", category},
            {"count", mCategoryLists[category].size()},
            {"head_to_tail_runtime_ids", std::move(runtimeIds)},
        });
    }

    nlohmann::json actors = nlohmann::json::array();
    for (const auto& actor : mActors) {
        actors.push_back({
            {"runtime_id", actor->RuntimeId},
            {"instance_key", actor->Entry.InstanceKey},
            {"profile_key", actor->Entry.ProfileKey},
            {"entry_index", actor->Entry.EntryIndex},
            {"room_index", actor->Entry.RoomIndex},
            {"actor_id", static_cast<uint16_t>(actor->Entry.ActorId)},
            {"category", actor->Category},
            {"params", actor->Entry.Params},
            {"instance_size", actor->Profile.InstanceSize},
            {"state", NativeActorLifecycleStateName(actor->State)},
            {"render_bound", actor->Entry.RenderBound},
            {"external_owner", actor->Entry.ExternalOwner},
            {"init_address", actor->Profile.InitFunctionAddress},
            {"destroy_address", actor->Profile.DestroyFunctionAddress},
            {"update_address", actor->Profile.UpdateFunctionAddress},
            {"draw_address", actor->Profile.DrawFunctionAddress},
            {"sfx_request", actor->SfxRequest},
            {"update_count", actor->UpdateCount},
        });
    }

    nlohmann::json unsupported = nlohmann::json::array();
    for (const auto& gap : mUnsupportedCallbacks) {
        const auto* closure =
            mNativeClosureCatalog ? mNativeClosureCatalog->Find(gap.Address) : nullptr;
        unsupported.push_back({
            {"runtime_id", gap.RuntimeId},
            {"entry_index", gap.EntryIndex},
            {"actor_id", static_cast<uint16_t>(gap.ActorId)},
            {"role", gap.Role},
            {"address", gap.Address},
            {"native_function", closure ? closure->Name : ""},
            {"native_module", closure ? closure->Module : ""},
            {"body_status", closure ? closure->BodyStatus : ""},
            {"compile_readiness", closure ? closure->CompileReadiness : ""},
            {"hazards", closure ? nlohmann::json(closure->Hazards)
                                  : nlohmann::json::array()},
        });
    }
    nlohmann::json profileGaps = nlohmann::json::array();
    for (const auto& [entryIndex, reason] : mProfileGaps) {
        profileGaps.push_back({{"entry_index", entryIndex}, {"reason", reason}});
    }
    nlohmann::json objectBankRuntime =
        mObjectBankRuntime
            ? mObjectBankRuntime->Diagnostics()
            : nlohmann::json{{"available", false}};
    nlohmann::json objectBanks = objectBankRuntime.value(
        "banks", nlohmann::json::array());
    objectBankRuntime.erase("banks");
    objectBankRuntime["cleanup_rooms"] = mObjectBankCleanupRooms;
    nlohmann::json deferredCompilationInstances = nlohmann::json::array();
    for (const auto& actor : mDeferredCompilationInstances) {
        deferredCompilationInstances.push_back({
            {"instance_key", actor.InstanceKey},
            {"profile_key", actor.ProfileKey},
            {"actor_id", actor.ActorId},
            {"room_index", actor.RoomIndex},
            {"reason", actor.Reason},
        });
    }
    nlohmann::json enKoVisuals = nlohmann::json::array();
    for (const auto& [runtimeId, visual] : mEnKoVisuals) {
        enKoVisuals.push_back({
            {"runtime_id", runtimeId},
            {"subtype", visual.Binding.Subtype},
            {"semantic_animation_index", visual.Binding.SemanticAnimationIndex},
            {"model_asset_id", visual.Binding.ModelAssetId},
            {"animation_asset_id", visual.Binding.AnimationAssetId},
            {"animation_frame", visual.AnimationFrame},
            {"eye_index", mEnKoBlinkProfile.Available
                              ? mEnKoBlinkProfile.Sequence.at(visual.Blink.SequenceIndex)
                              : 0},
            {"blink_sequence_index", visual.Blink.SequenceIndex},
            {"blink_timer", visual.Blink.Timer},
            {"tracking_mode", visual.Tracking.TrackingMode},
            {"tracking_auto_turn_timer", visual.Tracking.AutoTurnTimer},
            {"tracking_auto_turn_state", visual.Tracking.AutoTurnState},
            {"tracking_status", visual.Tracking.Status},
            {"tracking_error", visual.Tracking.Error},
            {"tracking_update_count", visual.Tracking.UpdateCount},
            {"limb_callback_status", visual.LimbCallback.Status},
            {"limb_callback_error", visual.LimbCallback.Error},
            {"limb_callback_update_count", visual.LimbCallback.UpdateCount},
            {"limb_callback_torso_transform",
             visual.LimbCallback.TorsoTransform},
            {"limb_callback_head_transform",
             visual.LimbCallback.HeadTransform},
            {"head_pitch", visual.Tracking.HeadPitch},
            {"head_yaw", visual.Tracking.HeadYaw},
            {"torso_pitch", visual.Tracking.TorsoPitch},
            {"torso_yaw", visual.Tracking.TorsoYaw},
            {"shape_yaw", visual.Tracking.ShapeYaw},
            {"status", visual.Status},
            {"error", visual.Error},
        });
    }
    nlohmann::json namedActorVisuals = nlohmann::json::array();
    for (const auto& [runtimeId, visual] : mNamedActorVisuals) {
        const auto definition = mNamedActorDefinitions.find(visual.ActorId);
        namedActorVisuals.push_back({
            {"actor_id", visual.ActorId},
            {"actor_label",
             definition != mNamedActorDefinitions.end() ? definition->second.Label : "unknown"},
            {"runtime_id", runtimeId},
            {"model_asset_id", visual.ModelAssetId},
            {"animation_asset_id", visual.AnimationAssetId},
            {"animation_member", visual.AnimationMember},
            {"animation_frame", visual.AnimationFrame},
            {"eye_index",
             definition != mNamedActorDefinitions.end()
                 ? definition->second.BlinkProfile.Sequence.at(visual.Blink.SequenceIndex)
                 : 0},
            {"mouth_selected", visual.MouthSelected},
            {"mouth_frame", visual.MouthFrame},
            {"blink_sequence_index", visual.Blink.SequenceIndex},
            {"blink_timer", visual.Blink.Timer},
            {"status", visual.Status},
            {"error", visual.Error},
        });
    }
    nlohmann::json namedActorDefinitions = nlohmann::json::array();
    for (const auto& [actorId, definition] : mNamedActorDefinitions) {
        namedActorDefinitions.push_back({
            {"actor_id", actorId},
            {"actor_label", definition.Label},
            {"spawn_state_available", definition.SpawnState.Available},
            {"spawn_state_index", definition.SpawnState.Index},
            {"spawn_state_semantic", definition.SpawnState.Semantic},
            {"spawn_state_status", definition.SpawnState.Status},
            {"blink_sequence", definition.BlinkProfile.Sequence},
            {"blink_timer_base", definition.BlinkProfile.TimerBase},
            {"blink_timer_range", definition.BlinkProfile.TimerRange},
        });
    }
    nlohmann::json environmentActorVisuals = nlohmann::json::array();
    for (const auto& [runtimeId, visual] : mEnvironmentActorVisuals) {
        environmentActorVisuals.push_back({
            {"actor_id", visual.ActorId},
            {"actor_label", visual.ActorLabel},
            {"contract_family", visual.ContractFamily},
            {"runtime_id", runtimeId},
            {"selector", visual.Selector},
            {"destroyed", visual.Destroyed},
            {"model_asset_id", visual.ModelAssetId},
            {"model_scale", visual.ModelScale},
            {"shape_y_offset", visual.ShapeYOffset},
            {"shape_yaw_step_per_native_tick", visual.ShapeYawStepPerNativeTick},
            {"shape_yaw_s16", visual.ShapeYawAccumulator},
            {"model_local_translation", {visual.ModelLocalTranslation.X,
                                         visual.ModelLocalTranslation.Y,
                                         visual.ModelLocalTranslation.Z}},
            {"initial_draw_suppressed", visual.InitialDrawSuppressed},
            {"initial_draw_suppression_mask", visual.InitialDrawSuppressionMask},
            {"visible_mesh_indices", visual.VisibleMeshIndices},
            {"status", visual.Status},
            {"error", visual.Error},
        });
    }
    nlohmann::json enHollStates = nlohmann::json::array();
    for (const auto& [runtimeId, state] : mEnHollStates) {
        enHollStates.push_back({
            {"runtime_id", runtimeId},
            {"action_mode", state.ActionMode},
            {"transition_index", state.TransitionIndex},
            {"front_room", state.FrontRoom},
            {"back_room", state.BackRoom},
            {"waiting_for_room", state.WaitingForRoom},
            {"request_count", state.RequestCount},
            {"supported", state.LastEvaluation.Supported},
            {"inside_trigger", state.LastEvaluation.InsideTrigger},
            {"room_request_required",
             state.LastEvaluation.RoomRequestRequired},
            {"side", state.LastEvaluation.Side},
            {"target_room", state.LastEvaluation.TargetRoom},
            {"half_width", state.LastEvaluation.HalfWidth},
            {"local_position",
             {state.LastEvaluation.LocalPosition.X,
              state.LastEvaluation.LocalPosition.Y,
              state.LastEvaluation.LocalPosition.Z}},
            {"status", state.Status},
        });
    }
    nlohmann::json enRiverSoundStates = nlohmann::json::array();
    for (const auto& [runtimeId, state] : mEnRiverSoundStates) {
        enRiverSoundStates.push_back({
            {"runtime_id", runtimeId},
            {"sound_id", state.SoundId},
            {"native_sound_id", state.NativeSoundId},
            {"path_index", state.PathIndex},
            {"update_count", state.UpdateCount},
            {"draw_initialized", state.DrawInitialized},
            {"status", state.Status},
        });
    }
    nlohmann::json behaviorProfiles = nlohmann::json::array();
    size_t boundBehaviorFunctionCount = 0;
    size_t boundIndirectRootCount = 0;
    size_t boundNativeAbiFunctionCount = 0;
    size_t boundConsumerFunctionCount = 0;
    size_t boundLifecycleConsumerCount = 0;
    if (mConfig.CompilationUnit) {
        for (const auto& profile : mConfig.CompilationUnit->actorProfiles) {
            size_t boundFunctionCount = 0;
            size_t boundProfileIndirectRootCount = 0;
            size_t boundProfileNativeAbiFunctionCount = 0;
            size_t boundProfileConsumerFunctionCount = 0;
            size_t boundProfileLifecycleConsumerCount = 0;
            for (const auto& function : profile.behaviorGraph.functions) {
                const bool bound = mCallbacks.contains(function.runtimeAddress);
                boundFunctionCount += bound ? 1u : 0u;
                boundProfileIndirectRootCount +=
                    bound && function.closureKind == "indirect_root" ? 1u : 0u;
                boundProfileNativeAbiFunctionCount +=
                    bound && !function.closureKind.empty() ? 1u : 0u;
                boundProfileConsumerFunctionCount +=
                    bound && !function.consumerRole.empty() ? 1u : 0u;
                boundProfileLifecycleConsumerCount +=
                    bound && function.consumerRole == "native_lifecycle_consumer"
                        ? 1u
                        : 0u;
            }
            boundBehaviorFunctionCount += boundFunctionCount;
            boundIndirectRootCount += boundProfileIndirectRootCount;
            boundNativeAbiFunctionCount += boundProfileNativeAbiFunctionCount;
            boundConsumerFunctionCount += boundProfileConsumerFunctionCount;
            boundLifecycleConsumerCount += boundProfileLifecycleConsumerCount;
            behaviorProfiles.push_back({
                {"profile_key", profile.profileKey},
                {"actor_id", profile.actorId},
                {"actor_name", profile.actorName},
                {"available", profile.behaviorGraph.available},
                {"status", profile.behaviorGraph.status},
                {"owner", profile.behaviorGraph.owner},
                {"structure", profile.behaviorGraph.structure},
                {"structure_size", profile.behaviorGraph.structureSize},
                {"function_count", profile.behaviorGraph.functions.size()},
                {"indirect_root_count", profile.behaviorGraph.indirectRootCount},
                {"native_abi_function_count",
                 profile.behaviorGraph.nativeAbiFunctionCount},
                {"consumer_status", profile.behaviorGraph.consumerStatus},
                {"consumer_root_count", profile.behaviorGraph.consumerRootCount},
                {"consumer_function_count",
                 profile.behaviorGraph.consumerFunctionCount},
                {"consumer_call_edge_count",
                 profile.behaviorGraph.consumerCallEdgeCount},
                {"actor_local_consumer_function_count",
                 profile.behaviorGraph.actorLocalConsumerFunctionCount},
                {"native_service_dependency_count",
                 profile.behaviorGraph.nativeServiceDependencyCount},
                {"bound_consumer_function_count",
                 boundProfileConsumerFunctionCount},
                {"bound_lifecycle_consumer_count",
                 boundProfileLifecycleConsumerCount},
                {"bound_indirect_root_count", boundProfileIndirectRootCount},
                {"bound_native_abi_function_count",
                 boundProfileNativeAbiFunctionCount},
                {"bound_function_count", boundFunctionCount},
                {"unbound_function_count",
                 profile.behaviorGraph.functions.size() - boundFunctionCount},
                {"action_transition_count",
                 profile.behaviorGraph.actionTransitions.size()},
                {"structure_field_count",
                 profile.behaviorGraph.structureFields.size()},
                {"initial_action_candidates",
                 profile.behaviorGraph.initialActionCandidates},
                {"runtime_binding_status",
                 profile.behaviorGraph.runtimeBindingStatus},
                {"consumer_runtime_binding_status",
                 profile.behaviorGraph.consumerRuntimeBindingStatus},
            });
        }
    }
    const size_t behaviorFunctionCount =
        mConfig.CompilationUnit ? mConfig.CompilationUnit->behaviorFunctionCount : 0;
    nlohmann::json roomLifecycle = {{"available", mRoomRuntime != nullptr}};
    if (mRoomRuntime && mConfig.CompilationUnit) {
        size_t residentRoomActorCount = 0;
        size_t residentTransitionActorCount = 0;
        for (const auto& actor : mConfig.CompilationUnit->actorInstances) {
            if (!mCompilationRuntimeIds.contains(actor.instanceKey)) {
                continue;
            }
            if (actor.transition) {
                ++residentTransitionActorCount;
            } else if (actor.sourceKind == "room_actor_list") {
                ++residentRoomActorCount;
            }
        }
        roomLifecycle = {
            {"available", true},
            {"source_snapshot_id", mConfig.CompilationUnit->roomLifecycle.sourceSnapshotId},
            {"current_room", mRoomRuntime->CurrentRoom()},
            {"previous_room", mRoomRuntime->PreviousRoom()},
            {"pending_room", mRoomRuntime->PendingRoom()},
            {"load_state", mRoomRuntime->LoadState()},
            {"resident_rooms", mRoomRuntime->ResidentRooms()},
            {"resident_room_actor_count", residentRoomActorCount},
            {"resident_transition_actor_count", residentTransitionActorCount},
            {"resource_cleanup_pending", mRoomRuntime->ResourceCleanupPending()},
            {"cleanup_ticks_remaining", mRoomRuntime->CleanupTicksRemaining()},
            {"request_count", mRoomRuntime->RequestCount()},
            {"completion_count", mRoomRuntime->CompletionCount()},
            {"resource_cleanup_completion_count",
             mRoomRuntime->ResourceCleanupCompletionCount()},
        };
    }
    roomLifecycle["render_resources"] =
        mRoomRenderRuntime
            ? mRoomRenderRuntime->Diagnostics()
            : nlohmann::json{{"available", false}};

    nlohmann::json nativeAbiSubsystemContracts = nlohmann::json::array();
    if (mNativeAbiCatalog) {
        for (const auto& contract : mNativeAbiCatalog->subsystemContracts) {
            nativeAbiSubsystemContracts.push_back({
                {"id", contract.id},
                {"status", contract.status},
                {"required_functions", contract.requiredFunctions},
                {"available_functions", contract.availableFunctions},
                {"missing_functions", contract.missingFunctions},
                {"source_tranches", contract.sourceTranches},
                {"evidence_files", contract.evidenceFiles},
            });
        }
    }

    const NativeAudioServiceStats audioStats = mNativeAudioService
        ? mNativeAudioService->Stats() : NativeAudioServiceStats{};

    const double frameDivisor = mFrameCount > 0
                                    ? static_cast<double>(mFrameCount)
                                    : 1.0;
    return {
        { "enabled", true },
        { "runtime_id", "oot3d_native_actor_core" },
        { "status", "native_actor_context_active_profile_callbacks_explicit" },
        { "performance", {
            { "sample_count", mFrameCount },
            { "actor_callbacks_average_ms", mActorCallbackSeconds * 1000.0 / frameDivisor },
            { "room_lifecycle_average_ms", mRoomLifecycleSeconds * 1000.0 / frameDivisor },
            { "room_render_average_ms", mRoomRenderSeconds * 1000.0 / frameDivisor },
            { "audio_state_average_ms", mAudioStateSeconds * 1000.0 / frameDivisor },
          } },
        { "contract_snapshot_id", mContract.SnapshotId() },
        { "contract_source_revision", mContract.SourceRevision() },
        { "actor_entry_size", mContract.ActorEntrySize() },
        { "actor_context_size", mContract.ActorContextSize() },
        { "category_list_count", mContract.CategoryListCount() },
        { "spawn_total_guard_value", mContract.SpawnTotalGuardValue() },
        { "frame_count", mFrameCount },
        { "population_source", mPopulationFromCompilationUnit
                                     ? "oot3d_room_compilation_unit"
                                     : "legacy_native_scene_graph" },
        { "room_compilation_unit", {
            { "available", mConfig.CompilationUnit != nullptr },
            { "route_id", mConfig.CompilationUnit ? mConfig.CompilationUnit->routeId : "" },
            { "unit_id", mConfig.CompilationUnit ? mConfig.CompilationUnit->unitId : "" },
            { "payload_sha256", mConfig.CompilationUnit
                                      ? mConfig.CompilationUnit->payloadSha256 : "" },
            { "compiled_room_count", mCompiledRoomCount },
            { "compiled_actor_instance_count", mCompiledActorInstanceCount },
            { "compiled_actor_profile_count", mCompiledActorProfileCount },
            { "compiled_object_dependency_count", mCompiledObjectDependencyCount },
            { "behavior_graph_profile_count", mConfig.CompilationUnit
                                                    ? mConfig.CompilationUnit
                                                          ->behaviorGraphProfileCount
                                                    : 0 },
            { "behavior_function_count", behaviorFunctionCount },
            { "bound_behavior_function_count", boundBehaviorFunctionCount },
            { "unbound_behavior_function_count",
              behaviorFunctionCount - boundBehaviorFunctionCount },
            { "behavior_action_transition_count", mConfig.CompilationUnit
                                                        ? mConfig.CompilationUnit
                                                              ->behaviorActionTransitionCount
                                                        : 0 },
            { "behavior_structure_field_count", mConfig.CompilationUnit
                                                     ? mConfig.CompilationUnit
                                                           ->behaviorStructureFieldCount
                                                           : 0 },
            { "behavior_indirect_root_count", mConfig.CompilationUnit
                                                    ? mConfig.CompilationUnit
                                                          ->behaviorIndirectRootCount
                                                    : 0 },
            { "bound_indirect_root_count", boundIndirectRootCount },
            { "behavior_native_abi_function_count", mConfig.CompilationUnit
                                                          ? mConfig.CompilationUnit
                                                                ->behaviorNativeAbiFunctionCount
                                                          : 0 },
            { "bound_native_abi_function_count", boundNativeAbiFunctionCount },
            { "behavior_consumer_root_count", mConfig.CompilationUnit
                                                    ? mConfig.CompilationUnit
                                                          ->behaviorConsumerRootCount
                                                    : 0 },
            { "behavior_consumer_function_count", mConfig.CompilationUnit
                                                        ? mConfig.CompilationUnit
                                                              ->behaviorConsumerFunctionCount
                                                        : 0 },
            { "behavior_consumer_call_edge_count", mConfig.CompilationUnit
                                                         ? mConfig.CompilationUnit
                                                               ->behaviorConsumerCallEdgeCount
                                                         : 0 },
            { "behavior_actor_local_consumer_function_count",
              mConfig.CompilationUnit
                  ? mConfig.CompilationUnit
                        ->behaviorActorLocalConsumerFunctionCount
                  : 0 },
            { "behavior_native_service_dependency_count",
              mConfig.CompilationUnit
                  ? mConfig.CompilationUnit
                        ->behaviorNativeServiceDependencyCount
                  : 0 },
            { "bound_consumer_function_count", boundConsumerFunctionCount },
            { "bound_lifecycle_consumer_count", boundLifecycleConsumerCount },
            { "native_abi_catalog", {
                { "available", mNativeAbiCatalog.has_value() },
                { "status", mNativeAbiCatalog
                                ? "reviewed_native_abi_catalog_mounted" : "" },
                { "source_snapshot_id", mNativeAbiCatalog
                                             ? mNativeAbiCatalog->sourceSnapshotId : "" },
                { "function_count", mNativeAbiCatalog
                                        ? mNativeAbiCatalog->functions.size() : 0 },
                { "subsystem_contract_count", mNativeAbiCatalog
                                                   ? mNativeAbiCatalog
                                                         ->subsystemContracts.size()
                                                   : 0 },
                { "complete_subsystem_contract_count", mNativeAbiCatalog
                                                            ? mNativeAbiCatalog
                                                                  ->completeSubsystemContractCount
                                                            : 0 },
                { "subsystem_contracts", std::move(nativeAbiSubsystemContracts) },
                { "profile_bound_function_count", mConfig.CompilationUnit
                                                        ? mConfig.CompilationUnit
                                                              ->nativeAbiCatalog
                                                              .profileBoundFunctionCount
                                                        : 0 },
                { "payload_sha256", mNativeAbiCatalog
                                         ? mNativeAbiCatalog->payloadSha256 : "" },
                { "enko_override_limb_draw", mNativeAbiCatalog &&
                                                    mNativeAbiCatalog->FindByName(
                                                        "EnKo_OverrideLimbDraw") != nullptr },
                { "light_context_insert_light", mNativeAbiCatalog &&
                                                     mNativeAbiCatalog->FindByName(
                                                         "LightContext_InsertLight") != nullptr },
                { "gameplay_camera_set_at_eye", mNativeAbiCatalog &&
                                                      mNativeAbiCatalog->FindByName(
                                                          "Gameplay_CameraSetAtEye") != nullptr },
                { "animation_on_frame_impl", mNativeAbiCatalog &&
                                                   mNativeAbiCatalog->FindByName(
                                                       "Animation_OnFrameImpl") != nullptr },
                { "waterbox_get_surface_impl", mNativeAbiCatalog &&
                                                     mNativeAbiCatalog->FindByName(
                                                         "WaterBox_GetSurfaceImpl") != nullptr },
                { "animation_change", mNativeAbiCatalog &&
                                             mNativeAbiCatalog->FindByName(
                                                 "Animation_Change") != nullptr },
                { "skel_anime_update", mNativeAbiCatalog &&
                                              mNativeAbiCatalog->FindByName(
                                                  "SkelAnime_Update") != nullptr },
                { "actor_shadow_draw", mNativeAbiCatalog &&
                                               mNativeAbiCatalog->FindByName(
                                                   "ActorShadow_Draw") != nullptr },
                { "effect_ss_spawn", mNativeAbiCatalog &&
                                             mNativeAbiCatalog->FindByName(
                                                 "EffectSs_Spawn") != nullptr },
                { "bgcheck_raycast_floor_impl", mNativeAbiCatalog &&
                                                         mNativeAbiCatalog->FindByName(
                                                             "BgCheck_RaycastFloorImpl") != nullptr },
                { "cutscene_process_commands", mNativeAbiCatalog &&
                                                    mNativeAbiCatalog->FindByName(
                                                        "Cutscene_ProcessCommands") != nullptr },
                { "gameplay_draw", mNativeAbiCatalog &&
                                           mNativeAbiCatalog->FindByName(
                                               "Gameplay_Draw") != nullptr },
                { "demo_kankyo_setup_type", mNativeAbiCatalog &&
                                                    mNativeAbiCatalog->FindByName(
                                                        "DemoKankyo_SetupType") != nullptr },
                { "player_post_limb_draw_gameplay", mNativeAbiCatalog &&
                                                            mNativeAbiCatalog->FindByName(
                                                                "oot3d_player_post_limb_draw_gameplay") != nullptr },
                { "effect_blure_draw", mNativeAbiCatalog &&
                                               mNativeAbiCatalog->FindByName(
                                                   "EffectBlure_Draw") != nullptr },
                { "effect_shield_particle_draw", mNativeAbiCatalog &&
                                                         mNativeAbiCatalog->FindByName(
                                                             "EffectShieldParticle_Draw") != nullptr },
            } },
            { "indirect_native_root_catalog", {
                { "available", mConfig.CompilationUnit &&
                                   mConfig.CompilationUnit->indirectRootCatalog.available },
                { "status", mConfig.CompilationUnit
                                ? mConfig.CompilationUnit->indirectRootCatalog.status : "" },
                { "root_count", mConfig.CompilationUnit
                                    ? mConfig.CompilationUnit->indirectRootCatalog.rootCount : 0 },
                { "actor_candidate_count", mConfig.CompilationUnit
                                               ? mConfig.CompilationUnit
                                                     ->indirectRootCatalog.actorCandidateCount
                                               : 0 },
                { "profile_bound_root_count", mConfig.CompilationUnit
                                                   ? mConfig.CompilationUnit
                                                         ->indirectRootCatalog
                                                         .profileBoundRootCount
                                                   : 0 },
                { "unbound_actor_candidate_count", mConfig.CompilationUnit
                                                       ? mConfig.CompilationUnit
                                                             ->indirectRootCatalog
                                                             .unboundActorCandidateCount
                                                       : 0 },
            } },
            { "behavior_profiles", std::move(behaviorProfiles) },
            { "deferred_instance_count", mDeferredCompilationInstances.size() },
            { "lifecycle", std::move(roomLifecycle) },
        } },
        { "source_room_actor_count", mSourceRoomActorCount },
        { "spawned_room_actor_count", mSpawnedRoomActorCount },
        { "live_count", mLiveCount },
        { "category_lists", std::move(categoryLists) },
        { "actors", std::move(actors) },
        { "unsupported_callbacks", std::move(unsupported) },
        { "native_closure_corpus",
          mNativeClosureCatalog
              ? mNativeClosureCatalog->Diagnostics()
              : nlohmann::json{{"available", false}} },
        { "profile_gaps", std::move(profileGaps) },
        { "object_banks", std::move(objectBanks) },
        { "object_bank_runtime", std::move(objectBankRuntime) },
        { "deferred_compilation_instances", std::move(deferredCompilationInstances) },
        { "native_render_provider_available", mRenderProvider != nullptr },
        { "archive_selected_visual_replacement_count",
          mArchiveSelectedVisualReplacementCount },
        { "native_random_state", mNativeRandomState },
        { "native_random_runtime", {
            { "status", mNativeRandomStatus },
            { "failure_count", mNativeRandomFailureCount },
            { "function_address", mEnKoBlinkProfile.RandomFunctionAddress },
            { "function_size", mEnKoBlinkProfile.RandomFunctionSize },
            { "function_sha256", mEnKoBlinkProfile.RandomFunctionSha256 },
            { "state_address", mEnKoBlinkProfile.RandomStateAddress },
        } },
        { "enko_blink_profile", {
            { "available", mEnKoBlinkProfile.Available },
            { "sequence", mEnKoBlinkProfile.Sequence },
            { "timer_base", mEnKoBlinkProfile.TimerBase },
            { "timer_range", mEnKoBlinkProfile.TimerRange },
            { "status", mEnKoBlinkProfile.Status },
        } },
        { "enriver_sound_runtime", {
            { "binding_status", mEnRiverSoundBindingStatus },
            { "active_instance_count", mEnRiverSoundStates.size() },
            { "draw_audio_service_available", mNativeAudioService != nullptr },
            { "archive_path", mConfig.NativeAudioArchivePath.string() },
            { "stream_archive_path", mConfig.NativeStreamArchivePath.string() },
            { "sound_id_table_address", mConfig.EnRiverSoundIdTableAddress },
            { "sound_id_table_source", mConfig.EnRiverSoundIdTableSource },
            { "resolved_sequence_count", audioStats.CachedSequenceCount },
            { "resolved_stream_count", audioStats.CachedStreamCount },
            { "active_sound_count", audioStats.ActiveSoundCount },
            { "active_sequence_player_count",
              audioStats.ActiveSequencePlayerCount },
            { "active_stream_player_count", audioStats.ActiveStreamPlayerCount },
            { "submitted_sound_request_count",
              audioStats.SubmittedSoundRequestCount },
            { "started_sound_count", audioStats.StartedSoundCount },
            { "refreshed_sound_count", audioStats.RefreshedSoundCount },
            { "behavior_entry_count", audioStats.BehaviorEntryCount },
            { "rejected_sound_count", audioStats.RejectedSoundCount },
            { "preempted_sound_count", audioStats.PreemptedSoundCount },
            { "active_mixed_voice_count", audioStats.ActiveMixedVoiceCount },
            { "maximum_mixed_voice_count", audioStats.MaximumMixedVoiceCount },
            { "mixed_voice_sample_count", audioStats.MixedVoiceSampleCount },
            { "cached_sequence_event_count", audioStats.CachedSequenceEventCount },
            { "cached_sequence_tick_zero_event_count",
              audioStats.CachedSequenceTickZeroEventCount },
            { "maximum_sequence_events_at_one_tick",
              audioStats.MaximumSequenceEventsAtOneTick },
            { "instances", std::move(enRiverSoundStates) },
        } },
        { "scene_audio", {
            { "available", mConfig.CompilationUnit &&
                               mConfig.CompilationUnit->sceneAudio.available },
            { "status", mSceneAudioStatus },
            { "bgm_started", mSceneBgmStarted },
            { "command_word", mConfig.CompilationUnit
                                    ? mConfig.CompilationUnit->sceneAudio.commandWord : 0 },
            { "sound_spec_id", mConfig.CompilationUnit
                                     ? mConfig.CompilationUnit->sceneAudio.soundSpecId : 0 },
            { "nature_ambience_id", mConfig.CompilationUnit
                                          ? mConfig.CompilationUnit->sceneAudio.natureAmbienceId
                                          : 0 },
            { "bgm_sound_id", mConfig.CompilationUnit
                                    ? mConfig.CompilationUnit->sceneAudio.bgmSoundId : 0 },
            { "source", "oot3d_native_zsi_scene_command_0x15" },
        } },
        { "enko_tracking_contract", {
            { "available", mEnKoTrackingContract.Available },
            { "status", mEnKoTrackingContract.Status },
            { "error", mEnKoTrackingContract.Error },
            { "preset_count", mEnKoTrackingContract.Presets.size() },
            { "mode_count", mEnKoTrackingContract.Modes.size() },
            { "initial_route_count", mEnKoTrackingContract.InitialRoutes.size() },
            { "service_address", mEnKoTrackingContract.ServiceAddress },
            { "service_size", mEnKoTrackingContract.ServiceSize },
            { "service_sha256", mEnKoTrackingContract.ServiceSha256 },
            { "selector_address", mEnKoTrackingContract.SelectorAddress },
            { "selector_size", mEnKoTrackingContract.SelectorSize },
            { "selector_sha256", mEnKoTrackingContract.SelectorSha256 },
            { "random_state_address",
              mEnKoTrackingContract.RandomStateAddress },
            { "limb_callback_address",
              mEnKoLimbCallbackContract.CallbackAddress },
            { "limb_callback_size",
              mEnKoLimbCallbackContract.CallbackSize },
            { "limb_callback_sha256",
              mEnKoLimbCallbackContract.CallbackSha256 },
            { "global_context_pointer_address",
              mEnKoTrackingContract.GlobalContextPointerAddress },
            { "global_context_update_rate",
              mEnKoTrackingContract.GlobalContextUpdateRate },
            { "smooth_scale", mEnKoTrackingContract.SmoothScale },
            { "smooth_max_step", mEnKoTrackingContract.SmoothMaxStep },
            { "smooth_min_step", mEnKoTrackingContract.SmoothMinStep },
            { "facing_threshold", mEnKoTrackingContract.FacingThreshold },
        } },
        { "enko_quest_state", mEnKoQuestState.has_value()
                                     ? nlohmann::json{
                                           { "available", mEnKoQuestState->Available },
                                           { "index", mEnKoQuestState->Index },
                                           { "semantic", mEnKoQuestState->Semantic },
                                           { "status", mEnKoQuestState->Status },
                                       }
                                     : nlohmann::json{ { "available", false } } },
        { "enko_visuals", std::move(enKoVisuals) },
        { "named_actor_definitions", std::move(namedActorDefinitions) },
        { "named_actor_visuals", std::move(namedActorVisuals) },
        { "environment_actor_visuals", std::move(environmentActorVisuals) },
        { "enholl_runtime", {
            { "contract_available", mEnHollContract.Available },
            { "contract_status", mEnHollContract.Status },
            { "supported_modes", mEnHollContract.SupportedModes },
            { "frame_context", {
                { "player_position_valid", mFrameContext.PlayerActorPositionValid },
                { "view_eye_valid", mFrameContext.ViewEyeValid },
                { "use_view_eye", mFrameContext.UseViewEyeForTransitionActors },
                { "special_bypass", mFrameContext.NativeSpecialTransitionBypass },
            } },
            { "instances", std::move(enHollStates) },
        } },
        { "room_material_animation_clock", {
            { "ticks_per_second", mConfig.MaterialAnimationTicksPerSecond },
            { "source", mConfig.MaterialAnimationClockSource },
            { "native_update_function", 0x00373BEC },
            { "frame_field_offset", 0x08 },
            { "play_speed_field_offset", 0x0C },
            { "loop_mode_field_offset", 0x10 },
        } },
        { "enkusa_behavior_coverage",
          mEnKusaRuntimeContract.is_object()
              ? mEnKusaRuntimeContract.value("behavior_coverage",
                                             nlohmann::json::object())
              : nlohmann::json::object() },
        { "objhana_behavior_coverage",
          mObjHanaRuntimeContract.is_object()
              ? mObjHanaRuntimeContract.value("behavior_coverage",
                                              nlohmann::json::object())
              : nlohmann::json::object() },
        { "n64_actor_fallback_used", false },
        { "runtime_code_bin_profile_reads", mPopulationFromCompilationUnit ? 0 : mSourceRoomActorCount },
    };
}

} // namespace Oot3dNativeGame
