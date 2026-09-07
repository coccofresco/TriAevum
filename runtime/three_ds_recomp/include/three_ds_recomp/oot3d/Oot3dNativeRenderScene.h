#pragma once

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Oot3d {

struct Oot3dNativeRenderVertex {
    Vec3f Position;
    Vec3f Normal = { 0.0f, 1.0f, 0.0f };
    Vec2f Uv0;
    Vec2f Uv1;
    Vec2f Uv2;
    Vec2f NativeSourceUv0;
    Vec2f NativePicaBumpUv;
    Vec2f NativePicaShadow2dTexCoord0;
    float NativePicaShadow2dTexCoord0W = 1.0f;
    ColorRgba8 Color;
    ColorRgba8 NativePicaLightingInputColor;
    bool NativeNormalAvailable = false;
    bool NativeColorAvailable = false;
    bool NativePicaLightingInputColorAvailable = false;
    bool NativeSourceUv0Available = false;
    bool NativePicaBumpUvAvailable = false;
    bool NativePicaShadow2dTexCoord0WAvailable = false;
    uint32_t NativeSourceVertexIndex = 0;
    bool NativeSourceVertexIndexAvailable = false;
};

struct Oot3dNativePicaScalarStats {
    bool Available = false;
    size_t Count = 0;
    double Min = 0.0;
    double Max = 0.0;
    double Average = 0.0;
    size_t NegativeCount = 0;
    size_t ZeroCount = 0;
    size_t PositiveCount = 0;
};

struct Oot3dNativePicaColorStats {
    bool Available = false;
    size_t Count = 0;
    ColorRgba8 Min = { 0, 0, 0, 0 };
    ColorRgba8 Max = { 0, 0, 0, 0 };
    ColorRgba8 Average = { 0, 0, 0, 0 };
};

struct Oot3dNativePicaLightingVertexDiagnosticSample {
    size_t VertexIndex = 0;
    Vec3f Position = { 0.0f, 0.0f, 0.0f };
    Vec3f LightingNormal = { 0.0f, 0.0f, 0.0f };
    Vec3f WorldNormal = { 0.0f, 0.0f, 0.0f };
    double Light0Dot = 0.0;
    double Light1Dot = 0.0;
    double Light0DiffuseScale = 0.0;
    double Light1DiffuseScale = 0.0;
    ColorRgba8 BaseColor = { 0, 0, 0, 0 };
    ColorRgba8 ModulationColor = { 0, 0, 0, 255 };
    ColorRgba8 OutputColor = { 0, 0, 0, 0 };
};

struct Oot3dNativePicaLightingBatchDiagnostics {
    bool Available = false;
    std::string SourceKind;
    size_t VertexCount = 0;
    size_t NativeNormalVertexCount = 0;
    size_t DirectionalEvaluatedVertexCount = 0;
    std::string Light0VectorSource;
    std::string Light1VectorSource;
    Vec3f Light0Vector = { 0.0f, 1.0f, 0.0f };
    Vec3f Light1Vector = { 0.0f, 1.0f, 0.0f };
    Vec3f AverageLightingNormal = { 0.0f, 0.0f, 0.0f };
    Vec3f AverageWorldNormal = { 0.0f, 0.0f, 0.0f };
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse1Color = { 0, 0, 0, 255 };
    ColorRgba8 MaterialEmissionColor = { 0, 0, 0, 255 };
    ColorRgba8 MaterialAmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 MaterialDiffuseColor = { 0, 0, 0, 255 };
    ColorRgba8 EffectiveMaterialDiffuseColor = { 0, 0, 0, 255 };
    Oot3dNativePicaScalarStats Light0DotStats;
    Oot3dNativePicaScalarStats Light0DiffuseScaleStats;
    Oot3dNativePicaScalarStats Light0OppositeDotStats;
    Oot3dNativePicaScalarStats Light0OppositeDiffuseScaleStats;
    Oot3dNativePicaScalarStats Light1DotStats;
    Oot3dNativePicaScalarStats Light1DiffuseScaleStats;
    Oot3dNativePicaColorStats BaseColorStats;
    Oot3dNativePicaColorStats ModulationColorStats;
    Oot3dNativePicaColorStats OutputColorStats;
    std::vector<Oot3dNativePicaLightingVertexDiagnosticSample> Samples;
};

struct Oot3dNativeRenderTextureMipLevel {
    uint32_t Level = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    bool Rgba8Decoded = false;
    size_t Rgba8ByteCount = 0;
    uint64_t Rgba8Hash = 0;
    std::vector<uint8_t> Rgba8;
};

struct Oot3dNativeRenderTexture {
    uint32_t SourceIndex = 0;
    std::string Name;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint16_t TextureFormat = 0;
    uint16_t DataType = 0;
    uint32_t MipmapCount = 1;
    bool MipmapLayoutDecoded = false;
    bool Rgba8Decoded = false;
    bool HasNativeAlpha = false;
    bool Rgba8HashAvailable = false;
    size_t Rgba8ByteCount = 0;
    uint64_t Rgba8Hash = 0;
    std::vector<uint8_t> Rgba8;
    std::vector<Oot3dNativeRenderTextureMipLevel> AdditionalMipLevels;
};

struct Oot3dNativeRenderLutRecord {
    uint32_t Index = 0;
    uint32_t SourceOffset = 0;
    uint32_t Size = 0;
    uint8_t Type = 0;
    uint8_t HeaderByte01 = 0;
    uint8_t HeaderByte02 = 0;
    uint8_t HeaderByte03 = 0;
    uint32_t PointCount = 0;
    uint32_t PointStrideBytes = 0;
    std::vector<float> Samples;
    std::vector<float> PackedBaseValues;
    std::vector<float> PackedDeltaValues;
};

struct Oot3dNativeRenderLutSection {
    bool Decoded = false;
    uint32_t SourceOffset = 0;
    uint32_t ChunkSize = 0;
    uint32_t Count = 0;
    uint32_t HeaderWord0C = 0;
    bool NativeAssetDecodeContractAvailable = false;
    bool FinalShaderSemanticResolved = false;
    std::string SourceKind;
    std::vector<Oot3dNativeRenderLutRecord> Records;
};

struct Oot3dNativeRawMaterialWord {
    uint32_t Offset = 0;
    uint32_t Value = 0;
};

struct Oot3dNativeRenderTextureEnvState {
    uint32_t TableIndex = 0;
    uint32_t SourceOffset = 0;
    uint32_t StageOrder = 0;
    bool Decoded = false;
    uint32_t RawSize = 0;
    uint64_t RawFnv1a64 = 0;
    std::string ColorShaderPath;
    bool ColorShaderPathSupported = false;
    bool ColorShaderPathApplied = false;
    bool FallbackTextureLightModulationUsed = false;
    uint16_t CombineRgb = 0;
    uint16_t CombineAlpha = 0;
    uint16_t ColorScale = 1;
    uint16_t AlphaScale = 1;
    float ColorScaleMultiplier = 1.0f;
    float AlphaScaleMultiplier = 1.0f;
    bool ColorScaleSupported = false;
    bool AlphaScaleSupported = false;
    uint32_t RgbActiveSourceCount = 0;
    std::array<uint16_t, 2> UnknownUshort1 = { 0, 0 };
    std::array<uint16_t, 2> UnknownGlConstant = { 0, 0 };
    std::array<uint16_t, 3> SourceRgb = { 0, 0, 0 };
    std::array<uint16_t, 3> OperandRgb = { 0, 0, 0 };
    std::array<uint16_t, 3> SourceAlpha = { 0, 0, 0 };
    std::array<uint16_t, 3> OperandAlpha = { 0, 0, 0 };
    std::array<uint16_t, 2> UnknownUshort2 = { 0, 0 };
    uint32_t ConstantColorIndex = 0;
    bool ConstantColorIndexRecognized = false;
    bool RequiresShaderEvaluation = false;
};

struct Oot3dNativeRenderTextureEnvProgram {
    bool Decoded = false;
    uint32_t StageCount = 0;
    std::string ColorShaderPath;
    bool ColorShaderPathSupported = false;
    bool ColorShaderPathApplied = false;
    bool FallbackTextureLightModulationUsed = false;
    bool RgbCombinesKnown = false;
    bool RgbSourcesKnown = false;
    bool RgbOperandsKnown = false;
    bool UsesPrimaryColor = false;
    bool UsesFragmentLightingColor = false;
    bool UsesPrevious = false;
    bool UsesPreviousBuffer = false;
    bool UsesConstantColor = false;
    bool UsesTexture0 = false;
    bool UsesTexture1 = false;
    bool UsesTexture2 = false;
    bool UsesTexture3 = false;
    bool RequiresMultiStageEvaluation = false;
    bool RequiresMultiTextureSampling = false;
    bool RequiresConstantColorSelection = false;
    bool RequiresPreviousBuffer = false;
    bool RequiresTextureColorAdd = false;
    bool RgbRouteDecoded = false;
    bool ConstantColorResolved = false;
    int32_t ConstantColorIndex = -1;
    std::string ConstantColorSource;
    uint32_t VertexColorConstantStageCount = 0;
    ColorRgba8 VertexColorMultiplier = { 255, 255, 255, 255 };
    bool PrimaryColorMultiplierResolved = false;
    uint32_t PrimaryColorMultiplierStageCount = 0;
    Vec3f PrimaryColorMultiplier = { 1.0f, 1.0f, 1.0f };
    bool UsesColorScale = false;
    bool ColorScalesKnown = false;
    bool TextureColorMultiplierResolved = false;
    uint32_t TextureColorMultiplierStageCount = 0;
    Vec3f TextureColorMultiplier = { 1.0f, 1.0f, 1.0f };
    bool TextureColorAddendResolved = false;
    uint32_t TextureColorAddStageCount = 0;
    ColorRgba8 TextureColorAddend = { 0, 0, 0, 255 };
    bool TextureColorAddUsesTexture0Alpha = false;
    bool Texture1ColorAddResolved = false;
    uint32_t Texture1ColorAddStageCount = 0;
    uint32_t Texture1ColorAddMapperSlot = 1;
    bool Texture1ColorAddUsesTexture0Alpha = false;
    std::string Texture1ColorAddSource;
    bool Texture1ColorMultiplyResolved = false;
    uint32_t Texture1ColorMultiplyStageCount = 0;
    uint32_t Texture1ColorMultiplyMapperSlot = 1;
    std::string Texture1ColorMultiplySource;
    bool Texture0Texture1AddThenPrimaryColorModulateResolved = false;
    uint32_t Texture0Texture1AddThenPrimaryColorModulateStageCount = 0;
    uint32_t Texture0Texture1AddThenPrimaryColorModulateMapperSlot = 1;
    std::string Texture0Texture1AddThenPrimaryColorModulateSource;
    bool Texture1Texture2MultiplyAddPreviousResolved = false;
    uint32_t Texture1Texture2MultiplyAddPreviousStageCount = 0;
    uint32_t Texture1Texture2MultiplyAddPreviousTexture1MapperSlot = 1;
    uint32_t Texture1Texture2MultiplyAddPreviousTexture2MapperSlot = 2;
    std::string Texture1Texture2MultiplyAddPreviousSource;
    bool Texture0Texture1AddMultiplyTexture0Resolved = false;
    uint32_t Texture0Texture1AddMultiplyTexture0StageCount = 0;
    uint32_t Texture0Texture1AddMultiplyTexture0MapperSlot = 1;
    std::string Texture0Texture1AddMultiplyTexture0Source;
    bool Texture0PrimaryColorAlphaModulateResolved = false;
    uint32_t Texture0PrimaryColorAlphaModulateStageCount = 0;
    std::string Texture0PrimaryColorAlphaModulateSource;
    bool Texture0ConstantColorAlphaModulateResolved = false;
    uint32_t Texture0ConstantColorAlphaModulateStageCount = 0;
    std::string Texture0ConstantColorAlphaModulateSource;
    bool AlphaMultiplierResolved = false;
    uint32_t AlphaMultiplierStageCount = 0;
    float AlphaMultiplier = 1.0f;
    std::string AlphaMultiplierSource;
};

struct Oot3dNativeRenderTextureCoordState {
    uint32_t Slot = 0;
    bool Active = false;
    bool SelectedPrimary = false;
    bool Decoded = false;
    uint8_t MatrixMode = 0;
    uint8_t ReferenceCamera = 0;
    uint8_t MappingMethod = 0;
    uint8_t CoordinateIndex = 0;
    Vec2f Scale = { 1.0f, 1.0f };
    float Rotation = 0.0f;
    Vec2f Translation = { 0.0f, 0.0f };
    bool TransformAppliesToUv0 = false;
    bool Transformed = false;
    std::string Source;
    bool NativeMaterialAnimationApplied = false;
    std::string NativeMaterialAnimationSource;
    std::string NativeMaterialAnimationKind;
    uint32_t NativeMaterialAnimationComponentMask = 0;
    float NativeMaterialAnimationSampleFrame = 0.0f;
};

struct Oot3dNativeRenderSamplerState {
    bool Decoded = false;
    uint16_t MinFilter = 0;
    uint16_t MagFilter = 0;
    uint16_t WrapS = 0;
    uint16_t WrapT = 0;
    float LodBias = 0.0f;
    std::string Source;
};

struct Oot3dNativePicaMaterialLutInputRegisterState {
    uint32_t Register = 0;
    std::string RegisterName;
    uint32_t KnownBitMask = 0;
    uint32_t KnownValue = 0;
    bool HasKnownBits = false;
};

struct Oot3dNativePicaMaterialLutSamplerState {
    std::string Sampler;
    std::string PicaRole;
    bool Complete = false;
    bool AbsInputKnown = false;
    bool AbsInputEnabled = false;
    uint32_t AbsDisableBit = 0;
    uint32_t AbsBitShift = 0;
    bool LutInputKnown = false;
    uint32_t LutInputRaw = 0;
    bool LutInputRawHighBitSet = false;
    uint32_t LutInput = 0;
    std::string LutInputName;
    std::string LutInputExpression;
    bool LutInputShaderSemanticResolved = false;
    uint32_t LutInputBitShift = 0;
    bool ScaleKnown = false;
    uint32_t Scale = 0;
    float ScaleValue = 1.0f;
    uint32_t ScaleBitShift = 0;
};

struct Oot3dNativePicaMaterialLutInputState {
    bool Available = false;
    bool Complete = false;
    std::string SourceKind;
    std::string SourceStatus;
    uint32_t ConsumerAddress = 0;
    uint32_t PacketWordCount = 0;
    uint32_t ResolvedFieldCount = 0;
    std::array<Oot3dNativePicaMaterialLutInputRegisterState, 3> Registers;
    std::array<Oot3dNativePicaMaterialLutSamplerState, 7> Samplers;
    std::vector<std::string> ResolvedFields;
    std::vector<std::string> UnresolvedFields;
};

struct Oot3dNativePicaFragmentLightingConfigPayloadWordState {
    uint32_t Register = 0;
    std::string RegisterName;
    uint32_t Value = 0;
    bool Known = false;
    std::string Source;
};

struct Oot3dNativePicaFragmentLightingConfigState {
    bool Available = false;
    bool Complete = false;
    bool RuntimeOverrideGateResolved = false;
    bool RuntimeMaterialPrimarySourceResolved = false;
    bool RuntimeSecondaryModeSourceResolved = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string PrimarySourceStatus;
    std::string SecondaryModeSourceStatus;
    uint32_t EmitterAddress = 0;
    uint32_t MaterialSetupAddress = 0;
    uint32_t PreparedStateInitializerAddress = 0;
    uint32_t PacketRegister = 0;
    uint32_t PacketHeader = 0;
    uint32_t PayloadWordCount = 0;
    uint32_t KnownPayloadWordCount = 0;
    uint32_t NativeType = 0;
    uint32_t Flags = 0;
    uint32_t PrimaryEnable = 0;
    bool PrimaryEnableKnown = false;
    uint32_t PrimaryMode = 0;
    bool PrimaryModeKnown = false;
    uint32_t PrimaryRuntimeLaneValue = 0;
    bool PrimaryRuntimeLaneValueKnown = false;
    uint32_t PrimaryDefaultGatePayload0Candidate = 0;
    bool PrimaryDefaultGatePayload0CandidateKnown = false;
    std::string RuntimeOverrideGateResolutionSource;
    uint32_t RuntimeStateInitializerAddress = 0;
    uint32_t RuntimeStateCopyHelperAddress = 0;
    uint32_t RuntimeOverrideGateDefaultValue = 0;
    bool RuntimeOverrideGateDefaultKnown = false;
    bool RuntimeOverrideGateCopyPathKnown = false;
    uint32_t RuntimeSecondaryModeDefaultValue = 0;
    bool RuntimeSecondaryModeDefaultKnown = false;
    bool RuntimeSecondaryModeCopyPathKnown = false;
    bool RuntimeStateWriterScanResolved = false;
    bool RuntimeStateWritersClassifiedAsDrawLocal = false;
    bool RuntimeActiveOverrideProducerResolvedFromWriterScan = false;
    std::vector<uint32_t> RuntimeOverrideWriterAddresses;
    std::vector<uint32_t> RuntimeSecondaryModeWriterAddresses;
    uint32_t RuntimeGameplayDrawAddress = 0;
    uint32_t RuntimeGameplayDrawDispatcherAddress = 0;
    uint32_t RuntimeGameplayDrawDispatcherCallsiteAddress = 0;
    uint32_t RuntimeDrawEntrySubmitAddress = 0;
    std::vector<uint32_t> RuntimeDrawEntrySubmitCallsiteAddresses;
    uint32_t RuntimeDrawEntryFlagsOffset = 0;
    uint32_t RuntimeDrawEntryRenderContextPointerOffset = 0;
    uint32_t RuntimeDrawEntryCallbackOffset = 0;
    uint32_t RuntimeDrawEntryVisibilityStateOffset = 0;
    uint32_t RuntimeDrawEntrySubmittedByteOffset = 0;
    uint32_t RuntimeDrawEntryFadeCounterOffset = 0;
    uint32_t RuntimeDrawEntryFadeLimitOffset = 0;
    uint32_t RuntimeDrawEntryOverrideGateFlagMask = 0;
    uint32_t RuntimeDrawEntryOverrideGateForceFullFlagMask = 0;
    bool RuntimeDrawEntrySubmitRouteResolved = false;
    bool RuntimeDrawEntryOverrideGateRuleResolved = false;
    bool RuntimeDrawEntryRoutePromotesActiveMaterialOverride = false;
    uint32_t RuntimeSubmitManagerVtableAddress = 0;
    uint32_t RuntimeSubmitManagerMaterialConfigSlotOffset = 0;
    uint32_t RuntimeSubmitManagerMaterialConfigAddress = 0;
    uint32_t RuntimeSubmitManagerMaterialStateSlotOffset = 0;
    uint32_t RuntimeSubmitManagerMaterialStateAddress = 0;
    bool RuntimeSubmitManagerMaterialRouteResolved = false;
    bool RuntimeSubmitManagerMaterialRouteResolvesActiveOverrideGate = false;
    uint32_t RuntimeMaterialLaneDispatchAddress = 0;
    uint32_t RuntimeMaterialLaneDispatchLoopEndAddress = 0;
    uint32_t RuntimeMaterialLaneDispatchCmbMeshMaterialLaneByteOffset = 0;
    uint32_t RuntimeMaterialLaneDispatchMaterialLaneStrideBytes = 0;
    bool RuntimeMaterialLaneDispatchResolved = false;
    bool RuntimeMaterialLaneDispatchPromotesActiveOverrideGate = false;
    std::string RuntimeMaterialLaneDispatchSource;
    uint32_t SecondaryEnable = 0;
    bool SecondaryEnableKnown = false;
    uint32_t SecondaryMode = 0;
    bool SecondaryModeKnown = false;
    uint32_t SecondaryModeCmbCandidate = 0;
    bool SecondaryModeCmbCandidateKnown = false;
    uint32_t SecondaryParam = 0;
    bool SecondaryParamKnown = false;
    uint32_t SecondaryTypeSelector = 0;
    bool SecondaryTypeSelectorKnown = false;
    uint32_t SecondaryTypeRegisterValue = 0;
    bool SecondaryTypeRegisterValueKnown = false;
    bool SecondaryTypeDisabled = false;
    uint32_t PrimaryCmbBlendGate = 0;
    bool PrimaryCmbBlendGateKnown = false;
    uint32_t AuxByte = 0;
    bool AuxByteKnown = false;
    int32_t AuxHalfword = 0;
    bool AuxHalfwordKnown = false;
    std::array<Oot3dNativePicaFragmentLightingConfigPayloadWordState, 4> PayloadWords;
    std::vector<std::string> ResolvedFields;
    std::vector<std::string> UnresolvedFields;
};

struct Oot3dNativeRenderMaterialState {
    bool Textured = false;
    int32_t TextureIndex = -1;
    uint32_t TextureMapperSlot = 0;
    std::string TextureBindingSource;
    bool NativeSamplerStateDecoded = false;
    uint16_t NativeSamplerMinFilter = 0;
    uint16_t NativeSamplerMagFilter = 0;
    uint16_t NativeSamplerWrapS = 0;
    uint16_t NativeSamplerWrapT = 0;
    float NativeSamplerLodBias = 0.0f;
    std::string NativeSamplerStateSource;
    std::array<int32_t, 3> TextureMapperTextureIndices = { -1, -1, -1 };
    std::array<Oot3dNativeRenderSamplerState, 3> TextureMapperSamplerStates;
    int32_t SecondaryTextureIndex = -1;
    uint32_t SecondaryTextureMapperSlot = 1;
    std::string SecondaryTextureBindingSource;
    int32_t TertiaryTextureIndex = -1;
    uint32_t TertiaryTextureMapperSlot = 2;
    std::string TertiaryTextureBindingSource;
    bool NativeMaterialAnimationApplied = false;
    std::string NativeMaterialAnimationSource;
    std::string NativeMaterialAnimationRole;
    std::string NativeMaterialAnimationTextureName;
    int32_t NativeMaterialAnimationTextureFrameIndex = -1;
    uint32_t NativeMaterialAnimationFrame = 0;
    bool NativeMaterialAnimationTextureApplied = false;
    bool NativeMaterialAnimationColorApplied = false;
    std::string NativeMaterialAnimationColorKind;
    int32_t NativeMaterialAnimationColorSelector = -1;
    uint32_t NativeMaterialAnimationColorComponentMask = 0;
    float NativeMaterialAnimationColorSampleFrame = 0.0f;
    ColorRgba8 NativeMaterialAnimationColorValue = { 0, 0, 0, 0 };
    bool NativeRuntimeMaterialColorOverrideApplied = false;
    std::string NativeRuntimeMaterialColorOverrideSource;
    int32_t NativeRuntimeMaterialColorOverrideSlot = -1;
    ColorRgba8 NativeRuntimeMaterialColorOverrideValue = { 0, 0, 0, 0 };
    bool NativeRuntimeVertexAlphaBlend = false;
    bool NativeKankyoLayerBlendAlphaApplied = false;
    uint8_t NativeKankyoLayerBlendAlpha = 255;
    std::string NativeKankyoLayerBlendAlphaSource;
    bool NativeMaterialAnimationTextureTransformApplied = false;
    std::string NativeMaterialAnimationTextureTransformKind;
    int32_t NativeMaterialAnimationTextureTransformSelector = -1;
    uint32_t NativeMaterialAnimationTextureTransformComponentMask = 0;
    float NativeMaterialAnimationTextureTransformSampleFrame = 0.0f;
    bool VertexColorModulatesTexture = false;
    bool NativePicaLightingApplied = false;
    bool NativePicaDirectionalLightingApplied = false;
    bool NativePicaVertexLightingApplied = false;
    bool NativePicaHemisphereLightingApplied = false;
    bool NativePicaCmbVShaderLightingAccumulatorApplied = false;
    uint32_t NativePicaCmbVShaderLightingActiveLightCount = 0;
    uint32_t NativePicaCmbVShaderLightingAmbientSlotCount = 0;
    double NativePicaCmbVShaderLightingAlphaScale = 1.0;
    std::string NativePicaCmbVShaderLightingAccumulatorSource;
    std::string NativePicaCmbVShaderLightingAmbientSlotSource;
    bool NativePicaPrimaryColorPreLightingScaleApplied = false;
    bool NativePicaSelfShadowCandidate = false;
    bool NativePicaSelfShadowApplied = false;
    bool NativePicaShadow2dMaterialTextureProjectionInputCandidate = false;
    bool NativePicaShadow2dMaterialTextureProjectionInputDecoded = false;
    bool NativePicaShadow2dTexCoord0WInputCandidate = false;
    bool NativePicaShadow2dTexCoord0WInputDecoded = false;
    bool NativePicaUnlitTextureEnvRouteDecoded = false;
    bool NativePicaUnlitTextureEnvRouteRequiresNativeColor = false;
    bool NativePicaUnlitTextureEnvRouteNativeColorAvailable = false;
    bool NativePicaUnlitTextureEnvRouteApplied = false;
    bool NativePicaFogOverrideDecoded = false;
    bool NativePicaFogEnabled = false;
    std::string NativePicaFogOverrideSource;
    bool NativeRuntimeMaterialLaneDecoded = false;
    int32_t NativeRuntimeMaterialLaneIndex = -1;
    uint32_t NativeRuntimeMaterialLaneStrideBytes = 0;
    size_t NativePicaSelfShadowVertexCount = 0;
    size_t NativePicaSelfShadowOccludedVertexCount = 0;
    std::string NativePicaLightingApplication;
    std::string NativePicaLightingApplicationSource;
    std::string NativePicaVertexHemisphereLightingSource;
    std::string NativePicaPrimaryColorPreLightingScaleSource;
    Vec3f NativePicaPrimaryColorPreLightingScale = { 1.0f, 1.0f, 1.0f };
    std::string NativePicaSelfShadowApplicationSource;
    std::string NativePicaShadow2dMaterialTextureProjectionInputSource;
    std::string NativePicaShadow2dTexCoord0WInputSource;
    std::string NativePicaUnlitTextureEnvRouteSource;
    std::string NativeRuntimeMaterialLaneSource;
    bool NativePicaVertexLightingDeferred = false;
    bool NativePicaHemisphereLightingDeferred = false;
    bool NativePicaVertexHemisphereVectorResolved = false;
    bool NativePicaVertexHemisphereVectorPending = false;
    bool NativePicaMaterialLutInputPacketAvailable = false;
    bool NativePicaMaterialLutInputPacketComplete = false;
    bool NativePicaMaterialLutInputPacketUsedForFragmentLighting = false;
    bool NativePicaMaterialLutInputEvaluationApplied = false;
    bool NativePicaMaterialLutInputEvaluationPending = false;
    bool NativePicaMaterialLutInputEvaluationRequired = false;
    bool NativePicaMaterialLutInputEvaluationCulledAsUnused = false;
    bool NativePicaBumpModeAvailable = false;
    bool NativePicaBumpModeRecognized = false;
    uint32_t NativePicaBumpMode = 0;
    bool NativePicaBumpModeActive = false;
    bool NativePicaBumpModeBackendPending = false;
    bool NativePicaBumpTextureUnitRecognized = false;
    uint32_t NativePicaBumpTextureUnit = 0;
    int32_t NativePicaBumpTextureIndex = -1;
    bool NativePicaBumpNormalMapBackendSupported = false;
    bool NativePicaBumpNormalMapApplied = false;
    std::string NativePicaBumpModeSource;
    std::string NativePicaBumpTextureSource;
    bool NativeMaterialLightingFlag3Available = false;
    bool NativeMaterialLightingFlag3Active = false;
    bool NativeMaterialLightingFlag3BackendPending = false;
    std::string NativeMaterialLightingFlag3Source;
    bool NativePicaFragmentLightingConfigPacketAvailable = false;
    bool NativePicaFragmentLightingConfigPacketComplete = false;
    bool NativePicaFragmentLightingConfigRuntimeOverridePending = false;
    uint32_t NativePicaFragmentLightingConfigKnownPayloadWordCount = 0;
    std::string NativePicaVertexHemisphereVectorSource;
    bool NativePicaVertexHemisphereActorVsColorPacketApplied = false;
    std::string NativePicaVertexHemisphereColorSource;
    ColorRgba8 NativePicaVertexHemisphereAmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 NativePicaVertexHemisphereDiffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 NativePicaVertexHemisphereDiffuse1Color = { 0, 0, 0, 255 };
    bool NativePicaEffectiveMaterialDiffuseResolved = false;
    bool NativePicaEffectiveMaterialDiffuseUsesAmbient = false;
    std::string NativePicaEffectiveMaterialDiffuseSource;
    ColorRgba8 NativePicaEffectiveMaterialDiffuseColor = { 0, 0, 0, 255 };
    Oot3dNativePicaLightingBatchDiagnostics NativePicaLightingDiagnostics;
    std::string NativePicaMaterialLutInputApplicationSource;
    std::string NativePicaFragmentLightingConfigApplicationSource;
    bool NativeMaterialAvailable = false;
    uint32_t NativeMaterialRawSize = 0;
    uint64_t NativeMaterialRawFnv1a64 = 0;
    bool FragmentLightingEnabled = false;
    bool VertexLightingEnabled = false;
    bool HemisphereLightingEnabled = false;
    bool HemisphereOcclusionEnabled = false;
    bool RawTextureStageSelectorDecoded = false;
    uint32_t RawTextureStageCount = 0;
    std::array<int16_t, kOot3dCmbMaterialTextureEnvStageCount> RawTextureStageSlots = { -1, -1, -1, -1, -1, -1 };
    bool RawTextureStageCountMatchesMappers = false;
    bool RawTextureStageCountExceedsMappers = false;
    bool PostMaterialTextureEnvTableDecoded = false;
    bool PostMaterialTextureEnvTableDerivedFromLanePointer = false;
    uint32_t PostMaterialTextureEnvTableSourceOffset = 0;
    uint32_t PostMaterialTextureEnvTableRecordSize = 0;
    uint32_t PostMaterialTextureEnvTableRecordCount = 0;
    std::string PostMaterialTextureEnvTableSource;
    std::array<bool, kOot3dCmbMaterialTextureEnvStageCount> PostMaterialTextureEnvStageResolved = {};
    std::array<uint32_t, kOot3dCmbMaterialTextureEnvStageCount> PostMaterialTextureEnvStageSourceOffsets = {};
    CmbMaterialLightingBlock MaterialLightingBlock;
    Oot3dNativePicaMaterialLutInputState MaterialPicaLutInput;
    Oot3dNativePicaFragmentLightingConfigState MaterialFragmentLightingConfig;
    bool MaterialColorsDecoded = false;
    ColorRgba8 EmissionColor;
    ColorRgba8 AmbientColor;
    ColorRgba8 DiffuseColor;
    ColorRgba8 Specular0Color;
    ColorRgba8 Specular1Color;
    std::array<ColorRgba8, kOot3dCmbMaterialConstantColorCount> ConstantColors;
    uint32_t NativeMaterialTextureStageCandidateStart = kOot3dCmbMaterialTextureStageCandidateStart;
    uint32_t NativeMaterialTextureStageCandidateEnd = kOot3dCmbMaterialTextureStageCandidateEnd;
    std::vector<Oot3dNativeRawMaterialWord> NativeMaterialTextureStageCandidateNonzeroWords;
    Oot3dNativeRenderTextureEnvState TextureEnv;
    uint32_t TextureEnvTableRecordCount = 0;
    bool TextureEnvStageIndicesCovered = false;
    uint32_t TextureEnvStageRecordCount = 0;
    int32_t TextureEnvSelectedStageIndex = -1;
    std::vector<Oot3dNativeRenderTextureEnvState> TextureEnvStages;
    Oot3dNativeRenderTextureEnvProgram TextureEnvProgram;
    std::vector<Oot3dNativeRenderTextureCoordState> TextureCoords;
    Oot3dNativeRenderTextureCoordState SelectedTextureCoord;
    bool SelectedTextureCoordDecoded = false;
    bool TextureBindingResolvedFromRawStageSelector = false;
    bool NativeMaterialCombinerRequiresDecoder = false;
    bool AlphaTest = false;
    uint8_t AlphaReference = 0;
    uint16_t AlphaFunction = 0;
    uint8_t CmbCullFace = 3;
    Oot3dNativePicaCullMode PicaCullMode = Oot3dNativePicaCullMode::KeepAll;
    bool DepthTest = true;
    bool DepthWrite = true;
    uint16_t DepthFunction = 0;
    uint32_t BlendMode = 0;
    uint16_t BlendSrc = 0;
    uint16_t BlendDst = 0;
    uint16_t BlendEquation = 0;
    uint16_t ColorBlendSrc = 0;
    uint16_t ColorBlendDst = 0;
    uint16_t ColorBlendEquation = 0;
    float BlendColorAlpha = 0.0f;
    bool NativeRenderStateDecoded = false;
    bool NativeBlendStateEnabled = false;
    bool NativeBlendFactorsSupported = false;
    bool NativeBlendEquationSupported = false;
    bool NativeBlendStateSupported = false;
    bool TextureHasNativeAlpha = false;
};

struct Oot3dNativeRenderBatch {
    uint32_t MeshIndex = 0;
    uint32_t ShapeIndex = 0;
    uint8_t VisibilityId = 0;
    int32_t MaterialIndex = -1;
    uint32_t PrimitiveIndex = 0;
    uint16_t SkinningMode = 0;
    uint16_t NativeCmbAttributeFlags = 0;
    uint16_t NativeCmbConstantAttributeFlags = 0;
    Oot3dNativeRenderMaterialState Material;
    std::vector<Oot3dNativeRenderVertex> Vertices;

    size_t TriangleCount() const;
};

Matrix4f Oot3dNativeRenderIdentityMatrix();

enum class Oot3dNativeSubmitQueue : uint8_t {
    Primary = 0,
    Small = 1,
};

enum class Oot3dNativeKankyoModelRole : uint8_t {
    None = 0,
    SkyBackground = 1,
    Sun = 2,
    Cloud = 3,
    Star = 4,
    SunHalo = 5,
};

struct Oot3dNativeRenderModel {
    std::string Source;
    std::string Name;
    nlohmann::json Diagnostics;
    Oot3dDemoBounds LocalBounds;
    Oot3dDemoBounds Bounds;
    Matrix4f ModelToWorld = Oot3dNativeRenderIdentityMatrix();
    Oot3dNativeSubmitQueue SubmitQueue = Oot3dNativeSubmitQueue::Primary;
    Oot3dNativeKankyoModelRole NativeKankyoRole = Oot3dNativeKankyoModelRole::None;
    bool TransformBakedIntoVertices = true;
    uint32_t NativeCmbSkeletonBoneCount = 0;
    bool NativeCmbMeshPassSplitIndexDecoded = false;
    uint32_t NativeCmbMeshPassSplitIndex = 0;
    bool NativeCmbResourceVisibilityApplied = false;
    std::vector<uint8_t> NativeCmbResourceVisibility;
    bool NativeRuntimePicaLightingBaked = false;
    uint64_t NativeGeometryId = 0;
    uint64_t NativeGeometryContentVersion = 1;
    bool NativeVertexDataCacheable = false;
    int32_t NativeKankyoProfileIndex = -1;
    int32_t NativeKankyoLayerAlpha = 255;
    bool NativeKankyoLayerBlendAlphaApplied = false;
    std::string NativeKankyoLayerBlendAlphaSource;
    bool NativeKankyoProfileAttributeBlendApplied = false;
    int32_t NativeKankyoProfileBlendCurrentIndex = -1;
    int32_t NativeKankyoProfileBlendNextIndex = -1;
    float NativeKankyoProfileBlendWeight = 0.0f;
    std::string NativeKankyoProfileAttributeBlendSource;
    bool NativeKankyoRuntimeTransformMaterialized = false;
    std::string NativeKankyoRuntimeTransformSource;
    std::vector<Oot3dNativeRenderTexture> Textures;
    Oot3dNativeRenderLutSection Luts;
    size_t TextureEnvTableRecordCount = 0;
    std::vector<Oot3dNativeRenderBatch> Batches;

    size_t VertexCount() const;
    size_t TriangleCount() const;
    size_t TexturedBatchCount() const;
    size_t UploadableTextureCount() const;
};

struct Oot3dNativeRenderModelBuildOptions {
    Oot3dDemoVec3 Offset;
    double Scale = 1.0;
    bool BakeTransformIntoVertices = true;
    const std::vector<uint32_t>* SelectedMeshIndices = nullptr;
    const std::vector<uint8_t>* ResourceVisibility = nullptr;
    const CsabPose* Pose = nullptr;
    const std::vector<Matrix4f>* SkinTransforms = nullptr;
};

struct Oot3dNativePicaLightingDebugState {
    bool Available = false;
    bool SemanticTableAvailable = false;
    std::string SourceKind;
    std::string Mode = "native_pica_lighting_debug";
    std::string RecordSelector;
    std::string AmbientColorSource;
    std::string DiffuseColorSource;
    int ActiveSetupIndex = -1;
    int RecordIndex = -1;
    int RecordOffset = -1;
    int AmbientGroupIndex = -1;
    int DiffuseGroupIndex = -1;
    int AmbientColorOffset = -1;
    int DiffuseColorOffset = -1;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 DiffuseColor = { 0, 0, 0, 255 };
    ColorRgba8 DebugColor = { 0, 0, 0, 255 };
    bool ForceUntexturedBatches = false;
    bool PreserveVertexAlpha = true;
    size_t AppliedBatchCount = 0;
    size_t AppliedVertexCount = 0;
};

struct Oot3dNativePicaActorVsLightPacketState {
    bool Available = false;
    bool ColorPacketAvailable = false;
    bool VectorLayoutResolved = false;
    bool VectorOriginResolved = false;
    bool FinalPacketWriterResolved = false;
    bool RuntimeFinalPacketEmitterResolved = false;
    bool CompactPayloadSourceResolved = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string ColorSource;
    std::string AmbientColorSource;
    std::string Diffuse0ColorSource;
    std::string Diffuse1ColorSource;
    std::string PicaFogColorSource;
    std::string VectorLayoutSource;
    std::string VectorSourceStatus;
    std::string CompactPayloadSource;
    std::string CompactPayloadDirectionSource;
    std::string CompactPayloadColorSource;
    std::string SelectionSource;
    bool RuntimeTransitionColorBlendApplied = false;
    bool RuntimeTransitionModeColorBlendApplied = false;
    uint32_t RecordSelectionIndexDelta = 0;
    uint32_t PacketPrepAddress = 0;
    uint32_t RuntimeLightPacketPackAddress = 0;
    uint32_t RuntimeFinalPacketEmitterAddress = 0;
    uint32_t CompactPayloadProducerAddress = 0;
    uint32_t CompactPayloadConsumerHandlerAddress = 0;
    uint32_t RuntimeFinalPacketWordCount = 0;
    uint32_t RuntimeFinalPacketRegisterBase = 0;
    uint32_t SlotCount = 0;
    uint32_t SlotStrideBytes = 0;
    uint32_t CompactPayloadSlotCount = 0;
    uint32_t CompactPayloadSlotStrideBytes = 0;
    std::array<uint32_t, 2> CompactPayloadSlotOffsets = { 0, 0 };
    uint32_t CompactPayloadDirectionOffset = 0;
    uint32_t CompactPayloadColorOffset = 0;
    uint32_t CompactPayloadSizeBytes = 0;
    std::array<uint32_t, 2> CompactPayloadDirectionSourceOffsets = { 0, 0 };
    std::array<uint32_t, 2> CompactPayloadColorSourceOffsets = { 0, 0 };
    uint32_t CompactPayloadDirectionAngleBias = 0;
    std::array<float, 3> CompactPayloadDirectionScales = { 0.0f, 0.0f, 0.0f };
    std::array<uint32_t, 4> SourceColorPayloadOffsets = { 0, 0, 0, 0 };
    std::array<uint32_t, 3> SourceVectorOffsets = { 0, 0, 0 };
    uint32_t EnableIntensityOffset = 0;
    std::array<uint32_t, 3> PreparedVectorOffsets = { 0, 0, 0 };
    uint32_t PreparedIntensityOffset = 0;
    uint32_t RuntimeSourceVectorStaticUpdateAddress = 0;
    uint32_t RuntimeSourceVectorDynamicSubmitCallbackAddress = 0;
    uint32_t RuntimeSourceVectorDynamicTransformUpdateSkipFlagMask = 0;
    uint32_t RuntimeSourceVectorStaticUpdateSkipFlagMask = 0;
    uint32_t RuntimeSourceVectorCommittedCopySkipFlagMask = 0;
    uint32_t RuntimeSourceVectorWorkingBlockOffset = 0;
    std::array<uint32_t, 3> RuntimeSourceVectorBaseVectorOffsets = { 0, 0, 0 };
    std::array<uint32_t, 3> RuntimeSourceVectorWorkingVectorSeedOffsets = { 0, 0, 0 };
    std::array<uint32_t, 3> RuntimeSourceVectorWorkingPacketSourceVectorOffsets = { 0, 0, 0 };
    uint32_t RuntimeSourceVectorWorkingBlockWordCount = 0;
    uint32_t RuntimeSourceVectorCommittedBlockOffset = 0;
    uint32_t RuntimeSourceVectorCommittedBlockWordCount = 0;
    uint32_t RuntimeLightPacketPackSourcePreparedVectorBaseOffset = 0;
    uint32_t RuntimeLightPacketPackSourcePreparedIntensityOffset = 0;
    uint32_t RuntimeLightPacketPackRequiredPreparedIntensityWord = 0;
    bool RuntimeLightPacketPackOutputRecordLayoutResolved = false;
    bool RuntimeLightPacketPackOutputFeedsFinalUploadEmitter = false;
    bool RuntimeLightPacketPackNegatesPreparedVector = false;
    std::array<uint32_t, 2> RuntimeLightPacketPackOutputFinalRecordDirectionPackedWordOffsets = { 0, 0 };
    std::array<uint32_t, 6> RuntimeFinalUploadCopiedWordSourceOffsets = { 0, 0, 0, 0, 0, 0 };
    std::array<uint32_t, 6> RuntimeFinalUploadCopiedWordPacketWordIndices = { 0, 0, 0, 0, 0, 0 };
    int EnvironmentRecordIndex = -1;
    int EnvironmentRecordOffset = -1;
    int RecordIndex = -1;
    int RecordOffset = -1;
    int RuntimeTransitionColorFromRecordIndex = -1;
    int RuntimeTransitionColorToRecordIndex = -1;
    int RuntimeTransitionColorTargetFromRecordIndex = -1;
    int RuntimeTransitionColorTargetToRecordIndex = -1;
    double RuntimeTransitionColorAngleWeight = 0.0;
    double RuntimeTransitionColorModeWeight = 0.0;
    int CompactPayloadActiveAngle = -1;
    bool PicaFogColorAvailable = false;
    uint32_t PicaFogColorOffset = 0;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse1Color = { 0, 0, 0, 255 };
    ColorRgba8 PicaFogColor = { 0, 0, 0, 255 };
    Vec3f CompactPayloadSlot0Direction = { 0.0f, 1.0f, 0.0f };
    Vec3f CompactPayloadSlot1Direction = { 0.0f, -1.0f, 0.0f };
    std::array<int, 3> CompactPayloadSlot0DirectionS8 = { 0, 0, 0 };
    std::array<int, 3> CompactPayloadSlot1DirectionS8 = { 0, 0, 0 };
    std::array<uint8_t, 6> CompactPayloadSlot0Bytes = { 0, 0, 0, 0, 0, 0 };
    std::array<uint8_t, 6> CompactPayloadSlot1Bytes = { 0, 0, 0, 0, 0, 0 };
};

struct Oot3dNativePicaResolvedRuntimeLightSetting {
    bool Available = false;
    bool UsedForRender = false;
    bool RuntimeColorAddendContractResolved = false;
    bool RuntimeAmbientColorAddendResolved = false;
    bool RuntimeLightColorAddendResolved = false;
    bool RuntimeFinalAmbientColorFormulaResolved = false;
    bool RuntimeFinalLightColorFormulaResolved = false;
    bool RuntimeFinalAmbientColorUsedForRender = false;
    bool RuntimeFinalLightColorUsedForRender = false;
    bool RuntimeFogColorAddendResolved = false;
    bool RuntimeFinalFogColorFormulaResolved = false;
    bool RuntimeFinalFogColorUsedForRender = false;
    bool RuntimeFogDistanceContractResolved = false;
    bool RuntimeFogDistanceAddendsResolved = false;
    bool RuntimeCameraFarUsedForRender = false;
    bool RuntimeFogDistancesUsedForRender = false;
    bool TransitionTableAvailable = false;
    bool TransitionTableBranchSupported = false;
    bool TransitionTableBranchApplied = false;
    bool DirectCurrentRecordBranchApplied = false;
    bool DirectTargetRecordBranchApplied = false;
    bool ModeBlendActive = false;
    std::string SourceKind;
    std::string Branch;
    std::string SourceStatus;
    std::string RuntimeColorAddendSource;
    std::string RuntimeFinalAmbientColorSource;
    std::string RuntimeFinalAmbientColorStatus;
    std::string RuntimeFinalLightColorSource;
    std::string RuntimeFinalLightColorStatus;
    std::string RuntimeFinalFogColorSource;
    std::string RuntimeFinalFogColorStatus;
    std::string RuntimeFogDistanceSource;
    std::string RuntimeFogDistanceStatus;
    std::string ActiveAngleSource;
    std::string ModeStateSource;
    std::string DirectTargetScenePath;
    std::filesystem::path DirectTargetSceneZsiPath;
    std::string DirectTargetSceneSourceStatus;
    bool DirectTargetSceneMatchesActiveScene = true;
    bool DirectTargetSceneLightingDecoded = false;
    bool ActiveAngleBootstrappedFromPlayerStart = false;
    bool ModeStateBootstrappedFromCodeBinFallback = false;
    int ActiveSetupIndex = -1;
    int ActiveAngle = -1;
    int CurrentMode = -1;
    int TargetMode = -1;
    int CurrentEntryIndex = -1;
    int TargetEntryIndex = -1;
    int CurrentFromLightSettingIndex = -1;
    int CurrentToLightSettingIndex = -1;
    int TargetFromLightSettingIndex = -1;
    int TargetToLightSettingIndex = -1;
    int CurrentRecordIndex = -1;
    int PreviousRecordIndex = -1;
    int TargetRecordIndex = -1;
    int TargetSetupIndex = -1;
    int TargetLightSettingRawIndex = -1;
    int TargetLightSettingIndex = -1;
    int CurrentRecordOffset = -1;
    int PreviousRecordOffset = -1;
    int TargetRecordOffset = -1;
    uint32_t ActiveAngleWorkingStateAddress = 0;
    uint32_t ActiveAngleWorkingHalfwordOffset = 0;
    uint32_t ActiveAngleOutputStateAddress = 0;
    uint32_t ActiveAngleOutputHalfwordOffset = 0;
    uint32_t ModeStateBasePlayOffset = 0;
    uint32_t ModeCurrentRelativeOffset = 0;
    uint32_t ModeTargetRelativeOffset = 0;
    uint32_t ModeBlendActiveRelativeOffset = 0;
    uint32_t ModeBlendRemainingHalfwordRelativeOffset = 0;
    uint32_t ModeBlendDurationHalfwordRelativeOffset = 0;
    uint32_t AmbientPreAddendRecordOffset = 0;
    uint32_t Light0PreAddendRecordOffset = 0;
    uint32_t Light1PreAddendRecordOffset = 0;
    uint32_t AmbientPreAddendStateOffset = 0;
    uint32_t Light0PreAddendStateOffset = 0;
    uint32_t Light1PreAddendStateOffset = 0;
    uint32_t AmbientColorAddendStateOffset = 0;
    uint32_t LightColorAddendStateOffset = 0;
    uint32_t FinalAmbientColorOutputOffset = 0;
    uint32_t FinalAmbientColorPlayOffset = 0;
    uint32_t Light0FinalPayloadColorStateOffset = 0;
    uint32_t Light1FinalPayloadColorStateOffset = 0;
    uint32_t FogPreAddendRecordOffset = 0;
    uint32_t FogPreAddendStateOffset = 0;
    uint32_t FogColorAddendStateOffset = 0;
    uint32_t FinalFogColorOutputOffset = 0;
    uint32_t FinalFogColorPlayOffset = 0;
    uint32_t CameraFarRecordOffset = 0;
    uint32_t FogFarRecordOffset = 0;
    uint32_t FogNearRecordOffset = 0;
    uint32_t CameraFarOutputOffset = 0;
    uint32_t FogFarOutputOffset = 0;
    uint32_t FogNearOutputOffset = 0;
    uint32_t CameraFarPlayOffset = 0;
    uint32_t FogFarPlayOffset = 0;
    uint32_t FogNearPlayOffset = 0;
    uint32_t FogNearAddendStateOffset = 0;
    uint32_t FogFarAddendStateOffset = 0;
    double AngleWeight = 0.0;
    double ModeBlendWeight = 0.0;
    double DirectBlendWeight = 1.0;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    Vec3f Light0Vector = { 0.0f, 1.0f, 0.0f };
    ColorRgba8 Light0Color = { 0, 0, 0, 255 };
    Vec3f Light1Vector = { 0.0f, 1.0f, 0.0f };
    ColorRgba8 Light1Color = { 0, 0, 0, 255 };
    ColorRgba8 AmbientPreAddendColor = { 0, 0, 0, 255 };
    ColorRgba8 Light0PreAddendColor = { 0, 0, 0, 255 };
    ColorRgba8 Light1PreAddendColor = { 0, 0, 0, 255 };
    std::array<int, 3> AmbientColorAddendI16 = { 0, 0, 0 };
    std::array<int, 3> LightColorAddendI16 = { 0, 0, 0 };
    ColorRgba8 FogPreAddendColor = { 0, 0, 0, 255 };
    std::array<int, 3> FogColorAddendI16 = { 0, 0, 0 };
    ColorRgba8 FinalFogColor = { 0, 0, 0, 255 };
    ColorRgba8 FogColor = { 0, 0, 0, 255 };
    double CameraFar = 0.0;
    double FogFarPreAddend = 0.0;
    double FogFarAddend = 0.0;
    double FogFar = 0.0;
    double FogNearInterpolated = 0.0;
    int FogNearPreAddend = 0;
    int FogNearAddend = 0;
    int FogNear = 0;
};

struct Oot3dNativePicaRuntimeUvTransformState {
    bool Available = false;
    bool UsedForRender = false;
    bool UploadsNativePicaVshUniforms = false;
    bool DirectlyWritesPacketPrepSource = false;
    std::string SourceKind;
    std::string SourceStatus;
    uint32_t RuntimeSubmitFunctionAddress = 0;
    uint32_t RuntimeRecordTextureLightFunctionAddress = 0;
    uint32_t RuntimeUvTransformBuildFunctionAddress = 0;
    uint32_t RuntimeUvTransformSourceBuilderAddress = 0;
    uint32_t RuntimeUvTransformCopyHelperAddress = 0;
    uint32_t RuntimeUvTransformPrimaryUploadHelperAddress = 0;
    uint32_t RuntimeUvTransformSecondaryUploadHelperAddress = 0;
    uint32_t SourceCountOffset = 0;
    uint32_t SourceRecordBaseOffset = 0;
    uint32_t SourceRecordStrideBytes = 0;
    uint32_t OutputSlotCount = 0;
    uint32_t OutputSlotStrideBytes = 0;
    uint32_t OutputWordCount = 0;
    uint32_t PrimaryUploadRegister = 0;
    uint32_t PrimaryUploadWordCount = 0;
    uint32_t SecondaryUploadRegisterBase = 0;
    uint32_t SecondaryUploadWordCount = 0;
    uint32_t FirstSlotOverrideOwnerOffset = 0;
    uint32_t FirstSlotOverrideGateByteOffset = 0;
    uint32_t FirstSlotOverrideSourcePointerOffset = 0;
    uint32_t FirstSlotOverridePayloadOffset = 0;
};

struct Oot3dNativePicaRuntimeSubmitDescriptorState {
    bool Available = false;
    bool UsedForRender = false;
    bool DirectlyWritesPacketPrepSource = false;
    std::string SourceKind;
    std::string SourceStatus;
    uint32_t RuntimeSubmitFunctionAddress = 0;
    uint32_t RuntimeRecordTextureLightFunctionAddress = 0;
    uint32_t DescriptorPointerWordIndex = 0;
    uint32_t TextureObjectPointerWordIndex = 0;
    uint32_t LightRecordTablePointerWordIndex = 0;
    uint32_t Color0ByteOffset = 0;
    uint32_t Color1ByteOffset = 0;
    uint32_t ColorComponentCount = 0;
    uint32_t ActiveLightSlotCountOffset = 0;
    uint32_t ActiveLightSlotIndexTableOffset = 0;
    uint32_t ActiveLightSlotIndexStrideBytes = 0;
    uint32_t LightRecordStrideBytes = 0;
    uint32_t LightRecordColorOp0HalfwordOffset = 0;
    uint32_t LightRecordColorOp1HalfwordOffset = 0;
    uint32_t LightRecordDisabledColorOpValue = 0;
    uint32_t LightSlotCount = 0;
    uint32_t TextureLightCountOffset = 0;
    uint32_t TextureLightUvSourceCountOffset = 0;
    uint32_t TextureLightTextureRefBaseOffset = 0;
    uint32_t TextureLightSourceRecordBaseOffset = 0;
    uint32_t TextureLightRecordStrideBytes = 0;
    uint32_t TextureLightOutputTextureIdHalfwordOffset = 0;
    uint32_t TextureLightOutputSamplerWordBaseOffset = 0;
    uint32_t TextureLightOutputModeWordBaseOffset = 0;
    uint32_t TextureLightOutputWordStrideBytes = 0;
};

struct Oot3dNativeFogRuntimeDefaults {
    bool Available = false;
    float SourceRgbScaleR = 0.0f;
    float SourceRgbScaleG = 0.0f;
    float SourceRgbScaleB = 0.0f;
    float SourceNear = 0.0f;
    float SourceFar = 0.0f;
    uint8_t SourceRebuildGate = 0;
    uint8_t SourceCurveMode = 0;
    float ProjectionNear = 0.0f;
    float ProjectionFar = 0.0f;
    float SceneProjectionFar = 0.0f;
    std::string SourceKind;
};

struct Oot3dNativePicaFogState {
    bool Available = false;
    bool UsedForRender = false;
    bool CodeBinSourceDecoded = false;
    bool ShaderSupported = false;
    bool LutShaderSupported = false;
    bool FogEnabled = false;
    bool FogFlip = false;
    bool RuntimeFinalFogColorFormulaResolved = false;
    bool RuntimeFogColorAddendResolved = false;
    bool RuntimeFinalFogColorUsedForRender = false;
    bool RuntimeFogDistanceContractResolved = false;
    bool RuntimeFogDistanceAddendsResolved = false;
    bool RuntimeFogDistancesUsedForRender = false;
    bool ProjectionRangeAvailable = false;
    bool SceneProjectionFarDecoded = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string ColorSource;
    std::string LutSource;
    std::string BlockedReason;
    ColorRgba8 Color = { 0, 0, 0, 255 };
    ColorRgba8 PreAddendColor = { 0, 0, 0, 255 };
    std::array<int, 3> ColorAddendI16 = { 0, 0, 0 };
    ColorRgba8 FinalColor = { 0, 0, 0, 255 };
    uint32_t ModeRaw = 0;
    uint32_t Mode = 0;
    uint32_t ColorRegister = 0;
    uint32_t PreAddendRecordOffset = 0;
    uint32_t PreAddendStateOffset = 0;
    uint32_t ColorAddendStateOffset = 0;
    uint32_t FinalOutputOffset = 0;
    uint32_t FinalPlayOffset = 0;
    uint32_t CameraFarPlayOffset = 0;
    uint32_t FogFarPlayOffset = 0;
    uint32_t FogNearPlayOffset = 0;
    uint32_t ProjectionMatrixPlayOffset = 0;
    uint32_t ViewInitAddress = 0;
    uint32_t ViewDefaultNearLiteralAddress = 0;
    uint32_t ViewDefaultFarLiteralAddress = 0;
    uint32_t ViewUpdateAddress = 0;
    uint32_t ProjectionBuildAddress = 0;
    uint32_t SceneProjectionFarLiteralAddress = 0;
    uint32_t SceneProjectionMatrixOffset = 0;
    uint32_t LutIndexRegister = 0;
    uint32_t LutDataRegisterBase = 0;
    uint32_t FogPayloadRuntimeUpdateAddress = 0;
    uint32_t FogPayloadBuildAddress = 0;
    uint32_t FogPayloadDefaultSourceAddress = 0;
    uint32_t FogPayloadSourceFloat0Offset = 0;
    uint32_t FogPayloadSourceFloat1Offset = 0;
    uint32_t FogPayloadPackedTableOffset = 0;
    uint32_t FogPayloadPackedTableEntryCount = 0;
    float SourceRgbScaleR = 0.0f;
    float SourceRgbScaleG = 0.0f;
    float SourceRgbScaleB = 0.0f;
    float SourceNear = 0.0f;
    float SourceFar = 0.0f;
    float ProjectionNear = 0.0f;
    float ProjectionFar = 0.0f;
    float RuntimeCameraFar = 0.0f;
    float SceneProjectionFar = 0.0f;
    uint8_t SourceRebuildGate = 0;
    uint8_t SourceCurveMode = 0;
    std::array<uint32_t, 128> LutWords = {};
    uint32_t LutWordCount = 0;
};

struct Oot3dNativePicaViewportState {
    bool Available = false;
    bool CodeBinSourceDecoded = false;
    std::string SourceKind;
    std::string SourceStatus;
    uint32_t HalfWidthAddress = 0;
    uint32_t HalfHeightAddress = 0;
    float HalfWidth = 0.0f;
    float HalfHeight = 0.0f;
    float Aspect = 0.0f;
};

struct Oot3dNativePicaLightingRenderState {
    bool Available = false;
    bool SemanticTableAvailable = false;
    std::string SourceKind;
    std::string Mode = "native_pica_lighting_vertex_color";
    std::string RecordSelector;
    std::string RecordSelectorSource;
    std::string VertexColorFormula;
    std::string DirectionalFormula;
    std::string TextureCombiner;
    std::string TexturedBaseColorSource;
    std::string AmbientColorSource;
    std::string DiffuseColorSource;
    std::string Light1ColorSource;
    std::string MaterialLightingEnableSource;
    std::string MaterialLightingDeferredSource;
    std::string MaterialVertexHemisphereModelScope;
    std::string MaterialVertexHemisphereLightingSource;
    std::string VertexHemisphereLightingFormula;
    std::string VertexHemisphereLightingReference;
    std::string VertexHemisphereLightColorMode;
    std::string MaterialEmissionSource;
    std::string MaterialAmbientSource;
    std::string MaterialDiffuseSource;
    int AmbientColorOffset = -1;
    int DiffuseColorOffset = -1;
    int Light1ColorOffset = -1;
    int Light0VectorGroupIndex = -1;
    std::string Light0VectorSource;
    bool DirectionalVectorsUseModelSpace = false;
    std::string DirectionalVectorSpaceSource;
    int Light0VectorOffset = -1;
    Vec3f Light0Vector = { 0.0f, 1.0f, 0.0f };
    int Light1VectorGroupIndex = -1;
    std::string Light1VectorSource;
    int Light1VectorOffset = -1;
    bool RuntimeEnvironmentTimeInputAvailable = false;
    uint16_t RuntimeEnvironmentDayTime = 0;
    uint16_t RuntimeEnvironmentSkyboxTime = 0;
    int RuntimeEnvironmentTimeStartFrame = -1;
    std::string RuntimeEnvironmentTimeSourceKind;
    std::string RuntimeEnvironmentTimeSourceStatus;
    bool RuntimeEnvironmentLightModeInputAvailable = false;
    int RuntimeEnvironmentLightModeCurrent = -1;
    int RuntimeEnvironmentLightModeTarget = -1;
    bool RuntimeEnvironmentLightModeBlendActive = false;
    uint16_t RuntimeEnvironmentLightModeBlendRemaining = 0;
    uint16_t RuntimeEnvironmentLightModeBlendDuration = 0;
    double RuntimeEnvironmentLightModeBlendWeight = 0.0;
    int RuntimeEnvironmentLightModeStartFrame = -1;
    int RuntimeEnvironmentLightModeSourceActionId = -1;
    std::string RuntimeEnvironmentLightModeSourceKind;
    std::string RuntimeEnvironmentLightModeSourceStatus;
    bool RuntimeEnvironmentLightSettingInputAvailable = false;
    int RuntimeEnvironmentLightSettingRawIndex = -1;
    int RuntimeEnvironmentLightSettingTarget = -1;
    int RuntimeEnvironmentLightSettingSetupIndex = -1;
    int RuntimeEnvironmentLightSettingStartFrame = -1;
    uint32_t RuntimeEnvironmentLightSettingPlayTargetOffset = 0;
    uint32_t RuntimeEnvironmentLightSettingPlayBlendWeightOffset = 0;
    std::string RuntimeEnvironmentLightSettingScenePath;
    std::string RuntimeEnvironmentLightSettingSourceKind;
    std::string RuntimeEnvironmentLightSettingSourceStatus;
    Vec3f Light1Vector = { 0.0f, 1.0f, 0.0f };
    int ActiveSetupIndex = -1;
    int RecordIndex = -1;
    int RecordOffset = -1;
    int FloorPolygonIndex = -1;
    int FloorSurfaceType = -1;
    int FloorLightSettingRawIndex = -1;
    int FloorLightSettingIndex = -1;
    int AmbientGroupIndex = -1;
    int DiffuseGroupIndex = -1;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 DiffuseColor = { 0, 0, 0, 255 };
    ColorRgba8 Light1Color = { 0, 0, 0, 255 };
    Oot3dNativePicaResolvedRuntimeLightSetting ResolvedRuntimeLightSetting;
    Oot3dNativePicaActorVsLightPacketState ActorVsLightPacket;
    Oot3dNativePicaRuntimeUvTransformState RuntimeUvTransform;
    Oot3dNativePicaRuntimeSubmitDescriptorState RuntimeSubmitDescriptor;
    bool VertexHemisphereUniformTraceDecoded = false;
    bool VertexHemisphereUniformTraceUsedAsRuntimeSource = false;
    std::string VertexHemisphereUniformTraceSourceKind;
    std::string VertexHemisphereUniformTraceFormat;
    std::string VertexHemisphereUniformTraceFormula;
    int VertexHemisphereUniformTraceDrawIndex = -1;
    int VertexHemisphereUniformTraceCandidateDrawCount = 0;
    int VertexHemisphereUniformTraceCandidateVertexCount = 0;
    int VertexHemisphereAmbientUniformIndex = -1;
    int VertexHemisphereDiffuseUniformIndex = -1;
    int VertexHemisphereLightVectorUniformIndex = -1;
    ColorRgba8 VertexHemisphereAmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 VertexHemisphereDiffuseColor = { 0, 0, 0, 255 };
    ColorRgba8 VertexHemisphereSecondaryColor = { 0, 0, 0, 255 };
    Vec3f VertexHemisphereLightVector = { 0.0f, 1.0f, 0.0f };
    Vec3f VertexHemisphereNegatedLightVector = { 0.0f, -1.0f, 0.0f };
    bool VertexHemisphereWorldLightVectorDecoded = false;
    Vec3f VertexHemisphereWorldLightVector = { 0.0f, 1.0f, 0.0f };
    Vec3f VertexHemisphereWorldNegatedLightVector = { 0.0f, -1.0f, 0.0f };
    std::string VertexHemisphereRuntimeColorSource;
    std::string VertexHemisphereRuntimeVectorSource;
    ColorRgba8 VertexModulationColor = { 0, 0, 0, 255 };
    uint32_t DirectionalLightCount = 0;
    bool CmbVShaderLightingAccumulatorDecoded = false;
    uint32_t CmbVShaderLightingAccumulatorSlotCount = 0;
    std::string CmbVShaderLightingAccumulatorSource;
    bool DirectionalLightingApplied = false;
    bool VertexHemisphereLightingSupported = false;
    size_t NativeNormalVertexCount = 0;
    bool ModulateTexturedBatches = true;
    bool PreserveVertexAlpha = true;
    size_t AppliedBatchCount = 0;
    size_t AppliedTexturedBatchCount = 0;
    size_t AppliedVertexCount = 0;
    size_t AppliedVertexLightingBatchCount = 0;
    size_t AppliedHemisphereLightingBatchCount = 0;
    size_t MaterialLutInputPacketAvailableBatchCount = 0;
    size_t MaterialLutInputPacketCompleteBatchCount = 0;
    size_t MaterialLutInputFragmentLightingBatchCount = 0;
    size_t MaterialLutInputEvaluationAppliedBatchCount = 0;
    size_t MaterialLutInputEvaluationPendingBatchCount = 0;
};

struct Oot3dNativePicaShadowState {
    bool Available = false;
    bool SemanticTableAvailable = false;
    bool LightEnvShadowRegistersSupported = false;
    bool FragmentLightShadowFlagsSupported = false;
    bool ShadowTextureProjectionRegistersSupported = false;
    bool ShaderEquationSemanticsSupported = false;
    bool ShadowTextureSamplingSemanticsSupported = false;
    bool PrimaryRgbShadowTermSupported = false;
    bool ShadowLightVectorSourceSupported = false;
    bool SelfShadowCandidateRouteSupported = false;
    bool NativeGeometryOcclusionSupported = false;
    bool FullPrimaryLightContributionShadowSupported = false;
    bool Shadow2dTextureTypeSupported = false;
    bool Shadow2dBackendPassRequestSupported = false;
    bool Shadow2dVisualPassRequestSupported = false;
    bool Shadow2dMaterialTextureProjectionInputSupported = false;
    bool Shadow2dTexCoord0WInputSupported = false;
    bool Shadow2dEncodedDepthCompareSupported = false;
    bool Shadow2dProjectionRegisterValuesDecoded = false;
    bool Shadow2dProjectionRegisterTraceAvailable = false;
    bool Shadow2dDmpShadowZUniformsDecoded = false;
    bool Shadow2dDmpPerspectiveShadowDecoded = false;
    bool Shadow2dPicaTextureShadowRegisterDecoded = false;
    bool Shadow2dPicaFramebufferShadowRegisterDecoded = false;
    bool Shadow2dShaderRouteRegisterTraceDecoded = false;
    bool Shadow2dFragmentLightingEnableDecoded = false;
    bool Shadow2dLightingConfig0Decoded = false;
    bool Shadow2dLightingConfig1Decoded = false;
    bool Shadow2dShadowTextureParamDecoded = false;
    bool Shadow2dShadowTextureDimDecoded = false;
    bool Shadow2dShaderRouteMatchesPrimaryRgbShadowTerm = false;
    bool Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm = false;
    bool Shadow2dBackendShadowMapRenderTargetDecoded = false;
    bool Shadow2dBackendShadowMapPassImplemented = false;
    bool Shadow2dVisualPassReady = false;
    bool Shadow2dVisualPassUsesRuntimeN64AssetSubstitution = true;
    bool SelfShadowShaderRouteDecoded = false;
    bool UsesRuntimeN64AssetSubstitution = false;
    std::string SourceKind;
    std::string Mode = "native_pica_self_shadow";
    std::string Status;
    std::string LightEnvShadowAlphaName;
    std::string LightEnvShadowSelectorName;
    std::string LightEnvShadowPrimaryName;
    std::string LightEnvShadowSecondaryName;
    std::string FragmentLightShadowedNamePattern;
    std::string TextureShadowZScaleName;
    std::string TextureShadowZBiasName;
    std::string ShaderEquationSourceKind;
    std::string EnableShadowSource;
    std::string ShadowSelectorSource;
    std::string ShadowInvertSource;
    std::string ShadowPrimarySource;
    std::string ShadowSecondarySource;
    std::string ShadowAlphaSource;
    std::string PerLightShadowEnableSource;
    std::string ShadowSampleSource;
    std::string ShadowDefaultFormula;
    std::string ShadowInvertFormula;
    std::string PrimaryRgbFormula;
    std::string SecondaryRgbFormula;
    std::string AlphaFormula;
    std::string ShadowTextureSamplingSourceKind;
    std::vector<std::string> ShadowTextureTypes;
    std::string ShadowTextureOrthographicSource;
    std::string ShadowTextureBiasSource;
    std::string ShadowTextureZFormula;
    std::string ShadowTextureCompareSource;
    std::string ShadowTextureFilter;
    std::string ShadowMapFormat;
    std::string Shadow2dEncodedDepthDecodeSource;
    std::string Shadow2dOutOfBoundsResult;
    std::string Shadow2dFilterInterpolationSource;
    std::string ShaderRouteSource;
    std::string ShadowMapSource;
    std::string Shadow2dBackendPassRequestSource;
    std::string Shadow2dVisualPassSourceKind;
    std::string Shadow2dVisualPassRenderTargetFormat;
    std::string Shadow2dMaterialTextureProjectionInputSource;
    std::string Shadow2dTexCoord0WInputSource;
    std::string Shadow2dProjectionRegisterValueSource;
    std::string Shadow2dProjectionRegisterTraceSourceKind;
    std::string Shadow2dProjectionRegisterTraceFormat;
    std::string Shadow2dProjectionRegisterDecodeSource;
    std::string Shadow2dShaderRouteDecodeSource;
    std::string Shadow2dShaderRouteTraceStatus;
    std::string Shadow2dBackendShadowMapRenderTargetSource;
    std::string Shadow2dVisualPassApplication;
    std::string Shadow2dVisualPassBlockedReason;
    std::string ShaderRouteApplication;
    std::string ShadowCasterScope;
    std::string ShadowLightVectorSource;
    std::string ShadowOcclusionFormula;
    std::string ShadowLightContributionFormula;
    std::string PendingRoute;
    uint32_t FragmentLightShadowedRegisterCount = 0;
    uint32_t DirectionalLightCount = 0;
    uint32_t Shadow2dEncodedDepthBits = 0;
    uint32_t Shadow2dEncodedAlphaBits = 0;
    uint32_t Shadow2dBiasShift = 0;
    uint32_t Shadow2dFilterTapCount = 0;
    uint32_t Shadow2dFilterResultChannelCount = 0;
    uint32_t Shadow2dPicaTextureShadowRegisterIndex = 0;
    uint32_t Shadow2dPicaFramebufferShadowRegisterIndex = 0;
    uint32_t Shadow2dPicaTextureShadowRegisterRaw = 0;
    uint32_t Shadow2dPicaFramebufferShadowRegisterRaw = 0;
    uint32_t Shadow2dTextureShadowRawBias = 0;
    uint32_t Shadow2dTextureShadowCompareBias = 0;
    uint32_t Shadow2dFramebufferShadowConstantRaw = 0;
    uint32_t Shadow2dFramebufferShadowLinearRaw = 0;
    uint32_t Shadow2dLightingConfig0Raw = 0;
    uint32_t Shadow2dLightingConfig1Raw = 0;
    uint32_t Shadow2dShadowSelector = 0;
    uint32_t Shadow2dPerLightShadowEnableMask = 0;
    uint32_t Shadow2dShadowTextureParamRegisterIndex = 0;
    uint32_t Shadow2dShadowTextureParamRaw = 0;
    uint32_t Shadow2dShadowTextureType = 0;
    uint32_t Shadow2dShadowTextureDimRegisterIndex = 0;
    uint32_t Shadow2dShadowTextureDimRaw = 0;
    uint32_t Shadow2dShadowTextureWidth = 0;
    uint32_t Shadow2dShadowTextureHeight = 0;
    bool Shadow2dTextureShadowOrthographic = false;
    bool Shadow2dDmpPerspectiveShadow = false;
    bool Shadow2dFragmentLightingEnabled = false;
    bool Shadow2dLightingEnableShadow = false;
    bool Shadow2dLightingShadowPrimary = false;
    bool Shadow2dLightingShadowSecondary = false;
    bool Shadow2dLightingShadowInvert = false;
    bool Shadow2dLightingShadowAlpha = false;
    bool Shadow2dShadowTextureIsShadow2d = false;
    double Shadow2dDmpShadowZBias = 0.0;
    double Shadow2dDmpShadowZScale = 0.0;
    double Shadow2dFramebufferShadowConstant = 0.0;
    double Shadow2dFramebufferShadowLinear = 0.0;
    size_t CandidateBatchCount = 0;
    size_t MaterialTextureProjectionCandidateBatchCount = 0;
    size_t MaterialTextureProjectionDecodedBatchCount = 0;
    size_t TexCoord0WInputCandidateBatchCount = 0;
    size_t TexCoord0WInputDecodedBatchCount = 0;
    size_t NativeMaterialLaneDecodedBatchCount = 0;
    size_t AppliedBatchCount = 0;
    size_t AppliedVertexCount = 0;
    size_t OccludedVertexCount = 0;
};

struct Oot3dNativeActorShadowState {
    bool Available = false;
    bool ActorShapeStateSupported = false;
    bool ReceiverFromNativeCollision = false;
    bool NativeBlendRouteSupported = false;
    bool UsesRuntimeN64AssetSubstitution = false;
    bool FootShadowDrawSupported = false;
    bool FootContactPairProjectionSupported = false;
    std::string SourceKind;
    std::string Actor;
    std::string DrawFunction;
    std::string ShapeSource;
    std::string ReceiverSource;
    std::string BlendSource;
    std::string PendingRoute;
    float ShapeYOffset = 0.0f;
    float ShapeShadowScale = 0.0f;
    uint8_t ShapeShadowAlpha = 0;
    int FloorPolygonIndex = -1;
    int FloorSurfaceType = -1;
    int FloorLightSettingRawIndex = -1;
    int FloorLightSettingIndex = -1;
    float ActorFloorHeight = 0.0f;
    float ActorDistanceToFloor = 0.0f;
    Vec3f ActorPosition = { 0.0f, 0.0f, 0.0f };
    Vec3f ReceiverPosition = { 0.0f, 0.0f, 0.0f };
    Vec3f ReceiverNormal = { 0.0f, 1.0f, 0.0f };
};

struct Oot3dNativeKankyoRuntimeCtxbState {
    bool Resolved = false;
    uint32_t NativeSubresourceId = 0;
    std::string EffectClass;
    std::filesystem::path ArchivePath;
    uint32_t EntryIndex = 0;
    std::string EntryName;
    uint32_t EntryTypeLocalIndex = 0;
    uint32_t EntryOffset = 0;
    uint32_t EntrySize = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint16_t TextureFormat = 0;
    uint16_t DataType = 0;
    uint32_t PayloadSize = 0;
    NativeCtxbDescriptorSlot DescriptorSlot;
    std::vector<NativeKankyoRuntimeBindingSlot> RuntimeBindingSlots;
    std::string SourceKind;
    std::string SourceStatus;
};

struct Oot3dNativeKankyoCommonTbdRgba8 {
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 0;
};

struct Oot3dNativeKankyoCommonTbdRecord {
    bool Resolved = false;
    uint32_t RecordIndex = 0;
    uint32_t RecordOffset = 0;
    std::string Name;
    uint32_t NativeIndex = 0;
    uint32_t RecordSize = 0;
    uint32_t Type = 0;
    uint32_t ValueCount = 0;
    uint32_t PayloadOffset = 0;
    uint32_t PayloadSize = 0;
    bool RecordSizeMatches = false;
    bool ValueCountMatchesPayload = false;
    std::vector<float> FloatValues;
    std::vector<uint8_t> ByteValues;
    std::vector<Oot3dNativeKankyoCommonTbdRgba8> Rgba8Values;
    std::string PayloadFirst32BytesHex;
    std::string SourceStatus;
};

struct Oot3dNativeKankyoCommonTbdState {
    bool Resolved = false;
    std::filesystem::path ArchivePath;
    uint32_t EntryIndex = 0;
    std::string EntryName;
    uint32_t EntryTypeLocalIndex = 0;
    uint32_t EntryOffset = 0;
    uint32_t EntrySize = 0;
    uint32_t Magic = 0;
    uint32_t Version = 0;
    uint32_t DeclaredSize = 0;
    uint32_t EntryCount = 0;
    bool DeclaredSizeMatches = false;
    bool RecordTableResolved = false;
    uint32_t ResolvedRecordCount = 0;
    std::vector<Oot3dNativeKankyoCommonTbdRecord> Records;
    std::string First32BytesHex;
    std::string SourceKind;
    std::string SourceStatus;
};

struct Oot3dNativeKankyoLensEffectElement {
    uint32_t Index = 0;
    float ScaleMin = 0.0f;
    float ScaleMax = 0.0f;
    float Offset = 0.0f;
    float Depth = 0.0f;
};

struct Oot3dNativeKankyoOverlayGeometryCoord {
    uint32_t Index = 0;
    float X = 0.0f;
    float Y = 0.0f;
};

struct Oot3dNativeKankyoPrimitiveBackendVertexInput {
    uint32_t Index = 0;
    float X = 0.0f;
    float Y = 0.0f;
    ColorRgba8 Color = { 255, 255, 255, 255 };
};

struct Oot3dNativeKankyoRuntime28CVisibleBatchInput {
    uint32_t VisibleBatchIndex = 0;
    uint32_t SourceElementIndex = 0;
    float LensScaleMin = 0.0f;
    float LensScaleMax = 0.0f;
    float LensOffset = 0.0f;
    float LensDepth = 0.0f;
    ColorRgba8 LensHalationColor = { 255, 255, 255, 255 };
};

struct Oot3dNativeKankyoRuntime28CQuadLaneVertexInput {
    uint32_t VisibleBatchIndex = 0;
    uint32_t SourceElementIndex = 0;
    uint32_t CornerIndex = 0;
    uint32_t PositionRecordByteOffset = 0;
    uint32_t MatrixRecordByteOffset = 0;
    uint32_t ColorRecordByteOffset = 0;
    uint32_t TexcoordRecordByteOffset = 0;
    uint32_t TexcoordCornerBaseUOffset = 0;
    uint32_t TexcoordCornerBaseVOffset = 0;
};

struct Oot3dNativeKankyoRuntime28CProjectionSample {
    std::string SourceKind;
    Oot3dDemoVec3 WorldPosition;
    float ClipX = 0.0f;
    float ClipY = 0.0f;
    float ClipZ = 0.0f;
    float ClipW = 1.0f;
    float InverseW = 1.0f;
    bool NativeWClampApplied = false;
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
};

struct Oot3dNativeKankyoRuntime28CPositionRecord {
    uint32_t VisibleBatchIndex = 0;
    uint32_t SourceElementIndex = 0;
    uint32_t PositionRecordByteOffset = 0;
    float LensOffset = 0.0f;
    float LensDepth = 0.0f;
    Oot3dDemoVec3 SourceWorldPosition;
    Oot3dDemoVec3 KankyoSourceOffset;
    Oot3dDemoVec3 RuntimePosition;
    Oot3dNativeKankyoRuntime28CProjectionSample Projection;
    float RuntimeDistanceFactor = 0.0f;
    float RuntimeInterpolatedScale = 0.0f;
    float RuntimeScale = 0.0f;
    uint32_t RuntimeScaleArgument = 0;
    float RuntimeScaleMultiplier = 0.0f;
};

struct Oot3dNativeKankyoPrimitiveBackendInputState {
    bool Available = false;
    bool PrimitivePacketBridgeResolved = false;
    bool AttributeMaskPacketResolved = false;
    bool QuadBatchLanePlanResolved = false;
    bool Runtime28CParticleBatchContractResolved = false;
    bool Runtime28CEnqueueDispatchResolved = false;
    bool Runtime28CPositionProducerResolved = false;
    bool Runtime28CPositionRuntimeInputResolved = false;
    bool Runtime28CPositionRuntimeScaleResolved = false;
    bool Runtime28CVisibleBatchInputsResolved = false;
    bool Runtime28CQuadLaneMaterialized = false;
    bool TextureInputResolved = false;
    bool DecodedTextureInputResolved = false;
    bool TerminalTextureInputResolved = false;
    bool TerminalDecodedTextureInputResolved = false;
    bool TerminalElementInputResolved = false;
    bool TextureHasNativeAlpha = false;
    bool NativePicaAlphaBlendSemanticsRequired = false;
    bool NativePicaAlphaBlendSemanticsResolved = false;
    bool GeometryInputResolved = false;
    bool ColorInputResolved = false;
    bool ReadyForBackendInput = false;
    bool VisibleBackendSubmitResolved = false;
    bool ReadyForBackendRender = false;
    bool MaterialScalarRuntimeListBinderResolved = false;
    bool MaterialScalarRuntimeListSourceMatchesFogSource = false;
    bool MaterialScalarRuntimeListPacketResolverResolved = false;
    bool MaterialScalarGameplayDrawOrderResolved = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string MaterialScalarRuntimeListSourceStatus;
    std::string NativePicaAlphaBlendSemanticsSourceStatus;
    std::string VisibleBackendBlockedReason;
    std::string Runtime28CPositionRuntimeInputBlockedReason;
    uint32_t MaterialScalarGameplayDrawFunctionAddress = 0;
    uint32_t MaterialScalarGameplayFogUpdateCallsiteAddress = 0;
    uint32_t MaterialScalarGameplayRuntimeList0BindCallsiteAddress = 0;
    uint32_t MaterialScalarGameplayRuntimeList1BindCallsiteAddress = 0;
    uint32_t MaterialScalarGameplayViewPlayOffset = 0;
    uint32_t MaterialScalarGameplayFinalFogRgbPlayOffset = 0;
    uint32_t MaterialScalarGameplayFogFarPlayOffset = 0;
    uint32_t MaterialScalarGameplayFogNearPlayOffset = 0;
    uint32_t MaterialScalarGameplayFogAlphaImmediate = 0;
    uint32_t MaterialScalarRuntimeListBinderAddress = 0;
    uint32_t MaterialScalarRuntimeListBinderSourcePlayOffset = 0;
    uint32_t MaterialScalarRuntimeList0PlayOffset = 0;
    uint32_t MaterialScalarRuntimeList1PlayOffset = 0;
    uint32_t MaterialScalarRuntimeListCountByteOffset = 0;
    uint32_t MaterialScalarRuntimeListEntryStrideBytes = 0;
    uint32_t MaterialScalarRuntimeListEntryRuntimeObjectPointerOffset = 0;
    uint32_t MaterialScalarRuntimeObjectPacketResolverAddress = 0;
    uint32_t MaterialScalarRuntimeObjectPacketOwnerPointerOffset = 0;
    uint32_t MaterialScalarRuntimeObjectPacketOwnerMaterialPacketOffset = 0;
    std::string TextureName;
    Oot3dNativeRenderTexture Texture;
    std::string TerminalTextureName;
    Oot3dNativeRenderTexture TerminalTexture;
    uint32_t PrimaryBatchLastElementIndex = 0;
    uint32_t TerminalElementIndex = 0;
    uint32_t TerminalNativeVertexCount = 0;
    uint32_t TerminalSubmitElementCount = 0;
    bool NativeLensVisibilityScreenGateResolved = false;
    bool NativeLensVisibilitySceneOcclusionResolved = false;
    bool NativeLensVisibilitySourceInViewport = false;
    bool NativeLensVisibilitySourceOccluded = false;
    float NativeLensVisibilityTarget = 0.0f;
    float NativeLensVisibilityVisibleTarget = 0.0f;
    float NativeLensVisibilityScale = 0.0f;
    float NativeLensVisibilityMaxStep = 0.0f;
    float NativeLensVisibilityMinStep = 0.0f;
    float NativeLensVisibilityScreenMaxX = 0.0f;
    float NativeLensVisibilityScreenMaxY = 0.0f;
    float NativeLensVisibilityOccluderDistance = 0.0f;
    std::string NativeLensVisibilityOccluderModel;
    std::string NativeLensVisibilitySourceStatus;
    uint32_t Runtime28CPositionProducerFunctionAddress = 0;
    uint32_t Runtime28CPositionProjectionHelperAddress = 0;
    uint32_t Runtime28CPositionViewProjectionHelperAddress = 0;
    uint32_t Runtime28CPositionViewProjectionMatrixPlayOffset = 0;
    bool Runtime28CPositionViewProjectionSourceResolved = false;
    uint32_t Runtime28CPositionGameplayDrawFunctionAddress = 0;
    uint32_t Runtime28CPositionViewSourceProducerFunctionAddress = 0;
    uint32_t Runtime28CPositionViewSourceProducerCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewPrepFunctionAddress = 0;
    uint32_t Runtime28CPositionViewPrepCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewUpdateFunctionAddress = 0;
    uint32_t Runtime28CPositionViewUpdateCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewProjectionSourceCopyHelperAddress = 0;
    uint32_t Runtime28CPositionViewProjectionSourceCopyCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewProjectionComposeCopyHelperAddress = 0;
    uint32_t Runtime28CPositionViewProjectionComposeCopyCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewProjectionComposeHelperAddress = 0;
    uint32_t Runtime28CPositionViewProjectionComposeCallsiteAddress = 0;
    uint32_t Runtime28CPositionViewStructPlayOffset = 0;
    uint32_t Runtime28CPositionViewProjectionSourceMatrixPlayOffset = 0;
    uint32_t Runtime28CPositionViewProjectionComposeMatrixPlayOffset = 0;
    uint32_t Runtime28CPositionViewProjectionMatrixWordCount = 0;
    uint32_t Runtime28CPositionViewProjectionComposeMatrixWordCount = 0;
    bool Runtime28CPositionViewProjectionIdentityRowPatched = false;
    bool Runtime28CPositionViewProjectionRuntimeMatrixMaterialized = false;
    std::string Runtime28CPositionViewProjectionRuntimeMatrixSourceKind;
    std::string Runtime28CPositionViewProjectionRuntimeMatrixStatus;
    Matrix4f Runtime28CPositionViewProjectionRuntimeMatrix;
    std::vector<Oot3dNativeKankyoRuntime28CProjectionSample>
        Runtime28CPositionProjectionSamples;
    std::vector<Oot3dNativeKankyoRuntime28CPositionRecord>
        Runtime28CPositionRecords;
    float Runtime28CPositionScreenCenterX = 0.0f;
    float Runtime28CPositionScreenCenterY = 0.0f;
    float Runtime28CPositionBaseZ = 0.0f;
    float Runtime28CPositionDirectionScale = 0.0f;
    float Runtime28CPositionDistanceLimitScale = 0.0f;
    uint32_t Runtime28CPositionRuntimeScaleArgument = 0;
    uint32_t Runtime28CPositionRuntimeScaleMultiplierAddress = 0;
    float Runtime28CPositionRuntimeScaleMultiplier = 0.0f;
    float Runtime28CPositionProjectionScaleX = 0.0f;
    float Runtime28CPositionProjectionScaleY = 0.0f;
    float Runtime28CPositionProjectionBaseY = 0.0f;
    uint32_t PrimitiveDrawPacketFunctionAddress = 0;
    uint32_t PrimitiveDrawAttributeMaskFunctionAddress = 0;
    uint32_t PrimitiveDrawCommandCommitAddress = 0;
    uint32_t PrimitiveDrawPacketWordCount = 0;
    uint32_t PrimitiveDrawIndexElementType = 0;
    uint32_t PrimitiveDrawEffectStackIndexBase = 0;
    uint32_t PrimitiveDrawAttributeMaskHeaderWord = 0;
    uint32_t PrimitiveDrawAttributeMaskPayloadOrMask = 0;
    uint32_t RuntimeDrawCountOffset = 0;
    uint32_t QuadBatchExpandedVertexCount = 0;
    uint32_t QuadBatchVisibleElementCount = 0;
    uint32_t QuadBatchNativeVerticesPerQuad = 0;
    uint32_t Runtime28CQuadVertexCount = 0;
    uint32_t Runtime28CDrawCountPerVisibleBatch = 0;
    uint32_t Runtime28CPositionRecordStrideBytes = 0;
    uint32_t Runtime28CMatrixRecordStrideBytes = 0;
    uint32_t Runtime28CLocalVectorRecordStrideBytes = 0;
    uint32_t Runtime28CColorRecordStrideBytes = 0;
    uint32_t Runtime28CTexcoordRecordStrideBytes = 0;
    uint32_t Runtime28CBatchCapacityOffset = 0;
    uint32_t Runtime28CLocalVectorArrayPointerOffset = 0;
    uint32_t Runtime28CQuadLaneVertexInputCount = 0;
    uint32_t Runtime28CDrawCount = 0;
    uint32_t Runtime28CQueuedElementCount = 0;
    uint32_t Runtime28CSpecialSubmitElementCount = 0;
    uint32_t Runtime28CSpecialSubmitStartElementIndex = 0;
    uint32_t Runtime28CSpecialSubmitEndElementIndex = 0;
    uint32_t Runtime28CPrimarySubmitQueueIndexBase = 0;
    uint32_t Runtime28CTerminalSubmitQueueIndexBase = 0;
    uint32_t OverlayPrimitiveVertexCount = 0;
    uint32_t ColorPassDrawModeRaw = 0;
    uint32_t ColorPassPrimitiveValue = 0;
    uint32_t AlphaPassDrawModeRaw = 0;
    uint32_t AlphaPassPrimitiveValue = 0;
    std::vector<uint32_t> PrimitiveDrawPacketLiteralWords;
    std::vector<Oot3dNativeKankyoRuntime28CVisibleBatchInput> Runtime28CVisibleBatches;
    Oot3dNativeKankyoRuntime28CVisibleBatchInput TerminalElementInput;
    std::vector<Oot3dNativeKankyoRuntime28CQuadLaneVertexInput> Runtime28CQuadLaneVertices;
    std::vector<Oot3dNativeKankyoPrimitiveBackendVertexInput> OverlayVertices;
};

struct Oot3dNativeKankyoLensEffectState {
    bool Available = false;
    bool TbdRecordsResolved = false;
    bool TextureInputsResolved = false;
    bool ReadyForBackendInput = false;
    bool NativeSubmitMappingResolved = false;
    bool OverlayHelperCallbackMappingResolved = false;
    bool NativeRuntimeListResolved = false;
    bool NativeBackendBufferHelpersResolved = false;
    bool NativeBackendInputPlanResolved = false;
    bool NativeVisibleBackendSubmitResolved = false;
    bool NativeLensPositionProducerResolved = false;
    bool NativeLensPositionRuntimeInputResolved = false;
    bool NativeLensPositionRuntimeScaleResolved = false;
    bool NativeLensVisibilityScreenGateResolved = false;
    bool NativeLensVisibilitySceneOcclusionResolved = false;
    bool NativeLensVisibilitySourceInViewport = false;
    bool NativeLensVisibilitySourceOccluded = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string NativeBackendPlanSourceStatus;
    std::string NativeLensPositionRuntimeInputStatus;
    std::string NativeLensVisibilitySourceStatus;
    std::string LensTbdEntryName;
    std::string SunTextureName;
    std::string LensflareTextureName;
    uint32_t ElementCount = 0;
    std::vector<Oot3dNativeKankyoLensEffectElement> Elements;
    std::vector<ColorRgba8> HalationColors;
    uint32_t EffectStateBuilderFunctionAddress = 0;
    uint32_t EffectTypeBytePlayOffset = 0;
    uint32_t EffectModeBytePlayOffset = 0;
    uint32_t EffectDescriptorBasePlayOffset = 0;
    uint32_t EffectDescriptorTypeSlotOffset = 0;
    uint32_t EffectDescriptorSubmitCallbackSlotOffset = 0;
    uint32_t FlashSubmitFunctionAddress = 0;
    uint32_t ColorBytesOverlayHelperAddress = 0;
    uint32_t PackedRgbaOverlayHelperAddress = 0;
    uint32_t ColorBytesOverlayHelperAssignmentLiteralAddress = 0;
    uint32_t PackedRgbaOverlayHelperAssignmentLiteralAddress = 0;
    uint32_t ColorBytesOverlayEffectTypePrimary = 0;
    uint32_t ColorBytesOverlayEffectTypeAlternate = 0;
    uint32_t PackedRgbaOverlayEffectTypeRangeStart = 0;
    uint32_t PackedRgbaOverlayEffectTypeRangeEnd = 0;
    uint32_t ImmediateSubmitHelperAddress = 0;
    uint32_t SchedulerSubmitHelperAddress = 0;
    uint32_t LensRuntimeListKankyoOffset = 0;
    uint32_t LensRuntimeDrawAddress = 0;
    uint32_t LensRuntimeDrawHelperAddress = 0;
    uint32_t LensRuntimePrimaryBuilderAddress = 0;
    uint32_t LensRuntimeSubmitQueueAddress = 0;
    uint32_t LensRuntimeSubmitRecordWriterAddress = 0;
    uint32_t LensRuntimeQuadBatchDrawMethodAddress = 0;
    uint32_t LensRuntimeQuadBatchNativeVerticesPerQuad = 0;
    uint32_t LensRuntimePrimaryElementSubmitIndex = 0;
    uint32_t LensRuntimeTerminalElementSubmitIndex = 0;
    uint32_t LensRuntimeVisibleFlagMask = 0;
    uint32_t LensPositionProducerFunctionAddress = 0;
    uint32_t LensPositionProjectionHelperAddress = 0;
    uint32_t LensPositionViewProjectionHelperAddress = 0;
    uint32_t LensPositionViewProjectionMatrixPlayOffset = 0;
    bool LensPositionViewProjectionSourceResolved = false;
    uint32_t LensPositionGameplayDrawFunctionAddress = 0;
    uint32_t LensPositionViewSourceProducerFunctionAddress = 0;
    uint32_t LensPositionViewSourceProducerCallsiteAddress = 0;
    uint32_t LensPositionViewPrepFunctionAddress = 0;
    uint32_t LensPositionViewPrepCallsiteAddress = 0;
    uint32_t LensPositionViewUpdateFunctionAddress = 0;
    uint32_t LensPositionViewUpdateCallsiteAddress = 0;
    uint32_t LensPositionViewProjectionSourceCopyHelperAddress = 0;
    uint32_t LensPositionViewProjectionSourceCopyCallsiteAddress = 0;
    uint32_t LensPositionViewProjectionComposeCopyHelperAddress = 0;
    uint32_t LensPositionViewProjectionComposeCopyCallsiteAddress = 0;
    uint32_t LensPositionViewProjectionComposeHelperAddress = 0;
    uint32_t LensPositionViewProjectionComposeCallsiteAddress = 0;
    uint32_t LensPositionViewStructPlayOffset = 0;
    uint32_t LensPositionViewProjectionSourceMatrixPlayOffset = 0;
    uint32_t LensPositionViewProjectionComposeMatrixPlayOffset = 0;
    uint32_t LensPositionViewProjectionMatrixWordCount = 0;
    uint32_t LensPositionViewProjectionComposeMatrixWordCount = 0;
    bool LensPositionViewProjectionIdentityRowPatched = false;
    bool NativeLensPositionRuntimeViewProjectionMatrixMaterialized = false;
    std::string NativeLensPositionRuntimeViewProjectionMatrixSourceKind;
    std::string NativeLensPositionRuntimeViewProjectionMatrixStatus;
    Matrix4f NativeLensPositionRuntimeViewProjectionMatrix;
    std::vector<Oot3dNativeKankyoRuntime28CProjectionSample>
        NativeLensPositionProjectionSamples;
    bool NativeLensPositionPrimarySourceVectorResolved = false;
    std::string NativeLensPositionPrimarySourceVectorStatus;
    uint32_t LensPositionPrimarySourceProducerFunctionAddress = 0;
    uint32_t LensPositionPrimarySourceProducerCallsiteAddress = 0;
    uint32_t LensPositionPrimarySourceOffsetUpdateFunctionAddress = 0;
    uint32_t LensPositionPrimarySourceBaseCameraXPlayOffset = 0;
    uint32_t LensPositionPrimarySourceBaseCameraYPlayOffset = 0;
    uint32_t LensPositionPrimarySourceBaseCameraZPlayOffset = 0;
    uint32_t LensPositionPrimarySourceKankyoOffsetXPlayOffset = 0;
    uint32_t LensPositionPrimarySourceKankyoOffsetYPlayOffset = 0;
    uint32_t LensPositionPrimarySourceKankyoOffsetZPlayOffset = 0;
    uint32_t LensPositionPrimarySourceActiveAngleStateAddress = 0;
    uint32_t LensPositionPrimarySourceActiveAngleHalfwordOffset = 0;
    uint32_t LensPositionPrimarySourceScaleXAddress = 0;
    uint32_t LensPositionPrimarySourceScaleYAddress = 0;
    uint32_t LensPositionPrimarySourceScaleZAddress = 0;
    uint32_t LensPositionPrimarySourceRadiusPointerAddress = 0;
    uint32_t LensPositionPrimarySourceRadiusValueAddress = 0;
    float LensPositionPrimarySourceScaleX = 0.0f;
    float LensPositionPrimarySourceScaleY = 0.0f;
    float LensPositionPrimarySourceScaleZ = 0.0f;
    float LensPositionPrimarySourceRadius = 0.0f;
    int LensPositionPrimarySourceActiveAngle = -1;
    std::string LensPositionPrimarySourceActiveAngleSource;
    Oot3dDemoVec3 NativeLensPositionPrimarySourceBaseCamera;
    Oot3dDemoVec3 NativeLensPositionPrimarySourceKankyoOffset;
    Oot3dDemoVec3 NativeLensPositionPrimarySourceWorldPosition;
    std::vector<Oot3dNativeKankyoRuntime28CPositionRecord>
        NativeLensPositionRuntimePositionRecords;
    uint32_t LensPositionScreenCenterXAddress = 0;
    uint32_t LensPositionScreenCenterYAddress = 0;
    uint32_t LensPositionBaseZAddress = 0;
    uint32_t LensPositionDirectionScaleAddress = 0;
    uint32_t LensPositionDistanceLimitScaleAddress = 0;
    uint32_t LensPositionRuntimeScaleMultiplierAddress = 0;
    uint32_t LensPositionRuntimeScaleArgument = 0;
    uint32_t LensPositionProjectionScaleXAddress = 0;
    uint32_t LensPositionProjectionScaleYAddress = 0;
    uint32_t LensPositionProjectionBaseYAddress = 0;
    float LensPositionScreenCenterX = 0.0f;
    float LensPositionScreenCenterY = 0.0f;
    float LensPositionBaseZ = 0.0f;
    float LensPositionDirectionScale = 0.0f;
    float LensPositionDistanceLimitScale = 0.0f;
    float LensPositionRuntimeScaleMultiplier = 0.0f;
    float LensPositionProjectionScaleX = 0.0f;
    float LensPositionProjectionScaleY = 0.0f;
    float LensPositionProjectionBaseY = 0.0f;
    uint32_t LensVisibilityMinStepAddress = 0;
    uint32_t LensVisibilityScaleAddress = 0;
    uint32_t LensVisibilityVisibleTargetAddress = 0;
    uint32_t LensVisibilityScreenMaxXAddress = 0;
    float LensVisibilityMinStep = 0.0f;
    float LensVisibilityMaxStep = 0.0f;
    float LensVisibilityScale = 0.0f;
    float LensVisibilityVisibleTarget = 0.0f;
    float LensVisibilityTarget = 0.0f;
    float LensVisibilityScreenMaxX = 0.0f;
    float LensVisibilityScreenMaxY = 0.0f;
    float LensVisibilityOccluderDistance = 0.0f;
    std::string LensVisibilityOccluderModel;
    uint32_t NativePrimitiveColorPass = 0;
    uint32_t NativePrimitiveAlphaPass = 0;
    uint32_t NativePrimitiveVertexCount = 0;
    uint32_t OverlayGeometryTableAddress = 0;
    uint32_t OverlayGeometryTableHeaderWord = 0;
    float OverlayColorByteScale = 0.0f;
    float OverlayBaseAlpha = 0.0f;
    float OverlayDepthThreshold = 0.0f;
    float OverlayDepthFadeScale = 0.0f;
    std::vector<float> OverlayGeometryTableValues;
    std::vector<Oot3dNativeKankyoOverlayGeometryCoord> OverlayGeometryCoordinatePairs;
    Oot3dNativeKankyoPrimitiveBackendInputState PrimitiveBackendInput;
};

struct Oot3dNativeKankyoMoonLayerState {
    uint32_t CtxbTypeLocalIndex = 0;
    std::string TextureName;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t TextureFormat = 0;
    uint32_t DataType = 0;
    uint32_t GeometryTemplateIndex = 0;
    float GeometryTemplateHalfExtent = 0.0f;
    uint32_t RuntimeObjectTemplateIndex = 0;
    uint32_t MinMagFilter = 0;
    uint32_t WrapS = 0;
    uint32_t WrapT = 0;
    bool RuntimeMaterialized = false;
    Oot3dDemoVec3 RuntimeWorldCenter;
    float RuntimeScale = 0.0f;
    float RuntimeUvMax = 0.0f;
    bool RuntimeAdditiveBlend = false;
    bool RuntimeAlphaTest = false;
};

struct Oot3dNativeKankyoMoonState {
    bool Available = false;
    bool NativeInitResolved = false;
    bool TextureInputsResolved = false;
    bool ReadyForBackendInput = false;
    bool NativeVisibleBackendSubmitResolved = false;
    std::string SourceKind;
    std::string SourceStatus;
    uint32_t SkyContextInitFunctionAddress = 0;
    uint32_t MoonInitFunctionAddress = 0;
    uint32_t MoonInitCallsiteAddress = 0;
    uint32_t CtxbBaseTypeLocalIndex = 0;
    uint32_t LayerCount = 0;
    bool RuntimeTransformResolved = false;
    uint16_t RuntimeActiveAngle = 0;
    Oot3dDemoVec3 RuntimeCelestialVector;
    Oot3dDemoVec3 RuntimeMoonDirection;
    std::string RuntimeTransformSource;
    std::vector<Oot3dNativeKankyoMoonLayerState> Layers;
};

struct Oot3dNativePicaLightingSemanticPlan {
    bool Available = false;
    std::string Format;
    std::string SourceKind;
    std::string Layout;
    int RecordSize = -1;
    Oot3dNativePicaLightingRenderState Template;
    int RuntimeTransitionActiveAngle = -1;
    int RuntimeTransitionCurrentMode = -1;
    int RuntimeTransitionTargetMode = -1;
    bool RuntimeTransitionModeBlendActive = false;
    double RuntimeTransitionModeBlendWeight = 0.0;
    std::string VertexHemisphereUniformTraceSource;
};

struct Oot3dNativeKankyoSunProfileTextureState {
    bool TextureInputResolved = false;
    uint32_t ProfileGroupIndex = 0;
    uint32_t CtxbTypeLocalIndex = 0;
    std::string TextureName;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t TextureFormat = 0;
    uint32_t DataType = 0;
};

struct Oot3dNativeKankyoSunHaloState {
    bool Available = false;
    bool NativeRouteResolved = false;
    bool TextureInputResolved = false;
    bool SamplerStateResolved = false;
    bool ReadyForBackendInput = false;
    bool RuntimeTransformResolved = false;
    bool NativeVisibleBackendSubmitResolved = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string TextureName;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t TextureFormat = 0;
    uint32_t DataType = 0;
    uint32_t CelestialVectorProducerFunctionAddress = 0;
    uint32_t GroupSubmitFunctionAddress = 0;
    uint32_t BillboardProducerFunctionAddress = 0;
    uint32_t RuntimeObjectDrawFunctionAddress = 0;
    uint32_t CelestialScaleXAddress = 0;
    uint32_t CelestialScaleYAddress = 0;
    uint32_t CelestialScaleZAddress = 0;
    uint32_t CelestialRadiusPointerAddress = 0;
    uint32_t CelestialRadiusValueAddress = 0;
    uint32_t PositionScaleAddress = 0;
    uint32_t BillboardScaleAddress = 0;
    uint32_t SamplerWrapAddress = 0;
    uint32_t SamplerFilterAddress = 0;
    float CelestialScaleX = 0.0f;
    float CelestialScaleY = 0.0f;
    float CelestialScaleZ = 0.0f;
    float CelestialRadius = 0.0f;
    float PositionScale = 0.0f;
    float BillboardScale = 0.0f;
    uint32_t MinMagFilter = 0;
    uint32_t WrapS = 0;
    uint32_t WrapT = 0;
    uint16_t RuntimeActiveAngle = 0;
    Oot3dDemoVec3 RuntimeCelestialVector;
    Oot3dDemoVec3 RuntimeWorldBasePosition;
    std::vector<float> RuntimeLayerAlphas;
    std::vector<std::string> RuntimeLayerTextureNames;
    std::vector<Oot3dNativeKankyoSunProfileTextureState> ProfileTextures;
    std::string RuntimeTransformSource;
};

struct Oot3dNativeRuntimeEnvironmentInput {
    bool TimeResolved = false;
    uint16_t DayTime = 0;
    uint16_t SkyboxTime = 0;
    int TimeStartFrame = -1;
    std::string SourceKind;
    std::string SourceStatus;
    bool LightModeResolved = false;
    int LightModeCurrent = -1;
    int LightModeTarget = -1;
    bool LightModeBlendActive = false;
    uint16_t LightModeBlendRemaining = 0;
    uint16_t LightModeBlendDuration = 0;
    double LightModeBlendWeight = 0.0;
    int LightModeStartFrame = -1;
    int LightModeSourceActionId = -1;
    std::string LightModeSourceKind;
    std::string LightModeSourceStatus;
    bool LightSettingResolved = false;
    int LightSettingRawIndex = -1;
    int LightSettingTarget = -1;
    int LightSettingSetupIndex = -1;
    int LightSettingStartFrame = -1;
    uint32_t LightSettingPlayTargetOffset = 0;
    uint32_t LightSettingPlayBlendWeightOffset = 0;
    std::string LightSettingScenePath;
    std::string LightSettingSourceKind;
    std::string LightSettingSourceStatus;
    bool ColorAddendsResolved = false;
    std::array<int, 3> AmbientColorAddends = { 0, 0, 0 };
    std::array<int, 3> LightColorAddends = { 0, 0, 0 };
    std::array<int, 3> FogColorAddends = { 0, 0, 0 };
    int ColorAddendSourceActionId = -1;
    bool ColorAddendRampActive = false;
    std::string ColorAddendSourceKind;
    std::string ColorAddendSourceStatus;
};

struct Oot3dNativeEnvironmentBackgroundState {
    bool Available = false;
    bool UsedForRender = false;
    bool RuntimeEnvironmentTimeInputAvailable = false;
    uint16_t RuntimeEnvironmentDayTime = 0;
    uint16_t RuntimeEnvironmentSkyboxTime = 0;
    int RuntimeEnvironmentTimeStartFrame = -1;
    std::string RuntimeEnvironmentTimeSourceKind;
    std::string RuntimeEnvironmentTimeSourceStatus;
    bool NativeKankyoDrawRouteDecoded = false;
    bool NativeKankyoArchiveAvailable = false;
    std::string SourceKind;
    std::string SourceStatus;
    std::string NativeKankyoRomPath;
    std::filesystem::path NativeKankyoArchivePath;
    uint32_t NativeKankyoSkyboxRecordTableAddress = 0;
    uint32_t NativeKankyoSkyboxRecordAddress = 0;
    uint32_t NativeKankyoScheduleTableAddress = 0;
    uint32_t NativeKankyoClockStateAddress = 0;
    uint32_t NativeKankyoClockHalfwordOffset = 0;
    uint32_t NativeKankyoDrawScaleAddress = 0;
    float NativeKankyoDrawScale = 1.0f;
    int NativeKankyoScheduleMode = -1;
    int NativeKankyoScheduleActiveAngle = -1;
    std::string NativeKankyoScheduleActiveAngleSource;
    int NativeKankyoScheduleEntryIndex = -1;
    int NativeKankyoCurrentProfileIndex = -1;
    int NativeKankyoNextProfileIndex = -1;
    int NativeKankyoBlendAlpha = -1;
    int NativeKankyoRecordParam44 = -1;
    int NativeKankyoRecordParam48 = -1;
    int NativeKankyoRecordParam4C = -1;
    std::vector<std::string> NativeKankyoSelectedCmbNames;
    std::vector<int> NativeKankyoSelectedCmbProfileIndices;
    std::vector<uint32_t> NativeKankyoSelectedCmbTypeLocalIndices;
    std::vector<int> NativeKankyoSelectedCmbLayerIndices;
    std::vector<std::string> NativeKankyoSelectedCmabNames;
    bool NativeKankyoExtraDrawRouteDecoded = false;
    uint32_t NativeKankyoExtraDrawCallsiteAddress = 0;
    uint32_t NativeKankyoExtraDrawFunctionAddress = 0;
    uint32_t NativeKankyoExtraIndexTablePointerAddress = 0;
    uint32_t NativeKankyoExtraIndexTableAddress = 0;
    uint32_t NativeKankyoExtraDrawCmbBaseIndex = 0;
    uint32_t NativeKankyoExtraDrawCount = 0;
    std::vector<uint32_t> NativeKankyoExtraDrawCmbTypeLocalIndices;
    std::vector<uint32_t> NativeKankyoExtraDrawCtxbTypeLocalIndices;
    std::vector<std::string> NativeKankyoExtraCmbNames;
    std::vector<std::string> NativeKankyoModelLoadErrors;
    std::vector<std::string> NativeKankyoExtraCtxbNames;
    std::vector<NativeCtxbDescriptorSlot> NativeKankyoExtraCtxbDescriptorSlots;
    std::vector<Oot3dNativeRenderTexture> NativeKankyoExtraCtxbTextures;
    bool NativeKankyoRuntimeCtxbSetResolved = false;
    std::filesystem::path NativeKankyoGameplayKeepArchivePath;
    std::vector<Oot3dNativeKankyoRuntimeCtxbState> NativeKankyoRuntimeCtxbStates;
    bool NativeKankyoCommonArchiveAvailable = false;
    std::filesystem::path NativeKankyoCommonArchivePath;
    std::vector<Oot3dNativeKankyoCommonTbdState> NativeKankyoCommonTbdStates;
    bool NativeKankyoCommonTbdSupportResolved = false;
    Oot3dNativeKankyoMoonState NativeKankyoMoon;
    Oot3dNativeKankyoSunHaloState NativeKankyoSunHalo;
    Oot3dNativeKankyoLensEffectState NativeKankyoLensEffect;
    uint32_t NativeKankyoMaterialAnimationCount = 0;
    uint32_t NativeKankyoMaterialAnimationAppliedBatchCount = 0;
    float NativeKankyoMaterialAnimationFrame = 0.0f;
    ColorRgba8 ClearColor = { 0, 0, 0, 255 };
    int ActiveSetupIndex = -1;
    int SkyboxCommandOffset = -1;
    int SkyboxCommandArgument = -1;
    int SkyboxCommandParameter = -1;
    int SpecialFilesCommandOffset = -1;
    int SpecialFilesCommandArgument = -1;
    int SpecialFilesCommandParameter = -1;
};

struct Oot3dNativeDemoRenderScene {
    Oot3dNativeRenderModel Room;
    std::vector<Oot3dNativeRenderModel> AdditionalRoomModels;
    uint32_t NativeRoomMaterialAnimationCount = 0;
    uint32_t NativeRoomMaterialAnimationAppliedBatchCount = 0;
    float NativeRoomMaterialAnimationFrame = 0.0f;
    Oot3dNativeRenderModel Link;
    std::vector<Oot3dNativeRenderModel> ActorVisuals;
    Oot3dDemoBounds Bounds;
    Oot3dNativePicaLightingRenderState PicaLighting;
    Oot3dNativePicaFogState PicaFog;
    Oot3dNativePicaViewportState PicaViewport;
    Oot3dNativePicaLightingDebugState PicaLightingDebug;
    Oot3dNativePicaShadowState PicaShadow;
    Oot3dNativeActorShadowState LinkActorShadow;
    Oot3dNativeEnvironmentBackgroundState EnvironmentBackground;
    std::vector<Oot3dNativeRenderModel> MoonModels;
    std::vector<Oot3dNativeRenderModel> EnvironmentModels;
};

enum class Oot3dNativeActorVisualSelection : uint8_t {
    ArchiveResolved = 0,
    NativeBehaviorResolved = 1,
};

Oot3dNativeRenderModel BuildOot3dNativeRenderModel(const CmbModel& model,
                                                    const Oot3dNativeRenderModelBuildOptions& options = {});
void MarkOot3dNativeRenderModelVertexDataCacheable(Oot3dNativeRenderModel& model);
size_t ApplyOot3dNativeRenderModelResourceVisibility(
    Oot3dNativeRenderModel& renderModel, const std::vector<uint8_t>& resourceVisibility);
size_t ApplyOot3dNativeRenderModelPose(Oot3dNativeRenderModel& renderModel,
                                       const CmbModel& sourceModel,
                                       const CsabPose* pose,
                                       const std::vector<Matrix4f>* skinTransforms);
size_t StripOot3dNativeRenderModelTexturePayloads(Oot3dNativeRenderModel& renderModel);
float ResolveOot3dNativeCmabMaterialAnimationFrame(uint32_t frameCountCandidate,
                                                    uint32_t loopModeCandidate,
                                                    float frame);
size_t ApplyOot3dNativeRenderModelMaterialAnimationFrame(Oot3dNativeRenderModel& renderModel,
                                                         const CmbModel& sourceModel,
                                                         const std::vector<CmabMaterialAnimation>& animations,
                                                         float frame);
size_t ApplyOot3dNativeRenderModelMaterialAnimationFrames(Oot3dNativeRenderModel& renderModel,
                                                           const CmbModel& sourceModel,
                                                           std::span<const CmabMaterialAnimation> animations,
                                                           const std::map<std::string, float>& frameByRole,
                                                           float fallbackFrame = 0.0f);
bool Oot3dNativeKankyoCmabAppliesToCmb(std::string_view cmabName, std::string_view cmbName);
size_t ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(Oot3dNativeRenderModel& renderModel,
                                                               uint32_t constantColorSlot,
                                                               ColorRgba8 color,
                                                               std::string_view source);
size_t ApplyOot3dNativeRenderModelRuntimeMaterialAlphaOverride(
    Oot3dNativeRenderModel& renderModel, int32_t materialIndex,
    uint32_t constantColorSlot, uint8_t alpha, std::string_view source);
bool BlendOot3dNativeKankyoProfileRenderModels(
    Oot3dNativeRenderModel& base, const Oot3dNativeRenderModel& current,
    const Oot3dNativeRenderModel& next,
    int32_t currentProfileIndex, int32_t nextProfileIndex, float weight);
uint8_t ResolveOot3dNativeKankyoSpecialChildAlpha(
    int currentSelector, int nextSelector, int blendAlpha);
uint8_t ResolveOot3dNativeKankyoSecondaryCloudAlpha(
    uint32_t typeLocalIndex, int currentSelector, int nextSelector, int blendAlpha);
Oot3dNativeRenderModel BuildOot3dNativeDemoLinkRenderModel(const Oot3dNativeDemoScene& scene,
                                                            const CsabPose& linkPose,
                                                           const std::vector<Matrix4f>& linkSkinTransforms,
                                                           float materialAnimationFrame = 0.0f);
Oot3dNativeActorShadowState BuildOot3dNativeLinkActorShadowState(
    const Oot3dNativeDemoScene& scene, const Oot3dDemoVec3& actorPosition, int floorPolygonIndex,
    int floorSurfaceType, int floorLightSettingRawIndex, int floorLightSettingIndex, double floorY);
Oot3dNativeDemoRenderScene BuildOot3dNativeDemoRenderScene(const Oot3dNativeDemoScene& scene);
Oot3dNativeDemoRenderScene BuildOot3dNativeDemoRenderScene(const Oot3dNativeDemoScene& scene,
                                                           const CsabPose& linkPose,
                                                           const std::vector<Matrix4f>& linkSkinTransforms,
                                                           float materialAnimationFrame = 0.0f,
                                                           const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr,
                                                           bool includeGameplayLink = true,
                                                           Oot3dNativeActorVisualSelection actorVisualSelection =
                                                               Oot3dNativeActorVisualSelection::ArchiveResolved);
Oot3dNativePicaLightingDebugState BuildOot3dNativePicaLightingDebugState(const Oot3dNativeDemoScene& scene);
Oot3dNativePicaLightingSemanticPlan CompileOot3dNativePicaLightingSemanticPlan(
    const nlohmann::json& semantics);
Oot3dNativePicaLightingRenderState BuildOot3dNativePicaLightingRenderState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr);
ColorRgba8 EvaluateOot3dNativePicaLightingVertexColor(
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRenderModel& model,
    Oot3dNativeRenderBatch& batch,
    const Matrix4f& modelToWorld,
    const Oot3dNativeRenderVertex& vertex);
Oot3dNativePicaLightingRenderState BuildOot3dNativePicaLightingRenderState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativePicaLightingSemanticPlan& semanticPlan,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr);
Oot3dNativePicaFogState BuildOot3dNativePicaFogState(
    const Oot3dNativeDemoScene& scene, const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeFogRuntimeDefaults* runtimeDefaults = nullptr);
Oot3dNativePicaShadowState BuildOot3dNativePicaShadowState(
    const Oot3dNativeDemoScene& scene, const Oot3dNativePicaLightingRenderState& lighting);
Oot3dNativeEnvironmentBackgroundState BuildOot3dNativeEnvironmentBackgroundState(
    const Oot3dNativeDemoScene& scene, const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr);
void ApplyOot3dNativePicaLighting(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr,
    const Oot3dNativePicaLightingSemanticPlan* semanticPlan = nullptr);
void RefreshOot3dNativeDemoRenderSceneRuntimeEnvironment(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment = nullptr,
    float materialAnimationFrame = 0.0f);
void RefreshOot3dNativeDemoRoomMaterialAnimations(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    float materialAnimationFrame);
void ApplyOot3dNativePicaLightingToActorVisualRange(
    Oot3dNativeDemoRenderScene& renderScene,
    size_t firstActorVisual,
    size_t actorVisualCount,
    bool collectDiagnostics = true);
void ApplyOot3dNativePicaLightingToLink(
    Oot3dNativeDemoRenderScene& renderScene,
    bool collectDiagnostics = true);
void ApplyOot3dNativePicaLightingStateToModel(
    const Oot3dNativePicaLightingRenderState& lighting,
    Oot3dNativeRenderModel& model,
    bool preserveMaterialState = false,
    bool collectDiagnostics = true);
void ApplyOot3dNativePicaLightingDebug(const Oot3dNativeDemoScene& scene,
                                       Oot3dNativeDemoRenderScene& renderScene);
Oot3dNativePicaViewportState BuildOot3dNativePicaViewportState(
    const Oot3dNativeEnvironmentBackgroundState& background);
double ResolveOot3dNativePicaProjectionAspect(
    const Oot3dNativeDemoRenderScene& scene, double fallbackAspect);
Oot3dNativeDemoRenderScene BuildOot3dNativeDemoPicaLightingDebugRenderScene(const Oot3dNativeDemoScene& scene);
Oot3dNativeDemoRenderScene BuildOot3dNativeDemoPicaLightingDebugRenderScene(
    const Oot3dNativeDemoScene& scene,
    const CsabPose& linkPose,
    const std::vector<Matrix4f>& linkSkinTransforms);
Matrix4f BuildOot3dNativeRenderScaleTranslateTransform(double scale, Oot3dDemoVec3 translation);
Matrix4f BuildOot3dNativeRenderScaleYawTranslateTransform(double scale, double yawRadians,
                                                          Oot3dDemoVec3 translation);
Matrix4f BuildOot3dNativeRenderActorEntryTransform(double scale, Oot3dDemoVec3 rotationS16,
                                                   Oot3dDemoVec3 translation);
Oot3dDemoBounds Oot3dNativeRenderTransformBounds(const Oot3dDemoBounds& bounds, const Matrix4f& transform);
void MaterializeOot3dNativeKankyoLensRuntimeViewProjection(
    Oot3dNativeDemoRenderScene& scene, const Matrix4f& viewProjectionMatrix,
    Oot3dDemoVec3 cameraEye, std::string sourceKind, std::string sourceStatus);
Oot3dNativeKankyoMoonState NativeRenderResolveKankyoMoonState(
    const Oot3dNativeEnvironmentBackgroundState& background);
void MaterializeOot3dNativeKankyoMoonRuntime(
    Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 cameraTarget, Oot3dDemoVec3 cameraUp);
void MaterializeOot3dNativeKankyoSkyRuntime(
    Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 cameraTarget, Oot3dDemoVec3 cameraUp);
Oot3dDemoBounds Oot3dNativeRenderModelWorldBounds(const Oot3dNativeRenderModel& model);
nlohmann::json Oot3dNativeRenderModelSummaryToJson(const Oot3dNativeRenderModel& model);
nlohmann::json Oot3dNativeDemoRenderSceneSummaryToJson(const Oot3dNativeDemoRenderScene& scene);

} // namespace ThreeDsRecomp::Oot3d
