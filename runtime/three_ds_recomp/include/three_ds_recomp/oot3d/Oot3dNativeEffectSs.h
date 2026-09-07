#pragma once

#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ThreeDsRecomp::Oot3d {

struct Oot3dNativeEffectSsCodeLayout {
    uint32_t CodeBase = 0;
    uint32_t EffectTypeTablePointerLiteralAddress = 0;
    uint32_t EffectInitUpdateTablePointerLiteralAddress = 0;
    uint32_t EffectInitDrawFunctionPointerLiteralAddress = 0;
    uint32_t EffectInitAlternateDrawFunctionPointerLiteralAddress = 0;
    uint32_t EffectInitColorRandomRangeAddress = 0;
    uint32_t EffectInitColorRandomOffsetAddress = 0;
    uint32_t ResourceArchiveTablePointerLiteralAddress = 0;
    uint32_t ResourceBindingTablePointerLiteralAddress = 0;
    uint32_t ParentResourceTablePointerLiteralAddress = 0;
    uint32_t ObjectArchivePathTableAddress = 0;
    uint32_t ObjectArchivePathStride = 0;
    uint16_t ObjectArchivePathFirstId = 0;
    uint32_t DustColorsPointerLiteralAddress = 0;
    uint32_t DrawGlobalColorNormalizeScaleAddress = 0;
    uint32_t DrawGlobalColorStatePointerLiteralAddress = 0;
    uint32_t DrawGlobalColorWriterBiasInstructionAddress = 0;
    uint32_t DrawGlobalColorWriterClampAddress = 0;
    uint32_t SceneSkyboxDefaultColorScaleAddress = 0;
    uint32_t SceneSkyboxByteColorScaleAddress = 0;
    uint32_t DrawScaleFactorAddress = 0;
    uint32_t DrawScaleMultiplierAddress = 0;
    uint32_t SpriteTemplatePositionPointerLiteralAddress = 0;
    uint32_t AtlasCellUAddress = 0;
    uint32_t AtlasCellVAddress = 0;
    uint32_t UpdateAccelerationSpanAddress = 0;
    uint32_t UpdateRateScaleAddress = 0;
    uint32_t GlobalUpdateRateInstructionAddress = 0;
    uint32_t UpdateRandomCenterAddress = 0;
    uint32_t TextureFrameCountAddress = 0;
    uint32_t TextureFrameMaxAddress = 0;
    uint32_t FadeThresholdInstructionAddress = 0;
    uint32_t RandomStatePointerLiteralAddress = 0;
    uint32_t RandomMultiplierAddress = 0;
    uint32_t RandomIncrementAddress = 0;
    uint32_t HorseDustScaleRandomRangeAddress = 0;
    uint32_t HorseDustScaleBaseInstructionAddress = 0;
    uint32_t HorseDustScaleStepRandomRangeAddress = 0;
    uint32_t HorseDustScaleStepBaseInstructionAddress = 0;
    uint32_t HorseDustDurationRandomRangeAddress = 0;
    uint32_t HorseDustDurationBaseInstructionAddress = 0;
    uint32_t DustDurationToLifeScaleAddress = 0;
    uint32_t DustDurationToLifeBiasAddress = 0;
    uint32_t HorseDustEffectFlagsInstructionAddress = 0;
    uint32_t HorseHoofOffsetXzAddress = 0;
    uint32_t HorseHoofOffsetYAddress = 0;
    std::array<uint32_t, 2> HorseGallopAnimationCompareInstructionAddresses{};
    std::array<uint32_t, 4> HorseContactSpanAddresses{};
    std::array<std::array<uint32_t, 2>, 4> HorseContactLowerBoundInstructionAddresses{};
    std::array<uint32_t, 4> HorseContactFlagInstructionAddresses{};
    std::array<uint32_t, 4> HorseContactNodeInstructionAddresses{};
    std::array<uint32_t, 4> HorseContactJitterFloatAddresses{};
    uint32_t HorseLastContactSpanMvnInstructionAddress = 0;
};

struct Oot3dNativeEffectSsDustProfile {
    bool Decoded = false;
    bool TextureLoaded = false;
    uint32_t EffectType = 0;
    uint32_t EffectTypeTableAddress = 0;
    uint32_t DescriptorAddress = 0;
    uint32_t InitFunctionAddress = 0;
    uint32_t DrawFunctionAddress = 0;
    uint32_t AlternateDrawFunctionAddress = 0;
    std::array<uint32_t, 2> UpdateFunctionAddresses{};
    uint32_t DescriptorFlags = 0;
    uint32_t DependencyStartIndex = 0;
    uint32_t DependencyCount = 0;
    uint8_t ResourceBindingIndex = 0;
    uint16_t ObjectId = 0;
    uint16_t CtxbTypeLocalIndex = 0;
    std::string ObjectArchiveRomPath;
    std::filesystem::path ObjectArchivePath;
    std::string TextureName;
    uint16_t SamplerMinFilter = 0;
    uint16_t SamplerMagFilter = 0;
    uint16_t SamplerWrapS = 0;
    uint16_t SamplerWrapT = 0;
    ColorRgba8 PrimaryColor = { 255, 255, 255, 255 };
    ColorRgba8 EnvironmentColor = { 0, 0, 0, 255 };
    uint32_t GlobalEnvironmentColorStateAddress = 0;
    uint8_t GlobalEnvironmentColorBias = 0;
    float GlobalEnvironmentColorNormalizeScale = 1.0f;
    float GlobalEnvironmentColorClamp = 255.0f;
    float SceneSkyboxDefaultColorScale = 1.0f;
    float SceneSkyboxByteColorScale = 1.0f / 255.0f;
    float ColorRandomRange = 0.0f;
    float ColorRandomOffset = 0.0f;
    float RenderScale = 1.0f;
    uint32_t SpriteTemplatePositionAddress = 0;
    float SpriteTemplateHalfExtent = 1.0f;
    float AccelerationRandomSpan = 0.0f;
    float AccelerationRandomCenter = 0.5f;
    float UpdateRateScale = 1.0f;
    uint32_t GlobalUpdateRate = 1;
    uint32_t AtlasColumns = 1;
    uint32_t AtlasRows = 1;
    uint32_t TextureFrameCount = 1;
    uint32_t FadeTickCount = 0;
    uint32_t RandomInitialState = 1;
    uint32_t RandomMultiplier = 0;
    uint32_t RandomIncrement = 0;
    Oot3dNativeRenderTexture Texture;
    std::string SourceStatus;
};

struct Oot3dNativeHorseDustContact {
    uint16_t Flag = 0;
    uint16_t SkeletonNodeIndex = 0;
    float FrameMinExclusive = 0.0f;
    float FrameMaxExclusive = 0.0f;
    float PositionJitter = 0.0f;
};

struct Oot3dNativeEffectSsDustSpawnProfile {
    bool Decoded = false;
    uint16_t Flags = 0;
    int16_t ScaleBase = 0;
    float ScaleRandomRange = 0.0f;
    int16_t ScaleStepBase = 0;
    float ScaleStepRandomRange = 0.0f;
    int16_t DurationBase = 0;
    float DurationRandomRange = 0.0f;
    float DurationToLifeScale = 1.0f;
    float DurationToLifeBias = 0.0f;
    std::string SourceStatus;
};

struct Oot3dNativeHorseDustEmitterProfile {
    bool Decoded = false;
    std::array<uint16_t, 2> GallopAnimationIndices{};
    Vec3f HoofLocalOffset{};
    std::array<Oot3dNativeHorseDustContact, 4> Contacts{};
    Oot3dNativeEffectSsDustSpawnProfile DustSpawn;
    std::string SourceStatus;
};

struct Oot3dNativeEffectSsParticle {
    Vec3f Position{};
    Vec3f Velocity{};
    Vec3f Acceleration{};
    int16_t Scale = 0;
    int16_t ScaleStep = 0;
    int16_t InitialLife = 0;
    int16_t RemainingLife = 0;
    uint16_t TextureFrame = 0;
    uint16_t Flags = 0;
    ColorRgba8 PrimaryColor = { 255, 255, 255, 255 };
    ColorRgba8 EnvironmentColor = { 0, 0, 0, 255 };
};

struct Oot3dNativeEffectSsRuntime {
    Oot3dNativeEffectSsDustProfile DustProfile;
    uint32_t RandomState = 1;
    int64_t LastUpdateTick = -1;
    std::vector<Oot3dNativeEffectSsParticle> Particles;
};

struct Oot3dNativeEffectSsRenderEnvironment {
    bool Resolved = false;
    ColorRgba8 AmbientColor = { 255, 255, 255, 255 };
    float SceneColorScale = 1.0f;
    Vec3f GlobalColorMultiplier = { 1.0f, 1.0f, 1.0f };
    std::string SourceStatus;
};

Oot3dNativeEffectSsCodeLayout Oot3dNativeEffectSsEuCodeLayout();
Oot3dNativeEffectSsDustProfile DecodeOot3dNativeEffectSsDustProfile(
    const std::filesystem::path& codeBinPath, const std::filesystem::path& romfsRoot,
    const Oot3dNativeEffectSsCodeLayout& layout = Oot3dNativeEffectSsEuCodeLayout());
Oot3dNativeHorseDustEmitterProfile DecodeOot3dNativeHorseDustEmitterProfile(
    const std::filesystem::path& codeBinPath,
    const Oot3dNativeEffectSsCodeLayout& layout = Oot3dNativeEffectSsEuCodeLayout());

void ResetOot3dNativeEffectSsRuntime(Oot3dNativeEffectSsRuntime& runtime,
                                     const Oot3dNativeEffectSsDustProfile& profile);
float Oot3dNativeEffectSsNextRandom(Oot3dNativeEffectSsRuntime& runtime);
float Oot3dNativeEffectSsNextRandomCentered(Oot3dNativeEffectSsRuntime& runtime,
                                           float halfExtent);
void SpawnOot3dNativeEffectSsDust(Oot3dNativeEffectSsRuntime& runtime,
                                 const Oot3dNativeEffectSsDustSpawnProfile& spawnProfile,
                                 Vec3f position,
                                 Vec3f velocity = { 0.0f, 1.0f, 0.0f },
                                 Vec3f acceleration = {});
void UpdateOot3dNativeEffectSs(Oot3dNativeEffectSsRuntime& runtime);
Oot3dNativeEffectSsRenderEnvironment ResolveOot3dNativeEffectSsRenderEnvironment(
    const Oot3dNativeEffectSsDustProfile& profile, ColorRgba8 ambientColor,
    uint32_t skyboxCommandArgument);
Oot3dNativeRenderModel MaterializeOot3dNativeEffectSsDust(
    const Oot3dNativeEffectSsRuntime& runtime, Vec3f cameraRight, Vec3f cameraUp,
    const Oot3dNativeEffectSsRenderEnvironment* environment = nullptr);

bool Oot3dNativeHorseDustAnimationMatches(const Oot3dNativeHorseDustEmitterProfile& profile,
                                          uint16_t animationIndex);
const Oot3dNativeHorseDustContact* Oot3dNativeHorseDustContactAtFrame(
    const Oot3dNativeHorseDustEmitterProfile& profile, uint16_t animationIndex,
    float animationFrame);

} // namespace ThreeDsRecomp::Oot3d
