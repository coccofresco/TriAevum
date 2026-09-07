#include "three_ds_recomp/oot3d/Oot3dNativeSceneEnvironmentProvider.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>

#include <nlohmann/json.hpp>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr std::string_view kLightingSemanticsResource =
    "oot3d/environment/oot3d_pica_lighting_semantics.json";
constexpr std::string_view kLightTransitionTableResource =
    "oot3d/environment/light_settings_transition_table.bin";
constexpr std::string_view kLightTransitionFallbackResource =
    "oot3d/environment/light_settings_transition_fallback.bin";
constexpr std::string_view kFogDefaultSourceResource =
    "oot3d/environment/fog_default_source.bin";
constexpr std::string_view kFogViewProjectionDefaultsResource =
    "oot3d/environment/fog_view_projection_defaults.bin";
constexpr std::string_view kFogSceneProjectionFarResource =
    "oot3d/environment/fog_scene_projection_far.bin";
constexpr std::string_view kNativeKankyoManifestResource =
    "oot3d/catalog/shards/oot3d-kankyo-native.json";
constexpr std::string_view kKankyoSkyboxRecordsResource =
    "oot3d/environment/kankyo_skybox_records.bin";
constexpr std::string_view kKankyoScheduleTableResource =
    "oot3d/environment/kankyo_schedule_table.bin";
constexpr std::string_view kKankyoDrawScaleResource =
    "oot3d/environment/kankyo_draw_scale.bin";

bool DecodeHexRecord(std::string_view hex, std::array<uint8_t, 0x1C>& output) {
    if (hex.size() != output.size() * 2) {
        return false;
    }
    for (size_t index = 0; index < output.size(); ++index) {
        unsigned int value = 0;
        const char* begin = hex.data() + index * 2;
        const auto result = std::from_chars(begin, begin + 2, value, 16);
        if (result.ec != std::errc() || result.ptr != begin + 2) {
            return false;
        }
        output[index] = static_cast<uint8_t>(value);
    }
    return true;
}

} // namespace

bool NativeSceneEnvironmentSetup::Ready() const {
    return Status == "ready" && Scene != nullptr;
}

bool NativeKankyoArchiveSource::Ready() const {
    return Status == "ready" && Source != nullptr && !Models.empty();
}

NativeSceneEnvironmentProvider::NativeSceneEnvironmentProvider(const AssetCatalog& catalog,
                                                               NativeSourceProvider& sources)
    : mCatalog(catalog), mSources(sources) {
    mLightingSemanticsResource = kLightingSemanticsResource;
    try {
        const auto source = mSources.Load(mLightingSemanticsResource);
        if (source == nullptr) {
            mLightingSemanticsError = "native PICA lighting semantics resource could not be loaded";
            return;
        }
        auto semantics = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
        if (semantics.value("format", "") != "oot3d_pica_lighting_semantics_v1") {
            mLightingSemanticsError = "native PICA lighting semantics format is unsupported";
            return;
        }
        mLightingSemanticPlan = CompileOot3dNativePicaLightingSemanticPlan(semantics);
        if (!mLightingSemanticPlan.Available) {
            mLightingSemanticsError = "native PICA lighting semantics contract is incomplete";
            return;
        }
        mLightingSemantics = std::move(semantics);
    } catch (const std::exception& error) {
        mLightingSemanticsError = error.what();
    }
    try {
        const auto source = mSources.Load(std::string(kLightTransitionTableResource));
        if (source == nullptr) {
            mRuntimeTransitionTableError = "native light transition table resource could not be loaded";
            return;
        }
        const auto& layout = BuildNativeKankyoRuntimeBridgeContract().ZsiLightSettingsRecord;
        const auto modes = ParseNativeLightSettingsTransitionTableBytes(
            std::span<const uint8_t>(*source->Bytes), layout.RuntimeTransitionModeCount,
            layout.RuntimeTransitionEntryCount, layout.RuntimeTransitionModeStrideBytes,
            layout.RuntimeTransitionEntrySizeBytes);
        for (const auto& sourceMode : modes) {
            Oot3dNativeDemoLightSettingsTransitionMode mode;
            mode.ModeIndex = static_cast<int>(sourceMode.ModeIndex);
            for (size_t entryIndex = 0; entryIndex < sourceMode.Entries.size(); ++entryIndex) {
                const auto& entry = sourceMode.Entries[entryIndex];
                mode.Entries.push_back({ static_cast<int>(entryIndex), entry.StartAngle, entry.EndAngle,
                                         entry.FromLightSettingIndex, entry.ToLightSettingIndex });
            }
            mRuntimeLightingContract.NativeRuntimeTransitionModes.push_back(std::move(mode));
        }
        mRuntimeLightingContract.NativeRuntimeTransitionTableAvailable =
            !mRuntimeLightingContract.NativeRuntimeTransitionModes.empty();
        mRuntimeLightingContract.NativeRuntimeTransitionTableDecodedFromCodeBin =
            mRuntimeLightingContract.NativeRuntimeTransitionTableAvailable;
        mRuntimeLightingContract.NativeRuntimeTransitionTableSourceKind =
            "oot3d_packaged_code_bin_light_settings_transition_table";

        const auto fallback = mSources.Load(std::string(kLightTransitionFallbackResource));
        if (fallback == nullptr) {
            throw std::runtime_error("native light transition fallback resource could not be loaded");
        }
        const auto& bytes = *fallback->Bytes;
        const auto canRead = [&bytes](size_t offset, size_t size) {
            return offset <= bytes.size() && size <= bytes.size() - offset;
        };
        if (!canRead(layout.RuntimeTransitionGlobalFallbackModeOffset, 1) ||
            !canRead(layout.RuntimeTransitionGlobalFallbackFromIndexOffset, 1) ||
            !canRead(layout.RuntimeTransitionGlobalFallbackToIndexOffset, 1) ||
            !canRead(layout.RuntimeTransitionGlobalFallbackModeWeightFloatOffset, sizeof(float))) {
            throw std::runtime_error("native light transition fallback resource is truncated");
        }
        float fallbackWeight = 0.0f;
        std::memcpy(&fallbackWeight,
                    bytes.data() + layout.RuntimeTransitionGlobalFallbackModeWeightFloatOffset,
                    sizeof(fallbackWeight));
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateAvailable = true;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin = true;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateSourceKind =
            "oot3d_packaged_code_bin_global_environment_fallback_state";
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateAddress =
            layout.RuntimeTransitionGlobalFallbackStateAddress;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeOffset =
            layout.RuntimeTransitionGlobalFallbackModeOffset;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset =
            layout.RuntimeTransitionGlobalFallbackModeWeightFloatOffset;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackFromIndexOffset =
            layout.RuntimeTransitionGlobalFallbackFromIndexOffset;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackToIndexOffset =
            layout.RuntimeTransitionGlobalFallbackToIndexOffset;
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackMode =
            bytes[layout.RuntimeTransitionGlobalFallbackModeOffset];
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackFromIndex =
            bytes[layout.RuntimeTransitionGlobalFallbackFromIndexOffset];
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackToIndex =
            bytes[layout.RuntimeTransitionGlobalFallbackToIndexOffset];
        mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeWeight = fallbackWeight;
    } catch (const std::exception& error) {
        mRuntimeTransitionTableError = error.what();
    }
    try {
        const auto source = mSources.Load(std::string(kFogDefaultSourceResource));
        const auto projection = mSources.Load(std::string(kFogViewProjectionDefaultsResource));
        const auto sceneFar = mSources.Load(std::string(kFogSceneProjectionFarResource));
        if (source == nullptr || source->Bytes->size() != 0x43 ||
            projection == nullptr || projection->Bytes->size() != 8 ||
            sceneFar == nullptr || sceneFar->Bytes->size() != 4) {
            throw std::runtime_error("native fog runtime default resources are missing or truncated");
        }
        const auto readFloat = [](const std::vector<uint8_t>& bytes, size_t offset) {
            float value = 0.0f;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        };
        const auto readS32AsFloat = [](const std::vector<uint8_t>& bytes, size_t offset) {
            int32_t value = 0;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return static_cast<float>(value);
        };
        const auto& bytes = *source->Bytes;
        mFogRuntimeDefaults.SourceRgbScaleR = readFloat(bytes, 0x24);
        mFogRuntimeDefaults.SourceRgbScaleG = readFloat(bytes, 0x28);
        mFogRuntimeDefaults.SourceRgbScaleB = readFloat(bytes, 0x2C);
        mFogRuntimeDefaults.SourceNear = readS32AsFloat(bytes, 0x34);
        mFogRuntimeDefaults.SourceFar = readS32AsFloat(bytes, 0x38);
        mFogRuntimeDefaults.SourceRebuildGate = bytes[0x41];
        mFogRuntimeDefaults.SourceCurveMode = bytes[0x42];
        mFogRuntimeDefaults.ProjectionNear = readFloat(*projection->Bytes, 0);
        mFogRuntimeDefaults.ProjectionFar = readFloat(*projection->Bytes, 4);
        mFogRuntimeDefaults.SceneProjectionFar = readFloat(*sceneFar->Bytes, 0);
        mFogRuntimeDefaults.SourceKind =
            "oot3d_packaged_code_bin_fog_runtime_defaults";
        mFogRuntimeDefaults.Available = true;
    } catch (const std::exception& error) {
        mFogRuntimeDefaultsError = error.what();
    }
    try {
        const auto source = mSources.Load(std::string(kNativeKankyoManifestResource));
        if (source == nullptr) {
            throw std::runtime_error("native kankyo shard manifest could not be loaded");
        }
        mKankyoManifest = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
        if (mKankyoManifest.value("format", "") != "oot3d_native_kankyo_shard_v1") {
            throw std::runtime_error("native kankyo shard manifest format is unsupported");
        }
    } catch (const std::exception& error) {
        mKankyoManifestError = error.what();
    }
    try {
        const auto records = mSources.Load(std::string(kKankyoSkyboxRecordsResource));
        const auto schedule = mSources.Load(std::string(kKankyoScheduleTableResource));
        const auto drawScale = mSources.Load(std::string(kKankyoDrawScaleResource));
        if (records == nullptr || records->Bytes->size() != 30 * 0x50 ||
            schedule == nullptr || schedule->Bytes->size() != 5 * 0x48 ||
            drawScale == nullptr || drawScale->Bytes->size() != sizeof(float)) {
            throw std::runtime_error("native kankyo runtime table resources are missing or truncated");
        }
        mKankyoSkyboxRecords = *records->Bytes;
        mKankyoScheduleTable = *schedule->Bytes;
        std::memcpy(&mKankyoDrawScale, drawScale->Bytes->data(), sizeof(mKankyoDrawScale));
        if (!std::isfinite(mKankyoDrawScale) || mKankyoDrawScale <= 0.0f) {
            throw std::runtime_error("native kankyo draw scale is invalid");
        }
        mKankyoRuntimeTablesAvailable = true;
    } catch (const std::exception& error) {
        mKankyoRuntimeTablesError = error.what();
    }
}

NativeKankyoTemporalState NativeSceneEnvironmentProvider::ResolveKankyoTemporalState(
    int32_t skyboxId, int32_t mode, uint16_t activeAngle) const {
    constexpr size_t kRecordSize = 0x50;
    constexpr size_t kModeStride = 0x48;
    constexpr size_t kEntrySize = 0x08;
    constexpr size_t kEntryCount = 9;
    NativeKankyoTemporalState state;
    state.SkyboxId = skyboxId;
    state.Mode = mode;
    state.ActiveAngle = activeAngle;
    state.DrawScale = mKankyoDrawScale;
    state.SourceKind = "oot3d_packaged_code_bin_kankyo_runtime_tables";
    if (!mKankyoRuntimeTablesAvailable || skyboxId < 0 || skyboxId >= 30 ||
        mode < 0 || mode >= 5) {
        return state;
    }
    const size_t recordOffset = static_cast<size_t>(skyboxId) * kRecordSize;
    const auto readU32 = [](const std::vector<uint8_t>& bytes, size_t offset) {
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    for (size_t index = 0; index < 0x40 && mKankyoSkyboxRecords[recordOffset + index] != 0; ++index) {
        state.RomPath.push_back(static_cast<char>(mKankyoSkyboxRecords[recordOffset + index]));
    }
    state.ArchiveName = std::filesystem::path(state.RomPath).filename().string();
    state.ProfileCount = static_cast<int32_t>(readU32(mKankyoSkyboxRecords, recordOffset + 0x44));
    state.LayerCount = static_cast<int32_t>(readU32(mKankyoSkyboxRecords, recordOffset + 0x48));
    state.CoreCmbCount = static_cast<int32_t>(readU32(mKankyoSkyboxRecords, recordOffset + 0x4C));
    const size_t modeOffset = static_cast<size_t>(mode) * kModeStride;
    for (size_t entryIndex = 0; entryIndex < kEntryCount; ++entryIndex) {
        const size_t offset = modeOffset + entryIndex * kEntrySize;
        uint16_t start = 0;
        uint16_t end = 0;
        std::memcpy(&start, mKankyoScheduleTable.data() + offset, sizeof(start));
        std::memcpy(&end, mKankyoScheduleTable.data() + offset + 2, sizeof(end));
        if (start <= activeAngle && (activeAngle < end || end == 0xFFFF)) {
            state.ScheduleEntryIndex = static_cast<int32_t>(entryIndex);
            state.CurrentProfileIndex = mKankyoScheduleTable[offset + 5];
            state.NextProfileIndex = mKankyoScheduleTable[offset + 6];
            if (mKankyoScheduleTable[offset + 4] != 0 && end != start) {
                const double weight = std::clamp(
                    1.0 - static_cast<double>(end - activeAngle) /
                              static_cast<double>(end - start), 0.0, 1.0);
                state.BlendAlpha = static_cast<uint8_t>(std::lround(weight * 255.0));
            }
            break;
        }
    }
    state.Available = !state.ArchiveName.empty() && state.ScheduleEntryIndex >= 0;
    return state;
}

std::shared_ptr<const NativeKankyoArchiveSource>
NativeSceneEnvironmentProvider::ResolveKankyoArchive(std::string_view archiveName) {
    const std::string key(archiveName);
    {
        std::scoped_lock lock(mMutex);
        const auto cached = mKankyoCache.find(key);
        if (cached != mKankyoCache.end()) {
            return cached->second;
        }
    }
    auto result = std::make_shared<NativeKankyoArchiveSource>();
    result->ArchiveName = key;
    try {
        if (!mKankyoManifest.is_object()) {
            throw std::runtime_error(mKankyoManifestError.empty()
                                         ? "native kankyo shard manifest is unavailable"
                                         : mKankyoManifestError);
        }
        const nlohmann::json* record = nullptr;
        for (const auto& candidate : mKankyoManifest.at("records")) {
            if (candidate.value("archive_name", "") == key) {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr) {
            result->Status = "kankyo_archive_not_packaged";
        } else {
            result->ResourcePath = record->at("resource").get<std::string>();
            result->Source = mSources.Load(result->ResourcePath);
            if (result->Source == nullptr) {
                throw std::runtime_error("native kankyo ZAR resource could not be loaded");
            }
            result->Archive = ParseZarArchiveBytes(*result->Source->Bytes, result->ResourcePath);
            for (const auto& file : result->Archive.Files) {
                if (file.Offset > result->Source->Bytes->size() ||
                    file.Size > result->Source->Bytes->size() - file.Offset) {
                    throw std::runtime_error("native kankyo ZAR entry lies outside source bytes");
                }
                const auto bytes = std::span<const uint8_t>(
                    result->Source->Bytes->data() + file.Offset, file.Size);
                if (file.TypeName == "cmb" || file.Name.ends_with(".cmb")) {
                    auto model = ParseCmbModelBytes(bytes, file.Name);
                    auto renderModel = BuildOot3dNativeRenderModel(
                        model, { {}, mKankyoDrawScale, false });
                    result->Models.push_back(
                        { file.Name, file.TypeLocalIndex, std::move(model), std::move(renderModel) });
                } else if (file.TypeName == "cmab" || file.Name.ends_with(".cmab")) {
                    result->MaterialAnimations.push_back(
                        { file.Name, ParseCmabMaterialAnimationBytes(bytes, file.Name) });
                } else if (file.TypeName == "ctxb" || file.Name.ends_with(".ctxb")) {
                    result->Textures.push_back({ file.Name, ParseCtxbTextureBytes(bytes, file.Name) });
                }
            }
            for (auto& model : result->Models) {
                for (const auto& animation : result->MaterialAnimations) {
                    if (Oot3dNativeKankyoCmabAppliesToCmb(animation.Name, model.Name)) {
                        model.MaterialAnimations.push_back(animation.Animation);
                    }
                }
            }
            result->Status = result->Models.empty() ? "kankyo_archive_contains_no_cmb" : "ready";
        }
    } catch (const std::exception& error) {
        result->Status = "kankyo_archive_parse_failed";
        result->Error = error.what();
    }
    std::scoped_lock lock(mMutex);
    return mKankyoCache.emplace(key, result).first->second;
}

std::shared_ptr<const NativeSceneEnvironmentSetup> NativeSceneEnvironmentProvider::Resolve(int32_t sceneId,
                                                                                            int32_t setupIndex) {
    const std::string key = std::to_string(sceneId) + ":" + std::to_string(setupIndex);
    {
        std::scoped_lock lock(mMutex);
        const auto cached = mCache.find(key);
        if (cached != mCache.end()) {
            return cached->second;
        }
    }
    auto result = std::make_shared<NativeSceneEnvironmentSetup>();
    result->Scene = mCatalog.FindScene(sceneId);
    result->SetupIndex = setupIndex;
    result->LightingSemanticsResource = mLightingSemanticsResource;
    result->FogRuntimeDefaults = mFogRuntimeDefaults;
    result->LightingSemanticsAvailable = mLightingSemanticPlan.Available;
    if (result->LightingSemanticsAvailable) {
        result->LightingSemantics = mLightingSemantics;
        result->LightingSemanticPlan = mLightingSemanticPlan;
    }
    if (result->Scene == nullptr || result->Scene->SceneShardManifestResource.empty()) {
        result->Status = "scene_environment_not_packaged";
    } else {
        try {
            const auto source = mSources.Load(result->Scene->SceneShardManifestResource);
            if (source == nullptr) {
                throw std::runtime_error("scene shard manifest could not be loaded");
            }
            const auto manifest = nlohmann::json::parse(source->Bytes->begin(), source->Bytes->end());
            const nlohmann::json* sceneRecord = nullptr;
            for (const auto& record : manifest.at("records")) {
                if (record.value("scene_stem", "") == result->Scene->SceneStem) {
                    sceneRecord = &record;
                    break;
                }
            }
            if (sceneRecord == nullptr) {
                throw std::runtime_error("scene is absent from its shard manifest");
            }
            const nlohmann::json* setupRecord = nullptr;
            for (const auto& setup : sceneRecord->at("environment_setups")) {
                if (setup.value("setup_index", -1) == setupIndex) {
                    setupRecord = &setup;
                    break;
                }
            }
            if (setupRecord == nullptr) {
                result->Status = "scene_environment_setup_missing";
            } else {
                result->SetupRole = setupRecord->value("setup_role", "");
                for (const auto& command : setupRecord->at("commands")) {
                    const auto& decoded = command.at("decoded");
                    const std::string name = command.value("command_name", "");
                    if (name == "skybox_settings") {
                        result->SkyboxId = decoded.value("skybox_id", -1);
                        result->Weather = decoded.value("weather_or_unk_05", -1);
                        result->Indoors = decoded.value("indoors", -1);
                    } else if (name == "misc_settings") {
                        result->CameraOrWorldMapArea = decoded.value("camera_or_world_map_area", -1);
                        result->MiscRawArgument = decoded.value("raw_argument", 0U);
                    } else if (name == "light_settings_list") {
                        std::vector<uint8_t> tableBytes;
                        const auto& records = decoded.at("records");
                        tableBytes.reserve(records.size() * 0x1C);
                        size_t expectedIndex = 0;
                        for (const auto& record : decoded.at("records")) {
                            NativePicaLightSettingRecord light;
                            light.Index = record.value("index", 0U);
                            if (!DecodeHexRecord(record.value("raw_hex", ""), light.Raw)) {
                                throw std::runtime_error("invalid native PICA light-setting raw record");
                            }
                            if (light.Index != expectedIndex++) {
                                throw std::runtime_error("non-contiguous native PICA light-setting record indices");
                            }
                            tableBytes.insert(tableBytes.end(), light.Raw.begin(), light.Raw.end());
                            result->LightSettings.push_back(light);
                        }
                        auto decodedRecords =
                            DecodeOot3dNativePicaLightSettingsTableForRuntime(tableBytes, setupIndex);
                        result->DecodedLighting.LightSettings.insert(
                            result->DecodedLighting.LightSettings.end(),
                            std::make_move_iterator(decodedRecords.begin()),
                            std::make_move_iterator(decodedRecords.end()));
                    }
                }
                result->DecodedLighting.Available = !result->DecodedLighting.LightSettings.empty();
                result->DecodedLighting.DecodedFromNativeZsi = result->DecodedLighting.Available;
                result->DecodedLighting.ActiveSetupIndex = setupIndex;
                result->DecodedLighting.ActiveSetupLightSettingsRecordCount =
                    static_cast<int>(result->DecodedLighting.LightSettings.size());
                result->DecodedLighting.DecodedLightSettingsRecordCount =
                    static_cast<int>(result->DecodedLighting.LightSettings.size());
                result->DecodedLighting.SelectedLightSettingsLayout =
                    result->DecodedLighting.LightSettings.empty()
                        ? ""
                        : result->DecodedLighting.LightSettings.front().Layout;
                result->DecodedLighting.SourceKind = "oot3d_packaged_zsi_light_settings_list";
                result->DecodedLighting.NativeRuntimeTransitionModes =
                    mRuntimeLightingContract.NativeRuntimeTransitionModes;
                result->DecodedLighting.NativeRuntimeTransitionTableAvailable =
                    mRuntimeLightingContract.NativeRuntimeTransitionTableAvailable;
                result->DecodedLighting.NativeRuntimeTransitionTableDecodedFromCodeBin =
                    mRuntimeLightingContract.NativeRuntimeTransitionTableDecodedFromCodeBin;
                result->DecodedLighting.NativeRuntimeTransitionTableSourceKind =
                    mRuntimeLightingContract.NativeRuntimeTransitionTableSourceKind;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackStateAvailable =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateAvailable;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackStateSourceKind =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateSourceKind;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackStateAddress =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackStateAddress;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackModeOffset =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeOffset;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackFromIndexOffset =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackFromIndexOffset;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackToIndexOffset =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackToIndexOffset;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackMode =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackMode;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackFromIndex =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackFromIndex;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackToIndex =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackToIndex;
                result->DecodedLighting.NativeRuntimeTransitionGlobalFallbackModeWeight =
                    mRuntimeLightingContract.NativeRuntimeTransitionGlobalFallbackModeWeight;
                if (!result->DecodedLighting.NativeRuntimeTransitionTableAvailable) {
                    result->Status = "scene_environment_transition_table_missing";
                    result->Error = mRuntimeTransitionTableError;
                } else if (!result->LightingSemanticsAvailable) {
                    result->Status = "scene_environment_lighting_semantics_missing";
                    result->Error = mLightingSemanticsError;
                } else if (result->LightingSemantics.value("layout", "") !=
                           result->DecodedLighting.SelectedLightSettingsLayout) {
                    result->Status = "scene_environment_lighting_layout_mismatch";
                    result->Error = "native PICA lighting semantics layout does not match the ZSI record layout";
                } else {
                    result->Status = "ready";
                }
            }
        } catch (const std::exception& error) {
            result->Status = "scene_environment_parse_failed";
            result->Error = error.what();
        }
    }
    std::scoped_lock lock(mMutex);
    return mCache.emplace(key, result).first->second;
}

void NativeSceneEnvironmentProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mCache.clear();
    mKankyoCache.clear();
}

} // namespace ThreeDsRecomp::Oot3d
