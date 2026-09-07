#include "oot3d_native_room_render_runtime.h"

#include "oot3d_native_a32_execution.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

namespace Oot3dNativeGame {
namespace {

const nlohmann::json* FindSceneShardRecord(
    const nlohmann::json& manifest, std::string_view sceneStem) {
    const nlohmann::json* result = nullptr;
    if (!manifest.contains("records") || !manifest.at("records").is_array()) {
        return nullptr;
    }
    for (const auto& record : manifest.at("records")) {
        if (!record.is_object() || record.value("scene_stem", "") != sceneStem) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error("native scene shard contains a duplicate scene record");
        }
        result = &record;
    }
    return result;
}

const nlohmann::json* FindRoomMaterialAnimationRow(
    const nlohmann::json& sceneRecord, int32_t roomIndex) {
    if (!sceneRecord.contains("room_material_animations") ||
        !sceneRecord.at("room_material_animations").is_array()) {
        return nullptr;
    }
    const nlohmann::json* result = nullptr;
    for (const auto& row : sceneRecord.at("room_material_animations")) {
        if (!row.is_object() || row.value("room_index", -1) != roomIndex) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(
                "native scene shard contains duplicate room material animation rows");
        }
        result = &row;
    }
    return result;
}

const ThreeDsRecomp::Oot3d::SemanticGameplayFact* FindGameplayFact(
    std::span<const ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts,
    std::string_view key) {
    const auto found = std::find_if(
        facts.begin(), facts.end(),
        [key](const auto& fact) { return fact.Key == key; });
    return found == facts.end() ? nullptr : &*found;
}

const Oot3d::RoomCompilationSceneCallback* FindPrepareDrawCallback(
    const Oot3d::RoomCompilationSceneCallbackContract& callbacks) {
    const auto found = std::find_if(
        callbacks.callbacks.begin(), callbacks.callbacks.end(),
        [](const auto& callback) { return callback.role == "prepare_draw"; });
    return found == callbacks.callbacks.end() ? nullptr : &*found;
}

} // namespace

NativeRoomTevAlphaEvaluation EvaluateNativeRoomMaterialTevAlphaOperation(
    const Oot3d::RoomCompilationMaterialTevAlphaOperation& operation,
    int32_t setupIndex,
    std::span<const ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts,
    std::span<const uint16_t> drawParams, uint32_t& randomState,
    NativeA32ExecutionRuntime* nativeExecution) {
    NativeRoomTevAlphaEvaluation result;
    result.Selector = operation.selector.defaultValue;

    if (operation.nativeOperation != 2) {
        result.Status = "native_room_callback_operation_unsupported";
        result.Error = "only the decoded OOT3D TEV constant alpha operation is supported";
        return result;
    }

    if (setupIndex != operation.selector.excludedSetupIndex) {
        if (setupIndex == operation.selector.drawParamSetupIndex) {
            if (operation.selector.drawParamIndex >= drawParams.size()) {
                result.Status = "native_room_callback_draw_param_unavailable";
                result.Error = "the decoded room callback requires a native room draw parameter";
                return result;
            }
            result.Selector = drawParams[operation.selector.drawParamIndex];
        } else {
            bool progressionSetup = setupIndex < operation.selector.setupLessThan;
            if (!progressionSetup) {
                const auto* alternate = FindGameplayFact(
                    facts, operation.selector.alternateFact.key);
                if (alternate == nullptr) {
                    result.Status = "native_room_callback_gameplay_fact_unavailable";
                    result.Error = "missing gameplay fact: " +
                                   operation.selector.alternateFact.key;
                    return result;
                }
                progressionSetup =
                    alternate->Value == operation.selector.alternateFact.equals;
            }
            if (progressionSetup) {
                const auto* required = FindGameplayFact(
                    facts, operation.selector.requiredFact.key);
                if (required == nullptr) {
                    result.Status = "native_room_callback_gameplay_fact_unavailable";
                    result.Error = "missing gameplay fact: " +
                                   operation.selector.requiredFact.key;
                    return result;
                }
                if (required->Value == operation.selector.requiredFact.equals) {
                    result.Selector = operation.selector.progressionValue;
                }
            }
        }
    }

    const float alphaBase = std::bit_cast<float>(operation.alphaBaseF32Bits);
    const float selectorScale =
        std::bit_cast<float>(operation.alphaSelectorScaleF32Bits);
    const float randomScale = std::bit_cast<float>(operation.random.scaleF32Bits);
    const float lowCompare =
        std::bit_cast<float>(operation.random.lowCompareF32Bits);
    if (!std::isfinite(alphaBase) || !std::isfinite(selectorScale) ||
        !std::isfinite(randomScale) || !std::isfinite(lowCompare) ||
        operation.random.range <= 0 || operation.random.multiplier == 0) {
        result.Status = "native_room_callback_numeric_contract_invalid";
        result.Error = "decoded room callback numeric inputs are invalid";
        return result;
    }

    if (nativeExecution == nullptr || !nativeExecution->Available()) {
        result.Status = "native_room_callback_a32_runtime_unavailable";
        result.Error = "the decoded room callback requires the native A32 runtime";
        return result;
    }
    if (!nativeExecution->Write32(operation.random.stateAddress, randomState)) {
        result.Status = "native_room_callback_a32_random_state_unmapped";
        result.Error = "the decoded native RNG state address is not mapped";
        return result;
    }
    oot3d::recomp::a32::GuestState guestState;
    guestState.r[0] = static_cast<uint32_t>(operation.random.offset);
    guestState.r[1] = static_cast<uint32_t>(operation.random.range);
    const auto call =
        nativeExecution->Call(operation.random.functionAddress, guestState);
    uint32_t nextRandomState = 0;
    if (!call.Completed ||
        !nativeExecution->Read32(operation.random.stateAddress, &nextRandomState)) {
        result.Status = "native_room_callback_a32_random_call_failed";
        result.Error = call.Error.empty()
                           ? "the native RNG state could not be read after execution"
                           : call.Error;
        return result;
    }
    result.RandomValue = static_cast<int32_t>(guestState.r[0]);
    const int64_t randomEnd = static_cast<int64_t>(operation.random.offset) +
                              operation.random.range;
    if (result.RandomValue < operation.random.offset ||
        static_cast<int64_t>(result.RandomValue) >= randomEnd) {
        result.Status = "native_room_callback_a32_random_result_invalid";
        result.Error = "the native RNG result is outside its decoded range";
        return result;
    }
    randomState = nextRandomState;

    const float randomTerm =
        static_cast<float>(result.RandomValue) * randomScale;
    result.Alpha = alphaBase - static_cast<float>(result.Selector) * selectorScale;
    const uint32_t alphaBits = std::bit_cast<uint32_t>(result.Alpha);
    if (alphaBits + operation.random.middleTestAddU32 <
        operation.random.middleTestLessThanU32) {
        result.Alpha -= randomTerm;
    } else if (result.Alpha < lowCompare) {
        result.Alpha -= result.Alpha * randomTerm;
    }
    if (!std::isfinite(result.Alpha)) {
        result.Status = "native_room_callback_alpha_non_finite";
        result.Error = "decoded room callback produced a non-finite alpha";
        return result;
    }
    const float clampedAlpha = std::clamp(result.Alpha, 0.0f, 1.0f);
    result.AlphaU8 = static_cast<uint8_t>(
        std::lround(clampedAlpha * 255.0f));
    result.Evaluated = true;
    result.Status = "native_room_callback_material_tev_alpha_evaluated";
    return result;
}

NativeRoomRenderRuntime::NativeRoomRenderRuntime(
    const ThreeDsRecomp::Oot3d::AssetCatalog& catalog,
    ThreeDsRecomp::Oot3d::NativeSourceProvider& sources, int32_t sceneId,
    int32_t setupIndex, NativeA32ExecutionRuntime* nativeExecution)
    : mSceneId(sceneId), mSetupIndex(setupIndex), mSources(sources),
      mNativeExecution(nativeExecution),
      mProvider(catalog, sources) {
}

void NativeRoomRenderRuntime::CaptureInitialRoom(
    int32_t roomIndex,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    mResident.clear();
    mCleanupQueue.clear();
    mPrepared.reset();
    mVisibleRoomOrder.clear();
    mPrepareCount = 0;
    mInstallCount = 0;
    mCleanupCount = 0;
    mCallbackInvocationCount = 0;
    mCallbackEvaluationCount = 0;
    mCallbackAppliedOperationCount = 0;
    mCallbackAppliedBatchCount = 0;
    mLastCallbackAddress = 0;
    mLastCallbackSelector = 0;
    mLastCallbackRandomValue = 0;
    mLastCallbackAlpha = 0.0f;
    mLastCallbackAlphaU8 = 0;
    mLastCallbackAppliedOperationCount = 0;
    mLastCallbackAppliedBatchCount = 0;
    mCallbackStatus = "native_room_callback_not_evaluated";
    mCallbackError.clear();
    mError.clear();

    ThreeDsRecomp::Oot3d::MarkOot3dNativeRenderModelVertexDataCacheable(renderScene.Room);
    for (auto& model : renderScene.AdditionalRoomModels) {
        ThreeDsRecomp::Oot3d::MarkOot3dNativeRenderModelVertexDataCacheable(model);
    }

    RoomResource initial;
    initial.RoomIndex = roomIndex;
    initial.Source = mProvider.Resolve(mSceneId, mSetupIndex, roomIndex);
    if (initial.Source == nullptr || !initial.Source->Ready()) {
        throw std::runtime_error(
            "initial native room cannot be resolved from its mounted scene shard");
    }
    initial.Models.push_back(renderScene.Room);
    initial.Models.insert(initial.Models.end(), renderScene.AdditionalRoomModels.begin(),
                          renderScene.AdditionalRoomModels.end());
    if (initial.Models.size() != initial.Source->RenderModels.size()) {
        throw std::runtime_error(
            "initial native room model count differs from its mounted ZSI source");
    }
    if (!LoadRoomMaterialAnimations(initial) ||
        !ApplyRoomMaterialAnimations(initial, initial.Models)) {
        throw std::runtime_error(initial.Error);
    }
    for (auto& model : initial.Models) {
        ThreeDsRecomp::Oot3d::MarkOot3dNativeRenderModelVertexDataCacheable(model);
    }
    initial.Status = "initial_native_scene_render_models_captured";
    mResident.emplace(roomIndex, std::move(initial));
    mVisibleRoomOrder.push_back(roomIndex);
    mStatus = "native_room_render_initial_residency_ready";
}

bool NativeRoomRenderRuntime::SynchronizeVisibleModels(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    if (mVisibleRoomOrder.empty()) {
        return true;
    }
    std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel> visible;
    visible.reserve(1 + renderScene.AdditionalRoomModels.size());
    visible.push_back(renderScene.Room);
    visible.insert(visible.end(), renderScene.AdditionalRoomModels.begin(),
                   renderScene.AdditionalRoomModels.end());

    size_t cursor = 0;
    for (const int32_t roomIndex : mVisibleRoomOrder) {
        const auto resident = mResident.find(roomIndex);
        if (resident == mResident.end() ||
            cursor + resident->second.Models.size() > visible.size()) {
            mStatus = "native_room_render_visible_ownership_mismatch";
            mError = "render-scene room model ownership no longer matches resident resources";
            return false;
        }
        resident->second.Models.assign(
            visible.begin() + static_cast<std::ptrdiff_t>(cursor),
            visible.begin() + static_cast<std::ptrdiff_t>(
                                  cursor + resident->second.Models.size()));
        cursor += resident->second.Models.size();
    }
    if (cursor != visible.size()) {
        mStatus = "native_room_render_visible_ownership_mismatch";
        mError = "render-scene contains unowned room models";
        return false;
    }
    return true;
}

bool NativeRoomRenderRuntime::LoadRoomMaterialAnimations(RoomResource& resource) {
    if (resource.Source == nullptr || resource.Source->CatalogRecord == nullptr) {
        resource.MaterialAnimationStatus = "native_room_catalog_record_unavailable";
        resource.Error = "room material animations require a cataloged native room source";
        return false;
    }
    const auto& catalog = *resource.Source->CatalogRecord;
    if (catalog.SceneShardManifestResource.empty() || catalog.SceneStem.empty()) {
        resource.MaterialAnimationStatus = "native_room_scene_shard_manifest_unavailable";
        resource.Error = "room catalog record does not identify its native scene shard";
        return false;
    }
    resource.MaterialAnimationManifestResource = catalog.SceneShardManifestResource;
    const auto manifestSource = mSources.Load(resource.MaterialAnimationManifestResource);
    if (manifestSource == nullptr) {
        resource.MaterialAnimationStatus = "native_room_scene_shard_manifest_not_mounted";
        resource.Error = "native scene shard manifest resource is not mounted";
        return false;
    }

    const auto manifest = nlohmann::json::parse(
        manifestSource->Bytes->begin(), manifestSource->Bytes->end());
    if (manifest.value("format", "") != "oot3d_native_scene_shard_v1" ||
        manifest.value("status", "") != "complete") {
        resource.MaterialAnimationStatus = "native_room_scene_shard_manifest_invalid";
        resource.Error = "mounted scene shard manifest has an unsupported contract";
        return false;
    }
    const auto sceneRecord = FindSceneShardRecord(manifest, catalog.SceneStem);
    if (sceneRecord == nullptr) {
        resource.MaterialAnimationStatus = "native_room_scene_shard_record_missing";
        resource.Error = "mounted scene shard does not contain the room's scene record";
        return false;
    }
    const auto row = FindRoomMaterialAnimationRow(*sceneRecord, resource.RoomIndex);
    if (row == nullptr) {
        resource.MaterialAnimationStatus =
            "native_room_namespace_contains_no_material_animations";
        return true;
    }
    if (!row->contains("archive_resource") ||
        !row->at("archive_resource").is_string() ||
        !row->contains("archive_byte_length") ||
        !row->at("archive_byte_length").is_number_unsigned() ||
        !row->contains("members") || !row->at("members").is_array() ||
        row->at("members").empty()) {
        resource.MaterialAnimationStatus = "native_room_material_animation_row_invalid";
        resource.Error = "scene shard room material animation row is incomplete";
        return false;
    }

    resource.MaterialAnimationArchiveResource =
        row->at("archive_resource").get<std::string>();
    const auto archiveSource = mSources.Load(resource.MaterialAnimationArchiveResource);
    if (archiveSource == nullptr ||
        archiveSource->Bytes->size() !=
            row->at("archive_byte_length").get<uint64_t>()) {
        resource.MaterialAnimationStatus = "native_room_material_animation_archive_invalid";
        resource.Error = "scene sidecar ZAR is absent or differs from its shard manifest";
        return false;
    }
    const auto archive = ThreeDsRecomp::Oot3d::ParseZarArchiveBytes(
        *archiveSource->Bytes, resource.MaterialAnimationArchiveResource);

    for (const auto& member : row->at("members")) {
        if (!member.is_object() || !member.contains("index") ||
            !member.at("index").is_number_unsigned() ||
            !member.contains("type_local_index") ||
            !member.at("type_local_index").is_number_unsigned() ||
            !member.contains("name") || !member.at("name").is_string() ||
            !member.contains("size") || !member.at("size").is_number_unsigned()) {
            resource.MaterialAnimationStatus =
                "native_room_material_animation_member_invalid";
            resource.Error = "scene sidecar CMAB member contract is incomplete";
            return false;
        }
        const uint32_t memberIndex = member.at("index").get<uint32_t>();
        const auto found = std::find_if(
            archive.Files.begin(), archive.Files.end(),
            [memberIndex](const auto& file) { return file.Index == memberIndex; });
        if (found == archive.Files.end() ||
            found->Name != member.at("name").get<std::string>() ||
            found->TypeName != "cmab" ||
            found->TypeLocalIndex !=
                member.at("type_local_index").get<uint32_t>() ||
            found->Size != member.at("size").get<uint32_t>() ||
            static_cast<uint64_t>(found->Offset) + found->Size >
                archiveSource->Bytes->size()) {
            resource.MaterialAnimationStatus =
                "native_room_material_animation_member_mismatch";
            resource.Error = "scene sidecar CMAB differs from its shard manifest";
            return false;
        }
        const std::span<const uint8_t> bytes(
            archiveSource->Bytes->data() + found->Offset, found->Size);
        resource.MaterialAnimationNames.push_back(found->Name);
        resource.MaterialAnimations.push_back(
            ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
                bytes, resource.MaterialAnimationArchiveResource + "!" + found->Name));
    }
    resource.MaterialAnimationStatus =
        "native_room_material_animations_loaded_from_scene_shard";
    return true;
}

bool NativeRoomRenderRuntime::ApplyRoomMaterialAnimations(
    RoomResource& resource,
    std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel>& models) {
    resource.MaterialAnimationAppliedBatchCount = 0;
    if (resource.MaterialAnimations.empty()) {
        return true;
    }
    if (resource.Source == nullptr || resource.Source->EmbeddedCmbs.size() != 1 ||
        models.size() != 1) {
        resource.MaterialAnimationStatus =
            "native_multi_cmb_room_material_binding_not_resolved";
        resource.Error =
            "room CMAB binding requires one unambiguous embedded CMB target";
        return false;
    }
    resource.MaterialAnimationAppliedBatchCount =
        ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
            models.front(), resource.Source->EmbeddedCmbs.front().Model,
            resource.MaterialAnimations, resource.MaterialAnimationFrame);
    return true;
}

bool NativeRoomRenderRuntime::PrepareRoomRequest(
    int32_t roomIndex,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    if (mPrepared.has_value()) {
        mStatus = "native_room_render_request_already_prepared";
        return false;
    }
    if (!SynchronizeVisibleModels(renderScene)) {
        return false;
    }
    const auto source = mProvider.Resolve(mSceneId, mSetupIndex, roomIndex);
    if (source == nullptr || !source->Ready()) {
        mStatus = source == nullptr ? "native_room_render_source_missing" : source->Status;
        mError = source == nullptr ? "room provider returned no source" : source->Error;
        return false;
    }

    RoomResource prepared;
    prepared.RoomIndex = roomIndex;
    prepared.Source = source;
    prepared.Models = source->RenderModels;
    for (auto& model : prepared.Models) {
        ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(
            renderScene.PicaLighting, model);
        ThreeDsRecomp::Oot3d::MarkOot3dNativeRenderModelVertexDataCacheable(model);
    }
    if (!LoadRoomMaterialAnimations(prepared) ||
        !ApplyRoomMaterialAnimations(prepared, prepared.Models)) {
        mStatus = prepared.MaterialAnimationStatus;
        mError = prepared.Error;
        return false;
    }
    prepared.Status = "native_zsi_cmb_render_models_prepared";
    mPrepared = std::move(prepared);
    ++mPrepareCount;
    mStatus = "native_room_render_request_prepared";
    mError.clear();
    return true;
}

bool NativeRoomRenderRuntime::CommitPreparedRoom(int32_t roomIndex,
                                                  int32_t droppedPreviousRoom) {
    if (!mPrepared.has_value() || mPrepared->RoomIndex != roomIndex) {
        mStatus = "native_room_render_prepared_request_mismatch";
        return false;
    }
    if (droppedPreviousRoom >= 0) {
        const auto dropped = mResident.find(droppedPreviousRoom);
        if (dropped != mResident.end()) {
            mCleanupQueue.push_back(std::move(dropped->second));
            mResident.erase(dropped);
        }
    }
    if (mResident.contains(roomIndex)) {
        mStatus = "native_room_render_target_still_resident";
        mError = "target room was not the previous resource selected for native cleanup";
        return false;
    }
    mResident.emplace(roomIndex, std::move(*mPrepared));
    mPrepared.reset();
    mVisibleRoomOrder.clear();
    ++mInstallCount;
    mStatus = "native_room_render_models_installed";
    mError.clear();
    return true;
}

void NativeRoomRenderRuntime::DiscardPreparedRoom() {
    mPrepared.reset();
}

void NativeRoomRenderRuntime::ReleaseCompletedCleanup() {
    mCleanupCount += mCleanupQueue.size();
    mCleanupQueue.clear();
}

uint32_t NativeRoomRenderRuntime::AdvanceMaterialAnimations(
    double deltaSeconds, double ticksPerSecond,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
        !std::isfinite(ticksPerSecond) || ticksPerSecond <= 0.0) {
        throw std::invalid_argument("native room material animation clock is invalid");
    }

    mPendingNativeRenderTicks += deltaSeconds * ticksPerSecond;
    const auto nativeTickCount = static_cast<uint32_t>(
        std::floor(mPendingNativeRenderTicks + 1.0e-9));
    if (nativeTickCount == 0) {
        return 0;
    }
    mPendingNativeRenderTicks -= static_cast<double>(nativeTickCount);

    std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel*> visible;
    visible.reserve(1 + renderScene.AdditionalRoomModels.size());
    visible.push_back(&renderScene.Room);
    for (auto& model : renderScene.AdditionalRoomModels) {
        visible.push_back(&model);
    }

    size_t cursor = 0;
    uint64_t animationCount = 0;
    uint64_t appliedBatchCount = 0;
    float currentRoomFrame = 0.0f;
    for (const int32_t roomIndex : mVisibleRoomOrder) {
        const auto resident = mResident.find(roomIndex);
        if (resident == mResident.end() ||
            cursor + resident->second.Models.size() > visible.size()) {
            throw std::runtime_error(
                "native room material animation ownership differs from visible models");
        }
        auto& resource = resident->second;
        if (!resource.MaterialAnimations.empty()) {
            resource.MaterialAnimationFrame +=
                static_cast<float>(nativeTickCount);
            resource.MaterialAnimationUpdateCount += nativeTickCount;
            if (resource.Models.size() != 1 || resource.Source == nullptr ||
                resource.Source->EmbeddedCmbs.size() != 1) {
                throw std::runtime_error(
                    "native multi-CMB room material binding is unresolved");
            }
            resource.MaterialAnimationAppliedBatchCount =
                ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
                    *visible[cursor], resource.Source->EmbeddedCmbs.front().Model,
                    resource.MaterialAnimations,
                    resource.MaterialAnimationFrame);
            const bool diffuseLightingInputAnimated = std::any_of(
                resource.MaterialAnimations.begin(), resource.MaterialAnimations.end(),
                [](const auto& animation) {
                    return std::any_of(animation.MmadRecords.begin(), animation.MmadRecords.end(),
                                       [](const auto& record) {
                                           return record.HeaderDecoded &&
                                                  record.NativeTypeFactorySupported &&
                                                  record.NativeType == 3;
                                       });
                });
            if (diffuseLightingInputAnimated) {
                ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(
                    renderScene.PicaLighting, *visible[cursor]);
            }
        } else {
            resource.MaterialAnimationAppliedBatchCount = 0;
        }
        if (roomIndex == mVisibleRoomOrder.front()) {
            currentRoomFrame = resource.MaterialAnimationFrame;
        }
        animationCount += resource.MaterialAnimations.size();
        appliedBatchCount += resource.MaterialAnimationAppliedBatchCount;
        cursor += resource.Models.size();
    }
    if (cursor != visible.size() || animationCount > UINT32_MAX ||
        appliedBatchCount > UINT32_MAX) {
        throw std::runtime_error(
            "native room material animation diagnostics exceed render ownership");
    }
    renderScene.NativeRoomMaterialAnimationCount =
        static_cast<uint32_t>(animationCount);
    renderScene.NativeRoomMaterialAnimationAppliedBatchCount =
        static_cast<uint32_t>(appliedBatchCount);
    renderScene.NativeRoomMaterialAnimationFrame = currentRoomFrame;
    return nativeTickCount;
}

void NativeRoomRenderRuntime::ApplySceneCallbacks(
    const Oot3d::RoomCompilationSceneCallbackContract& callbacks,
    std::span<const ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts,
    std::span<const uint16_t> drawParams, uint32_t& randomState,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    ++mCallbackInvocationCount;
    mLastCallbackAppliedOperationCount = 0;
    mLastCallbackAppliedBatchCount = 0;
    mCallbackError.clear();
    if (!callbacks.available) {
        mCallbackStatus = "native_room_callback_contract_unavailable";
        return;
    }
    const auto* prepareDraw = FindPrepareDrawCallback(callbacks);
    if (prepareDraw == nullptr) {
        mCallbackStatus = "native_room_prepare_draw_callback_unavailable";
        return;
    }
    mLastCallbackAddress = prepareDraw->runtimeAddress;
    if (!callbacks.executable) {
        mCallbackStatus = callbacks.status;
        return;
    }

    std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel*> visible;
    visible.reserve(1 + renderScene.AdditionalRoomModels.size());
    visible.push_back(&renderScene.Room);
    for (auto& model : renderScene.AdditionalRoomModels) {
        visible.push_back(&model);
    }

    size_t roomCursor = 0;
    for (const int32_t roomIndex : mVisibleRoomOrder) {
        const auto resident = mResident.find(roomIndex);
        if (resident == mResident.end() ||
            roomCursor + resident->second.Models.size() > visible.size()) {
            throw std::runtime_error(
                "native room callback ownership differs from visible models");
        }
        for (const auto& operation : callbacks.materialTevAlphaOperations) {
            if (operation.roomIndex != roomIndex) {
                continue;
            }
            if (operation.resourceIndex >= resident->second.Models.size()) {
                throw std::runtime_error(
                    "native room callback resource index exceeds the room model count");
            }
            auto& model = *visible[roomCursor + operation.resourceIndex];
            const auto evaluation = EvaluateNativeRoomMaterialTevAlphaOperation(
                operation, mSetupIndex, facts, drawParams, randomState,
                mNativeExecution);
            mLastCallbackSelector = evaluation.Selector;
            mLastCallbackRandomValue = evaluation.RandomValue;
            mLastCallbackAlpha = evaluation.Alpha;
            mLastCallbackAlphaU8 = evaluation.AlphaU8;
            mCallbackStatus = evaluation.Status;
            mCallbackError = evaluation.Error;
            if (!evaluation.Evaluated) {
                continue;
            }
            ++mCallbackEvaluationCount;
            size_t appliedBatchCount = 0;
            for (const int32_t materialIndex : operation.materialIndices) {
                appliedBatchCount +=
                    ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialAlphaOverride(
                        model, materialIndex, operation.constantIndex,
                        evaluation.AlphaU8,
                        "oot3d_code_bin_room_prepare_draw_material_tev_alpha_op2");
            }
            model.Diagnostics["native_room_prepare_draw"] = {
                {"callback_address", prepareDraw->runtimeAddress},
                {"callback_name", prepareDraw->name},
                {"native_operation", operation.nativeOperation},
                {"material_indices", operation.materialIndices},
                {"constant_index", operation.constantIndex},
                {"selector", evaluation.Selector},
                {"random_value", evaluation.RandomValue},
                {"alpha", evaluation.Alpha},
                {"alpha_u8", evaluation.AlphaU8},
                {"applied_batch_count", appliedBatchCount},
                {"source_registry", callbacks.sourceRegistry},
                {"function_sha256", callbacks.functionSha256},
            };
            ++mCallbackAppliedOperationCount;
            ++mLastCallbackAppliedOperationCount;
            mCallbackAppliedBatchCount += appliedBatchCount;
            mLastCallbackAppliedBatchCount += appliedBatchCount;
            mCallbackStatus = appliedBatchCount == 0
                                  ? "native_room_callback_target_material_not_present"
                                  : "native_room_callback_material_tev_alpha_applied";
        }
        roomCursor += resident->second.Models.size();
    }
    if (roomCursor != visible.size()) {
        throw std::runtime_error(
            "native room callback did not account for every visible room model");
    }
    if (mLastCallbackAppliedOperationCount == 0 && mCallbackError.empty()) {
        mCallbackStatus = "native_room_callback_target_room_not_visible";
    }
}

void NativeRoomRenderRuntime::RebuildBounds(
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    renderScene.Bounds = {};
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
        renderScene.Bounds,
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderScene.Room));
    for (const auto& model : renderScene.AdditionalRoomModels) {
        ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
            renderScene.Bounds,
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model));
    }
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds,
                                                renderScene.Link.Bounds);
    for (const auto& model : renderScene.ActorVisuals) {
        ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
            renderScene.Bounds,
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model));
    }
}

void NativeRoomRenderRuntime::ComposeResidentRooms(
    const NativeRoomRuntime& roomRuntime,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    if (!SynchronizeVisibleModels(renderScene)) {
        throw std::runtime_error(mError);
    }

    std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel> visible;
    mVisibleRoomOrder.clear();
    const int32_t candidates[] = {roomRuntime.CurrentRoom(),
                                  roomRuntime.PreviousRoom()};
    for (const int32_t roomIndex : candidates) {
        if (roomIndex < 0 || !roomRuntime.IsRoomLoaded(roomIndex)) {
            continue;
        }
        const auto resident = mResident.find(roomIndex);
        if (resident == mResident.end() || resident->second.Models.empty()) {
            mStatus = "native_room_render_resident_resource_missing";
            mError = "room lifecycle references an unavailable render resource";
            throw std::runtime_error(mError);
        }
        mVisibleRoomOrder.push_back(roomIndex);
        visible.insert(visible.end(), resident->second.Models.begin(),
                       resident->second.Models.end());
    }
    if (visible.empty()) {
        mStatus = "native_room_render_no_loaded_room";
        mError = "room lifecycle has no loaded render resource";
        throw std::runtime_error(mError);
    }

    renderScene.Room = visible.front();
    renderScene.AdditionalRoomModels.assign(visible.begin() + 1, visible.end());
    RebuildBounds(renderScene);
    mStatus = "native_room_render_residency_composed";
    mError.clear();
}

nlohmann::json NativeRoomRenderRuntime::Diagnostics() const {
    nlohmann::json resident = nlohmann::json::array();
    for (const auto& [roomIndex, resource] : mResident) {
        resident.push_back({
            {"room_index", roomIndex},
            {"model_count", resource.Models.size()},
            {"native_source", resource.Source != nullptr},
            {"asset_id", resource.Source != nullptr &&
                                     resource.Source->CatalogRecord != nullptr
                                 ? resource.Source->CatalogRecord->AssetId
                                 : ""},
            {"status", resource.Status},
            {"error", resource.Error},
            {"material_animation_manifest_resource",
             resource.MaterialAnimationManifestResource},
            {"material_animation_archive_resource",
             resource.MaterialAnimationArchiveResource},
            {"material_animation_names", resource.MaterialAnimationNames},
            {"material_animation_count", resource.MaterialAnimations.size()},
            {"material_animation_frame", resource.MaterialAnimationFrame},
            {"material_animation_update_count",
             resource.MaterialAnimationUpdateCount},
            {"material_animation_applied_batch_count",
             resource.MaterialAnimationAppliedBatchCount},
            {"material_animation_status", resource.MaterialAnimationStatus},
        });
    }
    nlohmann::json cleanup = nlohmann::json::array();
    for (const auto& resource : mCleanupQueue) {
        cleanup.push_back({
            {"room_index", resource.RoomIndex},
            {"model_count", resource.Models.size()},
            {"status", resource.Status},
        });
    }
    return {
        {"available", true},
        {"scene_id", mSceneId},
        {"setup_index", mSetupIndex},
        {"status", mStatus},
        {"error", mError},
        {"visible_room_order", mVisibleRoomOrder},
        {"resident", std::move(resident)},
        {"cleanup_queue", std::move(cleanup)},
        {"prepared_room", mPrepared.has_value() ? mPrepared->RoomIndex : -1},
        {"prepare_count", mPrepareCount},
        {"install_count", mInstallCount},
        {"cleanup_count", mCleanupCount},
        {"material_animation_status",
         "native_scene_shard_room_cmab_binding_active"},
        {"scene_callback", {
            {"status", mCallbackStatus},
            {"error", mCallbackError},
            {"invocation_count", mCallbackInvocationCount},
            {"evaluation_count", mCallbackEvaluationCount},
            {"applied_operation_count", mCallbackAppliedOperationCount},
            {"applied_batch_count", mCallbackAppliedBatchCount},
            {"last_callback_address", mLastCallbackAddress},
            {"last_selector", mLastCallbackSelector},
            {"last_random_value", mLastCallbackRandomValue},
            {"last_alpha", mLastCallbackAlpha},
            {"last_alpha_u8", mLastCallbackAlphaU8},
            {"last_applied_operation_count",
             mLastCallbackAppliedOperationCount},
            {"last_applied_batch_count", mLastCallbackAppliedBatchCount},
            {"a32_runtime_available",
             mNativeExecution != nullptr && mNativeExecution->Available()},
            {"a32_successful_call_count",
             mNativeExecution != nullptr
                 ? mNativeExecution->SuccessfulCallCount()
                 : 0},
            {"a32_failed_call_count",
             mNativeExecution != nullptr ? mNativeExecution->FailedCallCount()
                                         : 0},
        }},
    };
}

} // namespace Oot3dNativeGame
