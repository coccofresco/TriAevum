#pragma once

#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ThreeDsRecomp::Oot3d {

constexpr size_t kInvalidLinkCsabClipIndex = static_cast<size_t>(-1);
constexpr std::string_view kDefaultLinkStandingCsabName = "boy/anim/nml_wait_free.csab";
constexpr std::string_view kDefaultLinkWalkCsabName = "child/anim/nml_walk_free.csab";
constexpr std::string_view kDefaultLinkWalkEndLeftCsabName = "boy/anim/nml_walk_endL_free.csab";
constexpr std::string_view kDefaultLinkWalkEndRightCsabName = "boy/anim/nml_walk_endR_free.csab";

struct Oot3dNativeDemoPlayerClipBinding {
    std::string Id;
    std::string CsabName;
};

struct Oot3dNativeDemoPlayerClipSelection {
    std::string Idle;
    std::string Walk;
    std::string Run;
    std::string WalkEndLeft;
    std::string WalkEndRight;
    std::vector<Oot3dNativeDemoPlayerClipBinding> AdditionalClips;
    bool PoseSamplingPolicyAvailable = false;
    CsabPoseSamplingPolicy PoseSamplingPolicy;
    std::array<float, 3> RootBaseTranslation = { 0.0f, 0.0f, 0.0f };
};

struct Oot3dNativeDemoLinkCsabClip {
    std::string Id;
    std::string CsabName;
    CsabMetadata Metadata;
    std::vector<uint8_t> Bytes;
    std::string FacebName;
    FacebMaterialFrameTrack FacebTrack;
    bool FacebTrackAvailable = false;
};

struct Oot3dNativeDemoLinkMaterialFrameSelection {
    std::string Source;
    bool FacebTrackAvailable = false;
    bool EyeFrameSelected = false;
    bool MouthFrameSelected = false;
    uint8_t EyeFrame = 0;
    uint8_t MouthFrame = 0;
    uint8_t HoldValue = 0xFF;
};

struct Oot3dDemoVec3 {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

struct Oot3dDemoBounds {
    Oot3dDemoVec3 Min;
    Oot3dDemoVec3 Max;
    bool Valid = false;
};

struct Oot3dNativeDemoCollisionPolygon {
    int Type = 0;
    int VertexA = 0;
    int VertexB = 0;
    int VertexC = 0;
    int RawVertexA = 0;
    int RawVertexB = 0;
    int RawVertexC = 0;
    int VertexAFlags = 0;
    int VertexBFlags = 0;
    bool IgnoreCamera = false;
    bool IgnoreEntities = false;
    bool IgnoreProjectiles = false;
    bool Conveyor = false;
    int NormalX = 0;
    int NormalY = 0;
    int NormalZ = 0;
    int Dist = 0;
};

struct Oot3dNativeDemoSurfaceType {
    uint32_t Data1 = 0;
    uint32_t Data2 = 0;
};

struct Oot3dNativeDemoBgCamera {
    uint16_t Setting = 0;
    uint16_t Count = 0;
    uint32_t DataOffset = 0;
    int CameraPositionVectorIndex = 0;
};

struct Oot3dNativeDemoBgCameraPosition {
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    Oot3dDemoVec3 Other;
};

struct Oot3dNativeDemoPlayerStart {
    bool Valid = false;
    int SetupIndex = -1;
    int SetupOffset = -1;
    int SpawnCommandOffset = -1;
    int SpawnListOffset = -1;
    int SpawnListStartDelta = 0;
    int SpawnIndex = -1;
    int EntranceCommandOffset = -1;
    int EntranceListOffset = -1;
    int EntranceListStartDelta = 0;
    int EntranceIndex = -1;
    int RequestedEntranceIndex = -1;
    int RequestedGlobalEntranceIndex = -1;
    int GlobalEntranceTableRuntimeAddress = -1;
    int GlobalEntranceTableFileOffset = -1;
    int GlobalEntranceEntryFileOffset = -1;
    int GlobalEntranceSceneId = -1;
    int GlobalEntranceLocalEntranceIndex = -1;
    int GlobalEntranceField = -1;
    int Room = -1;
    int ActorId = -1;
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    int Params = 0;
    int CameraDataIndex = -1;
    int FloorPolygonIndex = -1;
    int FloorSurfaceType = -1;
    int FloorLightSettingRawIndex = -1;
    int FloorLightSettingIndex = -1;
    std::string FloorLightSettingSource;
    std::string SelectionSource;
    std::string GlobalEntranceSourceKind;
    std::string GlobalEntranceCodeBinPath;
};

struct Oot3dNativeDemoZsiCommandRecord {
    int SetupIndex = -1;
    int Offset = -1;
    int CommandId = -1;
    int Parameter = 0;
    uint32_t CommandWord = 0;
    uint32_t Argument = 0;
    bool ArgumentInFile = false;
};

struct Oot3dNativeDemoRoomReference {
    int SetupIndex = -1;
    int CommandOffset = -1;
    int Index = -1;
    int Offset = -1;
    std::string RomPath;
    std::filesystem::path ResolvedPath;
    bool Available = false;
};

struct Oot3dNativeDemoRoomPayloadList {
    bool Valid = false;
    int CommandOffset = -1;
    int CommandArgument = -1;
    int Count = 0;
    int StartOffset = -1;
    int StartDelta = 0;
    int EndOffset = -1;
    int PayloadEndHint = -1;
    int PayloadEndGap = 0;
    int EntrySize = 0;
    std::string Interpretation;
};

struct Oot3dNativeDemoRoomObjectEntry {
    int Index = -1;
    int Offset = -1;
    int ObjectId = -1;
    bool PossibleObjectId = false;
    std::string ObjectName;
    std::filesystem::path ArchivePath;
    bool ArchiveAvailable = false;
    std::string ArchiveResolutionStatus;
    int ArchiveFileCount = 0;
    int ArchiveCmbCount = 0;
    int ArchiveCsabCount = 0;
};

struct Oot3dNativeDemoRoomActorEntry {
    int Index = -1;
    int Offset = -1;
    int ActorId = -1;
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    int Params = 0;
    bool Plausible = false;
    std::string ActorName;
    bool NativeProfileAvailable = false;
    uint32_t NativeProfileAddress = 0;
    int NativeProfileObjectId = -1;
    std::string NativeProfileObjectName;
    bool NativeProfileObjectPresentInRoomBank = false;
    uint32_t NativeProfileInitFunctionAddress = 0;
    uint32_t NativeProfileUpdateFunctionAddress = 0;
    uint32_t NativeProfileDrawFunctionAddress = 0;
    bool NativeVisualBehaviorAvailable = false;
    int NativeVisualNormalCmbTypeLocalIndex = -1;
    int NativeVisualFieryCmbTypeLocalIndex = -1;
    double NativeVisualScale = 1.0;
    int NativeVisualRotationYStepS16PerTick = 0;
    std::string NativeVisualBehaviorSource;
    std::filesystem::path ArchivePath;
    bool ArchiveAvailable = false;
    std::string ArchiveResolutionStatus;
    int ArchiveFileCount = 0;
    int ArchiveCmbCount = 0;
    int ArchiveCsabCount = 0;
};

struct Oot3dNativeDemoAssetGraph {
    bool Valid = false;
    std::filesystem::path SceneZsiPath;
    std::filesystem::path RoomZsiPath;
    std::filesystem::path SemanticSourcePath;
    bool SemanticSourceAvailable = false;
    std::filesystem::path ActorArchiveRoot;
    bool ActorArchiveRootAvailable = false;
    std::filesystem::path NativeActorProfileCodeBinPath;
    bool NativeActorProfileCodeBinAvailable = false;
    int NativeActorProfileDecodedCount = 0;
    int ResolvedObjectArchiveCount = 0;
    int ResolvedActorArchiveCount = 0;
    int SceneSetupCount = 0;
    int SceneCommandCount = 0;
    int RequestedRoomSetupIndex = -1;
    int RoomBaseCommandTableOffset = -1;
    int RoomCommandTableOffset = -1;
    int RoomAlternateHeaderListOffset = -1;
    int RoomAlternateHeaderEntryIndex = -1;
    uint32_t RoomAlternateHeaderRawOffset = 0;
    std::string RoomCommandTableSelectionStatus;
    int RoomCommandCount = 0;
    bool ManifestRoomPathFallbackUsed = false;
    std::vector<Oot3dNativeDemoZsiCommandRecord> SceneCommands;
    std::vector<Oot3dNativeDemoZsiCommandRecord> RoomCommands;
    std::vector<Oot3dNativeDemoRoomReference> RoomReferences;
    Oot3dNativeDemoRoomPayloadList RoomObjectList;
    std::vector<Oot3dNativeDemoRoomObjectEntry> RoomObjects;
    Oot3dNativeDemoRoomPayloadList RoomActorList;
    std::vector<Oot3dNativeDemoRoomActorEntry> RoomActors;
};

struct Oot3dNativeDemoPicaByteGroup {
    int Raw0 = 0;
    int Raw1 = 0;
    int Raw2 = 0;
    int Raw3 = 0;
    int Signed0 = 0;
    int Signed1 = 0;
    int Signed2 = 0;
    int Signed3 = 0;
    double Normalized0 = 0.0;
    double Normalized1 = 0.0;
    double Normalized2 = 0.0;
    double Normalized3 = 0.0;
};

struct Oot3dNativeDemoPicaLightSettingsRecord {
    int SetupIndex = -1;
    int CommandOffset = -1;
    int CommandArgument = -1;
    int Index = -1;
    int Offset = -1;
    int EntrySize = 0;
    std::string Layout;
    std::vector<uint8_t> RawBytes;
    std::vector<int> RawHalfwords;
    std::vector<Oot3dNativeDemoPicaByteGroup> ByteGroups;
    bool NativeEnvLightSettingsAvailable = false;
    int NativeEnvLightSettingsPrefixSize = 0;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    Vec3f Light0Direction = { 0.0f, 0.0f, 0.0f };
    ColorRgba8 Light0Color = { 0, 0, 0, 255 };
    Vec3f Light1Direction = { 0.0f, 0.0f, 0.0f };
    ColorRgba8 Light1Color = { 0, 0, 0, 255 };
    bool NativeActorVsLightPacketColorCandidateAvailable = false;
    bool NativeActorVsAmbientColorCandidateAvailable = false;
    ColorRgba8 NativeActorVsAmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 NativeActorVsDiffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 NativeActorVsDiffuse1Color = { 0, 0, 0, 255 };
    bool NativeRuntimeEnvironmentLightSettingsAvailable = false;
    int NativeRuntimeEnvironmentRecordOffset = -1;
    int NativeRuntimeEnvironmentRecordStartDelta = 0;
    std::vector<uint8_t> NativeRuntimeEnvironmentRawBytes;
    ColorRgba8 NativeRuntimeAmbientColor = { 0, 0, 0, 255 };
    Vec3f NativeRuntimeLight0Direction = { 0.0f, 0.0f, 0.0f };
    ColorRgba8 NativeRuntimeLight0Color = { 0, 0, 0, 255 };
    Vec3f NativeRuntimeLight1Direction = { 0.0f, 0.0f, 0.0f };
    ColorRgba8 NativeRuntimeLight1Color = { 0, 0, 0, 255 };
    ColorRgba8 NativeRuntimeFogColor = { 0, 0, 0, 255 };
    uint32_t NativeRuntimeScalar0Raw = 0;
    uint32_t NativeRuntimeScalar1Raw = 0;
    uint32_t NativeRuntimePackedHalfwordRaw = 0;
    int Native3dsTailByte0 = -1;
    double FloatParam0 = 0.0;
    double FloatParam1 = 0.0;
    bool FloatParamsFinite = false;
    uint32_t TailRaw = 0;
    std::vector<uint8_t> TailBytes;
};

struct Oot3dNativeDemoLightSettingsTransitionEntry {
    int EntryIndex = -1;
    uint16_t StartAngle = 0;
    uint16_t EndAngle = 0;
    uint8_t FromLightSettingIndex = 0;
    uint8_t ToLightSettingIndex = 0;
};

struct Oot3dNativeDemoLightSettingsTransitionMode {
    int ModeIndex = -1;
    std::vector<Oot3dNativeDemoLightSettingsTransitionEntry> Entries;
};

struct Oot3dNativeDemoPicaLightingState {
    bool Available = false;
    bool DecodedFromNativeZsi = false;
    bool UsesRuntimeN64AssetSubstitution = false;
    std::filesystem::path SceneZsiPath;
    std::filesystem::path RoomZsiPath;
    std::filesystem::path CodeBinPath;
    std::string SourceKind;
    std::string SelectedLightSettingsLayout;
    bool NativeRuntimeTransitionTableAvailable = false;
    bool NativeRuntimeTransitionTableDecodedFromCodeBin = false;
    std::string NativeRuntimeTransitionTableSourceKind;
    std::vector<Oot3dNativeDemoLightSettingsTransitionMode> NativeRuntimeTransitionModes;
    bool NativeRuntimeTransitionGlobalFallbackStateAvailable = false;
    bool NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin = false;
    std::string NativeRuntimeTransitionGlobalFallbackStateSourceKind;
    uint32_t NativeRuntimeTransitionGlobalFallbackStateAddress = 0;
    uint32_t NativeRuntimeTransitionGlobalFallbackModeOffset = 0;
    uint32_t NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset = 0;
    uint32_t NativeRuntimeTransitionGlobalFallbackFromIndexOffset = 0;
    uint32_t NativeRuntimeTransitionGlobalFallbackToIndexOffset = 0;
    int NativeRuntimeTransitionGlobalFallbackMode = -1;
    int NativeRuntimeTransitionGlobalFallbackFromIndex = -1;
    int NativeRuntimeTransitionGlobalFallbackToIndex = -1;
    double NativeRuntimeTransitionGlobalFallbackModeWeight = 0.0;
    int ActiveSetupIndex = -1;
    int SceneSetupCount = 0;
    int SceneLightSettingsCommandCount = 0;
    int SceneLightListCommandCount = 0;
    int RoomLightSettingsCommandCount = 0;
    int RoomLightListCommandCount = 0;
    int DecodedLightSettingsRecordCount = 0;
    int ActiveSetupLightSettingsRecordCount = 0;
    std::vector<Oot3dNativeDemoRoomPayloadList> SceneLightSettingsLists;
    std::vector<Oot3dNativeDemoRoomPayloadList> RoomLightSettingsLists;
    std::vector<Oot3dNativeDemoZsiCommandRecord> SceneLightListCommands;
    std::vector<Oot3dNativeDemoZsiCommandRecord> RoomLightListCommands;
    std::vector<Oot3dNativeDemoPicaLightSettingsRecord> LightSettings;
};

struct Oot3dNativeDemoActorVisualModel {
    int ActorId = -1;
    std::string ActorName;
    std::filesystem::path ArchivePath;
    std::string CmbName;
    std::string SelectionStatus;
    CmbModel Model;
};

struct Oot3dNativeDemoActorVisualInstance {
    int ActorEntryIndex = -1;
    int ActorId = -1;
    std::string ActorName;
    size_t ModelIndex = 0;
    std::filesystem::path ArchivePath;
    std::string CmbName;
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    double Scale = 1.0;
    int RotationYStepS16PerTick = 0;
    int SelectedCmbTypeLocalIndex = -1;
    bool NativeVisualBehaviorResolved = false;
    std::string BehaviorSource;
    std::string TransformStatus;
    int Params = 0;
};

struct Oot3dNativeDemoActorVisualSkippedInstance {
    int ActorEntryIndex = -1;
    int ActorId = -1;
    std::string ActorName;
    int ModelIndex = -1;
    std::filesystem::path ArchivePath;
    std::string CmbName;
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    int Params = 0;
    int CmbEntryCount = 0;
    int ParseableCmbEntryCount = 0;
    int SkinnedPrimitiveCount = 0;
    std::string Reason;
    std::string SelectionStatus;
};

struct Oot3dNativeDemoCollisionScene {
    std::filesystem::path SourceZsiPath;
    std::filesystem::path ResourcePath;
    std::vector<Oot3dDemoVec3> Vertices;
    std::vector<Oot3dNativeDemoCollisionPolygon> Polygons;
    std::vector<Oot3dNativeDemoSurfaceType> SurfaceTypes;
    std::vector<Oot3dNativeDemoBgCamera> BgCameras;
    std::vector<Oot3dNativeDemoBgCameraPosition> BgCameraPositions;
    Oot3dDemoBounds Bounds;
    bool Valid = false;
    bool DecodedFromNativeZsi = false;
    bool XmlFallbackUsed = false;
    int SetupIndex = -1;
    int CommandOffset = -1;
    int CommandArgument = -1;
    int HeaderOffset = -1;
    int EffectiveVertexOffset = -1;
    int EffectivePolygonOffset = -1;
    int EffectiveSurfaceTypeOffset = -1;
    int EffectiveBgCamOffset = -1;
    int CameraPositionOffset = -1;
    int CameraPointerAdjustment = 0;
    int SurfaceTypeCount = 0;
    int BgCamCount = 0;
    int WaterBoxCount = 0;
};

struct Oot3dNativeDemoFloorHit {
    double Y = 0.0;
    int PolygonIndex = -1;
    int SurfaceType = -1;
};

struct Oot3dNativeDemoScene {
    CmbModel RoomModel;
    std::filesystem::path RoomMaterialAnimationArchivePath;
    int RoomMaterialAnimationRoomIndex = -1;
    std::string RoomMaterialAnimationStatus = "unresolved";
    std::vector<std::string> RoomMaterialAnimationNames;
    std::vector<CmabMaterialAnimation> RoomMaterialAnimations;
    CmbModel LinkModel;
    CsabMetadata LinkCsab;
    std::vector<uint8_t> LinkStandingCsabBytes;
    std::vector<Oot3dNativeDemoLinkCsabClip> LinkCsabClips;
    std::vector<CmabMaterialAnimation> LinkMaterialAnimations;
    size_t LinkStandingClipIndex = 0;
    size_t LinkWalkClipIndex = 0;
    size_t LinkMovementClipIndex = 0;
    size_t LinkWalkEndLeftClipIndex = kInvalidLinkCsabClipIndex;
    size_t LinkWalkEndRightClipIndex = kInvalidLinkCsabClipIndex;
    CsabPose LinkStandingPose;
    std::vector<Matrix4f> LinkBindWorldTransforms;
    std::vector<Matrix4f> LinkSkinTransforms;
    std::filesystem::path ManifestPath;
    std::filesystem::path RoomZsiPath;
    std::filesystem::path CollisionZsiPath;
    std::filesystem::path CollisionResourcePath;
    std::filesystem::path LinkManifestPath;
    std::filesystem::path NativeCameraTablePath;
    nlohmann::json NativeCameraTable;
    bool NativeCameraTableAvailable = false;
    bool NativeCameraTableRepoRootDefaultUsed = false;
    std::string NativeCameraTableSourceKind;
    std::string NativeCameraTableFormat;
    std::filesystem::path NativePicaLightingSemanticsPath;
    nlohmann::json NativePicaLightingSemantics;
    bool NativePicaLightingSemanticsAvailable = false;
    bool NativePicaLightingSemanticsRepoRootDefaultUsed = false;
    std::string NativePicaLightingSemanticsSourceKind;
    std::string NativePicaLightingSemanticsFormat;
    std::filesystem::path NativePicaRegisterTracePath;
    nlohmann::json NativePicaRegisterTrace;
    bool NativePicaRegisterTraceAvailable = false;
    bool NativePicaRegisterTraceRepoRootDefaultUsed = false;
    std::string NativePicaRegisterTraceSourceKind;
    std::string NativePicaRegisterTraceFormat;
    std::filesystem::path NativeCmbVShaderShbinPath;
    ShbinShaderBinary NativeCmbVShader;
    bool NativeCmbVShaderShbinAvailable = false;
    bool NativeCmbVShaderShbinDerivedFromRoomZsiPath = false;
    bool NativeCmbVShaderShbinRepoRootDefaultUsed = false;
    std::string NativeCmbVShaderShbinSourceKind;
    std::string NativeCmbVShaderShbinFormat;
    std::string LinkCmbName;
    std::string LinkManifestCsabName;
    std::string LinkStandingCsabName;
    std::vector<uint32_t> LinkMeshIndices;
    bool LinkPoseSamplingPolicyAvailable = false;
    CsabPoseSamplingPolicy LinkPoseSamplingPolicy;
    std::array<float, 3> LinkRootBaseTranslation = { 0.0f, 0.0f, 0.0f };
    Oot3dDemoVec3 Spawn;
    Oot3dDemoVec3 LinkOffset;
    Oot3dDemoBounds RoomBounds;
    Oot3dDemoBounds LinkBounds;
    Oot3dNativeDemoCollisionScene Collision;
    Oot3dNativeDemoPlayerStart PlayerStart;
    int ActiveSceneSetupIndex = -1;
    std::string ActiveSceneSetupSource;
    Oot3dNativeDemoAssetGraph AssetGraph;
    Oot3dNativeDemoPicaLightingState NativePicaLighting;
    std::vector<Oot3dNativeDemoActorVisualModel> ActorVisualModels;
    std::vector<Oot3dNativeDemoActorVisualInstance> ActorVisualInstances;
    std::vector<Oot3dNativeDemoActorVisualSkippedInstance> ActorVisualSkippedInstances;
    double LinkScale = 1.0;
    double LinkTargetHeight = 56.0;
    double LinkRadius = 12.0;
};

Oot3dNativeDemoScene LoadOot3dNativeDemoSceneFromManifest(
    const std::filesystem::path& manifestPath,
    std::string_view standingCsabName = kDefaultLinkStandingCsabName,
    int preferredEntranceIndex = -1,
    int activeSetupIndexOverride = -1,
    std::string_view activeSetupSource = {},
    const Oot3dNativeDemoPlayerClipSelection* playerClips = nullptr);
Oot3dNativeDemoPicaLightingState DecodeOot3dNativeDemoPicaLightingStateForRuntime(
    const std::filesystem::path& scenePath,
    const std::filesystem::path& roomPath,
    int activeSetupIndex);
Oot3dNativeDemoPicaLightSettingsRecord DecodeOot3dNativePicaLightSettingsRecordForRuntime(
    const std::vector<uint8_t>& bytes, int setupIndex, int recordIndex);
std::vector<Oot3dNativeDemoPicaLightSettingsRecord>
DecodeOot3dNativePicaLightSettingsTableForRuntime(
    const std::vector<uint8_t>& bytes, int setupIndex);
Oot3dNativeDemoAssetGraph DecodeOot3dNativeDemoAssetGraphForRuntime(
    const std::filesystem::path& scenePath,
    const std::filesystem::path& roomPath,
    const std::filesystem::path& manifestPath,
    const nlohmann::json& manifest,
    int activeSetupIndex = -1);

size_t NativeDemoDecodedTextureCount(const CmbModel& model);
size_t NativeDemoSelectedPrimitiveCountBySkinningMode(const CmbModel& model,
                                                      const std::vector<uint32_t>& selectedMeshIndices,
                                                      uint16_t skinningMode);
CsabPose SampleOot3dNativeDemoLinkPoseFrame(const Oot3dNativeDemoScene& scene, float frame);
CsabPose SampleOot3dNativeDemoLinkPoseFrame(const Oot3dNativeDemoScene& scene, size_t clipIndex, float frame);
Oot3dNativeDemoLinkMaterialFrameSelection SampleOot3dNativeDemoLinkMaterialFrameSelection(
    const Oot3dNativeDemoScene& scene, size_t clipIndex, float frame);
std::vector<Matrix4f> BuildOot3dNativeDemoSkinTransforms(const std::vector<Matrix4f>& bindWorldTransforms,
                                                         const CsabPose& pose);
Oot3dDemoBounds NativeDemoModelBounds(const CmbModel& model, Oot3dDemoVec3 offset = {});
Oot3dDemoBounds NativeDemoModelBoundsFromMeshes(const CmbModel& model, Oot3dDemoVec3 offset,
                                                const std::vector<uint32_t>* selectedMeshIndices,
                                                const CsabPose* pose,
                                                const std::vector<Matrix4f>* skinTransforms,
                                                double scale = 1.0);
Oot3dDemoVec3 NativeDemoBoundsCenter(const Oot3dDemoBounds& bounds);
double NativeDemoBoundsMaxExtent(const Oot3dDemoBounds& bounds);
void NativeDemoExpandBoundsByBounds(Oot3dDemoBounds& bounds, const Oot3dDemoBounds& value);
int NativeDemoSurfaceTypeCameraDataIndex(const Oot3dNativeDemoSurfaceType& surfaceType);
int NativeDemoSurfaceTypeLightSettingRawIndex(const Oot3dNativeDemoSurfaceType& surfaceType);
int NativeDemoNormalizeLightSettingIndex(int lightSettingIndex);
int NativeDemoSurfaceTypeLightSettingIndex(const Oot3dNativeDemoSurfaceType& surfaceType);
Vec3f NativeDemoPosedVertexPosition(const CmbPrimitive& primitive, const Vec3f& position, uint32_t vertexIndex,
                                    const CsabPose* pose, const std::vector<Matrix4f>* skinTransforms);
Vec3f NativeDemoPosedVertexNormal(const CmbPrimitive& primitive, const Vec3f& normal, uint32_t vertexIndex,
                                  const CsabPose* pose, const std::vector<Matrix4f>* skinTransforms);
std::optional<Oot3dNativeDemoFloorHit> Oot3dNativeDemoFloorHitAt(
    const Oot3dNativeDemoCollisionScene& collision, double x, double z, double queryY);
nlohmann::json Oot3dNativeDemoCollisionSceneSummaryToJson(const Oot3dNativeDemoCollisionScene& collision);
nlohmann::json Oot3dNativeDemoSceneSummaryToJson(const Oot3dNativeDemoScene& scene);

} // namespace ThreeDsRecomp::Oot3d
