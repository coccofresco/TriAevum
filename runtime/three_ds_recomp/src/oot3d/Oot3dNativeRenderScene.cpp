#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace ThreeDsRecomp::Oot3d {

std::filesystem::path NativeRenderResolveSceneZsiPath(const Oot3dNativeDemoScene& scene,
                                                      std::string_view scenePath);
bool NativeRenderScenePathMatches(const std::filesystem::path& lhs,
                                  const std::filesystem::path& rhs);

namespace {

constexpr double kOot3dS16AngleToRadians = (3.14159265358979323846 * 2.0) / 65536.0;
constexpr uint16_t kPicaTextureRgba = 0x6752;
constexpr uint16_t kPicaTextureAlpha = 0x6756;
constexpr uint16_t kPicaTextureLuminanceAlpha = 0x6758;
constexpr uint16_t kPicaTextureEtc1A4 = 0x675B;
constexpr uint16_t kPicaTextureEnvCombineReplace = 0x1E01;
constexpr uint16_t kPicaTextureEnvCombineModulate = 0x2100;
constexpr uint16_t kPicaTextureEnvCombineAdd = 0x0104;
constexpr uint16_t kPicaTextureEnvCombineMultiplyThenAdd = 0x6401;
constexpr uint16_t kPicaTextureEnvCombineAddThenMultiply = 0x6402;
constexpr uint16_t kPicaTextureEnvSourcePrimaryColor = 0x8577;
constexpr uint16_t kPicaTextureEnvSourceTexture0 = 0x84C0;
constexpr uint16_t kPicaTextureEnvSourceTexture1 = 0x84C1;
constexpr uint16_t kPicaTextureEnvSourceTexture2 = 0x84C2;
constexpr uint16_t kPicaTextureEnvSourceFragmentPrimaryColor = 0x6210;
constexpr uint16_t kPicaTextureEnvSourceFragmentSecondaryColor = 0x6211;
constexpr uint16_t kPicaTextureEnvSourceConstant = 0x8576;
constexpr uint16_t kPicaTextureEnvSourcePrevious = 0x8578;
constexpr uint16_t kPicaTextureEnvOperandSourceColor = 0x0300;
constexpr uint16_t kPicaTextureEnvOperandSourceAlpha = 0x0302;
constexpr uint32_t kPicaRegTexture0Dim = 0x082;
constexpr uint32_t kPicaRegTexture0Param = 0x083;
constexpr uint32_t kPicaRegTexture1Dim = 0x092;
constexpr uint32_t kPicaRegTexture1Param = 0x093;
constexpr uint32_t kPicaRegTexture2Dim = 0x09A;
constexpr uint32_t kPicaRegTexture2Param = 0x09B;
constexpr uint32_t kPicaRegTexunit0Shadow = 0x08B;
constexpr uint32_t kPicaRegLightingEnable0 = 0x08F;
constexpr uint32_t kPicaRegTexenvUpdateBuffer = 0x0E0;
constexpr uint32_t kPicaRegFogColor = 0x0E1;
constexpr uint32_t kPicaRegFogLutIndex = 0x0E6;
constexpr uint32_t kPicaRegFogLutData0 = 0x0E8;
constexpr uint32_t kPicaRegFragopShadow = 0x130;
constexpr uint32_t kPicaRegLightingConfig0 = 0x1C3;
constexpr uint32_t kPicaRegLightingConfig1 = 0x1C4;
constexpr uint32_t kPicaFogModeFog = 5;
constexpr uint32_t kPicaTextureTypeShadow2d = 2;
constexpr float kOot3dNativeLinkChildActorShadowScale = 60.0f;
constexpr uint8_t kOot3dNativeActorShapeDefaultShadowAlpha = 0xFF;
constexpr const char* kOot3dNativeLinkActorShadowShapeSource =
    "oot3d_player_initcommon_actor_shape_init_age_properties_offset_0x04";
constexpr const char* kOot3dNativeLinkActorShadowReceiverSource =
    "oot3d_actor_floor_poly_from_zsi_collision_bgcheck";
constexpr const char* kOot3dNativeLinkActorShadowBlendSource =
    "oot3d_actor_shape_shadow_alpha_native_render_state";
constexpr const char* kOot3dNativeLinkActorShadowPendingRoute =
    "feet_contact_pair_projection_pending_native_skelanime_limb_contact_mapping";

uint64_t NextNativeGeometryId() {
    static std::atomic<uint64_t> nextId{ 1 };
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

struct TextureBinding {
    size_t TextureIndex = 0;
    size_t Slot = 0;
    bool Valid = false;
    bool ResolvedFromRawStageSelector = false;
    std::string Source;
};

const NativeKankyoRuntimeBridgeContract& NativeKankyoRuntimeBridgeLayout() {
    static const auto contract = BuildNativeKankyoRuntimeBridgeContract();
    return contract;
}

const NativeZsiLightSettingsRecordContract& NativeZsiLightSettingsRecordLayout() {
    return NativeKankyoRuntimeBridgeLayout().ZsiLightSettingsRecord;
}

bool TextureHasNativeAlpha(const CmbTexture& texture) {
    return texture.TextureFormat == kPicaTextureRgba || texture.TextureFormat == kPicaTextureAlpha ||
           texture.TextureFormat == kPicaTextureLuminanceAlpha || texture.TextureFormat == kPicaTextureEtc1A4;
}

bool MeshIndexSelected(const std::vector<uint32_t>* selectedMeshIndices, uint32_t meshIndex) {
    if (selectedMeshIndices == nullptr || selectedMeshIndices->empty()) {
        return true;
    }
    return std::find(selectedMeshIndices->begin(), selectedMeshIndices->end(), meshIndex) != selectedMeshIndices->end();
}

size_t ActiveTextureMapperCount(const CmbMaterial& material) {
    return material.TextureMappersUsed != 0 ? std::min<size_t>(3, material.TextureMappersUsed) : 3;
}

bool TextureMapperSlotFromTextureEnvSource(uint16_t source, size_t& slotOut) {
    switch (source) {
        case 0x84C0:
            slotOut = 0;
            return true;
        case 0x84C1:
            slotOut = 1;
            return true;
        case 0x84C2:
            slotOut = 2;
            return true;
        default:
            return false;
    }
}

TextureBinding PrimaryTextureBindingFromMaterial(const CmbModel& model, const CmbMaterial& material) {
    const size_t mapperCount = material.TextureMappersUsed != 0 ? std::min<size_t>(3, material.TextureMappersUsed) : 3;

    for (const auto& textureEnv : material.TextureEnvStages) {
        for (const uint16_t source : textureEnv.SourceRgb) {
            size_t mapperSlot = 0;
            if (!TextureMapperSlotFromTextureEnvSource(source, mapperSlot) || mapperSlot >= mapperCount) {
                continue;
            }

            const int textureIndex = material.TextureMappers[mapperSlot].TextureIndex;
            if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= model.Textures.size()) {
                continue;
            }

            return { static_cast<size_t>(textureIndex), mapperSlot, true, true,
                     "cmb_texture_env_stage_source" };
        }
    }

    for (size_t slot = 0; slot < mapperCount; ++slot) {
        const int textureIndex = material.TextureMappers[slot].TextureIndex;
        if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < model.Textures.size()) {
            return { static_cast<size_t>(textureIndex), slot, true, false,
                     "cmb_texture_mapper_fallback" };
        }
    }
    return {};
}

TextureBinding TextureBindingForMapperSlot(const CmbModel& model, const CmbMaterial& material, size_t slot,
                                           std::string source) {
    const size_t mapperCount = ActiveTextureMapperCount(material);
    if (slot >= mapperCount) {
        return {};
    }

    const int textureIndex = material.TextureMappers[slot].TextureIndex;
    if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= model.Textures.size()) {
        return {};
    }

    return { static_cast<size_t>(textureIndex), slot, true, true, std::move(source) };
}

uint64_t Fnv1a64(const std::vector<uint8_t>& data) {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t Fnv1a64Append(uint64_t hash, const std::vector<uint8_t>& data) {
    for (const uint8_t byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool DecodedTexturePayloadsHaveStableIdentity(const Oot3dNativeRenderTexture& texture) {
    return texture.Rgba8Decoded && !texture.Rgba8.empty() &&
           std::all_of(texture.AdditionalMipLevels.begin(), texture.AdditionalMipLevels.end(),
                       [](const auto& mip) { return mip.Rgba8Decoded && !mip.Rgba8.empty(); });
}

uint64_t DecodedTexturePayloadHash(const Oot3dNativeRenderTexture& texture) {
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1a64Append(hash, texture.Rgba8);
    for (const auto& mip : texture.AdditionalMipLevels) {
        hash = Fnv1a64Append(hash, mip.Rgba8);
    }
    return hash;
}

TextureBinding PrimaryTextureBinding(const CmbModel& model, const CmbMesh& mesh) {
    if (mesh.MaterialIndex >= model.Materials.size()) {
        return {};
    }

    return PrimaryTextureBindingFromMaterial(model, model.Materials[mesh.MaterialIndex]);
}

Oot3dNativeRenderTexture BuildRenderTextureFromCmbTexture(const CmbTexture& texture) {
    Oot3dNativeRenderTexture renderTexture;
    renderTexture.SourceIndex = texture.Index;
    renderTexture.Name = texture.Name;
    renderTexture.Width = texture.Width;
    renderTexture.Height = texture.Height;
    renderTexture.TextureFormat = texture.TextureFormat;
    renderTexture.DataType = texture.DataType;
    renderTexture.MipmapCount = std::max<uint32_t>(1, texture.MipmapCount);
    renderTexture.MipmapLayoutDecoded = texture.MipmapLayoutDecoded;
    renderTexture.Rgba8Decoded = texture.Rgba8Decoded;
    renderTexture.HasNativeAlpha = TextureHasNativeAlpha(texture);
    renderTexture.Rgba8 = texture.Rgba8;
    renderTexture.Rgba8ByteCount = renderTexture.Rgba8.size();
    uint64_t mipChainHash = 14695981039346656037ull;
    mipChainHash = Fnv1a64Append(mipChainHash, renderTexture.Rgba8);
    bool mipChainDecoded = renderTexture.Rgba8Decoded;
    renderTexture.AdditionalMipLevels.reserve(texture.AdditionalMipLevels.size());
    for (const auto& sourceMip : texture.AdditionalMipLevels) {
        Oot3dNativeRenderTextureMipLevel mip;
        mip.Level = sourceMip.Level;
        mip.Width = sourceMip.Width;
        mip.Height = sourceMip.Height;
        mip.Rgba8Decoded = sourceMip.Rgba8Decoded;
        mip.Rgba8 = sourceMip.Rgba8;
        mip.Rgba8ByteCount = mip.Rgba8.size();
        mip.Rgba8Hash = mip.Rgba8Decoded ? Fnv1a64(mip.Rgba8) : 0;
        renderTexture.Rgba8ByteCount += mip.Rgba8ByteCount;
        mipChainHash = Fnv1a64Append(mipChainHash, mip.Rgba8);
        mipChainDecoded = mipChainDecoded && mip.Rgba8Decoded;
        renderTexture.AdditionalMipLevels.push_back(std::move(mip));
    }
    renderTexture.Rgba8HashAvailable =
        mipChainDecoded && DecodedTexturePayloadsHaveStableIdentity(renderTexture);
    renderTexture.Rgba8Hash = renderTexture.Rgba8HashAvailable ? mipChainHash : 0;
    return renderTexture;
}

Oot3dNativeRenderTexture BuildRenderTextureFromCtxbTexture(const CtxbTexture& texture, uint32_t sourceIndex) {
    Oot3dNativeRenderTexture renderTexture;
    renderTexture.SourceIndex = sourceIndex;
    renderTexture.Name = texture.Name;
    renderTexture.Width = texture.Width;
    renderTexture.Height = texture.Height;
    renderTexture.TextureFormat = texture.TextureFormat;
    renderTexture.DataType = texture.DataType;
    renderTexture.MipmapCount = 1;
    renderTexture.MipmapLayoutDecoded = true;
    renderTexture.Rgba8Decoded = texture.Rgba8Decoded;
    renderTexture.HasNativeAlpha = texture.TextureFormat == kPicaTextureRgba ||
                                   texture.TextureFormat == kPicaTextureAlpha ||
                                   texture.TextureFormat == kPicaTextureLuminanceAlpha ||
                                   texture.TextureFormat == kPicaTextureEtc1A4;
    renderTexture.Rgba8 = texture.Rgba8;
    renderTexture.Rgba8ByteCount = renderTexture.Rgba8.size();
    renderTexture.Rgba8HashAvailable = renderTexture.Rgba8Decoded;
    renderTexture.Rgba8Hash = renderTexture.Rgba8HashAvailable ? Fnv1a64(renderTexture.Rgba8) : 0;
    return renderTexture;
}

size_t ValidRawTextureStageCount(const CmbMaterial& material, const CmbModel& model) {
    if (!material.RawTextureStageSelectorDecoded) {
        return 0;
    }
    const size_t stageCount = std::min<size_t>(material.RawTextureStageCount, material.RawTextureStageSlots.size());
    size_t validCount = 0;
    for (size_t stage = 0; stage < stageCount; ++stage) {
        const int textureEnvIndex = material.RawTextureStageSlots[stage];
        if (textureEnvIndex >= 0 && static_cast<size_t>(textureEnvIndex) < model.TextureEnvSettings.size()) {
            ++validCount;
        }
    }
    return validCount;
}

bool TextureEnvRgbOperandIsSrcColor(const std::array<uint16_t, 3>& operands) {
    return operands[0] == kPicaTextureEnvOperandSourceColor &&
           operands[1] == kPicaTextureEnvOperandSourceColor &&
           operands[2] == kPicaTextureEnvOperandSourceColor;
}

bool TextureEnvRgbCombineKnown(uint16_t value) {
    switch (value) {
        case kPicaTextureEnvCombineReplace:
        case 0x2100:
        case 0x0104:
        case 0x8574:
        case 0x8575:
        case 0x84E7:
        case 0x86AE:
        case 0x86AF:
        case 0x6401:
        case 0x6402:
            return true;
        default:
            return false;
    }
}

size_t TextureEnvRgbActiveSourceCount(uint16_t value) {
    switch (value) {
        case kPicaTextureEnvCombineReplace:
            return 1;
        case 0x2100:
        case 0x0104:
        case 0x8574:
        case 0x84E7:
        case 0x86AE:
        case 0x86AF:
            return 2;
        case 0x8575:
        case 0x6401:
        case 0x6402:
            return 3;
        default:
            return 3;
    }
}

bool TextureEnvRgbSourceKnown(uint16_t value) {
    switch (value) {
        case 0x8577:
        case 0x6210:
        case 0x6211:
        case 0x84C0:
        case 0x84C1:
        case 0x84C2:
        case 0x84C3:
        case 0x8579:
        case 0x8576:
        case 0x8578:
            return true;
        default:
            return false;
    }
}

bool TextureEnvRgbOperandKnown(uint16_t value) {
    switch (value) {
        case 0x0300:
        case 0x0301:
        case 0x0302:
        case 0x0303:
        case 0x8580:
        case 0x8583:
        case 0x8581:
        case 0x8584:
        case 0x8582:
        case 0x8585:
            return true;
        default:
            return false;
    }
}

bool TextureEnvScaleKnown(uint16_t value) {
    return value == 1 || value == 2 || value == 4;
}

bool NativeCompareFunctionKnown(uint16_t value) {
    return value >= 0x0200 && value <= 0x0207;
}

bool NativeBlendFactorKnown(uint16_t value) {
    switch (value) {
        case 0x0000:
        case 0x0001:
        case 0x0300:
        case 0x0301:
        case 0x0302:
        case 0x0303:
        case 0x0304:
        case 0x0305:
        case 0x0306:
        case 0x0307:
        case 0x0308:
        case 0x8001:
        case 0x8002:
        case 0x8003:
        case 0x8004:
            return true;
        default:
            return false;
    }
}

bool NativeBlendEquationKnown(uint16_t value) {
    switch (value) {
        case 0x8006:
        case 0x8007:
        case 0x8008:
        case 0x800A:
        case 0x800B:
            return true;
        default:
            return false;
    }
}

float TextureEnvScaleMultiplier(uint16_t value) {
    return TextureEnvScaleKnown(value) ? static_cast<float>(value) : 1.0f;
}

std::string NativeTextureEnvColorShaderPath(const CmbMaterialTextureEnvSetting& textureEnv) {
    if (!textureEnv.Decoded) {
        return {};
    }

    if (textureEnv.CombineRgb == kPicaTextureEnvCombineModulate &&
        textureEnv.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
        textureEnv.SourceRgb[1] == kPicaTextureEnvSourceTexture0 &&
        TextureEnvRgbOperandIsSrcColor(textureEnv.OperandRgb)) {
        return "texenv_modulate_primary_color_texture0";
    }

    if (textureEnv.CombineRgb == kPicaTextureEnvCombineReplace &&
        textureEnv.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
        textureEnv.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
        TextureEnvScaleMultiplier(textureEnv.ColorScale) == 1.0f) {
        return "texenv_replace_primary_color";
    }

    return {};
}

Oot3dNativeRenderTextureEnvState BuildTextureEnvState(const CmbMaterialTextureEnvSetting& textureEnv,
                                                      uint32_t stageOrder) {
    Oot3dNativeRenderTextureEnvState state;
    state.TableIndex = textureEnv.Index;
    state.SourceOffset = textureEnv.SourceOffset;
    state.StageOrder = stageOrder;
    state.Decoded = textureEnv.Decoded;
    state.RawSize = static_cast<uint32_t>(textureEnv.RawTextureEnv.size());
    state.RawFnv1a64 = Fnv1a64(textureEnv.RawTextureEnv);
    state.CombineRgb = textureEnv.CombineRgb;
    state.CombineAlpha = textureEnv.CombineAlpha;
    state.RgbActiveSourceCount = static_cast<uint32_t>(TextureEnvRgbActiveSourceCount(textureEnv.CombineRgb));
    state.ColorScale = textureEnv.ColorScale;
    state.AlphaScale = textureEnv.AlphaScale;
    state.ColorScaleSupported = TextureEnvScaleKnown(textureEnv.ColorScale);
    state.AlphaScaleSupported = TextureEnvScaleKnown(textureEnv.AlphaScale);
    state.ColorScaleMultiplier = TextureEnvScaleMultiplier(textureEnv.ColorScale);
    state.AlphaScaleMultiplier = TextureEnvScaleMultiplier(textureEnv.AlphaScale);
    state.UnknownUshort1 = textureEnv.UnknownUshort1;
    state.UnknownGlConstant = textureEnv.UnknownGlConstant;
    state.SourceRgb = textureEnv.SourceRgb;
    state.OperandRgb = textureEnv.OperandRgb;
    state.SourceAlpha = textureEnv.SourceAlpha;
    state.OperandAlpha = textureEnv.OperandAlpha;
    state.UnknownUshort2 = textureEnv.UnknownUshort2;
    state.ConstantColorIndex = textureEnv.UnknownUshort2[0];
    state.ConstantColorIndexRecognized =
        state.ConstantColorIndex < kOot3dCmbMaterialConstantColorCount;
    state.ColorShaderPath = NativeTextureEnvColorShaderPath(textureEnv);
    state.ColorShaderPathSupported = !state.ColorShaderPath.empty();
    state.RequiresShaderEvaluation = textureEnv.Decoded;
    return state;
}

bool TextureCoordTransformed(const CmbTextureCoord& coord) {
    constexpr float epsilon = 0.000001f;
    return coord.CoordinateIndex != 0 ||
           std::abs(coord.Scale.X - 1.0f) > epsilon ||
           std::abs(coord.Scale.Y - 1.0f) > epsilon ||
           std::abs(coord.Rotation) > epsilon ||
           std::abs(coord.Translation.X) > epsilon ||
           std::abs(coord.Translation.Y) > epsilon;
}

bool TextureCoordTransformed(const Oot3dNativeRenderTextureCoordState& coord) {
    constexpr float epsilon = 0.000001f;
    return coord.CoordinateIndex != 0 ||
           std::abs(coord.Scale.X - 1.0f) > epsilon ||
           std::abs(coord.Scale.Y - 1.0f) > epsilon ||
           std::abs(coord.Rotation) > epsilon ||
           std::abs(coord.Translation.X) > epsilon ||
           std::abs(coord.Translation.Y) > epsilon;
}

void RefreshTextureCoordDerivedState(Oot3dNativeRenderTextureCoordState& coord) {
    coord.TransformAppliesToUv0 = coord.CoordinateIndex == 0;
    coord.Transformed = TextureCoordTransformed(coord);
    coord.Decoded = coord.Active && coord.TransformAppliesToUv0;
}

Oot3dNativeRenderTextureCoordState BuildTextureCoordState(const CmbMaterial& material,
                                                          uint32_t slot,
                                                          TextureBinding binding) {
    Oot3dNativeRenderTextureCoordState state;
    state.Slot = slot;
    state.Active = material.TextureCoordsUsed == 0 || slot < material.TextureCoordsUsed;
    state.SelectedPrimary = binding.Valid && binding.Slot == slot;
    if (slot >= 3) {
        return state;
    }

    const auto& coord = material.TextureCoords[slot];
    state.MatrixMode = coord.MatrixMode;
    state.ReferenceCamera = coord.ReferenceCamera;
    state.MappingMethod = coord.MappingMethod;
    state.CoordinateIndex = coord.CoordinateIndex;
    state.Scale = coord.Scale;
    state.Rotation = coord.Rotation;
    state.Translation = coord.Translation;
    RefreshTextureCoordDerivedState(state);
    state.Source = "oot3d_cmb_material_texture_coord";
    return state;
}

void AddKnownPicaMaterialLutInputBits(Oot3dNativePicaMaterialLutInputState& state,
                                      size_t registerIndex, uint32_t bitShift,
                                      uint32_t fieldMask, uint32_t value,
                                      bool resolved, std::string fieldName) {
    if (!resolved || registerIndex >= state.Registers.size()) {
        state.UnresolvedFields.push_back(std::move(fieldName));
        return;
    }
    auto& reg = state.Registers[registerIndex];
    const uint32_t shiftedMask = fieldMask << bitShift;
    reg.KnownBitMask |= shiftedMask;
    reg.KnownValue = (reg.KnownValue & ~shiftedMask) |
                     ((value & fieldMask) << bitShift);
    reg.HasKnownBits = true;
    ++state.ResolvedFieldCount;
    state.ResolvedFields.push_back(std::move(fieldName));
}

struct PicaMaterialLutSamplerDescriptor {
    const char* Sampler;
    const char* PicaRole;
    uint32_t AbsBitShift;
    uint32_t LutInputBitShift;
    uint32_t ScaleBitShift;
};

constexpr std::array<PicaMaterialLutSamplerDescriptor, 7> kPicaMaterialLutSamplerDescriptors = {
    PicaMaterialLutSamplerDescriptor{ "d0", "distribution0", 1, 0, 0 },
    PicaMaterialLutSamplerDescriptor{ "d1", "distribution1", 5, 4, 4 },
    PicaMaterialLutSamplerDescriptor{ "sp", "spotlight_attenuation", 9, 8, 8 },
    PicaMaterialLutSamplerDescriptor{ "fr", "fresnel", 13, 12, 12 },
    PicaMaterialLutSamplerDescriptor{ "rb", "reflect_blue", 17, 16, 16 },
    PicaMaterialLutSamplerDescriptor{ "rg", "reflect_green", 21, 20, 20 },
    PicaMaterialLutSamplerDescriptor{ "rr", "reflect_red", 25, 24, 24 },
};

uint32_t ExtractBitField(uint32_t value, uint32_t shift, uint32_t mask) {
    return (value >> shift) & mask;
}

bool PicaMaterialLutInputRegisterFieldKnown(const Oot3dNativePicaMaterialLutInputRegisterState& reg,
                                            uint32_t shift, uint32_t mask) {
    const uint32_t shiftedMask = mask << shift;
    return (reg.KnownBitMask & shiftedMask) == shiftedMask;
}

std::string PicaMaterialLutInputName(uint32_t value) {
    switch (value) {
        case 0:
            return "NH";
        case 1:
            return "VH";
        case 2:
            return "NV";
        case 3:
            return "LN";
        case 4:
            return "SP";
        case 5:
            return "CP";
        default:
            return "unknown";
    }
}

std::string PicaMaterialLutInputExpression(uint32_t value) {
    switch (value) {
        case 0:
            return "dot(normal, normalize(half_vector))";
        case 1:
            return "dot(normalize(view), normalize(half_vector))";
        case 2:
            return "dot(normal, normalize(view))";
        case 3:
            return "dot(light_vector, normal)";
        case 4:
            return "dot(light_vector, spot_dir)";
        case 5:
            return "config7_dot(projected_half_vector, tangent)";
        default:
            return "unknown";
    }
}

float PicaMaterialLutScaleValue(uint32_t value) {
    switch (value) {
        case 0:
            return 1.0f;
        case 1:
            return 2.0f;
        case 2:
            return 4.0f;
        case 3:
            return 8.0f;
        case 6:
            return 0.25f;
        case 7:
            return 0.5f;
        default:
            return 0.0f;
    }
}

std::array<Oot3dNativePicaMaterialLutSamplerState, 7> BuildPicaMaterialLutSamplerStates(
    const Oot3dNativePicaMaterialLutInputState& state) {
    std::array<Oot3dNativePicaMaterialLutSamplerState, 7> samplers{};
    for (size_t i = 0; i < samplers.size(); ++i) {
        const auto& descriptor = kPicaMaterialLutSamplerDescriptors[i];
        auto& sampler = samplers[i];
        sampler.Sampler = descriptor.Sampler;
        sampler.PicaRole = descriptor.PicaRole;
        sampler.AbsBitShift = descriptor.AbsBitShift;
        sampler.LutInputBitShift = descriptor.LutInputBitShift;
        sampler.ScaleBitShift = descriptor.ScaleBitShift;
        if (!state.Available) {
            continue;
        }

        const auto& absReg = state.Registers[0];
        const auto& inputReg = state.Registers[1];
        const auto& scaleReg = state.Registers[2];
        sampler.AbsInputKnown =
            PicaMaterialLutInputRegisterFieldKnown(absReg, descriptor.AbsBitShift, 0x1);
        if (sampler.AbsInputKnown) {
            sampler.AbsDisableBit = ExtractBitField(absReg.KnownValue, descriptor.AbsBitShift, 0x1);
            sampler.AbsInputEnabled = sampler.AbsDisableBit == 0;
        }

        sampler.LutInputKnown =
            PicaMaterialLutInputRegisterFieldKnown(inputReg, descriptor.LutInputBitShift, 0xF);
        if (sampler.LutInputKnown) {
            sampler.LutInputRaw = ExtractBitField(inputReg.KnownValue, descriptor.LutInputBitShift, 0xF);
            sampler.LutInputRawHighBitSet = (sampler.LutInputRaw & 0x8) != 0;
            sampler.LutInput = sampler.LutInputRaw & 0x7;
            sampler.LutInputName = PicaMaterialLutInputName(sampler.LutInput);
            sampler.LutInputExpression = PicaMaterialLutInputExpression(sampler.LutInput);
            sampler.LutInputShaderSemanticResolved =
                !sampler.LutInputRawHighBitSet && sampler.LutInput <= 5;
        }

        sampler.ScaleKnown =
            PicaMaterialLutInputRegisterFieldKnown(scaleReg, descriptor.ScaleBitShift, 0xF);
        if (sampler.ScaleKnown) {
            sampler.Scale = ExtractBitField(scaleReg.KnownValue, descriptor.ScaleBitShift, 0xF);
            sampler.ScaleValue = PicaMaterialLutScaleValue(sampler.Scale);
        }
        sampler.Complete = sampler.AbsInputKnown && sampler.LutInputKnown && sampler.ScaleKnown;
    }
    return samplers;
}

Oot3dNativePicaMaterialLutInputState BuildMaterialPicaLutInputState(
    const CmbMaterialLightingBlock& block,
    const NativeKankyoDrawHandleSubmitContract& submit) {
    Oot3dNativePicaMaterialLutInputState state;
    if (!block.Decoded) {
        return state;
    }

    state.Available = true;
    state.SourceKind = "oot3d_cmb_material_lighting_block_codebin_0040d040";
    state.ConsumerAddress = submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress;
    state.PacketWordCount = submit.MaterialLaneCopiedBlockPicaConfigPacketWordCount;
    for (size_t i = 0; i < state.Registers.size(); ++i) {
        state.Registers[i].Register = submit.MaterialLaneCopiedBlockPicaConfigRegisters[i];
        state.Registers[i].RegisterName =
            submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[i];
    }

    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift, 0x1,
        block.PicaLutInputAbsD0DisableBit, block.PicaLutInputAbsD0DisableBitResolved,
        "GPUREG_LIGHTING_LUTINPUT_ABS.disable_d0");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[0], 0xF,
        block.PicaLightingConfig, block.PicaLightingConfigRecognized,
        "GPUREG_LIGHTING_LUTINPUT_SELECT.d0");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[0], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SCALE.d0");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[1], 0x1,
        1, true, "GPUREG_LIGHTING_LUTINPUT_ABS.disable_d1");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[1], 0xF,
        block.PicaBumpTextureUnit, block.PicaBumpTextureUnitRecognized,
        "GPUREG_LIGHTING_LUTINPUT_SELECT.d1");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[1], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SCALE.d1");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[2], 0x1,
        block.PicaLutInputAbsSpDisableBit, block.PicaLutInputAbsSpDisableBitResolved,
        "GPUREG_LIGHTING_LUTINPUT_ABS.disable_sp");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[2], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SELECT.sp");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[2], 0xF,
        block.PicaLutScaleSp, block.PicaLutScaleSpResolved,
        "GPUREG_LIGHTING_LUTINPUT_SCALE.sp");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[3], 0xF,
        block.PicaLutInputFr, block.PicaLutInputFrResolved,
        "GPUREG_LIGHTING_LUTINPUT_SELECT.fr");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[3], 0x1,
        block.PicaLutInputAbsFrDisableBit, block.PicaLutInputAbsFrDisableBitResolved,
        "GPUREG_LIGHTING_LUTINPUT_ABS.disable_fr");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[3], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SCALE.fr");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[4], 0x1,
        block.PicaLutInputAbsRbDisableBit, block.PicaLutInputAbsRbDisableBitResolved,
        "GPUREG_LIGHTING_LUTINPUT_ABS.disable_rb");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[4], 0xF,
        block.PicaLutInputRb, block.PicaLutInputRbRecognized,
        "GPUREG_LIGHTING_LUTINPUT_SELECT.rb");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[4], 0xF,
        block.PicaLutScaleRb, block.PicaLutScaleRbRecognized,
        "GPUREG_LIGHTING_LUTINPUT_SCALE.rb");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[5], 0x1,
        1, true, "GPUREG_LIGHTING_LUTINPUT_ABS.disable_rg");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[5], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SELECT.rg");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[5], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SCALE.rg");
    AddKnownPicaMaterialLutInputBits(
        state, 0, submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[6], 0x1,
        1, true, "GPUREG_LIGHTING_LUTINPUT_ABS.disable_rr");
    AddKnownPicaMaterialLutInputBits(
        state, 1, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[6], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SELECT.rr");
    AddKnownPicaMaterialLutInputBits(
        state, 2, submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[6], 0xF,
        0, true, "GPUREG_LIGHTING_LUTINPUT_SCALE.rr");

    constexpr uint32_t kMaterialLutInputFieldCount = 21;
    state.Complete =
        state.ResolvedFieldCount == kMaterialLutInputFieldCount &&
        state.UnresolvedFields.empty();
    state.SourceStatus =
        state.Complete
            ? "complete_register_bits_from_codebin_lane_reset_and_cmb_material_block"
            : "partial_known_register_bits_only_unresolved_material_lane_fields_not_zero_filled";
    state.Samplers = BuildPicaMaterialLutSamplerStates(state);
    return state;
}

bool RawMaterialHasRange(const CmbMaterial& material, uint32_t offset, size_t size) {
    return offset <= material.RawMaterial.size() && size <= material.RawMaterial.size() - offset;
}

std::optional<uint32_t> RawMaterialU8(const CmbMaterial& material, uint32_t offset) {
    if (!RawMaterialHasRange(material, offset, sizeof(uint8_t))) {
        return std::nullopt;
    }
    return material.RawMaterial[offset];
}

std::optional<uint32_t> RawMaterialU16(const CmbMaterial& material, uint32_t offset) {
    if (!RawMaterialHasRange(material, offset, sizeof(uint16_t))) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(material.RawMaterial[offset]) |
           (static_cast<uint32_t>(material.RawMaterial[offset + 1]) << 8);
}

std::optional<int32_t> RawMaterialS16(const CmbMaterial& material, uint32_t offset) {
    const auto value = RawMaterialU16(material, offset);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<int16_t>(*value);
}

std::optional<uint32_t> FragmentLightingConfigPayload0(
    uint32_t type, uint32_t defaultType, uint32_t flags, bool primaryEnableKnown,
    uint32_t primaryEnable, bool primaryModeKnown, uint32_t primaryMode,
    uint32_t fallbackMask) {
    if (type != defaultType) {
        return fallbackMask;
    }
    if ((flags & 0x0F) == 0) {
        return 0;
    }
    if (!primaryEnableKnown || !primaryModeKnown) {
        return std::nullopt;
    }
    return primaryEnable == 0 && primaryMode == 0 ? 0 : fallbackMask;
}

std::optional<uint32_t> FragmentLightingConfigPayload2(
    uint32_t type, uint32_t defaultType, uint32_t alternateType, uint32_t flags,
    bool secondaryEnableKnown, uint32_t secondaryEnable, bool secondaryModeKnown,
    uint32_t secondaryMode, uint32_t enableBit) {
    if (type == alternateType) {
        return enableBit;
    }
    if (type != defaultType) {
        return 0;
    }
    if (!secondaryEnableKnown) {
        return std::nullopt;
    }
    if (secondaryEnable == 0) {
        return 0;
    }
    if (secondaryModeKnown && secondaryMode != 0) {
        return enableBit;
    }
    if ((flags & 0x0F) != 0) {
        return enableBit;
    }
    if (secondaryModeKnown) {
        return 0;
    }
    return std::nullopt;
}

std::optional<uint32_t> FragmentLightingConfigPayload3(
    uint32_t type, uint32_t defaultType, bool secondaryEnableKnown,
    uint32_t secondaryEnable, bool secondaryModeKnown, uint32_t secondaryMode,
    uint32_t enableBit) {
    if (type != defaultType) {
        return 0;
    }
    if (!secondaryEnableKnown) {
        return std::nullopt;
    }
    if (secondaryEnable == 0) {
        return 0;
    }
    if (!secondaryModeKnown) {
        return std::nullopt;
    }
    return secondaryMode != 0 ? enableBit : 0;
}

std::string FragmentLightingConfigPayloadRegisterName(size_t index) {
    switch (index) {
        case 0:
            return "GPUREG_FRAGMENT_LIGHTING_CONFIG0";
        case 1:
            return "GPUREG_FRAGMENT_LIGHTING_CONFIG1";
        case 2:
            return "GPUREG_FRAGMENT_LIGHTING_CONFIG2";
        case 3:
            return "GPUREG_FRAGMENT_LIGHTING_CONFIG3";
        default:
            return "GPUREG_FRAGMENT_LIGHTING_CONFIG_UNKNOWN";
    }
}

void SetFragmentLightingPayloadWord(
    Oot3dNativePicaFragmentLightingConfigState& state, size_t index,
    std::optional<uint32_t> value, std::string source) {
    if (index >= state.PayloadWords.size()) {
        return;
    }
    auto& word = state.PayloadWords[index];
    if (value.has_value()) {
        word.Value = *value;
        word.Known = true;
        ++state.KnownPayloadWordCount;
        state.ResolvedFields.push_back(word.RegisterName);
    } else {
        word.Known = false;
        state.UnresolvedFields.push_back(word.RegisterName);
    }
    word.Source = std::move(source);
}

void RemoveUnresolvedField(Oot3dNativePicaFragmentLightingConfigState& state,
                           std::string_view field) {
    state.UnresolvedFields.erase(
        std::remove(state.UnresolvedFields.begin(), state.UnresolvedFields.end(), field),
        state.UnresolvedFields.end());
}

Oot3dNativePicaFragmentLightingConfigState BuildMaterialFragmentLightingConfigState(
    const CmbMaterial& material, const NativePicaLightingRegisterEmitterContract& emitter,
    const NativeKankyoDrawHandleSubmitContract& submit) {
    Oot3dNativePicaFragmentLightingConfigState state;
    if (!emitter.FragmentLightingConfigPayloadLogicResolvedFromCodebin ||
        !emitter.FragmentLightingConfigPreparedStateInitializerResolvedFromCodebin ||
        !emitter.FragmentLightingConfigMaterialSetupSourceResolvedFromCodebin) {
        return state;
    }

    state.Available = true;
    state.SourceKind = "oot3d_cmb_material_fragment_lighting_config_codebin_003fad68_00313d6c";
    state.EmitterAddress = emitter.FragmentLightingConfigEmitterAddress;
    state.MaterialSetupAddress = emitter.FragmentLightingConfigMaterialSetupAddress;
    state.PreparedStateInitializerAddress = emitter.FragmentLightingConfigPreparedStateInitializerAddress;
    state.PacketRegister = emitter.FragmentLightingConfigRegister;
    state.PacketHeader = emitter.FragmentLightingConfigHeader;
    state.PayloadWordCount = emitter.FragmentLightingConfigPayloadWordCount;
    state.NativeType = emitter.FragmentLightingConfigNativeDefaultType;
    state.Flags = emitter.FragmentLightingConfigPreparedStateInitializerDefaultFlags;
    state.PrimaryMode = emitter.FragmentLightingConfigPreparedStateInitializerDefaultPrimaryMode;
    state.PrimaryModeKnown = true;
    state.PrimarySourceStatus =
        "runtime_material_lane_0x1c0_or_default_table_0x004e056c_pending_material_override_gate_0x0b";
    state.SecondaryModeSourceStatus = "not_resolved";
    for (size_t i = 0; i < state.PayloadWords.size(); ++i) {
        state.PayloadWords[i].Register = emitter.FragmentLightingConfigPayloadRegisters[i];
        state.PayloadWords[i].RegisterName = FragmentLightingConfigPayloadRegisterName(i);
    }

    const bool overrideGateOffsetMatches =
        emitter.FragmentLightingConfigRuntimeStateOverrideGateOffset ==
        emitter.FragmentLightingConfigMaterialPrimaryOverrideGateOffset;
    const bool secondaryModeOffsetMatches =
        emitter.FragmentLightingConfigRuntimeStateSecondaryModeOffset ==
        emitter.FragmentLightingConfigMaterialSecondaryOverrideModeOffset;
    if (emitter.FragmentLightingConfigRuntimeStateDefaultsResolvedFromCodebin &&
        overrideGateOffsetMatches && secondaryModeOffsetMatches) {
        state.RuntimeStateInitializerAddress =
            emitter.FragmentLightingConfigRuntimeStateInitializerAddress;
        state.RuntimeOverrideGateDefaultValue =
            emitter.FragmentLightingConfigRuntimeStateOverrideGateDefaultValue;
        state.RuntimeOverrideGateDefaultKnown = true;
        state.RuntimeSecondaryModeDefaultValue =
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeDefaultValue;
        state.RuntimeSecondaryModeDefaultKnown = true;
        state.ResolvedFields.push_back("runtime_state.override_gate_default");
        state.ResolvedFields.push_back("runtime_state.secondary_mode_default");
    }
    if (emitter.FragmentLightingConfigRuntimeStateCopyHelperResolvedFromCodebin &&
        overrideGateOffsetMatches && secondaryModeOffsetMatches) {
        state.RuntimeStateCopyHelperAddress =
            emitter.FragmentLightingConfigRuntimeStateCopyHelperAddress;
        state.RuntimeOverrideGateCopyPathKnown =
            emitter.FragmentLightingConfigRuntimeStateOverrideGateCopiedByCopyHelper;
        state.RuntimeSecondaryModeCopyPathKnown =
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeCopiedByCopyHelper;
        if (state.RuntimeOverrideGateCopyPathKnown) {
            state.ResolvedFields.push_back("runtime_state.override_gate_copy_path");
        }
        if (state.RuntimeSecondaryModeCopyPathKnown) {
            state.ResolvedFields.push_back("runtime_state.secondary_mode_copy_path");
        }
    }
    if (emitter.FragmentLightingConfigRuntimeStateWriterScanResolvedFromCodebin) {
        state.RuntimeStateWriterScanResolved = true;
        state.RuntimeStateWritersClassifiedAsDrawLocal =
            emitter.FragmentLightingConfigRuntimeStateWritersClassifiedAsDrawLocal;
        state.RuntimeActiveOverrideProducerResolvedFromWriterScan =
            emitter.FragmentLightingConfigRuntimeStateActiveProducerResolvedFromWriterScan;
        const auto overrideCount = std::min<size_t>(
            emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddressCount,
            emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddresses.size());
        state.RuntimeOverrideWriterAddresses.assign(
            emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddresses.begin(),
            emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddresses.begin() + overrideCount);
        const auto secondaryCount = std::min<size_t>(
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddressCount,
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses.size());
        state.RuntimeSecondaryModeWriterAddresses.assign(
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses.begin(),
            emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses.begin() +
                secondaryCount);
        state.ResolvedFields.push_back("runtime_state.writer_scan");
        if (!state.RuntimeActiveOverrideProducerResolvedFromWriterScan) {
            state.UnresolvedFields.push_back("runtime_state.active_override_producer");
        }
    }
    if (emitter.FragmentLightingConfigDrawEntrySubmitRouteResolvedFromCodebin) {
        state.RuntimeGameplayDrawAddress =
            emitter.FragmentLightingConfigGameplayDrawAddress;
        state.RuntimeGameplayDrawDispatcherAddress =
            emitter.FragmentLightingConfigGameplayDrawDispatcherAddress;
        state.RuntimeGameplayDrawDispatcherCallsiteAddress =
            emitter.FragmentLightingConfigGameplayDrawDispatcherCallsiteAddress;
        state.RuntimeDrawEntrySubmitAddress =
            emitter.FragmentLightingConfigDrawEntrySubmitAddress;
        const auto callsiteCount = std::min<size_t>(
            emitter.FragmentLightingConfigDrawEntrySubmitCallsiteCount,
            emitter.FragmentLightingConfigDrawEntrySubmitCallsiteAddresses.size());
        state.RuntimeDrawEntrySubmitCallsiteAddresses.assign(
            emitter.FragmentLightingConfigDrawEntrySubmitCallsiteAddresses.begin(),
            emitter.FragmentLightingConfigDrawEntrySubmitCallsiteAddresses.begin() +
                callsiteCount);
        state.RuntimeDrawEntryFlagsOffset =
            emitter.FragmentLightingConfigDrawEntryFlagsOffset;
        state.RuntimeDrawEntryRenderContextPointerOffset =
            emitter.FragmentLightingConfigDrawEntryRenderContextPointerOffset;
        state.RuntimeDrawEntryCallbackOffset =
            emitter.FragmentLightingConfigDrawEntryCallbackOffset;
        state.RuntimeDrawEntryVisibilityStateOffset =
            emitter.FragmentLightingConfigDrawEntryVisibilityStateOffset;
        state.RuntimeDrawEntrySubmittedByteOffset =
            emitter.FragmentLightingConfigDrawEntrySubmittedByteOffset;
        state.RuntimeDrawEntryFadeCounterOffset =
            emitter.FragmentLightingConfigDrawEntryFadeCounterOffset;
        state.RuntimeDrawEntryFadeLimitOffset =
            emitter.FragmentLightingConfigDrawEntryFadeLimitOffset;
        state.RuntimeDrawEntryOverrideGateFlagMask =
            emitter.FragmentLightingConfigDrawEntryOverrideGateFlagMask;
        state.RuntimeDrawEntryOverrideGateForceFullFlagMask =
            emitter.FragmentLightingConfigDrawEntryOverrideGateForceFullFlagMask;
        state.RuntimeDrawEntrySubmitRouteResolved = true;
        state.RuntimeDrawEntryOverrideGateRuleResolved =
            emitter.FragmentLightingConfigDrawEntryOverrideGateRuleResolvedFromCodebin;
        state.RuntimeDrawEntryRoutePromotesActiveMaterialOverride =
            emitter.FragmentLightingConfigDrawEntryRoutePromotesActiveMaterialOverride;
        state.ResolvedFields.push_back("runtime_state.draw_entry_submit_route");
        if (state.RuntimeDrawEntryOverrideGateRuleResolved) {
            state.ResolvedFields.push_back("runtime_state.draw_entry_override_gate_rule");
        }
        if (!state.RuntimeDrawEntryRoutePromotesActiveMaterialOverride) {
            state.UnresolvedFields.push_back("runtime_state.active_material_draw_entry_binding");
        }
    }
    if (emitter.FragmentLightingConfigSubmitManagerMaterialRouteResolvedFromCodebin) {
        state.RuntimeSubmitManagerVtableAddress =
            emitter.FragmentLightingConfigSubmitManagerVtableAddress;
        state.RuntimeSubmitManagerMaterialConfigSlotOffset =
            emitter.FragmentLightingConfigSubmitManagerMaterialConfigSlotOffset;
        state.RuntimeSubmitManagerMaterialConfigAddress =
            emitter.FragmentLightingConfigSubmitManagerMaterialConfigAddress;
        state.RuntimeSubmitManagerMaterialStateSlotOffset =
            emitter.FragmentLightingConfigSubmitManagerMaterialStateSlotOffset;
        state.RuntimeSubmitManagerMaterialStateAddress =
            emitter.FragmentLightingConfigSubmitManagerMaterialStateAddress;
        state.RuntimeSubmitManagerMaterialRouteResolved = true;
        state.RuntimeSubmitManagerMaterialRouteResolvesActiveOverrideGate =
            emitter.FragmentLightingConfigSubmitManagerMaterialRouteResolvesActiveOverrideGate;
        state.ResolvedFields.push_back("runtime_state.submit_manager_material_route");
        if (!state.RuntimeSubmitManagerMaterialRouteResolvesActiveOverrideGate) {
            state.UnresolvedFields.push_back(
                "runtime_state.submit_manager_material_route_active_override_gate");
        }
    }
    if (submit.MaterialDrawDispatchResolvedFromCodebin) {
        state.RuntimeMaterialLaneDispatchAddress = submit.MaterialDrawDispatchAddress;
        state.RuntimeMaterialLaneDispatchLoopEndAddress = submit.MaterialDrawDispatchLoopEndAddress;
        state.RuntimeMaterialLaneDispatchCmbMeshMaterialLaneByteOffset =
            submit.CmbMeshMaterialLaneByteOffset;
        state.RuntimeMaterialLaneDispatchMaterialLaneStrideBytes =
            submit.MaterialLaneStrideBytes;
        state.RuntimeMaterialLaneDispatchResolved = true;
        state.RuntimeMaterialLaneDispatchPromotesActiveOverrideGate =
            submit.MaterialDrawDispatchPromotesActiveOverrideGate;
        std::ostringstream source;
        source << "oot3d_codebin_" << std::hex << std::nouppercase << std::setfill('0')
               << std::setw(8) << submit.MaterialDrawDispatchAddress
               << "_material_draw_dispatch_cmb_mesh_material_index";
        state.RuntimeMaterialLaneDispatchSource = source.str();
        state.ResolvedFields.push_back("runtime_state.material_lane_dispatch");
        if (!state.RuntimeMaterialLaneDispatchPromotesActiveOverrideGate) {
            state.UnresolvedFields.push_back(
                "runtime_state.material_lane_dispatch_active_override_gate_promotion");
        }
    }

    const auto secondaryTypeSelector =
        RawMaterialU8(material, emitter.FragmentLightingConfigMaterialSecondaryTypeSelectorCmbOffset);
    if (secondaryTypeSelector.has_value()) {
        state.SecondaryTypeSelector = *secondaryTypeSelector;
        state.SecondaryTypeSelectorKnown = true;
        state.SecondaryTypeDisabled =
            state.SecondaryTypeSelector == emitter.FragmentLightingConfigMaterialSecondaryTypeDisabledValue;
        if (state.SecondaryTypeSelector < emitter.FragmentLightingConfigMaterialSecondaryTypeRegisterValues.size()) {
            state.SecondaryTypeRegisterValue =
                emitter.FragmentLightingConfigMaterialSecondaryTypeRegisterValues[state.SecondaryTypeSelector];
            state.SecondaryTypeRegisterValueKnown = true;
        } else if (state.SecondaryTypeDisabled) {
            state.SecondaryTypeRegisterValue = 0;
            state.SecondaryTypeRegisterValueKnown = true;
        }
        state.ResolvedFields.push_back("cmb_material.secondary_type_selector");
    } else {
        state.UnresolvedFields.push_back("cmb_material.secondary_type_selector");
    }

    const auto primaryBlendGate =
        RawMaterialU8(material, emitter.FragmentLightingConfigMaterialPrimaryCmbBlendGateOffset);
    if (primaryBlendGate.has_value()) {
        state.PrimaryCmbBlendGate = *primaryBlendGate;
        state.PrimaryCmbBlendGateKnown = true;
        state.ResolvedFields.push_back("cmb_material.primary_blend_gate");
        if (emitter.FragmentLightingConfigMaterialPrimaryCmbBlendGateOffset ==
                submit.CmbMaterialBlendModeByteOffset &&
            emitter.FragmentLightingConfigMaterialPrimarySourceRuntimeLaneOffset ==
                submit.MaterialLaneBlendEnabledByteOffset) {
            state.PrimaryRuntimeLaneValue =
                state.PrimaryCmbBlendGate == submit.CmbMaterialBlendModeEnabledValue ? 1u : 0u;
            state.PrimaryRuntimeLaneValueKnown = true;
            state.RuntimeMaterialPrimarySourceResolved = true;
            state.PrimarySourceStatus =
                "codebin_004c34ac_cmb_material_blend_gate_0x138_to_runtime_lane_0x1c0_"
                "active_payload_pending_material_override_gate_0x0b";
            state.ResolvedFields.push_back("runtime_material_lane.primary_enable_candidate");
        }
    } else {
        state.UnresolvedFields.push_back("cmb_material.primary_blend_gate");
    }

    const auto secondaryEnable =
        RawMaterialU8(material, emitter.FragmentLightingConfigMaterialSecondaryEnableCmbOffset);
    if (secondaryEnable.has_value()) {
        state.SecondaryEnable = *secondaryEnable != 0 ? 1 : 0;
        state.SecondaryEnableKnown = true;
        state.ResolvedFields.push_back("cmb_material.secondary_enable");
    } else {
        state.UnresolvedFields.push_back("cmb_material.secondary_enable");
    }

    const auto secondaryMode =
        RawMaterialU8(material, emitter.FragmentLightingConfigMaterialSecondaryModeCmbOffset);
    if (secondaryMode.has_value()) {
        state.SecondaryModeCmbCandidate = *secondaryMode;
        state.SecondaryModeCmbCandidateKnown = true;
        state.ResolvedFields.push_back("cmb_material.secondary_mode_candidate");
    } else {
        state.UnresolvedFields.push_back("cmb_material.secondary_mode_candidate");
    }

    if (state.SecondaryEnableKnown && state.SecondaryEnable == 0) {
        state.SecondaryMode = 0;
        state.SecondaryModeKnown = true;
        state.RuntimeSecondaryModeSourceResolved = true;
        state.SecondaryModeSourceStatus = "secondary_disabled_by_cmb_material_0x134";
    } else {
        state.SecondaryModeSourceStatus =
            "cmb_material_0x135_or_runtime_state_0x1ba_pending_material_override_gate_0x0b";
    }

    const auto secondaryParam =
        RawMaterialU16(material, emitter.FragmentLightingConfigMaterialSecondaryParamCmbOffset);
    if (secondaryParam.has_value()) {
        state.SecondaryParam = *secondaryParam;
        state.SecondaryParamKnown = true;
        state.ResolvedFields.push_back("cmb_material.secondary_param");
    } else {
        state.UnresolvedFields.push_back("cmb_material.secondary_param");
    }

    const auto auxByte =
        RawMaterialU8(material, emitter.FragmentLightingConfigMaterialAuxByteCmbOffset);
    if (auxByte.has_value()) {
        state.AuxByte = *auxByte;
        state.AuxByteKnown = true;
        state.ResolvedFields.push_back("cmb_material.aux_byte");
    } else {
        state.UnresolvedFields.push_back("cmb_material.aux_byte");
    }

    const auto auxHalfword =
        RawMaterialS16(material, emitter.FragmentLightingConfigMaterialAuxHalfwordCmbOffset);
    if (auxHalfword.has_value()) {
        state.AuxHalfword = *auxHalfword;
        state.AuxHalfwordKnown = true;
        state.ResolvedFields.push_back("cmb_material.aux_halfword");
    } else {
        state.UnresolvedFields.push_back("cmb_material.aux_halfword");
    }

    state.PrimaryEnableKnown = false;
    state.PrimaryEnable = 0;
    state.RuntimeOverrideGateResolved = false;
    const bool defaultOverrideGatePathResolved =
        state.RuntimeOverrideGateDefaultKnown &&
        state.RuntimeOverrideGateDefaultValue == 0 &&
        state.RuntimeSecondaryModeDefaultKnown &&
        state.RuntimeSecondaryModeDefaultValue == 1 &&
        state.RuntimeOverrideGateCopyPathKnown &&
        state.RuntimeSecondaryModeCopyPathKnown &&
        state.RuntimeStateWriterScanResolved &&
        state.RuntimeStateWritersClassifiedAsDrawLocal &&
        state.RuntimeDrawEntrySubmitRouteResolved &&
        state.RuntimeDrawEntryOverrideGateRuleResolved &&
        state.RuntimeMaterialLaneDispatchResolved &&
        state.RuntimeMaterialPrimarySourceResolved;
    if (defaultOverrideGatePathResolved) {
        state.RuntimeOverrideGateResolved = true;
        state.RuntimeOverrideGateResolutionSource =
            "AutoClass1_00347258_default_gate_0x0b_clear_plus_002d5f68_draw_entry_override_only_when_flag_0x80000000";
        state.PrimaryEnable = state.PrimaryRuntimeLaneValue;
        state.PrimaryEnableKnown = state.PrimaryRuntimeLaneValueKnown;
        state.PrimarySourceStatus =
            "codebin_004c34ac_cmb_material_blend_gate_0x138_to_runtime_lane_0x1c0_"
            "default_autoclass1_gate_0x0b_clear";
        state.ResolvedFields.push_back("runtime_state.default_override_gate_0x0b");
        state.ResolvedFields.push_back("runtime_state.draw_entry_override_gate_rule_default_path");
        RemoveUnresolvedField(state, "runtime_state.active_override_producer");
        RemoveUnresolvedField(state, "runtime_state.active_material_draw_entry_binding");
        RemoveUnresolvedField(state, "runtime_state.submit_manager_material_route_active_override_gate");
        RemoveUnresolvedField(state, "runtime_state.material_lane_dispatch_active_override_gate_promotion");

        if (!state.RuntimeSecondaryModeSourceResolved &&
            state.SecondaryModeCmbCandidateKnown) {
            state.SecondaryMode = state.SecondaryModeCmbCandidate;
            state.SecondaryModeKnown = true;
            state.RuntimeSecondaryModeSourceResolved = true;
            state.SecondaryModeSourceStatus =
                "cmb_material_0x135_default_autoclass1_gate_0x0b_clear";
            state.ResolvedFields.push_back("cmb_material.secondary_mode_default_gate_source");
        }
    }
    if (state.PrimaryRuntimeLaneValueKnown && state.PrimaryModeKnown) {
        const auto candidate = FragmentLightingConfigPayload0(
            state.NativeType, emitter.FragmentLightingConfigNativeDefaultType, state.Flags,
            true, state.PrimaryRuntimeLaneValue, state.PrimaryModeKnown, state.PrimaryMode,
            emitter.FragmentLightingConfigPayload0FallbackMask);
        if (candidate.has_value()) {
            state.PrimaryDefaultGatePayload0Candidate = *candidate;
            state.PrimaryDefaultGatePayload0CandidateKnown = true;
        }
    }

    const auto payload0Source =
        state.PrimaryEnableKnown && state.RuntimeOverrideGateResolved
            ? "FUN_00313D6C primary branch; primary enable from codebin_004c34ac runtime lane "
              "with AutoClass1 default gate clear"
            : "FUN_00313D6C primary branch; primary enable source pending FUN_003FAD68 override gate";
    const auto payload2Source =
        state.RuntimeSecondaryModeSourceResolved
            ? "FUN_00313D6C secondary enable/flags branch from CMB secondary enable, CMB/default "
              "secondary mode, and codebin defaults"
            : "FUN_00313D6C secondary enable/flags branch from CMB secondary enable and codebin defaults";
    const auto payload3Source =
        state.RuntimeSecondaryModeSourceResolved
            ? "FUN_00313D6C secondary enable/mode branch from CMB secondary enable and "
              "AutoClass1 default gate material mode"
            : "FUN_00313D6C secondary enable/mode branch; mode may depend on override gate";

    SetFragmentLightingPayloadWord(
        state, 0,
        FragmentLightingConfigPayload0(
            state.NativeType, emitter.FragmentLightingConfigNativeDefaultType, state.Flags,
            state.PrimaryEnableKnown, state.PrimaryEnable, state.PrimaryModeKnown, state.PrimaryMode,
            emitter.FragmentLightingConfigPayload0FallbackMask),
        payload0Source);

    SetFragmentLightingPayloadWord(
        state, 1,
        state.NativeType != emitter.FragmentLightingConfigNativeDefaultType || (state.Flags & 0x0F) != 0
            ? std::optional<uint32_t>(emitter.FragmentLightingConfigPayload1FallbackMask)
            : std::optional<uint32_t>(0),
        "FUN_00313D6C type/flags branch from codebin initializer defaults");

    SetFragmentLightingPayloadWord(
        state, 2,
        FragmentLightingConfigPayload2(
            state.NativeType, emitter.FragmentLightingConfigNativeDefaultType,
            emitter.FragmentLightingConfigNativeAlternateType, state.Flags, state.SecondaryEnableKnown,
            state.SecondaryEnable, state.SecondaryModeKnown, state.SecondaryMode,
            emitter.FragmentLightingConfigPayload2EnableBit),
        payload2Source);

    SetFragmentLightingPayloadWord(
        state, 3,
        FragmentLightingConfigPayload3(
            state.NativeType, emitter.FragmentLightingConfigNativeDefaultType,
            state.SecondaryEnableKnown, state.SecondaryEnable, state.SecondaryModeKnown,
            state.SecondaryMode, emitter.FragmentLightingConfigPayload3EnableBit),
        payload3Source);

    state.Complete =
        state.KnownPayloadWordCount == state.PayloadWords.size() && state.UnresolvedFields.empty();
    if (state.Complete) {
        state.SourceStatus = "complete_fragment_lighting_config_payload_from_codebin_and_cmb_material";
    } else if (state.RuntimeMaterialPrimarySourceResolved) {
        state.SourceStatus = "partial_fragment_lighting_config_payload_runtime_override_gate_pending";
    } else {
        state.SourceStatus =
            "partial_fragment_lighting_config_payload_runtime_override_gate_or_primary_source_pending";
    }
    return state;
}

uint8_t MultiplyColorComponent(uint8_t lhs, uint8_t rhs) {
    return static_cast<uint8_t>((static_cast<int>(lhs) * static_cast<int>(rhs)) / 255);
}

ColorRgba8 MultiplyColorRgb(ColorRgba8 lhs, ColorRgba8 rhs) {
    return {
        MultiplyColorComponent(lhs.R, rhs.R),
        MultiplyColorComponent(lhs.G, rhs.G),
        MultiplyColorComponent(lhs.B, rhs.B),
        lhs.A,
    };
}

ColorRgba8 MultiplyColorRgbByAlpha(ColorRgba8 lhs, uint8_t alpha) {
    return {
        MultiplyColorComponent(lhs.R, alpha),
        MultiplyColorComponent(lhs.G, alpha),
        MultiplyColorComponent(lhs.B, alpha),
        lhs.A,
    };
}

uint8_t AddColorComponentClamped(uint8_t lhs, uint8_t rhs) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(lhs) + static_cast<int>(rhs), 0, 255));
}

ColorRgba8 AddColorRgbClamped(ColorRgba8 lhs, ColorRgba8 rhs) {
    return {
        AddColorComponentClamped(lhs.R, rhs.R),
        AddColorComponentClamped(lhs.G, rhs.G),
        AddColorComponentClamped(lhs.B, rhs.B),
        lhs.A,
    };
}

Vec3f MultiplyVec3Scalar(Vec3f lhs, float rhs) {
    return { lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs };
}

Vec3f MultiplyVec3ColorRgb(Vec3f lhs, ColorRgba8 rhs) {
    return {
        lhs.X * static_cast<float>(rhs.R) / 255.0f,
        lhs.Y * static_cast<float>(rhs.G) / 255.0f,
        lhs.Z * static_cast<float>(rhs.B) / 255.0f,
    };
}

Vec3f MultiplyVec3ColorAlpha(Vec3f lhs, uint8_t alpha) {
    const float alphaMultiplier = static_cast<float>(alpha) / 255.0f;
    return MultiplyVec3Scalar(lhs, alphaMultiplier);
}

bool Vec3IsIdentity(Vec3f value) {
    return value.X == 1.0f && value.Y == 1.0f && value.Z == 1.0f;
}

bool ColorRgbIsZero(ColorRgba8 color) {
    return color.R == 0 && color.G == 0 && color.B == 0;
}

bool ColorRgbIsNeutral(ColorRgba8 color) {
    return color.R == color.G && color.G == color.B;
}

bool ColorRgbaEquals(ColorRgba8 lhs, ColorRgba8 rhs) {
    return lhs.R == rhs.R && lhs.G == rhs.G && lhs.B == rhs.B && lhs.A == rhs.A;
}

struct ResolvedTextureEnvConstantColor {
    bool Resolved = false;
    int32_t Index = -1;
    std::string Source;
    ColorRgba8 Color;
};

ResolvedTextureEnvConstantColor ResolveTextureEnvConstantColor(
    const std::vector<Oot3dNativeRenderTextureEnvState>& stages,
    const std::array<ColorRgba8, kOot3dCmbMaterialConstantColorCount>& constants) {
    int32_t referencedIndex = -1;
    for (const auto& stage : stages) {
        const size_t activeSourceCount =
            std::min(TextureEnvRgbActiveSourceCount(stage.CombineRgb), stage.SourceRgb.size());
        for (size_t sourceIndex = 0; sourceIndex < activeSourceCount; ++sourceIndex) {
            if (stage.SourceRgb[sourceIndex] != kPicaTextureEnvSourceConstant) {
                continue;
            }
            if (!stage.ConstantColorIndexRecognized ||
                stage.ConstantColorIndex >= constants.size()) {
                return {};
            }
            if (referencedIndex < 0) {
                referencedIndex = static_cast<int32_t>(stage.ConstantColorIndex);
                continue;
            }
            if (!ColorRgbaEquals(constants[static_cast<size_t>(referencedIndex)],
                                 constants[stage.ConstantColorIndex])) {
                return {};
            }
        }
    }
    if (referencedIndex >= 0) {
        return { true, referencedIndex, "cmb_texture_env_stage_constant_selector",
                 constants[static_cast<size_t>(referencedIndex)] };
    }

    bool allEqual = true;
    for (size_t index = 1; index < constants.size(); ++index) {
        if (!ColorRgbaEquals(constants[index], constants[0])) {
            allEqual = false;
            break;
        }
    }
    if (allEqual) {
        return { true, 0, "cmb_material_constants_all_equal", constants[0] };
    }

    int32_t nonzeroRgbIndex = -1;
    for (size_t index = 0; index < constants.size(); ++index) {
        if (ColorRgbIsZero(constants[index])) {
            continue;
        }
        if (nonzeroRgbIndex >= 0) {
            return {};
        }
        nonzeroRgbIndex = static_cast<int32_t>(index);
    }
    if (nonzeroRgbIndex >= 0) {
        return { true, nonzeroRgbIndex, "cmb_material_unique_nonzero_rgb_constant",
                 constants[static_cast<size_t>(nonzeroRgbIndex)] };
    }

    return {};
}

bool StageIsPrimaryTexture0Modulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrimaryColor);
}

bool StageIsConstantTexture0Modulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourceConstant &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceConstant);
}

bool StageIsReplacePrimaryColor(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineReplace &&
           stage.RgbActiveSourceCount == 1 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.ColorScaleMultiplier == 1.0f;
}

bool StageIsReplaceTexture0(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineReplace &&
           stage.RgbActiveSourceCount == 1 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.ColorScaleMultiplier == 1.0f;
}

bool StageIsPrimaryTexture0ConstantAdd(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineMultiplyThenAdd &&
           stage.RgbActiveSourceCount == 3 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
           stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0 &&
           stage.SourceRgb[2] == kPicaTextureEnvSourceConstant &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[1] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[2] == kPicaTextureEnvOperandSourceColor;
}

bool StageIsPreviousConstantModulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceConstant) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceConstant &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrevious);
}

bool StageIsPreviousConstantAlphaAddConstantRgb(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineMultiplyThenAdd &&
           stage.RgbActiveSourceCount == 3 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
           stage.SourceRgb[1] == kPicaTextureEnvSourceConstant &&
           stage.SourceRgb[2] == kPicaTextureEnvSourceConstant &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[1] == kPicaTextureEnvOperandSourceAlpha &&
           stage.OperandRgb[2] == kPicaTextureEnvOperandSourceColor;
}

bool StageIsPreviousPrimaryTexture1Add(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineMultiplyThenAdd ||
        stage.RgbActiveSourceCount != 3 ||
        stage.SourceRgb[2] != kPicaTextureEnvSourcePrevious ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[2] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture1 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrimaryColor);
}

bool StageIsPreviousTexture1AddByTexture0Alpha(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineMultiplyThenAdd &&
           stage.RgbActiveSourceCount == 3 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
           stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1 &&
           stage.SourceRgb[2] == kPicaTextureEnvSourcePrevious &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceAlpha &&
           stage.OperandRgb[1] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[2] == kPicaTextureEnvOperandSourceColor;
}

bool StageIsTexture0Texture1Modulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture1 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0);
}

bool StageIsPreviousTexture1Modulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture1 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrevious);
}

bool StageIsPreviousPrimaryModulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrimaryColor) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrevious);
}

bool StageIsReplacePreviousNoOp(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineReplace &&
           stage.RgbActiveSourceCount == 1 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.ColorScaleMultiplier == 1.0f;
}

bool CmbResourceVisible(const std::vector<uint8_t>* resourceVisibility, uint8_t visibilityId) {
    if (resourceVisibility == nullptr) {
        return true;
    }
    return visibilityId < resourceVisibility->size() && (*resourceVisibility)[visibilityId] != 0;
}

bool StageIsTexture0Texture1AddMultiplyTexture0(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineRgb == kPicaTextureEnvCombineAddThenMultiply &&
           stage.RgbActiveSourceCount == 3 &&
           stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
           stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1 &&
           stage.SourceRgb[2] == kPicaTextureEnvSourceTexture0 &&
           stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[1] == kPicaTextureEnvOperandSourceColor &&
           stage.OperandRgb[2] == kPicaTextureEnvOperandSourceColor &&
           stage.CombineAlpha == kPicaTextureEnvCombineModulate &&
           stage.SourceAlpha[0] == kPicaTextureEnvSourcePrimaryColor &&
           stage.SourceAlpha[1] == kPicaTextureEnvSourceTexture0 &&
           stage.OperandAlpha[0] == kPicaTextureEnvOperandSourceAlpha &&
           stage.OperandAlpha[1] == kPicaTextureEnvOperandSourceAlpha &&
           stage.ColorScaleMultiplier == 1.0f &&
           stage.AlphaScaleMultiplier == 1.0f;
}

bool StageIsPreviousConstantModulatePreservingAlpha(const Oot3dNativeRenderTextureEnvState& stage) {
    return StageIsPreviousConstantModulate(stage) &&
           stage.CombineAlpha == kPicaTextureEnvCombineReplace &&
           stage.SourceAlpha[0] == kPicaTextureEnvSourcePrevious &&
           stage.OperandAlpha[0] == kPicaTextureEnvOperandSourceAlpha &&
           stage.AlphaScaleMultiplier == 1.0f;
}

bool StageIsReplacePreviousRgbAndMultiplyPreviousConstantAlpha(
    const Oot3dNativeRenderTextureEnvState& stage) {
    return StageIsReplacePreviousNoOp(stage) &&
           stage.CombineAlpha == kPicaTextureEnvCombineModulate &&
           stage.SourceAlpha[0] == kPicaTextureEnvSourcePrevious &&
           stage.SourceAlpha[1] == kPicaTextureEnvSourceConstant &&
           stage.OperandAlpha[0] == kPicaTextureEnvOperandSourceAlpha &&
           stage.OperandAlpha[1] == kPicaTextureEnvOperandSourceAlpha;
}

bool StageMultipliesPreviousAlphaByConstantAlpha(
    const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineAlpha != kPicaTextureEnvCombineModulate ||
        stage.OperandAlpha[0] != kPicaTextureEnvOperandSourceAlpha ||
        stage.OperandAlpha[1] != kPicaTextureEnvOperandSourceAlpha) {
        return false;
    }
    return (stage.SourceAlpha[0] == kPicaTextureEnvSourcePrevious &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourceConstant) ||
           (stage.SourceAlpha[0] == kPicaTextureEnvSourceConstant &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourcePrevious);
}

bool StageIsTexture0PrimaryColorAlphaModulate(
    const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineAlpha != kPicaTextureEnvCombineModulate ||
        stage.OperandAlpha[0] != kPicaTextureEnvOperandSourceAlpha ||
        stage.OperandAlpha[1] != kPicaTextureEnvOperandSourceAlpha ||
        stage.AlphaScaleMultiplier != 1.0f) {
        return false;
    }
    return (stage.SourceAlpha[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourcePrimaryColor) ||
           (stage.SourceAlpha[0] == kPicaTextureEnvSourcePrimaryColor &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourceTexture0);
}

bool StageIsTexture0ConstantColorAlphaModulate(
    const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineAlpha != kPicaTextureEnvCombineModulate ||
        stage.OperandAlpha[0] != kPicaTextureEnvOperandSourceAlpha ||
        stage.OperandAlpha[1] != kPicaTextureEnvOperandSourceAlpha ||
        stage.AlphaScaleMultiplier != 1.0f ||
        !stage.ConstantColorIndexRecognized) {
        return false;
    }
    return (stage.SourceAlpha[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourceConstant) ||
           (stage.SourceAlpha[0] == kPicaTextureEnvSourceConstant &&
            stage.SourceAlpha[1] == kPicaTextureEnvSourceTexture0);
}

bool StagePreservesPreviousAlpha(const Oot3dNativeRenderTextureEnvState& stage) {
    return stage.CombineAlpha == kPicaTextureEnvCombineReplace &&
           stage.SourceAlpha[0] == kPicaTextureEnvSourcePrevious &&
           stage.OperandAlpha[0] == kPicaTextureEnvOperandSourceAlpha &&
           stage.AlphaScaleMultiplier == 1.0f;
}

bool StageIsTexture1Texture2MultiplyAddPrevious(
    const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineMultiplyThenAdd ||
        stage.RgbActiveSourceCount != 3 ||
        stage.SourceRgb[2] != kPicaTextureEnvSourcePrevious ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[2] != kPicaTextureEnvOperandSourceColor ||
        stage.ColorScaleMultiplier != 1.0f ||
        !StagePreservesPreviousAlpha(stage)) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture1 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture2) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture2 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1);
}

bool StageIsPreviousConstantAlphaModulate(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineModulate ||
        stage.RgbActiveSourceCount != 2) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourcePrevious &&
            stage.OperandRgb[0] == kPicaTextureEnvOperandSourceColor &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceConstant &&
            stage.OperandRgb[1] == kPicaTextureEnvOperandSourceAlpha) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceConstant &&
            stage.OperandRgb[0] == kPicaTextureEnvOperandSourceAlpha &&
            stage.SourceRgb[1] == kPicaTextureEnvSourcePrevious &&
            stage.OperandRgb[1] == kPicaTextureEnvOperandSourceColor);
}

bool StageIsTexture0Texture1Add(const Oot3dNativeRenderTextureEnvState& stage) {
    if (stage.CombineRgb != kPicaTextureEnvCombineAdd ||
        stage.RgbActiveSourceCount != 2 ||
        stage.OperandRgb[0] != kPicaTextureEnvOperandSourceColor ||
        stage.OperandRgb[1] != kPicaTextureEnvOperandSourceColor ||
        stage.ColorScaleMultiplier != 1.0f) {
        return false;
    }
    return (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture0 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture1) ||
           (stage.SourceRgb[0] == kPicaTextureEnvSourceTexture1 &&
            stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0);
}

struct ResolvedTextureEnvAlphaProgram {
    bool Resolved = false;
    bool UsesPrimaryColor = false;
    bool UsesConstantColorBase = false;
    float Multiplier = 1.0f;
    uint32_t MultiplierStageCount = 0;
    std::array<bool, kOot3dCmbMaterialConstantColorCount> ConstantSlots{};
};

ResolvedTextureEnvAlphaProgram ResolveTextureEnvAlphaProgram(
    const std::vector<Oot3dNativeRenderTextureEnvState>& stages,
    const std::array<ColorRgba8, kOot3dCmbMaterialConstantColorCount>& constants) {
    ResolvedTextureEnvAlphaProgram resolved;
    if (stages.empty()) {
        return resolved;
    }
    if (StageIsTexture0PrimaryColorAlphaModulate(stages.front())) {
        resolved.Resolved = true;
        resolved.UsesPrimaryColor = true;
    } else if (StageIsTexture0ConstantColorAlphaModulate(stages.front()) &&
               stages.front().ConstantColorIndex < constants.size()) {
        resolved.Resolved = true;
        resolved.UsesConstantColorBase = true;
        resolved.Multiplier =
            static_cast<float>(constants[stages.front().ConstantColorIndex].A) / 255.0f;
        resolved.MultiplierStageCount = 1;
        resolved.ConstantSlots[stages.front().ConstantColorIndex] = true;
    } else {
        return resolved;
    }
    for (size_t stageIndex = 1; stageIndex < stages.size(); ++stageIndex) {
        const auto& stage = stages[stageIndex];
        if (StagePreservesPreviousAlpha(stage)) {
            continue;
        }
        if (!StageMultipliesPreviousAlphaByConstantAlpha(stage) ||
            !stage.ConstantColorIndexRecognized ||
            stage.ConstantColorIndex >= constants.size() ||
            !stage.AlphaScaleSupported) {
            return {};
        }

        resolved.Multiplier *=
            static_cast<float>(constants[stage.ConstantColorIndex].A) / 255.0f *
            stage.AlphaScaleMultiplier;
        ++resolved.MultiplierStageCount;
        resolved.ConstantSlots[stage.ConstantColorIndex] = true;
    }
    return resolved;
}

Oot3dNativeRenderTextureEnvProgram BuildTextureEnvProgram(
    const std::vector<Oot3dNativeRenderTextureEnvState>& stages,
    const std::array<ColorRgba8, kOot3dCmbMaterialConstantColorCount>& constants) {
    Oot3dNativeRenderTextureEnvProgram program;
    program.StageCount = static_cast<uint32_t>(stages.size());
    program.Decoded = !stages.empty();
    program.RgbCombinesKnown = !stages.empty();
    program.RgbSourcesKnown = !stages.empty();
    program.RgbOperandsKnown = !stages.empty();
    program.ColorScalesKnown = !stages.empty();

    for (const auto& stage : stages) {
        program.Decoded = program.Decoded && stage.Decoded;
        program.RgbCombinesKnown = program.RgbCombinesKnown && TextureEnvRgbCombineKnown(stage.CombineRgb);
        program.ColorScalesKnown = program.ColorScalesKnown && stage.ColorScaleSupported;
        if (stage.ColorScaleMultiplier != 1.0f) {
            program.UsesColorScale = true;
        }
        const size_t activeSourceCount =
            std::min(TextureEnvRgbActiveSourceCount(stage.CombineRgb), stage.SourceRgb.size());
        for (size_t sourceIndex = 0; sourceIndex < activeSourceCount; ++sourceIndex) {
            const uint16_t source = stage.SourceRgb[sourceIndex];
            const uint16_t operand = stage.OperandRgb[sourceIndex];
            program.RgbSourcesKnown = program.RgbSourcesKnown && TextureEnvRgbSourceKnown(source);
            program.RgbOperandsKnown = program.RgbOperandsKnown && TextureEnvRgbOperandKnown(operand);
            switch (source) {
                case 0x8577:
                    program.UsesPrimaryColor = true;
                    break;
                case 0x6210:
                case 0x6211:
                    program.UsesFragmentLightingColor = true;
                    break;
                case 0x84C0:
                    program.UsesTexture0 = true;
                    break;
                case 0x84C1:
                    program.UsesTexture1 = true;
                    break;
                case 0x84C2:
                    program.UsesTexture2 = true;
                    break;
                case 0x84C3:
                    program.UsesTexture3 = true;
                    break;
                case 0x8579:
                    program.UsesPreviousBuffer = true;
                    break;
                case 0x8576:
                    program.UsesConstantColor = true;
                    break;
                case 0x8578:
                    program.UsesPrevious = true;
                    break;
                default:
                    break;
            }
        }
    }

    program.RequiresMultiStageEvaluation = program.StageCount > 1 || program.UsesPrevious;
    program.RequiresMultiTextureSampling = program.UsesTexture1 || program.UsesTexture2 || program.UsesTexture3;
    program.RequiresConstantColorSelection = program.UsesConstantColor;
    program.RequiresPreviousBuffer = program.UsesPreviousBuffer;
    program.RgbRouteDecoded =
        program.Decoded && program.RgbCombinesKnown && program.RgbSourcesKnown && program.RgbOperandsKnown;
    const auto alphaProgram = ResolveTextureEnvAlphaProgram(stages, constants);
    if (alphaProgram.Resolved) {
        if (alphaProgram.UsesPrimaryColor) {
            program.Texture0PrimaryColorAlphaModulateResolved = true;
            program.Texture0PrimaryColorAlphaModulateStageCount = 1;
            program.Texture0PrimaryColorAlphaModulateSource =
                alphaProgram.MultiplierStageCount == 0
                    ? "oot3d_pica_texenv_alpha_modulate_texture0_primary_color"
                    : "oot3d_pica_texenv_alpha_modulate_texture0_primary_color_then_previous_constant";
        } else if (alphaProgram.UsesConstantColorBase) {
            program.Texture0ConstantColorAlphaModulateResolved = true;
            program.Texture0ConstantColorAlphaModulateStageCount = 1;
            program.Texture0ConstantColorAlphaModulateSource =
                "oot3d_pica_texenv_alpha_modulate_texture0_constant_color";
        }
        if (alphaProgram.MultiplierStageCount > 0) {
            program.AlphaMultiplierResolved = true;
            program.AlphaMultiplierStageCount = alphaProgram.MultiplierStageCount;
            program.AlphaMultiplier = alphaProgram.Multiplier;
            program.AlphaMultiplierSource =
                alphaProgram.UsesConstantColorBase
                    ? "oot3d_pica_texenv_texture0_constant_color_alpha"
                    : "oot3d_pica_texenv_previous_constant_alpha_chain";
        }
    }
    if (!program.RgbRouteDecoded || !program.ColorScalesKnown || stages.empty()) {
        return program;
    }

    if (stages.size() == 3 &&
        StageIsTexture0Texture1Add(stages[0]) &&
        StageIsPreviousPrimaryModulate(stages[1]) &&
        stages[1].ColorScaleMultiplier == 1.0f &&
        StageIsReplacePreviousNoOp(stages[2]) &&
        alphaProgram.Resolved) {
        program.ColorShaderPath =
            "texenv_program_texture0_texture1_add_then_primary_color_modulate";
        program.ColorShaderPathSupported = true;
        program.Texture0Texture1AddThenPrimaryColorModulateResolved = true;
        program.Texture0Texture1AddThenPrimaryColorModulateStageCount = 2;
        program.Texture0Texture1AddThenPrimaryColorModulateMapperSlot = 1;
        program.Texture0Texture1AddThenPrimaryColorModulateSource =
            "oot3d_pica_texenv_add_texture0_texture1_then_modulate_previous_primary_color";
        return program;
    }

    if (stages.size() == 2 &&
        StageIsPrimaryTexture0Modulate(stages[0]) &&
        StageIsTexture1Texture2MultiplyAddPrevious(stages[1]) &&
        alphaProgram.Resolved) {
        program.ColorShaderPath =
            "texenv_program_primary_texture0_then_texture1_texture2_multiply_add_previous";
        program.ColorShaderPathSupported = true;
        program.Texture1Texture2MultiplyAddPreviousResolved = true;
        program.Texture1Texture2MultiplyAddPreviousStageCount = 1;
        program.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot = 1;
        program.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot = 2;
        program.Texture1Texture2MultiplyAddPreviousSource =
            "oot3d_pica_texenv_mult_add_texture2_texture1_previous";
        if (stages[0].ColorScaleMultiplier != 1.0f) {
            program.PrimaryColorMultiplierResolved = true;
            program.PrimaryColorMultiplierStageCount = 1;
            program.PrimaryColorMultiplier = {
                stages[0].ColorScaleMultiplier,
                stages[0].ColorScaleMultiplier,
                stages[0].ColorScaleMultiplier,
            };
        }
        return program;
    }

    if (stages.size() == 3 &&
        StageIsTexture0Texture1AddMultiplyTexture0(stages[0]) &&
        StageIsPreviousConstantModulatePreservingAlpha(stages[1]) &&
        StageIsReplacePreviousRgbAndMultiplyPreviousConstantAlpha(stages[2]) &&
        stages[1].ConstantColorIndexRecognized &&
        stages[2].ConstantColorIndexRecognized) {
        const auto& rgbConstant = constants[stages[1].ConstantColorIndex];
        const auto& alphaConstant = constants[stages[2].ConstantColorIndex];
        program.ColorShaderPath =
            "texenv_program_texture0_texture1_add_multiply_texture0_then_constant_modulate";
        program.ColorShaderPathSupported = true;
        program.ConstantColorResolved = true;
        program.ConstantColorIndex = static_cast<int32_t>(stages[1].ConstantColorIndex);
        program.ConstantColorSource =
            "oot3d_cmb_texture_env_stage_constant_color_selector_unknown_ushort2_0";
        program.Texture0Texture1AddMultiplyTexture0Resolved = true;
        program.Texture0Texture1AddMultiplyTexture0StageCount = 1;
        program.Texture0Texture1AddMultiplyTexture0MapperSlot = 1;
        program.Texture0Texture1AddMultiplyTexture0Source =
            "oot3d_pica_texenv_add_multiply_texture0_texture1_texture0";
        program.TextureColorMultiplierResolved = true;
        program.TextureColorMultiplierStageCount = 1;
        program.TextureColorMultiplier = {
            static_cast<float>(rgbConstant.R) / 255.0f * stages[1].ColorScaleMultiplier,
            static_cast<float>(rgbConstant.G) / 255.0f * stages[1].ColorScaleMultiplier,
            static_cast<float>(rgbConstant.B) / 255.0f * stages[1].ColorScaleMultiplier,
        };
        program.AlphaMultiplierResolved = true;
        program.AlphaMultiplierStageCount = 1;
        program.AlphaMultiplier =
            static_cast<float>(alphaConstant.A) / 255.0f * stages[2].AlphaScaleMultiplier;
        program.AlphaMultiplierSource =
            "oot3d_cmb_texture_env_stage_constant_alpha_selector_unknown_ushort2_0";
        return program;
    }

    if (stages.size() == 1 && StageIsReplacePrimaryColor(stages.front())) {
        program.ColorShaderPath = "texenv_program_primary_color";
        program.ColorShaderPathSupported = true;
        return program;
    }

    if (stages.size() == 1 && StageIsReplaceTexture0(stages.front()) && alphaProgram.Resolved) {
        program.ColorShaderPath = "texenv_program_texture0";
        program.ColorShaderPathSupported = true;
        return program;
    }

    bool factorizedTexture0 = false;
    ColorRgba8 vertexColorMultiplier = { 255, 255, 255, 255 };
    Vec3f primaryColorMultiplier = { 1.0f, 1.0f, 1.0f };
    Vec3f textureColorMultiplier = { 1.0f, 1.0f, 1.0f };
    ColorRgba8 textureColorAddend = { 0, 0, 0, 255 };
    bool previousContainsPrimaryColor = false;
    uint32_t constantStageCount = 0;
    uint32_t primaryColorMultiplierStageCount = 0;
    uint32_t textureColorMultiplierStageCount = 0;
    uint32_t textureColorAddStageCount = 0;
    bool textureColorAddendResolved = false;
    uint32_t texture1ColorAddStageCount = 0;
    bool texture1ColorAddResolved = false;
    uint32_t texture1ColorMultiplyStageCount = 0;
    bool texture1ColorMultiplyResolved = false;
    bool texture1ColorMultiplyFromPreviousTexture1 = false;
    const auto resolvedConstant = ResolveTextureEnvConstantColor(stages, constants);
    if (program.UsesConstantColor) {
        program.ConstantColorResolved = resolvedConstant.Resolved;
        program.ConstantColorIndex = resolvedConstant.Index;
        program.ConstantColorSource = resolvedConstant.Source;
    }
    const ColorRgba8 constantColor = resolvedConstant.Color;
    for (size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex) {
        const auto& stage = stages[stageIndex];
        if (stageIndex >= constants.size()) {
            factorizedTexture0 = false;
            break;
        }
        if (program.UsesConstantColor && !resolvedConstant.Resolved) {
            factorizedTexture0 = false;
            break;
        }
        if (stageIndex == 0) {
            if (StageIsPrimaryTexture0Modulate(stage)) {
                factorizedTexture0 = true;
                previousContainsPrimaryColor = true;
                if (stage.ColorScaleMultiplier != 1.0f) {
                    primaryColorMultiplier = MultiplyVec3Scalar(primaryColorMultiplier,
                                                                 stage.ColorScaleMultiplier);
                    ++primaryColorMultiplierStageCount;
                }
                continue;
            }
            if (StageIsTexture0Texture1Modulate(stage)) {
                factorizedTexture0 = true;
                texture1ColorMultiplyResolved = true;
                ++texture1ColorMultiplyStageCount;
                if (stage.ColorScaleMultiplier != 1.0f) {
                    textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                    ++textureColorMultiplierStageCount;
                }
                continue;
            }
            if (StageIsConstantTexture0Modulate(stage)) {
                factorizedTexture0 = true;
                bool textureMultiplierFromStage =
                    !ColorRgbaEquals(constantColor, { 255, 255, 255, 255 });
                textureColorMultiplier = MultiplyVec3ColorRgb(textureColorMultiplier, constantColor);
                if (stage.ColorScaleMultiplier != 1.0f) {
                    textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                    textureMultiplierFromStage = true;
                }
                if (textureMultiplierFromStage) {
                    ++textureColorMultiplierStageCount;
                }
                ++constantStageCount;
                continue;
            }
            if (StageIsPrimaryTexture0ConstantAdd(stage)) {
                factorizedTexture0 = true;
                previousContainsPrimaryColor = true;
                if (stage.ColorScaleMultiplier != 1.0f) {
                    textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                    textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                    ++textureColorMultiplierStageCount;
                }
                if (!ColorRgbIsZero(constantColor)) {
                    program.RequiresTextureColorAdd = true;
                    textureColorAddend = AddColorRgbClamped(textureColorAddend, constantColor);
                    textureColorAddendResolved = true;
                    ++textureColorAddStageCount;
                }
                ++constantStageCount;
                continue;
            }
            if (stage.CombineRgb == kPicaTextureEnvCombineMultiplyThenAdd &&
                stage.RgbActiveSourceCount == 3 &&
                stage.SourceRgb[0] == kPicaTextureEnvSourcePrimaryColor &&
                stage.SourceRgb[1] == kPicaTextureEnvSourceTexture0 &&
                stage.SourceRgb[2] == kPicaTextureEnvSourceConstant) {
                program.RequiresTextureColorAdd = true;
            }
            factorizedTexture0 = false;
            break;
        }

        if (StageIsReplacePreviousNoOp(stage)) {
            continue;
        }

        if (StageIsPreviousConstantModulate(stage)) {
            bool textureMultiplierFromStage = !ColorRgbaEquals(constantColor, { 255, 255, 255, 255 });
            textureColorMultiplier = MultiplyVec3ColorRgb(textureColorMultiplier, constantColor);
            if (textureColorAddendResolved) {
                textureColorAddend = MultiplyColorRgb(textureColorAddend, constantColor);
            }
            if (stage.ColorScaleMultiplier != 1.0f) {
                textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                textureMultiplierFromStage = true;
            }
            if (textureMultiplierFromStage) {
                ++textureColorMultiplierStageCount;
            }
            ++constantStageCount;
            continue;
        }

        if (StageIsPreviousConstantAlphaModulate(stage)) {
            bool textureMultiplierFromStage = constantColor.A != 255;
            textureColorMultiplier = MultiplyVec3ColorAlpha(textureColorMultiplier, constantColor.A);
            if (textureColorAddendResolved) {
                textureColorAddend = MultiplyColorRgbByAlpha(textureColorAddend, constantColor.A);
            }
            if (stage.ColorScaleMultiplier != 1.0f) {
                textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                textureMultiplierFromStage = true;
            }
            if (textureMultiplierFromStage) {
                ++textureColorMultiplierStageCount;
            }
            ++constantStageCount;
            continue;
        }

        if (StageIsPreviousConstantAlphaAddConstantRgb(stage)) {
            bool textureMultiplierFromStage = constantColor.A != 255;
            textureColorMultiplier = MultiplyVec3ColorAlpha(textureColorMultiplier, constantColor.A);
            if (textureColorAddendResolved) {
                textureColorAddend = MultiplyColorRgbByAlpha(textureColorAddend, constantColor.A);
            }
            if (!ColorRgbIsZero(constantColor)) {
                program.RequiresTextureColorAdd = true;
                textureColorAddend = AddColorRgbClamped(textureColorAddend, constantColor);
                textureColorAddendResolved = true;
                ++textureColorAddStageCount;
            }
            if (stage.ColorScaleMultiplier != 1.0f) {
                textureColorMultiplier.X *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Y *= stage.ColorScaleMultiplier;
                textureColorMultiplier.Z *= stage.ColorScaleMultiplier;
                textureMultiplierFromStage = true;
            }
            if (textureMultiplierFromStage) {
                ++textureColorMultiplierStageCount;
            }
            ++constantStageCount;
            continue;
        }

        if (!previousContainsPrimaryColor && !textureColorAddendResolved &&
            StageIsPreviousPrimaryModulate(stage)) {
            previousContainsPrimaryColor = true;
            ++primaryColorMultiplierStageCount;
            if (stage.ColorScaleMultiplier != 1.0f) {
                primaryColorMultiplier = MultiplyVec3Scalar(primaryColorMultiplier,
                                                             stage.ColorScaleMultiplier);
            }
            continue;
        }

        if (!program.UsesConstantColor && StageIsPreviousPrimaryTexture1Add(stage)) {
            program.RequiresTextureColorAdd = true;
            texture1ColorAddResolved = true;
            ++texture1ColorAddStageCount;
            continue;
        }

        if (!program.UsesConstantColor && StageIsPreviousTexture1AddByTexture0Alpha(stage)) {
            program.RequiresTextureColorAdd = true;
            texture1ColorAddResolved = true;
            program.Texture1ColorAddUsesTexture0Alpha = true;
            ++texture1ColorAddStageCount;
            continue;
        }

        if (!program.UsesConstantColor && StageIsPreviousTexture1Modulate(stage)) {
            texture1ColorMultiplyResolved = true;
            texture1ColorMultiplyFromPreviousTexture1 = true;
            ++texture1ColorMultiplyStageCount;
            if (stage.ColorScaleMultiplier != 1.0f) {
                primaryColorMultiplier =
                    MultiplyVec3Scalar(primaryColorMultiplier, stage.ColorScaleMultiplier);
                ++primaryColorMultiplierStageCount;
            }
            continue;
        }

        if (!program.UsesConstantColor && texture1ColorMultiplyResolved &&
            StageIsPreviousPrimaryModulate(stage)) {
            ++primaryColorMultiplierStageCount;
            if (stage.ColorScaleMultiplier != 1.0f) {
                primaryColorMultiplier = MultiplyVec3Scalar(primaryColorMultiplier,
                                                             stage.ColorScaleMultiplier);
            }
            continue;
        }

        factorizedTexture0 = false;
        break;
    }

    if (factorizedTexture0) {
        if (textureColorAddendResolved) {
            program.ColorShaderPath = "texenv_program_texture0_vertex_color_constant_add";
            program.TextureColorAddendResolved = true;
            program.TextureColorAddStageCount = textureColorAddStageCount;
            program.TextureColorAddend = textureColorAddend;
        } else {
            program.ColorShaderPath =
                texture1ColorMultiplyResolved
                    ? "texenv_program_texture0_texture1_vertex_color_multiply"
                    : texture1ColorAddResolved
                    ? "texenv_program_texture0_texture1_vertex_color_add"
                    : (constantStageCount > 0 ? "texenv_program_texture0_vertex_color_constant_factor"
                                              : "texenv_program_texture0_vertex_color");
        }
        program.ColorShaderPathSupported = true;
        program.VertexColorConstantStageCount = constantStageCount;
        program.VertexColorMultiplier = vertexColorMultiplier;
        if (texture1ColorAddResolved) {
            program.Texture1ColorAddResolved = true;
            program.Texture1ColorAddStageCount = texture1ColorAddStageCount;
            program.Texture1ColorAddMapperSlot = 1;
            program.Texture1ColorAddSource =
                program.Texture1ColorAddUsesTexture0Alpha
                    ? "oot3d_cmb_texture_env_stage_mult_add_previous_plus_texture0_alpha_texture1"
                    : "oot3d_cmb_texture_env_stage_mult_add_previous_plus_primary_texture1";
        }
        if (texture1ColorMultiplyResolved) {
            program.Texture1ColorMultiplyResolved = true;
            program.Texture1ColorMultiplyStageCount = texture1ColorMultiplyStageCount;
            program.Texture1ColorMultiplyMapperSlot = 1;
            program.Texture1ColorMultiplySource =
                texture1ColorMultiplyFromPreviousTexture1
                    ? "oot3d_cmb_texture_env_stage_modulate_previous_texture1"
                    : "oot3d_cmb_texture_env_stage_modulate_texture0_texture1_then_previous_primary";
        }
        if (primaryColorMultiplierStageCount > 0) {
            program.PrimaryColorMultiplierResolved = true;
            program.PrimaryColorMultiplierStageCount = primaryColorMultiplierStageCount;
            program.PrimaryColorMultiplier = primaryColorMultiplier;
        }
        if (textureColorMultiplierStageCount > 0 || !Vec3IsIdentity(textureColorMultiplier)) {
            program.TextureColorMultiplierResolved = true;
            program.TextureColorMultiplierStageCount = textureColorMultiplierStageCount;
            program.TextureColorMultiplier = textureColorMultiplier;
        }
    }
    return program;
}

void RefreshTextureEnvCombinerRequirement(Oot3dNativeRenderMaterialState& material,
                                          size_t validTextureEnvStageCount) {
    const bool unsupportedProgramRequiresDecoder =
        material.RawTextureStageSelectorDecoded &&
        !material.TextureEnvProgram.ColorShaderPathSupported &&
        (material.RawTextureStageCount > 1 || validTextureEnvStageCount > 1 ||
         !material.TextureEnvStageIndicesCovered ||
         material.RawTextureStageCountExceedsMappers ||
         material.TextureEnvProgram.RequiresTextureColorAdd ||
         material.TextureEnvProgram.RequiresMultiTextureSampling ||
         material.TextureEnvProgram.RequiresPreviousBuffer ||
         (material.TextureEnvProgram.Decoded && material.TextureEnvProgram.UsesConstantColor));
    const bool textureEnvProgramNeedsBoundTexture =
        material.TextureEnvProgram.UsesTexture0 || material.TextureEnvProgram.UsesTexture1 ||
        material.TextureEnvProgram.UsesTexture2 || material.TextureEnvProgram.UsesTexture3;
    const bool missingRawStageTextureBinding =
        textureEnvProgramNeedsBoundTexture &&
        material.RawTextureStageSelectorDecoded && material.RawTextureStageCount > 0 &&
        !material.TextureBindingResolvedFromRawStageSelector;
    material.NativeMaterialCombinerRequiresDecoder =
        unsupportedProgramRequiresDecoder || missingRawStageTextureBinding;
}

uint32_t RawMaterialLeU32(const std::vector<uint8_t>& raw, size_t offset) {
    return static_cast<uint32_t>(raw[offset]) |
           (static_cast<uint32_t>(raw[offset + 1]) << 8) |
           (static_cast<uint32_t>(raw[offset + 2]) << 16) |
           (static_cast<uint32_t>(raw[offset + 3]) << 24);
}

std::vector<Oot3dNativeRawMaterialWord> RawMaterialNonzeroWords(const std::vector<uint8_t>& raw,
                                                                size_t start,
                                                                size_t end) {
    std::vector<Oot3dNativeRawMaterialWord> words;
    if (raw.empty()) {
        return words;
    }

    const size_t clampedStart = std::min(start, raw.size());
    const size_t clampedEnd = std::min(end, raw.size());
    const size_t alignedEnd = clampedEnd - ((clampedEnd - clampedStart) % sizeof(uint32_t));
    for (size_t offset = clampedStart; offset + sizeof(uint32_t) <= alignedEnd; offset += sizeof(uint32_t)) {
        const uint32_t value = RawMaterialLeU32(raw, offset);
        if (value != 0) {
            words.push_back({ static_cast<uint32_t>(offset), value });
        }
    }
    return words;
}

Vec2f NativeTextureCoordSource(uint8_t mappingMethod, Vec2f uv, Vec3f normal, bool normalAvailable) {
    if (mappingMethod == 3 && normalAvailable) {
        return { 0.5f + normal.X * 0.5f, 0.5f + normal.Y * 0.5f };
    }
    return uv;
}

Vec2f ApplyTextureCoordTransform(Vec2f source, Vec2f scale, float rotation, Vec2f translation) {
    const double cosR = std::cos(rotation);
    const double sinR = std::sin(rotation);
    const double s = scale.X * (cosR * source.X - sinR * source.Y + 0.5 * sinR - 0.5 * cosR + 0.5 -
                                translation.X);
    const double t = scale.Y * (sinR * source.X + cosR * source.Y - 0.5 * sinR - 0.5 * cosR + 0.5 -
                                translation.Y);
    return { static_cast<float>(s), static_cast<float>(t) };
}

Vec2f ApplyTextureCoord(const CmbMaterial* material, TextureBinding binding, Vec2f uv, Vec3f normal,
                        bool normalAvailable) {
    if (material == nullptr || !binding.Valid || binding.Slot >= 3) {
        return uv;
    }
    if (material->TextureCoordsUsed != 0 && binding.Slot >= material->TextureCoordsUsed) {
        return uv;
    }

    const auto& coord = material->TextureCoords[binding.Slot];
    if (coord.CoordinateIndex != 0) {
        return uv;
    }

    const Vec2f source = NativeTextureCoordSource(coord.MappingMethod, uv, normal, normalAvailable);
    return ApplyTextureCoordTransform(source, coord.Scale, coord.Rotation, coord.Translation);
}

Vec2f ApplyTextureCoordState(const Oot3dNativeRenderTextureCoordState& coord, Vec2f uv, Vec3f normal,
                             bool normalAvailable) {
    if (!coord.Active || coord.CoordinateIndex != 0) {
        return uv;
    }

    const Vec2f source = NativeTextureCoordSource(coord.MappingMethod, uv, normal, normalAvailable);
    return ApplyTextureCoordTransform(source, coord.Scale, coord.Rotation, coord.Translation);
}

void ExpandBounds(Oot3dDemoBounds& bounds, const Vec3f& value) {
    const Oot3dDemoVec3 point{ value.X, value.Y, value.Z };
    if (!bounds.Valid) {
        bounds.Min = point;
        bounds.Max = point;
        bounds.Valid = true;
        return;
    }
    bounds.Min.X = std::min(bounds.Min.X, point.X);
    bounds.Min.Y = std::min(bounds.Min.Y, point.Y);
    bounds.Min.Z = std::min(bounds.Min.Z, point.Z);
    bounds.Max.X = std::max(bounds.Max.X, point.X);
    bounds.Max.Y = std::max(bounds.Max.Y, point.Y);
    bounds.Max.Z = std::max(bounds.Max.Z, point.Z);
}

void ExpandBoundsByPoint(Oot3dDemoBounds& bounds, const Oot3dDemoVec3& point) {
    if (!bounds.Valid) {
        bounds.Min = point;
        bounds.Max = point;
        bounds.Valid = true;
        return;
    }
    bounds.Min.X = std::min(bounds.Min.X, point.X);
    bounds.Min.Y = std::min(bounds.Min.Y, point.Y);
    bounds.Min.Z = std::min(bounds.Min.Z, point.Z);
    bounds.Max.X = std::max(bounds.Max.X, point.X);
    bounds.Max.Y = std::max(bounds.Max.Y, point.Y);
    bounds.Max.Z = std::max(bounds.Max.Z, point.Z);
}

Oot3dNativeRenderLutSection BuildRenderLutSection(const CmbLutSection& luts) {
    Oot3dNativeRenderLutSection section;
    section.Decoded = luts.Decoded;
    section.SourceOffset = luts.SourceOffset;
    section.ChunkSize = luts.ChunkSize;
    section.Count = luts.Count;
    section.HeaderWord0C = luts.HeaderWord0C;

    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    const auto& lutContract = bridgeContract.CmbLutAssetDecode;
    section.NativeAssetDecodeContractAvailable =
        lutContract.DecodeAddress != 0 &&
        lutContract.SourceSampleCount != 0 &&
        lutContract.PackedValueCount != 0;
    section.FinalShaderSemanticResolved = lutContract.FinalShaderSemanticResolved;
    section.SourceKind = "oot3d_cmb_luts_chunk_codebin_native_decoder";

    section.Records.reserve(luts.Records.size());
    for (const auto& record : luts.Records) {
        Oot3dNativeRenderLutRecord renderRecord;
        renderRecord.Index = record.Index;
        renderRecord.SourceOffset = record.SourceOffset;
        renderRecord.Size = record.Size;
        renderRecord.Type = record.Type;
        renderRecord.HeaderByte01 = record.HeaderByte01;
        renderRecord.HeaderByte02 = record.HeaderByte02;
        renderRecord.HeaderByte03 = record.HeaderByte03;
        renderRecord.PointCount = record.PointCount;
        renderRecord.PointStrideBytes = record.PointStrideBytes;
        renderRecord.Samples = record.Samples;
        renderRecord.PackedBaseValues = record.PackedBaseValues;
        renderRecord.PackedDeltaValues = record.PackedDeltaValues;
        section.Records.push_back(std::move(renderRecord));
    }

    return section;
}

Oot3dNativeRenderMaterialState BuildMaterialState(const CmbModel& model, const CmbMaterial* material,
                                                  TextureBinding binding, int32_t materialIndex) {
    Oot3dNativeRenderMaterialState state;
    if (material != nullptr) {
        const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
        state.NativeMaterialAvailable = true;
        state.NativePicaFogOverrideDecoded = true;
        state.NativePicaFogEnabled = false;
        state.NativePicaFogOverrideSource =
            "oot3d_material_packet_enable_offset_0x0a_default_clear_until_002d960c_runtime_list_binding";
        state.NativeMaterialRawSize = static_cast<uint32_t>(material->RawMaterial.size());
        state.NativeMaterialRawFnv1a64 = Fnv1a64(material->RawMaterial);
        state.NativeRuntimeMaterialLaneDecoded = materialIndex >= 0;
        state.NativeRuntimeMaterialLaneIndex = materialIndex;
        state.NativeRuntimeMaterialLaneStrideBytes =
            bridgeContract.DrawHandleSubmit.MaterialLaneStrideBytes;
        if (bridgeContract.DrawHandleSubmit.MaterialDrawDispatchResolvedFromCodebin) {
            std::ostringstream source;
            source << "oot3d_codebin_" << std::hex << std::nouppercase << std::setfill('0')
                   << std::setw(8) << bridgeContract.DrawHandleSubmit.MaterialDrawDispatchAddress
                   << "_material_draw_dispatch_cmb_mesh_material_index";
            state.NativeRuntimeMaterialLaneSource = source.str();
        }
        state.FragmentLightingEnabled = material->FragmentLightingEnabled;
        state.VertexLightingEnabled = material->VertexLightingEnabled;
        state.HemisphereLightingEnabled = material->HemisphereLightingEnabled;
        state.HemisphereOcclusionEnabled = material->HemisphereOcclusionEnabled;
        state.RawTextureStageSelectorDecoded = material->RawTextureStageSelectorDecoded;
        state.RawTextureStageCount = material->RawTextureStageCount;
        state.RawTextureStageSlots = material->RawTextureStageSlots;
        state.RawTextureStageCountMatchesMappers =
            material->RawTextureStageSelectorDecoded && material->RawTextureStageCount == material->TextureMappersUsed;
        state.RawTextureStageCountExceedsMappers =
            material->RawTextureStageSelectorDecoded && material->RawTextureStageCount > material->TextureMappersUsed;
        state.PostMaterialTextureEnvTableDecoded = material->PostMaterialTextureEnvTableDecoded;
        state.PostMaterialTextureEnvTableDerivedFromLanePointer =
            material->PostMaterialTextureEnvTableDerivedFromLanePointer;
        state.PostMaterialTextureEnvTableSourceOffset = material->PostMaterialTextureEnvTableSourceOffset;
        state.PostMaterialTextureEnvTableRecordSize = material->PostMaterialTextureEnvTableRecordSize;
        state.PostMaterialTextureEnvTableRecordCount = material->PostMaterialTextureEnvTableRecordCount;
        state.PostMaterialTextureEnvStageResolved = material->PostMaterialTextureEnvStageResolved;
        state.PostMaterialTextureEnvStageSourceOffsets = material->PostMaterialTextureEnvStageSourceOffsets;
        if (state.PostMaterialTextureEnvTableDerivedFromLanePointer &&
            bridgeContract.DrawHandleSubmit.MaterialLaneHeaderPointersResolvedFromCodeBin) {
            std::ostringstream source;
            source << "oot3d_codebin_" << std::hex << std::nouppercase << std::setfill('0')
                   << std::setw(8) << bridgeContract.DrawHandleSubmit.MaterialLanePopulateAddress
                   << "_lane_0x08_mats_texture_env_table_base";
            state.PostMaterialTextureEnvTableSource = source.str();
        }
        state.MaterialLightingBlock = material->LightingBlock;
        state.MaterialPicaLutInput =
            BuildMaterialPicaLutInputState(state.MaterialLightingBlock, bridgeContract.DrawHandleSubmit);
        state.NativePicaMaterialLutInputPacketAvailable = state.MaterialPicaLutInput.Available;
        state.NativePicaMaterialLutInputPacketComplete = state.MaterialPicaLutInput.Complete;
        state.NativePicaBumpModeAvailable = state.MaterialLightingBlock.Decoded;
        state.NativePicaBumpModeRecognized = state.MaterialLightingBlock.PicaBumpModeRecognized;
        state.NativePicaBumpMode = state.MaterialLightingBlock.PicaBumpMode;
        state.NativePicaBumpModeActive =
            state.NativePicaBumpModeRecognized && state.NativePicaBumpMode != 0;
        state.NativePicaBumpTextureUnitRecognized =
            state.MaterialLightingBlock.PicaBumpTextureUnitRecognized;
        state.NativePicaBumpTextureUnit = state.MaterialLightingBlock.PicaBumpTextureUnit;
        state.NativeMaterialLightingFlag3Available = state.MaterialLightingBlock.Decoded;
        state.NativeMaterialLightingFlag3Active =
            state.MaterialLightingBlock.Decoded && state.MaterialLightingBlock.Flag3;
        state.NativeMaterialLightingFlag3BackendPending = false;
        if (state.NativeMaterialLightingFlag3Available) {
            state.NativeMaterialLightingFlag3Source =
                state.NativeMaterialLightingFlag3Active
                    ? "oot3d_cmb_material_lighting_block_flag3_copied_no_material_lighting_consumer"
                    : "oot3d_cmb_material_lighting_block_flag3_disabled";
        }
        state.MaterialFragmentLightingConfig =
            BuildMaterialFragmentLightingConfigState(
                *material, bridgeContract.LightingRegisterEmitter, bridgeContract.DrawHandleSubmit);
        state.NativePicaFragmentLightingConfigPacketAvailable =
            state.MaterialFragmentLightingConfig.Available;
        state.NativePicaFragmentLightingConfigPacketComplete =
            state.MaterialFragmentLightingConfig.Complete;
        state.NativePicaFragmentLightingConfigKnownPayloadWordCount =
            state.MaterialFragmentLightingConfig.KnownPayloadWordCount;
        state.NativePicaFragmentLightingConfigRuntimeOverridePending =
            state.MaterialFragmentLightingConfig.Available &&
            !state.MaterialFragmentLightingConfig.RuntimeOverrideGateResolved;
        state.NativePicaFragmentLightingConfigApplicationSource =
            state.MaterialFragmentLightingConfig.SourceKind;
        state.MaterialColorsDecoded = material->MaterialColorsDecoded;
        state.EmissionColor = material->EmissionColor;
        state.AmbientColor = material->AmbientColor;
        state.DiffuseColor = material->DiffuseColor;
        state.Specular0Color = material->Specular0Color;
        state.Specular1Color = material->Specular1Color;
        state.ConstantColors = material->ConstantColors;
        state.NativeMaterialTextureStageCandidateStart = kOot3dCmbMaterialTextureStageCandidateStart;
        state.NativeMaterialTextureStageCandidateEnd = kOot3dCmbMaterialTextureStageCandidateEnd;
        state.NativeMaterialTextureStageCandidateNonzeroWords =
            RawMaterialNonzeroWords(material->RawMaterial,
                                    kOot3dCmbMaterialTextureStageCandidateStart,
                                    kOot3dCmbMaterialTextureStageCandidateEnd);
        state.TextureEnvTableRecordCount = static_cast<uint32_t>(model.TextureEnvSettings.size());
        const size_t validRawStageCount = ValidRawTextureStageCount(*material, model);
        const size_t requestedRawStageCount =
            std::min<size_t>(material->RawTextureStageCount, material->RawTextureStageSlots.size());
        state.TextureEnvStageIndicesCovered =
            material->RawTextureStageSelectorDecoded &&
            material->RawTextureStageCount <= material->RawTextureStageSlots.size() &&
            validRawStageCount == requestedRawStageCount;
        state.TextureEnvStageRecordCount = static_cast<uint32_t>(material->TextureEnvStages.size());
        state.TextureEnvStages.reserve(material->TextureEnvStages.size());
        for (size_t stageOrder = 0; stageOrder < material->TextureEnvStages.size(); ++stageOrder) {
            state.TextureEnvStages.push_back(
                BuildTextureEnvState(material->TextureEnvStages[stageOrder], static_cast<uint32_t>(stageOrder)));
        }
        state.TextureEnvProgram = BuildTextureEnvProgram(state.TextureEnvStages, state.ConstantColors);
        const size_t mapperCount = ActiveTextureMapperCount(*material);
        for (size_t mapperSlot = 0; mapperSlot < state.TextureMapperTextureIndices.size(); ++mapperSlot) {
            if (mapperSlot >= mapperCount) {
                continue;
            }
            const auto& mapper = material->TextureMappers[mapperSlot];
            auto& sampler = state.TextureMapperSamplerStates[mapperSlot];
            sampler.Decoded = true;
            sampler.MinFilter = mapper.MinFilter;
            sampler.MagFilter = mapper.MagFilter;
            sampler.WrapS = mapper.WrapS;
            sampler.WrapT = mapper.WrapT;
            sampler.LodBias = mapper.LodBias;
            sampler.Source = "oot3d_cmb_material_texture_mapper";
            const int textureIndex = mapper.TextureIndex;
            if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < model.Textures.size()) {
                state.TextureMapperTextureIndices[mapperSlot] = textureIndex;
            }
        }
        if (state.NativePicaBumpModeActive && state.NativePicaBumpTextureUnitRecognized &&
            state.NativePicaBumpTextureUnit < state.TextureMapperTextureIndices.size()) {
            state.NativePicaBumpTextureIndex =
                state.TextureMapperTextureIndices[state.NativePicaBumpTextureUnit];
            if (state.NativePicaBumpTextureIndex >= 0 &&
                static_cast<size_t>(state.NativePicaBumpTextureIndex) < model.Textures.size()) {
                state.NativePicaBumpTextureSource =
                    "oot3d_cmb_material_lighting_block_pica_bump_texture_unit";
            }
        }
        if (state.NativePicaBumpModeAvailable) {
            state.NativePicaBumpNormalMapBackendSupported =
                state.NativePicaBumpModeActive &&
                state.NativePicaBumpMode == 1 &&
                state.NativePicaBumpTextureIndex >= 0;
            state.NativePicaBumpModeBackendPending =
                state.NativePicaBumpModeActive &&
                !state.NativePicaBumpNormalMapBackendSupported;
            if (!state.NativePicaBumpModeActive) {
                state.NativePicaBumpModeSource =
                    "oot3d_cmb_material_lighting_block_pica_bump_mode_none";
            } else if (state.NativePicaBumpNormalMapBackendSupported) {
                state.NativePicaBumpModeSource =
                    "oot3d_cmb_material_lighting_block_pica_bump_mode_normal_map_vertex_backend";
            } else if (state.NativePicaBumpMode == 1) {
                state.NativePicaBumpModeSource =
                    "oot3d_cmb_material_lighting_block_pica_bump_mode_normal_map_missing_texture_unit";
            } else {
                state.NativePicaBumpModeSource =
                    "oot3d_cmb_material_lighting_block_pica_bump_mode_tangent_map_requires_backend_consumer";
            }
        }
        if (state.TextureEnvProgram.Texture1ColorAddResolved &&
            state.TextureMapperTextureIndices[state.TextureEnvProgram.Texture1ColorAddMapperSlot] >= 0) {
            state.SecondaryTextureMapperSlot = state.TextureEnvProgram.Texture1ColorAddMapperSlot;
            state.SecondaryTextureIndex =
                state.TextureMapperTextureIndices[state.TextureEnvProgram.Texture1ColorAddMapperSlot];
            state.SecondaryTextureBindingSource = state.TextureEnvProgram.Texture1ColorAddSource;
        }
        if (state.TextureEnvProgram.Texture1ColorMultiplyResolved &&
            state.TextureMapperTextureIndices[state.TextureEnvProgram.Texture1ColorMultiplyMapperSlot] >= 0) {
            state.SecondaryTextureMapperSlot = state.TextureEnvProgram.Texture1ColorMultiplyMapperSlot;
            state.SecondaryTextureIndex =
                state.TextureMapperTextureIndices[state.TextureEnvProgram.Texture1ColorMultiplyMapperSlot];
            state.SecondaryTextureBindingSource = state.TextureEnvProgram.Texture1ColorMultiplySource;
        }
        if (state.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateResolved &&
            state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateMapperSlot] >= 0) {
            state.SecondaryTextureMapperSlot =
                state.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateMapperSlot;
            state.SecondaryTextureIndex = state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateMapperSlot];
            state.SecondaryTextureBindingSource =
                state.TextureEnvProgram.Texture0Texture1AddThenPrimaryColorModulateSource;
        }
        if (state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousResolved &&
            state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot] >= 0 &&
            state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot] >= 0) {
            state.SecondaryTextureMapperSlot =
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot;
            state.SecondaryTextureIndex = state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot];
            state.SecondaryTextureBindingSource =
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousSource;
            state.TertiaryTextureMapperSlot =
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot;
            state.TertiaryTextureIndex = state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot];
            state.TertiaryTextureBindingSource =
                state.TextureEnvProgram.Texture1Texture2MultiplyAddPreviousSource;
        }
        if (state.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0Resolved &&
            state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0MapperSlot] >= 0) {
            state.SecondaryTextureMapperSlot =
                state.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0MapperSlot;
            state.SecondaryTextureIndex = state.TextureMapperTextureIndices[
                state.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0MapperSlot];
            state.SecondaryTextureBindingSource =
                state.TextureEnvProgram.Texture0Texture1AddMultiplyTexture0Source;
        }
        if (!state.TextureEnvStages.empty()) {
            state.TextureEnv = state.TextureEnvStages.front();
            state.TextureEnvSelectedStageIndex = static_cast<int32_t>(state.TextureEnv.TableIndex);
        } else if (material->TextureEnv.Decoded) {
            state.TextureEnv = BuildTextureEnvState(material->TextureEnv, 0);
            state.TextureEnvSelectedStageIndex = static_cast<int32_t>(state.TextureEnv.TableIndex);
        }
        state.TextureCoords.reserve(3);
        for (uint32_t slot = 0; slot < 3; ++slot) {
            auto textureCoord = BuildTextureCoordState(*material, slot, binding);
            if (textureCoord.SelectedPrimary) {
                state.SelectedTextureCoord = textureCoord;
                state.SelectedTextureCoordDecoded = textureCoord.Decoded;
            }
            state.TextureCoords.push_back(textureCoord);
        }
        state.AlphaTest = material->AlphaTest;
        state.AlphaReference = material->AlphaReference;
        state.AlphaFunction = material->AlphaFunction;
        state.CmbCullFace = material->CullFace;
        state.PicaCullMode = material->PicaCullMode;
        state.DepthTest = material->DepthTest;
        state.DepthWrite = material->DepthWrite;
        state.DepthFunction = material->DepthFunction;
        state.BlendMode = material->BlendMode;
        state.BlendSrc = material->BlendSrc;
        state.BlendDst = material->BlendDst;
        state.BlendEquation = material->BlendEquation;
        state.ColorBlendSrc = material->ColorBlendSrc;
        state.ColorBlendDst = material->ColorBlendDst;
        state.ColorBlendEquation = material->ColorBlendEquation;
        state.BlendColorAlpha = material->BlendColorAlpha;
        state.NativeRenderStateDecoded = true;
        state.NativeBlendStateEnabled = material->BlendMode != 0;
        state.NativeBlendFactorsSupported =
            NativeBlendFactorKnown(material->BlendSrc) &&
            NativeBlendFactorKnown(material->BlendDst) &&
            NativeBlendFactorKnown(material->ColorBlendSrc) &&
            NativeBlendFactorKnown(material->ColorBlendDst);
        state.NativeBlendEquationSupported =
            NativeBlendEquationKnown(material->BlendEquation) &&
            NativeBlendEquationKnown(material->ColorBlendEquation);
        state.NativeBlendStateSupported =
            state.NativeBlendStateEnabled && state.NativeBlendFactorsSupported &&
            state.NativeBlendEquationSupported;
    }

    if (binding.Valid && binding.TextureIndex < model.Textures.size()) {
        const auto& texture = model.Textures[binding.TextureIndex];
        state.Textured = texture.Rgba8Decoded && !texture.Rgba8.empty();
        state.TextureIndex = static_cast<int32_t>(binding.TextureIndex);
        state.TextureMapperSlot = static_cast<uint32_t>(binding.Slot);
        state.TextureBindingSource = binding.Source;
        state.TextureBindingResolvedFromRawStageSelector = binding.ResolvedFromRawStageSelector;
        state.TextureHasNativeAlpha = TextureHasNativeAlpha(texture);
        if (material != nullptr && binding.Slot < std::size(material->TextureMappers)) {
            const auto& mapper = material->TextureMappers[binding.Slot];
            state.NativeSamplerStateDecoded = true;
            state.NativeSamplerMinFilter = mapper.MinFilter;
            state.NativeSamplerMagFilter = mapper.MagFilter;
            state.NativeSamplerWrapS = mapper.WrapS;
            state.NativeSamplerWrapT = mapper.WrapT;
            state.NativeSamplerLodBias = mapper.LodBias;
            state.NativeSamplerStateSource = "oot3d_cmb_material_texture_mapper";
        }
    }

    if (material != nullptr) {
        RefreshTextureEnvCombinerRequirement(state, ValidRawTextureStageCount(*material, model));
    }

    return state;
}

nlohmann::json BoundsJson(const Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return nullptr;
    }
    return {
        { "min", { { "x", bounds.Min.X }, { "y", bounds.Min.Y }, { "z", bounds.Min.Z } } },
        { "max", { { "x", bounds.Max.X }, { "y", bounds.Max.Y }, { "z", bounds.Max.Z } } },
        { "max_extent", NativeDemoBoundsMaxExtent(bounds) },
    };
}

nlohmann::json DemoVec3Json(const Oot3dDemoVec3& value) {
    return { { "x", value.X }, { "y", value.Y }, { "z", value.Z } };
}

nlohmann::json MatrixJson(const Matrix4f& matrix) {
    nlohmann::json rows = nlohmann::json::array();
    for (size_t row = 0; row < 4; ++row) {
        rows.push_back({ matrix.M[row][0], matrix.M[row][1], matrix.M[row][2], matrix.M[row][3] });
    }
    return rows;
}

const nlohmann::json* JsonObjectChild(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_object()) {
        return nullptr;
    }
    return &object.at(key);
}

std::string JsonStringValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return "";
    }
    return object.at(key).get<std::string>();
}

int JsonIntValue(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number_integer()) {
        return fallback;
    }
    return object.at(key).get<int>();
}

bool JsonBoolValue(const nlohmann::json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_boolean()) {
        return fallback;
    }
    return object.at(key).get<bool>();
}

std::optional<double> JsonNumberValue(const nlohmann::json& value) {
    if (!value.is_number()) {
        return std::nullopt;
    }
    return value.get<double>();
}

double JsonDoubleValue(const nlohmann::json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) {
        return fallback;
    }
    return object.at(key).get<double>();
}

std::optional<Vec3f> JsonVec3ArrayValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_array() ||
        object.at(key).size() < 3) {
        return std::nullopt;
    }
    const auto& array = object.at(key);
    const auto x = JsonNumberValue(array.at(0));
    const auto y = JsonNumberValue(array.at(1));
    const auto z = JsonNumberValue(array.at(2));
    if (!x.has_value() || !y.has_value() || !z.has_value()) {
        return std::nullopt;
    }
    return Vec3f{ static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z) };
}

std::optional<ColorRgba8> JsonColorU8ObjectValue(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_object()) {
        return std::nullopt;
    }
    const auto& color = object.at(key);
    if (!color.contains("r") || !color.contains("g") || !color.contains("b")) {
        return std::nullopt;
    }
    const auto r = JsonNumberValue(color.at("r"));
    const auto g = JsonNumberValue(color.at("g"));
    const auto b = JsonNumberValue(color.at("b"));
    const auto a = color.contains("a") ? JsonNumberValue(color.at("a")) : std::optional<double>{ 255.0 };
    if (!r.has_value() || !g.has_value() || !b.has_value() || !a.has_value()) {
        return std::nullopt;
    }
    return ColorRgba8{
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(*r)), 0, 255)),
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(*g)), 0, 255)),
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(*b)), 0, 255)),
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(*a)), 0, 255)),
    };
}

std::vector<std::string> JsonStringArrayValue(const nlohmann::json& object, const char* key) {
    std::vector<std::string> values;
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_array()) {
        return values;
    }
    for (const auto& value : object.at(key)) {
        if (value.is_string()) {
            values.push_back(value.get<std::string>());
        }
    }
    return values;
}

struct DecodedOot3dNativePicaShadowProjectionRegisters {
    bool TraceAvailable = false;
    std::string TraceSourceKind;
    std::string TraceFormat;
    bool DmpShadowZBiasDecoded = false;
    bool DmpShadowZScaleDecoded = false;
    bool DmpShadowZUniformsDecoded = false;
    bool DmpPerspectiveShadowDecoded = false;
    bool DmpPerspectiveShadow = false;
    double DmpShadowZBias = 0.0;
    double DmpShadowZScale = 0.0;
    bool TextureShadowRegisterDecoded = false;
    uint32_t TextureShadowRegisterRaw = 0;
    bool TextureShadowOrthographic = false;
    uint32_t TextureShadowRawBias = 0;
    uint32_t TextureShadowCompareBias = 0;
    bool FramebufferShadowRegisterDecoded = false;
    uint32_t FramebufferShadowRegisterRaw = 0;
    uint32_t FramebufferShadowConstantRaw = 0;
    uint32_t FramebufferShadowLinearRaw = 0;
    double FramebufferShadowConstant = 0.0;
    double FramebufferShadowLinear = 0.0;
    bool FragmentLightingEnableDecoded = false;
    uint32_t FragmentLightingEnableRaw = 0;
    bool FragmentLightingEnabled = false;
    bool LightingConfig0Decoded = false;
    bool LightingConfig1Decoded = false;
    bool ShaderRouteRegisterTraceDecoded = false;
    uint32_t LightingConfig0Raw = 0;
    uint32_t LightingConfig1Raw = 0;
    bool LightingEnableShadow = false;
    bool LightingShadowPrimary = false;
    bool LightingShadowSecondary = false;
    bool LightingShadowInvert = false;
    bool LightingShadowAlpha = false;
    uint32_t ShadowSelector = 0;
    uint32_t PerLightShadowEnableMask = 0;
    bool TextureDimDecoded[3] = {};
    uint32_t TextureDimRaw[3] = {};
    bool TextureParamDecoded[3] = {};
    uint32_t TextureParamRaw[3] = {};
    bool ShadowTextureParamDecoded = false;
    uint32_t ShadowTextureParamRegisterIndex = 0;
    uint32_t ShadowTextureParamRaw = 0;
    uint32_t ShadowTextureType = 0;
    bool ShadowTextureIsShadow2d = false;
    bool ShadowTextureDimDecoded = false;
    uint32_t ShadowTextureDimRegisterIndex = 0;
    uint32_t ShadowTextureDimRaw = 0;
    uint32_t ShadowTextureWidth = 0;
    uint32_t ShadowTextureHeight = 0;
    bool ShaderRouteMatchesPrimaryRgbShadowTerm = false;
    std::string DecodeSource;
    std::string ShaderRouteDecodeSource;
};

std::string TrimAscii(std::string value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<uint32_t> JsonUint32Flexible(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const auto parsed = value.get<uint64_t>();
        if (parsed <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(parsed);
        }
        return std::nullopt;
    }
    if (value.is_number_integer()) {
        const auto parsed = value.get<int64_t>();
        if (parsed >= 0 &&
            static_cast<uint64_t>(parsed) <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(parsed);
        }
        return std::nullopt;
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    const std::string text = TrimAscii(value.get<std::string>());
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        size_t parsedLength = 0;
        const auto parsed = std::stoull(text, &parsedLength, 0);
        if (parsedLength == text.size() &&
            parsed <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<double> JsonDoubleFlexible(const nlohmann::json& value) {
    if (value.is_number()) {
        const double parsed = value.get<double>();
        return std::isfinite(parsed) ? std::optional<double>(parsed) : std::nullopt;
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    const std::string text = TrimAscii(value.get<std::string>());
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        size_t parsedLength = 0;
        const double parsed = std::stod(text, &parsedLength);
        if (parsedLength == text.size() && std::isfinite(parsed)) {
            return parsed;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<bool> JsonBoolFlexible(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        const auto parsed = value.get<int64_t>();
        if (parsed == 0 || parsed == 1) {
            return parsed != 0;
        }
        return std::nullopt;
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    const std::string text = LowerAscii(TrimAscii(value.get<std::string>()));
    if (text == "true" || text == "1" || text == "yes") {
        return true;
    }
    if (text == "false" || text == "0" || text == "no") {
        return false;
    }
    return std::nullopt;
}

const nlohmann::json* JsonPayloadValue(const nlohmann::json& value) {
    if (!value.is_object()) {
        return &value;
    }

    for (const char* key : { "final_value", "value", "raw", "raw_value" }) {
        if (value.contains(key)) {
            return &value.at(key);
        }
    }
    return &value;
}

std::optional<uint32_t> JsonUint32Payload(const nlohmann::json& value) {
    const auto* payload = JsonPayloadValue(value);
    return payload == nullptr ? std::nullopt : JsonUint32Flexible(*payload);
}

std::optional<double> JsonDoublePayload(const nlohmann::json& value) {
    const auto* payload = JsonPayloadValue(value);
    return payload == nullptr ? std::nullopt : JsonDoubleFlexible(*payload);
}

std::optional<bool> JsonBoolPayload(const nlohmann::json& value) {
    const auto* payload = JsonPayloadValue(value);
    return payload == nullptr ? std::nullopt : JsonBoolFlexible(*payload);
}

template <typename Parser>
auto JsonMemberByKeys(const nlohmann::json& object, std::initializer_list<const char*> keys,
                      Parser parser) -> decltype(parser(object)) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    for (const char* key : keys) {
        if (object.contains(key)) {
            auto parsed = parser(object.at(key));
            if (parsed.has_value()) {
                return parsed;
            }
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> PicaRegisterIndexFromString(std::string value) {
    const std::string trimmed = TrimAscii(std::move(value));
    const std::string lowered = LowerAscii(trimmed);
    if (lowered == "gpureg_texunit0_dim" || lowered == "texunit0_dim" ||
        lowered == "texture0_dim") {
        return kPicaRegTexture0Dim;
    }
    if (lowered == "gpureg_texunit0_param" || lowered == "texunit0_param" ||
        lowered == "texture0_param") {
        return kPicaRegTexture0Param;
    }
    if (lowered == "gpureg_texunit1_dim" || lowered == "texunit1_dim" ||
        lowered == "texture1_dim") {
        return kPicaRegTexture1Dim;
    }
    if (lowered == "gpureg_texunit1_param" || lowered == "texunit1_param" ||
        lowered == "texture1_param") {
        return kPicaRegTexture1Param;
    }
    if (lowered == "gpureg_texunit2_dim" || lowered == "texunit2_dim" ||
        lowered == "texture2_dim") {
        return kPicaRegTexture2Dim;
    }
    if (lowered == "gpureg_texunit2_param" || lowered == "texunit2_param" ||
        lowered == "texture2_param") {
        return kPicaRegTexture2Param;
    }
    if (lowered == "gpureg_texunit0_shadow" || lowered == "texunit0_shadow" ||
        lowered == "pica::texturingregs::shadow" || lowered == "texturing.shadow" ||
        lowered == "texture_shadow") {
        return kPicaRegTexunit0Shadow;
    }
    if (lowered == "gpureg_lighting_enable0" || lowered == "lighting_enable0" ||
        lowered == "fragment_lighting_enable") {
        return kPicaRegLightingEnable0;
    }
    if (lowered == "gpureg_fragop_shadow" || lowered == "fragop_shadow" ||
        lowered == "pica::framebufferregs::shadow" || lowered == "framebuffer.shadow" ||
        lowered == "framebuffer_shadow") {
        return kPicaRegFragopShadow;
    }
    if (lowered == "gpureg_lighting_config0" || lowered == "lighting_config0" ||
        lowered == "pica::lightingregs::config0" || lowered == "lighting.config0") {
        return kPicaRegLightingConfig0;
    }
    if (lowered == "gpureg_lighting_config1" || lowered == "lighting_config1" ||
        lowered == "pica::lightingregs::config1" || lowered == "lighting.config1") {
        return kPicaRegLightingConfig1;
    }
    return JsonUint32Flexible(trimmed);
}

std::optional<uint32_t> PicaRegisterIndexFromJsonValue(const nlohmann::json& value) {
    if (auto parsed = JsonUint32Flexible(value); parsed.has_value()) {
        return parsed;
    }
    if (value.is_string()) {
        return PicaRegisterIndexFromString(value.get<std::string>());
    }
    return std::nullopt;
}

uint32_t PicaExpandedByteMask(uint32_t mask) {
    uint32_t expanded = 0;
    for (uint32_t byteIndex = 0; byteIndex < 4; ++byteIndex) {
        if ((mask & (1u << byteIndex)) != 0) {
            expanded |= 0xFFu << (byteIndex * 8);
        }
    }
    return expanded;
}

double PicaFloat1_5_10ToDouble(uint16_t raw) {
    const bool negative = (raw & 0x8000u) != 0;
    const int exponent = static_cast<int>((raw >> 10) & 0x1Fu);
    const uint32_t fraction = raw & 0x03FFu;
    double value = 0.0;
    if (exponent == 0) {
        value = fraction == 0 ? 0.0 : std::ldexp(static_cast<double>(fraction) / 1024.0, -14);
    } else if (exponent == 0x1F) {
        value = fraction == 0 ? std::numeric_limits<double>::infinity()
                              : std::numeric_limits<double>::quiet_NaN();
    } else {
        value = std::ldexp(1.0 + static_cast<double>(fraction) / 1024.0, exponent - 15);
    }
    return negative ? -value : value;
}

void UpdateDecodedPicaShadowShaderRoute(DecodedOot3dNativePicaShadowProjectionRegisters& decoded) {
    decoded.ShaderRouteRegisterTraceDecoded =
        decoded.FragmentLightingEnableDecoded && decoded.LightingConfig0Decoded &&
        decoded.LightingConfig1Decoded;
    decoded.ShadowTextureParamDecoded = false;
    decoded.ShadowTextureParamRegisterIndex = 0;
    decoded.ShadowTextureParamRaw = 0;
    decoded.ShadowTextureType = 0;
    decoded.ShadowTextureIsShadow2d = false;
    decoded.ShadowTextureDimDecoded = false;
    decoded.ShadowTextureDimRegisterIndex = 0;
    decoded.ShadowTextureDimRaw = 0;
    decoded.ShadowTextureWidth = 0;
    decoded.ShadowTextureHeight = 0;

    if (decoded.LightingConfig0Decoded && decoded.ShadowSelector < 3 &&
        decoded.TextureParamDecoded[decoded.ShadowSelector]) {
        const uint32_t selector = decoded.ShadowSelector;
        decoded.ShadowTextureParamDecoded = true;
        decoded.ShadowTextureParamRegisterIndex =
            selector == 0 ? kPicaRegTexture0Param
                          : selector == 1 ? kPicaRegTexture1Param : kPicaRegTexture2Param;
        decoded.ShadowTextureParamRaw = decoded.TextureParamRaw[selector];
        decoded.ShadowTextureType = (decoded.ShadowTextureParamRaw >> 28) & 0x7u;
        decoded.ShadowTextureIsShadow2d =
            decoded.ShadowTextureType == kPicaTextureTypeShadow2d;
    }
    if (decoded.LightingConfig0Decoded && decoded.ShadowSelector < 3 &&
        decoded.TextureDimDecoded[decoded.ShadowSelector]) {
        const uint32_t selector = decoded.ShadowSelector;
        decoded.ShadowTextureDimDecoded = true;
        decoded.ShadowTextureDimRegisterIndex =
            selector == 0 ? kPicaRegTexture0Dim
                          : selector == 1 ? kPicaRegTexture1Dim : kPicaRegTexture2Dim;
        decoded.ShadowTextureDimRaw = decoded.TextureDimRaw[selector];
        decoded.ShadowTextureHeight = decoded.ShadowTextureDimRaw & 0x7FFu;
        decoded.ShadowTextureWidth = (decoded.ShadowTextureDimRaw >> 16) & 0x7FFu;
    }

    decoded.ShaderRouteMatchesPrimaryRgbShadowTerm =
        decoded.ShaderRouteRegisterTraceDecoded && decoded.ShadowTextureParamDecoded &&
        decoded.FragmentLightingEnabled && decoded.LightingEnableShadow &&
        decoded.LightingShadowPrimary && decoded.PerLightShadowEnableMask != 0 &&
        decoded.ShadowTextureIsShadow2d;

    if (decoded.ShaderRouteMatchesPrimaryRgbShadowTerm) {
        decoded.ShaderRouteDecodeSource =
            "oot3d_native_pica_lighting_shadow_route_register_trace";
    }
}

void RecordDecodedPicaShadowRegister(DecodedOot3dNativePicaShadowProjectionRegisters& decoded,
                                     uint32_t registerIndex, uint32_t registerValue) {
    if (registerIndex == kPicaRegTexture0Dim || registerIndex == kPicaRegTexture1Dim ||
        registerIndex == kPicaRegTexture2Dim) {
        const uint32_t slot = registerIndex == kPicaRegTexture0Dim
                                  ? 0
                                  : registerIndex == kPicaRegTexture1Dim ? 1 : 2;
        decoded.TextureDimDecoded[slot] = true;
        decoded.TextureDimRaw[slot] = registerValue;
        UpdateDecodedPicaShadowShaderRoute(decoded);
        return;
    }

    if (registerIndex == kPicaRegTexture0Param || registerIndex == kPicaRegTexture1Param ||
        registerIndex == kPicaRegTexture2Param) {
        const uint32_t slot = registerIndex == kPicaRegTexture0Param
                                  ? 0
                                  : registerIndex == kPicaRegTexture1Param ? 1 : 2;
        decoded.TextureParamDecoded[slot] = true;
        decoded.TextureParamRaw[slot] = registerValue;
        UpdateDecodedPicaShadowShaderRoute(decoded);
        return;
    }

    if (registerIndex == kPicaRegTexunit0Shadow) {
        decoded.TextureShadowRegisterDecoded = true;
        decoded.TextureShadowRegisterRaw = registerValue;
        decoded.TextureShadowOrthographic = (registerValue & 0x1u) != 0;
        decoded.TextureShadowRawBias = (registerValue >> 1) & 0x7FFFFFu;
        decoded.TextureShadowCompareBias = decoded.TextureShadowRawBias << 1;
        return;
    }

    if (registerIndex == kPicaRegLightingEnable0) {
        decoded.FragmentLightingEnableDecoded = true;
        decoded.FragmentLightingEnableRaw = registerValue;
        decoded.FragmentLightingEnabled = (registerValue & 0x1u) != 0;
        UpdateDecodedPicaShadowShaderRoute(decoded);
        return;
    }

    if (registerIndex == kPicaRegFragopShadow) {
        decoded.FramebufferShadowRegisterDecoded = true;
        decoded.FramebufferShadowRegisterRaw = registerValue;
        decoded.FramebufferShadowConstantRaw = registerValue & 0xFFFFu;
        decoded.FramebufferShadowLinearRaw = (registerValue >> 16) & 0xFFFFu;
        decoded.FramebufferShadowConstant =
            PicaFloat1_5_10ToDouble(static_cast<uint16_t>(decoded.FramebufferShadowConstantRaw));
        decoded.FramebufferShadowLinear =
            PicaFloat1_5_10ToDouble(static_cast<uint16_t>(decoded.FramebufferShadowLinearRaw));
        return;
    }

    if (registerIndex == kPicaRegLightingConfig0) {
        decoded.LightingConfig0Decoded = true;
        decoded.LightingConfig0Raw = registerValue;
        decoded.LightingEnableShadow = (registerValue & 0x1u) != 0;
        decoded.LightingShadowPrimary = ((registerValue >> 16) & 0x1u) != 0;
        decoded.LightingShadowSecondary = ((registerValue >> 17) & 0x1u) != 0;
        decoded.LightingShadowInvert = ((registerValue >> 18) & 0x1u) != 0;
        decoded.LightingShadowAlpha = ((registerValue >> 19) & 0x1u) != 0;
        decoded.ShadowSelector = (registerValue >> 24) & 0x3u;
        UpdateDecodedPicaShadowShaderRoute(decoded);
        return;
    }

    if (registerIndex == kPicaRegLightingConfig1) {
        decoded.LightingConfig1Decoded = true;
        decoded.LightingConfig1Raw = registerValue;
        const uint32_t disabledShadowMask = registerValue & 0xFFu;
        decoded.PerLightShadowEnableMask = (~disabledShadowMask) & 0xFFu;
        UpdateDecodedPicaShadowShaderRoute(decoded);
    }
}

void DecodePicaRegisterObject(const nlohmann::json& registers,
                              DecodedOot3dNativePicaShadowProjectionRegisters& decoded) {
    if (registers.is_object()) {
        for (auto it = registers.begin(); it != registers.end(); ++it) {
            const auto registerIndex = PicaRegisterIndexFromString(it.key());
            const auto registerValue = JsonUint32Payload(it.value());
            if (registerIndex.has_value() && registerValue.has_value()) {
                RecordDecodedPicaShadowRegister(decoded, *registerIndex, *registerValue);
            }
        }
        return;
    }

    if (!registers.is_array()) {
        return;
    }
    for (const auto& entry : registers) {
        if (!entry.is_object()) {
            continue;
        }
        const auto registerIndex =
            JsonMemberByKeys(entry, { "cmd_id", "register_id", "register", "reg", "id", "index", "name" },
                             [](const nlohmann::json& value) {
                                 return PicaRegisterIndexFromJsonValue(value);
                             });
        const auto registerValue =
            JsonMemberByKeys(entry, { "final_value", "value", "raw" }, JsonUint32Payload);
        if (registerIndex.has_value() && registerValue.has_value()) {
            RecordDecodedPicaShadowRegister(decoded, *registerIndex, *registerValue);
        }
    }
}

void DecodePicaRegisterWrites(const nlohmann::json& writes,
                              DecodedOot3dNativePicaShadowProjectionRegisters& decoded) {
    if (!writes.is_array()) {
        return;
    }

    std::map<uint32_t, uint32_t> registerValues;
    if (decoded.TextureDimDecoded[0]) {
        registerValues[kPicaRegTexture0Dim] = decoded.TextureDimRaw[0];
    }
    if (decoded.TextureDimDecoded[1]) {
        registerValues[kPicaRegTexture1Dim] = decoded.TextureDimRaw[1];
    }
    if (decoded.TextureDimDecoded[2]) {
        registerValues[kPicaRegTexture2Dim] = decoded.TextureDimRaw[2];
    }
    if (decoded.TextureParamDecoded[0]) {
        registerValues[kPicaRegTexture0Param] = decoded.TextureParamRaw[0];
    }
    if (decoded.TextureParamDecoded[1]) {
        registerValues[kPicaRegTexture1Param] = decoded.TextureParamRaw[1];
    }
    if (decoded.TextureParamDecoded[2]) {
        registerValues[kPicaRegTexture2Param] = decoded.TextureParamRaw[2];
    }
    if (decoded.TextureShadowRegisterDecoded) {
        registerValues[kPicaRegTexunit0Shadow] = decoded.TextureShadowRegisterRaw;
    }
    if (decoded.FramebufferShadowRegisterDecoded) {
        registerValues[kPicaRegFragopShadow] = decoded.FramebufferShadowRegisterRaw;
    }
    if (decoded.FragmentLightingEnableDecoded) {
        registerValues[kPicaRegLightingEnable0] = decoded.FragmentLightingEnableRaw;
    }
    if (decoded.LightingConfig0Decoded) {
        registerValues[kPicaRegLightingConfig0] = decoded.LightingConfig0Raw;
    }
    if (decoded.LightingConfig1Decoded) {
        registerValues[kPicaRegLightingConfig1] = decoded.LightingConfig1Raw;
    }

    for (const auto& write : writes) {
        if (!write.is_object()) {
            continue;
        }
        const auto registerIndex =
            JsonMemberByKeys(write, { "cmd_id", "register_id", "register", "reg", "id", "index", "name" },
                             [](const nlohmann::json& value) {
                                 return PicaRegisterIndexFromJsonValue(value);
                             });
        if (!registerIndex.has_value()) {
            continue;
        }

        auto finalValue = JsonMemberByKeys(write, { "final_value", "value" }, JsonUint32Payload);
        if (!finalValue.has_value()) {
            const auto commandValue =
                JsonMemberByKeys(write, { "command_value", "raw_value" }, JsonUint32Payload);
            if (!commandValue.has_value()) {
                continue;
            }
            const auto priorIt = registerValues.find(*registerIndex);
            const uint32_t priorValue =
                priorIt == registerValues.end() ? 0 : priorIt->second;
            const uint32_t mask = JsonMemberByKeys(write, { "mask" }, JsonUint32Payload).value_or(0xFu);
            const uint32_t byteMask = PicaExpandedByteMask(mask & 0xFu);
            finalValue = (priorValue & ~byteMask) | (*commandValue & byteMask);
        }

        registerValues[*registerIndex] = *finalValue;
        RecordDecodedPicaShadowRegister(decoded, *registerIndex, *finalValue);
    }
}

void DecodeDmpShadowUniformObject(const nlohmann::json& uniforms,
                                  DecodedOot3dNativePicaShadowProjectionRegisters& decoded) {
    if (!uniforms.is_object()) {
        return;
    }

    if (const auto value =
            JsonMemberByKeys(uniforms,
                             { "dmp_Texture[0].shadowZBias", "shadowZBias", "shadow_z_bias" },
                             JsonDoublePayload);
        value.has_value()) {
        decoded.DmpShadowZBiasDecoded = true;
        decoded.DmpShadowZBias = *value;
    }
    if (const auto value =
            JsonMemberByKeys(uniforms,
                             { "dmp_Texture[0].shadowZScale", "shadowZScale", "shadow_z_scale" },
                             JsonDoublePayload);
        value.has_value()) {
        decoded.DmpShadowZScaleDecoded = true;
        decoded.DmpShadowZScale = *value;
    }
    if (const auto value = JsonMemberByKeys(uniforms,
                                            { "dmp_Texture[0].perspectiveShadow",
                                              "perspectiveShadow", "perspective_shadow" },
                                            JsonBoolPayload);
        value.has_value()) {
        decoded.DmpPerspectiveShadowDecoded = true;
        decoded.DmpPerspectiveShadow = *value;
    }
    decoded.DmpShadowZUniformsDecoded =
        decoded.DmpShadowZBiasDecoded && decoded.DmpShadowZScaleDecoded;
}

DecodedOot3dNativePicaShadowProjectionRegisters DecodeOot3dNativePicaShadowProjectionRegisters(
    const Oot3dNativeDemoScene& scene) {
    DecodedOot3dNativePicaShadowProjectionRegisters decoded;
    decoded.TraceAvailable =
        scene.NativePicaRegisterTraceAvailable && scene.NativePicaRegisterTrace.is_object();
    decoded.TraceSourceKind = scene.NativePicaRegisterTraceSourceKind;
    decoded.TraceFormat = scene.NativePicaRegisterTraceFormat;
    if (!decoded.TraceAvailable) {
        return decoded;
    }

    const auto& trace = scene.NativePicaRegisterTrace;
    DecodeDmpShadowUniformObject(trace, decoded);
    if (const auto* uniforms = JsonObjectChild(trace, "uniforms"); uniforms != nullptr) {
        DecodeDmpShadowUniformObject(*uniforms, decoded);
    }
    if (const auto* uniforms = JsonObjectChild(trace, "dmp_uniforms"); uniforms != nullptr) {
        DecodeDmpShadowUniformObject(*uniforms, decoded);
    }
    if (const auto* texture = JsonObjectChild(trace, "texture_shadow"); texture != nullptr) {
        DecodeDmpShadowUniformObject(*texture, decoded);
    }

    if (trace.contains("registers")) {
        DecodePicaRegisterObject(trace.at("registers"), decoded);
    }
    if (trace.contains("pica_registers")) {
        DecodePicaRegisterObject(trace.at("pica_registers"), decoded);
    }
    if (trace.contains("writes")) {
        DecodePicaRegisterWrites(trace.at("writes"), decoded);
    }
    if (trace.contains("pica_writes")) {
        DecodePicaRegisterWrites(trace.at("pica_writes"), decoded);
    }

    if (decoded.DmpShadowZUniformsDecoded &&
        (decoded.TextureShadowRegisterDecoded || decoded.FramebufferShadowRegisterDecoded)) {
        decoded.DecodeSource =
            "oot3d_native_dmp_shadow_z_uniforms_and_pica_shadow_register_trace";
    } else if (decoded.DmpShadowZUniformsDecoded) {
        decoded.DecodeSource = "oot3d_native_dmp_shadow_z_uniform_trace";
    } else if (decoded.TextureShadowRegisterDecoded || decoded.FramebufferShadowRegisterDecoded) {
        decoded.DecodeSource = "oot3d_native_pica_shadow_register_trace";
    }

    return decoded;
}

const Oot3dNativeDemoPicaLightSettingsRecord* SelectPicaLightingRecord(
    const Oot3dNativeDemoPicaLightingState& lighting,
    const std::string& selector,
    int playerFloorLightSettingIndex) {
    for (const auto& record : lighting.LightSettings) {
        if (record.SetupIndex != lighting.ActiveSetupIndex) {
            continue;
        }
        if (selector == "active_setup_record_from_player_floor_light_setting_index") {
            if (playerFloorLightSettingIndex >= 0 && record.Index == playerFloorLightSettingIndex) {
                return &record;
            }
            continue;
        }
        if (selector == "first_active_setup_record") {
            return &record;
        }
        if (selector == "first_active_setup_record_with_nonzero_light0_vector" &&
            record.ByteGroups.size() > 1 &&
            (record.ByteGroups[1].Raw0 != 0 || record.ByteGroups[1].Raw1 != 0 ||
             record.ByteGroups[1].Raw2 != 0)) {
            return &record;
        }
        if (selector == "first_active_setup_record_with_nonzero_light0_color" &&
            record.NativeRuntimeEnvironmentLightSettingsAvailable &&
            !ColorRgbIsZero(record.NativeRuntimeLight0Color)) {
            return &record;
        }
    }
    return nullptr;
}

const Oot3dNativeDemoPicaLightSettingsRecord* FindPicaLightingRecordBySetupAndIndex(
    const Oot3dNativeDemoPicaLightingState& lighting,
    int setupIndex,
    int recordIndex) {
    for (const auto& record : lighting.LightSettings) {
        if (record.SetupIndex == setupIndex && record.Index == recordIndex) {
            return &record;
        }
    }
    return nullptr;
}

bool CanReadRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset, int count);
Vec3f NormalizeVec3(Vec3f value);
Vec3f NativeLerpDirection(Vec3f from, Vec3f to, double weight);

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

uint8_t NativeLerpU8(uint8_t from, uint8_t to, double weight) {
    const double value = static_cast<double>(from) +
                         (static_cast<double>(to) - static_cast<double>(from)) * Clamp01(weight);
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

int8_t NativeLerpS8(uint8_t from, uint8_t to, double weight) {
    const auto fromSigned = static_cast<int>(static_cast<int8_t>(from));
    const auto toSigned = static_cast<int>(static_cast<int8_t>(to));
    const double value = static_cast<double>(fromSigned) +
                         (static_cast<double>(toSigned - fromSigned)) * Clamp01(weight);
    return static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(value)), -128, 127));
}

ColorRgba8 NativeLerpColor(ColorRgba8 from, ColorRgba8 to, double weight) {
    return {
        NativeLerpU8(from.R, to.R, weight),
        NativeLerpU8(from.G, to.G, weight),
        NativeLerpU8(from.B, to.B, weight),
        NativeLerpU8(from.A, to.A, weight),
    };
}

bool HasNativeRuntimeEnvironmentRecord(const Oot3dNativeDemoPicaLightSettingsRecord& record) {
    return record.EntrySize == static_cast<int>(NativeZsiLightSettingsRecordLayout().NativeRecordSizeBytes) &&
           record.NativeRuntimeEnvironmentLightSettingsAvailable;
}

bool CanReadRuntimeRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record,
                               int offset,
                               int count) {
    if (offset < 0 || count <= 0) {
        return false;
    }
    const auto& bytes = record.NativeRuntimeEnvironmentRawBytes.empty()
                            ? record.RawBytes
                            : record.NativeRuntimeEnvironmentRawBytes;
    return static_cast<size_t>(offset) + static_cast<size_t>(count) <= bytes.size();
}

uint8_t RuntimeRecordByte(const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset) {
    const auto& bytes = record.NativeRuntimeEnvironmentRawBytes.empty()
                            ? record.RawBytes
                            : record.NativeRuntimeEnvironmentRawBytes;
    return bytes[static_cast<size_t>(offset)];
}

ColorRgba8 RuntimeColorFromRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset) {
    if (!CanReadRuntimeRecordBytes(record, offset, 3)) {
        return {};
    }
    return {
        RuntimeRecordByte(record, offset + 0),
        RuntimeRecordByte(record, offset + 1),
        RuntimeRecordByte(record, offset + 2),
        255,
    };
}

Vec3f RuntimeVectorFromRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset) {
    if (!CanReadRuntimeRecordBytes(record, offset, 3)) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return NormalizeVec3({
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 0))),
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 1))),
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 2))),
    });
}

Vec3f RuntimeCompactPayloadDirectionFromRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record,
                                                    int offset,
                                                    uint32_t denominator) {
    if (!CanReadRuntimeRecordBytes(record, offset, 3) || denominator == 0) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return {
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 0))) /
            static_cast<float>(denominator),
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 1))) /
            static_cast<float>(denominator),
        static_cast<float>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 2))) /
            static_cast<float>(denominator),
    };
}

std::array<int, 3> RuntimeDirectionS8FromRecordBytes(
    const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset) {
    if (!CanReadRuntimeRecordBytes(record, offset, 3)) {
        return { 0, 0, 0 };
    }
    return {
        static_cast<int>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 0))),
        static_cast<int>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 1))),
        static_cast<int>(static_cast<int8_t>(RuntimeRecordByte(record, offset + 2))),
    };
}

std::array<uint8_t, 6> RuntimeCompactPayloadBytesFromRecord(
    const Oot3dNativeDemoPicaLightSettingsRecord& record, int directionOffset, int colorOffset) {
    std::array<uint8_t, 6> bytes = { 0, 0, 0, 0, 0, 0 };
    if (!CanReadRuntimeRecordBytes(record, directionOffset, 3) ||
        !CanReadRuntimeRecordBytes(record, colorOffset, 3)) {
        return bytes;
    }
    for (int index = 0; index < 3; ++index) {
        bytes[static_cast<size_t>(index)] =
            RuntimeRecordByte(record, directionOffset + index);
        bytes[static_cast<size_t>(index + 3)] =
            RuntimeRecordByte(record, colorOffset + index);
    }
    return bytes;
}

int8_t NativeActorVsCompactPayloadTrigComponent(double value) {
    return static_cast<int8_t>(
        std::clamp(static_cast<int>(std::lrint(value)), -128, 127));
}

std::array<int, 3> RuntimeActorVsCompactPayloadDirectionS8FromActiveAngle(
    int activeAngle, const NativeZsiLightSettingsRecordContract& layout) {
    if (activeAngle < 0) {
        return { 0, 0, 0 };
    }

    const uint16_t biasedRaw = static_cast<uint16_t>(
        (activeAngle - static_cast<int>(layout.RuntimeActorVsCompactPayloadDirectionAngleBias)) &
        0xFFFF);
    const auto signedAngle = static_cast<int16_t>(biasedRaw);
    const double radians = static_cast<double>(signedAngle) * kOot3dS16AngleToRadians;
    const double sine = std::sin(radians);
    const double cosine = std::cos(radians);
    return {
        static_cast<int>(NativeActorVsCompactPayloadTrigComponent(
            sine * static_cast<double>(layout.RuntimeActorVsCompactPayloadDirectionSinScaleX))),
        static_cast<int>(NativeActorVsCompactPayloadTrigComponent(
            cosine * static_cast<double>(layout.RuntimeActorVsCompactPayloadDirectionCosScaleY))),
        static_cast<int>(NativeActorVsCompactPayloadTrigComponent(
            cosine * static_cast<double>(layout.RuntimeActorVsCompactPayloadDirectionCosScaleZ))),
    };
}

std::array<int, 3> NegatedS8Direction(std::array<int, 3> value) {
    return {
        std::clamp(-value[0], -128, 127),
        std::clamp(-value[1], -128, 127),
        std::clamp(-value[2], -128, 127),
    };
}

uint8_t S8ToByte(int value) {
    return static_cast<uint8_t>(static_cast<int8_t>(std::clamp(value, -128, 127)));
}

Vec3f RuntimeCompactPayloadDirectionFromS8(std::array<int, 3> direction,
                                           uint32_t denominator) {
    if (denominator == 0) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return NormalizeVec3({
        static_cast<float>(direction[0]) / static_cast<float>(denominator),
        static_cast<float>(direction[1]) / static_cast<float>(denominator),
        static_cast<float>(direction[2]) / static_cast<float>(denominator),
    });
}

std::array<uint8_t, 6> RuntimeCompactPayloadBytesFromDirectionAndColor(
    std::array<int, 3> direction, ColorRgba8 color) {
    return {
        S8ToByte(direction[0]),
        S8ToByte(direction[1]),
        S8ToByte(direction[2]),
        color.R,
        color.G,
        color.B,
    };
}

ColorRgba8 BlendRuntimeColorBytes(const Oot3dNativeDemoPicaLightSettingsRecord& from,
                                  const Oot3dNativeDemoPicaLightSettingsRecord& to,
                                  int offset,
                                  double weight) {
    if (!CanReadRuntimeRecordBytes(from, offset, 3) ||
        !CanReadRuntimeRecordBytes(to, offset, 3)) {
        return {};
    }
    return {
        NativeLerpU8(RuntimeRecordByte(from, offset + 0),
                     RuntimeRecordByte(to, offset + 0), weight),
        NativeLerpU8(RuntimeRecordByte(from, offset + 1),
                     RuntimeRecordByte(to, offset + 1), weight),
        NativeLerpU8(RuntimeRecordByte(from, offset + 2),
                     RuntimeRecordByte(to, offset + 2), weight),
        255,
    };
}

Vec3f BlendRuntimeVectorBytes(const Oot3dNativeDemoPicaLightSettingsRecord& from,
                              const Oot3dNativeDemoPicaLightSettingsRecord& to,
                              int offset,
                              double weight) {
    if (!CanReadRuntimeRecordBytes(from, offset, 3) ||
        !CanReadRuntimeRecordBytes(to, offset, 3)) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return NormalizeVec3({
        static_cast<float>(NativeLerpS8(RuntimeRecordByte(from, offset + 0),
                                        RuntimeRecordByte(to, offset + 0), weight)),
        static_cast<float>(NativeLerpS8(RuntimeRecordByte(from, offset + 1),
                                        RuntimeRecordByte(to, offset + 1), weight)),
        static_cast<float>(NativeLerpS8(RuntimeRecordByte(from, offset + 2),
                                        RuntimeRecordByte(to, offset + 2), weight)),
    });
}

double NativeLerpScalar(double from, double to, double weight) {
    const float fromF32 = static_cast<float>(from);
    const float toF32 = static_cast<float>(to);
    const float weightF32 = static_cast<float>(Clamp01(weight));
    const float weightedDeltaF32 = (toF32 - fromF32) * weightF32;
    return static_cast<double>(fromF32 + weightedDeltaF32);
}

double RuntimePackedFogNear(const Oot3dNativeDemoPicaLightSettingsRecord& record) {
    return static_cast<double>(record.NativeRuntimePackedHalfwordRaw &
                               NativeZsiLightSettingsRecordLayout().RuntimePackedHalfwordMask);
}

void PopulateRuntimeFogDistanceContract(
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    double cameraFar,
    double fogFarPreAddend,
    double fogNearInterpolated,
    int fogNearAddend = 0,
    double fogFarAddend = 0.0,
    bool addendsResolved = false) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    resolved.RuntimeFogDistanceContractResolved =
        layout.RuntimeCameraFarOutputOffset != 0 && layout.RuntimeFogFarOutputOffset != 0 &&
        layout.RuntimeFogNearOutputOffset != 0 && layout.RuntimeCameraFarPlayOffset != 0 &&
        layout.RuntimeFogFarPlayOffset != 0 && layout.RuntimeFogNearPlayOffset != 0;
    resolved.RuntimeFogDistanceAddendsResolved = addendsResolved;
    resolved.CameraFar = std::isfinite(cameraFar) ? cameraFar : 0.0;
    resolved.FogFarPreAddend = std::isfinite(fogFarPreAddend) ? fogFarPreAddend : 0.0;
    resolved.FogFarAddend = std::isfinite(fogFarAddend) ? fogFarAddend : 0.0;
    resolved.FogFar = std::max(0.0, resolved.FogFarPreAddend + resolved.FogFarAddend);
    resolved.FogNearInterpolated = std::isfinite(fogNearInterpolated) ? fogNearInterpolated : 0.0;
    resolved.FogNearPreAddend = std::clamp(static_cast<int>(resolved.FogNearInterpolated), 0, 0xFFFF);
    resolved.FogNearAddend = fogNearAddend;
    resolved.FogNear = std::max(0, resolved.FogNearPreAddend + resolved.FogNearAddend);
    resolved.RuntimeCameraFarUsedForRender =
        resolved.RuntimeFogDistanceContractResolved && resolved.CameraFar > 0.0;
    resolved.RuntimeFogDistancesUsedForRender =
        resolved.RuntimeFogDistanceContractResolved && resolved.FogFar > resolved.FogNear;
    resolved.CameraFarRecordOffset = layout.RuntimeScalar0Offset;
    resolved.FogFarRecordOffset = layout.RuntimeScalar1Offset;
    resolved.FogNearRecordOffset = layout.RuntimePackedHalfwordOffset;
    resolved.CameraFarOutputOffset = layout.RuntimeCameraFarOutputOffset;
    resolved.FogFarOutputOffset = layout.RuntimeFogFarOutputOffset;
    resolved.FogNearOutputOffset = layout.RuntimeFogNearOutputOffset;
    resolved.CameraFarPlayOffset = layout.RuntimeCameraFarPlayOffset;
    resolved.FogFarPlayOffset = layout.RuntimeFogFarPlayOffset;
    resolved.FogNearPlayOffset = layout.RuntimeFogNearPlayOffset;
    resolved.FogNearAddendStateOffset = layout.RuntimeFogNearAddendStateOffset;
    resolved.FogFarAddendStateOffset = layout.RuntimeFogFarAddendStateOffset;
    resolved.RuntimeFogDistanceSource =
        "0045dd50_maps_runtime_record_float_0_to_play_0x0a74_camera_far, float_1_plus_"
        "state_0x84_to_play_0x0a78_fog_far, and_packed_low10_plus_state_0x7e_to_"
        "play_0x0a7c_fog_near";
    resolved.RuntimeFogDistanceStatus =
        addendsResolved
            ? "native_runtime_fog_distance_record_values_and_addends_resolved"
            : "native_runtime_fog_distance_record_values_resolved; zero preaddend path used while "
              "state_0x7e_and_0x84_addends_are_not_materialized";
}

uint8_t NativeRuntimeClampColorAddend(uint8_t value, int addend) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(value) + addend, 0, 255));
}

ColorRgba8 NativeRuntimeApplyColorAddends(ColorRgba8 color, const std::array<int, 3>& addends) {
    return {
        NativeRuntimeClampColorAddend(color.R, addends[0]),
        NativeRuntimeClampColorAddend(color.G, addends[1]),
        NativeRuntimeClampColorAddend(color.B, addends[2]),
        color.A,
    };
}

std::array<int, 3> NativeRuntimeLerpColorAddends(const std::array<int, 3>& from,
                                                 const std::array<int, 3>& to,
                                                 double weight) {
    return {
        static_cast<int>(std::lround(static_cast<double>(from[0]) +
                                     (static_cast<double>(to[0] - from[0]) * weight))),
        static_cast<int>(std::lround(static_cast<double>(from[1]) +
                                     (static_cast<double>(to[1] - from[1]) * weight))),
        static_cast<int>(std::lround(static_cast<double>(from[2]) +
                                     (static_cast<double>(to[2] - from[2]) * weight))),
    };
}

bool NativeRuntimeColorAddendContractResolved(const NativeZsiLightSettingsRecordContract& layout) {
    return layout.RuntimePreAddendAmbientColorStateOffset != 0 &&
           layout.RuntimePreAddendLight0ColorStateOffset != 0 &&
           layout.RuntimePreAddendLight1ColorStateOffset != 0 &&
           layout.RuntimePreAddendFogColorStateOffset != 0 &&
           layout.RuntimeColorAddendAmbientStateOffset != 0 &&
           layout.RuntimeColorAddendLightStateOffset != 0 &&
           layout.RuntimeColorAddendFogStateOffset != 0 &&
           layout.RuntimeFinalAmbientColorOutputOffset != 0 &&
           layout.RuntimeFinalAmbientColorPlayOffset != 0 &&
           layout.RuntimeFinalFogColorOutputOffset != 0 &&
           layout.RuntimeFinalFogColorPlayOffset != 0;
}

void PopulateRuntimeLightFinalColorContract(
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    ColorRgba8 ambientPreAddendColor,
    ColorRgba8 light0PreAddendColor,
    ColorRgba8 light1PreAddendColor,
    const std::array<int, 3>& ambientColorAddends,
    const std::array<int, 3>& lightColorAddends,
    bool ambientColorAddendResolved,
    bool lightColorAddendResolved) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    resolved.AmbientPreAddendColor = ambientPreAddendColor;
    resolved.Light0PreAddendColor = light0PreAddendColor;
    resolved.Light1PreAddendColor = light1PreAddendColor;
    resolved.AmbientColorAddendI16 = ambientColorAddends;
    resolved.LightColorAddendI16 = lightColorAddends;
    resolved.AmbientColor = NativeRuntimeApplyColorAddends(
        ambientPreAddendColor, ambientColorAddends);
    resolved.Light0Color = NativeRuntimeApplyColorAddends(
        light0PreAddendColor, lightColorAddends);
    resolved.Light1Color = NativeRuntimeApplyColorAddends(
        light1PreAddendColor, lightColorAddends);
    resolved.RuntimeColorAddendContractResolved = NativeRuntimeColorAddendContractResolved(layout);
    resolved.RuntimeAmbientColorAddendResolved = ambientColorAddendResolved;
    resolved.RuntimeLightColorAddendResolved = lightColorAddendResolved;
    resolved.RuntimeFinalAmbientColorFormulaResolved = resolved.RuntimeColorAddendContractResolved;
    resolved.RuntimeFinalLightColorFormulaResolved =
        layout.RuntimePreAddendLight0ColorStateOffset != 0 &&
        layout.RuntimePreAddendLight1ColorStateOffset != 0 &&
        layout.RuntimeColorAddendLightStateOffset != 0 &&
        layout.RuntimeActorVsCompactPayloadColorSourceOffsets[0] != 0 &&
        layout.RuntimeActorVsCompactPayloadColorSourceOffsets[1] != 0;
    resolved.RuntimeFinalAmbientColorUsedForRender = ambientColorAddendResolved;
    resolved.RuntimeFinalLightColorUsedForRender = lightColorAddendResolved;
    resolved.AmbientPreAddendRecordOffset = layout.RuntimeAmbientColorOffset;
    resolved.Light0PreAddendRecordOffset = layout.RuntimeLight0ColorOffset;
    resolved.Light1PreAddendRecordOffset = layout.RuntimeLight1ColorOffset;
    resolved.AmbientPreAddendStateOffset = layout.RuntimePreAddendAmbientColorStateOffset;
    resolved.Light0PreAddendStateOffset = layout.RuntimePreAddendLight0ColorStateOffset;
    resolved.Light1PreAddendStateOffset = layout.RuntimePreAddendLight1ColorStateOffset;
    resolved.AmbientColorAddendStateOffset = layout.RuntimeColorAddendAmbientStateOffset;
    resolved.LightColorAddendStateOffset = layout.RuntimeColorAddendLightStateOffset;
    resolved.FinalAmbientColorOutputOffset = layout.RuntimeFinalAmbientColorOutputOffset;
    resolved.FinalAmbientColorPlayOffset = layout.RuntimeFinalAmbientColorPlayOffset;
    resolved.Light0FinalPayloadColorStateOffset =
        layout.RuntimeActorVsCompactPayloadColorSourceOffsets[0];
    resolved.Light1FinalPayloadColorStateOffset =
        layout.RuntimeActorVsCompactPayloadColorSourceOffsets[1];
    resolved.RuntimeColorAddendSource = layout.RuntimeColorAddendSource;
    resolved.RuntimeFinalAmbientColorSource =
        "0045dd50_writes_final_ambient_rgb_to_play_plus_0x0a7e_from_state_plus_0xb2_"
        "plus_s16_addends_state_plus_0x6c";
    resolved.RuntimeFinalLightColorSource =
        "0045dd50_writes_final_light_rgb_to_state_plus_0x33_0x4b_from_state_plus_0xb8_"
        "0xbe_plus_s16_addends_state_plus_0x72";
    resolved.RuntimeFinalAmbientColorStatus =
        ambientColorAddendResolved
            ? "native_runtime_ambient_addends_resolved_and_final_play_0x0a7e_color_available"
            : "native_runtime_ambient_preaddend_color_available_addend_state_0x6c_values_pending";
    resolved.RuntimeFinalLightColorStatus =
        lightColorAddendResolved
            ? "native_runtime_light_addends_resolved_and_final_payload_0x33_0x4b_colors_available"
            : "native_runtime_light_preaddend_color_available_addend_state_0x72_values_pending";
}

void PopulateRuntimeFogFinalColorContract(
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    ColorRgba8 fogPreAddendColor,
    const std::array<int, 3>& fogColorAddends,
    bool fogColorAddendResolved) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    resolved.FogPreAddendColor = fogPreAddendColor;
    resolved.FogColorAddendI16 = fogColorAddends;
    resolved.FinalFogColor = NativeRuntimeApplyColorAddends(fogPreAddendColor, fogColorAddends);
    resolved.FogColor = resolved.FinalFogColor;
    resolved.RuntimeColorAddendContractResolved = NativeRuntimeColorAddendContractResolved(layout);
    resolved.RuntimeFogColorAddendResolved = fogColorAddendResolved;
    resolved.RuntimeFinalFogColorFormulaResolved = resolved.RuntimeColorAddendContractResolved;
    resolved.RuntimeFinalFogColorUsedForRender = fogColorAddendResolved;
    resolved.FogPreAddendRecordOffset = layout.RuntimeFogColorOffset;
    resolved.FogPreAddendStateOffset = layout.RuntimePreAddendFogColorStateOffset;
    resolved.FogColorAddendStateOffset = layout.RuntimeColorAddendFogStateOffset;
    resolved.FinalFogColorOutputOffset = layout.RuntimeFinalFogColorOutputOffset;
    resolved.FinalFogColorPlayOffset = layout.RuntimeFinalFogColorPlayOffset;
    resolved.RuntimeColorAddendSource = layout.RuntimeColorAddendSource;
    resolved.RuntimeFinalFogColorSource = layout.RuntimeFinalFogColorSource;
    resolved.RuntimeFinalFogColorStatus =
        fogColorAddendResolved
            ? "native_runtime_fog_addends_resolved_and_final_play_0x0a82_color_available"
            : "native_runtime_fog_preaddend_color_available_addend_state_0x78_values_pending";
}

void ApplyRuntimeEnvironmentColorAddends(
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    if (runtimeEnvironment == nullptr || !runtimeEnvironment->ColorAddendsResolved) {
        return;
    }

    PopulateRuntimeLightFinalColorContract(
        resolved,
        resolved.AmbientPreAddendColor,
        resolved.Light0PreAddendColor,
        resolved.Light1PreAddendColor,
        runtimeEnvironment->AmbientColorAddends,
        runtimeEnvironment->LightColorAddends,
        true,
        true);
    PopulateRuntimeFogFinalColorContract(
        resolved,
        resolved.FogPreAddendColor,
        runtimeEnvironment->FogColorAddends,
        true);
    if (!runtimeEnvironment->ColorAddendSourceKind.empty()) {
        resolved.RuntimeColorAddendSource = runtimeEnvironment->ColorAddendSourceKind;
    }
    if (!runtimeEnvironment->ColorAddendSourceStatus.empty()) {
        resolved.RuntimeFinalAmbientColorStatus = runtimeEnvironment->ColorAddendSourceStatus;
        resolved.RuntimeFinalLightColorStatus = runtimeEnvironment->ColorAddendSourceStatus;
        resolved.RuntimeFinalFogColorStatus = runtimeEnvironment->ColorAddendSourceStatus;
    }
}

void CopyRuntimeFinalColorContractFields(
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    const Oot3dNativePicaResolvedRuntimeLightSetting& branch) {
    resolved.AmbientColor = branch.AmbientColor;
    resolved.Light0Color = branch.Light0Color;
    resolved.Light1Color = branch.Light1Color;
    resolved.AmbientPreAddendColor = branch.AmbientPreAddendColor;
    resolved.Light0PreAddendColor = branch.Light0PreAddendColor;
    resolved.Light1PreAddendColor = branch.Light1PreAddendColor;
    resolved.AmbientColorAddendI16 = branch.AmbientColorAddendI16;
    resolved.LightColorAddendI16 = branch.LightColorAddendI16;
    resolved.FogColor = branch.FogColor;
    resolved.FogPreAddendColor = branch.FogPreAddendColor;
    resolved.FogColorAddendI16 = branch.FogColorAddendI16;
    resolved.FinalFogColor = branch.FinalFogColor;
    resolved.RuntimeColorAddendContractResolved = branch.RuntimeColorAddendContractResolved;
    resolved.RuntimeAmbientColorAddendResolved = branch.RuntimeAmbientColorAddendResolved;
    resolved.RuntimeLightColorAddendResolved = branch.RuntimeLightColorAddendResolved;
    resolved.RuntimeFinalAmbientColorFormulaResolved =
        branch.RuntimeFinalAmbientColorFormulaResolved;
    resolved.RuntimeFinalLightColorFormulaResolved = branch.RuntimeFinalLightColorFormulaResolved;
    resolved.RuntimeFinalAmbientColorUsedForRender = branch.RuntimeFinalAmbientColorUsedForRender;
    resolved.RuntimeFinalLightColorUsedForRender = branch.RuntimeFinalLightColorUsedForRender;
    resolved.RuntimeFogColorAddendResolved = branch.RuntimeFogColorAddendResolved;
    resolved.RuntimeFinalFogColorFormulaResolved = branch.RuntimeFinalFogColorFormulaResolved;
    resolved.RuntimeFinalFogColorUsedForRender = branch.RuntimeFinalFogColorUsedForRender;
    resolved.RuntimeColorAddendSource = branch.RuntimeColorAddendSource;
    resolved.RuntimeFinalAmbientColorSource = branch.RuntimeFinalAmbientColorSource;
    resolved.RuntimeFinalAmbientColorStatus = branch.RuntimeFinalAmbientColorStatus;
    resolved.RuntimeFinalLightColorSource = branch.RuntimeFinalLightColorSource;
    resolved.RuntimeFinalLightColorStatus = branch.RuntimeFinalLightColorStatus;
    resolved.RuntimeFinalFogColorSource = branch.RuntimeFinalFogColorSource;
    resolved.RuntimeFinalFogColorStatus = branch.RuntimeFinalFogColorStatus;
    resolved.AmbientPreAddendRecordOffset = branch.AmbientPreAddendRecordOffset;
    resolved.Light0PreAddendRecordOffset = branch.Light0PreAddendRecordOffset;
    resolved.Light1PreAddendRecordOffset = branch.Light1PreAddendRecordOffset;
    resolved.AmbientPreAddendStateOffset = branch.AmbientPreAddendStateOffset;
    resolved.Light0PreAddendStateOffset = branch.Light0PreAddendStateOffset;
    resolved.Light1PreAddendStateOffset = branch.Light1PreAddendStateOffset;
    resolved.AmbientColorAddendStateOffset = branch.AmbientColorAddendStateOffset;
    resolved.LightColorAddendStateOffset = branch.LightColorAddendStateOffset;
    resolved.FinalAmbientColorOutputOffset = branch.FinalAmbientColorOutputOffset;
    resolved.FinalAmbientColorPlayOffset = branch.FinalAmbientColorPlayOffset;
    resolved.Light0FinalPayloadColorStateOffset = branch.Light0FinalPayloadColorStateOffset;
    resolved.Light1FinalPayloadColorStateOffset = branch.Light1FinalPayloadColorStateOffset;
    resolved.FogPreAddendRecordOffset = branch.FogPreAddendRecordOffset;
    resolved.FogPreAddendStateOffset = branch.FogPreAddendStateOffset;
    resolved.FogColorAddendStateOffset = branch.FogColorAddendStateOffset;
    resolved.FinalFogColorOutputOffset = branch.FinalFogColorOutputOffset;
    resolved.FinalFogColorPlayOffset = branch.FinalFogColorPlayOffset;
    resolved.RuntimeFogDistanceContractResolved = branch.RuntimeFogDistanceContractResolved;
    resolved.RuntimeFogDistanceAddendsResolved = branch.RuntimeFogDistanceAddendsResolved;
    resolved.RuntimeCameraFarUsedForRender = branch.RuntimeCameraFarUsedForRender;
    resolved.RuntimeFogDistancesUsedForRender = branch.RuntimeFogDistancesUsedForRender;
    resolved.RuntimeFogDistanceSource = branch.RuntimeFogDistanceSource;
    resolved.RuntimeFogDistanceStatus = branch.RuntimeFogDistanceStatus;
    resolved.CameraFarRecordOffset = branch.CameraFarRecordOffset;
    resolved.FogFarRecordOffset = branch.FogFarRecordOffset;
    resolved.FogNearRecordOffset = branch.FogNearRecordOffset;
    resolved.CameraFarOutputOffset = branch.CameraFarOutputOffset;
    resolved.FogFarOutputOffset = branch.FogFarOutputOffset;
    resolved.FogNearOutputOffset = branch.FogNearOutputOffset;
    resolved.CameraFarPlayOffset = branch.CameraFarPlayOffset;
    resolved.FogFarPlayOffset = branch.FogFarPlayOffset;
    resolved.FogNearPlayOffset = branch.FogNearPlayOffset;
    resolved.FogNearAddendStateOffset = branch.FogNearAddendStateOffset;
    resolved.FogFarAddendStateOffset = branch.FogFarAddendStateOffset;
    resolved.CameraFar = branch.CameraFar;
    resolved.FogFarPreAddend = branch.FogFarPreAddend;
    resolved.FogFarAddend = branch.FogFarAddend;
    resolved.FogFar = branch.FogFar;
    resolved.FogNearInterpolated = branch.FogNearInterpolated;
    resolved.FogNearPreAddend = branch.FogNearPreAddend;
    resolved.FogNearAddend = branch.FogNearAddend;
    resolved.FogNear = branch.FogNear;
}

bool ResolveRuntimeLightFieldsFromRecords(Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
                                          const Oot3dNativeDemoPicaLightSettingsRecord& from,
                                          const Oot3dNativeDemoPicaLightSettingsRecord& to,
                                          double weight) {
    if (!HasNativeRuntimeEnvironmentRecord(from) || !HasNativeRuntimeEnvironmentRecord(to)) {
        return false;
    }

    const auto& layout = NativeZsiLightSettingsRecordLayout();
    resolved.Light0Vector = BlendRuntimeVectorBytes(from, to, layout.RuntimeLight0DirectionOffset, weight);
    resolved.Light1Vector = BlendRuntimeVectorBytes(from, to, layout.RuntimeLight1DirectionOffset, weight);
    PopulateRuntimeLightFinalColorContract(
        resolved,
        BlendRuntimeColorBytes(from, to, layout.RuntimeAmbientColorOffset, weight),
        BlendRuntimeColorBytes(from, to, layout.RuntimeLight0ColorOffset, weight),
        BlendRuntimeColorBytes(from, to, layout.RuntimeLight1ColorOffset, weight),
        { 0, 0, 0 },
        { 0, 0, 0 },
        false,
        false);
    PopulateRuntimeFogFinalColorContract(
        resolved, BlendRuntimeColorBytes(from, to, layout.RuntimeFogColorOffset, weight),
        { 0, 0, 0 }, false);
    PopulateRuntimeFogDistanceContract(
        resolved,
        NativeLerpScalar(from.FloatParam0, to.FloatParam0, weight),
        NativeLerpScalar(from.FloatParam1, to.FloatParam1, weight),
        NativeLerpScalar(RuntimePackedFogNear(from), RuntimePackedFogNear(to), weight));
    return true;
}

const Oot3dNativeDemoLightSettingsTransitionEntry* FindTransitionEntryForAngle(
    const Oot3dNativeDemoLightSettingsTransitionMode& mode,
    uint16_t activeAngle) {
    for (const auto& entry : mode.Entries) {
        if (entry.StartAngle <= activeAngle &&
            (activeAngle < entry.EndAngle || entry.EndAngle == 0xFFFF)) {
            return &entry;
        }
    }
    return nullptr;
}

double RuntimeTransitionAngleWeight(const Oot3dNativeDemoLightSettingsTransitionEntry& entry,
                                    uint16_t activeAngle) {
    const int span = static_cast<int>(entry.EndAngle) - static_cast<int>(entry.StartAngle);
    if (span <= 0) {
        return 0.0;
    }
    return Clamp01(1.0 - static_cast<double>(static_cast<int>(entry.EndAngle) -
                             static_cast<int>(activeAngle)) /
                             static_cast<double>(span));
}

int NativeU16AngleFromS16Double(double value) {
    const int rounded = std::clamp(static_cast<int>(std::round(value)), -32768, 32767);
    return rounded & 0xFFFF;
}

bool ResolveRuntimeLightFieldsForTransitionMode(
    const Oot3dNativeDemoPicaLightingState& lighting,
    const Oot3dNativeDemoLightSettingsTransitionMode& mode,
    uint16_t activeAngle,
    int activeSetupIndex,
    Oot3dNativePicaResolvedRuntimeLightSetting& resolved,
    bool writeTargetMetadata) {
    const auto* entry = FindTransitionEntryForAngle(mode, activeAngle);
    if (entry == nullptr) {
        return false;
    }
    const auto* from = FindPicaLightingRecordBySetupAndIndex(
        lighting, activeSetupIndex, entry->FromLightSettingIndex);
    const auto* to = FindPicaLightingRecordBySetupAndIndex(
        lighting, activeSetupIndex, entry->ToLightSettingIndex);
    if (from == nullptr || to == nullptr) {
        return false;
    }

    const double angleWeight = RuntimeTransitionAngleWeight(*entry, activeAngle);
    Oot3dNativePicaResolvedRuntimeLightSetting branch;
    if (!ResolveRuntimeLightFieldsFromRecords(branch, *from, *to, angleWeight)) {
        return false;
    }

    resolved.Light0Vector = branch.Light0Vector;
    resolved.Light1Vector = branch.Light1Vector;
    CopyRuntimeFinalColorContractFields(resolved, branch);
    if (!writeTargetMetadata) {
        resolved.CurrentEntryIndex = entry->EntryIndex;
        resolved.CurrentFromLightSettingIndex = entry->FromLightSettingIndex;
        resolved.CurrentToLightSettingIndex = entry->ToLightSettingIndex;
        resolved.AngleWeight = angleWeight;
    } else {
        resolved.TargetEntryIndex = entry->EntryIndex;
        resolved.TargetFromLightSettingIndex = entry->FromLightSettingIndex;
        resolved.TargetToLightSettingIndex = entry->ToLightSettingIndex;
    }
    return true;
}

Oot3dNativePicaResolvedRuntimeLightSetting BuildResolvedRuntimeLightSetting(
    const Oot3dNativeDemoScene& scene,
    int defaultActiveAngle,
    int defaultCurrentMode,
    int defaultTargetMode,
    bool defaultModeBlendActive,
    double defaultModeBlendWeight,
    const Oot3dNativeDemoPicaLightSettingsRecord& selectedRecord,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    Oot3dNativePicaResolvedRuntimeLightSetting resolved;
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    resolved.SourceKind = "code_bin_0045dd50_resolved_runtime_environment_light_setting";
    resolved.ActiveSetupIndex = scene.NativePicaLighting.ActiveSetupIndex;
    resolved.TransitionTableAvailable = scene.NativePicaLighting.NativeRuntimeTransitionTableAvailable;
    resolved.ActiveAngleSource = layout.RuntimeTransitionActiveAngleSource;
    resolved.ModeStateSource = layout.RuntimeTransitionModeStateSource;
    resolved.ActiveAngleWorkingStateAddress = layout.RuntimeTransitionActiveAngleWorkingStateAddress;
    resolved.ActiveAngleWorkingHalfwordOffset = layout.RuntimeTransitionActiveAngleWorkingHalfwordOffset;
    resolved.ActiveAngleOutputStateAddress = layout.RuntimeTransitionActiveAngleOutputStateAddress;
    resolved.ActiveAngleOutputHalfwordOffset = layout.RuntimeTransitionActiveAngleOutputHalfwordOffset;
    resolved.ModeStateBasePlayOffset = layout.RuntimeTransitionModeStateBasePlayOffset;
    resolved.ModeCurrentRelativeOffset = layout.RuntimeTransitionModeCurrentRelativeOffset;
    resolved.ModeTargetRelativeOffset = layout.RuntimeTransitionModeTargetRelativeOffset;
    resolved.ModeBlendActiveRelativeOffset = layout.RuntimeTransitionModeBlendActiveRelativeOffset;
    resolved.ModeBlendRemainingHalfwordRelativeOffset =
        layout.RuntimeTransitionModeBlendRemainingHalfwordRelativeOffset;
    resolved.ModeBlendDurationHalfwordRelativeOffset =
        layout.RuntimeTransitionModeBlendDurationHalfwordRelativeOffset;

    int activeAngle = defaultActiveAngle;
    int currentMode = defaultCurrentMode;
    int targetMode = defaultTargetMode;
    bool modeBlendActive = defaultModeBlendActive;
    double modeBlendWeight = Clamp01(defaultModeBlendWeight);
    bool activeAngleBootstrappedFromPlayerStart = false;
    bool modeStateBootstrappedFromCodeBinFallback = false;
    if (runtimeEnvironment != nullptr && runtimeEnvironment->TimeResolved) {
        activeAngle = static_cast<int>(runtimeEnvironment->DayTime);
        resolved.ActiveAngleSource +=
            "; oot3d_cutscene_settime_day_time_playstate_input";
    } else if (activeAngle < 0 && scene.PlayerStart.Valid) {
        activeAngle = NativeU16AngleFromS16Double(scene.PlayerStart.Rotation.Y);
        activeAngleBootstrappedFromPlayerStart = true;
        resolved.ActiveAngleSource +=
            "; standalone_playstate_bootstrap_from_native_player_actor_start_shape_yaw_rotation_y";
    }
    if (runtimeEnvironment != nullptr && runtimeEnvironment->LightModeResolved) {
        currentMode = runtimeEnvironment->LightModeCurrent;
        targetMode = runtimeEnvironment->LightModeTarget >= 0
                         ? runtimeEnvironment->LightModeTarget
                         : runtimeEnvironment->LightModeCurrent;
        modeBlendActive = runtimeEnvironment->LightModeBlendActive;
        modeBlendWeight = Clamp01(runtimeEnvironment->LightModeBlendWeight);
        resolved.ModeStateSource +=
            "; oot3d_cutscene_misc_action_light_mode_playstate_input";
        if (!runtimeEnvironment->LightModeSourceKind.empty()) {
            resolved.ModeStateSource += "; ";
            resolved.ModeStateSource += runtimeEnvironment->LightModeSourceKind;
        }
    }
    if (currentMode < 0 &&
        scene.NativePicaLighting.NativeRuntimeTransitionGlobalFallbackStateAvailable &&
        scene.NativePicaLighting.NativeRuntimeTransitionGlobalFallbackMode >= 0) {
        currentMode = scene.NativePicaLighting.NativeRuntimeTransitionGlobalFallbackMode;
        modeStateBootstrappedFromCodeBinFallback = true;
        resolved.ModeStateSource +=
            "; standalone_playstate_bootstrap_from_code_bin_00531eb4_plus_native_fallback_mode_offset";
    }
    if (targetMode < 0) {
        targetMode = currentMode;
    }
    resolved.ActiveAngle = activeAngle;
    resolved.CurrentMode = currentMode;
    resolved.TargetMode = targetMode;
    resolved.ActiveAngleBootstrappedFromPlayerStart = activeAngleBootstrappedFromPlayerStart;
    resolved.ModeStateBootstrappedFromCodeBinFallback = modeStateBootstrappedFromCodeBinFallback;

    if (runtimeEnvironment != nullptr && runtimeEnvironment->LightSettingResolved &&
        runtimeEnvironment->LightSettingTarget >= 0 &&
        runtimeEnvironment->LightSettingTarget !=
            static_cast<int>(NativeKankyoRuntimeBridgeLayout()
                                 .EnvironmentLightSettingState.TargetInvalidValue)) {
        const int targetSetupIndex = runtimeEnvironment->LightSettingSetupIndex >= 0
                                         ? runtimeEnvironment->LightSettingSetupIndex
                                         : resolved.ActiveSetupIndex;
        resolved.TargetSetupIndex = targetSetupIndex;
        resolved.DirectTargetScenePath = runtimeEnvironment->LightSettingScenePath;
        resolved.DirectTargetSceneMatchesActiveScene = true;
        const Oot3dNativeDemoPicaLightingState* targetLighting = &scene.NativePicaLighting;
        Oot3dNativeDemoPicaLightingState decodedTargetLighting;
        if (!runtimeEnvironment->LightSettingScenePath.empty()) {
            const auto targetSceneZsiPath =
                NativeRenderResolveSceneZsiPath(scene, runtimeEnvironment->LightSettingScenePath);
            resolved.DirectTargetSceneZsiPath = targetSceneZsiPath;
            resolved.DirectTargetSceneMatchesActiveScene =
                NativeRenderScenePathMatches(targetSceneZsiPath, scene.CollisionZsiPath);
            if (resolved.DirectTargetSceneMatchesActiveScene) {
                resolved.DirectTargetSceneSourceStatus =
                    "runtime_light_setting_scene_path_matches_active_scene";
            } else if (std::filesystem::is_regular_file(targetSceneZsiPath)) {
                decodedTargetLighting =
                    DecodeOot3dNativeDemoPicaLightingStateForRuntime(targetSceneZsiPath, {}, targetSetupIndex);
                resolved.DirectTargetSceneLightingDecoded = decodedTargetLighting.Available;
                if (decodedTargetLighting.Available) {
                    targetLighting = &decodedTargetLighting;
                    resolved.DirectTargetSceneSourceStatus =
                        "decoded_runtime_light_setting_target_scene_zsi_from_native_scene_path";
                } else {
                    resolved.DirectTargetSceneSourceStatus =
                        "runtime_light_setting_target_scene_zsi_decoded_no_light_settings";
                }
            } else {
                resolved.DirectTargetSceneSourceStatus =
                    "runtime_light_setting_target_scene_zsi_not_found";
            }
        }
        const auto* targetRecord = FindPicaLightingRecordBySetupAndIndex(
            *targetLighting,
            targetSetupIndex,
            runtimeEnvironment->LightSettingTarget);
        if (targetRecord != nullptr && HasNativeRuntimeEnvironmentRecord(*targetRecord) &&
            ResolveRuntimeLightFieldsFromRecords(resolved, *targetRecord, *targetRecord, 1.0)) {
            resolved.Available = true;
            resolved.UsedForRender = true;
            resolved.ActiveSetupIndex = targetLighting->ActiveSetupIndex;
            resolved.DirectTargetRecordBranchApplied = true;
            resolved.TransitionTableBranchSupported = resolved.TransitionTableAvailable;
            resolved.Branch = "direct_target_light_setting_record";
            resolved.SourceStatus =
                "native_cutscene_cs_cmd_set_lighting_target_play_0x3237_selected_runtime_light_setting_record";
            if (!runtimeEnvironment->LightSettingSourceStatus.empty()) {
                resolved.SourceStatus += "; ";
                resolved.SourceStatus += runtimeEnvironment->LightSettingSourceStatus;
            }
            resolved.TargetLightSettingRawIndex = runtimeEnvironment->LightSettingRawIndex;
            resolved.TargetLightSettingIndex = runtimeEnvironment->LightSettingTarget;
            resolved.CurrentRecordIndex = targetRecord->Index;
            resolved.PreviousRecordIndex = targetRecord->Index;
            resolved.TargetRecordIndex = targetRecord->Index;
            resolved.CurrentRecordOffset = targetRecord->Offset;
            resolved.PreviousRecordOffset = targetRecord->Offset;
            resolved.TargetRecordOffset = targetRecord->Offset;
            resolved.DirectBlendWeight = 1.0;
            ApplyRuntimeEnvironmentColorAddends(resolved, runtimeEnvironment);
            return resolved;
        }
    }

    if (resolved.TransitionTableAvailable && activeAngle >= 0 && currentMode >= 0 &&
        static_cast<size_t>(currentMode) < scene.NativePicaLighting.NativeRuntimeTransitionModes.size() &&
        targetMode >= 0 &&
        static_cast<size_t>(targetMode) < scene.NativePicaLighting.NativeRuntimeTransitionModes.size()) {
        Oot3dNativePicaResolvedRuntimeLightSetting currentResolved = resolved;
        if (ResolveRuntimeLightFieldsForTransitionMode(
                scene.NativePicaLighting,
                scene.NativePicaLighting.NativeRuntimeTransitionModes[static_cast<size_t>(currentMode)],
                static_cast<uint16_t>(activeAngle), resolved.ActiveSetupIndex, currentResolved, false)) {
            resolved = currentResolved;
            resolved.Available = true;
            resolved.UsedForRender = true;
            resolved.TransitionTableBranchSupported = true;
            resolved.TransitionTableBranchApplied = true;
            resolved.Branch = "transition_table_mode_angle";
            resolved.SourceStatus =
                activeAngleBootstrappedFromPlayerStart || modeStateBootstrappedFromCodeBinFallback
                    ? "native_0045dd50_transition_table_branch_applied_from_code_bin_table_with_standalone_playstate_bootstrap_mode_angle_inputs"
                    : "native_0045dd50_transition_table_branch_applied_from_code_bin_table_and_runtime_mode_angle_inputs";
            resolved.ActiveAngle = activeAngle;
            resolved.CurrentMode = currentMode;
            resolved.TargetMode = targetMode;
            resolved.ActiveAngleBootstrappedFromPlayerStart = activeAngleBootstrappedFromPlayerStart;
            resolved.ModeStateBootstrappedFromCodeBinFallback = modeStateBootstrappedFromCodeBinFallback;
            resolved.ModeBlendActive = modeBlendActive;
            resolved.ModeBlendWeight = modeBlendWeight;

            if (resolved.ModeBlendActive && targetMode != currentMode) {
                Oot3dNativePicaResolvedRuntimeLightSetting targetResolved = resolved;
                if (ResolveRuntimeLightFieldsForTransitionMode(
                        scene.NativePicaLighting,
                        scene.NativePicaLighting.NativeRuntimeTransitionModes[static_cast<size_t>(targetMode)],
                        static_cast<uint16_t>(activeAngle), resolved.ActiveSetupIndex, targetResolved, true)) {
                    PopulateRuntimeLightFinalColorContract(
                        resolved,
                        NativeLerpColor(
                            currentResolved.AmbientPreAddendColor,
                            targetResolved.AmbientPreAddendColor,
                            resolved.ModeBlendWeight),
                        NativeLerpColor(
                            currentResolved.Light0PreAddendColor,
                            targetResolved.Light0PreAddendColor,
                            resolved.ModeBlendWeight),
                        NativeLerpColor(
                            currentResolved.Light1PreAddendColor,
                            targetResolved.Light1PreAddendColor,
                            resolved.ModeBlendWeight),
                        NativeRuntimeLerpColorAddends(
                            currentResolved.AmbientColorAddendI16,
                            targetResolved.AmbientColorAddendI16,
                            resolved.ModeBlendWeight),
                        NativeRuntimeLerpColorAddends(
                            currentResolved.LightColorAddendI16,
                            targetResolved.LightColorAddendI16,
                            resolved.ModeBlendWeight),
                        currentResolved.RuntimeAmbientColorAddendResolved &&
                            targetResolved.RuntimeAmbientColorAddendResolved,
                        currentResolved.RuntimeLightColorAddendResolved &&
                            targetResolved.RuntimeLightColorAddendResolved);
                    PopulateRuntimeFogFinalColorContract(
                        resolved,
                        NativeLerpColor(
                            currentResolved.FogPreAddendColor,
                            targetResolved.FogPreAddendColor,
                            resolved.ModeBlendWeight),
                        NativeRuntimeLerpColorAddends(
                            currentResolved.FogColorAddendI16,
                            targetResolved.FogColorAddendI16,
                            resolved.ModeBlendWeight),
                        currentResolved.RuntimeFogColorAddendResolved &&
                            targetResolved.RuntimeFogColorAddendResolved);
                    PopulateRuntimeFogDistanceContract(
                        resolved,
                        currentResolved.CameraFar,
                        NativeLerpScalar(
                            currentResolved.FogFarPreAddend,
                            targetResolved.FogFarPreAddend,
                            resolved.ModeBlendWeight),
                        NativeLerpScalar(
                            currentResolved.FogNearInterpolated,
                            targetResolved.FogNearInterpolated,
                            resolved.ModeBlendWeight));
                    resolved.Light0Vector = NativeLerpDirection(
                        currentResolved.Light0Vector, targetResolved.Light0Vector,
                        resolved.ModeBlendWeight);
                    resolved.Light1Vector = NativeLerpDirection(
                        currentResolved.Light1Vector, targetResolved.Light1Vector,
                        resolved.ModeBlendWeight);
                    resolved.TargetEntryIndex = targetResolved.TargetEntryIndex;
                    resolved.TargetFromLightSettingIndex = targetResolved.TargetFromLightSettingIndex;
                    resolved.TargetToLightSettingIndex = targetResolved.TargetToLightSettingIndex;
                }
            }
            PopulateRuntimeFogDistanceContract(
                resolved,
                selectedRecord.FloatParam0,
                resolved.FogFarPreAddend,
                resolved.FogNearInterpolated);
            ApplyRuntimeEnvironmentColorAddends(resolved, runtimeEnvironment);
            return resolved;
        }
    }

    if (!HasNativeRuntimeEnvironmentRecord(selectedRecord)) {
        resolved.SourceStatus =
            "native_0045dd50_runtime_record_unavailable_for_selected_light_setting";
        return resolved;
    }

    resolved.Available = true;
    resolved.UsedForRender = true;
    resolved.DirectCurrentRecordBranchApplied = true;
    resolved.TransitionTableBranchSupported = resolved.TransitionTableAvailable;
    resolved.Branch = "direct_current_record";
    resolved.SourceStatus =
        "native_0045dd50_direct_current_record_branch_structured_from_selected_runtime_light_setting; "
        "transition_table_branch_waiting_for_native_play_mode_angle_state";
    resolved.CurrentRecordIndex = selectedRecord.Index;
    resolved.PreviousRecordIndex = selectedRecord.Index;
    resolved.CurrentRecordOffset = selectedRecord.Offset;
    resolved.PreviousRecordOffset = selectedRecord.Offset;
    resolved.DirectBlendWeight = 1.0;
    resolved.Light0Vector = RuntimeVectorFromRecordBytes(
        selectedRecord, layout.RuntimeLight0DirectionOffset);
    resolved.Light1Vector = RuntimeVectorFromRecordBytes(
        selectedRecord, layout.RuntimeLight1DirectionOffset);
    PopulateRuntimeLightFinalColorContract(
        resolved,
        RuntimeColorFromRecordBytes(selectedRecord, layout.RuntimeAmbientColorOffset),
        RuntimeColorFromRecordBytes(selectedRecord, layout.RuntimeLight0ColorOffset),
        RuntimeColorFromRecordBytes(selectedRecord, layout.RuntimeLight1ColorOffset),
        { 0, 0, 0 },
        { 0, 0, 0 },
        false,
        false);
    PopulateRuntimeFogFinalColorContract(
        resolved,
        RuntimeColorFromRecordBytes(selectedRecord, layout.RuntimeFogColorOffset),
        { 0, 0, 0 }, false);
    PopulateRuntimeFogDistanceContract(
        resolved,
        selectedRecord.FloatParam0,
        selectedRecord.FloatParam1,
        RuntimePackedFogNear(selectedRecord));
    ApplyRuntimeEnvironmentColorAddends(resolved, runtimeEnvironment);
    return resolved;
}

bool HasCompleteActorVsLightPacketColors(const Oot3dNativeDemoPicaLightSettingsRecord& record) {
    return record.NativeActorVsLightPacketColorCandidateAvailable &&
           record.NativeActorVsAmbientColorCandidateAvailable;
}

struct ActorVsLightPacketColorSelection {
    bool Available = false;
    bool RuntimeTransitionColorBlendApplied = false;
    bool RuntimeTransitionModeColorBlendApplied = false;
    std::string SelectionSource;
    const Oot3dNativeDemoPicaLightSettingsRecord* Record = nullptr;
    const Oot3dNativeDemoPicaLightSettingsRecord* FromRecord = nullptr;
    const Oot3dNativeDemoPicaLightSettingsRecord* ToRecord = nullptr;
    const Oot3dNativeDemoPicaLightSettingsRecord* TargetFromRecord = nullptr;
    const Oot3dNativeDemoPicaLightSettingsRecord* TargetToRecord = nullptr;
    int RecordIndex = -1;
    int RecordOffset = -1;
    int FromRecordIndex = -1;
    int ToRecordIndex = -1;
    int TargetFromRecordIndex = -1;
    int TargetToRecordIndex = -1;
    double AngleWeight = 0.0;
    double ModeWeight = 0.0;
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse1Color = { 0, 0, 0, 255 };
    bool PicaFogColorAvailable = false;
    ColorRgba8 PicaFogColor = { 0, 0, 0, 255 };
};

ActorVsLightPacketColorSelection ActorVsLightPacketColorSelectionFromRecord(
    const Oot3dNativeDemoPicaLightSettingsRecord& record) {
    ActorVsLightPacketColorSelection selection;
    if (!HasCompleteActorVsLightPacketColors(record)) {
        return selection;
    }
    selection.Available = true;
    selection.Record = &record;
    selection.RecordIndex = record.Index;
    selection.RecordOffset = record.Offset;
    selection.AmbientColor = record.NativeActorVsAmbientColor;
    selection.Diffuse0Color = record.NativeActorVsDiffuse0Color;
    selection.Diffuse1Color = record.NativeActorVsDiffuse1Color;
    return selection;
}

bool ActorVsLightPacketRecordHasPicaFogColor(
    const Oot3dNativeDemoPicaLightSettingsRecord& record,
    const NativeZsiLightSettingsRecordContract& layout) {
    return CanReadRecordBytes(record, layout.ActorPacketPicaFogColorOffset,
                              layout.RuntimeColorComponentCount);
}

void ApplyActorVsPicaFogColorToSelection(
    ActorVsLightPacketColorSelection& selection,
    const NativeZsiLightSettingsRecordContract& layout) {
    if (selection.FromRecord != nullptr && selection.ToRecord != nullptr &&
        ActorVsLightPacketRecordHasPicaFogColor(*selection.FromRecord, layout) &&
        ActorVsLightPacketRecordHasPicaFogColor(*selection.ToRecord, layout)) {
        selection.PicaFogColorAvailable = true;
        selection.PicaFogColor = BlendRuntimeColorBytes(
            *selection.FromRecord, *selection.ToRecord,
            static_cast<int>(layout.ActorPacketPicaFogColorOffset), selection.AngleWeight);
        return;
    }
    if (selection.Record != nullptr &&
        ActorVsLightPacketRecordHasPicaFogColor(*selection.Record, layout)) {
        selection.PicaFogColorAvailable = true;
        selection.PicaFogColor = RuntimeColorFromRecordBytes(
            *selection.Record, layout.ActorPacketPicaFogColorOffset);
    }
}

bool ResolveActorVsLightPacketColorsForTransitionMode(
    const Oot3dNativeDemoPicaLightingState& lighting,
    const Oot3dNativeDemoLightSettingsTransitionMode& mode,
    uint16_t activeAngle,
    int activeSetupIndex,
    uint32_t recordIndexDelta,
    ActorVsLightPacketColorSelection& selection,
    bool writeTargetMetadata) {
    const auto* entry = FindTransitionEntryForAngle(mode, activeAngle);
    if (entry == nullptr) {
        return false;
    }
    const auto* fromRecord = FindPicaLightingRecordBySetupAndIndex(
        lighting, activeSetupIndex,
        static_cast<int>(entry->FromLightSettingIndex + recordIndexDelta));
    const auto* toRecord = FindPicaLightingRecordBySetupAndIndex(
        lighting, activeSetupIndex,
        static_cast<int>(entry->ToLightSettingIndex + recordIndexDelta));
    if (fromRecord == nullptr || toRecord == nullptr ||
        !HasCompleteActorVsLightPacketColors(*fromRecord) ||
        !HasCompleteActorVsLightPacketColors(*toRecord)) {
        return false;
    }

    const double angleWeight = RuntimeTransitionAngleWeight(*entry, activeAngle);
    selection.Available = true;
    selection.RuntimeTransitionColorBlendApplied = true;
    selection.Record = fromRecord;
    selection.RecordIndex = fromRecord->Index;
    selection.RecordOffset = fromRecord->Offset;
    selection.AmbientColor = NativeLerpColor(
        fromRecord->NativeActorVsAmbientColor, toRecord->NativeActorVsAmbientColor,
        angleWeight);
    selection.Diffuse0Color = NativeLerpColor(
        fromRecord->NativeActorVsDiffuse0Color, toRecord->NativeActorVsDiffuse0Color,
        angleWeight);
    selection.Diffuse1Color = NativeLerpColor(
        fromRecord->NativeActorVsDiffuse1Color, toRecord->NativeActorVsDiffuse1Color,
        angleWeight);
    selection.AngleWeight = angleWeight;
    if (!writeTargetMetadata) {
        selection.FromRecord = fromRecord;
        selection.ToRecord = toRecord;
        selection.FromRecordIndex = fromRecord->Index;
        selection.ToRecordIndex = toRecord->Index;
    } else {
        selection.TargetFromRecord = fromRecord;
        selection.TargetToRecord = toRecord;
        selection.TargetFromRecordIndex = fromRecord->Index;
        selection.TargetToRecordIndex = toRecord->Index;
    }
    return true;
}

ActorVsLightPacketColorSelection ResolveActorVsLightPacketColorSelection(
    const Oot3dNativeDemoPicaLightingState& lighting,
    const Oot3dNativeDemoPicaLightSettingsRecord& environmentRecord,
    const Oot3dNativePicaResolvedRuntimeLightSetting& resolvedRuntimeLightSetting,
    const NativeZsiLightSettingsRecordContract& layout) {
    const uint32_t recordIndexDelta = layout.ActorPacketRecordIndexDelta;
    const auto* packetRecord = recordIndexDelta > 0
        ? FindPicaLightingRecordBySetupAndIndex(
              lighting, environmentRecord.SetupIndex,
              environmentRecord.Index + static_cast<int>(recordIndexDelta))
        : nullptr;

    ActorVsLightPacketColorSelection fallback;
    if (packetRecord != nullptr) {
        fallback = ActorVsLightPacketColorSelectionFromRecord(*packetRecord);
        fallback.SelectionSource = layout.ActorPacketRecordSelectionSource;
        if (!fallback.Available) {
            fallback.Record = packetRecord;
            fallback.RecordIndex = packetRecord->Index;
            fallback.RecordOffset = packetRecord->Offset;
        }
    }
    if (!fallback.Available && HasCompleteActorVsLightPacketColors(environmentRecord)) {
        fallback = ActorVsLightPacketColorSelectionFromRecord(environmentRecord);
        fallback.SelectionSource = "selected_record_complete_actor_vs_packet_fields";
    }

    if (resolvedRuntimeLightSetting.DirectTargetRecordBranchApplied &&
        resolvedRuntimeLightSetting.DirectTargetSceneMatchesActiveScene &&
        resolvedRuntimeLightSetting.TargetLightSettingIndex >= 0) {
        const int targetSetupIndex = resolvedRuntimeLightSetting.TargetSetupIndex >= 0
                                         ? resolvedRuntimeLightSetting.TargetSetupIndex
                                         : resolvedRuntimeLightSetting.ActiveSetupIndex;
        const auto* targetRecord = FindPicaLightingRecordBySetupAndIndex(
            lighting, targetSetupIndex,
            resolvedRuntimeLightSetting.TargetLightSettingIndex + static_cast<int>(recordIndexDelta));
        if (targetRecord != nullptr) {
            auto selection = ActorVsLightPacketColorSelectionFromRecord(*targetRecord);
            if (selection.Available) {
                selection.SelectionSource =
                    "selected_0045dd50_direct_target_light_setting_actor_vs_record_plus_delta";
                ApplyActorVsPicaFogColorToSelection(selection, layout);
                return selection;
            }
        }
    }

    if (resolvedRuntimeLightSetting.TransitionTableBranchApplied &&
        resolvedRuntimeLightSetting.ActiveAngle >= 0 &&
        resolvedRuntimeLightSetting.CurrentMode >= 0 &&
        static_cast<size_t>(resolvedRuntimeLightSetting.CurrentMode) <
            lighting.NativeRuntimeTransitionModes.size() &&
        resolvedRuntimeLightSetting.TargetMode >= 0 &&
        static_cast<size_t>(resolvedRuntimeLightSetting.TargetMode) <
            lighting.NativeRuntimeTransitionModes.size()) {
        ActorVsLightPacketColorSelection currentSelection;
        if (ResolveActorVsLightPacketColorsForTransitionMode(
                lighting,
                lighting.NativeRuntimeTransitionModes[
                    static_cast<size_t>(resolvedRuntimeLightSetting.CurrentMode)],
                static_cast<uint16_t>(resolvedRuntimeLightSetting.ActiveAngle),
                resolvedRuntimeLightSetting.ActiveSetupIndex, recordIndexDelta, currentSelection,
                false)) {
            ApplyActorVsPicaFogColorToSelection(currentSelection, layout);
            auto resolvedSelection = currentSelection;
            resolvedSelection.SelectionSource =
                "selected_0045dd50_runtime_light_setting_compact_actor_vs_payload_transition_record_plus_delta";
            if (resolvedRuntimeLightSetting.ModeBlendActive &&
                resolvedRuntimeLightSetting.TargetMode != resolvedRuntimeLightSetting.CurrentMode) {
                ActorVsLightPacketColorSelection targetSelection;
                if (ResolveActorVsLightPacketColorsForTransitionMode(
                        lighting,
                        lighting.NativeRuntimeTransitionModes[
                            static_cast<size_t>(resolvedRuntimeLightSetting.TargetMode)],
                        static_cast<uint16_t>(resolvedRuntimeLightSetting.ActiveAngle),
                        resolvedRuntimeLightSetting.ActiveSetupIndex, recordIndexDelta,
                        targetSelection, true)) {
                    ApplyActorVsPicaFogColorToSelection(targetSelection, layout);
                    resolvedSelection.AmbientColor = NativeLerpColor(
                        currentSelection.AmbientColor, targetSelection.AmbientColor,
                        resolvedRuntimeLightSetting.ModeBlendWeight);
                    resolvedSelection.Diffuse0Color = NativeLerpColor(
                        currentSelection.Diffuse0Color, targetSelection.Diffuse0Color,
                        resolvedRuntimeLightSetting.ModeBlendWeight);
                    resolvedSelection.Diffuse1Color = NativeLerpColor(
                        currentSelection.Diffuse1Color, targetSelection.Diffuse1Color,
                        resolvedRuntimeLightSetting.ModeBlendWeight);
                    resolvedSelection.RuntimeTransitionModeColorBlendApplied = true;
                    resolvedSelection.ModeWeight = resolvedRuntimeLightSetting.ModeBlendWeight;
                    resolvedSelection.TargetFromRecord = targetSelection.TargetFromRecord;
                    resolvedSelection.TargetToRecord = targetSelection.TargetToRecord;
                    resolvedSelection.TargetFromRecordIndex = targetSelection.TargetFromRecordIndex;
                    resolvedSelection.TargetToRecordIndex = targetSelection.TargetToRecordIndex;
                    if (currentSelection.PicaFogColorAvailable &&
                        targetSelection.PicaFogColorAvailable) {
                        resolvedSelection.PicaFogColorAvailable = true;
                        resolvedSelection.PicaFogColor = NativeLerpColor(
                            currentSelection.PicaFogColor, targetSelection.PicaFogColor,
                            resolvedRuntimeLightSetting.ModeBlendWeight);
                    }
                    resolvedSelection.SelectionSource += "_with_mode_blend";
                }
            }
            return resolvedSelection;
        }
    }

    ApplyActorVsPicaFogColorToSelection(fallback, layout);
    return fallback;
}

Oot3dNativePicaActorVsLightPacketState BuildActorVsLightPacketStateFromRecord(
    const Oot3dNativeDemoPicaLightingState& lighting,
    const Oot3dNativeDemoPicaLightSettingsRecord& environmentRecord,
    const Oot3dNativePicaResolvedRuntimeLightSetting& resolvedRuntimeLightSetting) {
    Oot3dNativePicaActorVsLightPacketState packet;
    const auto& contract = NativeKankyoRuntimeBridgeLayout();
    const auto& layout = contract.ZsiLightSettingsRecord;
    packet.SourceKind = "oot3d_0045dd50_runtime_actor_vs_compact_payload";
    packet.SourceStatus =
        "native_0045dd50_runtime_actor_vs_compact_directions_and_resolved_environment_rgb; "
        "legacy_zsi_adjacent_record_candidate_used_only_when_compact_payload_unavailable";
    packet.PacketPrepAddress = contract.PacketPrep.FunctionAddress;
    packet.RuntimeLightPacketPackAddress = contract.RuntimeLightPacketPack.FunctionAddress;
    packet.RuntimeFinalPacketEmitterResolved =
        contract.RuntimeLightPacketPack.FinalUploadRecordPacketEmitterResolved;
    packet.RuntimeFinalPacketEmitterAddress =
        contract.RuntimeLightPacketPack.FinalUploadSlotPacketEmitterAddress;
    packet.CompactPayloadProducerAddress =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadProducerAddress;
    packet.CompactPayloadConsumerHandlerAddress =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadConsumerHandlerAddress;
    packet.RuntimeFinalPacketWordCount = contract.RuntimeLightPacketPack.FinalUploadPacketWordCount;
    packet.RuntimeFinalPacketRegisterBase = contract.RuntimeLightPacketPack.FinalUploadPacketRegisterBase;
    packet.SlotCount = contract.PacketPrep.SlotCount;
    packet.SlotStrideBytes = contract.PacketPrep.SlotStrideBytes;
    packet.CompactPayloadSlotCount =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotCount;
    packet.CompactPayloadSlotStrideBytes =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotStrideBytes;
    packet.CompactPayloadSlotOffsets =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotOffsets;
    packet.CompactPayloadDirectionOffset =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionOffset;
    packet.CompactPayloadColorOffset =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorOffset;
    packet.CompactPayloadSizeBytes =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSizeBytes;
    packet.CompactPayloadDirectionSourceOffsets =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSourceOffsets;
    packet.CompactPayloadColorSourceOffsets =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSourceOffsets;
    packet.CompactPayloadDirectionAngleBias =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionAngleBias;
    packet.CompactPayloadDirectionScales = {
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSinScaleX,
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleY,
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleZ,
    };
    packet.CompactPayloadSource =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSource;
    packet.CompactPayloadDirectionSource =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSource;
    packet.CompactPayloadColorSource =
        contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSource;
    packet.SourceColorPayloadOffsets = contract.RuntimeLightPacketPack.SourceColorPayloadOffsets;
    packet.RecordSelectionIndexDelta = contract.ZsiLightSettingsRecord.ActorPacketRecordIndexDelta;
    packet.VectorLayoutResolved = true;
    packet.VectorLayoutSource =
        "codebin_0045dd50_compact_payload_to_00253a4c_record_to_003130a4_packet_prep";
    packet.SourceVectorOffsets = contract.PacketPrep.SourceVectorOffsets;
    packet.EnableIntensityOffset = contract.PacketPrep.EnableIntensityOffset;
    packet.PreparedVectorOffsets = contract.PacketPrep.PreparedVectorOffsets;
    packet.PreparedIntensityOffset = contract.PacketPrep.PreparedIntensityOffset;
    packet.RuntimeSourceVectorStaticUpdateAddress = contract.RuntimeSourceVector.StaticUpdateAddress;
    packet.RuntimeSourceVectorDynamicSubmitCallbackAddress =
        contract.RuntimeSourceVector.DynamicSubmitCallbackAddress;
    packet.RuntimeSourceVectorDynamicTransformUpdateSkipFlagMask =
        contract.RuntimeSourceVector.DynamicTransformUpdateSkipFlagMask;
    packet.RuntimeSourceVectorStaticUpdateSkipFlagMask =
        contract.RuntimeSourceVector.StaticUpdateSkipFlagMask;
    packet.RuntimeSourceVectorCommittedCopySkipFlagMask =
        contract.RuntimeSourceVector.CommittedCopySkipFlagMask;
    packet.RuntimeSourceVectorWorkingBlockOffset = contract.RuntimeSourceVector.WorkingBlockOffset;
    packet.RuntimeSourceVectorBaseVectorOffsets = {
        contract.RuntimeSourceVector.BaseVectorXOffset,
        contract.RuntimeSourceVector.BaseVectorYOffset,
        contract.RuntimeSourceVector.BaseVectorZOffset,
    };
    packet.RuntimeSourceVectorWorkingVectorSeedOffsets =
        contract.RuntimeSourceVector.WorkingVectorSeedOffsets;
    packet.RuntimeSourceVectorWorkingPacketSourceVectorOffsets =
        contract.RuntimeSourceVector.WorkingPacketSourceVectorOffsets;
    packet.RuntimeSourceVectorWorkingBlockWordCount = contract.RuntimeSourceVector.WorkingBlockWordCount;
    packet.RuntimeSourceVectorCommittedBlockOffset = contract.RuntimeSourceVector.CommittedBlockOffset;
    packet.RuntimeSourceVectorCommittedBlockWordCount =
        contract.RuntimeSourceVector.CommittedBlockWordCount;
    packet.RuntimeLightPacketPackSourcePreparedVectorBaseOffset =
        contract.RuntimeLightPacketPack.SourcePreparedVectorBaseOffset;
    packet.RuntimeLightPacketPackSourcePreparedIntensityOffset =
        contract.RuntimeLightPacketPack.SourcePreparedIntensityOffset;
    packet.RuntimeLightPacketPackRequiredPreparedIntensityWord =
        contract.RuntimeLightPacketPack.RequiredPreparedIntensityWord;
    packet.RuntimeLightPacketPackOutputRecordLayoutResolved =
        contract.RuntimeLightPacketPack.RuntimeOutputRecordLayoutResolved;
    packet.RuntimeLightPacketPackOutputFeedsFinalUploadEmitter =
        contract.RuntimeLightPacketPack.RuntimeOutputFeedsFinalUploadEmitter;
    packet.RuntimeLightPacketPackNegatesPreparedVector =
        contract.RuntimeLightPacketPack.RuntimeOutputNegatesPreparedVectorBeforePack;
    packet.RuntimeLightPacketPackOutputFinalRecordDirectionPackedWordOffsets =
        contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordDirectionPackedWordOffsets;
    packet.RuntimeFinalUploadCopiedWordSourceOffsets =
        contract.RuntimeLightPacketPack.FinalUploadCopiedWordSourceOffsets;
    packet.RuntimeFinalUploadCopiedWordPacketWordIndices =
        contract.RuntimeLightPacketPack.FinalUploadCopiedWordPacketWordIndices;
    packet.FinalPacketWriterResolved = contract.ZsiLightSettingsRecord.FinalPacketWriterResolved;
    packet.EnvironmentRecordIndex = environmentRecord.Index;
    packet.EnvironmentRecordOffset = environmentRecord.Offset;
    const auto runtimeVectorBytesAvailable =
        [&](uint32_t offset) {
            return CanReadRuntimeRecordBytes(
                environmentRecord, static_cast<int>(offset),
                static_cast<int>(contract.ZsiLightSettingsRecord.RuntimeDirectionComponentCount));
        };
    const bool runtimeEnvironmentVectorOriginResolved =
        contract.ZsiLightSettingsRecord.RuntimeConsumerResolved &&
        HasNativeRuntimeEnvironmentRecord(environmentRecord) &&
        runtimeVectorBytesAvailable(contract.ZsiLightSettingsRecord.RuntimeLight0DirectionOffset) &&
        runtimeVectorBytesAvailable(contract.ZsiLightSettingsRecord.RuntimeLight1DirectionOffset);
    const bool compactDirectionOriginResolved =
        layout.RuntimeActorVsCompactPayloadResolved &&
        resolvedRuntimeLightSetting.Available &&
        resolvedRuntimeLightSetting.ActiveAngle >= 0;
    packet.VectorOriginResolved =
        compactDirectionOriginResolved || runtimeEnvironmentVectorOriginResolved;
    packet.VectorSourceStatus =
        compactDirectionOriginResolved
            ? "native_0045dd50_runtime_actor_vs_compact_payload_direction_origin_resolved; "
              "active_angle_minus_0x8000_sin_cos_scaled_then_payload_expanded_by_00253a4c"
            : (runtimeEnvironmentVectorOriginResolved
                   ? "native_0045dd50_runtime_environment_signed_vec3_origin_resolved; "
                     "compact_actor_vs_direction_angle_unavailable"
                   : "native_actor_vs_vector_transform_pack_and_final_upload_layout_resolved_from_codebin; "
                     "active_runtime_environment_vector_origin_unresolved");

    const auto colorSelection = ResolveActorVsLightPacketColorSelection(
        lighting, environmentRecord, resolvedRuntimeLightSetting, layout);

    const bool compactPayloadAvailable =
        layout.RuntimeActorVsCompactPayloadResolved &&
        resolvedRuntimeLightSetting.Available &&
        resolvedRuntimeLightSetting.ActiveAngle >= 0;
    if (compactPayloadAvailable) {
        const auto slot0Direction =
            RuntimeActorVsCompactPayloadDirectionS8FromActiveAngle(
                resolvedRuntimeLightSetting.ActiveAngle, layout);
        const auto slot1Direction = NegatedS8Direction(slot0Direction);
        const bool finalColorsAvailable =
            resolvedRuntimeLightSetting.RuntimeFinalAmbientColorUsedForRender &&
            resolvedRuntimeLightSetting.RuntimeFinalLightColorUsedForRender;

        packet.Available = true;
        packet.ColorPacketAvailable = true;
        packet.CompactPayloadSourceResolved = true;
        packet.RecordSelectionIndexDelta = 0;
        packet.ColorSource = finalColorsAvailable
                                 ? "oot3d_0045dd50_runtime_final_environment_rgb"
                                 : "oot3d_0045dd50_runtime_preaddend_environment_rgb";
        packet.AmbientColorSource =
            finalColorsAvailable
                ? "oot3d_0045dd50_final_play_ambient_rgb"
                : "oot3d_0045dd50_preaddend_environment_ambient_rgb";
        packet.Diffuse0ColorSource =
            finalColorsAvailable
                ? "oot3d_0045dd50_final_compact_payload_slot0_rgb"
                : "oot3d_0045dd50_preaddend_environment_light0_rgb";
        packet.Diffuse1ColorSource =
            finalColorsAvailable
                ? "oot3d_0045dd50_final_compact_payload_slot1_rgb"
                : "oot3d_0045dd50_preaddend_environment_light1_rgb";
        packet.SelectionSource =
            "selected_0045dd50_resolved_runtime_environment_output";
        packet.RuntimeTransitionColorBlendApplied =
            resolvedRuntimeLightSetting.TransitionTableBranchApplied &&
            resolvedRuntimeLightSetting.CurrentFromLightSettingIndex !=
                resolvedRuntimeLightSetting.CurrentToLightSettingIndex;
        packet.RuntimeTransitionModeColorBlendApplied =
            resolvedRuntimeLightSetting.ModeBlendActive &&
            resolvedRuntimeLightSetting.CurrentMode != resolvedRuntimeLightSetting.TargetMode;
        packet.RuntimeTransitionColorFromRecordIndex =
            resolvedRuntimeLightSetting.CurrentFromLightSettingIndex;
        packet.RuntimeTransitionColorToRecordIndex =
            resolvedRuntimeLightSetting.CurrentToLightSettingIndex;
        packet.RuntimeTransitionColorTargetFromRecordIndex =
            resolvedRuntimeLightSetting.TargetFromLightSettingIndex;
        packet.RuntimeTransitionColorTargetToRecordIndex =
            resolvedRuntimeLightSetting.TargetToLightSettingIndex;
        packet.RuntimeTransitionColorAngleWeight = resolvedRuntimeLightSetting.AngleWeight;
        packet.RuntimeTransitionColorModeWeight = resolvedRuntimeLightSetting.ModeBlendWeight;
        packet.RecordIndex = resolvedRuntimeLightSetting.CurrentRecordIndex;
        packet.RecordOffset = resolvedRuntimeLightSetting.CurrentRecordOffset;
        packet.CompactPayloadActiveAngle = resolvedRuntimeLightSetting.ActiveAngle;
        packet.AmbientColor = resolvedRuntimeLightSetting.AmbientColor;
        packet.Diffuse0Color = resolvedRuntimeLightSetting.Light0Color;
        packet.Diffuse1Color = resolvedRuntimeLightSetting.Light1Color;
        packet.PicaFogColorAvailable = true;
        packet.PicaFogColorOffset =
            resolvedRuntimeLightSetting.RuntimeFinalFogColorUsedForRender
                ? resolvedRuntimeLightSetting.FinalFogColorPlayOffset
                : layout.RuntimeFogColorOffset;
        packet.PicaFogColorSource =
            resolvedRuntimeLightSetting.RuntimeFinalFogColorUsedForRender
                ? "oot3d_0045dd50_final_play_fog_rgb"
                : "oot3d_0045dd50_preaddend_environment_fog_rgb";
        packet.PicaFogColor = resolvedRuntimeLightSetting.FogColor;
        packet.CompactPayloadSlot0Direction = RuntimeCompactPayloadDirectionFromS8(
            slot0Direction, layout.RuntimeActorVsCompactPayloadDirectionScaleDenominator);
        packet.CompactPayloadSlot1Direction = RuntimeCompactPayloadDirectionFromS8(
            slot1Direction, layout.RuntimeActorVsCompactPayloadDirectionScaleDenominator);
        packet.CompactPayloadSlot0DirectionS8 = slot0Direction;
        packet.CompactPayloadSlot1DirectionS8 = slot1Direction;
        packet.CompactPayloadSlot0Bytes = RuntimeCompactPayloadBytesFromDirectionAndColor(
            slot0Direction, packet.Diffuse0Color);
        packet.CompactPayloadSlot1Bytes = RuntimeCompactPayloadBytesFromDirectionAndColor(
            slot1Direction, packet.Diffuse1Color);
        return packet;
    }

    packet.SourceKind = "oot3d_zsi_light_settings_record_actor_vs_packet_candidate";
    packet.SourceStatus =
        "native_0045dd50_compact_payload_unavailable_for_this_record; "
        "legacy_zsi_record_color_fields_mapped_to_runtime_actor_vs_light_packet; "
        "runtime_source_vector_transform_and_final_packet_emitter_resolved; "
        "zsi_to_packet_buffer_writer_still_pending";
    packet.VectorLayoutSource =
        "codebin_003130a4_packet_prep_and_003f95c8_runtime_source_vector_layout";
    packet.VectorSourceStatus =
        packet.VectorOriginResolved
            ? "native_0045dd50_runtime_environment_signed_vec3_origin_resolved; "
              "final packet writer remains diagnostic and is not required for CPU vertex_hemisphere evaluation"
            : "native_actor_vs_vector_transform_pack_and_final_upload_layout_resolved_from_codebin; "
              "active_runtime_environment_vector_origin_unresolved";

    if (!colorSelection.Available) {
        packet.RecordIndex = colorSelection.RecordIndex;
        packet.RecordOffset = colorSelection.RecordOffset;
        return packet;
    }

    packet.Available = true;
    packet.ColorPacketAvailable = true;
    packet.ColorSource = "oot3d_zsi_light_settings_record_0x1c_actor_vs_color_fields";
    packet.AmbientColorSource =
        "previous_record_rgb_u8_tail_0x1a_0x1b_plus_current_record_rgb_u8_0x00";
    packet.Diffuse0ColorSource = "rgb_u8_offset_0x04";
    packet.Diffuse1ColorSource = "rgb_u8_offset_0x0a";
    packet.SelectionSource = colorSelection.SelectionSource;
    packet.RuntimeTransitionColorBlendApplied =
        colorSelection.RuntimeTransitionColorBlendApplied;
    packet.RuntimeTransitionModeColorBlendApplied =
        colorSelection.RuntimeTransitionModeColorBlendApplied;
    packet.RuntimeTransitionColorFromRecordIndex = colorSelection.FromRecordIndex;
    packet.RuntimeTransitionColorToRecordIndex = colorSelection.ToRecordIndex;
    packet.RuntimeTransitionColorTargetFromRecordIndex = colorSelection.TargetFromRecordIndex;
    packet.RuntimeTransitionColorTargetToRecordIndex = colorSelection.TargetToRecordIndex;
    packet.RuntimeTransitionColorAngleWeight = colorSelection.AngleWeight;
    packet.RuntimeTransitionColorModeWeight = colorSelection.ModeWeight;
    packet.RecordIndex = colorSelection.RecordIndex;
    packet.RecordOffset = colorSelection.RecordOffset;
    packet.AmbientColor = colorSelection.AmbientColor;
    packet.Diffuse0Color = colorSelection.Diffuse0Color;
    packet.Diffuse1Color = colorSelection.Diffuse1Color;
    if (colorSelection.PicaFogColorAvailable) {
        packet.PicaFogColorAvailable = true;
        packet.PicaFogColorOffset = layout.ActorPacketPicaFogColorOffset;
        packet.PicaFogColorSource = layout.ActorPacketPicaFogColorSource;
        packet.PicaFogColor = colorSelection.PicaFogColor;
    }
    return packet;
}

Oot3dNativePicaRuntimeUvTransformState BuildRuntimeUvTransformState(
    const NativePicaLightingRegisterEmitterContract& emitter, bool usedForRender) {
    Oot3dNativePicaRuntimeUvTransformState state;
    state.Available =
        emitter.RuntimeUvTransformBuildFunctionAddress != 0 &&
        emitter.RuntimeUvTransformSourceBuilderAddress != 0 &&
        emitter.RuntimeUvTransformPrimaryUploadHelperAddress != 0 &&
        emitter.RuntimeUvTransformSecondaryUploadHelperAddress != 0;
    if (!state.Available) {
        return state;
    }

    state.UsedForRender = usedForRender;
    state.UploadsNativePicaVshUniforms = emitter.RuntimeUvTransformUploadsNativePicaVshUniforms;
    state.DirectlyWritesPacketPrepSource =
        emitter.RuntimeUvTransformDirectlyWritesPacketPrepSource;
    state.SourceKind = "oot3d_codebin_pica_lighting_runtime_uv_transform_submit_path";
    state.SourceStatus =
        "native_003f9f68_builds_three_12_word_transform_slots_and_uploads_pica_vsh_uniforms; "
        "not_the_003130a4_packet_prep_source_writer";
    state.RuntimeSubmitFunctionAddress = emitter.RuntimeSubmitFunctionAddress;
    state.RuntimeRecordTextureLightFunctionAddress =
        emitter.RuntimeRecordTextureLightFunctionAddress;
    state.RuntimeUvTransformBuildFunctionAddress = emitter.RuntimeUvTransformBuildFunctionAddress;
    state.RuntimeUvTransformSourceBuilderAddress =
        emitter.RuntimeUvTransformSourceBuilderAddress;
    state.RuntimeUvTransformCopyHelperAddress = emitter.RuntimeUvTransformCopyHelperAddress;
    state.RuntimeUvTransformPrimaryUploadHelperAddress =
        emitter.RuntimeUvTransformPrimaryUploadHelperAddress;
    state.RuntimeUvTransformSecondaryUploadHelperAddress =
        emitter.RuntimeUvTransformSecondaryUploadHelperAddress;
    state.SourceCountOffset = emitter.RuntimeUvTransformSourceCountOffset;
    state.SourceRecordBaseOffset = emitter.RuntimeUvTransformSourceRecordBaseOffset;
    state.SourceRecordStrideBytes = emitter.RuntimeUvTransformSourceRecordStrideBytes;
    state.OutputSlotCount = emitter.RuntimeUvTransformOutputSlotCount;
    state.OutputSlotStrideBytes = emitter.RuntimeUvTransformOutputSlotStrideBytes;
    state.OutputWordCount = emitter.RuntimeUvTransformOutputWordCount;
    state.PrimaryUploadRegister = emitter.RuntimeUvTransformPrimaryUploadRegister;
    state.PrimaryUploadWordCount = emitter.RuntimeUvTransformPrimaryUploadWordCount;
    state.SecondaryUploadRegisterBase = emitter.RuntimeUvTransformSecondaryUploadRegisterBase;
    state.SecondaryUploadWordCount = emitter.RuntimeUvTransformSecondaryUploadWordCount;
    state.FirstSlotOverrideOwnerOffset = emitter.RuntimeUvTransformFirstSlotOverrideOwnerOffset;
    state.FirstSlotOverrideGateByteOffset =
        emitter.RuntimeUvTransformFirstSlotOverrideGateByteOffset;
    state.FirstSlotOverrideSourcePointerOffset =
        emitter.RuntimeUvTransformFirstSlotOverrideSourcePointerOffset;
    state.FirstSlotOverridePayloadOffset = emitter.RuntimeUvTransformFirstSlotOverridePayloadOffset;
    return state;
}

Oot3dNativePicaRuntimeSubmitDescriptorState BuildRuntimeSubmitDescriptorState(
    const NativePicaLightingRegisterEmitterContract& emitter, bool usedForRender) {
    Oot3dNativePicaRuntimeSubmitDescriptorState state;
    state.Available =
        emitter.RuntimeSubmitFunctionAddress != 0 &&
        emitter.RuntimeRecordTextureLightFunctionAddress != 0 &&
        emitter.RuntimeSubmitLightRecordStrideBytes != 0 &&
        emitter.RuntimeSubmitLightSlotCount != 0;
    if (!state.Available) {
        return state;
    }

    state.UsedForRender = usedForRender;
    state.DirectlyWritesPacketPrepSource = false;
    state.SourceKind = "oot3d_codebin_pica_lighting_runtime_submit_descriptor";
    state.SourceStatus =
        "native_003f9b5c_descriptor_light_list_build_structured_from_codebin; "
        "final_active_room_descriptor_source_still_pending";
    state.RuntimeSubmitFunctionAddress = emitter.RuntimeSubmitFunctionAddress;
    state.RuntimeRecordTextureLightFunctionAddress =
        emitter.RuntimeRecordTextureLightFunctionAddress;
    state.DescriptorPointerWordIndex = emitter.RuntimeSubmitDescriptorPointerWordIndex;
    state.TextureObjectPointerWordIndex = emitter.RuntimeSubmitTextureObjectPointerWordIndex;
    state.LightRecordTablePointerWordIndex =
        emitter.RuntimeSubmitLightRecordTablePointerWordIndex;
    state.Color0ByteOffset = emitter.RuntimeSubmitColor0ByteOffset;
    state.Color1ByteOffset = emitter.RuntimeSubmitColor1ByteOffset;
    state.ColorComponentCount = emitter.RuntimeSubmitColorComponentCount;
    state.ActiveLightSlotCountOffset = emitter.RuntimeSubmitActiveLightSlotCountOffset;
    state.ActiveLightSlotIndexTableOffset =
        emitter.RuntimeSubmitActiveLightSlotIndexTableOffset;
    state.ActiveLightSlotIndexStrideBytes =
        emitter.RuntimeSubmitActiveLightSlotIndexStrideBytes;
    state.LightRecordStrideBytes = emitter.RuntimeSubmitLightRecordStrideBytes;
    state.LightRecordColorOp0HalfwordOffset =
        emitter.RuntimeSubmitLightRecordColorOp0HalfwordOffset;
    state.LightRecordColorOp1HalfwordOffset =
        emitter.RuntimeSubmitLightRecordColorOp1HalfwordOffset;
    state.LightRecordDisabledColorOpValue =
        emitter.RuntimeSubmitLightRecordDisabledColorOpValue;
    state.LightSlotCount = emitter.RuntimeSubmitLightSlotCount;
    state.TextureLightCountOffset = emitter.RuntimeRecordTextureLightCountOffset;
    state.TextureLightUvSourceCountOffset =
        emitter.RuntimeRecordTextureLightUvSourceCountOffset;
    state.TextureLightTextureRefBaseOffset =
        emitter.RuntimeRecordTextureLightTextureRefBaseOffset;
    state.TextureLightSourceRecordBaseOffset =
        emitter.RuntimeRecordTextureLightSourceRecordBaseOffset;
    state.TextureLightRecordStrideBytes =
        emitter.RuntimeRecordTextureLightRecordStrideBytes;
    state.TextureLightOutputTextureIdHalfwordOffset =
        emitter.RuntimeRecordTextureLightOutputTextureIdHalfwordOffset;
    state.TextureLightOutputSamplerWordBaseOffset =
        emitter.RuntimeRecordTextureLightOutputSamplerWordBaseOffset;
    state.TextureLightOutputModeWordBaseOffset =
        emitter.RuntimeRecordTextureLightOutputModeWordBaseOffset;
    state.TextureLightOutputWordStrideBytes =
        emitter.RuntimeRecordTextureLightOutputWordStrideBytes;
    return state;
}

ColorRgba8 ColorFromPicaByteGroup(const Oot3dNativeDemoPicaByteGroup& group) {
    return {
        static_cast<uint8_t>(std::clamp(group.Raw0, 0, 255)),
        static_cast<uint8_t>(std::clamp(group.Raw1, 0, 255)),
        static_cast<uint8_t>(std::clamp(group.Raw2, 0, 255)),
        static_cast<uint8_t>(std::clamp(group.Raw3, 0, 255)),
    };
}

bool CanReadRecordBytes(const Oot3dNativeDemoPicaLightSettingsRecord& record, int offset, int count) {
    return offset >= 0 && count > 0 &&
           static_cast<size_t>(offset + count) <= record.RawBytes.size();
}

ColorRgba8 ColorFromLightSettingsRecord(const Oot3dNativeDemoPicaLightSettingsRecord& record,
                                        int offset,
                                        const std::string& source) {
    if (source == "env_light_settings_bgr_u8") {
        if (!CanReadRecordBytes(record, offset, 3)) {
            return {};
        }
        return {
            record.RawBytes[static_cast<size_t>(offset + 2)],
            record.RawBytes[static_cast<size_t>(offset + 1)],
            record.RawBytes[static_cast<size_t>(offset + 0)],
            255,
        };
    }
    if (source == "oot3d_runtime_light_settings_rgb_u8") {
        return RuntimeColorFromRecordBytes(record, offset);
    }
    if (source != "env_light_settings_rgb_u8" || !CanReadRecordBytes(record, offset, 3)) {
        return {};
    }
    return {
        record.RawBytes[static_cast<size_t>(offset + 0)],
        record.RawBytes[static_cast<size_t>(offset + 1)],
        record.RawBytes[static_cast<size_t>(offset + 2)],
        255,
    };
}

bool IsSupportedLightSettingsColorSource(const std::string& source) {
    return source == "env_light_settings_rgb_u8" ||
           source == "env_light_settings_bgr_u8" ||
           source == "oot3d_runtime_light_settings_rgb_u8";
}

Vec3f NormalizeVec3(Vec3f value) {
    const double length = std::sqrt(static_cast<double>(value.X) * value.X +
                                    static_cast<double>(value.Y) * value.Y +
                                    static_cast<double>(value.Z) * value.Z);
    if (length <= 0.000001) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return {
        static_cast<float>(value.X / length),
        static_cast<float>(value.Y / length),
        static_cast<float>(value.Z / length),
    };
}

Vec3f NegateVec3(Vec3f value) {
    return { -value.X, -value.Y, -value.Z };
}

Vec3f NativeLerpDirection(Vec3f from, Vec3f to, double weight) {
    const double clamped = Clamp01(weight);
    return NormalizeVec3({
        static_cast<float>(from.X + (to.X - from.X) * clamped),
        static_cast<float>(from.Y + (to.Y - from.Y) * clamped),
        static_cast<float>(from.Z + (to.Z - from.Z) * clamped),
    });
}

Vec3f TransformDirection(const Matrix4f& transform, Vec3f direction) {
    return NormalizeVec3({
        transform.M[0][0] * direction.X + transform.M[0][1] * direction.Y + transform.M[0][2] * direction.Z,
        transform.M[1][0] * direction.X + transform.M[1][1] * direction.Y + transform.M[1][2] * direction.Z,
        transform.M[2][0] * direction.X + transform.M[2][1] * direction.Y + transform.M[2][2] * direction.Z,
    });
}

double Dot(Vec3f lhs, Vec3f rhs) {
    return static_cast<double>(lhs.X) * rhs.X +
           static_cast<double>(lhs.Y) * rhs.Y +
           static_cast<double>(lhs.Z) * rhs.Z;
}

bool CollisionPolygonIndexValid(const Oot3dNativeDemoCollisionScene& collision, int polygonIndex) {
    if (polygonIndex < 0 || static_cast<size_t>(polygonIndex) >= collision.Polygons.size()) {
        return false;
    }
    const auto& polygon = collision.Polygons[static_cast<size_t>(polygonIndex)];
    return polygon.VertexA >= 0 && polygon.VertexB >= 0 && polygon.VertexC >= 0 &&
           static_cast<size_t>(polygon.VertexA) < collision.Vertices.size() &&
           static_cast<size_t>(polygon.VertexB) < collision.Vertices.size() &&
           static_cast<size_t>(polygon.VertexC) < collision.Vertices.size();
}

Vec3f CollisionPolygonNormal(const Oot3dNativeDemoCollisionScene& collision, int polygonIndex) {
    if (!CollisionPolygonIndexValid(collision, polygonIndex)) {
        return { 0.0f, 1.0f, 0.0f };
    }
    const auto& polygon = collision.Polygons[static_cast<size_t>(polygonIndex)];
    return NormalizeVec3({
        static_cast<float>(static_cast<double>(polygon.NormalX) / 32767.0),
        static_cast<float>(static_cast<double>(polygon.NormalY) / 32767.0),
        static_cast<float>(static_cast<double>(polygon.NormalZ) / 32767.0),
    });
}

Vec3f LightVectorFromPicaByteGroup(const Oot3dNativeDemoPicaByteGroup& group,
                                   const std::string& source) {
    if (source == "negated_signed_rgb_normalized") {
        return NormalizeVec3({
            static_cast<float>(-group.Signed0),
            static_cast<float>(-group.Signed1),
            static_cast<float>(-group.Signed2),
        });
    }
    if (source == "signed_rgb_normalized") {
        return NormalizeVec3({
            static_cast<float>(group.Signed0),
            static_cast<float>(group.Signed1),
            static_cast<float>(group.Signed2),
        });
    }
    return {};
}

Vec3f LightVectorFromLightSettingsRecord(const Oot3dNativeDemoPicaLightSettingsRecord& record,
                                         int offset,
                                         const std::string& source) {
    if (source == "oot3d_runtime_light_settings_signed_vec3_normalized") {
        return RuntimeVectorFromRecordBytes(record, offset);
    }
    if (!CanReadRecordBytes(record, offset, 3)) {
        return {};
    }
    const Vec3f signedVector = {
        static_cast<float>(static_cast<int8_t>(record.RawBytes[static_cast<size_t>(offset + 0)])),
        static_cast<float>(static_cast<int8_t>(record.RawBytes[static_cast<size_t>(offset + 1)])),
        static_cast<float>(static_cast<int8_t>(record.RawBytes[static_cast<size_t>(offset + 2)])),
    };
    if (source == "env_light_settings_signed_vec3_normalized" ||
        source == "oot3d_runtime_light_settings_signed_vec3_normalized") {
        return NormalizeVec3(signedVector);
    }
    if (source == "env_light_settings_negated_signed_vec3_normalized" ||
        source == "oot3d_runtime_light_settings_negated_signed_vec3_normalized") {
        return NormalizeVec3({ -signedVector.X, -signedVector.Y, -signedVector.Z });
    }
    return {};
}

bool IsSupportedLightSettingsVectorSource(const std::string& source) {
    return source == "env_light_settings_signed_vec3_normalized" ||
           source == "env_light_settings_negated_signed_vec3_normalized" ||
           source == "oot3d_runtime_light_settings_signed_vec3_normalized" ||
           source == "oot3d_runtime_light_settings_negated_signed_vec3_normalized";
}

void DecodeVertexHemisphereUniformTrace(const Oot3dNativeDemoScene& scene,
                                        const std::string& source,
                                        Oot3dNativePicaLightingRenderState& lighting) {
    if (source != "azahar_pica_frame_vs_uniforms" ||
        !scene.NativePicaRegisterTraceAvailable ||
        !scene.NativePicaRegisterTrace.is_object()) {
        return;
    }

    const auto* frameCapture = JsonObjectChild(scene.NativePicaRegisterTrace, "frame_capture");
    if (frameCapture == nullptr) {
        return;
    }
    const auto* trace = JsonObjectChild(*frameCapture, "vertex_hemisphere_lighting_uniform_trace");
    if (trace == nullptr || !JsonBoolValue(*trace, "available", false)) {
        return;
    }

    const auto ambientColor = JsonColorU8ObjectValue(*trace, "ambient_color_u8");
    const auto diffuseColor = JsonColorU8ObjectValue(*trace, "diffuse_color_u8");
    const auto lightVector = JsonVec3ArrayValue(*trace, "light_vector");
    if (!ambientColor.has_value() || !diffuseColor.has_value() || !lightVector.has_value()) {
        return;
    }

    lighting.VertexHemisphereUniformTraceDecoded = true;
    lighting.VertexHemisphereUniformTraceSourceKind = JsonStringValue(*trace, "source_kind");
    lighting.VertexHemisphereUniformTraceFormat = JsonStringValue(*trace, "format");
    lighting.VertexHemisphereUniformTraceFormula = JsonStringValue(*trace, "formula");
    lighting.VertexHemisphereUniformTraceDrawIndex = JsonIntValue(*trace, "selected_draw_index", -1);
    lighting.VertexHemisphereUniformTraceCandidateDrawCount =
        JsonIntValue(*trace, "candidate_draw_count", 0);
    lighting.VertexHemisphereUniformTraceCandidateVertexCount =
        JsonIntValue(*trace, "candidate_vertex_count", 0);
    lighting.VertexHemisphereAmbientUniformIndex =
        JsonIntValue(*trace, "ambient_uniform_index", -1);
    lighting.VertexHemisphereDiffuseUniformIndex =
        JsonIntValue(*trace, "diffuse_uniform_index", -1);
    lighting.VertexHemisphereLightVectorUniformIndex =
        JsonIntValue(*trace, "light_vector_uniform_index", -1);
    lighting.VertexHemisphereAmbientColor = *ambientColor;
    lighting.VertexHemisphereDiffuseColor = *diffuseColor;
    if (const auto secondaryColor = JsonColorU8ObjectValue(*trace, "secondary_color_u8");
        secondaryColor.has_value()) {
        lighting.VertexHemisphereSecondaryColor = *secondaryColor;
    }
    lighting.VertexHemisphereLightVector = NormalizeVec3(*lightVector);
    if (const auto negatedLightVector = JsonVec3ArrayValue(*trace, "negated_light_vector");
        negatedLightVector.has_value()) {
        lighting.VertexHemisphereNegatedLightVector = NormalizeVec3(*negatedLightVector);
    }
    const auto worldLightVector = JsonVec3ArrayValue(*trace, "world_light_vector_from_normal_matrix");
    const auto worldNegatedLightVector =
        JsonVec3ArrayValue(*trace, "world_negated_light_vector_from_normal_matrix");
    if (worldLightVector.has_value() && worldNegatedLightVector.has_value()) {
        lighting.VertexHemisphereWorldLightVectorDecoded = true;
        lighting.VertexHemisphereWorldLightVector = NormalizeVec3(*worldLightVector);
        lighting.VertexHemisphereWorldNegatedLightVector = NormalizeVec3(*worldNegatedLightVector);
    }
}

ColorRgba8 CombinePicaLightingDebugColor(ColorRgba8 ambient, ColorRgba8 diffuse,
                                         const std::string& formula) {
    if (formula != "clamp((ambient.rgb + diffuse.rgb) * 0.5)") {
        return {};
    }
    return {
        static_cast<uint8_t>((static_cast<int>(ambient.R) + static_cast<int>(diffuse.R)) / 2),
        static_cast<uint8_t>((static_cast<int>(ambient.G) + static_cast<int>(diffuse.G)) / 2),
        static_cast<uint8_t>((static_cast<int>(ambient.B) + static_cast<int>(diffuse.B)) / 2),
        static_cast<uint8_t>((static_cast<int>(ambient.A) + static_cast<int>(diffuse.A)) / 2),
    };
}

uint8_t ClampColorInt(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

uint8_t MaxColorRgb(ColorRgba8 color) {
    return std::max(color.R, std::max(color.G, color.B));
}

ColorRgba8 NeutralRgbIntensity(ColorRgba8 color) {
    const uint8_t intensity = MaxColorRgb(color);
    return { intensity, intensity, intensity, color.A };
}

bool MaterialUsesNeutralLightingColors(const Oot3dNativeRenderMaterialState& material) {
    return material.MaterialColorsDecoded &&
           ColorRgbIsNeutral(material.EmissionColor) &&
           ColorRgbIsNeutral(material.AmbientColor) &&
           ColorRgbIsNeutral(material.DiffuseColor);
}

struct NativePicaEffectiveMaterialDiffuse {
    ColorRgba8 Color = { 0, 0, 0, 255 };
    std::string Source;
    bool UsesAmbient = false;
    bool Resolved = false;
};

NativePicaEffectiveMaterialDiffuse ResolveNativePicaEffectiveMaterialDiffuse(
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRenderMaterialState& material,
    bool nativeVertexOrHemisphereLighting) {
    (void)nativeVertexOrHemisphereLighting;
    NativePicaEffectiveMaterialDiffuse resolved;
    resolved.Color = material.DiffuseColor;
    resolved.Source = lighting.MaterialDiffuseSource.empty()
                          ? "oot3d_cmb_material_diffuse_rgb"
                           : lighting.MaterialDiffuseSource;
    resolved.Resolved = material.MaterialColorsDecoded;
    return resolved;
}

ColorRgba8 LightColorForVertexHemispherePath(const Oot3dNativePicaLightingRenderState& lighting,
                                             const Oot3dNativeRenderMaterialState& material,
                                             ColorRgba8 color,
                                             bool nativeVertexOrHemisphereLighting) {
    const bool neutralFallbackMode =
        lighting.VertexHemisphereLightColorMode ==
            "neutral_max_rgb_intensity_for_neutral_cmb_material_colors" ||
        lighting.VertexHemisphereLightColorMode ==
            "azahar_vs_uniform_trace_with_neutral_max_record_fallback";
    if (!nativeVertexOrHemisphereLighting ||
        !neutralFallbackMode ||
        !MaterialUsesNeutralLightingColors(material)) {
        return color;
    }
    return NeutralRgbIntensity(color);
}

ColorRgba8 CombinePicaLightingVertexModulationColor(ColorRgba8 ambient, ColorRgba8 diffuse,
                                                    ColorRgba8 materialEmission,
                                                    ColorRgba8 materialAmbient,
                                                    ColorRgba8 materialDiffuse,
                                                    const std::string& formula) {
    if (formula !=
        "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb / 255, 0, 255) / 255)") {
        return {};
    }
    return {
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.R) +
                                                   static_cast<double>(materialAmbient.R) * ambient.R / 255.0 +
                                                   static_cast<double>(materialDiffuse.R) * diffuse.R / 255.0))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.G) +
                                                   static_cast<double>(materialAmbient.G) * ambient.G / 255.0 +
                                                   static_cast<double>(materialDiffuse.G) * diffuse.G / 255.0))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.B) +
                                                   static_cast<double>(materialAmbient.B) * ambient.B / 255.0 +
                                                   static_cast<double>(materialDiffuse.B) * diffuse.B / 255.0))),
        255,
    };
}

ColorRgba8 CombinePicaLightingDirectionalModulationColor(ColorRgba8 ambient, ColorRgba8 diffuse,
                                                         ColorRgba8 light1,
                                                         ColorRgba8 materialEmission,
                                                         ColorRgba8 materialAmbient,
                                                         ColorRgba8 materialDiffuse,
                                                         double normalLightDot,
                                                         double normalLight1Dot,
                                                         double ambientShadowFactor,
                                                         double primaryShadowFactor,
                                                         const std::string& formula) {
    const double ambientScale = std::clamp(ambientShadowFactor, 0.0, 1.0);
    const double shadowScale = std::clamp(primaryShadowFactor, 0.0, 1.0);
    const double diffuseScale = std::clamp(normalLightDot, 0.0, 1.0) * shadowScale;
    if (formula ==
        "clamp(base.rgb * clamp(ambient.rgb + light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0), 0, 255) / 255)") {
        const double light1Scale = std::clamp(normalLight1Dot, 0.0, 1.0) * shadowScale;
        return {
            ClampColorInt(static_cast<int>(std::lround(
                static_cast<double>(ambient.R) * ambientScale +
                static_cast<double>(diffuse.R) * diffuseScale +
                static_cast<double>(light1.R) * light1Scale))),
            ClampColorInt(static_cast<int>(std::lround(
                static_cast<double>(ambient.G) * ambientScale +
                static_cast<double>(diffuse.G) * diffuseScale +
                static_cast<double>(light1.G) * light1Scale))),
            ClampColorInt(static_cast<int>(std::lround(
                static_cast<double>(ambient.B) * ambientScale +
                static_cast<double>(diffuse.B) * diffuseScale +
                static_cast<double>(light1.B) * light1Scale))),
            255,
        };
    }
    if (formula ==
        "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb * max(dot(normal, light0), 0) / 255, 0, 255) / 255)") {
        return {
            ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.R) +
                                                       static_cast<double>(materialAmbient.R) * ambient.R *
                                                           ambientScale / 255.0 +
                                                       static_cast<double>(materialDiffuse.R) * diffuse.R *
                                                           diffuseScale / 255.0))),
            ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.G) +
                                                       static_cast<double>(materialAmbient.G) * ambient.G *
                                                           ambientScale / 255.0 +
                                                       static_cast<double>(materialDiffuse.G) * diffuse.G *
                                                           diffuseScale / 255.0))),
            ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.B) +
                                                       static_cast<double>(materialAmbient.B) * ambient.B *
                                                           ambientScale / 255.0 +
                                                       static_cast<double>(materialDiffuse.B) * diffuse.B *
                                                           diffuseScale / 255.0))),
            255,
        };
    }

    if (formula !=
        "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)") {
        return {};
    }
    const double light1Scale = std::clamp(normalLight1Dot, 0.0, 1.0) * shadowScale;
    return {
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.R) +
                                                   static_cast<double>(materialAmbient.R) * ambient.R *
                                                       ambientScale / 255.0 +
                                                   static_cast<double>(materialDiffuse.R) *
                                                       (static_cast<double>(diffuse.R) * diffuseScale +
                                                        static_cast<double>(light1.R) * light1Scale) /
                                                       255.0))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.G) +
                                                   static_cast<double>(materialAmbient.G) * ambient.G *
                                                       ambientScale / 255.0 +
                                                   static_cast<double>(materialDiffuse.G) *
                                                       (static_cast<double>(diffuse.G) * diffuseScale +
                                                        static_cast<double>(light1.G) * light1Scale) /
                                                       255.0))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(materialEmission.B) +
                                                   static_cast<double>(materialAmbient.B) * ambient.B *
                                                       ambientScale / 255.0 +
                                                   static_cast<double>(materialDiffuse.B) *
                                                       (static_cast<double>(diffuse.B) * diffuseScale +
                                                        static_cast<double>(light1.B) * light1Scale) /
                                                       255.0))),
        255,
    };
}

ColorRgba8 PicaLightingBaseColorForVertex(const Oot3dNativePicaLightingRenderState& lighting,
                                          const Oot3dNativeRenderBatch& batch,
                                          ColorRgba8 color) {
    ColorRgba8 baseColor = color;
    if (batch.Material.Textured &&
        lighting.TexturedBaseColorSource == "constant_white_until_material_combiner_route_decoded") {
        if (batch.Material.TextureEnvProgram.RgbRouteDecoded &&
            batch.Material.TextureEnvProgram.ColorShaderPathSupported &&
            batch.Material.TextureEnvProgram.UsesPrimaryColor) {
            baseColor = color;
        } else {
            baseColor = { 255, 255, 255, color.A };
        }
    }
    return baseColor;
}

ColorRgba8 ApplyPicaLightingVertexModulation(ColorRgba8 color, ColorRgba8 modulation,
                                             bool preserveAlpha) {
    const uint8_t alpha = preserveAlpha ? color.A : modulation.A;
    return {
        static_cast<uint8_t>((static_cast<int>(color.R) * static_cast<int>(modulation.R)) / 255),
        static_cast<uint8_t>((static_cast<int>(color.G) * static_cast<int>(modulation.G)) / 255),
        static_cast<uint8_t>((static_cast<int>(color.B) * static_cast<int>(modulation.B)) / 255),
        alpha,
    };
}

ColorRgba8 ApplyTextureEnvProgramVertexMultiplier(ColorRgba8 color,
                                                  const Oot3dNativeRenderTextureEnvProgram& program) {
    return ApplyPicaLightingVertexModulation(color, program.VertexColorMultiplier, true);
}

float WrapUnitInterval(float value) {
    return value - std::floor(value);
}

std::optional<Vec3f> SampleNativePicaNormalMap(const Oot3dNativeRenderTexture& texture, Vec2f uv) {
    if (!texture.Rgba8Decoded || texture.Width == 0 || texture.Height == 0 ||
        texture.Rgba8.size() != static_cast<size_t>(texture.Width) * texture.Height * 4) {
        return std::nullopt;
    }
    const uint32_t x = std::min<uint32_t>(
        texture.Width - 1,
        static_cast<uint32_t>(std::floor(WrapUnitInterval(uv.X) * texture.Width)));
    const uint32_t y = std::min<uint32_t>(
        texture.Height - 1,
        static_cast<uint32_t>(std::floor(WrapUnitInterval(uv.Y) * texture.Height)));
    const size_t offset = (static_cast<size_t>(y) * texture.Width + x) * 4;
    Vec3f normal{
        static_cast<float>(static_cast<double>(texture.Rgba8[offset + 0]) / 127.5 - 1.0),
        static_cast<float>(static_cast<double>(texture.Rgba8[offset + 1]) / 127.5 - 1.0),
        static_cast<float>(static_cast<double>(texture.Rgba8[offset + 2]) / 127.5 - 1.0),
    };
    const double xyLengthSquared =
        static_cast<double>(normal.X) * normal.X + static_cast<double>(normal.Y) * normal.Y;
    if (xyLengthSquared <= 1.0) {
        normal.Z = static_cast<float>(std::sqrt(1.0 - xyLengthSquared));
    }
    return NormalizeVec3(normal);
}

Vec3f NativePicaLightingNormalForVertex(const Oot3dNativeRenderModel& model,
                                        Oot3dNativeRenderBatch& batch,
                                        const Oot3dNativeRenderVertex& vertex) {
    if (!batch.Material.NativePicaBumpNormalMapBackendSupported ||
        batch.Material.NativePicaBumpTextureIndex < 0 ||
        static_cast<size_t>(batch.Material.NativePicaBumpTextureIndex) >= model.Textures.size() ||
        !vertex.NativePicaBumpUvAvailable) {
        return vertex.Normal;
    }
    const auto normal = SampleNativePicaNormalMap(
        model.Textures[static_cast<size_t>(batch.Material.NativePicaBumpTextureIndex)],
        vertex.NativePicaBumpUv);
    if (!normal.has_value()) {
        return vertex.Normal;
    }
    batch.Material.NativePicaBumpNormalMapApplied = true;
    return *normal;
}

bool MaterialUsesNativePicaLighting(const Oot3dNativePicaLightingRenderState& lighting,
                                    const Oot3dNativeRenderMaterialState& material) {
    if (lighting.MaterialLightingEnableSource == "oot3d_cmb_material_fragment_lighting_flag") {
        return material.FragmentLightingEnabled;
    }
    if (lighting.MaterialLightingEnableSource == "oot3d_cmb_material_diffuse_rgb_nonzero") {
        return !ColorRgbIsZero(material.DiffuseColor);
    }
    if (lighting.MaterialLightingEnableSource == "oot3d_cmb_material_lighting_flags") {
        return material.FragmentLightingEnabled || material.VertexLightingEnabled ||
               material.HemisphereLightingEnabled;
    }
    return lighting.MaterialLightingEnableSource.empty();
}

bool MaterialDefersNativeVertexOrHemisphereLighting(const Oot3dNativePicaLightingRenderState& lighting,
                                                    const Oot3dNativeRenderMaterialState& material) {
    return lighting.MaterialLightingEnableSource == "oot3d_cmb_material_fragment_lighting_flag" &&
           lighting.MaterialLightingDeferredSource == "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" &&
           !material.FragmentLightingEnabled &&
           (material.VertexLightingEnabled || material.HemisphereLightingEnabled);
}

bool ModelSupportsNativeVertexOrHemisphereLighting(const Oot3dNativePicaLightingRenderState& lighting,
                                                   const Oot3dNativeRenderModel& model) {
    if (!lighting.VertexHemisphereLightingSupported) {
        return false;
    }
    if (lighting.MaterialVertexHemisphereModelScope ==
        "native_cmb_skeleton_transform_instance_models_with_native_normals") {
        return model.NativeCmbSkeletonBoneCount > 0 && !model.TransformBakedIntoVertices;
    }
    if (lighting.MaterialVertexHemisphereModelScope ==
        "native_cmb_transform_instance_or_static_baked_models_with_native_normals") {
        return true;
    }
    return model.NativeCmbSkeletonBoneCount > 0;
}

bool BatchHasNativeNormal(const Oot3dNativeRenderBatch& batch) {
    return std::any_of(batch.Vertices.begin(), batch.Vertices.end(),
                       [](const Oot3dNativeRenderVertex& vertex) {
                           return vertex.NativeNormalAvailable;
                       });
}

bool MaterialUsesNativeVertexOrHemisphereLighting(const Oot3dNativePicaLightingRenderState& lighting,
                                                  const Oot3dNativeRenderModel& model,
                                                  const Oot3dNativeRenderBatch& batch) {
    if (!ModelSupportsNativeVertexOrHemisphereLighting(lighting, model) ||
        !MaterialDefersNativeVertexOrHemisphereLighting(lighting, batch.Material)) {
        return false;
    }
    if (BatchHasNativeNormal(batch)) {
        return true;
    }
    return batch.Material.MaterialColorsDecoded &&
           ColorRgbIsZero(batch.Material.DiffuseColor) &&
           !ColorRgbIsZero(batch.Material.AmbientColor);
}

bool ShadowLightVectorSourceSupported(const std::string& source) {
    return source == "active_env_light_settings_primary_nonzero_directional_light_vector" ||
           source == "active_0045dd50_runtime_environment_primary_nonzero_directional_light_vector";
}

bool ModelCanReceiveNativePicaSelfShadowRoute(const Oot3dNativePicaShadowState& shadow,
                                              const Oot3dNativeRenderModel& model) {
    return shadow.SelfShadowCandidateRouteSupported &&
           !model.TransformBakedIntoVertices &&
           model.NativeCmbSkeletonBoneCount > 0;
}

bool ModelSupportsNativePicaSelfShadow(const Oot3dNativePicaShadowState& shadow,
                                       const Oot3dNativeRenderModel& model) {
    return ModelCanReceiveNativePicaSelfShadowRoute(shadow, model) &&
           shadow.SelfShadowShaderRouteDecoded &&
           shadow.NativeGeometryOcclusionSupported;
}

bool BatchCanReceiveNativePicaSelfShadowRoute(const Oot3dNativePicaShadowState& shadow,
                                              const Oot3dNativePicaLightingRenderState& lighting,
                                              const Oot3dNativeRenderModel& model,
                                              const Oot3dNativeRenderBatch& batch) {
    if (!ModelCanReceiveNativePicaSelfShadowRoute(shadow, model) || !BatchHasNativeNormal(batch)) {
        return false;
    }
    return MaterialUsesNativePicaLighting(lighting, batch.Material) ||
           MaterialUsesNativeVertexOrHemisphereLighting(lighting, model, batch);
}

bool BatchUsesNativePicaSelfShadow(const Oot3dNativePicaShadowState& shadow,
                                   const Oot3dNativePicaLightingRenderState& lighting,
                                   const Oot3dNativeRenderModel& model,
                                   const Oot3dNativeRenderBatch& batch) {
    return BatchCanReceiveNativePicaSelfShadowRoute(shadow, lighting, model, batch) &&
           ModelSupportsNativePicaSelfShadow(shadow, model);
}

bool VertexHemisphereModeUsesActorVsColorPacket(const Oot3dNativePicaLightingRenderState& lighting) {
    return lighting.VertexHemisphereLightColorMode ==
               "oot3d_zsi_actor_vs_light_packet_colors_with_runtime_environment_vector_until_final_writer_resolved" ||
           lighting.VertexHemisphereLightColorMode ==
               "oot3d_zsi_actor_vs_light_packet_colors_with_env_record_vector_until_final_writer_resolved" ||
           lighting.VertexHemisphereLightColorMode ==
               "oot3d_draw_local_light_packet_colors_with_explicit_vectors";
}

bool VertexHemisphereModeUsesActorVsVectorPacket(const Oot3dNativePicaLightingRenderState& lighting) {
    return VertexHemisphereModeUsesActorVsColorPacket(lighting) ||
           lighting.VertexHemisphereLightColorMode ==
               "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors";
}

bool ModelUsesActorVsVertexHemisphereColorPacket(const Oot3dNativePicaLightingRenderState& lighting,
                                                 const Oot3dNativeRenderModel& model,
                                                 bool nativeVertexOrHemisphereLighting) {
    if (!nativeVertexOrHemisphereLighting ||
        !VertexHemisphereModeUsesActorVsColorPacket(lighting) ||
        !lighting.ActorVsLightPacket.Available) {
        return false;
    }
    return !model.TransformBakedIntoVertices;
}

bool ModelUsesActorVsVertexHemisphereVectorGate(const Oot3dNativePicaLightingRenderState& lighting,
                                                const Oot3dNativeRenderModel& model,
                                                bool nativeVertexOrHemisphereLighting) {
    if (!nativeVertexOrHemisphereLighting ||
        !VertexHemisphereModeUsesActorVsVectorPacket(lighting) ||
        !lighting.ActorVsLightPacket.Available) {
        return false;
    }
    if (model.TransformBakedIntoVertices) {
        return false;
    }
    return true;
}

std::string NativeVertexHemisphereColorSourceForModel(const Oot3dNativePicaLightingRenderState& lighting,
                                                      const Oot3dNativeRenderModel& model,
                                                      bool nativeVertexOrHemisphereLighting,
                                                      bool nativeVertexHemispherePacket) {
    if (!nativeVertexOrHemisphereLighting) {
        return {};
    }
    if (nativeVertexHemispherePacket) {
        return lighting.VertexHemisphereRuntimeColorSource.empty()
                   ? lighting.ActorVsLightPacket.ColorSource
                   : lighting.VertexHemisphereRuntimeColorSource;
    }
    if (lighting.VertexHemisphereLightColorMode ==
            "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors" &&
        lighting.ResolvedRuntimeLightSetting.Available) {
        const auto& resolved = lighting.ResolvedRuntimeLightSetting;
        if (resolved.RuntimeFinalAmbientColorUsedForRender ||
            resolved.RuntimeFinalLightColorUsedForRender) {
            return "active_0045dd50_final_runtime_environment_rgb_after_color_addends";
        }
        return "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends";
    }
    if (model.TransformBakedIntoVertices && lighting.ResolvedRuntimeLightSetting.Available &&
        lighting.AmbientColorSource == "oot3d_runtime_light_settings_rgb_u8" &&
        lighting.DiffuseColorSource == "oot3d_runtime_light_settings_rgb_u8") {
        const auto& resolved = lighting.ResolvedRuntimeLightSetting;
        if (resolved.RuntimeFinalAmbientColorUsedForRender ||
            resolved.RuntimeFinalLightColorUsedForRender) {
            return "active_0045dd50_final_runtime_environment_rgb_after_color_addends";
        }
        return "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends";
    }
    if (!lighting.AmbientColorSource.empty() || !lighting.DiffuseColorSource.empty()) {
        return lighting.AmbientColorSource + "/" + lighting.DiffuseColorSource;
    }
    return "oot3d_pica_light_settings_record_byte_groups";
}

ColorRgba8 NativeVertexHemisphereAmbientColorForBatch(const Oot3dNativePicaLightingRenderState& lighting,
                                                      bool nativeVertexHemispherePacket) {
    return nativeVertexHemispherePacket ? lighting.ActorVsLightPacket.AmbientColor : lighting.AmbientColor;
}

ColorRgba8 NativeVertexHemisphereDiffuse0ColorForBatch(const Oot3dNativePicaLightingRenderState& lighting,
                                                       bool nativeVertexHemispherePacket) {
    return nativeVertexHemispherePacket ? lighting.ActorVsLightPacket.Diffuse0Color : lighting.DiffuseColor;
}

ColorRgba8 NativeVertexHemisphereDiffuse1ColorForBatch(const Oot3dNativePicaLightingRenderState& lighting,
                                                       bool nativeVertexHemispherePacket) {
    return nativeVertexHemispherePacket ? lighting.ActorVsLightPacket.Diffuse1Color : lighting.Light1Color;
}

bool NativeVertexHemisphereDirectionalVectorResolved(const Oot3dNativePicaLightingRenderState& lighting,
                                                     const Oot3dNativeRenderModel& model,
                                                     bool nativeVertexOrHemisphereLighting) {
    if (!ModelUsesActorVsVertexHemisphereVectorGate(lighting, model, nativeVertexOrHemisphereLighting)) {
        return true;
    }
    return lighting.ActorVsLightPacket.VectorOriginResolved;
}

std::string NativeVertexHemisphereVectorSourceForModel(const Oot3dNativePicaLightingRenderState& lighting,
                                                       const Oot3dNativeRenderModel& model,
                                                       bool nativeVertexOrHemisphereLighting) {
    if (!nativeVertexOrHemisphereLighting) {
        return {};
    }
    if (ModelUsesActorVsVertexHemisphereVectorGate(lighting, model, nativeVertexOrHemisphereLighting)) {
        return lighting.VertexHemisphereRuntimeVectorSource;
    }
    if (model.TransformBakedIntoVertices && lighting.ResolvedRuntimeLightSetting.Available &&
        lighting.Light0VectorSource == "oot3d_runtime_light_settings_signed_vec3_normalized") {
        return "active_0045dd50_runtime_environment_signed_vec3";
    }
    return lighting.Light0VectorSource;
}

bool NativeCmbVShaderHasTexCoord0WOutput(const Oot3dNativeDemoScene& scene) {
    if (!scene.NativeCmbVShaderShbinAvailable) {
        return false;
    }
    for (const auto& program : scene.NativeCmbVShader.Programs) {
        for (const auto& output : program.Outputs) {
            if (output.Type == 4 && output.SemanticName == "out.tex0w") {
                return true;
            }
        }
    }
    return false;
}

bool NativeCmbVShaderHasLightingAccumulator(const Oot3dNativeDemoScene& scene) {
    if (!scene.NativeCmbVShaderShbinAvailable || scene.NativeCmbVShader.Programs.size() != 1) {
        return false;
    }
    const auto& program = scene.NativeCmbVShader.Programs.front();
    const auto hasUniform = [&](std::string_view name, uint16_t registerStart) {
        return std::any_of(program.Uniforms.begin(), program.Uniforms.end(),
                           [&](const ShbinUniformInfo& uniform) {
                               return uniform.Name == name &&
                                      uniform.RegisterStart == registerStart;
                           });
    };
    if (!hasUniform("MatDiffuseColor", 24) || !hasUniform("MatAmbientColor", 25) ||
        !hasUniform("LightDir0", 96) || !hasUniform("LightDiffuseColor0", 97) ||
        !hasUniform("LightAmbientColor0.xyz", 98) || !hasUniform("LightDir1", 99) ||
        !hasUniform("LightDiffuseColor1", 100) ||
        !hasUniform("LightAmbientColor1.xyz", 101) ||
        !hasUniform("VertexAttributeScale0.xyz", 106) ||
        !hasUniform("IsVertexLighting", 129) || !hasUniform("HasColor", 125)) {
        return false;
    }

    // Recognize the native per-light RGBA accumulator and final scaled vertex-color multiply.
    constexpr std::array<std::pair<size_t, uint32_t>, 9> signature = {{
        { 0x130 / 4, 0x9E41C004 },
        { 0x134 / 4, 0x4E028017 },
        { 0x138 / 4, 0x4E229017 },
        { 0x174 / 4, 0x0341AA11 },
        { 0x18C / 4, 0x0341AA11 },
        { 0x1AC / 4, 0x0341AC81 },
        { 0x1B4 / 4, 0x2327A11A },
        { 0x1B8 / 4, 0x2341AC80 },
        { 0x1E0 / 4, 0x4C21A017 },
    }};
    return std::all_of(signature.begin(), signature.end(), [&](const auto& entry) {
        return entry.first < scene.NativeCmbVShader.ProgramCode.size() &&
               scene.NativeCmbVShader.ProgramCode[entry.first] == entry.second;
    });
}

bool MaterialColorSourcesSupported(const Oot3dNativePicaLightingRenderState& lighting) {
    return lighting.MaterialEmissionSource == "oot3d_cmb_material_emission_rgb" &&
           lighting.MaterialAmbientSource == "oot3d_cmb_material_ambient_rgb" &&
           lighting.MaterialDiffuseSource == "oot3d_cmb_material_diffuse_rgb";
}

bool MaterialLightingEnableSourceSupported(const std::string& source) {
    return source == "oot3d_cmb_material_fragment_lighting_flag" ||
           source == "oot3d_cmb_material_diffuse_rgb_nonzero" ||
           source == "oot3d_cmb_material_lighting_flags";
}

bool MaterialUsesPrimaryColorTextureEnv(const Oot3dNativeRenderMaterialState& material) {
    return material.TextureEnvProgram.UsesPrimaryColor;
}

bool MaterialHasNativeUnlitTextureEnvRoute(const Oot3dNativeRenderMaterialState& material) {
    return material.NativeMaterialAvailable &&
           !material.FragmentLightingEnabled &&
           !material.VertexLightingEnabled &&
           !material.HemisphereLightingEnabled &&
           material.Textured &&
           material.TextureEnvProgram.RgbRouteDecoded &&
           material.TextureEnvProgram.ColorShaderPathSupported &&
           material.TextureEnvProgram.UsesPrimaryColor &&
           material.TextureEnvProgram.UsesTexture0 &&
           !material.TextureEnvProgram.UsesFragmentLightingColor &&
           !material.TextureEnvProgram.RequiresPreviousBuffer;
}

bool BatchHasNativeColor(const Oot3dNativeRenderBatch& batch) {
    return std::any_of(batch.Vertices.begin(), batch.Vertices.end(),
                       [](const Oot3dNativeRenderVertex& vertex) {
                           return vertex.NativeColorAvailable;
                       });
}

void ApplyTextureEnvShaderCoverage(Oot3dNativeRenderMaterialState& material) {
    if (material.TextureEnvProgram.ColorShaderPathSupported) {
        material.TextureEnvProgram.ColorShaderPathApplied = true;
        if (material.TextureEnv.ColorShaderPathSupported) {
            material.TextureEnv.ColorShaderPathApplied = true;
        }
        if (!material.TextureEnvStages.empty() && material.TextureEnvStages.front().ColorShaderPathSupported) {
            material.TextureEnvStages.front().ColorShaderPathApplied = true;
        }
    } else if (material.TextureEnv.ColorShaderPathSupported) {
        material.TextureEnv.ColorShaderPathApplied = true;
        if (!material.TextureEnvStages.empty()) {
            material.TextureEnvStages.front().ColorShaderPathApplied = true;
        }
    }
}

void RebuildTextureEnvProgramPreservingShaderCoverage(Oot3dNativeRenderMaterialState& material) {
    const bool shaderCoverageApplied = material.TextureEnvProgram.ColorShaderPathApplied;
    material.TextureEnvProgram = BuildTextureEnvProgram(material.TextureEnvStages, material.ConstantColors);
    if (shaderCoverageApplied) {
        ApplyTextureEnvShaderCoverage(material);
    }
    RefreshTextureEnvCombinerRequirement(material, material.TextureEnvStages.size());
}

void ApplyTextureEnvProgramOnlyToBatch(Oot3dNativeRenderBatch& batch) {
    ApplyTextureEnvShaderCoverage(batch.Material);
    if (MaterialHasNativeUnlitTextureEnvRoute(batch.Material)) {
        batch.Material.NativePicaUnlitTextureEnvRouteDecoded = true;
        batch.Material.NativePicaUnlitTextureEnvRouteRequiresNativeColor = true;
        batch.Material.NativePicaUnlitTextureEnvRouteNativeColorAvailable = BatchHasNativeColor(batch);
        batch.Material.NativePicaUnlitTextureEnvRouteSource =
            "oot3d_cmb_lighting_flags_disabled_texture_env_primary_color_texture0";
    }
    if (batch.Material.Textured && MaterialUsesPrimaryColorTextureEnv(batch.Material)) {
        batch.Material.VertexColorModulatesTexture = true;
    }
    if (batch.Material.Textured && batch.Material.TextureEnvProgram.ColorShaderPathApplied) {
        for (auto& vertex : batch.Vertices) {
            vertex.Color = ApplyTextureEnvProgramVertexMultiplier(vertex.Color, batch.Material.TextureEnvProgram);
        }
    }
    batch.Material.NativePicaUnlitTextureEnvRouteApplied =
        batch.Material.NativePicaUnlitTextureEnvRouteDecoded &&
        batch.Material.NativePicaUnlitTextureEnvRouteNativeColorAvailable &&
        batch.Material.TextureEnvProgram.ColorShaderPathApplied &&
        batch.Material.VertexColorModulatesTexture;
}

void ApplyTextureEnvProgramOnlyToModel(Oot3dNativeRenderModel& model) {
    for (auto& batch : model.Batches) {
        ApplyTextureEnvProgramOnlyToBatch(batch);
    }
}

struct NativePicaLightingVertexEvaluation {
    ColorRgba8 BaseColor = { 0, 0, 0, 0 };
    ColorRgba8 AmbientColor = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse0Color = { 0, 0, 0, 255 };
    ColorRgba8 Diffuse1Color = { 0, 0, 0, 255 };
    ColorRgba8 ModulationColor = { 0, 0, 0, 255 };
    ColorRgba8 OutputColor = { 0, 0, 0, 0 };
    Vec3f LightingNormal = { 0.0f, 0.0f, 0.0f };
    Vec3f WorldNormal = { 0.0f, 0.0f, 0.0f };
    Vec3f Light0Vector = { 0.0f, 1.0f, 0.0f };
    Vec3f Light1Vector = { 0.0f, 1.0f, 0.0f };
    double Light0Dot = 0.0;
    double Light1Dot = 0.0;
    double Light0DiffuseScale = 0.0;
    double Light1DiffuseScale = 0.0;
    bool DirectionalEvaluated = false;
};

struct NativePicaScalarStatsAccumulator {
    size_t Count = 0;
    double Sum = 0.0;
    double Min = std::numeric_limits<double>::infinity();
    double Max = -std::numeric_limits<double>::infinity();
    size_t NegativeCount = 0;
    size_t ZeroCount = 0;
    size_t PositiveCount = 0;

    void Add(double value) {
        ++Count;
        Sum += value;
        Min = std::min(Min, value);
        Max = std::max(Max, value);
        if (value < -0.000001) {
            ++NegativeCount;
        } else if (value > 0.000001) {
            ++PositiveCount;
        } else {
            ++ZeroCount;
        }
    }

    Oot3dNativePicaScalarStats Finish() const {
        Oot3dNativePicaScalarStats stats;
        stats.Available = Count > 0;
        stats.Count = Count;
        stats.Min = Count > 0 ? Min : 0.0;
        stats.Max = Count > 0 ? Max : 0.0;
        stats.Average = Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
        stats.NegativeCount = NegativeCount;
        stats.ZeroCount = ZeroCount;
        stats.PositiveCount = PositiveCount;
        return stats;
    }
};

struct NativePicaColorStatsAccumulator {
    size_t Count = 0;
    uint64_t SumR = 0;
    uint64_t SumG = 0;
    uint64_t SumB = 0;
    uint64_t SumA = 0;
    ColorRgba8 Min = { 255, 255, 255, 255 };
    ColorRgba8 Max = { 0, 0, 0, 0 };

    void Add(ColorRgba8 color) {
        ++Count;
        SumR += color.R;
        SumG += color.G;
        SumB += color.B;
        SumA += color.A;
        Min.R = std::min(Min.R, color.R);
        Min.G = std::min(Min.G, color.G);
        Min.B = std::min(Min.B, color.B);
        Min.A = std::min(Min.A, color.A);
        Max.R = std::max(Max.R, color.R);
        Max.G = std::max(Max.G, color.G);
        Max.B = std::max(Max.B, color.B);
        Max.A = std::max(Max.A, color.A);
    }

    Oot3dNativePicaColorStats Finish() const {
        Oot3dNativePicaColorStats stats;
        stats.Available = Count > 0;
        stats.Count = Count;
        stats.Min = Count > 0 ? Min : ColorRgba8{ 0, 0, 0, 0 };
        stats.Max = Count > 0 ? Max : ColorRgba8{ 0, 0, 0, 0 };
        if (Count > 0) {
            stats.Average = {
                static_cast<uint8_t>(SumR / Count),
                static_cast<uint8_t>(SumG / Count),
                static_cast<uint8_t>(SumB / Count),
                static_cast<uint8_t>(SumA / Count),
            };
        }
        return stats;
    }
};

struct NativePicaLightingBatchDiagnosticsAccumulator {
    Oot3dNativePicaLightingBatchDiagnostics Diagnostics;
    NativePicaScalarStatsAccumulator Light0DotStats;
    NativePicaScalarStatsAccumulator Light0DiffuseScaleStats;
    NativePicaScalarStatsAccumulator Light0OppositeDotStats;
    NativePicaScalarStatsAccumulator Light0OppositeDiffuseScaleStats;
    NativePicaScalarStatsAccumulator Light1DotStats;
    NativePicaScalarStatsAccumulator Light1DiffuseScaleStats;
    NativePicaColorStatsAccumulator BaseColorStats;
    NativePicaColorStatsAccumulator ModulationColorStats;
    NativePicaColorStatsAccumulator OutputColorStats;
    double LightingNormalX = 0.0;
    double LightingNormalY = 0.0;
    double LightingNormalZ = 0.0;
    double WorldNormalX = 0.0;
    double WorldNormalY = 0.0;
    double WorldNormalZ = 0.0;

    void Add(size_t vertexIndex,
             const Oot3dNativeRenderVertex& vertex,
             const NativePicaLightingVertexEvaluation& evaluation,
             bool recordSample) {
        ++Diagnostics.VertexCount;
        BaseColorStats.Add(evaluation.BaseColor);
        ModulationColorStats.Add(evaluation.ModulationColor);
        OutputColorStats.Add(evaluation.OutputColor);
        if (vertex.NativeNormalAvailable) {
            ++Diagnostics.NativeNormalVertexCount;
        }
        if (!evaluation.DirectionalEvaluated) {
            return;
        }
        ++Diagnostics.DirectionalEvaluatedVertexCount;
        Light0DotStats.Add(evaluation.Light0Dot);
        Light0DiffuseScaleStats.Add(evaluation.Light0DiffuseScale);
        Light0OppositeDotStats.Add(-evaluation.Light0Dot);
        Light0OppositeDiffuseScaleStats.Add(Clamp01(-evaluation.Light0Dot));
        Light1DotStats.Add(evaluation.Light1Dot);
        Light1DiffuseScaleStats.Add(evaluation.Light1DiffuseScale);
        LightingNormalX += evaluation.LightingNormal.X;
        LightingNormalY += evaluation.LightingNormal.Y;
        LightingNormalZ += evaluation.LightingNormal.Z;
        WorldNormalX += evaluation.WorldNormal.X;
        WorldNormalY += evaluation.WorldNormal.Y;
        WorldNormalZ += evaluation.WorldNormal.Z;
        if (recordSample) {
            Diagnostics.Samples.push_back({
                vertexIndex,
                vertex.Position,
                evaluation.LightingNormal,
                evaluation.WorldNormal,
                evaluation.Light0Dot,
                evaluation.Light1Dot,
                evaluation.Light0DiffuseScale,
                evaluation.Light1DiffuseScale,
                evaluation.BaseColor,
                evaluation.ModulationColor,
                evaluation.OutputColor,
            });
        }
    }

    Oot3dNativePicaLightingBatchDiagnostics Finish() {
        Diagnostics.Available = Diagnostics.VertexCount > 0;
        Diagnostics.Light0DotStats = Light0DotStats.Finish();
        Diagnostics.Light0DiffuseScaleStats = Light0DiffuseScaleStats.Finish();
        Diagnostics.Light0OppositeDotStats = Light0OppositeDotStats.Finish();
        Diagnostics.Light0OppositeDiffuseScaleStats =
            Light0OppositeDiffuseScaleStats.Finish();
        Diagnostics.Light1DotStats = Light1DotStats.Finish();
        Diagnostics.Light1DiffuseScaleStats = Light1DiffuseScaleStats.Finish();
        Diagnostics.BaseColorStats = BaseColorStats.Finish();
        Diagnostics.ModulationColorStats = ModulationColorStats.Finish();
        Diagnostics.OutputColorStats = OutputColorStats.Finish();
        if (Diagnostics.DirectionalEvaluatedVertexCount > 0) {
            const double invCount =
                1.0 / static_cast<double>(Diagnostics.DirectionalEvaluatedVertexCount);
            Diagnostics.AverageLightingNormal = {
                static_cast<float>(LightingNormalX * invCount),
                static_cast<float>(LightingNormalY * invCount),
                static_cast<float>(LightingNormalZ * invCount),
            };
            Diagnostics.AverageWorldNormal = {
                static_cast<float>(WorldNormalX * invCount),
                static_cast<float>(WorldNormalY * invCount),
                static_cast<float>(WorldNormalZ * invCount),
            };
        }
        return Diagnostics;
    }
};

NativePicaLightingVertexEvaluation EvaluatePicaLightingForVertex(
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRenderModel& model,
    Oot3dNativeRenderBatch& batch,
    const Matrix4f& modelToWorld,
    const Oot3dNativeRenderVertex& vertex,
    bool nativeVertexOrHemisphereLighting,
    bool nativeVertexHemispherePacket,
    bool nativeVertexHemisphereDirectionalVectorResolved,
    ColorRgba8 effectiveMaterialDiffuse,
    bool shadowFullPrimaryLightContribution,
    double primaryShadowFactor) {
    NativePicaLightingVertexEvaluation evaluation;
    evaluation.BaseColor = PicaLightingBaseColorForVertex(lighting, batch, vertex.Color);
    const bool nativeVertexHemisphereTrace =
        nativeVertexOrHemisphereLighting && lighting.VertexHemisphereUniformTraceDecoded &&
        lighting.VertexHemisphereUniformTraceUsedAsRuntimeSource;
    ColorRgba8 ambientColor = LightColorForVertexHemispherePath(
        lighting, batch.Material, lighting.AmbientColor, nativeVertexOrHemisphereLighting);
    ColorRgba8 diffuseColor = LightColorForVertexHemispherePath(
        lighting, batch.Material, lighting.DiffuseColor, nativeVertexOrHemisphereLighting);
    ColorRgba8 light1Color = LightColorForVertexHemispherePath(
        lighting, batch.Material, lighting.Light1Color, nativeVertexOrHemisphereLighting);
    if (nativeVertexHemisphereTrace) {
        ambientColor = lighting.VertexHemisphereAmbientColor;
        diffuseColor = lighting.VertexHemisphereDiffuseColor;
        light1Color = lighting.VertexHemisphereSecondaryColor;
    }
    if (nativeVertexHemispherePacket) {
        ambientColor = lighting.ActorVsLightPacket.AmbientColor;
        diffuseColor = lighting.ActorVsLightPacket.Diffuse0Color;
        light1Color = lighting.ActorVsLightPacket.Diffuse1Color;
    }
    const bool nativeVertexHemisphereWorldTrace =
        nativeVertexHemisphereTrace && lighting.VertexHemisphereWorldLightVectorDecoded;
    nativeVertexHemisphereDirectionalVectorResolved =
        nativeVertexHemisphereTrace || nativeVertexHemisphereDirectionalVectorResolved;
    const bool nativeActorVsCompactPayloadVector =
        ModelUsesActorVsVertexHemisphereVectorGate(
            lighting, model, nativeVertexOrHemisphereLighting) &&
        lighting.ActorVsLightPacket.CompactPayloadSourceResolved;
    const Vec3f compactSlot0Vector = lighting.ActorVsLightPacket.CompactPayloadSlot0Direction;
    const Vec3f compactSlot1Vector = lighting.ActorVsLightPacket.CompactPayloadSlot1Direction;
    const Vec3f light0Vector =
        nativeActorVsCompactPayloadVector
            ? compactSlot0Vector
            : (nativeVertexHemisphereWorldTrace
                   ? lighting.VertexHemisphereWorldNegatedLightVector
                   : (nativeVertexHemisphereTrace ? lighting.VertexHemisphereNegatedLightVector
                                                  : lighting.Light0Vector));
    const Vec3f light1Vector =
        nativeActorVsCompactPayloadVector
            ? compactSlot1Vector
            : (nativeVertexHemisphereWorldTrace
                   ? lighting.VertexHemisphereWorldLightVector
                   : (nativeVertexHemisphereTrace ? lighting.VertexHemisphereLightVector
                                                  : lighting.Light1Vector));
    evaluation.AmbientColor = ambientColor;
    evaluation.Diffuse0Color = diffuseColor;
    evaluation.Diffuse1Color = light1Color;
    evaluation.Light0Vector = light0Vector;
    evaluation.Light1Vector = light1Vector;
    const bool nativeCmbVShaderLightingAccumulator =
        lighting.CmbVShaderLightingAccumulatorDecoded &&
        batch.Material.VertexLightingEnabled && nativeVertexOrHemisphereLighting;
    const uint32_t activeLightCount = nativeCmbVShaderLightingAccumulator
                                          ? std::min<uint32_t>(
                                                lighting.DirectionalLightCount,
                                                lighting.CmbVShaderLightingAccumulatorSlotCount)
                                          : 0;
    // Static CMB packets repeat environment ambient in every active slot. Actor
    // compact packets carry ambient only in slot 0; slot 1 has zero RGB ambient.
    const uint32_t ambientSlotCount =
        nativeCmbVShaderLightingAccumulator
            ? (nativeActorVsCompactPayloadVector ? std::min<uint32_t>(activeLightCount, 1)
                                                 : activeLightCount)
            : 0;
    ColorRgba8 modulation = CombinePicaLightingVertexModulationColor(
        ambientColor, diffuseColor, batch.Material.EmissionColor, batch.Material.AmbientColor,
        effectiveMaterialDiffuse, lighting.VertexColorFormula);
    if (!lighting.DirectionalFormula.empty() && vertex.NativeNormalAvailable &&
        nativeVertexHemisphereDirectionalVectorResolved) {
        const auto lightingNormal = NativePicaLightingNormalForVertex(model, batch, vertex);
        const bool directionalVectorsUseModelSpace =
            lighting.DirectionalVectorsUseModelSpace || nativeActorVsCompactPayloadVector;
        const auto worldNormal = directionalVectorsUseModelSpace
                                     ? lightingNormal
                                     : TransformDirection(modelToWorld, lightingNormal);
        const double ambientShadowFactor =
            shadowFullPrimaryLightContribution ? primaryShadowFactor : 1.0;
        const double light0Dot = Dot(worldNormal, light0Vector);
        const double light1Dot = Dot(worldNormal, light1Vector);
        const double shadowScale = Clamp01(primaryShadowFactor);
        evaluation.LightingNormal = lightingNormal;
        evaluation.WorldNormal = worldNormal;
        evaluation.Light0Dot = light0Dot;
        evaluation.Light1Dot = light1Dot;
        evaluation.Light0DiffuseScale = Clamp01(light0Dot) * shadowScale;
        evaluation.Light1DiffuseScale = Clamp01(light1Dot) * shadowScale;
        evaluation.DirectionalEvaluated = true;
        modulation = CombinePicaLightingDirectionalModulationColor(
            ambientColor, diffuseColor, light1Color, batch.Material.EmissionColor,
            batch.Material.AmbientColor, effectiveMaterialDiffuse,
            light0Dot, light1Dot,
            ambientShadowFactor, primaryShadowFactor, lighting.DirectionalFormula);
    }
    if (nativeCmbVShaderLightingAccumulator) {
        const double ambientScale = shadowFullPrimaryLightContribution
                                        ? Clamp01(primaryShadowFactor)
                                        : 1.0;
        const uint32_t additionalAmbientSlotCount =
            ambientSlotCount > 0 ? ambientSlotCount - 1 : 0;
        const auto addAmbientSlots = [&](uint8_t current, uint8_t material, uint8_t ambient) {
            const double additional = static_cast<double>(additionalAmbientSlotCount) *
                                      static_cast<double>(material) * ambient *
                                      ambientScale / 255.0;
            return ClampColorInt(static_cast<int>(std::lround(current + additional)));
        };
        modulation.R = addAmbientSlots(
            modulation.R, batch.Material.AmbientColor.R, ambientColor.R);
        modulation.G = addAmbientSlots(
            modulation.G, batch.Material.AmbientColor.G, ambientColor.G);
        modulation.B = addAmbientSlots(
            modulation.B, batch.Material.AmbientColor.B, ambientColor.B);

        double lightAlphaAccumulator = 0.0;
        if (activeLightCount >= 1) {
            lightAlphaAccumulator += static_cast<double>(diffuseColor.A) / 255.0;
        }
        if (activeLightCount >= 2) {
            lightAlphaAccumulator += static_cast<double>(light1Color.A) / 255.0;
        }
        const double alphaScale =
            static_cast<double>(effectiveMaterialDiffuse.A) / 255.0 * lightAlphaAccumulator;
        batch.Material.NativePicaCmbVShaderLightingAccumulatorApplied = true;
        batch.Material.NativePicaCmbVShaderLightingActiveLightCount = activeLightCount;
        batch.Material.NativePicaCmbVShaderLightingAmbientSlotCount = ambientSlotCount;
        batch.Material.NativePicaCmbVShaderLightingAlphaScale = alphaScale;
        batch.Material.NativePicaCmbVShaderLightingAccumulatorSource =
            lighting.CmbVShaderLightingAccumulatorSource;
        batch.Material.NativePicaCmbVShaderLightingAmbientSlotSource =
            nativeActorVsCompactPayloadVector
                ? "oot3d_0045dd50_actor_packet_f82_then_zero_f85"
                : "oot3d_static_cmb_environment_packet_repeats_ambient_per_active_slot";
    }
    evaluation.ModulationColor = modulation;
    evaluation.OutputColor =
        ApplyPicaLightingVertexModulation(evaluation.BaseColor, modulation, lighting.PreserveVertexAlpha);
    if (nativeCmbVShaderLightingAccumulator) {
        const double alpha = static_cast<double>(evaluation.BaseColor.A) *
                             batch.Material.NativePicaCmbVShaderLightingAlphaScale;
        evaluation.OutputColor.A = ClampColorInt(static_cast<int>(std::lround(alpha)));
    }
    if (batch.Material.Textured && batch.Material.TextureEnvProgram.ColorShaderPathApplied) {
        evaluation.OutputColor = ApplyTextureEnvProgramVertexMultiplier(
            evaluation.OutputColor, batch.Material.TextureEnvProgram);
    }
    return evaluation;
}

ColorRgba8 ApplyPicaLightingToVertex(const Oot3dNativePicaLightingRenderState& lighting,
                                     const Oot3dNativeRenderModel& model,
                                     Oot3dNativeRenderBatch& batch,
                                     const Matrix4f& modelToWorld,
                                     const Oot3dNativeRenderVertex& vertex,
                                     bool nativeVertexOrHemisphereLighting,
                                     bool nativeVertexHemispherePacket,
                                     bool nativeVertexHemisphereDirectionalVectorResolved,
                                     ColorRgba8 effectiveMaterialDiffuse,
                                     bool shadowFullPrimaryLightContribution,
                                     double primaryShadowFactor) {
    return EvaluatePicaLightingForVertex(
               lighting, model, batch, modelToWorld, vertex, nativeVertexOrHemisphereLighting,
               nativeVertexHemispherePacket, nativeVertexHemisphereDirectionalVectorResolved,
               effectiveMaterialDiffuse, shadowFullPrimaryLightContribution, primaryShadowFactor)
        .OutputColor;
}

bool TextureEnvConsumesFragmentLightingOutput(
    const Oot3dNativeRenderMaterialState& material) {
    if (material.TextureEnvProgram.UsesFragmentLightingColor) {
        return true;
    }
    for (const auto& stage : material.TextureEnvStages) {
        const size_t activeSourceCount =
            std::min(TextureEnvRgbActiveSourceCount(stage.CombineAlpha), stage.SourceAlpha.size());
        for (size_t sourceIndex = 0; sourceIndex < activeSourceCount; ++sourceIndex) {
            if (stage.SourceAlpha[sourceIndex] == kPicaTextureEnvSourceFragmentPrimaryColor ||
                stage.SourceAlpha[sourceIndex] == kPicaTextureEnvSourceFragmentSecondaryColor) {
                return true;
            }
        }
    }
    return false;
}

void ApplyPicaLightingToModel(const Oot3dNativePicaLightingRenderState& lighting,
                              const Oot3dNativePicaShadowState& shadow,
                              Oot3dNativeRenderModel& model,
                              bool collectDiagnostics = true) {
    if (!lighting.Available) {
        return;
    }
    for (size_t batchIndex = 0; batchIndex < model.Batches.size(); ++batchIndex) {
        auto& batch = model.Batches[batchIndex];
        for (auto& vertex : batch.Vertices) {
            if (vertex.NativePicaLightingInputColorAvailable) {
                vertex.Color = vertex.NativePicaLightingInputColor;
            } else {
                vertex.NativePicaLightingInputColor = vertex.Color;
                vertex.NativePicaLightingInputColorAvailable = true;
            }
        }

        batch.Material.NativePicaLightingApplied = false;
        batch.Material.NativePicaDirectionalLightingApplied = false;
        batch.Material.NativePicaVertexLightingApplied = false;
        batch.Material.NativePicaHemisphereLightingApplied = false;
        batch.Material.NativePicaPrimaryColorPreLightingScaleApplied = false;
        batch.Material.NativePicaSelfShadowCandidate = false;
        batch.Material.NativePicaSelfShadowApplied = false;
        batch.Material.NativePicaSelfShadowVertexCount = 0;
        batch.Material.NativePicaSelfShadowOccludedVertexCount = 0;
        batch.Material.NativePicaVertexLightingDeferred = false;
        batch.Material.NativePicaHemisphereLightingDeferred = false;
        batch.Material.NativePicaVertexHemisphereVectorResolved = false;
        batch.Material.NativePicaVertexHemisphereVectorPending = false;
        batch.Material.NativePicaVertexHemisphereActorVsColorPacketApplied = false;
        batch.Material.NativePicaEffectiveMaterialDiffuseResolved = false;
        batch.Material.NativePicaEffectiveMaterialDiffuseUsesAmbient = false;
        batch.Material.NativePicaBumpNormalMapApplied = false;
        batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting = false;
        batch.Material.NativePicaMaterialLutInputEvaluationApplied = false;
        batch.Material.NativePicaMaterialLutInputEvaluationPending = false;
        batch.Material.NativePicaMaterialLutInputEvaluationRequired = false;
        batch.Material.NativePicaMaterialLutInputEvaluationCulledAsUnused = false;
        batch.Material.NativePicaLightingDiagnostics = {};

        ApplyTextureEnvShaderCoverage(batch.Material);
        batch.Material.NativePicaLightingApplicationSource = lighting.MaterialLightingEnableSource;
        batch.Material.NativePicaMaterialLutInputPacketAvailable =
            batch.Material.MaterialPicaLutInput.Available;
        batch.Material.NativePicaMaterialLutInputPacketComplete =
            batch.Material.MaterialPicaLutInput.Complete;
        const bool nativeVertexOrHemisphereLighting =
            MaterialUsesNativeVertexOrHemisphereLighting(lighting, model, batch);
        const bool nativeVertexHemispherePacket =
            ModelUsesActorVsVertexHemisphereColorPacket(lighting, model, nativeVertexOrHemisphereLighting);
        const bool nativeVertexHemisphereDirectionalVectorResolved =
            NativeVertexHemisphereDirectionalVectorResolved(lighting, model, nativeVertexOrHemisphereLighting);
        const bool nativePicaSelfShadowCandidate =
            BatchCanReceiveNativePicaSelfShadowRoute(shadow, lighting, model, batch);
        const bool nativePicaSelfShadow =
            BatchUsesNativePicaSelfShadow(shadow, lighting, model, batch);
        if (nativePicaSelfShadowCandidate) {
            batch.Material.NativePicaSelfShadowCandidate = true;
            batch.Material.NativePicaSelfShadowApplicationSource = shadow.ShaderRouteApplication;
            batch.Material.NativePicaShadow2dMaterialTextureProjectionInputCandidate =
                shadow.Shadow2dMaterialTextureProjectionInputSupported;
            batch.Material.NativePicaShadow2dMaterialTextureProjectionInputSource =
                shadow.Shadow2dMaterialTextureProjectionInputSource;
            batch.Material.NativePicaShadow2dTexCoord0WInputCandidate =
                shadow.Shadow2dTexCoord0WInputSupported;
            batch.Material.NativePicaShadow2dTexCoord0WInputSource =
                shadow.Shadow2dTexCoord0WInputSource;
            batch.Material.NativePicaShadow2dTexCoord0WInputDecoded =
                shadow.Shadow2dTexCoord0WInputSupported;
            batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded =
                shadow.Shadow2dMaterialTextureProjectionInputSupported &&
                batch.Material.SelectedTextureCoordDecoded &&
                batch.Material.NativeRuntimeMaterialLaneDecoded &&
                batch.Material.NativePicaShadow2dTexCoord0WInputDecoded;
        }
        if (!MaterialUsesNativePicaLighting(lighting, batch.Material) &&
            !nativeVertexOrHemisphereLighting) {
            if (MaterialDefersNativeVertexOrHemisphereLighting(lighting, batch.Material)) {
                batch.Material.NativePicaLightingApplication =
                    "oot3d_cmb_vertex_or_hemisphere_lighting_deferred";
                batch.Material.NativePicaVertexLightingDeferred =
                    batch.Material.VertexLightingEnabled;
                batch.Material.NativePicaHemisphereLightingDeferred =
                    batch.Material.HemisphereLightingEnabled;
            } else {
                batch.Material.NativePicaLightingApplication = "oot3d_cmb_no_fragment_lighting";
            }
            ApplyTextureEnvProgramOnlyToBatch(batch);
            continue;
        }

        batch.Material.NativePicaLightingApplied = true;
        if (nativeVertexOrHemisphereLighting) {
            batch.Material.NativePicaLightingApplication =
                model.TransformBakedIntoVertices
                    ? "oot3d_cmb_static_baked_vertex_or_hemisphere_lighting"
                    : "oot3d_cmb_skeleton_vertex_or_hemisphere_lighting";
            batch.Material.NativePicaLightingApplicationSource =
                lighting.MaterialVertexHemisphereLightingSource;
            batch.Material.NativePicaVertexLightingApplied =
                batch.Material.VertexLightingEnabled;
            batch.Material.NativePicaHemisphereLightingApplied =
                batch.Material.HemisphereLightingEnabled;
            batch.Material.NativePicaVertexHemisphereLightingSource =
                lighting.MaterialVertexHemisphereLightingSource;
            batch.Material.NativePicaVertexHemisphereVectorSource =
                NativeVertexHemisphereVectorSourceForModel(lighting, model, true);
            batch.Material.NativePicaVertexHemisphereActorVsColorPacketApplied =
                nativeVertexHemispherePacket;
            batch.Material.NativePicaVertexHemisphereColorSource =
                NativeVertexHemisphereColorSourceForModel(
                    lighting, model, true, nativeVertexHemispherePacket);
            batch.Material.NativePicaVertexHemisphereAmbientColor =
                NativeVertexHemisphereAmbientColorForBatch(
                    lighting, nativeVertexHemispherePacket);
            batch.Material.NativePicaVertexHemisphereDiffuse0Color =
                NativeVertexHemisphereDiffuse0ColorForBatch(
                    lighting, nativeVertexHemispherePacket);
            batch.Material.NativePicaVertexHemisphereDiffuse1Color =
                NativeVertexHemisphereDiffuse1ColorForBatch(
                    lighting, nativeVertexHemispherePacket);
            batch.Material.NativePicaVertexHemisphereVectorResolved =
                nativeVertexHemisphereDirectionalVectorResolved;
            batch.Material.NativePicaVertexHemisphereVectorPending =
                !batch.Material.NativePicaVertexHemisphereVectorResolved &&
                !lighting.VertexHemisphereRuntimeVectorSource.empty();
        } else {
            batch.Material.NativePicaLightingApplication = "oot3d_cmb_fragment_lighting";
            batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting =
                batch.Material.NativePicaMaterialLutInputPacketComplete;
            batch.Material.NativePicaMaterialLutInputApplicationSource =
                batch.Material.MaterialPicaLutInput.SourceKind;
            batch.Material.NativePicaMaterialLutInputEvaluationRequired =
                batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting &&
                TextureEnvConsumesFragmentLightingOutput(batch.Material);
            batch.Material.NativePicaMaterialLutInputEvaluationCulledAsUnused =
                batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting &&
                !batch.Material.NativePicaMaterialLutInputEvaluationRequired;
            batch.Material.NativePicaMaterialLutInputEvaluationPending =
                batch.Material.NativePicaMaterialLutInputEvaluationRequired;
            if (batch.Material.NativePicaMaterialLutInputEvaluationCulledAsUnused) {
                batch.Material.NativePicaMaterialLutInputApplicationSource +=
                    ";texture_env_fragment_lighting_output_unused";
            }
        }
        const bool batchHasNativeNormal = BatchHasNativeNormal(batch);
        batch.Material.NativePicaDirectionalLightingApplied =
            !lighting.DirectionalFormula.empty() && batchHasNativeNormal &&
            (!nativeVertexOrHemisphereLighting ||
             batch.Material.NativePicaVertexHemisphereVectorResolved);
        if (batch.Material.Textured && lighting.ModulateTexturedBatches) {
            const bool textureEnvProgramSupported =
                batch.Material.TextureEnvProgram.ColorShaderPathSupported;
            batch.Material.VertexColorModulatesTexture =
                textureEnvProgramSupported
                    ? batch.Material.TextureEnvProgram.UsesPrimaryColor
                    : true;
            if (!textureEnvProgramSupported &&
                !batch.Material.TextureEnv.ColorShaderPathSupported &&
                batch.Material.TextureEnv.Decoded) {
                batch.Material.TextureEnvProgram.FallbackTextureLightModulationUsed = true;
                batch.Material.TextureEnv.FallbackTextureLightModulationUsed = true;
                if (!batch.Material.TextureEnvStages.empty()) {
                    batch.Material.TextureEnvStages.front().FallbackTextureLightModulationUsed = true;
                }
            }
        }
        if (nativePicaSelfShadow) {
            batch.Material.NativePicaSelfShadowApplied = true;
            batch.Material.NativePicaSelfShadowVertexCount = batch.Vertices.size();
        }
        const bool shadowFullPrimaryLightContribution =
            nativePicaSelfShadow &&
            nativeVertexOrHemisphereLighting &&
            batch.Material.HemisphereLightingEnabled &&
            shadow.ShadowLightContributionFormula ==
                "pica_shadow_map_multiplies_native_cmb_vertex_hemisphere_material_ambient_light_ambient_plus_material_diffuse_light_diffuse_dot";
        const auto effectiveMaterialDiffuse =
            ResolveNativePicaEffectiveMaterialDiffuse(
                lighting, batch.Material, nativeVertexOrHemisphereLighting);
        batch.Material.NativePicaEffectiveMaterialDiffuseResolved =
            effectiveMaterialDiffuse.Resolved;
        batch.Material.NativePicaEffectiveMaterialDiffuseUsesAmbient =
            effectiveMaterialDiffuse.UsesAmbient;
        batch.Material.NativePicaEffectiveMaterialDiffuseSource =
            effectiveMaterialDiffuse.Source;
        batch.Material.NativePicaEffectiveMaterialDiffuseColor =
            effectiveMaterialDiffuse.Color;
        std::optional<NativePicaLightingBatchDiagnosticsAccumulator> lightingDiagnostics;
        if (collectDiagnostics) {
            lightingDiagnostics.emplace();
            lightingDiagnostics->Diagnostics.SourceKind =
                "oot3d_native_pica_lighting_vertex_evaluation_dump_v1";
            lightingDiagnostics->Diagnostics.Light0VectorSource =
                nativeVertexOrHemisphereLighting
                    ? batch.Material.NativePicaVertexHemisphereVectorSource
                    : lighting.Light0VectorSource;
            lightingDiagnostics->Diagnostics.Light1VectorSource =
                nativeVertexOrHemisphereLighting
                    ? batch.Material.NativePicaVertexHemisphereVectorSource
                    : lighting.Light1VectorSource;
            if (nativeVertexOrHemisphereLighting &&
                lighting.VertexHemisphereUniformTraceDecoded &&
                lighting.VertexHemisphereUniformTraceUsedAsRuntimeSource) {
                lightingDiagnostics->Diagnostics.Light0VectorSource =
                    "native_pica_vsh_uniform_trace_negated_light_vector";
                lightingDiagnostics->Diagnostics.Light1VectorSource =
                    "native_pica_vsh_uniform_trace_light_vector";
            }
            lightingDiagnostics->Diagnostics.MaterialEmissionColor = batch.Material.EmissionColor;
            lightingDiagnostics->Diagnostics.MaterialAmbientColor = batch.Material.AmbientColor;
            lightingDiagnostics->Diagnostics.MaterialDiffuseColor = batch.Material.DiffuseColor;
            lightingDiagnostics->Diagnostics.EffectiveMaterialDiffuseColor =
                effectiveMaterialDiffuse.Color;
        }
        const size_t lightingDiagnosticSampleStride =
            std::max<size_t>(1, batch.Vertices.size() / 32);
        for (size_t vertexIndex = 0; vertexIndex < batch.Vertices.size(); ++vertexIndex) {
            auto& vertex = batch.Vertices[vertexIndex];
            const auto evaluation = EvaluatePicaLightingForVertex(
                lighting, model, batch, model.ModelToWorld, vertex, nativeVertexOrHemisphereLighting,
                nativeVertexHemispherePacket, nativeVertexHemisphereDirectionalVectorResolved,
                effectiveMaterialDiffuse.Color, shadowFullPrimaryLightContribution, 1.0);
            if (lightingDiagnostics.has_value()) {
                if (vertexIndex == 0) {
                    lightingDiagnostics->Diagnostics.Light0Vector = evaluation.Light0Vector;
                    lightingDiagnostics->Diagnostics.Light1Vector = evaluation.Light1Vector;
                    lightingDiagnostics->Diagnostics.AmbientColor = evaluation.AmbientColor;
                    lightingDiagnostics->Diagnostics.Diffuse0Color = evaluation.Diffuse0Color;
                    lightingDiagnostics->Diagnostics.Diffuse1Color = evaluation.Diffuse1Color;
                }
                const bool recordLightingDiagnosticSample =
                    (vertexIndex % lightingDiagnosticSampleStride) == 0 &&
                    lightingDiagnostics->Diagnostics.Samples.size() < 32;
                lightingDiagnostics->Add(vertexIndex, vertex, evaluation,
                                         recordLightingDiagnosticSample);
            }
            vertex.Color = evaluation.OutputColor;
        }
        batch.Material.NativePicaLightingDiagnostics =
            lightingDiagnostics.has_value()
                ? lightingDiagnostics->Finish()
                : Oot3dNativePicaLightingBatchDiagnostics{};
    }
    if (model.NativeVertexDataCacheable && ++model.NativeGeometryContentVersion == 0) {
        model.NativeGeometryContentVersion = 1;
    }
}

void CountPicaLightingModelApplication(Oot3dNativePicaLightingRenderState& lighting,
                                       const Oot3dNativeRenderModel& model) {
    if (!lighting.Available) {
        return;
    }
    for (const auto& batch : model.Batches) {
        if (batch.Material.NativePicaMaterialLutInputPacketAvailable) {
            ++lighting.MaterialLutInputPacketAvailableBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputPacketComplete) {
            ++lighting.MaterialLutInputPacketCompleteBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting) {
            ++lighting.MaterialLutInputFragmentLightingBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputEvaluationApplied) {
            ++lighting.MaterialLutInputEvaluationAppliedBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputEvaluationPending) {
            ++lighting.MaterialLutInputEvaluationPendingBatchCount;
        }
        if (!batch.Material.NativePicaLightingApplied) {
            continue;
        }
        ++lighting.AppliedBatchCount;
        lighting.AppliedVertexCount += batch.Vertices.size();
        if (batch.Material.NativePicaDirectionalLightingApplied) {
            lighting.DirectionalLightingApplied = true;
        }
        if (batch.Material.NativePicaVertexLightingApplied) {
            ++lighting.AppliedVertexLightingBatchCount;
        }
        if (batch.Material.NativePicaHemisphereLightingApplied) {
            ++lighting.AppliedHemisphereLightingBatchCount;
        }
        for (const auto& vertex : batch.Vertices) {
            if (vertex.NativeNormalAvailable) {
                ++lighting.NativeNormalVertexCount;
            }
        }
        if (batch.Material.Textured && batch.Material.VertexColorModulatesTexture) {
            ++lighting.AppliedTexturedBatchCount;
        }
    }
}

void CountPicaSelfShadowModelApplication(Oot3dNativePicaShadowState& shadow,
                                         const Oot3dNativeRenderModel& model) {
    if (!shadow.Available) {
        return;
    }
    for (const auto& batch : model.Batches) {
        if (batch.Material.NativePicaSelfShadowCandidate) {
            ++shadow.CandidateBatchCount;
            if (batch.Material.NativePicaShadow2dMaterialTextureProjectionInputCandidate) {
                ++shadow.MaterialTextureProjectionCandidateBatchCount;
            }
            if (batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded) {
                ++shadow.MaterialTextureProjectionDecodedBatchCount;
            }
            if (batch.Material.NativePicaShadow2dTexCoord0WInputCandidate) {
                ++shadow.TexCoord0WInputCandidateBatchCount;
            }
            if (batch.Material.NativePicaShadow2dTexCoord0WInputDecoded) {
                ++shadow.TexCoord0WInputDecodedBatchCount;
            }
            if (batch.Material.NativeRuntimeMaterialLaneDecoded) {
                ++shadow.NativeMaterialLaneDecodedBatchCount;
            }
        }
        if (!batch.Material.NativePicaSelfShadowApplied) {
            continue;
        }
        ++shadow.AppliedBatchCount;
        shadow.AppliedVertexCount += batch.Material.NativePicaSelfShadowVertexCount;
        shadow.OccludedVertexCount += batch.Material.NativePicaSelfShadowOccludedVertexCount;
    }
}

void ApplyPicaLightingDebugToModel(const Oot3dNativePicaLightingDebugState& debug,
                                   Oot3dNativeRenderModel& model) {
    if (!debug.Available) {
        return;
    }
    for (auto& batch : model.Batches) {
        if (debug.ForceUntexturedBatches) {
            batch.Material.Textured = false;
            batch.Material.TextureIndex = -1;
            batch.Material.TextureMapperSlot = 0;
            batch.Material.TextureHasNativeAlpha = false;
        }
        for (auto& vertex : batch.Vertices) {
            const auto alpha = debug.PreserveVertexAlpha ? vertex.Color.A : debug.DebugColor.A;
            vertex.Color = debug.DebugColor;
            vertex.Color.A = alpha;
        }
    }
}

void CountPicaLightingDebugModelApplication(Oot3dNativePicaLightingDebugState& debug,
                                            const Oot3dNativeRenderModel& model) {
    if (!debug.Available) {
        return;
    }
    debug.AppliedBatchCount += model.Batches.size();
    debug.AppliedVertexCount += model.VertexCount();
}

nlohmann::json ColorJson(ColorRgba8 color) {
    return {
        { "r", static_cast<int>(color.R) },
        { "g", static_cast<int>(color.G) },
        { "b", static_cast<int>(color.B) },
        { "a", static_cast<int>(color.A) },
    };
}

struct ColorRgba8Stats {
    bool Available = false;
    size_t Count = 0;
    ColorRgba8 Min = { 0, 0, 0, 0 };
    ColorRgba8 Max = { 0, 0, 0, 0 };
    ColorRgba8 Average = { 0, 0, 0, 0 };
};

nlohmann::json ColorStatsJson(const ColorRgba8Stats& stats) {
    return {
        { "available", stats.Available },
        { "count", stats.Count },
        { "min", ColorJson(stats.Min) },
        { "max", ColorJson(stats.Max) },
        { "average", ColorJson(stats.Average) },
    };
}

nlohmann::json Vec3Json(Vec3f value) {
    return {
        { "x", value.X },
        { "y", value.Y },
        { "z", value.Z },
    };
}

nlohmann::json Vec2Json(Vec2f value) {
    return {
        { "x", value.X },
        { "y", value.Y },
    };
}

nlohmann::json PicaScalarStatsJson(const Oot3dNativePicaScalarStats& stats) {
    return {
        { "available", stats.Available },
        { "count", stats.Count },
        { "min", stats.Min },
        { "max", stats.Max },
        { "average", stats.Average },
        { "negative_count", stats.NegativeCount },
        { "zero_count", stats.ZeroCount },
        { "positive_count", stats.PositiveCount },
    };
}

nlohmann::json PicaColorStatsJson(const Oot3dNativePicaColorStats& stats) {
    return {
        { "available", stats.Available },
        { "count", stats.Count },
        { "min", ColorJson(stats.Min) },
        { "max", ColorJson(stats.Max) },
        { "average", ColorJson(stats.Average) },
    };
}

nlohmann::json PicaLightingVertexDiagnosticSamplesJson(
    const std::vector<Oot3dNativePicaLightingVertexDiagnosticSample>& samples) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& sample : samples) {
        out.push_back({
            { "vertex_index", sample.VertexIndex },
            { "position", Vec3Json(sample.Position) },
            { "lighting_normal", Vec3Json(sample.LightingNormal) },
            { "world_normal", Vec3Json(sample.WorldNormal) },
            { "light0_dot", sample.Light0Dot },
            { "light1_dot", sample.Light1Dot },
            { "light0_diffuse_scale", sample.Light0DiffuseScale },
            { "light1_diffuse_scale", sample.Light1DiffuseScale },
            { "base_color", ColorJson(sample.BaseColor) },
            { "modulation_color", ColorJson(sample.ModulationColor) },
            { "output_color", ColorJson(sample.OutputColor) },
        });
    }
    return out;
}

nlohmann::json PicaLightingBatchDiagnosticsJson(
    const Oot3dNativePicaLightingBatchDiagnostics& diagnostics) {
    return {
        { "available", diagnostics.Available },
        { "source_kind", diagnostics.SourceKind },
        { "vertex_count", diagnostics.VertexCount },
        { "native_normal_vertex_count", diagnostics.NativeNormalVertexCount },
        { "directional_evaluated_vertex_count", diagnostics.DirectionalEvaluatedVertexCount },
        { "light0_vector_source", diagnostics.Light0VectorSource },
        { "light1_vector_source", diagnostics.Light1VectorSource },
        { "light0_vector", Vec3Json(diagnostics.Light0Vector) },
        { "light1_vector", Vec3Json(diagnostics.Light1Vector) },
        { "average_lighting_normal", Vec3Json(diagnostics.AverageLightingNormal) },
        { "average_world_normal", Vec3Json(diagnostics.AverageWorldNormal) },
        { "ambient_color", ColorJson(diagnostics.AmbientColor) },
        { "diffuse0_color", ColorJson(diagnostics.Diffuse0Color) },
        { "diffuse1_color", ColorJson(diagnostics.Diffuse1Color) },
        { "material_emission_color", ColorJson(diagnostics.MaterialEmissionColor) },
        { "material_ambient_color", ColorJson(diagnostics.MaterialAmbientColor) },
        { "material_diffuse_color", ColorJson(diagnostics.MaterialDiffuseColor) },
        { "effective_material_diffuse_color",
          ColorJson(diagnostics.EffectiveMaterialDiffuseColor) },
        { "light0_dot_stats", PicaScalarStatsJson(diagnostics.Light0DotStats) },
        { "light0_diffuse_scale_stats",
          PicaScalarStatsJson(diagnostics.Light0DiffuseScaleStats) },
        { "light0_opposite_dot_stats",
          PicaScalarStatsJson(diagnostics.Light0OppositeDotStats) },
        { "light0_opposite_diffuse_scale_stats",
          PicaScalarStatsJson(diagnostics.Light0OppositeDiffuseScaleStats) },
        { "light1_dot_stats", PicaScalarStatsJson(diagnostics.Light1DotStats) },
        { "light1_diffuse_scale_stats",
          PicaScalarStatsJson(diagnostics.Light1DiffuseScaleStats) },
        { "base_color_stats", PicaColorStatsJson(diagnostics.BaseColorStats) },
        { "modulation_color_stats", PicaColorStatsJson(diagnostics.ModulationColorStats) },
        { "output_color_stats", PicaColorStatsJson(diagnostics.OutputColorStats) },
        { "samples", PicaLightingVertexDiagnosticSamplesJson(diagnostics.Samples) },
    };
}

nlohmann::json RawTextureStageSlotsJson(
    const std::array<int16_t, kOot3dCmbMaterialTextureEnvStageCount>& slots) {
    nlohmann::json out = nlohmann::json::array();
    for (const int16_t slot : slots) {
        out.push_back(static_cast<int>(slot));
    }
    return out;
}

nlohmann::json TextureEnvStageResolvedJson(
    const std::array<bool, kOot3dCmbMaterialTextureEnvStageCount>& resolved) {
    nlohmann::json out = nlohmann::json::array();
    for (const bool value : resolved) {
        out.push_back(value);
    }
    return out;
}

nlohmann::json TextureEnvStageSourceOffsetsJson(
    const std::array<uint32_t, kOot3dCmbMaterialTextureEnvStageCount>& offsets,
    const std::array<bool, kOot3dCmbMaterialTextureEnvStageCount>& resolved) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t index = 0; index < offsets.size(); ++index) {
        if (resolved[index]) {
            out.push_back(offsets[index]);
        } else {
            out.push_back(nullptr);
        }
    }
    return out;
}

std::string HexU32(uint32_t value, int width);

nlohmann::json TextureMapperTextureIndicesJson(const std::array<int32_t, 3>& indices) {
    nlohmann::json out = nlohmann::json::array();
    for (const int32_t index : indices) {
        out.push_back(index);
    }
    return out;
}

nlohmann::json TextureMapperSamplerStatesJson(
    const std::array<Oot3dNativeRenderSamplerState, 3>& samplers) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& sampler : samplers) {
        out.push_back({
            { "decoded", sampler.Decoded },
            { "min_filter", HexU32(sampler.MinFilter, 4) },
            { "mag_filter", HexU32(sampler.MagFilter, 4) },
            { "wrap_s", HexU32(sampler.WrapS, 4) },
            { "wrap_t", HexU32(sampler.WrapT, 4) },
            { "lod_bias", sampler.LodBias },
            { "source", sampler.Source },
        });
    }
    return out;
}

nlohmann::json ConstantColorsJson(
    const std::array<ColorRgba8, kOot3dCmbMaterialConstantColorCount>& colors) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto color : colors) {
        out.push_back(ColorJson(color));
    }
    return out;
}

ColorRgba8 AverageTextureColor(const Oot3dNativeRenderTexture& texture) {
    if (!texture.Rgba8Decoded || texture.Rgba8.size() < 4) {
        return { 0, 0, 0, 0 };
    }
    uint64_t r = 0;
    uint64_t g = 0;
    uint64_t b = 0;
    uint64_t a = 0;
    const size_t pixelCount = texture.Rgba8.size() / 4;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t offset = pixel * 4;
        r += texture.Rgba8[offset + 0];
        g += texture.Rgba8[offset + 1];
        b += texture.Rgba8[offset + 2];
        a += texture.Rgba8[offset + 3];
    }
    return {
        static_cast<uint8_t>(r / pixelCount),
        static_cast<uint8_t>(g / pixelCount),
        static_cast<uint8_t>(b / pixelCount),
        static_cast<uint8_t>(a / pixelCount),
    };
}

nlohmann::json NativeTextureJson(const Oot3dNativeRenderTexture& texture) {
    nlohmann::json mipLevels = nlohmann::json::array();
    for (const auto& mip : texture.AdditionalMipLevels) {
        mipLevels.push_back({
            { "level", mip.Level },
            { "width", mip.Width },
            { "height", mip.Height },
            { "rgba8_decoded", mip.Rgba8Decoded },
            { "rgba8_byte_count", mip.Rgba8.size() },
        });
    }
    return {
        { "index", texture.SourceIndex },
        { "name", texture.Name },
        { "width", texture.Width },
        { "height", texture.Height },
        { "texture_format", texture.TextureFormat },
        { "data_type", texture.DataType },
        { "mipmap_count", texture.MipmapCount },
        { "mipmap_layout_decoded", texture.MipmapLayoutDecoded },
        { "additional_mip_levels", std::move(mipLevels) },
        { "has_native_alpha", texture.HasNativeAlpha },
        { "rgba8_decoded", texture.Rgba8Decoded },
        { "rgba8_byte_count", texture.Rgba8.size() },
        { "mip_chain_rgba8_byte_count", texture.Rgba8ByteCount },
        { "average_rgba8", ColorJson(AverageTextureColor(texture)) },
    };
}

nlohmann::json NativeTexturesJson(const std::vector<Oot3dNativeRenderTexture>& textures) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& texture : textures) {
        out.push_back(NativeTextureJson(texture));
    }
    return out;
}

ColorRgba8 ApplyPrimaryColorMultiplierEstimate(ColorRgba8 color,
                                               const Oot3dNativeRenderTextureEnvProgram& program) {
    return {
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(color.R) *
                                                   static_cast<double>(program.PrimaryColorMultiplier.X)))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(color.G) *
                                                   static_cast<double>(program.PrimaryColorMultiplier.Y)))),
        ClampColorInt(static_cast<int>(std::lround(static_cast<double>(color.B) *
                                                   static_cast<double>(program.PrimaryColorMultiplier.Z)))),
        color.A,
    };
}

ColorRgba8Stats BatchVertexColorStats(const Oot3dNativeRenderBatch& batch, bool applyPrimaryColorMultiplier) {
    ColorRgba8Stats stats;
    if (batch.Vertices.empty()) {
        return stats;
    }

    uint64_t r = 0;
    uint64_t g = 0;
    uint64_t b = 0;
    uint64_t a = 0;
    stats.Available = true;
    stats.Count = batch.Vertices.size();
    stats.Min = { 255, 255, 255, 255 };
    stats.Max = { 0, 0, 0, 0 };
    for (const auto& vertex : batch.Vertices) {
        const ColorRgba8 color = applyPrimaryColorMultiplier
                                     ? ApplyPrimaryColorMultiplierEstimate(vertex.Color,
                                                                          batch.Material.TextureEnvProgram)
                                     : vertex.Color;
        stats.Min.R = std::min(stats.Min.R, color.R);
        stats.Min.G = std::min(stats.Min.G, color.G);
        stats.Min.B = std::min(stats.Min.B, color.B);
        stats.Min.A = std::min(stats.Min.A, color.A);
        stats.Max.R = std::max(stats.Max.R, color.R);
        stats.Max.G = std::max(stats.Max.G, color.G);
        stats.Max.B = std::max(stats.Max.B, color.B);
        stats.Max.A = std::max(stats.Max.A, color.A);
        r += color.R;
        g += color.G;
        b += color.B;
        a += color.A;
    }
    stats.Average = {
        static_cast<uint8_t>(r / stats.Count),
        static_cast<uint8_t>(g / stats.Count),
        static_cast<uint8_t>(b / stats.Count),
        static_cast<uint8_t>(a / stats.Count),
    };
    return stats;
}

nlohmann::json NativeBatchTextureJson(const Oot3dNativeRenderModel& model,
                                      const Oot3dNativeRenderBatch& batch) {
    if (batch.Material.TextureIndex < 0 ||
        static_cast<size_t>(batch.Material.TextureIndex) >= model.Textures.size()) {
        return {
            { "resolved", false },
            { "texture_index", batch.Material.TextureIndex },
            { "texture_mapper_slot", batch.Material.TextureMapperSlot },
        };
    }

    auto out = NativeTextureJson(model.Textures[static_cast<size_t>(batch.Material.TextureIndex)]);
    out["resolved"] = true;
    out["texture_index"] = batch.Material.TextureIndex;
    out["texture_mapper_slot"] = batch.Material.TextureMapperSlot;
    return out;
}

nlohmann::json NativeBatchSecondaryTextureJson(const Oot3dNativeRenderModel& model,
                                               const Oot3dNativeRenderBatch& batch) {
    if (batch.Material.SecondaryTextureIndex < 0 ||
        static_cast<size_t>(batch.Material.SecondaryTextureIndex) >= model.Textures.size()) {
        return {
            { "resolved", false },
            { "texture_index", batch.Material.SecondaryTextureIndex },
            { "texture_mapper_slot", batch.Material.SecondaryTextureMapperSlot },
            { "binding_source", batch.Material.SecondaryTextureBindingSource },
        };
    }

    auto out = NativeTextureJson(model.Textures[static_cast<size_t>(batch.Material.SecondaryTextureIndex)]);
    out["resolved"] = true;
    out["texture_index"] = batch.Material.SecondaryTextureIndex;
    out["texture_mapper_slot"] = batch.Material.SecondaryTextureMapperSlot;
    out["binding_source"] = batch.Material.SecondaryTextureBindingSource;
    return out;
}

nlohmann::json NativeBatchTertiaryTextureJson(const Oot3dNativeRenderModel& model,
                                              const Oot3dNativeRenderBatch& batch) {
    if (batch.Material.TertiaryTextureIndex < 0 ||
        static_cast<size_t>(batch.Material.TertiaryTextureIndex) >= model.Textures.size()) {
        return {
            { "resolved", false },
            { "texture_index", batch.Material.TertiaryTextureIndex },
            { "texture_mapper_slot", batch.Material.TertiaryTextureMapperSlot },
            { "binding_source", batch.Material.TertiaryTextureBindingSource },
        };
    }

    auto out = NativeTextureJson(model.Textures[static_cast<size_t>(batch.Material.TertiaryTextureIndex)]);
    out["resolved"] = true;
    out["texture_index"] = batch.Material.TertiaryTextureIndex;
    out["texture_mapper_slot"] = batch.Material.TertiaryTextureMapperSlot;
    out["binding_source"] = batch.Material.TertiaryTextureBindingSource;
    return out;
}

ColorRgba8 AverageModelVertexColor(const Oot3dNativeRenderModel& model) {
    uint64_t r = 0;
    uint64_t g = 0;
    uint64_t b = 0;
    uint64_t a = 0;
    size_t count = 0;
    for (const auto& batch : model.Batches) {
        for (const auto& vertex : batch.Vertices) {
            r += vertex.Color.R;
            g += vertex.Color.G;
            b += vertex.Color.B;
            a += vertex.Color.A;
            ++count;
        }
    }
    if (count == 0) {
        return { 0, 0, 0, 0 };
    }
    return {
        static_cast<uint8_t>(r / count),
        static_cast<uint8_t>(g / count),
        static_cast<uint8_t>(b / count),
        static_cast<uint8_t>(a / count),
    };
}

nlohmann::json MaterialColorsJson(const Oot3dNativeRenderMaterialState& material) {
    return {
        { "emission", ColorJson(material.EmissionColor) },
        { "ambient", ColorJson(material.AmbientColor) },
        { "diffuse", ColorJson(material.DiffuseColor) },
        { "specular0", ColorJson(material.Specular0Color) },
        { "specular1", ColorJson(material.Specular1Color) },
        { "constant", ConstantColorsJson(material.ConstantColors) },
    };
}

std::string HexU32(uint32_t value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

std::string HexU16(uint16_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(4) << value;
    return stream.str();
}

std::string HexU8(uint8_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
           << static_cast<uint32_t>(value);
    return stream.str();
}

std::string PicaTextureEnvCombineName(uint16_t value) {
    switch (value) {
        case kPicaTextureEnvCombineReplace:
            return "replace";
        case 0x2100:
            return "modulate";
        case 0x0104:
            return "add";
        case 0x8574:
            return "add_signed";
        case 0x8575:
            return "interpolate";
        case 0x84E7:
            return "subtract";
        case 0x86AE:
            return "dot3_rgb";
        case 0x86AF:
            return "dot3_rgba";
        case 0x6401:
            return "mult_add";
        case 0x6402:
            return "add_mult";
        default:
            return "unknown";
    }
}

std::string PicaTextureEnvSourceName(uint16_t value) {
    switch (value) {
        case 0x8577:
            return "primary_color";
        case 0x6210:
            return "fragment_primary_color_dmp";
        case 0x6211:
            return "fragment_secondary_color_dmp";
        case 0x84C0:
            return "texture0";
        case 0x84C1:
            return "texture1";
        case 0x84C2:
            return "texture2";
        case 0x84C3:
            return "texture3";
        case 0x8579:
            return "previous_buffer_dmp";
        case 0x8576:
            return "constant";
        case 0x8578:
            return "previous";
        default:
            return "unknown";
    }
}

std::string PicaTextureEnvOperandRgbName(uint16_t value) {
    switch (value) {
        case 0x0300:
            return "src_color";
        case 0x0301:
            return "one_minus_src_color";
        case 0x0302:
            return "src_alpha";
        case 0x0303:
            return "one_minus_src_alpha";
        case 0x8580:
            return "src_r_dmp";
        case 0x8583:
            return "one_minus_src_r_dmp";
        case 0x8581:
            return "src_g_dmp";
        case 0x8584:
            return "one_minus_src_g_dmp";
        case 0x8582:
            return "src_b_dmp";
        case 0x8585:
            return "one_minus_src_b_dmp";
        default:
            return "unknown";
    }
}

std::string PicaTextureEnvOperandAlphaName(uint16_t value) {
    switch (value) {
        case 0x0302:
            return "src_alpha";
        case 0x0303:
            return "one_minus_src_alpha";
        case 0x8580:
            return "src_r_dmp";
        case 0x8583:
            return "one_minus_src_r_dmp";
        case 0x8581:
            return "src_g_dmp";
        case 0x8584:
            return "one_minus_src_g_dmp";
        case 0x8582:
            return "src_b_dmp";
        case 0x8585:
            return "one_minus_src_b_dmp";
        default:
            return "unknown";
    }
}

std::string NativeCompareFunctionName(uint16_t value) {
    switch (value) {
        case 0x0200:
            return "never";
        case 0x0201:
            return "less";
        case 0x0202:
            return "equal";
        case 0x0203:
            return "lequal";
        case 0x0204:
            return "greater";
        case 0x0205:
            return "notequal";
        case 0x0206:
            return "gequal";
        case 0x0207:
            return "always";
        default:
            return "unknown";
    }
}

std::string NativeBlendModeName(uint32_t value) {
    if (value == 0) {
        return "disabled";
    }
    if (value == 1) {
        return "enabled";
    }
    return "unknown_nonzero";
}

std::string NativeBlendFactorName(uint16_t value) {
    switch (value) {
        case 0x0000:
            return "zero";
        case 0x0001:
            return "one";
        case 0x0300:
            return "src_color";
        case 0x0301:
            return "one_minus_src_color";
        case 0x0302:
            return "src_alpha";
        case 0x0303:
            return "one_minus_src_alpha";
        case 0x0304:
            return "dst_alpha";
        case 0x0305:
            return "one_minus_dst_alpha";
        case 0x0306:
            return "dst_color";
        case 0x0307:
            return "one_minus_dst_color";
        case 0x0308:
            return "src_alpha_saturate";
        case 0x8001:
            return "constant_color";
        case 0x8002:
            return "one_minus_constant_color";
        case 0x8003:
            return "constant_alpha";
        case 0x8004:
            return "one_minus_constant_alpha";
        default:
            return "unknown";
    }
}

std::string NativeBlendEquationName(uint16_t value) {
    switch (value) {
        case 0x8006:
            return "func_add";
        case 0x8007:
            return "min";
        case 0x8008:
            return "max";
        case 0x800A:
            return "func_subtract";
        case 0x800B:
            return "func_reverse_subtract";
        default:
            return "unknown";
    }
}

nlohmann::json U16ArrayJson(const std::array<uint16_t, 2>& values) {
    return nlohmann::json::array({ HexU16(values[0]), HexU16(values[1]) });
}

nlohmann::json U16ArrayJson(const std::array<uint16_t, 3>& values) {
    return nlohmann::json::array({ HexU16(values[0]), HexU16(values[1]), HexU16(values[2]) });
}

nlohmann::json TextureEnvSourceArrayJson(const std::array<uint16_t, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const uint16_t value : values) {
        out.push_back({ { "raw", HexU16(value) }, { "name", PicaTextureEnvSourceName(value) } });
    }
    return out;
}

nlohmann::json TextureEnvOperandRgbArrayJson(const std::array<uint16_t, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const uint16_t value : values) {
        out.push_back({ { "raw", HexU16(value) }, { "name", PicaTextureEnvOperandRgbName(value) } });
    }
    return out;
}

nlohmann::json TextureEnvOperandAlphaArrayJson(const std::array<uint16_t, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const uint16_t value : values) {
        out.push_back({ { "raw", HexU16(value) }, { "name", PicaTextureEnvOperandAlphaName(value) } });
    }
    return out;
}

nlohmann::json RawMaterialWordsJson(const std::vector<Oot3dNativeRawMaterialWord>& words) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& word : words) {
        out.push_back({
            { "offset", HexU32(word.Offset, 3) },
            { "u32", HexU32(word.Value, 8) },
        });
    }
    return out;
}

nlohmann::json PostMaterialTextureEnvTableJson(const Oot3dNativeRenderMaterialState& material) {
    return {
        { "decoded", material.PostMaterialTextureEnvTableDecoded },
        { "derived_from_lane_pointer", material.PostMaterialTextureEnvTableDerivedFromLanePointer },
        { "source", material.PostMaterialTextureEnvTableSource },
        { "table_source_offset", material.PostMaterialTextureEnvTableSourceOffset },
        { "record_size", material.PostMaterialTextureEnvTableRecordSize },
        { "record_count", material.PostMaterialTextureEnvTableRecordCount },
        { "stage_resolved", TextureEnvStageResolvedJson(material.PostMaterialTextureEnvStageResolved) },
        { "stage_source_offsets",
          TextureEnvStageSourceOffsetsJson(material.PostMaterialTextureEnvStageSourceOffsets,
                                           material.PostMaterialTextureEnvStageResolved) },
    };
}

nlohmann::json TextureEnvStateJson(const Oot3dNativeRenderTextureEnvState& textureEnv) {
    return {
        { "table_index", textureEnv.TableIndex },
        { "source_offset", textureEnv.SourceOffset },
        { "stage_order", textureEnv.StageOrder },
        { "decoded", textureEnv.Decoded },
        { "raw_size", textureEnv.RawSize },
        { "raw_fnv1a64", std::to_string(textureEnv.RawFnv1a64) },
        { "color_shader_path", textureEnv.ColorShaderPath },
        { "color_shader_path_supported", textureEnv.ColorShaderPathSupported },
        { "color_shader_path_applied", textureEnv.ColorShaderPathApplied },
        { "fallback_texture_light_modulation_used", textureEnv.FallbackTextureLightModulationUsed },
        { "combine_rgb", { { "raw", HexU16(textureEnv.CombineRgb) },
                           { "name", PicaTextureEnvCombineName(textureEnv.CombineRgb) } } },
        { "combine_alpha", { { "raw", HexU16(textureEnv.CombineAlpha) },
                             { "name", PicaTextureEnvCombineName(textureEnv.CombineAlpha) } } },
        { "rgb_active_source_count", textureEnv.RgbActiveSourceCount },
        { "color_scale", textureEnv.ColorScale },
        { "alpha_scale", textureEnv.AlphaScale },
        { "color_scale_multiplier", textureEnv.ColorScaleMultiplier },
        { "alpha_scale_multiplier", textureEnv.AlphaScaleMultiplier },
        { "color_scale_supported", textureEnv.ColorScaleSupported },
        { "alpha_scale_supported", textureEnv.AlphaScaleSupported },
        { "unknown_ushort1", U16ArrayJson(textureEnv.UnknownUshort1) },
        { "unknown_gl_constant", U16ArrayJson(textureEnv.UnknownGlConstant) },
        { "source_rgb", TextureEnvSourceArrayJson(textureEnv.SourceRgb) },
        { "operand_rgb", TextureEnvOperandRgbArrayJson(textureEnv.OperandRgb) },
        { "source_alpha", TextureEnvSourceArrayJson(textureEnv.SourceAlpha) },
        { "operand_alpha", TextureEnvOperandAlphaArrayJson(textureEnv.OperandAlpha) },
        { "unknown_ushort2", U16ArrayJson(textureEnv.UnknownUshort2) },
        { "constant_color_index", textureEnv.ConstantColorIndex },
        { "constant_color_index_recognized", textureEnv.ConstantColorIndexRecognized },
        { "requires_shader_evaluation", textureEnv.RequiresShaderEvaluation },
    };
}

nlohmann::json TextureEnvStagesJson(const std::vector<Oot3dNativeRenderTextureEnvState>& stages) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& stage : stages) {
        out.push_back(TextureEnvStateJson(stage));
    }
    return out;
}

nlohmann::json TextureCoordStateJson(const Oot3dNativeRenderTextureCoordState& coord) {
    return {
        { "slot", coord.Slot },
        { "active", coord.Active },
        { "selected_primary", coord.SelectedPrimary },
        { "decoded", coord.Decoded },
        { "matrix_mode", coord.MatrixMode },
        { "reference_camera", coord.ReferenceCamera },
        { "mapping_method", coord.MappingMethod },
        { "coordinate_index", coord.CoordinateIndex },
        { "scale", Vec2Json(coord.Scale) },
        { "rotation", coord.Rotation },
        { "translation", Vec2Json(coord.Translation) },
        { "transform_applies_to_uv0", coord.TransformAppliesToUv0 },
        { "transformed", coord.Transformed },
        { "source", coord.Source },
        { "native_material_animation_applied", coord.NativeMaterialAnimationApplied },
        { "native_material_animation_source", coord.NativeMaterialAnimationSource },
        { "native_material_animation_kind", coord.NativeMaterialAnimationKind },
        { "native_material_animation_component_mask", coord.NativeMaterialAnimationComponentMask },
        { "native_material_animation_sample_frame", coord.NativeMaterialAnimationSampleFrame },
    };
}

nlohmann::json TextureCoordStatesJson(const std::vector<Oot3dNativeRenderTextureCoordState>& coords) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& coord : coords) {
        out.push_back(TextureCoordStateJson(coord));
    }
    return out;
}

nlohmann::json MaterialLightingBlockU16FieldJson(const CmbMaterialLightingBlock& block,
                                                 uint32_t localOffset,
                                                 uint16_t nativeValue,
                                                 uint32_t decoded,
                                                 bool recognized,
                                                 std::optional<uint32_t> runtimeLaneByteOffset = std::nullopt) {
    auto field = nlohmann::json{
        { "source_local_offset", HexU32(localOffset, 2) },
        { "material_offset", HexU32(block.SourceOffset + localOffset, 3) },
        { "native", HexU16(nativeValue) },
        { "decoded", decoded },
        { "recognized", recognized },
    };
    if (runtimeLaneByteOffset.has_value()) {
        field["runtime_lane_byte_offset"] = HexU32(*runtimeLaneByteOffset, 3);
        field["runtime_subblock_byte_offset"] =
            HexU32(*runtimeLaneByteOffset -
                       NativeKankyoRuntimeBridgeLayout()
                           .DrawHandleSubmit.MaterialLaneCopiedBlockDestinationOffset,
                   3);
        field["runtime_lane_populate_source"] = "codebin_004c6364_material_lighting_block_copy";
    }
    return field;
}

nlohmann::json MaterialLightingBlockFlagFieldJson(const CmbMaterialLightingBlock& block,
                                                  uint32_t localOffset,
                                                  uint8_t rawValue,
                                                  bool enabled,
                                                  uint32_t runtimeLaneByteOffset) {
    return {
        { "source_local_offset", HexU32(localOffset, 2) },
        { "material_offset", HexU32(block.SourceOffset + localOffset, 3) },
        { "runtime_lane_byte_offset", HexU32(runtimeLaneByteOffset, 3) },
        { "runtime_subblock_byte_offset",
          HexU32(runtimeLaneByteOffset -
                     NativeKankyoRuntimeBridgeLayout()
                         .DrawHandleSubmit.MaterialLaneCopiedBlockDestinationOffset,
                 3) },
        { "runtime_lane_populate_source", "codebin_004c6364_material_lighting_block_copy" },
        { "raw", HexU8(rawValue) },
        { "enabled", enabled },
    };
}

nlohmann::json MaterialLightingBlockPayload3ScaleGateFlagJson(
    const CmbMaterialLightingBlock& block, const NativeKankyoRuntimeBridgeContract& bridgeContract) {
    const auto& submit = bridgeContract.DrawHandleSubmit;
    const auto& pack = bridgeContract.RuntimeLightPacketPack;
    auto field = MaterialLightingBlockFlagFieldJson(
        block, submit.MaterialLaneCopiedBlockFlag5SourceLocalOffset, block.Flag5Raw, block.Flag5,
        submit.MaterialLaneCopiedBlockFlag5ByteOffset);
    field["semantic_resolved"] = pack.RuntimePayload3ScaleGateSemanticResolved;
    field["semantic"] = pack.RuntimePayload3ScaleGateSemantic;
    field["consumer_address"] = HexU32(pack.RuntimePayload3ScaleGateConsumerAddress, 8);
    field["final_upload_helper_address"] =
        HexU32(pack.RuntimePayload3ScaleGateFinalUploadAddress, 8);
    field["descriptor_payload_3_scale_offsets"] = pack.DescriptorPayload3ScaleOffsets;
    field["behavior_when_disabled"] =
        "payload3_rgb_scaled_by_material_descriptor_offsets_0x0b0_0x0b2";
    field["behavior_when_enabled"] =
        "payload3_rgb_uses_runtime_light_values_without_payload3_material_scale";
    field["source"] = "codebin_003fa5d0_runtime_light_packet_pack";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutInputAbsD0Json(
    const CmbMaterialLightingBlock& block, const NativeKankyoDrawHandleSubmitContract& submit) {
    auto field = MaterialLightingBlockU16FieldJson(
        block, submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SourceLocalOffset,
        block.PicaLutInputAbsD0Raw, block.PicaLutInputAbsD0Selector,
        block.PicaLutInputAbsD0SelectorRecognized,
        submit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset);
    field["semantic_resolved"] = submit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved;
    field["register"] = HexU32(submit.MaterialLaneCopiedBlockPicaLutInputAbsD0Register, 3);
    field["register_name"] = submit.MaterialLaneCopiedBlockPicaLutInputAbsD0RegisterName;
    field["field_name"] = submit.MaterialLaneCopiedBlockPicaLutInputAbsD0FieldName;
    field["sampler"] = submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SamplerName;
    field["bit_shift"] = submit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift;
    field["pica_disable_bit"] = block.PicaLutInputAbsD0DisableBit;
    field["pica_disable_bit_resolved"] = block.PicaLutInputAbsD0DisableBitResolved;
    field["native_selector_disable_bit_by_decoded_value"] =
        submit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector;
    field["packing_rule"] = submit.MaterialLaneCopiedBlockPicaLutInputAbsD0PackingRule;
    field["consumer_address"] =
        HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8);
    field["consumer_source"] = "codebin_0040d040_lighting_lutinput_abs_emit";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutInputAbsFlagJson(
    const CmbMaterialLightingBlock& block, uint32_t localOffset, uint8_t rawValue, bool enabled,
    uint32_t runtimeLaneByteOffset, uint32_t samplerIndex, uint32_t disableBit,
    bool disableBitResolved, const NativeKankyoDrawHandleSubmitContract& submit) {
    auto field = MaterialLightingBlockFlagFieldJson(
        block, localOffset, rawValue, enabled, runtimeLaneByteOffset);
    const auto sampler = submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[samplerIndex];
    field["semantic_resolved"] = disableBitResolved;
    field["register"] = HexU32(submit.MaterialLaneCopiedBlockPicaConfigRegisters[0], 3);
    field["register_name"] = submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[0];
    field["field_name"] = std::string("disable_") + sampler;
    field["sampler"] = sampler;
    field["bit_shift"] =
        submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[samplerIndex];
    field["pica_disable_bit"] = disableBit;
    field["pica_disable_bit_resolved"] = disableBitResolved;
    field["packing_rule"] = "flag ? 0 : 1";
    field["consumer_address"] =
        HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8);
    field["consumer_source"] = "codebin_0040d040_lighting_lutinput_abs_emit";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutScaleFlagJson(
    const CmbMaterialLightingBlock& block, uint32_t localOffset, uint8_t rawValue, bool enabled,
    uint32_t runtimeLaneByteOffset, uint32_t samplerIndex, uint32_t decodedScale,
    bool resolved, const NativeKankyoDrawHandleSubmitContract& submit) {
    auto field = MaterialLightingBlockFlagFieldJson(
        block, localOffset, rawValue, enabled, runtimeLaneByteOffset);
    const auto sampler = submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[samplerIndex];
    field["semantic_resolved"] = resolved;
    field["register"] = HexU32(submit.MaterialLaneCopiedBlockPicaConfigRegisters[2], 3);
    field["register_name"] = submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[2];
    field["field_name"] = sampler;
    field["sampler"] = sampler;
    field["bit_shift"] =
        submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[samplerIndex];
    field["decoded_lighting_scale"] = decodedScale;
    field["decoded_lighting_scale_resolved"] = resolved;
    field["native_flag_scale_by_value"] = std::array<uint32_t, 2>{ 0, 1 };
    field["consumer_address"] =
        HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8);
    field["consumer_source"] = "codebin_0040d040_lighting_lutinput_scale_emit";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutInputFlagJson(
    const CmbMaterialLightingBlock& block, uint32_t localOffset, uint8_t rawValue, bool enabled,
    uint32_t runtimeLaneByteOffset, uint32_t samplerIndex, uint32_t decodedInput,
    bool resolved, const NativeKankyoDrawHandleSubmitContract& submit) {
    auto field = MaterialLightingBlockFlagFieldJson(
        block, localOffset, rawValue, enabled, runtimeLaneByteOffset);
    const auto sampler = submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[samplerIndex];
    field["semantic_resolved"] = resolved;
    field["register"] = HexU32(submit.MaterialLaneCopiedBlockPicaConfigRegisters[1], 3);
    field["register_name"] = submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[1];
    field["field_name"] = sampler;
    field["sampler"] = sampler;
    field["bit_shift"] =
        submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[samplerIndex];
    field["decoded_lighting_lut_input"] = decodedInput;
    field["decoded_lighting_lut_input_resolved"] = resolved;
    field["native_flag_lut_input_by_value"] = std::array<uint32_t, 2>{ 0, 1 };
    field["consumer_address"] =
        HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8);
    field["consumer_source"] = "codebin_0040d040_lighting_lutinput_select_emit";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutInputValueJson(
    const CmbMaterialLightingBlock& block, uint32_t localOffset, uint16_t rawValue,
    uint32_t decodedInput, bool recognized, uint32_t runtimeLaneByteOffset,
    uint32_t samplerIndex, const NativeKankyoDrawHandleSubmitContract& submit) {
    auto field = MaterialLightingBlockU16FieldJson(
        block, localOffset, rawValue, decodedInput, recognized, runtimeLaneByteOffset);
    const auto sampler = submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[samplerIndex];
    field["semantic_resolved"] = recognized;
    field["register"] = HexU32(submit.MaterialLaneCopiedBlockPicaConfigRegisters[1], 3);
    field["register_name"] = submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[1];
    field["field_name"] = sampler;
    field["sampler"] = sampler;
    field["bit_shift"] =
        submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[samplerIndex];
    field["decoded_lighting_lut_input"] = decodedInput;
    field["decoded_lighting_lut_input_resolved"] = recognized;
    field["consumer_address"] =
        HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8);
    field["consumer_source"] = "codebin_0040d040_lighting_lutinput_select_emit";
    return field;
}

nlohmann::json MaterialLightingBlockPicaLutScaleValueJson(
    const CmbMaterialLightingBlock& block, uint32_t decodedScale, bool recognized,
    uint32_t samplerIndex, const NativeKankyoDrawHandleSubmitContract& submit) {
    const auto sampler = submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[samplerIndex];
    return {
        { "source_local_offset", "0x28" },
        { "material_offset", HexU32(block.SourceOffset + 0x28, 3) },
        { "runtime_lane_byte_offset",
          HexU32(submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset, 3) },
        { "runtime_subblock_byte_offset",
          HexU32(submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset -
                     submit.MaterialLaneCopiedBlockDestinationOffset,
                 3) },
        { "runtime_lane_populate_source", "codebin_004c6364_material_lighting_block_copy" },
        { "native_bits", HexU32(block.PicaLutScaleSourceBits, 8) },
        { "native_value", block.PicaLutScaleSourceValue },
        { "decoded", decodedScale },
        { "recognized", recognized },
        { "semantic_resolved", recognized },
        { "register", HexU32(submit.MaterialLaneCopiedBlockPicaConfigRegisters[2], 3) },
        { "register_name", submit.MaterialLaneCopiedBlockPicaConfigRegisterNames[2] },
        { "field_name", sampler },
        { "sampler", sampler },
        { "bit_shift", submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[samplerIndex] },
        { "decoded_lighting_scale", decodedScale },
        { "decoded_lighting_scale_resolved", recognized },
        { "consumer_address",
          HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8) },
        { "consumer_source", "codebin_0040d040_lighting_lutinput_scale_emit" },
    };
}

nlohmann::json MaterialLightingBlockPicaConfigPacketJson(
    const NativeKankyoDrawHandleSubmitContract& submit) {
    return {
        { "source", "codebin_0040d040_material_lane_copied_block_pica_config_packet" },
        { "submit_wrapper_address",
          HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketSubmitWrapperAddress, 8) },
        { "emitter_address",
          HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress, 8) },
        { "followup_emitter_address",
          HexU32(submit.MaterialLaneCopiedBlockPicaConfigPacketFollowupEmitterAddress, 8) },
        { "packet_word_count", submit.MaterialLaneCopiedBlockPicaConfigPacketWordCount },
        { "packet_headers", submit.MaterialLaneCopiedBlockPicaConfigPacketHeaders },
        { "boolean_bytes_invert_clamp01",
          submit.MaterialLaneCopiedBlockPicaConfigBooleansInvertClamp01 },
        { "boolean_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets },
        { "boolean_bit_shifts", submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts },
        { "primary_nibble_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigPrimaryNibbleByteOffsets },
        { "secondary_nibble_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigSecondaryNibbleByteOffsets },
        { "nibble_bit_shifts", submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts },
        { "pica_lut_input_abs_d0",
          { { "register", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0Register },
            { "register_name", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0RegisterName },
            { "field_name", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0FieldName },
            { "sampler", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SamplerName },
            { "source_local_offset",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SourceLocalOffset },
            { "runtime_lane_byte_offset",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset },
            { "bit_shift", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift },
            { "native_constant_base",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantBase },
            { "native_constant_count",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantCount },
            { "disable_bit_by_decoded_value",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector },
            { "packing_rule", submit.MaterialLaneCopiedBlockPicaLutInputAbsD0PackingRule },
            { "semantic_resolved",
              submit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved } } },
        { "resolved_material_block_runtime_offsets",
          { { "pica_lighting_config",
              submit.MaterialLaneCopiedBlockPicaLightingConfigByteOffset },
            { "pica_62c0_selector", submit.MaterialLaneCopiedBlockEnum4ByteOffset },
            { "pica_lut_input_abs_d0",
              submit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset },
            { "pica_lut_input_abs_sp", submit.MaterialLaneCopiedBlockFlag1ByteOffset },
            { "pica_lut_scale_sp", submit.MaterialLaneCopiedBlockFlag2ByteOffset },
            { "pica_lut_input_fr", submit.MaterialLaneCopiedBlockFlag4ByteOffset },
            { "pica_lut_input_abs_fr", submit.MaterialLaneCopiedBlockFlag5ByteOffset },
            { "pica_lut_input_abs_rb", submit.MaterialLaneCopiedBlockFlag0ByteOffset },
            { "pica_lut_input", submit.MaterialLaneCopiedBlockPicaLutInputByteOffset },
            { "pica_lut_input_rb", submit.MaterialLaneCopiedBlockPicaLutInputByteOffset },
            { "pica_lut_scale", submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset },
            { "pica_lut_scale_rb", submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset },
            { "pica_bump_texture_unit",
              submit.MaterialLaneCopiedBlockPicaBumpTextureUnitByteOffset },
            { "flag0", submit.MaterialLaneCopiedBlockFlag0ByteOffset },
            { "flag1", submit.MaterialLaneCopiedBlockFlag1ByteOffset },
            { "flag2", submit.MaterialLaneCopiedBlockFlag2ByteOffset },
            { "flag4", submit.MaterialLaneCopiedBlockFlag4ByteOffset },
            { "flag5", submit.MaterialLaneCopiedBlockFlag5ByteOffset } } },
        { "unresolved_material_block_runtime_offsets",
          { { "pica_bump_mode", submit.MaterialLaneCopiedBlockPicaBumpModeByteOffset },
            { "flag3", submit.MaterialLaneCopiedBlockFlag3ByteOffset } } },
    };
}

nlohmann::json MaterialLightingBlockJson(const CmbMaterialLightingBlock& block) {
    if (!block.Decoded) {
        return nullptr;
    }
    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    const auto& submit = bridgeContract.DrawHandleSubmit;
    return {
        { "source", "oot3d_cmb_material_lighting_block_offset_0xcc" },
        { "native_lane_populate_function", "0x004c6364" },
        { "decoded", block.Decoded },
        { "source_offset", HexU32(block.SourceOffset, 3) },
        { "size", block.Size },
        { "raw_fnv1a64", std::to_string(Fnv1a64(block.RawBlock)) },
        { "pica_bump_texture_unit",
          MaterialLightingBlockU16FieldJson(block, 0x10, block.PicaBumpTextureUnitRaw,
                                            block.PicaBumpTextureUnit,
                                            block.PicaBumpTextureUnitRecognized,
                                            submit.MaterialLaneCopiedBlockPicaBumpTextureUnitByteOffset) },
        { "pica_bump_mode",
          MaterialLightingBlockU16FieldJson(block, 0x12, block.PicaBumpModeRaw,
                                            block.PicaBumpMode, block.PicaBumpModeRecognized,
                                            submit.MaterialLaneCopiedBlockPicaBumpModeByteOffset) },
        { "pica_lighting_config",
          MaterialLightingBlockU16FieldJson(block, 0x18, block.PicaLightingConfigRaw,
                                            block.PicaLightingConfig,
                                            block.PicaLightingConfigRecognized,
                                            submit.MaterialLaneCopiedBlockPicaLightingConfigByteOffset) },
        { "pica_62c0_selector",
          MaterialLightingBlockU16FieldJson(block, 0x1C, block.Pica62C0SelectorRaw,
                                            block.Pica62C0Selector,
                                            block.Pica62C0SelectorRecognized,
                                            submit.MaterialLaneCopiedBlockEnum4ByteOffset) },
        { "pica_lut_input_abs_d0",
          MaterialLightingBlockPicaLutInputAbsD0Json(block, submit) },
        { "pica_lut_input_abs_sp",
          MaterialLightingBlockPicaLutInputAbsFlagJson(
              block, submit.MaterialLaneCopiedBlockFlag1SourceLocalOffset, block.Flag1Raw,
              block.Flag1, submit.MaterialLaneCopiedBlockFlag1ByteOffset, 2,
              block.PicaLutInputAbsSpDisableBit,
              block.PicaLutInputAbsSpDisableBitResolved, submit) },
        { "pica_lut_scale_sp",
          MaterialLightingBlockPicaLutScaleFlagJson(
              block, submit.MaterialLaneCopiedBlockFlag2SourceLocalOffset, block.Flag2Raw,
              block.Flag2, submit.MaterialLaneCopiedBlockFlag2ByteOffset, 2,
              block.PicaLutScaleSp, block.PicaLutScaleSpResolved, submit) },
        { "pica_lut_input_fr",
          MaterialLightingBlockPicaLutInputFlagJson(
              block, submit.MaterialLaneCopiedBlockFlag4SourceLocalOffset, block.Flag4Raw,
              block.Flag4, submit.MaterialLaneCopiedBlockFlag4ByteOffset, 3,
              block.PicaLutInputFr, block.PicaLutInputFrResolved, submit) },
        { "pica_lut_input_abs_fr",
          MaterialLightingBlockPicaLutInputAbsFlagJson(
              block, submit.MaterialLaneCopiedBlockFlag5SourceLocalOffset, block.Flag5Raw,
              block.Flag5, submit.MaterialLaneCopiedBlockFlag5ByteOffset, 3,
              block.PicaLutInputAbsFrDisableBit,
              block.PicaLutInputAbsFrDisableBitResolved, submit) },
        { "pica_lut_input_abs_rb",
          MaterialLightingBlockPicaLutInputAbsFlagJson(
              block, submit.MaterialLaneCopiedBlockFlag0SourceLocalOffset, block.Flag0Raw,
              block.Flag0, submit.MaterialLaneCopiedBlockFlag0ByteOffset, 4,
              block.PicaLutInputAbsRbDisableBit,
              block.PicaLutInputAbsRbDisableBitResolved, submit) },
        { "unresolved_enum4",
          MaterialLightingBlockU16FieldJson(block, 0x1C, block.UnresolvedEnum4Raw,
                                            block.UnresolvedEnum4Encoded,
                                            block.UnresolvedEnum4Recognized,
                                            submit.MaterialLaneCopiedBlockEnum4ByteOffset) },
        { "unresolved_enum4_semantic_resolved",
          submit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved },
        { "unresolved_enum4_semantic_alias", "pica_lut_input_abs_d0" },
        { "unresolved_enum4_runtime_lane_mapping_resolved", true },
        { "pica_lut_input",
          MaterialLightingBlockU16FieldJson(block, 0x26, block.PicaLutInputRaw,
                                            block.PicaLutInput, block.PicaLutInputRecognized,
                                            submit.MaterialLaneCopiedBlockPicaLutInputByteOffset) },
        { "pica_lut_input_rb",
          MaterialLightingBlockPicaLutInputValueJson(
              block, submit.MaterialLaneCopiedBlockPicaLutInputSourceLocalOffset,
              block.PicaLutInputRaw, block.PicaLutInputRb,
              block.PicaLutInputRbRecognized,
              submit.MaterialLaneCopiedBlockPicaLutInputByteOffset, 4, submit) },
        { "pica_lut_scale",
          { { "source_local_offset", "0x28" },
            { "material_offset", HexU32(block.SourceOffset + 0x28, 3) },
            { "runtime_lane_byte_offset",
              HexU32(submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset, 3) },
            { "runtime_subblock_byte_offset",
              HexU32(submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset -
                         submit.MaterialLaneCopiedBlockDestinationOffset,
                     3) },
            { "runtime_lane_populate_source", "codebin_004c6364_material_lighting_block_copy" },
            { "native_bits", HexU32(block.PicaLutScaleSourceBits, 8) },
            { "native_value", block.PicaLutScaleSourceValue },
            { "decoded", block.PicaLutScale },
            { "recognized", block.PicaLutScaleRecognized } } },
        { "pica_lut_scale_rb",
          MaterialLightingBlockPicaLutScaleValueJson(
              block, block.PicaLutScaleRb, block.PicaLutScaleRbRecognized, 4, submit) },
        { "flags",
          { { "flag0", MaterialLightingBlockFlagFieldJson(
                           block, 0x24, block.Flag0Raw, block.Flag0,
                           submit.MaterialLaneCopiedBlockFlag0ByteOffset) },
            { "flag1", MaterialLightingBlockFlagFieldJson(
                           block, 0x14, block.Flag1Raw, block.Flag1,
                           submit.MaterialLaneCopiedBlockFlag1ByteOffset) },
            { "flag2", MaterialLightingBlockFlagFieldJson(
                           block, 0x1E, block.Flag2Raw, block.Flag2,
                           submit.MaterialLaneCopiedBlockFlag2ByteOffset) },
            { "flag3", MaterialLightingBlockFlagFieldJson(
                           block, 0x1F, block.Flag3Raw, block.Flag3,
                           submit.MaterialLaneCopiedBlockFlag3ByteOffset) },
            { "flag4", MaterialLightingBlockFlagFieldJson(
                           block, 0x20, block.Flag4Raw, block.Flag4,
                           submit.MaterialLaneCopiedBlockFlag4ByteOffset) },
            { "flag5",
              MaterialLightingBlockPayload3ScaleGateFlagJson(block, bridgeContract) } } },
        { "flag_semantics_resolved", false },
        { "flag_semantics_partially_resolved", true },
        { "resolved_flag_semantics",
          { { "flag0", "pica_lut_input_abs_rb_disable_bit" },
            { "flag1", "pica_lut_input_abs_sp_disable_bit" },
            { "flag2", "pica_lut_scale_sp" },
            { "flag4", "pica_lut_input_fr" },
            { "flag5",
              { "pica_lut_input_abs_fr_disable_bit",
                "runtime_payload3_material_scale_gate" } } } },
        { "flag_runtime_lane_mapping_resolved", true },
        { "flag_runtime_lane_mapping_source", "codebin_004c6364_material_lighting_block_copy" },
        { "native_pica_config_packet", MaterialLightingBlockPicaConfigPacketJson(submit) },
    };
}

nlohmann::json RawMaterialAnalysisJson(const Oot3dNativeRenderMaterialState& material) {
    return {
        { "size", material.NativeMaterialRawSize },
        { "fnv1a64", std::to_string(material.NativeMaterialRawFnv1a64) },
        { "material_lighting_block", MaterialLightingBlockJson(material.MaterialLightingBlock) },
        { "post_material_texture_env_table", PostMaterialTextureEnvTableJson(material) },
        { "texture_stage_candidate_range",
          { material.NativeMaterialTextureStageCandidateStart, material.NativeMaterialTextureStageCandidateEnd } },
        { "texture_stage_candidate_nonzero_word_count",
          material.NativeMaterialTextureStageCandidateNonzeroWords.size() },
        { "texture_stage_candidate_nonzero_words",
          RawMaterialWordsJson(material.NativeMaterialTextureStageCandidateNonzeroWords) },
    };
}

nlohmann::json MaterialPicaLutInputRegisterStateJson(
    const Oot3dNativePicaMaterialLutInputRegisterState& reg) {
    return {
        { "register", HexU32(reg.Register, 3) },
        { "register_name", reg.RegisterName },
        { "known_bit_mask", HexU32(reg.KnownBitMask, 8) },
        { "known_value", HexU32(reg.KnownValue, 8) },
        { "has_known_bits", reg.HasKnownBits },
    };
}

nlohmann::json MaterialPicaLutInputSamplerStateJson(
    const Oot3dNativePicaMaterialLutSamplerState& sampler) {
    return {
        { "sampler", sampler.Sampler },
        { "pica_role", sampler.PicaRole },
        { "complete", sampler.Complete },
        { "abs_input_known", sampler.AbsInputKnown },
        { "abs_input_enabled", sampler.AbsInputEnabled },
        { "abs_disable_bit", sampler.AbsDisableBit },
        { "abs_bit_shift", sampler.AbsBitShift },
        { "lut_input_known", sampler.LutInputKnown },
        { "lut_input_raw", sampler.LutInputRaw },
        { "lut_input_raw_high_bit_set", sampler.LutInputRawHighBitSet },
        { "lut_input", sampler.LutInput },
        { "lut_input_name", sampler.LutInputName },
        { "lut_input_expression", sampler.LutInputExpression },
        { "lut_input_shader_semantic_resolved", sampler.LutInputShaderSemanticResolved },
        { "lut_input_bit_shift", sampler.LutInputBitShift },
        { "scale_known", sampler.ScaleKnown },
        { "scale", sampler.Scale },
        { "scale_value", sampler.ScaleValue },
        { "scale_bit_shift", sampler.ScaleBitShift },
    };
}

nlohmann::json MaterialPicaLutInputStateJson(
    const Oot3dNativePicaMaterialLutInputState& state) {
    if (!state.Available) {
        return nullptr;
    }
    nlohmann::json registers = nlohmann::json::array();
    for (const auto& reg : state.Registers) {
        registers.push_back(MaterialPicaLutInputRegisterStateJson(reg));
    }
    nlohmann::json samplers = nlohmann::json::array();
    for (const auto& sampler : state.Samplers) {
        samplers.push_back(MaterialPicaLutInputSamplerStateJson(sampler));
    }
    return {
        { "available", state.Available },
        { "complete", state.Complete },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "consumer_address", HexU32(state.ConsumerAddress, 8) },
        { "packet_word_count", state.PacketWordCount },
        { "resolved_field_count", state.ResolvedFieldCount },
        { "registers", registers },
        { "samplers", samplers },
        { "resolved_fields", state.ResolvedFields },
        { "unresolved_fields", state.UnresolvedFields },
    };
}

nlohmann::json HexU32VectorJson(const std::vector<uint32_t>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(value, 8));
    }
    return out;
}

nlohmann::json MaterialFragmentLightingConfigPayloadWordJson(
    const Oot3dNativePicaFragmentLightingConfigPayloadWordState& word) {
    return {
        { "register", HexU32(word.Register, 3) },
        { "register_name", word.RegisterName },
        { "value", HexU32(word.Value, 8) },
        { "known", word.Known },
        { "source", word.Source },
    };
}

nlohmann::json MaterialFragmentLightingConfigStateJson(
    const Oot3dNativePicaFragmentLightingConfigState& state) {
    if (!state.Available) {
        return nullptr;
    }
    nlohmann::json payloadWords = nlohmann::json::array();
    for (const auto& word : state.PayloadWords) {
        payloadWords.push_back(MaterialFragmentLightingConfigPayloadWordJson(word));
    }
    return {
        { "available", state.Available },
        { "complete", state.Complete },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "primary_source_status", state.PrimarySourceStatus },
        { "secondary_mode_source_status", state.SecondaryModeSourceStatus },
        { "runtime_override_gate_resolved", state.RuntimeOverrideGateResolved },
        { "runtime_material_primary_source_resolved",
          state.RuntimeMaterialPrimarySourceResolved },
        { "runtime_secondary_mode_source_resolved",
          state.RuntimeSecondaryModeSourceResolved },
        { "emitter_address", HexU32(state.EmitterAddress, 8) },
        { "material_setup_address", HexU32(state.MaterialSetupAddress, 8) },
        { "prepared_state_initializer_address",
          HexU32(state.PreparedStateInitializerAddress, 8) },
        { "packet_register", HexU32(state.PacketRegister, 3) },
        { "packet_header", HexU32(state.PacketHeader, 8) },
        { "payload_word_count", state.PayloadWordCount },
        { "known_payload_word_count", state.KnownPayloadWordCount },
        { "native_type", HexU32(state.NativeType, 4) },
        { "flags", HexU32(state.Flags, 4) },
        { "primary_enable", state.PrimaryEnable },
        { "primary_enable_known", state.PrimaryEnableKnown },
        { "primary_mode", state.PrimaryMode },
        { "primary_mode_known", state.PrimaryModeKnown },
        { "primary_runtime_lane_value", state.PrimaryRuntimeLaneValue },
        { "primary_runtime_lane_value_known", state.PrimaryRuntimeLaneValueKnown },
        { "primary_default_gate_payload0_candidate",
          HexU32(state.PrimaryDefaultGatePayload0Candidate, 8) },
        { "primary_default_gate_payload0_candidate_known",
          state.PrimaryDefaultGatePayload0CandidateKnown },
        { "runtime_override_gate_resolution_source",
          state.RuntimeOverrideGateResolutionSource },
        { "runtime_state_initializer_address",
          HexU32(state.RuntimeStateInitializerAddress, 8) },
        { "runtime_state_copy_helper_address",
          HexU32(state.RuntimeStateCopyHelperAddress, 8) },
        { "runtime_override_gate_default_value", state.RuntimeOverrideGateDefaultValue },
        { "runtime_override_gate_default_known", state.RuntimeOverrideGateDefaultKnown },
        { "runtime_override_gate_copy_path_known", state.RuntimeOverrideGateCopyPathKnown },
        { "runtime_secondary_mode_default_value", state.RuntimeSecondaryModeDefaultValue },
        { "runtime_secondary_mode_default_known", state.RuntimeSecondaryModeDefaultKnown },
        { "runtime_secondary_mode_copy_path_known",
          state.RuntimeSecondaryModeCopyPathKnown },
        { "runtime_state_writer_scan_resolved", state.RuntimeStateWriterScanResolved },
        { "runtime_state_writers_classified_as_draw_local",
          state.RuntimeStateWritersClassifiedAsDrawLocal },
        { "runtime_active_override_producer_resolved_from_writer_scan",
          state.RuntimeActiveOverrideProducerResolvedFromWriterScan },
        { "runtime_override_writer_addresses",
          HexU32VectorJson(state.RuntimeOverrideWriterAddresses) },
        { "runtime_secondary_mode_writer_addresses",
          HexU32VectorJson(state.RuntimeSecondaryModeWriterAddresses) },
        { "runtime_gameplay_draw_address", HexU32(state.RuntimeGameplayDrawAddress, 8) },
        { "runtime_gameplay_draw_dispatcher_address",
          HexU32(state.RuntimeGameplayDrawDispatcherAddress, 8) },
        { "runtime_gameplay_draw_dispatcher_callsite_address",
          HexU32(state.RuntimeGameplayDrawDispatcherCallsiteAddress, 8) },
        { "runtime_draw_entry_submit_address",
          HexU32(state.RuntimeDrawEntrySubmitAddress, 8) },
        { "runtime_draw_entry_submit_callsite_addresses",
          HexU32VectorJson(state.RuntimeDrawEntrySubmitCallsiteAddresses) },
        { "runtime_draw_entry_flags_offset",
          HexU32(state.RuntimeDrawEntryFlagsOffset, 3) },
        { "runtime_draw_entry_render_context_pointer_offset",
          HexU32(state.RuntimeDrawEntryRenderContextPointerOffset, 3) },
        { "runtime_draw_entry_callback_offset",
          HexU32(state.RuntimeDrawEntryCallbackOffset, 3) },
        { "runtime_draw_entry_visibility_state_offset",
          HexU32(state.RuntimeDrawEntryVisibilityStateOffset, 3) },
        { "runtime_draw_entry_submitted_byte_offset",
          HexU32(state.RuntimeDrawEntrySubmittedByteOffset, 3) },
        { "runtime_draw_entry_fade_counter_offset",
          HexU32(state.RuntimeDrawEntryFadeCounterOffset, 3) },
        { "runtime_draw_entry_fade_limit_offset",
          HexU32(state.RuntimeDrawEntryFadeLimitOffset, 3) },
        { "runtime_draw_entry_override_gate_flag_mask",
          HexU32(state.RuntimeDrawEntryOverrideGateFlagMask, 8) },
        { "runtime_draw_entry_override_gate_force_full_flag_mask",
          HexU32(state.RuntimeDrawEntryOverrideGateForceFullFlagMask, 8) },
        { "runtime_draw_entry_submit_route_resolved",
          state.RuntimeDrawEntrySubmitRouteResolved },
        { "runtime_draw_entry_override_gate_rule_resolved",
          state.RuntimeDrawEntryOverrideGateRuleResolved },
        { "runtime_draw_entry_route_promotes_active_material_override",
          state.RuntimeDrawEntryRoutePromotesActiveMaterialOverride },
        { "runtime_submit_manager_vtable_address",
          HexU32(state.RuntimeSubmitManagerVtableAddress, 8) },
        { "runtime_submit_manager_material_config_slot_offset",
          HexU32(state.RuntimeSubmitManagerMaterialConfigSlotOffset, 2) },
        { "runtime_submit_manager_material_config_address",
          HexU32(state.RuntimeSubmitManagerMaterialConfigAddress, 8) },
        { "runtime_submit_manager_material_state_slot_offset",
          HexU32(state.RuntimeSubmitManagerMaterialStateSlotOffset, 2) },
        { "runtime_submit_manager_material_state_address",
          HexU32(state.RuntimeSubmitManagerMaterialStateAddress, 8) },
        { "runtime_submit_manager_material_route_resolved",
          state.RuntimeSubmitManagerMaterialRouteResolved },
        { "runtime_submit_manager_material_route_resolves_active_override_gate",
          state.RuntimeSubmitManagerMaterialRouteResolvesActiveOverrideGate },
        { "runtime_material_lane_dispatch_address",
          HexU32(state.RuntimeMaterialLaneDispatchAddress, 8) },
        { "runtime_material_lane_dispatch_loop_end_address",
          HexU32(state.RuntimeMaterialLaneDispatchLoopEndAddress, 8) },
        { "runtime_material_lane_dispatch_cmb_mesh_material_lane_byte_offset",
          HexU32(state.RuntimeMaterialLaneDispatchCmbMeshMaterialLaneByteOffset, 3) },
        { "runtime_material_lane_dispatch_material_lane_stride_bytes",
          HexU32(state.RuntimeMaterialLaneDispatchMaterialLaneStrideBytes, 3) },
        { "runtime_material_lane_dispatch_resolved",
          state.RuntimeMaterialLaneDispatchResolved },
        { "runtime_material_lane_dispatch_promotes_active_override_gate",
          state.RuntimeMaterialLaneDispatchPromotesActiveOverrideGate },
        { "runtime_material_lane_dispatch_source",
          state.RuntimeMaterialLaneDispatchSource },
        { "secondary_enable", state.SecondaryEnable },
        { "secondary_enable_known", state.SecondaryEnableKnown },
        { "secondary_mode", state.SecondaryMode },
        { "secondary_mode_known", state.SecondaryModeKnown },
        { "secondary_mode_cmb_candidate", state.SecondaryModeCmbCandidate },
        { "secondary_mode_cmb_candidate_known", state.SecondaryModeCmbCandidateKnown },
        { "secondary_param", HexU32(state.SecondaryParam, 4) },
        { "secondary_param_known", state.SecondaryParamKnown },
        { "secondary_type_selector", state.SecondaryTypeSelector },
        { "secondary_type_selector_known", state.SecondaryTypeSelectorKnown },
        { "secondary_type_register_value",
          HexU32(state.SecondaryTypeRegisterValue, 4) },
        { "secondary_type_register_value_known",
          state.SecondaryTypeRegisterValueKnown },
        { "secondary_type_disabled", state.SecondaryTypeDisabled },
        { "primary_cmb_blend_gate", state.PrimaryCmbBlendGate },
        { "primary_cmb_blend_gate_known", state.PrimaryCmbBlendGateKnown },
        { "aux_byte", state.AuxByte },
        { "aux_byte_known", state.AuxByteKnown },
        { "aux_halfword", state.AuxHalfword },
        { "aux_halfword_known", state.AuxHalfwordKnown },
        { "payload_words", payloadWords },
        { "resolved_fields", state.ResolvedFields },
        { "unresolved_fields", state.UnresolvedFields },
    };
}

nlohmann::json NativeRenderLutSampleJson(const std::vector<float>& samples, size_t index) {
    if (index >= samples.size()) {
        return nullptr;
    }
    return samples[index];
}

nlohmann::json NativeRenderLutRecordJson(const Oot3dNativeRenderLutRecord& record) {
    return {
        { "index", record.Index },
        { "source_offset", HexU32(record.SourceOffset, 4) },
        { "size", record.Size },
        { "type", record.Type },
        { "header_byte_01", record.HeaderByte01 },
        { "header_byte_02", record.HeaderByte02 },
        { "header_byte_03", record.HeaderByte03 },
        { "point_count", record.PointCount },
        { "point_stride_bytes", record.PointStrideBytes },
        { "sample_count", record.Samples.size() },
        { "sample_0", NativeRenderLutSampleJson(record.Samples, 0) },
        { "sample_128", NativeRenderLutSampleJson(record.Samples, 128) },
        { "sample_255", NativeRenderLutSampleJson(record.Samples, 255) },
        { "packed_base_value_count", record.PackedBaseValues.size() },
        { "packed_delta_value_count", record.PackedDeltaValues.size() },
        { "packed_base_0", NativeRenderLutSampleJson(record.PackedBaseValues, 0) },
        { "packed_delta_0", NativeRenderLutSampleJson(record.PackedDeltaValues, 0) },
    };
}

nlohmann::json NativeRenderLutSectionJson(const Oot3dNativeRenderLutSection& section) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : section.Records) {
        records.push_back(NativeRenderLutRecordJson(record));
    }
    return {
        { "decoded", section.Decoded },
        { "source_offset", HexU32(section.SourceOffset, 4) },
        { "chunk_size", section.ChunkSize },
        { "declared_record_count", section.Count },
        { "record_count", section.Records.size() },
        { "header_word_0c", section.HeaderWord0C },
        { "source_kind", section.SourceKind },
        { "native_asset_decode_contract_available", section.NativeAssetDecodeContractAvailable },
        { "final_shader_semantic_resolved", section.FinalShaderSemanticResolved },
        { "shader_evaluation_pending",
          section.Decoded && !section.Records.empty() && !section.FinalShaderSemanticResolved },
        { "records", records },
    };
}

nlohmann::json TextureEnvProgramJson(const Oot3dNativeRenderTextureEnvProgram& program) {
    return {
        { "decoded", program.Decoded },
        { "stage_count", program.StageCount },
        { "color_shader_path", program.ColorShaderPath },
        { "color_shader_path_supported", program.ColorShaderPathSupported },
        { "color_shader_path_applied", program.ColorShaderPathApplied },
        { "fallback_texture_light_modulation_used", program.FallbackTextureLightModulationUsed },
        { "rgb_combines_known", program.RgbCombinesKnown },
        { "rgb_sources_known", program.RgbSourcesKnown },
        { "rgb_operands_known", program.RgbOperandsKnown },
        { "uses_color_scale", program.UsesColorScale },
        { "color_scales_known", program.ColorScalesKnown },
        { "uses_primary_color", program.UsesPrimaryColor },
        { "uses_fragment_lighting_color", program.UsesFragmentLightingColor },
        { "uses_previous", program.UsesPrevious },
        { "uses_previous_buffer", program.UsesPreviousBuffer },
        { "uses_constant_color", program.UsesConstantColor },
        { "uses_texture0", program.UsesTexture0 },
        { "uses_texture1", program.UsesTexture1 },
        { "uses_texture2", program.UsesTexture2 },
        { "uses_texture3", program.UsesTexture3 },
        { "requires_multi_stage_evaluation", program.RequiresMultiStageEvaluation },
        { "requires_multi_texture_sampling", program.RequiresMultiTextureSampling },
        { "requires_constant_color_selection", program.RequiresConstantColorSelection },
        { "requires_previous_buffer", program.RequiresPreviousBuffer },
        { "requires_texture_color_add", program.RequiresTextureColorAdd },
        { "rgb_route_decoded", program.RgbRouteDecoded },
        { "constant_color_resolved", program.ConstantColorResolved },
        { "constant_color_index", program.ConstantColorIndex },
        { "constant_color_source", program.ConstantColorSource },
        { "vertex_color_constant_stage_count", program.VertexColorConstantStageCount },
        { "vertex_color_multiplier", ColorJson(program.VertexColorMultiplier) },
        { "primary_color_multiplier_resolved", program.PrimaryColorMultiplierResolved },
        { "primary_color_multiplier_stage_count", program.PrimaryColorMultiplierStageCount },
        { "primary_color_multiplier", Vec3Json(program.PrimaryColorMultiplier) },
        { "texture_color_multiplier_resolved", program.TextureColorMultiplierResolved },
        { "texture_color_multiplier_stage_count", program.TextureColorMultiplierStageCount },
        { "texture_color_multiplier", Vec3Json(program.TextureColorMultiplier) },
        { "texture_color_addend_resolved", program.TextureColorAddendResolved },
        { "texture_color_add_stage_count", program.TextureColorAddStageCount },
        { "texture_color_addend", ColorJson(program.TextureColorAddend) },
        { "texture_color_add_uses_texture0_alpha", program.TextureColorAddUsesTexture0Alpha },
        { "texture1_color_add_resolved", program.Texture1ColorAddResolved },
        { "texture1_color_add_stage_count", program.Texture1ColorAddStageCount },
        { "texture1_color_add_mapper_slot", program.Texture1ColorAddMapperSlot },
        { "texture1_color_add_uses_texture0_alpha", program.Texture1ColorAddUsesTexture0Alpha },
        { "texture1_color_add_source", program.Texture1ColorAddSource },
        { "texture1_color_multiply_resolved", program.Texture1ColorMultiplyResolved },
        { "texture1_color_multiply_stage_count", program.Texture1ColorMultiplyStageCount },
        { "texture1_color_multiply_mapper_slot", program.Texture1ColorMultiplyMapperSlot },
        { "texture1_color_multiply_source", program.Texture1ColorMultiplySource },
        { "texture0_texture1_add_then_primary_color_modulate_resolved",
          program.Texture0Texture1AddThenPrimaryColorModulateResolved },
        { "texture0_texture1_add_then_primary_color_modulate_stage_count",
          program.Texture0Texture1AddThenPrimaryColorModulateStageCount },
        { "texture0_texture1_add_then_primary_color_modulate_mapper_slot",
          program.Texture0Texture1AddThenPrimaryColorModulateMapperSlot },
        { "texture0_texture1_add_then_primary_color_modulate_source",
          program.Texture0Texture1AddThenPrimaryColorModulateSource },
        { "texture1_texture2_multiply_add_previous_resolved",
          program.Texture1Texture2MultiplyAddPreviousResolved },
        { "texture1_texture2_multiply_add_previous_stage_count",
          program.Texture1Texture2MultiplyAddPreviousStageCount },
        { "texture1_texture2_multiply_add_previous_texture1_mapper_slot",
          program.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot },
        { "texture1_texture2_multiply_add_previous_texture2_mapper_slot",
          program.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot },
        { "texture1_texture2_multiply_add_previous_source",
          program.Texture1Texture2MultiplyAddPreviousSource },
        { "texture0_texture1_add_multiply_texture0_resolved",
          program.Texture0Texture1AddMultiplyTexture0Resolved },
        { "texture0_texture1_add_multiply_texture0_stage_count",
          program.Texture0Texture1AddMultiplyTexture0StageCount },
        { "texture0_texture1_add_multiply_texture0_mapper_slot",
          program.Texture0Texture1AddMultiplyTexture0MapperSlot },
        { "texture0_texture1_add_multiply_texture0_source",
          program.Texture0Texture1AddMultiplyTexture0Source },
        { "texture0_primary_color_alpha_modulate_resolved",
          program.Texture0PrimaryColorAlphaModulateResolved },
        { "texture0_primary_color_alpha_modulate_stage_count",
          program.Texture0PrimaryColorAlphaModulateStageCount },
        { "texture0_primary_color_alpha_modulate_source",
          program.Texture0PrimaryColorAlphaModulateSource },
        { "texture0_constant_color_alpha_modulate_resolved",
          program.Texture0ConstantColorAlphaModulateResolved },
        { "texture0_constant_color_alpha_modulate_stage_count",
          program.Texture0ConstantColorAlphaModulateStageCount },
        { "texture0_constant_color_alpha_modulate_source",
          program.Texture0ConstantColorAlphaModulateSource },
        { "alpha_multiplier_resolved", program.AlphaMultiplierResolved },
        { "alpha_multiplier_stage_count", program.AlphaMultiplierStageCount },
        { "alpha_multiplier", program.AlphaMultiplier },
        { "alpha_multiplier_source", program.AlphaMultiplierSource },
    };
}

nlohmann::json MaterialLightingFlagsJson(const Oot3dNativeRenderMaterialState& material) {
    return {
        { "fragment_lighting_enabled", material.FragmentLightingEnabled },
        { "vertex_lighting_enabled", material.VertexLightingEnabled },
        { "hemisphere_lighting_enabled", material.HemisphereLightingEnabled },
        { "hemisphere_occlusion_enabled", material.HemisphereOcclusionEnabled },
        { "source", "oot3d_cmb_material_header_bytes_0x00_0x03" },
    };
}

std::vector<std::string> NativeMaterialLightingIncompleteReasons(
    const Oot3dNativeRenderMaterialState& material) {
    std::vector<std::string> reasons;
    if (!material.MaterialLightingBlock.Decoded) {
        reasons.push_back("cmb_material_lighting_block_not_decoded");
    }
    if (!material.MaterialPicaLutInput.Available) {
        reasons.push_back("pica_lut_input_packet_not_available");
    } else if (!material.MaterialPicaLutInput.Complete) {
        reasons.push_back("pica_lut_input_packet_incomplete");
    }
    if (!material.MaterialFragmentLightingConfig.Available) {
        reasons.push_back("fragment_lighting_config_not_available");
    } else if (!material.MaterialFragmentLightingConfig.Complete) {
        reasons.push_back("fragment_lighting_config_incomplete");
    }
    if (!material.MaterialColorsDecoded) {
        reasons.push_back("material_colors_not_decoded");
    }
    if (material.NativePicaMaterialLutInputEvaluationPending) {
        reasons.push_back("pica_material_lut_input_shader_evaluation_pending");
    }
    if (material.NativePicaFragmentLightingConfigRuntimeOverridePending) {
        reasons.push_back("fragment_lighting_config_runtime_override_pending");
    }
    if (material.NativePicaVertexHemisphereVectorPending) {
        reasons.push_back("vertex_hemisphere_vector_pending");
    }
    if (material.NativePicaBumpModeBackendPending) {
        reasons.push_back("pica_bump_mode_backend_pending");
    }
    if (material.NativeMaterialLightingFlag3BackendPending) {
        reasons.push_back("material_lighting_flag3_backend_pending");
    }
    if (material.NativeMaterialCombinerRequiresDecoder) {
        reasons.push_back("texture_env_combiner_requires_decoder");
    }
    return reasons;
}

bool NativeMaterialLightingComplete(const Oot3dNativeRenderMaterialState& material) {
    return NativeMaterialLightingIncompleteReasons(material).empty();
}

nlohmann::json NativeRenderStateJson(const Oot3dNativeRenderMaterialState& material) {
    const auto picaCullModeName = [&]() -> const char* {
        switch (material.PicaCullMode) {
            case Oot3dNativePicaCullMode::KeepAll:
                return "keep_all";
            case Oot3dNativePicaCullMode::KeepClockwise:
                return "keep_clockwise";
            case Oot3dNativePicaCullMode::KeepCounterClockwise:
                return "keep_counter_clockwise";
        }
        return "unknown";
    };
    return {
        { "source", "oot3d_cmb_material_state_plus_codebin_003fad68_00408e24" },
        { "decoded", material.NativeRenderStateDecoded },
        { "cmb_cull_face", material.CmbCullFace },
        { "pica_cull_mode",
          { { "raw", static_cast<uint8_t>(material.PicaCullMode) },
            { "name", picaCullModeName() },
            { "register", "GPUREG_FACECULLING_CONFIG" },
            { "register_index", "0x040" } } },
        { "cull_back", material.CmbCullFace == 1 },
        { "alpha_test", material.AlphaTest },
        { "alpha_reference", material.AlphaReference },
        { "alpha_function",
          { { "raw", HexU16(material.AlphaFunction) },
            { "name", NativeCompareFunctionName(material.AlphaFunction) },
            { "known", NativeCompareFunctionKnown(material.AlphaFunction) } } },
        { "depth_test", material.DepthTest },
        { "depth_write", material.DepthWrite },
        { "depth_function",
          { { "raw", HexU16(material.DepthFunction) },
            { "name", NativeCompareFunctionName(material.DepthFunction) },
            { "known", NativeCompareFunctionKnown(material.DepthFunction) } } },
        { "blend_mode",
          { { "raw", HexU32(material.BlendMode, 8) },
            { "name", NativeBlendModeName(material.BlendMode) } } },
        { "blend_src",
          { { "raw", HexU16(material.BlendSrc) },
            { "name", NativeBlendFactorName(material.BlendSrc) },
            { "known", NativeBlendFactorKnown(material.BlendSrc) } } },
        { "blend_dst",
          { { "raw", HexU16(material.BlendDst) },
            { "name", NativeBlendFactorName(material.BlendDst) },
            { "known", NativeBlendFactorKnown(material.BlendDst) } } },
        { "blend_equation",
          { { "raw", HexU16(material.BlendEquation) },
            { "name", NativeBlendEquationName(material.BlendEquation) },
            { "known", NativeBlendEquationKnown(material.BlendEquation) } } },
        { "color_blend_src",
          { { "raw", HexU16(material.ColorBlendSrc) },
            { "name", NativeBlendFactorName(material.ColorBlendSrc) },
            { "known", NativeBlendFactorKnown(material.ColorBlendSrc) } } },
        { "color_blend_dst",
          { { "raw", HexU16(material.ColorBlendDst) },
            { "name", NativeBlendFactorName(material.ColorBlendDst) },
            { "known", NativeBlendFactorKnown(material.ColorBlendDst) } } },
        { "color_blend_equation",
          { { "raw", HexU16(material.ColorBlendEquation) },
            { "name", NativeBlendEquationName(material.ColorBlendEquation) },
            { "known", NativeBlendEquationKnown(material.ColorBlendEquation) } } },
        { "blend_color_alpha", material.BlendColorAlpha },
        { "native_blend_state_enabled", material.NativeBlendStateEnabled },
        { "native_blend_factors_supported", material.NativeBlendFactorsSupported },
        { "native_blend_equation_supported", material.NativeBlendEquationSupported },
        { "native_blend_state_supported", material.NativeBlendStateSupported },
    };
}

nlohmann::json NativeMaterialStateJson(int32_t materialIndex,
                                       const Oot3dNativeRenderMaterialState& material) {
    return {
        { "material_index", materialIndex },
        { "raw_material_size", material.NativeMaterialRawSize },
        { "raw_material_fnv1a64", std::to_string(material.NativeMaterialRawFnv1a64) },
        { "raw_material_analysis", RawMaterialAnalysisJson(material) },
        { "material_lighting_block", MaterialLightingBlockJson(material.MaterialLightingBlock) },
        { "material_pica_lut_input", MaterialPicaLutInputStateJson(material.MaterialPicaLutInput) },
        { "material_fragment_lighting_config",
          MaterialFragmentLightingConfigStateJson(material.MaterialFragmentLightingConfig) },
        { "material_lighting_flags", MaterialLightingFlagsJson(material) },
        { "render_state", NativeRenderStateJson(material) },
        { "texture_env", TextureEnvStateJson(material.TextureEnv) },
        { "post_material_texture_env_table", PostMaterialTextureEnvTableJson(material) },
        { "texture_env_table_record_count", material.TextureEnvTableRecordCount },
        { "texture_env_stage_indices_covered", material.TextureEnvStageIndicesCovered },
        { "texture_env_stage_record_count", material.TextureEnvStageRecordCount },
        { "texture_env_selected_stage_index", material.TextureEnvSelectedStageIndex },
        { "texture_env_stages", TextureEnvStagesJson(material.TextureEnvStages) },
        { "texture_env_program", TextureEnvProgramJson(material.TextureEnvProgram) },
        { "texture_coords", TextureCoordStatesJson(material.TextureCoords) },
        { "selected_texture_coord", TextureCoordStateJson(material.SelectedTextureCoord) },
        { "selected_texture_coord_decoded", material.SelectedTextureCoordDecoded },
        { "raw_texture_stage_selector_decoded", material.RawTextureStageSelectorDecoded },
        { "raw_texture_stage_count", material.RawTextureStageCount },
        { "raw_texture_stage_slots", RawTextureStageSlotsJson(material.RawTextureStageSlots) },
        { "raw_texture_stage_count_matches_texture_mappers_used",
          material.RawTextureStageCountMatchesMappers },
        { "raw_texture_stage_count_exceeds_texture_mappers_used",
          material.RawTextureStageCountExceedsMappers },
        { "material_colors_decoded", material.MaterialColorsDecoded },
        { "material_colors", MaterialColorsJson(material) },
        { "texture_binding_source", material.TextureBindingSource },
        { "texture_binding_resolved_from_raw_stage_selector",
          material.TextureBindingResolvedFromRawStageSelector },
        { "texture_index", material.TextureIndex },
        { "texture_mapper_slot", material.TextureMapperSlot },
        { "native_sampler_state_decoded", material.NativeSamplerStateDecoded },
        { "native_sampler_min_filter", HexU32(material.NativeSamplerMinFilter, 4) },
        { "native_sampler_mag_filter", HexU32(material.NativeSamplerMagFilter, 4) },
        { "native_sampler_wrap_s", HexU32(material.NativeSamplerWrapS, 4) },
        { "native_sampler_wrap_t", HexU32(material.NativeSamplerWrapT, 4) },
        { "native_sampler_lod_bias", material.NativeSamplerLodBias },
        { "native_sampler_state_source", material.NativeSamplerStateSource },
        { "texture_mapper_texture_indices",
          TextureMapperTextureIndicesJson(material.TextureMapperTextureIndices) },
        { "texture_mapper_sampler_states",
          TextureMapperSamplerStatesJson(material.TextureMapperSamplerStates) },
        { "secondary_texture_index", material.SecondaryTextureIndex },
        { "secondary_texture_mapper_slot", material.SecondaryTextureMapperSlot },
        { "secondary_texture_binding_source", material.SecondaryTextureBindingSource },
        { "tertiary_texture_index", material.TertiaryTextureIndex },
        { "tertiary_texture_mapper_slot", material.TertiaryTextureMapperSlot },
        { "tertiary_texture_binding_source", material.TertiaryTextureBindingSource },
        { "native_material_animation_applied", material.NativeMaterialAnimationApplied },
        { "native_material_animation_source", material.NativeMaterialAnimationSource },
        { "native_material_animation_role", material.NativeMaterialAnimationRole },
        { "native_material_animation_texture_name", material.NativeMaterialAnimationTextureName },
        { "native_material_animation_texture_frame_index",
          material.NativeMaterialAnimationTextureFrameIndex },
        { "native_material_animation_frame", material.NativeMaterialAnimationFrame },
        { "native_material_animation_texture_applied",
          material.NativeMaterialAnimationTextureApplied },
        { "native_material_animation_color_applied",
          material.NativeMaterialAnimationColorApplied },
        { "native_material_animation_color_kind",
          material.NativeMaterialAnimationColorKind },
        { "native_material_animation_color_selector",
          material.NativeMaterialAnimationColorSelector },
        { "native_material_animation_color_component_mask",
          material.NativeMaterialAnimationColorComponentMask },
        { "native_material_animation_color_sample_frame",
          material.NativeMaterialAnimationColorSampleFrame },
        { "native_material_animation_color_value",
          ColorJson(material.NativeMaterialAnimationColorValue) },
        { "native_runtime_material_color_override_applied",
          material.NativeRuntimeMaterialColorOverrideApplied },
        { "native_runtime_material_color_override_source",
          material.NativeRuntimeMaterialColorOverrideSource },
        { "native_runtime_material_color_override_slot",
          material.NativeRuntimeMaterialColorOverrideSlot },
        { "native_runtime_material_color_override_value",
          ColorJson(material.NativeRuntimeMaterialColorOverrideValue) },
        { "native_runtime_vertex_alpha_blend", material.NativeRuntimeVertexAlphaBlend },
        { "native_kankyo_layer_blend_alpha_applied",
          material.NativeKankyoLayerBlendAlphaApplied },
        { "native_kankyo_layer_blend_alpha",
          static_cast<int>(material.NativeKankyoLayerBlendAlpha) },
        { "native_kankyo_layer_blend_alpha_source",
          material.NativeKankyoLayerBlendAlphaSource },
        { "native_material_animation_texture_transform_applied",
          material.NativeMaterialAnimationTextureTransformApplied },
        { "native_material_animation_texture_transform_kind",
          material.NativeMaterialAnimationTextureTransformKind },
        { "native_material_animation_texture_transform_selector",
          material.NativeMaterialAnimationTextureTransformSelector },
        { "native_material_animation_texture_transform_component_mask",
          material.NativeMaterialAnimationTextureTransformComponentMask },
        { "native_material_animation_texture_transform_sample_frame",
          material.NativeMaterialAnimationTextureTransformSampleFrame },
        { "textured", material.Textured },
        { "vertex_color_modulates_texture", material.VertexColorModulatesTexture },
        { "native_pica_lighting_applied", material.NativePicaLightingApplied },
        { "native_pica_directional_lighting_applied", material.NativePicaDirectionalLightingApplied },
        { "native_pica_vertex_lighting_applied", material.NativePicaVertexLightingApplied },
        { "native_pica_hemisphere_lighting_applied", material.NativePicaHemisphereLightingApplied },
        { "native_pica_cmb_vshader_lighting_accumulator_applied",
          material.NativePicaCmbVShaderLightingAccumulatorApplied },
        { "native_pica_cmb_vshader_lighting_active_light_count",
          material.NativePicaCmbVShaderLightingActiveLightCount },
        { "native_pica_cmb_vshader_lighting_ambient_slot_count",
          material.NativePicaCmbVShaderLightingAmbientSlotCount },
        { "native_pica_cmb_vshader_lighting_alpha_scale",
          material.NativePicaCmbVShaderLightingAlphaScale },
        { "native_pica_cmb_vshader_lighting_accumulator_source",
          material.NativePicaCmbVShaderLightingAccumulatorSource },
        { "native_pica_cmb_vshader_lighting_ambient_slot_source",
          material.NativePicaCmbVShaderLightingAmbientSlotSource },
        { "native_pica_fog_override_decoded", material.NativePicaFogOverrideDecoded },
        { "native_pica_fog_enabled", material.NativePicaFogEnabled },
        { "native_pica_fog_override_source", material.NativePicaFogOverrideSource },
        { "native_pica_primary_color_pre_lighting_scale_applied",
          material.NativePicaPrimaryColorPreLightingScaleApplied },
        { "native_pica_primary_color_pre_lighting_scale",
          Vec3Json(material.NativePicaPrimaryColorPreLightingScale) },
        { "native_pica_primary_color_pre_lighting_scale_source",
          material.NativePicaPrimaryColorPreLightingScaleSource },
        { "native_pica_self_shadow_candidate", material.NativePicaSelfShadowCandidate },
        { "native_pica_self_shadow_applied", material.NativePicaSelfShadowApplied },
        { "native_pica_shadow2d_material_texture_projection_input_candidate",
          material.NativePicaShadow2dMaterialTextureProjectionInputCandidate },
        { "native_pica_shadow2d_material_texture_projection_input_decoded",
          material.NativePicaShadow2dMaterialTextureProjectionInputDecoded },
        { "native_pica_shadow2d_material_texture_projection_input_source",
          material.NativePicaShadow2dMaterialTextureProjectionInputSource },
        { "native_pica_shadow2d_texcoord0_w_input_candidate",
          material.NativePicaShadow2dTexCoord0WInputCandidate },
        { "native_pica_shadow2d_texcoord0_w_input_decoded",
          material.NativePicaShadow2dTexCoord0WInputDecoded },
        { "native_pica_shadow2d_texcoord0_w_input_source",
          material.NativePicaShadow2dTexCoord0WInputSource },
        { "native_pica_unlit_texture_env_route_decoded",
          material.NativePicaUnlitTextureEnvRouteDecoded },
        { "native_pica_unlit_texture_env_route_requires_native_color",
          material.NativePicaUnlitTextureEnvRouteRequiresNativeColor },
        { "native_pica_unlit_texture_env_route_native_color_available",
          material.NativePicaUnlitTextureEnvRouteNativeColorAvailable },
        { "native_pica_unlit_texture_env_route_applied",
          material.NativePicaUnlitTextureEnvRouteApplied },
        { "native_pica_unlit_texture_env_route_source",
          material.NativePicaUnlitTextureEnvRouteSource },
        { "native_runtime_material_lane_decoded",
          material.NativeRuntimeMaterialLaneDecoded },
        { "native_runtime_material_lane_index",
          material.NativeRuntimeMaterialLaneIndex },
        { "native_runtime_material_lane_stride_bytes",
          material.NativeRuntimeMaterialLaneStrideBytes },
        { "native_runtime_material_lane_source",
          material.NativeRuntimeMaterialLaneSource },
        { "native_pica_self_shadow_vertex_count", material.NativePicaSelfShadowVertexCount },
        { "native_pica_self_shadow_occluded_vertex_count",
          material.NativePicaSelfShadowOccludedVertexCount },
        { "native_pica_lighting_application", material.NativePicaLightingApplication },
        { "native_pica_lighting_application_source", material.NativePicaLightingApplicationSource },
        { "native_pica_vertex_hemisphere_lighting_source",
          material.NativePicaVertexHemisphereLightingSource },
        { "native_pica_vertex_hemisphere_vector_resolved",
          material.NativePicaVertexHemisphereVectorResolved },
        { "native_pica_vertex_hemisphere_vector_pending",
          material.NativePicaVertexHemisphereVectorPending },
        { "native_pica_vertex_hemisphere_vector_source",
          material.NativePicaVertexHemisphereVectorSource },
        { "native_pica_vertex_hemisphere_actor_vs_color_packet_applied",
          material.NativePicaVertexHemisphereActorVsColorPacketApplied },
        { "native_pica_vertex_hemisphere_color_source",
          material.NativePicaVertexHemisphereColorSource },
        { "native_pica_vertex_hemisphere_ambient_color",
          ColorJson(material.NativePicaVertexHemisphereAmbientColor) },
        { "native_pica_vertex_hemisphere_diffuse0_color",
          ColorJson(material.NativePicaVertexHemisphereDiffuse0Color) },
        { "native_pica_vertex_hemisphere_diffuse1_color",
          ColorJson(material.NativePicaVertexHemisphereDiffuse1Color) },
        { "native_pica_effective_material_diffuse_resolved",
          material.NativePicaEffectiveMaterialDiffuseResolved },
        { "native_pica_effective_material_diffuse_uses_ambient",
          material.NativePicaEffectiveMaterialDiffuseUsesAmbient },
        { "native_pica_effective_material_diffuse_source",
          material.NativePicaEffectiveMaterialDiffuseSource },
        { "native_pica_effective_material_diffuse_color",
          ColorJson(material.NativePicaEffectiveMaterialDiffuseColor) },
        { "native_pica_lighting_diagnostics",
          PicaLightingBatchDiagnosticsJson(material.NativePicaLightingDiagnostics) },
        { "native_pica_material_lut_input_packet_available",
          material.NativePicaMaterialLutInputPacketAvailable },
        { "native_pica_material_lut_input_packet_complete",
          material.NativePicaMaterialLutInputPacketComplete },
        { "native_pica_material_lut_input_packet_used_for_fragment_lighting",
          material.NativePicaMaterialLutInputPacketUsedForFragmentLighting },
        { "native_pica_material_lut_input_evaluation_applied",
          material.NativePicaMaterialLutInputEvaluationApplied },
        { "native_pica_material_lut_input_evaluation_pending",
          material.NativePicaMaterialLutInputEvaluationPending },
        { "native_pica_material_lut_input_evaluation_required",
          material.NativePicaMaterialLutInputEvaluationRequired },
        { "native_pica_material_lut_input_evaluation_culled_as_unused",
          material.NativePicaMaterialLutInputEvaluationCulledAsUnused },
        { "native_pica_material_lut_input_application_source",
          material.NativePicaMaterialLutInputApplicationSource },
        { "native_pica_bump_mode_available", material.NativePicaBumpModeAvailable },
        { "native_pica_bump_mode_recognized", material.NativePicaBumpModeRecognized },
        { "native_pica_bump_mode", material.NativePicaBumpMode },
        { "native_pica_bump_mode_active", material.NativePicaBumpModeActive },
        { "native_pica_bump_mode_backend_pending",
          material.NativePicaBumpModeBackendPending },
        { "native_pica_bump_texture_unit_recognized",
          material.NativePicaBumpTextureUnitRecognized },
        { "native_pica_bump_texture_unit", material.NativePicaBumpTextureUnit },
        { "native_pica_bump_texture_index", material.NativePicaBumpTextureIndex },
        { "native_pica_bump_normal_map_backend_supported",
          material.NativePicaBumpNormalMapBackendSupported },
        { "native_pica_bump_normal_map_applied",
          material.NativePicaBumpNormalMapApplied },
        { "native_pica_bump_mode_source", material.NativePicaBumpModeSource },
        { "native_pica_bump_texture_source", material.NativePicaBumpTextureSource },
        { "native_material_lighting_flag3_available",
          material.NativeMaterialLightingFlag3Available },
        { "native_material_lighting_flag3_active",
          material.NativeMaterialLightingFlag3Active },
        { "native_material_lighting_flag3_backend_pending",
          material.NativeMaterialLightingFlag3BackendPending },
        { "native_material_lighting_flag3_source",
          material.NativeMaterialLightingFlag3Source },
        { "native_pica_fragment_lighting_config_packet_available",
          material.NativePicaFragmentLightingConfigPacketAvailable },
        { "native_pica_fragment_lighting_config_packet_complete",
          material.NativePicaFragmentLightingConfigPacketComplete },
        { "native_pica_fragment_lighting_config_runtime_override_pending",
          material.NativePicaFragmentLightingConfigRuntimeOverridePending },
        { "native_pica_fragment_lighting_config_known_payload_word_count",
          material.NativePicaFragmentLightingConfigKnownPayloadWordCount },
        { "native_pica_fragment_lighting_config_application_source",
          material.NativePicaFragmentLightingConfigApplicationSource },
        { "native_pica_self_shadow_application_source",
          material.NativePicaSelfShadowApplicationSource },
        { "native_pica_vertex_lighting_deferred", material.NativePicaVertexLightingDeferred },
        { "native_pica_hemisphere_lighting_deferred", material.NativePicaHemisphereLightingDeferred },
        { "native_material_lighting_complete",
          NativeMaterialLightingComplete(material) },
        { "native_material_lighting_incomplete_reasons",
          NativeMaterialLightingIncompleteReasons(material) },
        { "native_material_lighting_complete_source",
          "oot3d_native_cmb_material_lighting_block_lut_fragment_config_vertex_vector_bump_flag3_textureenv_contract" },
        { "texture_has_native_alpha", material.TextureHasNativeAlpha },
        { "native_material_combiner_requires_decoder",
          material.NativeMaterialCombinerRequiresDecoder },
    };
}

nlohmann::json NativeBatchTextureEnvProgramSummaryJson(
    const Oot3dNativeRenderTextureEnvProgram& program) {
    return {
        { "decoded", program.Decoded },
        { "stage_count", program.StageCount },
        { "color_shader_path", program.ColorShaderPath },
        { "color_shader_path_supported", program.ColorShaderPathSupported },
        { "color_shader_path_applied", program.ColorShaderPathApplied },
        { "rgb_route_decoded", program.RgbRouteDecoded },
        { "uses_primary_color", program.UsesPrimaryColor },
        { "uses_fragment_lighting_color", program.UsesFragmentLightingColor },
        { "uses_previous", program.UsesPrevious },
        { "uses_previous_buffer", program.UsesPreviousBuffer },
        { "uses_constant_color", program.UsesConstantColor },
        { "uses_texture0", program.UsesTexture0 },
        { "uses_texture1", program.UsesTexture1 },
        { "uses_texture2", program.UsesTexture2 },
        { "uses_texture3", program.UsesTexture3 },
        { "requires_multi_stage_evaluation", program.RequiresMultiStageEvaluation },
        { "requires_multi_texture_sampling", program.RequiresMultiTextureSampling },
        { "requires_constant_color_selection", program.RequiresConstantColorSelection },
        { "requires_texture_color_add", program.RequiresTextureColorAdd },
        { "vertex_color_constant_stage_count", program.VertexColorConstantStageCount },
        { "vertex_color_multiplier", ColorJson(program.VertexColorMultiplier) },
        { "primary_color_multiplier_resolved", program.PrimaryColorMultiplierResolved },
        { "primary_color_multiplier_stage_count", program.PrimaryColorMultiplierStageCount },
        { "primary_color_multiplier", Vec3Json(program.PrimaryColorMultiplier) },
        { "texture_color_multiplier_resolved", program.TextureColorMultiplierResolved },
        { "texture_color_multiplier_stage_count", program.TextureColorMultiplierStageCount },
        { "texture_color_multiplier", Vec3Json(program.TextureColorMultiplier) },
        { "texture_color_addend_resolved", program.TextureColorAddendResolved },
        { "texture_color_add_stage_count", program.TextureColorAddStageCount },
        { "texture_color_addend", ColorJson(program.TextureColorAddend) },
        { "texture_color_add_uses_texture0_alpha", program.TextureColorAddUsesTexture0Alpha },
        { "texture1_color_add_resolved", program.Texture1ColorAddResolved },
        { "texture1_color_add_stage_count", program.Texture1ColorAddStageCount },
        { "texture1_color_add_mapper_slot", program.Texture1ColorAddMapperSlot },
        { "texture1_color_add_uses_texture0_alpha", program.Texture1ColorAddUsesTexture0Alpha },
        { "texture1_color_add_source", program.Texture1ColorAddSource },
        { "texture1_color_multiply_resolved", program.Texture1ColorMultiplyResolved },
        { "texture1_color_multiply_stage_count", program.Texture1ColorMultiplyStageCount },
        { "texture1_color_multiply_mapper_slot", program.Texture1ColorMultiplyMapperSlot },
        { "texture1_color_multiply_source", program.Texture1ColorMultiplySource },
        { "texture0_texture1_add_then_primary_color_modulate_resolved",
          program.Texture0Texture1AddThenPrimaryColorModulateResolved },
        { "texture0_texture1_add_then_primary_color_modulate_stage_count",
          program.Texture0Texture1AddThenPrimaryColorModulateStageCount },
        { "texture0_texture1_add_then_primary_color_modulate_mapper_slot",
          program.Texture0Texture1AddThenPrimaryColorModulateMapperSlot },
        { "texture0_texture1_add_then_primary_color_modulate_source",
          program.Texture0Texture1AddThenPrimaryColorModulateSource },
        { "texture1_texture2_multiply_add_previous_resolved",
          program.Texture1Texture2MultiplyAddPreviousResolved },
        { "texture1_texture2_multiply_add_previous_stage_count",
          program.Texture1Texture2MultiplyAddPreviousStageCount },
        { "texture1_texture2_multiply_add_previous_texture1_mapper_slot",
          program.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot },
        { "texture1_texture2_multiply_add_previous_texture2_mapper_slot",
          program.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot },
        { "texture1_texture2_multiply_add_previous_source",
          program.Texture1Texture2MultiplyAddPreviousSource },
        { "texture0_texture1_add_multiply_texture0_resolved",
          program.Texture0Texture1AddMultiplyTexture0Resolved },
        { "texture0_texture1_add_multiply_texture0_stage_count",
          program.Texture0Texture1AddMultiplyTexture0StageCount },
        { "texture0_texture1_add_multiply_texture0_mapper_slot",
          program.Texture0Texture1AddMultiplyTexture0MapperSlot },
        { "texture0_texture1_add_multiply_texture0_source",
          program.Texture0Texture1AddMultiplyTexture0Source },
        { "texture0_primary_color_alpha_modulate_resolved",
          program.Texture0PrimaryColorAlphaModulateResolved },
        { "texture0_primary_color_alpha_modulate_stage_count",
          program.Texture0PrimaryColorAlphaModulateStageCount },
        { "texture0_primary_color_alpha_modulate_source",
          program.Texture0PrimaryColorAlphaModulateSource },
        { "texture0_constant_color_alpha_modulate_resolved",
          program.Texture0ConstantColorAlphaModulateResolved },
        { "texture0_constant_color_alpha_modulate_stage_count",
          program.Texture0ConstantColorAlphaModulateStageCount },
        { "texture0_constant_color_alpha_modulate_source",
          program.Texture0ConstantColorAlphaModulateSource },
        { "alpha_multiplier_resolved", program.AlphaMultiplierResolved },
        { "alpha_multiplier_stage_count", program.AlphaMultiplierStageCount },
        { "alpha_multiplier", program.AlphaMultiplier },
        { "alpha_multiplier_source", program.AlphaMultiplierSource },
    };
}

nlohmann::json NativeRenderBatchSummaryJson(const Oot3dNativeRenderModel& model,
                                            const Oot3dNativeRenderBatch& batch,
                                            size_t batchIndex) {
    const auto vertexColorStats = BatchVertexColorStats(batch, false);
    const auto shaderPrimaryColorStats =
        BatchVertexColorStats(batch, batch.Material.TextureEnvProgram.ColorShaderPathApplied);
    return {
        { "batch_index", batchIndex },
        { "mesh_index", batch.MeshIndex },
        { "shape_index", batch.ShapeIndex },
        { "visibility_id", batch.VisibilityId },
        { "material_index", batch.MaterialIndex },
        { "primitive_index", batch.PrimitiveIndex },
        { "skinning_mode", batch.SkinningMode },
        { "native_cmb_attribute_flags", batch.NativeCmbAttributeFlags },
        { "native_cmb_constant_attribute_flags", batch.NativeCmbConstantAttributeFlags },
        { "native_cmb_constant_position",
          (batch.NativeCmbConstantAttributeFlags & kOot3dCmbAttributePosition) != 0 },
        { "native_cmb_constant_normal",
          (batch.NativeCmbConstantAttributeFlags & kOot3dCmbAttributeNormal) != 0 },
        { "native_cmb_constant_color",
          (batch.NativeCmbConstantAttributeFlags & kOot3dCmbAttributeColor) != 0 },
        { "native_cmb_constant_uv0",
          (batch.NativeCmbConstantAttributeFlags & kOot3dCmbAttributeUv0) != 0 },
        { "vertex_count", batch.Vertices.size() },
        { "triangle_count", batch.TriangleCount() },
        { "texture", NativeBatchTextureJson(model, batch) },
        { "secondary_texture", NativeBatchSecondaryTextureJson(model, batch) },
        { "tertiary_texture", NativeBatchTertiaryTextureJson(model, batch) },
        { "native_color_available", std::any_of(batch.Vertices.begin(), batch.Vertices.end(),
                                                [](const Oot3dNativeRenderVertex& vertex) {
                                                    return vertex.NativeColorAvailable;
                                                }) },
        { "vertex_color_stats", ColorStatsJson(vertexColorStats) },
        { "estimated_shader_primary_color_stats", ColorStatsJson(shaderPrimaryColorStats) },
        { "material_lighting_flags", MaterialLightingFlagsJson(batch.Material) },
        { "material_lighting_block", MaterialLightingBlockJson(batch.Material.MaterialLightingBlock) },
        { "material_pica_lut_input",
          MaterialPicaLutInputStateJson(batch.Material.MaterialPicaLutInput) },
        { "material_fragment_lighting_config",
          MaterialFragmentLightingConfigStateJson(batch.Material.MaterialFragmentLightingConfig) },
        { "material_colors_decoded", batch.Material.MaterialColorsDecoded },
        { "material_colors", MaterialColorsJson(batch.Material) },
        { "texture_env_program",
          NativeBatchTextureEnvProgramSummaryJson(batch.Material.TextureEnvProgram) },
        { "textured", batch.Material.Textured },
        { "native_material_animation_applied", batch.Material.NativeMaterialAnimationApplied },
        { "native_material_animation_source", batch.Material.NativeMaterialAnimationSource },
        { "native_material_animation_role", batch.Material.NativeMaterialAnimationRole },
        { "native_material_animation_texture_name",
          batch.Material.NativeMaterialAnimationTextureName },
        { "native_material_animation_texture_frame_index",
          batch.Material.NativeMaterialAnimationTextureFrameIndex },
        { "native_material_animation_frame", batch.Material.NativeMaterialAnimationFrame },
        { "native_material_animation_texture_applied",
          batch.Material.NativeMaterialAnimationTextureApplied },
        { "native_material_animation_color_applied",
          batch.Material.NativeMaterialAnimationColorApplied },
        { "native_material_animation_color_kind",
          batch.Material.NativeMaterialAnimationColorKind },
        { "native_material_animation_color_selector",
          batch.Material.NativeMaterialAnimationColorSelector },
        { "native_material_animation_color_component_mask",
          batch.Material.NativeMaterialAnimationColorComponentMask },
        { "native_material_animation_color_sample_frame",
          batch.Material.NativeMaterialAnimationColorSampleFrame },
        { "native_material_animation_color_value",
          ColorJson(batch.Material.NativeMaterialAnimationColorValue) },
        { "native_runtime_material_color_override_applied",
          batch.Material.NativeRuntimeMaterialColorOverrideApplied },
        { "native_runtime_material_color_override_source",
          batch.Material.NativeRuntimeMaterialColorOverrideSource },
        { "native_runtime_material_color_override_slot",
          batch.Material.NativeRuntimeMaterialColorOverrideSlot },
        { "native_runtime_material_color_override_value",
          ColorJson(batch.Material.NativeRuntimeMaterialColorOverrideValue) },
        { "native_runtime_vertex_alpha_blend",
          batch.Material.NativeRuntimeVertexAlphaBlend },
        { "native_material_animation_texture_transform_applied",
          batch.Material.NativeMaterialAnimationTextureTransformApplied },
        { "native_material_animation_texture_transform_kind",
          batch.Material.NativeMaterialAnimationTextureTransformKind },
        { "native_material_animation_texture_transform_selector",
          batch.Material.NativeMaterialAnimationTextureTransformSelector },
        { "native_material_animation_texture_transform_component_mask",
          batch.Material.NativeMaterialAnimationTextureTransformComponentMask },
        { "native_material_animation_texture_transform_sample_frame",
          batch.Material.NativeMaterialAnimationTextureTransformSampleFrame },
        { "vertex_color_modulates_texture", batch.Material.VertexColorModulatesTexture },
        { "native_material_available", batch.Material.NativeMaterialAvailable },
        { "raw_material_size", batch.Material.NativeMaterialRawSize },
        { "raw_material_fnv1a64", std::to_string(batch.Material.NativeMaterialRawFnv1a64) },
        { "native_pica_lighting_applied", batch.Material.NativePicaLightingApplied },
        { "native_pica_lighting_application", batch.Material.NativePicaLightingApplication },
        { "native_pica_lighting_application_source",
          batch.Material.NativePicaLightingApplicationSource },
        { "native_pica_directional_lighting_applied",
          batch.Material.NativePicaDirectionalLightingApplied },
        { "native_pica_vertex_lighting_applied", batch.Material.NativePicaVertexLightingApplied },
        { "native_pica_hemisphere_lighting_applied",
          batch.Material.NativePicaHemisphereLightingApplied },
        { "native_pica_cmb_vshader_lighting_accumulator_applied",
          batch.Material.NativePicaCmbVShaderLightingAccumulatorApplied },
        { "native_pica_cmb_vshader_lighting_active_light_count",
          batch.Material.NativePicaCmbVShaderLightingActiveLightCount },
        { "native_pica_cmb_vshader_lighting_ambient_slot_count",
          batch.Material.NativePicaCmbVShaderLightingAmbientSlotCount },
        { "native_pica_cmb_vshader_lighting_alpha_scale",
          batch.Material.NativePicaCmbVShaderLightingAlphaScale },
        { "native_pica_cmb_vshader_lighting_accumulator_source",
          batch.Material.NativePicaCmbVShaderLightingAccumulatorSource },
        { "native_pica_cmb_vshader_lighting_ambient_slot_source",
          batch.Material.NativePicaCmbVShaderLightingAmbientSlotSource },
        { "native_pica_primary_color_pre_lighting_scale_applied",
          batch.Material.NativePicaPrimaryColorPreLightingScaleApplied },
        { "native_pica_primary_color_pre_lighting_scale",
          Vec3Json(batch.Material.NativePicaPrimaryColorPreLightingScale) },
        { "native_pica_primary_color_pre_lighting_scale_source",
          batch.Material.NativePicaPrimaryColorPreLightingScaleSource },
        { "native_pica_vertex_lighting_deferred", batch.Material.NativePicaVertexLightingDeferred },
        { "native_pica_hemisphere_lighting_deferred",
          batch.Material.NativePicaHemisphereLightingDeferred },
        { "native_pica_vertex_hemisphere_lighting_source",
          batch.Material.NativePicaVertexHemisphereLightingSource },
        { "native_pica_vertex_hemisphere_vector_resolved",
          batch.Material.NativePicaVertexHemisphereVectorResolved },
        { "native_pica_vertex_hemisphere_vector_pending",
          batch.Material.NativePicaVertexHemisphereVectorPending },
        { "native_pica_vertex_hemisphere_vector_source",
          batch.Material.NativePicaVertexHemisphereVectorSource },
        { "native_pica_vertex_hemisphere_actor_vs_color_packet_applied",
          batch.Material.NativePicaVertexHemisphereActorVsColorPacketApplied },
        { "native_pica_vertex_hemisphere_color_source",
          batch.Material.NativePicaVertexHemisphereColorSource },
        { "native_pica_vertex_hemisphere_ambient_color",
          ColorJson(batch.Material.NativePicaVertexHemisphereAmbientColor) },
        { "native_pica_vertex_hemisphere_diffuse0_color",
          ColorJson(batch.Material.NativePicaVertexHemisphereDiffuse0Color) },
        { "native_pica_vertex_hemisphere_diffuse1_color",
          ColorJson(batch.Material.NativePicaVertexHemisphereDiffuse1Color) },
        { "native_pica_effective_material_diffuse_resolved",
          batch.Material.NativePicaEffectiveMaterialDiffuseResolved },
        { "native_pica_effective_material_diffuse_uses_ambient",
          batch.Material.NativePicaEffectiveMaterialDiffuseUsesAmbient },
        { "native_pica_effective_material_diffuse_source",
          batch.Material.NativePicaEffectiveMaterialDiffuseSource },
        { "native_pica_effective_material_diffuse_color",
          ColorJson(batch.Material.NativePicaEffectiveMaterialDiffuseColor) },
        { "native_pica_lighting_diagnostics",
          PicaLightingBatchDiagnosticsJson(batch.Material.NativePicaLightingDiagnostics) },
        { "native_pica_material_lut_input_packet_available",
          batch.Material.NativePicaMaterialLutInputPacketAvailable },
        { "native_pica_material_lut_input_packet_complete",
          batch.Material.NativePicaMaterialLutInputPacketComplete },
        { "native_pica_material_lut_input_packet_used_for_fragment_lighting",
          batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting },
        { "native_pica_material_lut_input_evaluation_applied",
          batch.Material.NativePicaMaterialLutInputEvaluationApplied },
        { "native_pica_material_lut_input_evaluation_pending",
          batch.Material.NativePicaMaterialLutInputEvaluationPending },
        { "native_pica_material_lut_input_evaluation_required",
          batch.Material.NativePicaMaterialLutInputEvaluationRequired },
        { "native_pica_material_lut_input_evaluation_culled_as_unused",
          batch.Material.NativePicaMaterialLutInputEvaluationCulledAsUnused },
        { "native_pica_material_lut_input_application_source",
          batch.Material.NativePicaMaterialLutInputApplicationSource },
        { "native_pica_bump_mode_available", batch.Material.NativePicaBumpModeAvailable },
        { "native_pica_bump_mode_recognized", batch.Material.NativePicaBumpModeRecognized },
        { "native_pica_bump_mode", batch.Material.NativePicaBumpMode },
        { "native_pica_bump_mode_active", batch.Material.NativePicaBumpModeActive },
        { "native_pica_bump_mode_backend_pending",
          batch.Material.NativePicaBumpModeBackendPending },
        { "native_pica_bump_texture_unit_recognized",
          batch.Material.NativePicaBumpTextureUnitRecognized },
        { "native_pica_bump_texture_unit", batch.Material.NativePicaBumpTextureUnit },
        { "native_pica_bump_texture_index", batch.Material.NativePicaBumpTextureIndex },
        { "native_pica_bump_normal_map_backend_supported",
          batch.Material.NativePicaBumpNormalMapBackendSupported },
        { "native_pica_bump_normal_map_applied",
          batch.Material.NativePicaBumpNormalMapApplied },
        { "native_pica_bump_mode_source", batch.Material.NativePicaBumpModeSource },
        { "native_pica_bump_texture_source", batch.Material.NativePicaBumpTextureSource },
        { "native_material_lighting_flag3_available",
          batch.Material.NativeMaterialLightingFlag3Available },
        { "native_material_lighting_flag3_active",
          batch.Material.NativeMaterialLightingFlag3Active },
        { "native_material_lighting_flag3_backend_pending",
          batch.Material.NativeMaterialLightingFlag3BackendPending },
        { "native_material_lighting_flag3_source",
          batch.Material.NativeMaterialLightingFlag3Source },
        { "native_pica_fragment_lighting_config_packet_available",
          batch.Material.NativePicaFragmentLightingConfigPacketAvailable },
        { "native_pica_fragment_lighting_config_packet_complete",
          batch.Material.NativePicaFragmentLightingConfigPacketComplete },
        { "native_pica_fragment_lighting_config_runtime_override_pending",
          batch.Material.NativePicaFragmentLightingConfigRuntimeOverridePending },
        { "native_pica_fragment_lighting_config_known_payload_word_count",
          batch.Material.NativePicaFragmentLightingConfigKnownPayloadWordCount },
        { "native_pica_fragment_lighting_config_application_source",
          batch.Material.NativePicaFragmentLightingConfigApplicationSource },
        { "native_material_lighting_complete",
          NativeMaterialLightingComplete(batch.Material) },
        { "native_material_lighting_incomplete_reasons",
          NativeMaterialLightingIncompleteReasons(batch.Material) },
        { "native_material_lighting_complete_source",
          "oot3d_native_cmb_material_lighting_block_lut_fragment_config_vertex_vector_bump_flag3_textureenv_contract" },
        { "native_pica_self_shadow_candidate", batch.Material.NativePicaSelfShadowCandidate },
        { "native_pica_self_shadow_applied", batch.Material.NativePicaSelfShadowApplied },
        { "native_pica_shadow2d_material_texture_projection_input_candidate",
          batch.Material.NativePicaShadow2dMaterialTextureProjectionInputCandidate },
        { "native_pica_shadow2d_material_texture_projection_input_decoded",
          batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded },
        { "native_pica_shadow2d_texcoord0_w_input_candidate",
          batch.Material.NativePicaShadow2dTexCoord0WInputCandidate },
        { "native_pica_shadow2d_texcoord0_w_input_decoded",
          batch.Material.NativePicaShadow2dTexCoord0WInputDecoded },
        { "native_pica_unlit_texture_env_route_decoded",
          batch.Material.NativePicaUnlitTextureEnvRouteDecoded },
        { "native_pica_unlit_texture_env_route_requires_native_color",
          batch.Material.NativePicaUnlitTextureEnvRouteRequiresNativeColor },
        { "native_pica_unlit_texture_env_route_native_color_available",
          batch.Material.NativePicaUnlitTextureEnvRouteNativeColorAvailable },
        { "native_pica_unlit_texture_env_route_applied",
          batch.Material.NativePicaUnlitTextureEnvRouteApplied },
        { "native_pica_unlit_texture_env_route_source",
          batch.Material.NativePicaUnlitTextureEnvRouteSource },
        { "native_runtime_material_lane_decoded",
          batch.Material.NativeRuntimeMaterialLaneDecoded },
        { "native_runtime_material_lane_index",
          batch.Material.NativeRuntimeMaterialLaneIndex },
        { "native_runtime_material_lane_stride_bytes",
          batch.Material.NativeRuntimeMaterialLaneStrideBytes },
        { "native_runtime_material_lane_source",
          batch.Material.NativeRuntimeMaterialLaneSource },
    };
}

nlohmann::json NativeRenderBatchesSummaryJson(const Oot3dNativeRenderModel& model) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t batchIndex = 0; batchIndex < model.Batches.size(); ++batchIndex) {
        out.push_back(NativeRenderBatchSummaryJson(model, model.Batches[batchIndex], batchIndex));
    }
    return out;
}

Matrix4f MultiplyMatrices(const Matrix4f& a, const Matrix4f& b) {
    Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t i = 0; i < 4; ++i) {
                out.M[row][column] += a.M[row][i] * b.M[i][column];
            }
        }
    }
    return out;
}

Matrix4f ScaleMatrix(double scale) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    matrix.M[0][0] = static_cast<float>(scale);
    matrix.M[1][1] = static_cast<float>(scale);
    matrix.M[2][2] = static_cast<float>(scale);
    return matrix;
}

Matrix4f TranslateMatrix(Oot3dDemoVec3 translation) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    matrix.M[0][3] = static_cast<float>(translation.X);
    matrix.M[1][3] = static_cast<float>(translation.Y);
    matrix.M[2][3] = static_cast<float>(translation.Z);
    return matrix;
}

Matrix4f RotateXMatrix(double angle) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    matrix.M[1][1] = static_cast<float>(c);
    matrix.M[1][2] = static_cast<float>(-s);
    matrix.M[2][1] = static_cast<float>(s);
    matrix.M[2][2] = static_cast<float>(c);
    return matrix;
}

Matrix4f RotateYMatrix(double angle) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    matrix.M[0][0] = static_cast<float>(c);
    matrix.M[0][2] = static_cast<float>(s);
    matrix.M[2][0] = static_cast<float>(-s);
    matrix.M[2][2] = static_cast<float>(c);
    return matrix;
}

Matrix4f RotateZMatrix(double angle) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    matrix.M[0][0] = static_cast<float>(c);
    matrix.M[0][1] = static_cast<float>(-s);
    matrix.M[1][0] = static_cast<float>(s);
    matrix.M[1][1] = static_cast<float>(c);
    return matrix;
}

std::string NormalizedTextureName(std::string_view value) {
    std::string normalized(value);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return LowerAscii(std::move(normalized));
}

bool TextureIndexMatchesCmabTarget(int32_t textureIndex, const CmabMaterialAnimationBinding& binding) {
    if (textureIndex < 0) {
        return false;
    }
    return std::find(binding.TargetTextureIndices.begin(), binding.TargetTextureIndices.end(), textureIndex) !=
           binding.TargetTextureIndices.end();
}

uint32_t MaterialAnimationMapperSlot(const Oot3dNativeRenderMaterialState& material,
                                     const CmabMaterialAnimationBinding& binding) {
    for (uint32_t slot = 0; slot < material.TextureMapperTextureIndices.size(); ++slot) {
        if (TextureIndexMatchesCmabTarget(material.TextureMapperTextureIndices[slot], binding)) {
            return slot;
        }
    }
    return material.TextureMapperSlot;
}

bool MaterialLaneMatchesCmabBinding(const Oot3dNativeRenderMaterialState& material,
                                    const CmabMaterialAnimationBinding& binding) {
    if (!material.NativeRuntimeMaterialLaneDecoded) {
        return false;
    }
    return std::find(binding.NativeRuntimeMaterialLaneIndices.begin(),
                     binding.NativeRuntimeMaterialLaneIndices.end(),
                     material.NativeRuntimeMaterialLaneIndex) != binding.NativeRuntimeMaterialLaneIndices.end();
}

bool MaterialLaneMatchesCmabRecord(const Oot3dNativeRenderMaterialState& material,
                                   const CmabMmadRecord& record) {
    if (!material.NativeRuntimeMaterialLaneDecoded) {
        return false;
    }
    return material.NativeRuntimeMaterialLaneIndex == static_cast<int32_t>(record.TargetMaterialIndex);
}

float EffectiveCmabMaterialAnimationFrame(uint32_t frameCountCandidate, uint32_t loopModeCandidate,
                                          float frame) {
    if (!std::isfinite(frame)) {
        return 0.0f;
    }
    if (frameCountCandidate == 0) {
        return std::max(frame, 0.0f);
    }
    if (loopModeCandidate != 0) {
        float wrapped = std::fmod(frame, static_cast<float>(frameCountCandidate));
        if (wrapped < 0.0f) {
            wrapped += static_cast<float>(frameCountCandidate);
        }
        return wrapped;
    }
    return std::max(frame, 0.0f);
}

float EffectiveCmabMaterialAnimationFrame(const CmabMaterialAnimationBinding& binding, float frame) {
    return EffectiveCmabMaterialAnimationFrame(
        binding.FrameCountCandidate, binding.LoopModeCandidate, frame);
}

uint8_t CmabNormalizedFloatToColorByte(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(scaled)), 0, 255));
}

bool ApplyCmabColorCurveToColor(const CmabSourceCurve& curve, float frame, ColorRgba8& color,
                                uint32_t& componentMask) {
    if (!curve.Decoded || curve.ComponentIndex > 3) {
        return false;
    }

    const uint8_t value = CmabNormalizedFloatToColorByte(SampleCmabSourceCurve(curve, frame));
    switch (curve.ComponentIndex) {
        case 0:
            color.R = value;
            break;
        case 1:
            color.G = value;
            break;
        case 2:
            color.B = value;
            break;
        case 3:
            color.A = value;
            break;
        default:
            return false;
    }
    componentMask |= 1u << curve.ComponentIndex;
    return true;
}

bool ApplyCmabColorCurvesToColor(const CmabMmadRecord& record, float frame, ColorRgba8& color,
                                 uint32_t& componentMask) {
    if (record.NativeSourceCurves.empty()) {
        return false;
    }

    componentMask = 0;
    for (const auto& curve : record.NativeSourceCurves) {
        if (!ApplyCmabColorCurveToColor(curve, frame, color, componentMask)) {
            componentMask = 0;
            return false;
        }
    }
    return componentMask != 0;
}

bool ApplyCmabTransformVec2CurvesToTextureCoord(const CmabMmadRecord& record, float frame,
                                                Oot3dNativeRenderTextureCoordState& coord,
                                                uint32_t& componentMask) {
    if (record.NativeSourceCurves.empty()) {
        return false;
    }

    componentMask = 0;
    for (const auto& curve : record.NativeSourceCurves) {
        if (!curve.Decoded || curve.ComponentIndex > 1) {
            componentMask = 0;
            return false;
        }

        const float value = SampleCmabSourceCurve(curve, frame);
        switch (curve.ComponentIndex) {
            case 0:
                coord.Translation.X = value;
                break;
            case 1:
                coord.Translation.Y = value;
                break;
            default:
                componentMask = 0;
                return false;
        }
        componentMask |= 1u << curve.ComponentIndex;
    }
    return componentMask != 0;
}

bool ApplyCmabTransformScalarCurveToTextureCoord(const CmabMmadRecord& record, float frame,
                                                 Oot3dNativeRenderTextureCoordState& coord,
                                                 uint32_t& componentMask) {
    if (record.NativeSourceCurves.empty()) {
        return false;
    }

    componentMask = 0;
    for (const auto& curve : record.NativeSourceCurves) {
        if (!curve.Decoded || curve.ComponentIndex != 0) {
            componentMask = 0;
            return false;
        }

        coord.Rotation = SampleCmabSourceCurve(curve, frame);
        componentMask |= 1u;
    }
    return componentMask != 0;
}

void RefreshRenderBatchTextureCoordsForSlot(Oot3dNativeRenderBatch& batch, uint32_t slot) {
    if (slot >= batch.Material.TextureCoords.size()) {
        return;
    }

    auto& coord = batch.Material.TextureCoords[slot];
    RefreshTextureCoordDerivedState(coord);
    coord.Source = "oot3d_cmb_material_texture_coord_plus_cmab_native_transform";
    coord.SelectedPrimary = batch.Material.TextureMapperSlot == slot;
    if (coord.SelectedPrimary) {
        batch.Material.SelectedTextureCoord = coord;
        batch.Material.SelectedTextureCoordDecoded = coord.Decoded;
    }

    for (auto& vertex : batch.Vertices) {
        if (!vertex.NativeSourceUv0Available) {
            continue;
        }
        if (batch.Material.TextureIndex >= 0 && batch.Material.TextureMapperSlot == slot) {
            vertex.Uv0 = ApplyTextureCoordState(coord, vertex.NativeSourceUv0, vertex.Normal,
                                                vertex.NativeNormalAvailable);
        }
        if (batch.Material.SecondaryTextureIndex >= 0 && batch.Material.SecondaryTextureMapperSlot == slot) {
            vertex.Uv1 = ApplyTextureCoordState(coord, vertex.NativeSourceUv0, vertex.Normal,
                                                vertex.NativeNormalAvailable);
        }
        if (batch.Material.TertiaryTextureIndex >= 0 && batch.Material.TertiaryTextureMapperSlot == slot) {
            vertex.Uv2 = ApplyTextureCoordState(coord, vertex.NativeSourceUv0, vertex.Normal,
                                                vertex.NativeNormalAvailable);
        }
        if (vertex.NativePicaBumpUvAvailable &&
            batch.Material.NativePicaBumpTextureUnitRecognized &&
            batch.Material.NativePicaBumpTextureUnit == slot) {
            vertex.NativePicaBumpUv = ApplyTextureCoordState(coord, vertex.NativeSourceUv0, vertex.Normal,
                                                            vertex.NativeNormalAvailable);
        }
    }
}

const CmabTextureSwapFrameBinding* SelectCmabTextureSwapFrame(const CmabMaterialAnimationBinding& binding,
                                                              float frame) {
    if (binding.TextureSwapFrames.empty()) {
        return nullptr;
    }

    const float effectiveFrame = EffectiveCmabMaterialAnimationFrame(binding, frame);
    const CmabTextureSwapFrameBinding* selected = &binding.TextureSwapFrames.front();
    for (const auto& candidate : binding.TextureSwapFrames) {
        if (static_cast<float>(candidate.Frame) <= effectiveFrame) {
            selected = &candidate;
            continue;
        }
        break;
    }
    return selected;
}

int32_t FindRenderTextureIndexByName(const Oot3dNativeRenderModel& renderModel, std::string_view name) {
    const auto normalizedName = NormalizedTextureName(name);
    for (uint32_t textureIndex = 0; textureIndex < renderModel.Textures.size(); ++textureIndex) {
        if (NormalizedTextureName(renderModel.Textures[textureIndex].Name) == normalizedName) {
            return static_cast<int32_t>(textureIndex);
        }
    }
    return -1;
}

int32_t EnsureCmabFrameRenderTexture(Oot3dNativeRenderModel& renderModel,
                                     const CmabMaterialAnimation& animation,
                                     const CmabTextureSwapFrameBinding& frame) {
    if (frame.TargetTextureIndex >= 0 &&
        static_cast<size_t>(frame.TargetTextureIndex) < renderModel.Textures.size()) {
        return frame.TargetTextureIndex;
    }
    if (frame.EmbeddedTextureIndex < 0 ||
        static_cast<size_t>(frame.EmbeddedTextureIndex) >= animation.EmbeddedTextures.size()) {
        return -1;
    }

    const auto& texture = animation.EmbeddedTextures[static_cast<size_t>(frame.EmbeddedTextureIndex)];
    const int32_t existingIndex = FindRenderTextureIndexByName(renderModel, texture.Name);
    if (existingIndex >= 0) {
        return existingIndex;
    }

    renderModel.Textures.push_back(BuildRenderTextureFromCmbTexture(texture));
    return static_cast<int32_t>(renderModel.Textures.size() - 1);
}

} // namespace

ColorRgba8 EvaluateOot3dNativePicaLightingVertexColor(
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRenderModel& model,
    Oot3dNativeRenderBatch& batch,
    const Matrix4f& modelToWorld,
    const Oot3dNativeRenderVertex& vertex) {
    if (!lighting.Available || !batch.Material.NativePicaLightingApplied) {
        return vertex.Color;
    }
    Oot3dNativeRenderVertex sourceVertex = vertex;
    if (sourceVertex.NativePicaLightingInputColorAvailable) {
        sourceVertex.Color = sourceVertex.NativePicaLightingInputColor;
    }
    const bool nativeVertexOrHemisphereLighting =
        MaterialUsesNativeVertexOrHemisphereLighting(lighting, model, batch);
    const bool nativeVertexHemispherePacket = ModelUsesActorVsVertexHemisphereColorPacket(
        lighting, model, nativeVertexOrHemisphereLighting);
    const bool directionalVectorResolved = NativeVertexHemisphereDirectionalVectorResolved(
        lighting, model, nativeVertexOrHemisphereLighting);
    const auto effectiveMaterialDiffuse = ResolveNativePicaEffectiveMaterialDiffuse(
        lighting, batch.Material, nativeVertexOrHemisphereLighting);
    return ApplyPicaLightingToVertex(
        lighting, model, batch, modelToWorld, sourceVertex,
        nativeVertexOrHemisphereLighting, nativeVertexHemispherePacket,
        directionalVectorResolved, effectiveMaterialDiffuse.Color, false, 1.0);
}

Matrix4f Oot3dNativeRenderIdentityMatrix() {
    Matrix4f matrix{};
    for (size_t i = 0; i < 4; ++i) {
        matrix.M[i][i] = 1.0f;
    }
    return matrix;
}

size_t Oot3dNativeRenderBatch::TriangleCount() const {
    return Vertices.size() / 3;
}

size_t Oot3dNativeRenderModel::VertexCount() const {
    size_t count = 0;
    for (const auto& batch : Batches) {
        count += batch.Vertices.size();
    }
    return count;
}

size_t Oot3dNativeRenderModel::TriangleCount() const {
    size_t count = 0;
    for (const auto& batch : Batches) {
        count += batch.TriangleCount();
    }
    return count;
}

size_t Oot3dNativeRenderModel::TexturedBatchCount() const {
    return static_cast<size_t>(std::count_if(Batches.begin(), Batches.end(),
                                             [](const auto& batch) { return batch.Material.Textured; }));
}

size_t Oot3dNativeRenderModel::UploadableTextureCount() const {
    return static_cast<size_t>(
        std::count_if(Textures.begin(), Textures.end(), [](const auto& texture) {
            return texture.Rgba8Decoded && texture.Rgba8.size() == static_cast<size_t>(texture.Width) *
                                                         static_cast<size_t>(texture.Height) * 4;
        }));
}

Oot3dNativeRenderModel BuildOot3dNativeRenderModel(const CmbModel& model,
                                                    const Oot3dNativeRenderModelBuildOptions& options) {
    Oot3dNativeRenderModel renderModel;
    renderModel.NativeGeometryId = NextNativeGeometryId();
    renderModel.Source = model.Source;
    renderModel.Name = model.Name;
    renderModel.TransformBakedIntoVertices = options.BakeTransformIntoVertices;
    renderModel.NativeCmbSkeletonBoneCount =
        static_cast<uint32_t>(std::min<size_t>(model.Skeleton.Bones.size(),
                                               std::numeric_limits<uint32_t>::max()));
    renderModel.NativeCmbMeshPassSplitIndexDecoded = model.MeshPassSplitIndexDecoded;
    renderModel.NativeCmbMeshPassSplitIndex =
        model.MeshPassSplitIndexDecoded ? static_cast<uint32_t>(model.MeshPassSplitIndex) : 0;
    if (options.ResourceVisibility != nullptr) {
        renderModel.NativeCmbResourceVisibilityApplied = true;
        renderModel.NativeCmbResourceVisibility = *options.ResourceVisibility;
    }
    renderModel.Luts = BuildRenderLutSection(model.Luts);
    renderModel.TextureEnvTableRecordCount = model.TextureEnvSettings.size();
    if (!options.BakeTransformIntoVertices) {
        renderModel.ModelToWorld = BuildOot3dNativeRenderScaleTranslateTransform(options.Scale, options.Offset);
    }
    renderModel.Textures.reserve(model.Textures.size());
    for (const auto& texture : model.Textures) {
        renderModel.Textures.push_back(BuildRenderTextureFromCmbTexture(texture));
    }

    for (const auto& mesh : model.Meshes) {
        if (!MeshIndexSelected(options.SelectedMeshIndices, mesh.Index) ||
            !CmbResourceVisible(options.ResourceVisibility, mesh.VisibilityId) ||
            mesh.ShapeIndex >= model.Shapes.size()) {
            continue;
        }

        const auto& shape = model.Shapes[mesh.ShapeIndex];
        const auto* material = mesh.MaterialIndex < model.Materials.size() ? &model.Materials[mesh.MaterialIndex]
                                                                           : nullptr;
        const auto binding = PrimaryTextureBinding(model, mesh);
        const auto texture1Binding =
            material != nullptr
                ? TextureBindingForMapperSlot(model, *material, 1,
                                              "cmb_texture_env_stage_texture1_source")
                : TextureBinding{};
        const auto texture2Binding =
            material != nullptr
                ? TextureBindingForMapperSlot(model, *material, 2,
                                              "cmb_texture_env_stage_texture2_source")
                : TextureBinding{};
        const int32_t materialIndex = material != nullptr ? static_cast<int32_t>(mesh.MaterialIndex) : -1;
        const auto materialState = BuildMaterialState(model, material, binding, materialIndex);
        const auto bumpTextureBinding =
            material != nullptr && materialState.NativePicaBumpModeActive &&
                    materialState.NativePicaBumpTextureUnitRecognized
                ? TextureBindingForMapperSlot(
                      model, *material, materialState.NativePicaBumpTextureUnit,
                      "cmb_material_lighting_block_pica_bump_texture_unit")
                : TextureBinding{};

        for (size_t primitiveIndex = 0; primitiveIndex < shape.Primitives.size(); ++primitiveIndex) {
            const auto& primitive = shape.Primitives[primitiveIndex];
            Oot3dNativeRenderBatch batch;
            batch.MeshIndex = mesh.Index;
            batch.ShapeIndex = mesh.ShapeIndex;
            batch.VisibilityId = mesh.VisibilityId;
            batch.MaterialIndex = materialIndex;
            batch.PrimitiveIndex = static_cast<uint32_t>(primitiveIndex);
            batch.SkinningMode = primitive.SkinningMode;
            batch.NativeCmbAttributeFlags = shape.Flags;
            batch.NativeCmbConstantAttributeFlags = shape.AutoFlags;
            batch.Material = materialState;
            batch.Vertices.reserve((primitive.Indices.size() / 3) * 3);

            for (size_t i = 0; i + 2 < primitive.Indices.size(); i += 3) {
                const uint32_t indices[3] = {
                    primitive.Indices[i + 0],
                    primitive.Indices[i + 1],
                    primitive.Indices[i + 2],
                };
                if (indices[0] >= shape.Positions.size() || indices[1] >= shape.Positions.size() ||
                    indices[2] >= shape.Positions.size()) {
                    continue;
                }

                for (const auto index : indices) {
                    Oot3dNativeRenderVertex vertex;
                    vertex.NativeSourceVertexIndex = index;
                    vertex.NativeSourceVertexIndexAvailable = true;
                    vertex.Position =
                        NativeDemoPosedVertexPosition(primitive, shape.Positions[index], index, options.Pose,
                                                      options.SkinTransforms);
                    const bool nativeNormalAvailable =
                        shape.HasAttribute(kOot3dCmbAttributeNormal) && index < shape.Normals.size();
                    if (index < shape.Normals.size()) {
                        vertex.Normal =
                            NativeDemoPosedVertexNormal(primitive, shape.Normals[index], index, options.Pose,
                                                        options.SkinTransforms);
                        vertex.NativeNormalAvailable = nativeNormalAvailable;
                    }
                    if (options.BakeTransformIntoVertices) {
                        vertex.Position.X = static_cast<float>(vertex.Position.X * options.Scale + options.Offset.X);
                        vertex.Position.Y = static_cast<float>(vertex.Position.Y * options.Scale + options.Offset.Y);
                        vertex.Position.Z = static_cast<float>(vertex.Position.Z * options.Scale + options.Offset.Z);
                    }
                    if (index < shape.Uv0.size()) {
                        vertex.NativeSourceUv0 = shape.Uv0[index];
                        vertex.NativeSourceUv0Available = true;
                        vertex.Uv0 = ApplyTextureCoord(material, binding, vertex.NativeSourceUv0, vertex.Normal,
                                                       vertex.NativeNormalAvailable);
                        vertex.Uv1 = ApplyTextureCoord(material, texture1Binding, vertex.NativeSourceUv0,
                                                       vertex.Normal, vertex.NativeNormalAvailable);
                        vertex.Uv2 = ApplyTextureCoord(material, texture2Binding, vertex.NativeSourceUv0,
                                                       vertex.Normal, vertex.NativeNormalAvailable);
                        if (bumpTextureBinding.Valid) {
                            vertex.NativePicaBumpUv =
                                ApplyTextureCoord(material, bumpTextureBinding, vertex.NativeSourceUv0,
                                                  vertex.Normal, vertex.NativeNormalAvailable);
                            vertex.NativePicaBumpUvAvailable = true;
                        }
                    }
                    const bool nativeColorAvailable =
                        shape.HasAttribute(kOot3dCmbAttributeColor) && index < shape.Colors.size();
                    if (index < shape.Colors.size()) {
                        vertex.Color = shape.Colors[index];
                        vertex.NativeColorAvailable = nativeColorAvailable;
                    }
                    batch.Vertices.push_back(vertex);
                    ExpandBounds(renderModel.LocalBounds, vertex.Position);
                }
            }

            if (!batch.Vertices.empty()) {
                renderModel.Batches.push_back(std::move(batch));
            }
        }
    }

    if (!renderModel.LocalBounds.Valid) {
        const Oot3dDemoVec3 localOffset = options.BakeTransformIntoVertices ? options.Offset : Oot3dDemoVec3{};
        const double localScale = options.BakeTransformIntoVertices ? options.Scale : 1.0;
        renderModel.LocalBounds = NativeDemoModelBoundsFromMeshes(model, localOffset, options.SelectedMeshIndices,
                                                                  options.Pose, options.SkinTransforms, localScale);
    }

    renderModel.Bounds = Oot3dNativeRenderModelWorldBounds(renderModel);
    return renderModel;
}

void MarkOot3dNativeRenderModelVertexDataCacheable(Oot3dNativeRenderModel& model) {
    if (model.NativeGeometryId == 0) {
        model.NativeGeometryId = NextNativeGeometryId();
    }
    if (model.NativeGeometryContentVersion == 0) {
        model.NativeGeometryContentVersion = 1;
    }
    model.NativeVertexDataCacheable = true;
}

size_t ApplyOot3dNativeRenderModelResourceVisibility(
    Oot3dNativeRenderModel& renderModel, const std::vector<uint8_t>& resourceVisibility) {
    renderModel.NativeCmbResourceVisibilityApplied = true;
    renderModel.NativeCmbResourceVisibility = resourceVisibility;
    std::erase_if(renderModel.Batches, [&](const Oot3dNativeRenderBatch& batch) {
        return !CmbResourceVisible(&resourceVisibility, batch.VisibilityId);
    });

    renderModel.LocalBounds = {};
    for (const auto& batch : renderModel.Batches) {
        for (const auto& vertex : batch.Vertices) {
            ExpandBounds(renderModel.LocalBounds, vertex.Position);
        }
    }
    renderModel.Bounds = Oot3dNativeRenderModelWorldBounds(renderModel);
    if (renderModel.NativeVertexDataCacheable && ++renderModel.NativeGeometryContentVersion == 0) {
        renderModel.NativeGeometryContentVersion = 1;
    }
    return renderModel.Batches.size();
}

size_t ApplyOot3dNativeRenderModelPose(Oot3dNativeRenderModel& renderModel,
                                       const CmbModel& sourceModel,
                                       const CsabPose* pose,
                                       const std::vector<Matrix4f>* skinTransforms) {
    size_t updatedVertexCount = 0;
    renderModel.LocalBounds = {};
    for (auto& batch : renderModel.Batches) {
        if (batch.ShapeIndex >= sourceModel.Shapes.size()) {
            continue;
        }
        const auto& shape = sourceModel.Shapes[batch.ShapeIndex];
        if (batch.PrimitiveIndex >= shape.Primitives.size()) {
            continue;
        }
        const auto& primitive = shape.Primitives[batch.PrimitiveIndex];
        for (auto& vertex : batch.Vertices) {
            if (!vertex.NativeSourceVertexIndexAvailable ||
                vertex.NativeSourceVertexIndex >= shape.Positions.size()) {
                continue;
            }
            const auto sourceVertexIndex = vertex.NativeSourceVertexIndex;
            vertex.Position = NativeDemoPosedVertexPosition(
                primitive, shape.Positions[sourceVertexIndex], sourceVertexIndex, pose, skinTransforms);
            const bool nativeNormalAvailable =
                shape.HasAttribute(kOot3dCmbAttributeNormal) && sourceVertexIndex < shape.Normals.size();
            if (sourceVertexIndex < shape.Normals.size()) {
                vertex.Normal = NativeDemoPosedVertexNormal(
                    primitive, shape.Normals[sourceVertexIndex], sourceVertexIndex, pose, skinTransforms);
                vertex.NativeNormalAvailable = nativeNormalAvailable;
            }
            const bool nativeColorAvailable =
                shape.HasAttribute(kOot3dCmbAttributeColor) && sourceVertexIndex < shape.Colors.size();
            vertex.NativeColorAvailable = nativeColorAvailable;
            vertex.Color = sourceVertexIndex < shape.Colors.size()
                               ? shape.Colors[sourceVertexIndex]
                               : ColorRgba8{};
            vertex.NativePicaLightingInputColor = vertex.Color;
            vertex.NativePicaLightingInputColorAvailable = true;
            ExpandBounds(renderModel.LocalBounds, vertex.Position);
            ++updatedVertexCount;
        }
    }
    renderModel.Bounds = Oot3dNativeRenderModelWorldBounds(renderModel);
    if (renderModel.NativeVertexDataCacheable && ++renderModel.NativeGeometryContentVersion == 0) {
        renderModel.NativeGeometryContentVersion = 1;
    }
    return updatedVertexCount;
}

size_t StripOot3dNativeRenderModelTexturePayloads(Oot3dNativeRenderModel& renderModel) {
    size_t strippedByteCount = 0;
    for (auto& texture : renderModel.Textures) {
        size_t payloadByteCount = texture.Rgba8.size();
        for (const auto& mip : texture.AdditionalMipLevels) {
            payloadByteCount += mip.Rgba8.size();
        }
        if (payloadByteCount == 0) {
            continue;
        }
        if (texture.Rgba8ByteCount == 0) {
            texture.Rgba8ByteCount = payloadByteCount;
        }
        if (!texture.Rgba8HashAvailable && DecodedTexturePayloadsHaveStableIdentity(texture)) {
            texture.Rgba8Hash = DecodedTexturePayloadHash(texture);
            texture.Rgba8HashAvailable = true;
        }
        strippedByteCount += payloadByteCount;
        texture.Rgba8.clear();
        texture.Rgba8.shrink_to_fit();
        for (auto& mip : texture.AdditionalMipLevels) {
            if (mip.Rgba8ByteCount == 0) {
                mip.Rgba8ByteCount = mip.Rgba8.size();
            }
            if (mip.Rgba8Hash == 0 && mip.Rgba8Decoded) {
                mip.Rgba8Hash = Fnv1a64(mip.Rgba8);
            }
            mip.Rgba8.clear();
            mip.Rgba8.shrink_to_fit();
        }
    }
    return strippedByteCount;
}

float ResolveOot3dNativeCmabMaterialAnimationFrame(uint32_t frameCountCandidate,
                                                    uint32_t loopModeCandidate,
                                                    float frame) {
    return EffectiveCmabMaterialAnimationFrame(frameCountCandidate, loopModeCandidate, frame);
}

size_t ApplyOot3dNativeRenderModelMaterialAnimationFrame(Oot3dNativeRenderModel& renderModel,
                                                         const CmbModel& sourceModel,
                                                         const std::vector<CmabMaterialAnimation>& animations,
                                                         float frame) {
    return ApplyOot3dNativeRenderModelMaterialAnimationFrames(renderModel, sourceModel, animations, {}, frame);
}

size_t ApplyOot3dNativeRenderModelMaterialAnimationFrames(Oot3dNativeRenderModel& renderModel,
                                                           const CmbModel& sourceModel,
                                                           std::span<const CmabMaterialAnimation> animations,
                                                           const std::map<std::string, float>& frameByRole,
                                                           float fallbackFrame) {
    size_t appliedBatchCount = 0;
    for (const auto& animation : animations) {
        const float transformFrame = EffectiveCmabMaterialAnimationFrame(
            animation.FrameCountCandidate, animation.LoopModeCandidate, fallbackFrame);
        for (const auto& record : animation.MmadRecords) {
            if (!record.HeaderDecoded || !record.NativeTypeFactorySupported ||
                (record.NativeType != 1 && record.NativeType != 5) ||
                !record.TargetSelectorPresent || record.TargetSelector < 0) {
                continue;
            }

            const auto selector = static_cast<uint32_t>(record.TargetSelector);
            for (auto& batch : renderModel.Batches) {
                if (!MaterialLaneMatchesCmabRecord(batch.Material, record) ||
                    selector >= batch.Material.TextureCoords.size()) {
                    continue;
                }

                auto& coord = batch.Material.TextureCoords[selector];
                if (!coord.Active) {
                    continue;
                }

                uint32_t componentMask = 0;
                const bool transformApplied =
                    record.NativeType == 1
                        ? ApplyCmabTransformVec2CurvesToTextureCoord(record, transformFrame, coord,
                                                                     componentMask)
                        : ApplyCmabTransformScalarCurveToTextureCoord(record, transformFrame, coord,
                                                                      componentMask);
                if (!transformApplied) {
                    continue;
                }

                coord.NativeMaterialAnimationApplied = true;
                coord.NativeMaterialAnimationSource = animation.Source;
                coord.NativeMaterialAnimationKind = record.NativeValueKind;
                coord.NativeMaterialAnimationComponentMask = componentMask;
                coord.NativeMaterialAnimationSampleFrame = transformFrame;
                RefreshRenderBatchTextureCoordsForSlot(batch, selector);

                batch.Material.NativeMaterialAnimationApplied = true;
                batch.Material.NativeMaterialAnimationTextureTransformApplied = true;
                batch.Material.NativeMaterialAnimationSource = animation.Source;
                batch.Material.NativeMaterialAnimationTextureTransformKind = record.NativeValueKind;
                batch.Material.NativeMaterialAnimationTextureTransformSelector =
                    static_cast<int32_t>(selector);
                batch.Material.NativeMaterialAnimationTextureTransformComponentMask = componentMask;
                batch.Material.NativeMaterialAnimationTextureTransformSampleFrame = transformFrame;
                ++appliedBatchCount;
            }
        }

        const float colorFrame = transformFrame;
        for (const auto& record : animation.MmadRecords) {
            if (!record.HeaderDecoded || !record.NativeTypeFactorySupported ||
                (record.NativeType != 3 && record.NativeType != 4)) {
                continue;
            }

            int32_t colorSelector = -1;
            if (record.NativeType == 4) {
                if (!record.TargetSelectorPresent || record.TargetSelector < 0 ||
                    static_cast<size_t>(record.TargetSelector) >= kOot3dCmbMaterialConstantColorCount) {
                    continue;
                }
                colorSelector = record.TargetSelector;
            }

            for (auto& batch : renderModel.Batches) {
                if (!batch.Material.MaterialColorsDecoded ||
                    !MaterialLaneMatchesCmabRecord(batch.Material, record)) {
                    continue;
                }

                ColorRgba8 color = record.NativeType == 3
                                       ? batch.Material.DiffuseColor
                                       : batch.Material.ConstantColors[static_cast<size_t>(colorSelector)];
                uint32_t componentMask = 0;
                if (!ApplyCmabColorCurvesToColor(record, colorFrame, color, componentMask)) {
                    continue;
                }

                if (record.NativeType == 3) {
                    batch.Material.DiffuseColor = color;
                } else {
                    batch.Material.ConstantColors[static_cast<size_t>(colorSelector)] = color;
                    RebuildTextureEnvProgramPreservingShaderCoverage(batch.Material);
                }

                batch.Material.NativeMaterialAnimationApplied = true;
                batch.Material.NativeMaterialAnimationColorApplied = true;
                batch.Material.NativeMaterialAnimationSource = animation.Source;
                batch.Material.NativeMaterialAnimationColorKind = record.NativeValueKind;
                batch.Material.NativeMaterialAnimationColorSelector = colorSelector;
                batch.Material.NativeMaterialAnimationColorComponentMask = componentMask;
                batch.Material.NativeMaterialAnimationColorSampleFrame = colorFrame;
                batch.Material.NativeMaterialAnimationColorValue = color;
                ++appliedBatchCount;
            }
        }

        const auto binding = BuildCmabMaterialAnimationBinding(sourceModel, animation);
        if (!binding.TargetResolved || !binding.TextureSwapTrackDecoded) {
            continue;
        }

        float frame = fallbackFrame;
        if (const auto found = frameByRole.find(binding.Role); found != frameByRole.end()) {
            frame = found->second;
        }
        const auto* textureFrame = SelectCmabTextureSwapFrame(binding, frame);
        if (textureFrame == nullptr) {
            continue;
        }

        const int32_t renderTextureIndex =
            EnsureCmabFrameRenderTexture(renderModel, animation, *textureFrame);
        if (renderTextureIndex < 0) {
            continue;
        }

        for (auto& batch : renderModel.Batches) {
            if (!MaterialLaneMatchesCmabBinding(batch.Material, binding)) {
                continue;
            }

            const uint32_t mapperSlot = MaterialAnimationMapperSlot(batch.Material, binding);
            batch.Material.Textured = true;
            batch.Material.TextureIndex = renderTextureIndex;
            batch.Material.TextureMapperSlot = mapperSlot;
            batch.Material.TextureBindingSource = "oot3d_cmab_material_animation_texture_swap";
            if (mapperSlot < batch.Material.TextureMapperTextureIndices.size()) {
                batch.Material.TextureMapperTextureIndices[mapperSlot] = renderTextureIndex;
            }
            if (batch.Material.SecondaryTextureIndex >= 0 &&
                TextureIndexMatchesCmabTarget(batch.Material.SecondaryTextureIndex, binding)) {
                batch.Material.SecondaryTextureIndex = renderTextureIndex;
                batch.Material.SecondaryTextureBindingSource =
                    "oot3d_cmab_material_animation_texture_swap";
            }
            if (batch.Material.TertiaryTextureIndex >= 0 &&
                TextureIndexMatchesCmabTarget(batch.Material.TertiaryTextureIndex, binding)) {
                batch.Material.TertiaryTextureIndex = renderTextureIndex;
                batch.Material.TertiaryTextureBindingSource =
                    "oot3d_cmab_material_animation_texture_swap";
            }
            batch.Material.NativeMaterialAnimationApplied = true;
            batch.Material.NativeMaterialAnimationTextureApplied = true;
            batch.Material.NativeMaterialAnimationSource = animation.Source;
            batch.Material.NativeMaterialAnimationRole = binding.Role;
            batch.Material.NativeMaterialAnimationTextureName = textureFrame->TextureName;
            batch.Material.NativeMaterialAnimationTextureFrameIndex = textureFrame->TextureFrameIndex;
            batch.Material.NativeMaterialAnimationFrame = textureFrame->Frame;
            ++appliedBatchCount;
        }
    }
    if (appliedBatchCount > 0 && renderModel.NativeVertexDataCacheable &&
        ++renderModel.NativeGeometryContentVersion == 0) {
        renderModel.NativeGeometryContentVersion = 1;
    }
    return appliedBatchCount;
}

size_t ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(Oot3dNativeRenderModel& renderModel,
                                                               uint32_t constantColorSlot,
                                                               ColorRgba8 color,
                                                               std::string_view source) {
    if (constantColorSlot >= kOot3dCmbMaterialConstantColorCount) {
        return 0;
    }

    size_t appliedBatchCount = 0;
    for (auto& batch : renderModel.Batches) {
        auto& material = batch.Material;
        material.ConstantColors[static_cast<size_t>(constantColorSlot)] = color;
        RebuildTextureEnvProgramPreservingShaderCoverage(material);
        const auto alphaProgram =
            ResolveTextureEnvAlphaProgram(material.TextureEnvStages, material.ConstantColors);
        if (alphaProgram.Resolved && alphaProgram.ConstantSlots[constantColorSlot]) {
            material.NativeRuntimeVertexAlphaBlend = color.A < 255;
        }
        material.NativeRuntimeMaterialColorOverrideApplied = true;
        material.NativeRuntimeMaterialColorOverrideSource = std::string(source);
        material.NativeRuntimeMaterialColorOverrideSlot = static_cast<int32_t>(constantColorSlot);
        material.NativeRuntimeMaterialColorOverrideValue = color;
        ++appliedBatchCount;
    }
    if (appliedBatchCount > 0 && renderModel.NativeVertexDataCacheable &&
        ++renderModel.NativeGeometryContentVersion == 0) {
        renderModel.NativeGeometryContentVersion = 1;
    }
    return appliedBatchCount;
}

size_t ApplyOot3dNativeRenderModelRuntimeMaterialAlphaOverride(
    Oot3dNativeRenderModel& renderModel, int32_t materialIndex,
    uint32_t constantColorSlot, uint8_t alpha, std::string_view source) {
    if (materialIndex < 0 ||
        constantColorSlot >= kOot3dCmbMaterialConstantColorCount) {
        return 0;
    }

    size_t appliedBatchCount = 0;
    for (auto& batch : renderModel.Batches) {
        if (batch.MaterialIndex != materialIndex) {
            continue;
        }
        auto& material = batch.Material;
        auto color = material.ConstantColors[constantColorSlot];
        color.A = alpha;
        material.ConstantColors[constantColorSlot] = color;
        RebuildTextureEnvProgramPreservingShaderCoverage(material);
        const auto alphaProgram =
            ResolveTextureEnvAlphaProgram(material.TextureEnvStages,
                                          material.ConstantColors);
        if (alphaProgram.Resolved &&
            alphaProgram.ConstantSlots[constantColorSlot]) {
            material.NativeRuntimeVertexAlphaBlend = alpha < 255;
        }
        material.NativeRuntimeMaterialColorOverrideApplied = true;
        material.NativeRuntimeMaterialColorOverrideSource = std::string(source);
        material.NativeRuntimeMaterialColorOverrideSlot =
            static_cast<int32_t>(constantColorSlot);
        material.NativeRuntimeMaterialColorOverrideValue = color;
        ++appliedBatchCount;
    }
    if (appliedBatchCount > 0 && renderModel.NativeVertexDataCacheable &&
        ++renderModel.NativeGeometryContentVersion == 0) {
        renderModel.NativeGeometryContentVersion = 1;
    }
    return appliedBatchCount;
}

Oot3dNativeRenderModel BuildOot3dNativeDemoLinkRenderModel(const Oot3dNativeDemoScene& scene,
                                                           const CsabPose& linkPose,
                                                           const std::vector<Matrix4f>& linkSkinTransforms,
                                                           float materialAnimationFrame) {
    auto renderModel = BuildOot3dNativeRenderModel(
        scene.LinkModel,
        {
            scene.LinkOffset,
            scene.LinkScale,
            false,
            &scene.LinkMeshIndices,
            nullptr,
            &linkPose,
            &linkSkinTransforms,
        });
    ApplyOot3dNativeRenderModelMaterialAnimationFrame(renderModel, scene.LinkModel, scene.LinkMaterialAnimations,
                                                      materialAnimationFrame);
    return renderModel;
}

Oot3dNativeActorShadowState BuildOot3dNativeLinkActorShadowState(
    const Oot3dNativeDemoScene& scene, const Oot3dDemoVec3& actorPosition, int floorPolygonIndex,
    int floorSurfaceType, int floorLightSettingRawIndex, int floorLightSettingIndex, double floorY) {
    Oot3dNativeActorShadowState shadow;
    shadow.SourceKind = "oot3d_actor_shape_shadow_state";
    shadow.Actor = "player_link_child";
    shadow.DrawFunction = "ActorShadow_DrawFeet";
    shadow.ShapeSource = kOot3dNativeLinkActorShadowShapeSource;
    shadow.ReceiverSource = kOot3dNativeLinkActorShadowReceiverSource;
    shadow.BlendSource = kOot3dNativeLinkActorShadowBlendSource;
    shadow.PendingRoute = kOot3dNativeLinkActorShadowPendingRoute;
    shadow.ShapeYOffset = 0.0f;
    shadow.ShapeShadowScale = kOot3dNativeLinkChildActorShadowScale;
    shadow.ShapeShadowAlpha = kOot3dNativeActorShapeDefaultShadowAlpha;
    shadow.ActorShapeStateSupported = shadow.ShapeShadowScale > 0.0f && shadow.ShapeShadowAlpha > 0;
    shadow.FootShadowDrawSupported = true;
    shadow.FootContactPairProjectionSupported = false;
    shadow.FloorPolygonIndex = floorPolygonIndex;
    shadow.FloorSurfaceType = floorSurfaceType;
    shadow.FloorLightSettingRawIndex = floorLightSettingRawIndex;
    shadow.FloorLightSettingIndex = floorLightSettingIndex;
    shadow.ActorFloorHeight = static_cast<float>(floorY);
    shadow.ActorDistanceToFloor = static_cast<float>(actorPosition.Y - floorY);
    shadow.ActorPosition = {
        static_cast<float>(actorPosition.X),
        static_cast<float>(actorPosition.Y),
        static_cast<float>(actorPosition.Z),
    };
    shadow.ReceiverPosition = {
        static_cast<float>(actorPosition.X),
        static_cast<float>(floorY),
        static_cast<float>(actorPosition.Z),
    };
    shadow.ReceiverNormal = CollisionPolygonNormal(scene.Collision, floorPolygonIndex);
    shadow.ReceiverFromNativeCollision =
        scene.Collision.Valid &&
        scene.Collision.DecodedFromNativeZsi &&
        CollisionPolygonIndexValid(scene.Collision, floorPolygonIndex) &&
        floorSurfaceType >= 0 &&
        static_cast<size_t>(floorSurfaceType) < scene.Collision.SurfaceTypes.size();
    shadow.NativeBlendRouteSupported = shadow.ActorShapeStateSupported;
    shadow.Available =
        shadow.ActorShapeStateSupported &&
        shadow.ReceiverFromNativeCollision &&
        std::isfinite(shadow.ActorDistanceToFloor);
    return shadow;
}

Oot3dNativeActorShadowState BuildOot3dNativeInitialLinkActorShadowState(const Oot3dNativeDemoScene& scene) {
    double floorY = scene.PlayerStart.Position.Y;
    int floorPolygonIndex = scene.PlayerStart.FloorPolygonIndex;
    int floorSurfaceType = scene.PlayerStart.FloorSurfaceType;
    int floorLightSettingRawIndex = scene.PlayerStart.FloorLightSettingRawIndex;
    int floorLightSettingIndex = scene.PlayerStart.FloorLightSettingIndex;
    if (const auto hit = Oot3dNativeDemoFloorHitAt(scene.Collision, scene.PlayerStart.Position.X,
                                                  scene.PlayerStart.Position.Z,
                                                  scene.PlayerStart.Position.Y + 50.0)) {
        floorY = hit->Y;
        floorPolygonIndex = hit->PolygonIndex;
        floorSurfaceType = hit->SurfaceType;
        if (floorSurfaceType >= 0 &&
            static_cast<size_t>(floorSurfaceType) < scene.Collision.SurfaceTypes.size()) {
            const auto& surfaceType = scene.Collision.SurfaceTypes[static_cast<size_t>(floorSurfaceType)];
            floorLightSettingRawIndex = NativeDemoSurfaceTypeLightSettingRawIndex(surfaceType);
            floorLightSettingIndex = floorLightSettingRawIndex < 0
                                         ? -1
                                         : NativeDemoNormalizeLightSettingIndex(floorLightSettingRawIndex);
        }
    }
    return BuildOot3dNativeLinkActorShadowState(scene, scene.PlayerStart.Position, floorPolygonIndex,
                                                floorSurfaceType, floorLightSettingRawIndex,
                                                floorLightSettingIndex, floorY);
}

const Oot3dNativeDemoZsiCommandRecord* FindSceneCommandForSetup(
    const std::vector<Oot3dNativeDemoZsiCommandRecord>& commands, int setupIndex, int commandId) {
    for (const auto& command : commands) {
        if (command.SetupIndex == setupIndex && command.CommandId == commandId) {
            return &command;
        }
    }
    return nullptr;
}

const Oot3dNativeDemoZsiCommandRecord* FindSceneCommandForSetup(
    const Oot3dNativeDemoScene& scene, int setupIndex, int commandId) {
    return FindSceneCommandForSetup(scene.AssetGraph.SceneCommands, setupIndex, commandId);
}

bool NativeRenderCanRead(const std::vector<uint8_t>& data, size_t offset, size_t size) {
    return offset <= data.size() && size <= data.size() - offset;
}

uint16_t NativeRenderReadLeU16(const std::vector<uint8_t>& data, size_t offset) {
    if (!NativeRenderCanRead(data, offset, 2)) {
        return 0;
    }
    return static_cast<uint16_t>(data[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint8_t NativeRenderReadU8(const std::vector<uint8_t>& data, size_t offset) {
    if (!NativeRenderCanRead(data, offset, 1)) {
        return 0;
    }
    return data[offset];
}

uint32_t NativeRenderReadLeU32(const std::vector<uint8_t>& data, size_t offset) {
    if (!NativeRenderCanRead(data, offset, 4)) {
        return 0;
    }
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

float NativeRenderReadLeFloat(const std::vector<uint8_t>& data, size_t offset) {
    const uint32_t raw = NativeRenderReadLeU32(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

const std::vector<uint8_t>& NativeRenderReadBinaryFile(const std::filesystem::path& path) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    const std::string cacheKey = path.lexically_normal().string();
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<uint8_t> data;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return cache.emplace(cacheKey, std::move(data)).first->second;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) {
        return cache.emplace(cacheKey, std::move(data)).first->second;
    }
    data.resize(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return cache.emplace(cacheKey, std::move(data)).first->second;
}

struct NativeRenderCachedZarArchive {
    ZarArchive Archive;
    const std::vector<uint8_t>* Bytes = nullptr;
};

const NativeRenderCachedZarArchive& NativeRenderReadZarArchive(
    const std::filesystem::path& path) {
    static std::map<std::string, NativeRenderCachedZarArchive> cache;
    const std::string cacheKey = path.lexically_normal().string();
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    const auto& bytes = NativeRenderReadBinaryFile(path);
    if (bytes.empty()) {
        throw std::runtime_error("could not read ZAR archive: " + path.string());
    }
    NativeRenderCachedZarArchive archive;
    archive.Archive = ParseZarArchiveBytes(bytes, path.string());
    archive.Bytes = &bytes;
    return cache.emplace(cacheKey, std::move(archive)).first->second;
}

std::span<const uint8_t> NativeRenderReadZarFile(
    const std::filesystem::path& archivePath, std::string_view fileName) {
    const auto& cached = NativeRenderReadZarArchive(archivePath);
    const std::string normalizedTarget = LowerAscii(std::filesystem::path(fileName).generic_string());
    for (const auto& file : cached.Archive.Files) {
        if (LowerAscii(std::filesystem::path(file.Name).generic_string()) != normalizedTarget) {
            continue;
        }
        if (cached.Bytes == nullptr ||
            file.Offset > cached.Bytes->size() ||
            file.Size > cached.Bytes->size() - file.Offset) {
            break;
        }
        return std::span<const uint8_t>(cached.Bytes->data() + file.Offset, file.Size);
    }
    throw std::runtime_error(archivePath.string() + ": ZAR file entry not found: " +
                             std::string(fileName));
}

std::optional<size_t> NativeRenderRuntimeAddressToFileOffset(uint32_t runtimeAddress, uint32_t codeBase,
                                                             size_t codeSize) {
    if (runtimeAddress < codeBase) {
        return std::nullopt;
    }
    const size_t offset = static_cast<size_t>(runtimeAddress - codeBase);
    if (offset >= codeSize) {
        return std::nullopt;
    }
    return offset;
}

std::string NativeRenderReadCString(const std::vector<uint8_t>& data, size_t offset, size_t maxLength) {
    std::string out;
    for (size_t i = 0; i < maxLength && NativeRenderCanRead(data, offset + i, 1); ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == '\0') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::filesystem::path NativeRenderResolveCodeBinPath(const Oot3dNativeDemoScene& scene) {
    if (!scene.PlayerStart.GlobalEntranceCodeBinPath.empty()) {
        std::filesystem::path path(scene.PlayerStart.GlobalEntranceCodeBinPath);
        if (std::filesystem::is_regular_file(path)) {
            return path;
        }
    }
    std::filesystem::path cursor = scene.CollisionZsiPath;
    while (!cursor.empty()) {
        if (cursor.filename() == "romfs") {
            const auto candidate = cursor.parent_path() / "exefs" / "code.bin";
            if (std::filesystem::is_regular_file(candidate)) {
                return candidate;
            }
            break;
        }
        cursor = cursor.parent_path();
    }
    return {};
}

std::filesystem::path NativeRenderResolveRomPath(const Oot3dNativeDemoScene& scene,
                                                 std::string_view romPath) {
    constexpr std::string_view kRomPrefix = "rom:/";
    if (romPath.substr(0, kRomPrefix.size()) != kRomPrefix) {
        return {};
    }
    std::filesystem::path cursor = scene.CollisionZsiPath;
    while (!cursor.empty()) {
        if (cursor.filename() == "romfs") {
            return cursor / std::filesystem::path(std::string(romPath.substr(kRomPrefix.size())));
        }
        cursor = cursor.parent_path();
    }
    return {};
}

std::string NativeRenderNormalizePathText(std::string_view value) {
    std::string normalized = LowerAscii(std::string(value));
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

std::filesystem::path NativeRenderResolveSceneZsiPath(const Oot3dNativeDemoScene& scene,
                                                      std::string_view scenePath) {
    if (scenePath.empty()) {
        return {};
    }

    constexpr std::string_view kRomPrefix = "rom:/";
    if (scenePath.substr(0, kRomPrefix.size()) == kRomPrefix) {
        return NativeRenderResolveRomPath(scene, scenePath);
    }

    const std::filesystem::path requested{ std::string(scenePath) };
    if (requested.is_absolute()) {
        return requested;
    }

    std::filesystem::path cursor = scene.CollisionZsiPath;
    while (!cursor.empty()) {
        if (cursor.filename() == "romfs") {
            const std::string normalized = NativeRenderNormalizePathText(scenePath);
            if (normalized.rfind("scene/", 0) == 0) {
                return cursor / requested;
            }
            return cursor / "scene" / requested.filename();
        }
        cursor = cursor.parent_path();
    }

    if (!scene.CollisionZsiPath.empty()) {
        return scene.CollisionZsiPath.parent_path() / requested.filename();
    }
    return requested;
}

bool NativeRenderScenePathMatches(const std::filesystem::path& lhs,
                                  const std::filesystem::path& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return false;
    }
    return NativeRenderNormalizePathText(lhs.filename().generic_string()) ==
           NativeRenderNormalizePathText(rhs.filename().generic_string());
}

bool NativeRenderEndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

std::string NativeRenderArchiveStem(std::string_view name) {
    std::string normalized = LowerAscii(std::string(name));
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const size_t slash = normalized.find_last_of('/');
    if (slash != std::string::npos) {
        normalized = normalized.substr(slash + 1);
    }
    const size_t dot = normalized.find_last_of('.');
    if (dot != std::string::npos) {
        normalized = normalized.substr(0, dot);
    }
    return normalized;
}

std::string NativeRenderTrimTrailingDigits(std::string value) {
    while (!value.empty() &&
           std::isdigit(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

bool Oot3dNativeKankyoCmabAppliesToCmb(std::string_view cmabName, std::string_view cmbName) {
    const std::string cmabStem = NativeRenderArchiveStem(cmabName);
    const std::string cmbStem = NativeRenderArchiveStem(cmbName);
    if (cmabStem.empty() || cmbStem.empty()) {
        return false;
    }
    return cmabStem == cmbStem || cmabStem == NativeRenderTrimTrailingDigits(cmbStem);
}

bool NativeRenderKankyoCmbNameMatchesProfileSuffix(std::string_view name, int profileIndex) {
    if (profileIndex < 0 || profileIndex > 99) {
        return false;
    }
    const std::string lower = LowerAscii(std::string(name));
    const std::string suffix = std::to_string(profileIndex) + ".cmb";
    const bool profiledSky = lower.find("tenkyu") != std::string::npos;
    const bool profiledCloud = lower.find("kumo_a") != std::string::npos;
    return (profiledSky || profiledCloud) && NativeRenderEndsWith(lower, suffix);
}

struct NativeRenderKankyoCmbSelection {
    std::vector<std::string> Names;
    std::vector<int> ProfileIndices;
    std::vector<uint32_t> TypeLocalIndices;
    std::vector<int> LayerIndices;
};

NativeRenderKankyoCmbSelection NativeRenderSelectKankyoCmbNames(
    const std::filesystem::path& archivePath,
    int currentProfile, int nextProfile,
    int profileCount, int layerCount,
    int coreCmbCount) {
    NativeRenderKankyoCmbSelection selection;
    if (!std::filesystem::is_regular_file(archivePath)) {
        return selection;
    }
    try {
        const auto& archive = NativeRenderReadZarArchive(archivePath).Archive;
        std::map<uint32_t, int> selectedTypeLocalProfiles;
        const auto addProfileLayers = [&](int profileIndex) {
            if (profileIndex < 0 || profileCount <= 0 || layerCount <= 0) {
                return;
            }
            if (profileIndex >= profileCount) {
                return;
            }
            for (int layer = 0; layer < layerCount; ++layer) {
                const int typeLocalIndex = profileIndex + layer * profileCount;
                if (typeLocalIndex < 0) {
                    continue;
                }
                if (coreCmbCount >= 0 && typeLocalIndex >= coreCmbCount) {
                    continue;
                }
                selectedTypeLocalProfiles[static_cast<uint32_t>(typeLocalIndex)] = profileIndex;
            }
        };

        addProfileLayers(currentProfile);
        if (nextProfile != currentProfile) {
            addProfileLayers(nextProfile);
        }
        if (currentProfile >= 0 && nextProfile >= 0 &&
            (currentProfile >> 2) == (nextProfile >> 2)) {
            addProfileLayers((currentProfile >> 2) << 2);
        }

        for (const auto& file : archive.Files) {
            if (file.TypeName != "cmb") {
                continue;
            }
            const auto profileIt = selectedTypeLocalProfiles.find(file.TypeLocalIndex);
            if (!selectedTypeLocalProfiles.empty() && profileIt != selectedTypeLocalProfiles.end()) {
                selection.Names.push_back(file.Name);
                selection.ProfileIndices.push_back(profileIt->second);
                selection.TypeLocalIndices.push_back(file.TypeLocalIndex);
                selection.LayerIndices.push_back(
                    profileCount > 0 ? static_cast<int>(file.TypeLocalIndex) / profileCount : -1);
            }
        }
        if (!selection.Names.empty()) {
            return selection;
        }

        // Older diagnostics only knew the profile suffix. Keep this as a last-resort
        // decoder for malformed/partial tables, but prefer the native type-local index
        // table because it preserves BlueSky's fine/cloud/holy/dark profile families.
        const auto addSuffixFallbackProfile = [&](const ZarFileEntry& file, int profileIndex) {
            if (!NativeRenderKankyoCmbNameMatchesProfileSuffix(file.Name, profileIndex)) {
                return false;
            }
            selection.Names.push_back(file.Name);
            selection.ProfileIndices.push_back(profileIndex);
            selection.TypeLocalIndices.push_back(file.TypeLocalIndex);
            selection.LayerIndices.push_back(-1);
            return true;
        };
        for (const auto& file : archive.Files) {
            if (file.TypeName != "cmb") {
                continue;
            }
            if (addSuffixFallbackProfile(file, currentProfile)) {
                continue;
            }
            if (nextProfile != currentProfile) {
                addSuffixFallbackProfile(file, nextProfile);
            }
        }
    } catch (const std::exception&) {
    }
    return selection;
}

std::vector<std::string> NativeRenderSelectKankyoCmabNames(const std::filesystem::path& archivePath) {
    std::vector<std::string> names;
    if (!std::filesystem::is_regular_file(archivePath)) {
        return names;
    }
    try {
        const auto& archive = NativeRenderReadZarArchive(archivePath).Archive;
        for (const auto& file : archive.Files) {
            const std::string lowerName = LowerAscii(file.Name);
            if (file.TypeName == "cmab" || NativeRenderEndsWith(lowerName, ".cmab")) {
                names.push_back(file.Name);
            }
        }
    } catch (const std::exception&) {
    }
    return names;
}

std::string NativeRenderKankyoProfilePrefixFromCmbName(std::string_view name) {
    std::string stem = NativeRenderArchiveStem(name);
    const std::array<std::string_view, 2> profileMarkers = {
        "tenkyu",
        "kumo_a",
    };
    for (const auto marker : profileMarkers) {
        const size_t markerPos = stem.find(marker);
        if (markerPos == std::string::npos) {
            continue;
        }
        std::string prefix = stem.substr(0, markerPos);
        while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-')) {
            prefix.pop_back();
        }
        return prefix;
    }
    return {};
}

std::set<std::string> NativeRenderKankyoProfilePrefixesFromCmbNames(
    const std::vector<std::string>& cmbNames) {
    std::set<std::string> prefixes;
    for (const auto& cmbName : cmbNames) {
        const auto prefix = NativeRenderKankyoProfilePrefixFromCmbName(cmbName);
        if (!prefix.empty()) {
            prefixes.insert(prefix);
        }
    }
    return prefixes;
}

std::string NativeRenderStemBeforeMarker(std::string stem, std::string_view marker) {
    const size_t markerPos = stem.find(marker);
    if (markerPos == std::string::npos) {
        return {};
    }
    std::string prefix = stem.substr(0, markerPos);
    while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-')) {
        prefix.pop_back();
    }
    return prefix;
}

struct NativeRenderKankyoExtraCmbSelection {
    std::vector<uint32_t> TypeLocalIndices;
    std::vector<std::string> Names;
};

NativeRenderKankyoExtraCmbSelection NativeRenderSelectKankyoExtraCmbNames(
    const std::filesystem::path& archivePath,
    const std::vector<std::string>& selectedProfileCmbNames,
    int skyboxId, int currentProfile) {
    NativeRenderKankyoExtraCmbSelection selection;
    if (!std::filesystem::is_regular_file(archivePath)) {
        return selection;
    }

    const auto selectedPrefixes =
        NativeRenderKankyoProfilePrefixesFromCmbNames(selectedProfileCmbNames);
    try {
        const auto& archive = NativeRenderReadZarArchive(archivePath).Archive;
        std::set<uint32_t> selectedTypeLocalIndices;

        // FUN_002e47c8 constructs these non-profile-table CMBs per native
        // skybox route. ID 1 uses 0x20/0x21 for the sun, 0x22 for stars, and
        // 0x23/0x24 for profile-group 0/2 secondary clouds. ID 8 uses 8 for
        // the sun, 9 for stars, and 10 for its secondary cloud.
        if (skyboxId == 1) {
            selectedTypeLocalIndices.insert(0x22);
            const int currentProfileGroup =
                currentProfile >= 0 ? currentProfile >> 2 : -1;
            if (currentProfileGroup == 0) {
                selectedTypeLocalIndices.insert(0x23);
            }
            if (currentProfileGroup == 2) {
                selectedTypeLocalIndices.insert(0x24);
            }
        } else if (skyboxId == 8) {
            selectedTypeLocalIndices.insert(9);
            selectedTypeLocalIndices.insert(10);
        }

        for (const auto& file : archive.Files) {
            if (file.TypeName != "cmb") {
                continue;
            }
            const std::string stem = NativeRenderArchiveStem(file.Name);
            bool selected = selectedTypeLocalIndices.count(file.TypeLocalIndex) != 0;
            if (NativeRenderEndsWith(stem, "_sun")) {
                const auto prefix = NativeRenderStemBeforeMarker(stem, "sun");
                selected = selected || selectedPrefixes.empty() || selectedPrefixes.count(prefix) != 0;
            }
            if (selected) {
                selection.TypeLocalIndices.push_back(file.TypeLocalIndex);
                selection.Names.push_back(file.Name);
            }
        }
    } catch (const std::exception&) {
    }
    return selection;
}

struct NativeRenderKankyoExtraCtxbSelection {
    std::vector<uint32_t> TypeLocalIndices;
    std::vector<std::string> Names;
    std::vector<NativeCtxbDescriptorSlot> DescriptorSlots;
    std::vector<Oot3dNativeRenderTexture> RenderTextures;
};

NativeRenderKankyoExtraCtxbSelection NativeRenderSelectKankyoExtraCtxbs(
    const std::filesystem::path& archivePath) {
    NativeRenderKankyoExtraCtxbSelection selection;
    if (!std::filesystem::is_regular_file(archivePath)) {
        return selection;
    }

    try {
        const auto& archive = NativeRenderReadZarArchive(archivePath).Archive;
        uint32_t descriptorSlot = 0;
        for (const auto& file : archive.Files) {
            if (file.TypeName != "ctxb") {
                continue;
            }
            const std::string stem = NativeRenderArchiveStem(file.Name);
            const bool isCelestialOrLensflare =
                stem.find("sun") != std::string::npos ||
                stem.find("moon") != std::string::npos ||
                stem.find("lensflare") != std::string::npos;
            if (!isCelestialOrLensflare) {
                continue;
            }

            selection.TypeLocalIndices.push_back(file.TypeLocalIndex);
            selection.Names.push_back(file.Name);
            selection.RenderTextures.emplace_back();
            try {
                const auto bytes = NativeRenderReadZarFile(archivePath, file.Name);
                const auto texture =
                    ParseCtxbTextureBytes(bytes, archivePath.string() + "!" + file.Name);
                selection.DescriptorSlots.push_back(BuildNativeCtxbDescriptorSlot(texture, descriptorSlot));
                selection.RenderTextures.back() =
                    BuildRenderTextureFromCtxbTexture(texture, file.TypeLocalIndex);
                ++descriptorSlot;
            } catch (const std::exception&) {
            }
        }
    } catch (const std::exception&) {
    }

    return selection;
}

std::string NativeRenderKankyoEffectClass(uint32_t nativeSubresourceId) {
    switch (nativeSubresourceId) {
        case 0x44:
            return "rain_drop";
        case 0x45:
            return "rain_ripple";
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
            return "thunder_selector";
        case 0x4A:
        case 0x4B:
            return "storm_layer";
        default:
            return "unknown";
    }
}

std::filesystem::path NativeRenderResolveGameplayKeepArchivePath(const Oot3dNativeDemoScene& scene) {
    constexpr std::string_view kGameplayKeepObjectName = "OBJECT_GAMEPLAY_KEEP";
    constexpr std::string_view kGameplayKeepArchiveName = "zelda_keep.zar";

    for (const auto& object : scene.AssetGraph.RoomObjects) {
        const std::string archiveName = LowerAscii(object.ArchivePath.filename().string());
        if ((object.ObjectName == kGameplayKeepObjectName || archiveName == kGameplayKeepArchiveName) &&
            object.ArchiveAvailable && std::filesystem::is_regular_file(object.ArchivePath)) {
            return object.ArchivePath;
        }
    }

    const auto fallback = NativeRenderResolveRomPath(scene, "rom:/actor/zelda_keep.zar");
    if (std::filesystem::is_regular_file(fallback)) {
        return fallback;
    }
    return {};
}

std::vector<NativeKankyoRuntimeBindingSlot> NativeRenderKankyoRuntimeSlotsForSubresource(
    const NativeKankyoRuntimeBridgeContract& bridgeContract,
    uint32_t nativeSubresourceId) {
    std::vector<NativeKankyoRuntimeBindingSlot> slots;
    for (const auto& slot : bridgeContract.RuntimeBindingSlots) {
        if (nativeSubresourceId >= slot.SubresourceIdStart &&
            nativeSubresourceId <= slot.SubresourceIdEnd) {
            slots.push_back(slot);
        }
    }
    return slots;
}

std::string NativeRenderFirstBytesHex(std::span<const uint8_t> bytes, size_t maxCount) {
    std::ostringstream stream;
    const size_t count = std::min(bytes.size(), maxCount);
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) {
            stream << ' ';
        }
        stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
               << static_cast<uint32_t>(bytes[i]);
    }
    return stream.str();
}

std::string NativeRenderReadTbdName(const std::vector<uint8_t>& bytes, size_t offset, size_t size) {
    std::string name;
    for (size_t i = 0; i < size && NativeRenderCanRead(bytes, offset + i, 1); ++i) {
        const uint8_t value = bytes[offset + i];
        if (value == 0) {
            break;
        }
        name.push_back(static_cast<char>(value));
    }
    return name;
}

Oot3dNativeKankyoCommonTbdRecord NativeRenderDecodeKankyoCommonTbdRecord(
    const std::vector<uint8_t>& bytes, uint32_t recordIndex, size_t recordOffset) {
    constexpr size_t kRecordHeaderSize = 0x30;
    Oot3dNativeKankyoCommonTbdRecord record;
    record.RecordIndex = recordIndex;
    record.RecordOffset = static_cast<uint32_t>(recordOffset);
    record.PayloadOffset = static_cast<uint32_t>(recordOffset + kRecordHeaderSize);
    if (!NativeRenderCanRead(bytes, recordOffset, kRecordHeaderSize)) {
        record.SourceStatus = "TBD record is truncated before the native 0x30-byte header";
        return record;
    }

    record.Resolved = true;
    record.Name = NativeRenderReadTbdName(bytes, recordOffset, 0x20);
    record.NativeIndex = NativeRenderReadLeU32(bytes, recordOffset + 0x20);
    record.RecordSize = NativeRenderReadLeU32(bytes, recordOffset + 0x24);
    record.Type = NativeRenderReadLeU32(bytes, recordOffset + 0x28);
    record.ValueCount = NativeRenderReadLeU32(bytes, recordOffset + 0x2C);
    if (record.RecordSize < kRecordHeaderSize) {
        record.SourceStatus = "TBD record size is smaller than its native header";
        return record;
    }

    record.PayloadSize = record.RecordSize - kRecordHeaderSize;
    record.RecordSizeMatches = NativeRenderCanRead(bytes, recordOffset, record.RecordSize);
    const size_t payloadAvailable =
        NativeRenderCanRead(bytes, record.PayloadOffset, record.PayloadSize) ? record.PayloadSize : 0;
    if (payloadAvailable > 0) {
        record.PayloadFirst32BytesHex =
            NativeRenderFirstBytesHex(std::span<const uint8_t>(
                                          bytes.data() + record.PayloadOffset, payloadAvailable),
                                      32);
    }

    if (record.Type == 6) {
        record.ValueCountMatchesPayload = record.PayloadSize == record.ValueCount * sizeof(float);
        const uint32_t valueCount =
            std::min<uint32_t>(record.ValueCount, static_cast<uint32_t>(payloadAvailable / sizeof(float)));
        for (uint32_t i = 0; i < valueCount; ++i) {
            record.FloatValues.push_back(
                NativeRenderReadLeFloat(bytes, record.PayloadOffset + i * sizeof(float)));
        }
        record.SourceStatus = record.ValueCountMatchesPayload
                                  ? "decoded TBD type 6 float payload"
                                  : "decoded partial TBD type 6 float payload; value count does not match payload size";
    } else if (record.Type == 0) {
        record.ValueCountMatchesPayload = record.PayloadSize == record.ValueCount;
        const uint32_t valueCount =
            std::min<uint32_t>(record.ValueCount, static_cast<uint32_t>(payloadAvailable));
        for (uint32_t i = 0; i < valueCount; ++i) {
            record.ByteValues.push_back(NativeRenderReadU8(bytes, record.PayloadOffset + i));
        }
        for (uint32_t i = 0; i + 3 < valueCount; i += 4) {
            record.Rgba8Values.push_back({
                NativeRenderReadU8(bytes, record.PayloadOffset + i + 0),
                NativeRenderReadU8(bytes, record.PayloadOffset + i + 1),
                NativeRenderReadU8(bytes, record.PayloadOffset + i + 2),
                NativeRenderReadU8(bytes, record.PayloadOffset + i + 3),
            });
        }
        record.SourceStatus = record.ValueCountMatchesPayload
                                  ? "decoded TBD type 0 byte payload with RGBA grouping"
                                  : "decoded partial TBD type 0 byte payload; value count does not match payload size";
    } else {
        record.SourceStatus = "TBD record header decoded; payload type is not yet semantically named";
    }

    return record;
}

std::vector<Oot3dNativeKankyoCommonTbdRecord> NativeRenderDecodeKankyoCommonTbdRecords(
    const std::vector<uint8_t>& bytes, uint32_t entryCount) {
    constexpr size_t kTbdHeaderSize = 0x10;
    std::vector<Oot3dNativeKankyoCommonTbdRecord> records;
    size_t recordOffset = kTbdHeaderSize;
    for (uint32_t i = 0; i < entryCount && recordOffset < bytes.size(); ++i) {
        auto record = NativeRenderDecodeKankyoCommonTbdRecord(bytes, i, recordOffset);
        const uint32_t step = record.RecordSize;
        records.push_back(std::move(record));
        if (step < 0x30) {
            break;
        }
        recordOffset += step;
    }
    return records;
}

void NativeRenderResolveKankyoRuntimeCtxbStates(const Oot3dNativeDemoScene& scene,
                                                Oot3dNativeEnvironmentBackgroundState& background,
                                                const NativeKankyoRuntimeBridgeContract& bridgeContract) {
    const auto& provider = bridgeContract.ProviderTable;
    background.NativeKankyoGameplayKeepArchivePath = NativeRenderResolveGameplayKeepArchivePath(scene);
    const bool archiveAvailable =
        std::filesystem::is_regular_file(background.NativeKankyoGameplayKeepArchivePath);

    for (uint32_t subresourceId = provider.KankyoCtxbNativeIdStart;
         subresourceId <= provider.KankyoCtxbNativeIdEnd; ++subresourceId) {
        Oot3dNativeKankyoRuntimeCtxbState state;
        state.NativeSubresourceId = subresourceId;
        state.EffectClass = NativeRenderKankyoEffectClass(subresourceId);
        state.ArchivePath = background.NativeKankyoGameplayKeepArchivePath;
        state.EntryTypeLocalIndex = subresourceId;
        state.RuntimeBindingSlots =
            NativeRenderKankyoRuntimeSlotsForSubresource(bridgeContract, subresourceId);
        state.SourceKind =
            "oot3d_object_gameplay_keep_kankyo_ctxb_subresource_provider";

        if (!archiveAvailable) {
            state.SourceStatus =
                "OBJECT_GAMEPLAY_KEEP archive is not available, so the native kankyo CTXB "
                "subresource cannot be decoded";
            background.NativeKankyoRuntimeCtxbStates.push_back(std::move(state));
            continue;
        }

        try {
            const auto& archive =
                NativeRenderReadZarArchive(background.NativeKankyoGameplayKeepArchivePath).Archive;
            const auto fileIt = std::find_if(
                archive.Files.begin(), archive.Files.end(), [subresourceId](const ZarFileEntry& file) {
                    return file.TypeName == "ctxb" && file.TypeLocalIndex == subresourceId;
                });
            if (fileIt == archive.Files.end()) {
                state.SourceStatus =
                    "OBJECT_GAMEPLAY_KEEP archive parsed, but this native CTXB subresource id "
                    "was not present in the provider table";
                background.NativeKankyoRuntimeCtxbStates.push_back(std::move(state));
                continue;
            }

            const auto bytes =
                NativeRenderReadZarFile(background.NativeKankyoGameplayKeepArchivePath, fileIt->Name);
            const auto texture = ParseCtxbTextureBytes(
                bytes, background.NativeKankyoGameplayKeepArchivePath.string() + "!" + fileIt->Name);
            const uint32_t descriptorSlot =
                state.RuntimeBindingSlots.empty() ? 0 : state.RuntimeBindingSlots.front().DescriptorSlot;

            state.Resolved = true;
            state.EntryIndex = fileIt->Index;
            state.EntryName = fileIt->Name;
            state.EntryTypeLocalIndex = fileIt->TypeLocalIndex;
            state.EntryOffset = fileIt->Offset;
            state.EntrySize = fileIt->Size;
            state.Width = texture.Width;
            state.Height = texture.Height;
            state.TextureFormat = texture.TextureFormat;
            state.DataType = texture.DataType;
            state.PayloadSize = texture.PayloadSize;
            state.DescriptorSlot = BuildNativeCtxbDescriptorSlot(texture, descriptorSlot);
            state.SourceStatus =
                "decoded from OBJECT_GAMEPLAY_KEEP through native z_kankyo source id 1 and "
                "subresource resolver 0x00372C90; descriptor layout follows 0x00348A64";
        } catch (const std::exception& exc) {
            state.SourceStatus = std::string("native kankyo CTXB decode failed: ") + exc.what();
        }

        background.NativeKankyoRuntimeCtxbStates.push_back(std::move(state));
    }

    background.NativeKankyoRuntimeCtxbSetResolved =
        !background.NativeKankyoRuntimeCtxbStates.empty() &&
        std::all_of(background.NativeKankyoRuntimeCtxbStates.begin(),
                    background.NativeKankyoRuntimeCtxbStates.end(),
                    [](const Oot3dNativeKankyoRuntimeCtxbState& state) {
                        return state.Resolved && !state.RuntimeBindingSlots.empty();
                    });
}

void NativeRenderResolveKankyoCommonTbdStates(const Oot3dNativeDemoScene& scene,
                                              Oot3dNativeEnvironmentBackgroundState& background,
                                              const NativeKankyoRuntimeBridgeContract& bridgeContract) {
    background.NativeKankyoCommonArchivePath =
        NativeRenderResolveRomPath(scene, bridgeContract.ProviderTable.KankyoCommonPath);
    background.NativeKankyoCommonArchiveAvailable =
        std::filesystem::is_regular_file(background.NativeKankyoCommonArchivePath);
    if (!background.NativeKankyoCommonArchiveAvailable) {
        return;
    }

    try {
        const auto& archive =
            NativeRenderReadZarArchive(background.NativeKankyoCommonArchivePath).Archive;
        for (const auto& file : archive.Files) {
            if (file.TypeName != "tbd") {
                continue;
            }

            Oot3dNativeKankyoCommonTbdState state;
            state.ArchivePath = background.NativeKankyoCommonArchivePath;
            state.EntryIndex = file.Index;
            state.EntryName = file.Name;
            state.EntryTypeLocalIndex = file.TypeLocalIndex;
            state.EntryOffset = file.Offset;
            state.EntrySize = file.Size;
            state.SourceKind = "oot3d_kankyo_common_tbd_provider_payload";

            const auto bytesView =
                NativeRenderReadZarFile(background.NativeKankyoCommonArchivePath, file.Name);
            const std::vector<uint8_t> bytes(bytesView.begin(), bytesView.end());
            if (NativeRenderCanRead(bytes, 0, 16)) {
                state.Resolved = true;
                state.Magic = NativeRenderReadLeU32(bytes, 0x00);
                state.Version = NativeRenderReadLeU32(bytes, 0x04);
                state.DeclaredSize = NativeRenderReadLeU32(bytes, 0x08);
                state.EntryCount = NativeRenderReadLeU32(bytes, 0x0C);
                state.DeclaredSizeMatches = state.DeclaredSize == bytes.size();
                state.First32BytesHex = NativeRenderFirstBytesHex(bytes, 32);
                state.Records = NativeRenderDecodeKankyoCommonTbdRecords(bytes, state.EntryCount);
                state.ResolvedRecordCount = static_cast<uint32_t>(std::count_if(
                    state.Records.begin(), state.Records.end(),
                    [](const Oot3dNativeKankyoCommonTbdRecord& record) {
                        return record.Resolved && record.RecordSizeMatches &&
                               record.ValueCountMatchesPayload;
                    }));
                state.RecordTableResolved =
                    state.ResolvedRecordCount == state.EntryCount &&
                    state.Records.size() == state.EntryCount;
                state.SourceStatus =
                    state.RecordTableResolved
                        ? "decoded kankyo_common TBD header and native record table"
                        : "decoded kankyo_common TBD header, but one or more records need follow-up";
            } else {
                state.SourceStatus = "kankyo_common TBD payload is truncated before the native header";
            }

            background.NativeKankyoCommonTbdStates.push_back(std::move(state));
        }
    } catch (const std::exception&) {
        background.NativeKankyoCommonTbdStates.clear();
    }

    bool hasLensflare = false;
    bool hasStorm = false;
    for (const auto& state : background.NativeKankyoCommonTbdStates) {
        const std::string lowerName = LowerAscii(state.EntryName);
        hasLensflare = hasLensflare || lowerName.find("lensflare_info.tbd") != std::string::npos;
        hasStorm = hasStorm || lowerName.find("storm_info.tbd") != std::string::npos;
    }
    background.NativeKankyoCommonTbdSupportResolved = hasLensflare && hasStorm;
}

const Oot3dNativeKankyoCommonTbdState* NativeRenderFindKankyoCommonTbdState(
    const Oot3dNativeEnvironmentBackgroundState& background, std::string_view name) {
    const std::string lowerNeedle = LowerAscii(std::string(name));
    for (const auto& state : background.NativeKankyoCommonTbdStates) {
        if (LowerAscii(state.EntryName).find(lowerNeedle) != std::string::npos) {
            return &state;
        }
    }
    return nullptr;
}

const Oot3dNativeKankyoCommonTbdRecord* NativeRenderFindKankyoCommonTbdRecord(
    const Oot3dNativeKankyoCommonTbdState& state, std::string_view name) {
    const std::string lowerNeedle = LowerAscii(std::string(name));
    for (const auto& record : state.Records) {
        if (LowerAscii(record.Name) == lowerNeedle) {
            return &record;
        }
    }
    return nullptr;
}

bool NativeRenderFindKankyoExtraCtxbName(const Oot3dNativeEnvironmentBackgroundState& background,
                                         std::string_view stemNeedle,
                                         std::string& outName) {
    const std::string lowerNeedle = LowerAscii(std::string(stemNeedle));
    for (const auto& name : background.NativeKankyoExtraCtxbNames) {
        if (LowerAscii(name).find(lowerNeedle) != std::string::npos) {
            outName = name;
            return true;
        }
    }
    return false;
}

const Oot3dNativeRenderTexture* NativeRenderFindKankyoExtraCtxbTexture(
    const Oot3dNativeEnvironmentBackgroundState& background,
    std::string_view name) {
    const std::string lowerName = LowerAscii(std::string(name));
    for (size_t i = 0; i < background.NativeKankyoExtraCtxbNames.size(); ++i) {
        if (i >= background.NativeKankyoExtraCtxbTextures.size()) {
            break;
        }
        if (LowerAscii(background.NativeKankyoExtraCtxbNames[i]) == lowerName) {
            return &background.NativeKankyoExtraCtxbTextures[i];
        }
    }
    return nullptr;
}

Oot3dNativeKankyoMoonState NativeRenderResolveKankyoMoonState(
    const Oot3dNativeEnvironmentBackgroundState& background) {
    Oot3dNativeKankyoMoonState state;
    state.SourceKind = "oot3d_code_bin_kankyo_moon_runtime";
    state.SkyContextInitFunctionAddress = 0x002E47C8;
    state.MoonInitFunctionAddress = 0x002D4F10;

    const int skyboxId = background.SkyboxCommandArgument >= 0
                             ? background.SkyboxCommandArgument & 0xFF
                             : -1;
    switch (skyboxId) {
        case 0:
            state.MoonInitCallsiteAddress = 0x002E4988;
            state.CtxbBaseTypeLocalIndex = 4;
            break;
        case 8:
            state.MoonInitCallsiteAddress = 0x002E4B14;
            state.CtxbBaseTypeLocalIndex = 2;
            break;
        case 1:
            state.MoonInitCallsiteAddress = 0x002E4BD8;
            state.CtxbBaseTypeLocalIndex = 4;
            break;
        case 3:
            state.MoonInitCallsiteAddress = 0x002E4CE4;
            state.CtxbBaseTypeLocalIndex = 4;
            break;
        case 5:
            state.MoonInitCallsiteAddress = 0x002E4DA4;
            state.CtxbBaseTypeLocalIndex = 4;
            break;
        default:
            state.SourceStatus =
                "FUN_002e47c8 does not initialize FUN_002d4f10 for the selected skybox id";
            return state;
    }
    state.LayerCount = 3;

    constexpr std::array<uint32_t, 3> geometryTemplateIndices = { 1, 4, 4 };
    constexpr std::array<float, 3> geometryTemplateHalfExtents = { 0.5f, 0.5f, 0.5f };
    constexpr std::array<uint32_t, 3> runtimeObjectTemplateIndices = { 3, 4, 5 };
    constexpr std::array<uint32_t, 3> wrapModes = { 0x812F, 0x8370, 0x8370 };
    for (uint32_t layerIndex = 0; layerIndex < state.LayerCount; ++layerIndex) {
        const uint32_t typeLocalIndex = state.CtxbBaseTypeLocalIndex + layerIndex;
        const auto indexIt = std::find(
            background.NativeKankyoExtraDrawCtxbTypeLocalIndices.begin(),
            background.NativeKankyoExtraDrawCtxbTypeLocalIndices.end(), typeLocalIndex);
        if (indexIt == background.NativeKankyoExtraDrawCtxbTypeLocalIndices.end()) {
            continue;
        }
        const size_t selectedIndex = static_cast<size_t>(std::distance(
            background.NativeKankyoExtraDrawCtxbTypeLocalIndices.begin(), indexIt));
        if (selectedIndex >= background.NativeKankyoExtraCtxbNames.size() ||
            selectedIndex >= background.NativeKankyoExtraCtxbTextures.size()) {
            continue;
        }
        const auto& texture = background.NativeKankyoExtraCtxbTextures[selectedIndex];
        Oot3dNativeKankyoMoonLayerState layer;
        layer.CtxbTypeLocalIndex = typeLocalIndex;
        layer.TextureName = background.NativeKankyoExtraCtxbNames[selectedIndex];
        layer.Width = texture.Width;
        layer.Height = texture.Height;
        layer.TextureFormat = texture.TextureFormat;
        layer.DataType = texture.DataType;
        layer.GeometryTemplateIndex = geometryTemplateIndices[layerIndex];
        layer.GeometryTemplateHalfExtent = geometryTemplateHalfExtents[layerIndex];
        layer.RuntimeObjectTemplateIndex = runtimeObjectTemplateIndices[layerIndex];
        layer.MinMagFilter = 0x2601;
        layer.WrapS = wrapModes[layerIndex];
        layer.WrapT = wrapModes[layerIndex];
        state.Layers.push_back(std::move(layer));
    }

    state.NativeInitResolved = true;
    state.TextureInputsResolved = state.Layers.size() == state.LayerCount;
    state.Available = state.TextureInputsResolved;
    state.ReadyForBackendInput = state.NativeInitResolved && state.TextureInputsResolved;
    state.SourceStatus =
        "FUN_002e47c8 selects the skybox-specific FUN_002d4f10 callsite and CTXB base; "
        "FUN_002d4f10 resolves three consecutive native CTXB layers, geometry "
        "templates [1,4,4] with normalized half-extent 0.5, runtime object templates [3,4,5], "
        "GL_LINEAR filtering, and native "
        "CLAMP_TO_EDGE/MIRRORED_REPEAT sampler modes";
    return state;
}

Oot3dNativeKankyoSunHaloState NativeRenderResolveKankyoSunHaloState(
    const Oot3dNativeEnvironmentBackgroundState& background,
    const std::vector<uint8_t>& codeBin, uint32_t codeBase) {
    constexpr uint32_t kCelestialVectorProducerFunctionAddress = 0x004594E0;
    constexpr uint32_t kGroupSubmitFunctionAddress = 0x0047D578;
    constexpr uint32_t kBillboardProducerFunctionAddress = 0x002CEBD0;
    constexpr uint32_t kRuntimeObjectDrawFunctionAddress = 0x003F96BC;
    constexpr uint32_t kCelestialScaleXAddress = 0x004596B0;
    constexpr uint32_t kCelestialScaleYAddress = 0x004596B8;
    constexpr uint32_t kCelestialScaleZAddress = 0x004596BC;
    constexpr uint32_t kCelestialRadiusPointerAddress = 0x004596B4;
    constexpr uint32_t kCelestialRadiusValueOffset = 0x1C;
    constexpr uint32_t kPositionScaleAddress = 0x002CEFC4;
    constexpr uint32_t kBillboardScaleAddress = 0x002CEFC8;
    constexpr uint32_t kSunConstructorTablePointerAddress = 0x002D5434;
    constexpr uint32_t kSunCtxbSelectorTableWordIndex = 12;
    constexpr uint32_t kSunProfileTextureCount = 2;
    constexpr uint32_t kSunProfileCtxbStride = 2;
    constexpr uint32_t kSamplerWrapAddress = 0x002D5450;
    constexpr uint32_t kSamplerFilterAddress = 0x002D5454;

    const auto readCodeU32 = [&](uint32_t runtimeAddress) -> uint32_t {
        const auto offset = NativeRenderRuntimeAddressToFileOffset(
            runtimeAddress, codeBase, codeBin.size());
        return offset.has_value() && NativeRenderCanRead(codeBin, *offset, sizeof(uint32_t))
                   ? NativeRenderReadLeU32(codeBin, *offset)
                   : 0;
    };
    const auto readCodeFloat = [&](uint32_t runtimeAddress) -> float {
        const auto offset = NativeRenderRuntimeAddressToFileOffset(
            runtimeAddress, codeBase, codeBin.size());
        return offset.has_value() && NativeRenderCanRead(codeBin, *offset, sizeof(float))
                   ? NativeRenderReadLeFloat(codeBin, *offset)
                   : 0.0f;
    };

    Oot3dNativeKankyoSunHaloState state;
    state.SourceKind = "oot3d_code_bin_kankyo_profile_sun_billboard_runtime";
    state.CelestialVectorProducerFunctionAddress =
        kCelestialVectorProducerFunctionAddress;
    state.GroupSubmitFunctionAddress = kGroupSubmitFunctionAddress;
    state.BillboardProducerFunctionAddress = kBillboardProducerFunctionAddress;
    state.RuntimeObjectDrawFunctionAddress = kRuntimeObjectDrawFunctionAddress;
    state.CelestialScaleXAddress = kCelestialScaleXAddress;
    state.CelestialScaleYAddress = kCelestialScaleYAddress;
    state.CelestialScaleZAddress = kCelestialScaleZAddress;
    state.CelestialRadiusPointerAddress = kCelestialRadiusPointerAddress;
    state.PositionScaleAddress = kPositionScaleAddress;
    state.BillboardScaleAddress = kBillboardScaleAddress;
    state.SamplerWrapAddress = kSamplerWrapAddress;
    state.SamplerFilterAddress = kSamplerFilterAddress;
    state.CelestialScaleX = readCodeFloat(kCelestialScaleXAddress);
    state.CelestialScaleY = readCodeFloat(kCelestialScaleYAddress);
    state.CelestialScaleZ = readCodeFloat(kCelestialScaleZAddress);
    const uint32_t radiusSource = readCodeU32(kCelestialRadiusPointerAddress);
    state.CelestialRadiusValueAddress =
        radiusSource == 0 ? 0 : radiusSource + kCelestialRadiusValueOffset;
    state.CelestialRadius = state.CelestialRadiusValueAddress == 0
                                ? 0.0f
                                : readCodeFloat(state.CelestialRadiusValueAddress);
    state.PositionScale = readCodeFloat(kPositionScaleAddress);
    state.BillboardScale = readCodeFloat(kBillboardScaleAddress);
    state.WrapS = readCodeU32(kSamplerWrapAddress);
    state.WrapT = state.WrapS;
    state.MinMagFilter = readCodeU32(kSamplerFilterAddress);

    const uint32_t constructorTableAddress = readCodeU32(kSunConstructorTablePointerAddress);
    const uint32_t ctxbSelectorAddress =
        constructorTableAddress + kSunCtxbSelectorTableWordIndex * sizeof(uint32_t);
    const uint32_t firstSunCtxbTypeLocalIndex = readCodeU32(ctxbSelectorAddress);
    for (uint32_t profileGroup = 0; profileGroup < kSunProfileTextureCount; ++profileGroup) {
        Oot3dNativeKankyoSunProfileTextureState profileTexture;
        profileTexture.ProfileGroupIndex = profileGroup;
        profileTexture.CtxbTypeLocalIndex =
            firstSunCtxbTypeLocalIndex + profileGroup * kSunProfileCtxbStride;
        const auto indexIt = std::find(
            background.NativeKankyoExtraDrawCtxbTypeLocalIndices.begin(),
            background.NativeKankyoExtraDrawCtxbTypeLocalIndices.end(),
            profileTexture.CtxbTypeLocalIndex);
        if (indexIt != background.NativeKankyoExtraDrawCtxbTypeLocalIndices.end()) {
            const size_t selectedIndex = static_cast<size_t>(std::distance(
                background.NativeKankyoExtraDrawCtxbTypeLocalIndices.begin(), indexIt));
            if (selectedIndex < background.NativeKankyoExtraCtxbNames.size() &&
                selectedIndex < background.NativeKankyoExtraCtxbTextures.size()) {
                profileTexture.TextureName = background.NativeKankyoExtraCtxbNames[selectedIndex];
                const auto& texture = background.NativeKankyoExtraCtxbTextures[selectedIndex];
                profileTexture.Width = texture.Width;
                profileTexture.Height = texture.Height;
                profileTexture.TextureFormat = texture.TextureFormat;
                profileTexture.DataType = texture.DataType;
                profileTexture.TextureInputResolved =
                    texture.Rgba8Decoded && !texture.Rgba8.empty();
            }
        }
        state.ProfileTextures.push_back(std::move(profileTexture));
    }
    state.Available = std::any_of(
        state.ProfileTextures.begin(), state.ProfileTextures.end(),
        [](const auto& profileTexture) {
            return !profileTexture.TextureName.empty();
        });
    state.TextureInputResolved =
        state.Available &&
        std::all_of(state.ProfileTextures.begin(), state.ProfileTextures.end(),
                    [](const auto& profileTexture) {
                        return profileTexture.TextureInputResolved;
                    });
    if (!state.ProfileTextures.empty()) {
        const auto& texture = state.ProfileTextures.front();
        state.TextureName = texture.TextureName;
        state.Width = texture.Width;
        state.Height = texture.Height;
        state.TextureFormat = texture.TextureFormat;
        state.DataType = texture.DataType;
    }
    state.SamplerStateResolved =
        state.MinMagFilter == 0x2601 && state.WrapS == 0x812F && state.WrapT == 0x812F;
    state.NativeRouteResolved =
        std::isfinite(state.CelestialScaleX) &&
        std::isfinite(state.CelestialScaleY) &&
        std::isfinite(state.CelestialScaleZ) &&
        std::isfinite(state.CelestialRadius) && state.CelestialRadius > 0.0f &&
        std::isfinite(state.PositionScale) && state.PositionScale > 0.0f &&
        std::isfinite(state.BillboardScale) && state.BillboardScale > 0.0f;
    state.ReadyForBackendInput = state.NativeRouteResolved &&
                                 state.TextureInputResolved &&
                                 state.SamplerStateResolved;
    state.SourceStatus = state.ReadyForBackendInput
                             ? "FUN_004594e0 computes the solar vector from code.bin constants; "
                               "FUN_0047d578 selects current/target profile layers; FUN_002cebd0 "
                               "materializes the independent sun billboard; FUN_002d5124 selects "
                               "CTXB rows from its code.bin table with the native two-entry profile "
                               "stride before FUN_003f96bc draws it"
                             : "native profile-selected sun billboard input is incomplete; verify code.bin "
                               "producer constants, sampler words, and decoded CTXB payload";
    return state;
}

uint32_t NativeRenderResolveEffectPrimitiveDrawMode(
    const NativeKankyoEffectDrawConsumerContract& effectDrawConsumer,
    uint32_t drawModeRaw) {
    if (drawModeRaw == 5) {
        return effectDrawConsumer.PrimitiveDrawMode5ResolvedPrimitiveValue;
    }
    if (drawModeRaw == 6) {
        return effectDrawConsumer.PrimitiveDrawMode6ResolvedPrimitiveValue;
    }
    if (drawModeRaw == 4) {
        return effectDrawConsumer.PrimitiveDrawMode4ResolvedPrimitiveValue;
    }
    if (drawModeRaw == 0x6010) {
        return effectDrawConsumer.PrimitiveDrawMode6010ResolvedPrimitiveValue;
    }
    return 0;
}

Oot3dNativeKankyoPrimitiveBackendInputState NativeRenderBuildKankyoPrimitiveBackendInputState(
    const Oot3dNativeKankyoLensEffectState& lensEffect,
    const NativeKankyoRuntimeBridgeContract& bridgeContract,
    const Oot3dNativeRenderTexture* texture,
    const Oot3dNativeRenderTexture* terminalTexture) {
    const auto& effectDrawConsumer = bridgeContract.EffectDrawConsumer;
    const auto& lensRuntimeList = bridgeContract.LensflareRuntimeList;
    const auto& materialScalar = bridgeContract.MaterialScalarEmit;
    const auto& runtime28cBatch = bridgeContract.Runtime28CParticleBatch;

    Oot3dNativeKankyoPrimitiveBackendInputState input;
    input.SourceKind = "oot3d_kankyo_effect_primitive_backend_input_plan";
    input.MaterialScalarGameplayDrawFunctionAddress = 0x002E25F0;
    input.MaterialScalarGameplayFogUpdateCallsiteAddress =
        materialScalar.FogPayloadRuntimeUpdateCallerAddress;
    input.MaterialScalarGameplayRuntimeList0BindCallsiteAddress = 0x002E2C20;
    input.MaterialScalarGameplayRuntimeList1BindCallsiteAddress = 0x002E2C34;
    input.MaterialScalarGameplayViewPlayOffset = 0x01DC;
    input.MaterialScalarGameplayFinalFogRgbPlayOffset = 0x0A82;
    input.MaterialScalarGameplayFogFarPlayOffset = 0x0A78;
    input.MaterialScalarGameplayFogNearPlayOffset = 0x0A7C;
    input.MaterialScalarGameplayFogAlphaImmediate = 0xFF;
    input.MaterialScalarRuntimeListBinderAddress = materialScalar.RuntimeListBinderAddress;
    input.MaterialScalarRuntimeListBinderSourcePlayOffset =
        materialScalar.RuntimeListBinderSourcePlayOffset;
    input.MaterialScalarRuntimeList0PlayOffset = materialScalar.RuntimeList0PlayOffset;
    input.MaterialScalarRuntimeList1PlayOffset = materialScalar.RuntimeList1PlayOffset;
    input.MaterialScalarRuntimeListCountByteOffset =
        materialScalar.RuntimeListCountByteOffset;
    input.MaterialScalarRuntimeListEntryStrideBytes =
        materialScalar.RuntimeListEntryStrideBytes;
    input.MaterialScalarRuntimeListEntryRuntimeObjectPointerOffset =
        materialScalar.RuntimeListEntryRuntimeObjectPointerOffset;
    input.MaterialScalarRuntimeObjectPacketResolverAddress =
        materialScalar.RuntimeObjectMaterialPacketResolverAddress;
    input.MaterialScalarRuntimeObjectPacketOwnerPointerOffset =
        materialScalar.RuntimeObjectPacketOwnerPointerOffset;
    input.MaterialScalarRuntimeObjectPacketOwnerMaterialPacketOffset =
        materialScalar.RuntimeObjectPacketOwnerMaterialPacketOffset;
    input.MaterialScalarRuntimeListBinderResolved =
        materialScalar.RuntimeListBinderAddress == 0x002D960C &&
        materialScalar.RuntimePayloadBinderAddress == 0x00368704 &&
        materialScalar.RuntimeListBinderAppliesFogSourceToMaterialPackets;
    input.MaterialScalarRuntimeListSourceMatchesFogSource =
        materialScalar.RuntimeListBinderSourcePlayOffset == 0x5FC8 &&
        materialScalar.FogPayloadRuntimeUpdateAddress == 0x00464B2C &&
        materialScalar.FogPayloadRuntimeUpdateCallerAddress == 0x002E2674;
    input.MaterialScalarGameplayDrawOrderResolved =
        input.MaterialScalarGameplayDrawFunctionAddress == 0x002E25F0 &&
        input.MaterialScalarGameplayFogUpdateCallsiteAddress == 0x002E2674 &&
        materialScalar.FogPayloadRuntimeUpdateAddress == 0x00464B2C &&
        materialScalar.RuntimeListBinderAddress == 0x002D960C &&
        input.MaterialScalarGameplayRuntimeList0BindCallsiteAddress == 0x002E2C20 &&
        input.MaterialScalarGameplayRuntimeList1BindCallsiteAddress == 0x002E2C34 &&
        input.MaterialScalarGameplayViewPlayOffset == 0x01DC &&
        input.MaterialScalarGameplayFinalFogRgbPlayOffset == 0x0A82 &&
        input.MaterialScalarGameplayFogFarPlayOffset == 0x0A78 &&
        input.MaterialScalarGameplayFogNearPlayOffset == 0x0A7C &&
        input.MaterialScalarGameplayFogAlphaImmediate == 0xFF;
    input.MaterialScalarRuntimeListPacketResolverResolved =
        materialScalar.RuntimeObjectMaterialPacketResolverAddress == 0x003687A8 &&
        materialScalar.RuntimeListCountByteOffset == 0x06 &&
        materialScalar.RuntimeListEntryStrideBytes == 0x3C &&
        materialScalar.RuntimeListEntryRuntimeObjectPointerOffset == 0x0C &&
        materialScalar.RuntimeObjectPacketOwnerPointerOffset == 0x14 &&
        materialScalar.RuntimeObjectPacketOwnerMaterialPacketOffset == 0x10 &&
        materialScalar.RuntimePayloadBinderPacketVectorDestinationOffset ==
            materialScalar.MaterialPacketVectorBaseOffset &&
        materialScalar.RuntimePayloadBinderPacketAuxWordDestinationOffset ==
            materialScalar.MaterialPacketAuxWordOffset &&
        materialScalar.RuntimePayloadBinderPacketPayloadPointerDestinationOffset ==
            materialScalar.MaterialPacketPayloadPointerOffset;
    input.MaterialScalarRuntimeListSourceStatus =
        input.MaterialScalarRuntimeListBinderResolved &&
                input.MaterialScalarRuntimeListSourceMatchesFogSource &&
                input.MaterialScalarRuntimeListPacketResolverResolved
            ? "native material runtime list binder route is resolved from code.bin: "
              "Gameplay_Draw calls 00464B2C at 002E2674, then 002D960C walks "
              "play+0x4C30/play+0x500C, 003687A8 resolves packets, and 00368704 "
              "applies play+0x5FC8 fog source"
            : "native material runtime list binder route is incomplete";
    input.Runtime28CPositionProducerResolved = lensEffect.NativeLensPositionProducerResolved;
    input.Runtime28CPositionRuntimeInputResolved = lensEffect.NativeLensPositionRuntimeInputResolved;
    input.Runtime28CPositionRuntimeInputBlockedReason =
        lensEffect.NativeLensPositionRuntimeInputStatus;
    input.Runtime28CPositionProducerFunctionAddress =
        lensEffect.LensPositionProducerFunctionAddress;
    input.Runtime28CPositionProjectionHelperAddress =
        lensEffect.LensPositionProjectionHelperAddress;
    input.Runtime28CPositionViewProjectionHelperAddress =
        lensEffect.LensPositionViewProjectionHelperAddress;
    input.Runtime28CPositionViewProjectionMatrixPlayOffset =
        lensEffect.LensPositionViewProjectionMatrixPlayOffset;
    input.Runtime28CPositionViewProjectionSourceResolved =
        lensEffect.LensPositionViewProjectionSourceResolved;
    input.Runtime28CPositionGameplayDrawFunctionAddress =
        lensEffect.LensPositionGameplayDrawFunctionAddress;
    input.Runtime28CPositionViewSourceProducerFunctionAddress =
        lensEffect.LensPositionViewSourceProducerFunctionAddress;
    input.Runtime28CPositionViewSourceProducerCallsiteAddress =
        lensEffect.LensPositionViewSourceProducerCallsiteAddress;
    input.Runtime28CPositionViewPrepFunctionAddress =
        lensEffect.LensPositionViewPrepFunctionAddress;
    input.Runtime28CPositionViewPrepCallsiteAddress =
        lensEffect.LensPositionViewPrepCallsiteAddress;
    input.Runtime28CPositionViewUpdateFunctionAddress =
        lensEffect.LensPositionViewUpdateFunctionAddress;
    input.Runtime28CPositionViewUpdateCallsiteAddress =
        lensEffect.LensPositionViewUpdateCallsiteAddress;
    input.Runtime28CPositionViewProjectionSourceCopyHelperAddress =
        lensEffect.LensPositionViewProjectionSourceCopyHelperAddress;
    input.Runtime28CPositionViewProjectionSourceCopyCallsiteAddress =
        lensEffect.LensPositionViewProjectionSourceCopyCallsiteAddress;
    input.Runtime28CPositionViewProjectionComposeCopyHelperAddress =
        lensEffect.LensPositionViewProjectionComposeCopyHelperAddress;
    input.Runtime28CPositionViewProjectionComposeCopyCallsiteAddress =
        lensEffect.LensPositionViewProjectionComposeCopyCallsiteAddress;
    input.Runtime28CPositionViewProjectionComposeHelperAddress =
        lensEffect.LensPositionViewProjectionComposeHelperAddress;
    input.Runtime28CPositionViewProjectionComposeCallsiteAddress =
        lensEffect.LensPositionViewProjectionComposeCallsiteAddress;
    input.Runtime28CPositionViewStructPlayOffset =
        lensEffect.LensPositionViewStructPlayOffset;
    input.Runtime28CPositionViewProjectionSourceMatrixPlayOffset =
        lensEffect.LensPositionViewProjectionSourceMatrixPlayOffset;
    input.Runtime28CPositionViewProjectionComposeMatrixPlayOffset =
        lensEffect.LensPositionViewProjectionComposeMatrixPlayOffset;
    input.Runtime28CPositionViewProjectionMatrixWordCount =
        lensEffect.LensPositionViewProjectionMatrixWordCount;
    input.Runtime28CPositionViewProjectionComposeMatrixWordCount =
        lensEffect.LensPositionViewProjectionComposeMatrixWordCount;
    input.Runtime28CPositionViewProjectionIdentityRowPatched =
        lensEffect.LensPositionViewProjectionIdentityRowPatched;
    input.Runtime28CPositionViewProjectionRuntimeMatrixMaterialized =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixMaterialized;
    input.Runtime28CPositionViewProjectionRuntimeMatrixSourceKind =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixSourceKind;
    input.Runtime28CPositionViewProjectionRuntimeMatrixStatus =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixStatus;
    input.Runtime28CPositionViewProjectionRuntimeMatrix =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrix;
    input.Runtime28CPositionProjectionSamples =
        lensEffect.NativeLensPositionProjectionSamples;
    input.Runtime28CPositionScreenCenterX = lensEffect.LensPositionScreenCenterX;
    input.Runtime28CPositionScreenCenterY = lensEffect.LensPositionScreenCenterY;
    input.Runtime28CPositionBaseZ = lensEffect.LensPositionBaseZ;
    input.Runtime28CPositionDirectionScale = lensEffect.LensPositionDirectionScale;
    input.Runtime28CPositionDistanceLimitScale =
        lensEffect.LensPositionDistanceLimitScale;
    input.Runtime28CPositionRuntimeScaleResolved =
        lensEffect.NativeLensPositionRuntimeScaleResolved;
    input.Runtime28CPositionRuntimeScaleArgument =
        lensEffect.LensPositionRuntimeScaleArgument;
    input.Runtime28CPositionRuntimeScaleMultiplierAddress =
        lensEffect.LensPositionRuntimeScaleMultiplierAddress;
    input.Runtime28CPositionRuntimeScaleMultiplier =
        lensEffect.LensPositionRuntimeScaleMultiplier;
    input.Runtime28CPositionProjectionScaleX = lensEffect.LensPositionProjectionScaleX;
    input.Runtime28CPositionProjectionScaleY = lensEffect.LensPositionProjectionScaleY;
    input.Runtime28CPositionProjectionBaseY = lensEffect.LensPositionProjectionBaseY;
    input.PrimitiveDrawPacketFunctionAddress =
        effectDrawConsumer.PrimitiveDrawPacketFunctionAddress;
    input.PrimitiveDrawAttributeMaskFunctionAddress =
        effectDrawConsumer.PrimitiveDrawAttributeMaskFunctionAddress;
    input.PrimitiveDrawCommandCommitAddress =
        effectDrawConsumer.PrimitiveDrawCommandCommitAddress;
    input.PrimitiveDrawPacketWordCount = effectDrawConsumer.PrimitiveDrawPacketWordCount;
    input.PrimitiveDrawIndexElementType =
        effectDrawConsumer.PrimitiveDrawIndexElementTypeLiteral;
    input.PrimitiveDrawEffectStackIndexBase =
        effectDrawConsumer.PrimitiveDrawEffectIndexBaseStackValue;
    input.PrimitiveDrawAttributeMaskHeaderWord =
        effectDrawConsumer.PrimitiveDrawAttributeMaskHeaderWord;
    input.PrimitiveDrawAttributeMaskPayloadOrMask =
        effectDrawConsumer.PrimitiveDrawAttributeMaskPayloadOrMask;
    input.RuntimeDrawCountOffset = effectDrawConsumer.RuntimeDrawPayloadOffset;
    input.PrimaryBatchLastElementIndex = lensRuntimeList.PrimaryElementSubmitIndex;
    input.TerminalElementIndex = lensRuntimeList.TerminalElementSubmitIndex;
    input.QuadBatchVisibleElementCount =
        std::min(lensEffect.ElementCount, input.PrimaryBatchLastElementIndex + 1);
    input.QuadBatchNativeVerticesPerQuad = lensRuntimeList.QuadBatchNativeVerticesPerQuad;
    input.QuadBatchExpandedVertexCount =
        input.QuadBatchVisibleElementCount * input.QuadBatchNativeVerticesPerQuad;
    input.Runtime28CQuadVertexCount = runtime28cBatch.QuadVertexCount;
    input.Runtime28CDrawCountPerVisibleBatch = runtime28cBatch.DrawCountPerVisibleBatch;
    input.Runtime28CPositionRecordStrideBytes = runtime28cBatch.PositionRecordStrideBytes;
    input.Runtime28CMatrixRecordStrideBytes = runtime28cBatch.MatrixArrayRecordStrideBytes;
    input.Runtime28CLocalVectorRecordStrideBytes = runtime28cBatch.LocalVectorRecordStrideBytes;
    input.Runtime28CColorRecordStrideBytes = runtime28cBatch.ColorRecordStrideBytes;
    input.Runtime28CTexcoordRecordStrideBytes = runtime28cBatch.TexcoordRecordStrideBytes;
    input.Runtime28CBatchCapacityOffset = runtime28cBatch.RuntimeBatchCapacityOffset;
    input.Runtime28CLocalVectorArrayPointerOffset =
        runtime28cBatch.RuntimeLocalVectorArrayPointerOffset;
    input.Runtime28CSpecialSubmitStartElementIndex =
        lensRuntimeList.SpecialSubmitElementStartIndex;
    input.Runtime28CSpecialSubmitEndElementIndex =
        lensRuntimeList.SpecialSubmitElementEndIndex;
    input.Runtime28CPrimarySubmitQueueIndexBase =
        lensRuntimeList.PrimarySubmitQueueIndexBase;
    input.Runtime28CTerminalSubmitQueueIndexBase =
        lensRuntimeList.TerminalSubmitQueueIndexBase;
    input.TerminalNativeVertexCount = lensRuntimeList.QuadBatchFallbackQuadVertexCount;
    input.Runtime28CDrawCount =
        input.QuadBatchVisibleElementCount * input.Runtime28CDrawCountPerVisibleBatch;
    input.OverlayPrimitiveVertexCount = lensEffect.NativePrimitiveVertexCount;
    input.ColorPassDrawModeRaw = lensEffect.NativePrimitiveColorPass;
    input.ColorPassPrimitiveValue =
        NativeRenderResolveEffectPrimitiveDrawMode(effectDrawConsumer, input.ColorPassDrawModeRaw);
    input.AlphaPassDrawModeRaw = lensEffect.NativePrimitiveAlphaPass;
    input.AlphaPassPrimitiveValue =
        NativeRenderResolveEffectPrimitiveDrawMode(effectDrawConsumer, input.AlphaPassDrawModeRaw);
    input.TextureName = !lensEffect.LensflareTextureName.empty()
                            ? lensEffect.LensflareTextureName
                            : lensEffect.SunTextureName;
    input.TerminalTextureName = lensEffect.SunTextureName;
    if (texture != nullptr) {
        input.Texture = *texture;
    }
    if (terminalTexture != nullptr) {
        input.TerminalTexture = *terminalTexture;
    }
    input.PrimitiveDrawPacketLiteralWords.assign(
        effectDrawConsumer.PrimitiveDrawPacketLiteralWords.begin(),
        effectDrawConsumer.PrimitiveDrawPacketLiteralWords.end());

    input.PrimitivePacketBridgeResolved =
        effectDrawConsumer.PrimitiveDrawPacketBridgeResolved &&
        effectDrawConsumer.PrimitiveDrawCountComesFromRuntimePayload &&
        effectDrawConsumer.PrimitiveDrawEffectStackIndexBaseIsZero &&
        input.PrimitiveDrawPacketFunctionAddress != 0 &&
        input.PrimitiveDrawCommandCommitAddress != 0 &&
        input.PrimitiveDrawPacketWordCount > 0 &&
        input.PrimitiveDrawIndexElementType == 0x1403;
    input.AttributeMaskPacketResolved =
        effectDrawConsumer.PrimitiveDrawAttributeMaskPacketResolved &&
        input.PrimitiveDrawAttributeMaskFunctionAddress != 0 &&
        input.PrimitiveDrawAttributeMaskHeaderWord != 0 &&
        input.PrimitiveDrawAttributeMaskPayloadOrMask != 0;
    input.QuadBatchLanePlanResolved =
        lensRuntimeList.RuntimeBackendBufferHelpersResolved &&
        lensRuntimeList.QuadBatchPrimaryBufferResolverAddress != 0 &&
        lensRuntimeList.QuadBatchOptionalBuffer0ResolverAddress != 0 &&
        lensRuntimeList.QuadBatchOptionalBuffer1ResolverAddress != 0 &&
        lensRuntimeList.QuadBatchDrawHandleVertexCountOffset == input.RuntimeDrawCountOffset &&
        input.QuadBatchNativeVerticesPerQuad == 6;
    input.Runtime28CParticleBatchContractResolved =
        runtime28cBatch.CallbackAddress == lensRuntimeList.QuadBatchRuntimeDrawMethodAddress &&
        runtime28cBatch.OutputPositionBufferResolverAddress ==
            lensRuntimeList.QuadBatchPrimaryBufferResolverAddress &&
        runtime28cBatch.OutputColorBufferResolverAddress ==
            lensRuntimeList.QuadBatchOptionalBuffer0ResolverAddress &&
        runtime28cBatch.OutputTexcoordBufferResolverAddress ==
            lensRuntimeList.QuadBatchOptionalBuffer1ResolverAddress &&
        input.Runtime28CQuadVertexCount == 4 &&
        input.Runtime28CDrawCountPerVisibleBatch == 6 &&
        input.Runtime28CPositionRecordStrideBytes == 0x0C &&
        input.Runtime28CMatrixRecordStrideBytes == 0x30 &&
        input.Runtime28CLocalVectorRecordStrideBytes == 0x0C &&
        input.Runtime28CColorRecordStrideBytes == 0x10 &&
        input.Runtime28CTexcoordRecordStrideBytes == 0x08 &&
        input.Runtime28CBatchCapacityOffset ==
            lensRuntimeList.QuadBatchRuntimeElementCapacityOffset &&
        input.Runtime28CLocalVectorArrayPointerOffset ==
            lensRuntimeList.QuadBatchRuntimeInputDepthsPointerOffset &&
        runtime28cBatch.EnqueueWriterResolved &&
        runtime28cBatch.ObjectTransformReplicatesColorForAllQuadCorners;
    input.Runtime28CEnqueueDispatchResolved =
        lensRuntimeList.DrawHelperDispatchResolved &&
        lensRuntimeList.TargetGroupDispatchOnlyWhenDifferent &&
        lensRuntimeList.LensflareRuntimeElementCount == lensEffect.ElementCount &&
        lensRuntimeList.DrawAddress == 0x00484F5C &&
        lensRuntimeList.DrawHelperAddress == 0x002C5314 &&
        lensRuntimeList.ObjectTransformAddress == 0x00371F1C &&
        lensRuntimeList.SubmitQueueAddress == 0x002C517C &&
        lensRuntimeList.SubmitRecordWriterAddress == 0x002C1AE8 &&
        input.Runtime28CSpecialSubmitStartElementIndex == 0x0B &&
        input.Runtime28CSpecialSubmitEndElementIndex == 0x0C &&
        input.Runtime28CPrimarySubmitQueueIndexBase == 0x12 &&
        input.Runtime28CTerminalSubmitQueueIndexBase == 0x14 &&
        input.Runtime28CSpecialSubmitStartElementIndex ==
            lensRuntimeList.PrimaryElementSubmitIndex &&
        input.Runtime28CSpecialSubmitEndElementIndex ==
            lensRuntimeList.TerminalElementSubmitIndex;
    input.TextureInputResolved = lensEffect.TextureInputsResolved && !input.TextureName.empty();
    input.DecodedTextureInputResolved =
        input.TextureInputResolved &&
        input.Texture.Rgba8Decoded &&
        input.Texture.Width > 0 &&
        input.Texture.Height > 0 &&
        input.Texture.Rgba8.size() ==
            static_cast<size_t>(input.Texture.Width) * static_cast<size_t>(input.Texture.Height) * 4;
    input.TerminalTextureInputResolved =
        lensEffect.TextureInputsResolved && !input.TerminalTextureName.empty();
    input.TerminalDecodedTextureInputResolved =
        input.TerminalTextureInputResolved &&
        input.TerminalTexture.Rgba8Decoded &&
        input.TerminalTexture.Width > 0 &&
        input.TerminalTexture.Height > 0 &&
        input.TerminalTexture.Rgba8.size() ==
            static_cast<size_t>(input.TerminalTexture.Width) *
                static_cast<size_t>(input.TerminalTexture.Height) * 4;
    input.TextureHasNativeAlpha =
        input.DecodedTextureInputResolved && input.Texture.HasNativeAlpha;
    input.NativePicaAlphaBlendSemanticsRequired =
        input.DecodedTextureInputResolved &&
        !input.TextureHasNativeAlpha &&
        input.AlphaPassPrimitiveValue != 0;
    input.NativePicaAlphaBlendSemanticsResolved =
        input.DecodedTextureInputResolved;
    input.NativePicaAlphaBlendSemanticsSourceStatus =
        input.NativePicaAlphaBlendSemanticsResolved
            ? (input.NativePicaAlphaBlendSemanticsRequired
                   ? "native PICA output-merger source-alpha blend is represented by the "
                     "backend native blend state; the CTXB payload does not need to carry "
                     "texture alpha for this primitive"
                   : "native texture alpha is directly representable by the decoded CTXB payload")
            : "native PICA alpha/blend semantics are waiting for decoded texture input";
    input.ColorInputResolved = !lensEffect.HalationColors.empty();
    input.VisibleBackendSubmitResolved = lensEffect.NativeVisibleBackendSubmitResolved;

    for (const auto& element : lensEffect.Elements) {
        const auto elementInput = Oot3dNativeKankyoRuntime28CVisibleBatchInput{
            static_cast<uint32_t>(std::min<size_t>(
                input.Runtime28CVisibleBatches.size(),
                std::numeric_limits<uint32_t>::max())),
            element.Index,
            element.ScaleMin,
            element.ScaleMax,
            element.Offset,
            element.Depth,
            lensEffect.HalationColors.empty()
                ? ColorRgba8{ 255, 255, 255, 255 }
                : lensEffect.HalationColors[element.Index % lensEffect.HalationColors.size()],
        };
        if (element.Index == input.TerminalElementIndex) {
            input.TerminalElementInput = elementInput;
            input.TerminalElementInputResolved = true;
            ++input.TerminalSubmitElementCount;
            continue;
        }
        if (element.Index > input.PrimaryBatchLastElementIndex) {
            continue;
        }
        const uint32_t visibleBatchIndex =
            static_cast<uint32_t>(std::min<size_t>(
                input.Runtime28CVisibleBatches.size(),
                std::numeric_limits<uint32_t>::max()));
        auto primaryElementInput = elementInput;
        primaryElementInput.VisibleBatchIndex = visibleBatchIndex;
        input.Runtime28CVisibleBatches.push_back(primaryElementInput);
        for (uint32_t corner = 0; corner < input.Runtime28CQuadVertexCount; ++corner) {
            input.Runtime28CQuadLaneVertices.push_back({
                visibleBatchIndex,
                element.Index,
                corner,
                element.Index * input.Runtime28CPositionRecordStrideBytes,
                element.Index * input.Runtime28CMatrixRecordStrideBytes,
                (element.Index * input.Runtime28CQuadVertexCount + corner) *
                    input.Runtime28CColorRecordStrideBytes,
                element.Index * input.Runtime28CTexcoordRecordStrideBytes,
                0x260 + corner * input.Runtime28CTexcoordRecordStrideBytes,
                0x264 + corner * input.Runtime28CTexcoordRecordStrideBytes,
            });
        }
        ++input.Runtime28CQueuedElementCount;
        if (element.Index >= input.Runtime28CSpecialSubmitStartElementIndex &&
            element.Index <= input.Runtime28CSpecialSubmitEndElementIndex) {
            ++input.Runtime28CSpecialSubmitElementCount;
        }
    }
    input.Runtime28CQuadLaneVertexInputCount =
        static_cast<uint32_t>(std::min<size_t>(
            input.Runtime28CQuadLaneVertices.size(),
            std::numeric_limits<uint32_t>::max()));
    input.Runtime28CVisibleBatchInputsResolved =
        input.Runtime28CVisibleBatches.size() == input.QuadBatchVisibleElementCount &&
        input.QuadBatchVisibleElementCount > 0;
    input.Runtime28CQuadLaneMaterialized =
        input.Runtime28CParticleBatchContractResolved &&
        input.Runtime28CEnqueueDispatchResolved &&
        input.Runtime28CVisibleBatchInputsResolved &&
        input.Runtime28CQuadLaneVertexInputCount ==
            input.QuadBatchVisibleElementCount * input.Runtime28CQuadVertexCount &&
        input.Runtime28CDrawCount == input.QuadBatchExpandedVertexCount;
    input.GeometryInputResolved = input.Runtime28CQuadLaneMaterialized;
    input.VisibleBackendSubmitResolved =
        input.VisibleBackendSubmitResolved ||
        (input.Runtime28CQuadLaneMaterialized &&
         input.Runtime28CVisibleBatchInputsResolved &&
         !input.Runtime28CVisibleBatches.empty());

    for (const auto& coord : lensEffect.OverlayGeometryCoordinatePairs) {
        const auto& color =
            lensEffect.HalationColors.empty()
                ? ColorRgba8{ 255, 255, 255, 255 }
                : lensEffect.HalationColors[coord.Index % lensEffect.HalationColors.size()];
        input.OverlayVertices.push_back({ coord.Index, coord.X, coord.Y, color });
    }

    input.ReadyForBackendInput =
        input.PrimitivePacketBridgeResolved &&
        input.AttributeMaskPacketResolved &&
        input.QuadBatchLanePlanResolved &&
        input.Runtime28CQuadLaneMaterialized &&
        input.TextureInputResolved &&
        input.GeometryInputResolved &&
        input.ColorInputResolved &&
        input.ColorPassPrimitiveValue != 0 &&
        input.AlphaPassPrimitiveValue != 0 &&
        input.QuadBatchExpandedVertexCount > 0;
    input.ReadyForBackendRender =
        input.ReadyForBackendInput &&
        input.Runtime28CPositionRuntimeInputResolved &&
        input.Runtime28CPositionRuntimeScaleResolved &&
        input.NativePicaAlphaBlendSemanticsResolved &&
        input.VisibleBackendSubmitResolved;
    input.Available =
        input.ReadyForBackendInput ||
        input.PrimitivePacketBridgeResolved ||
        input.QuadBatchLanePlanResolved;

    if (input.ReadyForBackendRender) {
        input.SourceStatus =
            "native primary lensflare packet, quad-batch lanes, and visible backend submit are "
            "resolved for Lens* elements 0..11; native element 12 remains a separate terminal "
            "object route and is not rendered through this screen-space batch";
    } else if (input.ReadyForBackendInput) {
        if (!input.Runtime28CPositionRuntimeInputResolved) {
            input.SourceStatus =
                "native kankyo primitive packet, quad-batch backend input, and lens position "
                "producer constants are resolved from code.bin and Lens* TBD/CTXB resources; "
                "the native play+0x5bb4 source chain is identified, but final runtime centers "
                "still require host materialization of the current native view/projection matrix";
            input.VisibleBackendBlockedReason =
                "native_lens_position_runtime_view_projection_matrices_not_materialized";
        } else {
            input.SourceStatus =
                "native kankyo primitive packet and quad-batch backend input are resolved from "
                "code.bin and Lens* TBD/CTXB resources; final visible backend submit is still pending";
            input.VisibleBackendBlockedReason =
                "native_effect_primitive_submit_backend_not_yet_bound_to_opengl_renderer";
        }
    } else {
        input.SourceStatus =
            "native kankyo primitive backend input is incomplete; verify packet bridge, quad-batch "
            "lanes, Lens* TBD records, and CTXB texture inputs";
    }
    return input;
}

Oot3dNativeKankyoLensEffectState NativeRenderResolveKankyoLensEffectState(
    const Oot3dNativeEnvironmentBackgroundState& background,
    const std::vector<uint8_t>& codeBin,
    uint32_t codeBase) {
    constexpr uint32_t kEffectStateBuilderFunctionAddress = 0x002E2E60;
    constexpr uint32_t kEffectTypeBytePlayOffset = 0x5C76;
    constexpr uint32_t kEffectModeBytePlayOffset = 0x7F12;
    constexpr uint32_t kEffectDescriptorBasePlayOffset = 0x7D08;
    constexpr uint32_t kEffectDescriptorTypeSlotOffset = 0x1E0;
    constexpr uint32_t kEffectDescriptorSubmitCallbackSlotOffset = 0x1F0;
    constexpr uint32_t kFlashSubmitFunctionAddress = 0x0045C25C;
    constexpr uint32_t kColorBytesOverlayHelperAddress = 0x0045F538;
    constexpr uint32_t kPackedRgbaOverlayHelperAddress = 0x0045F868;
    constexpr uint32_t kColorBytesOverlayHelperAssignmentLiteralAddress = 0x002E35E0;
    constexpr uint32_t kPackedRgbaOverlayHelperAssignmentLiteralAddress = 0x002E31D8;
    constexpr uint32_t kImmediateSubmitHelperAddress = 0x003339E8;
    constexpr uint32_t kSchedulerSubmitHelperAddress = 0x00328350;
    constexpr uint32_t kLensPositionProducerFunctionAddress = 0x002D97E4;
    constexpr uint32_t kLensPositionProjectionHelperAddress = 0x00484DB4;
    constexpr uint32_t kLensPositionViewProjectionHelperAddress = 0x00368CC0;
    constexpr uint32_t kLensPositionViewProjectionMatrixPlayOffset = 0x5BB4;
    constexpr uint32_t kLensPositionGameplayDrawFunctionAddress = 0x002E25F0;
    constexpr uint32_t kLensPositionViewSourceProducerFunctionAddress = 0x00464B2C;
    constexpr uint32_t kLensPositionViewSourceProducerCallsiteAddress = 0x002E2674;
    constexpr uint32_t kLensPositionViewPrepFunctionAddress = 0x002DE690;
    constexpr uint32_t kLensPositionViewPrepCallsiteAddress = 0x002E2688;
    constexpr uint32_t kLensPositionViewUpdateFunctionAddress = 0x00471BA4;
    constexpr uint32_t kLensPositionViewUpdateCallsiteAddress = 0x002E2694;
    constexpr uint32_t kLensPositionViewProjectionSourceCopyHelperAddress = 0x00324744;
    constexpr uint32_t kLensPositionViewProjectionSourceCopyCallsiteAddress = 0x002E26A8;
    constexpr uint32_t kLensPositionViewProjectionComposeCopyHelperAddress = 0x00372224;
    constexpr uint32_t kLensPositionViewProjectionComposeCopyCallsiteAddress = 0x002E26B4;
    constexpr uint32_t kLensPositionViewProjectionComposeHelperAddress = 0x002D9688;
    constexpr uint32_t kLensPositionViewProjectionComposeCallsiteAddress = 0x002E26E0;
    constexpr uint32_t kLensPositionViewStructPlayOffset = 0x0188;
    constexpr uint32_t kLensPositionViewProjectionSourceMatrixPlayOffset = 0x01DC;
    constexpr uint32_t kLensPositionViewProjectionComposeMatrixPlayOffset = 0x029C;
    constexpr uint32_t kLensPositionViewProjectionMatrixWordCount = 16;
    constexpr uint32_t kLensPositionViewProjectionComposeMatrixWordCount = 12;
    constexpr uint32_t kLensPositionScreenCenterXAddress = 0x002D9B9C;
    constexpr uint32_t kLensPositionScreenCenterYAddress = 0x002D9BA0;
    constexpr uint32_t kLensPositionBaseZAddress = 0x002D9BA4;
    constexpr uint32_t kLensPositionDirectionScaleAddress = 0x002D9BD0;
    constexpr uint32_t kLensPositionDistanceLimitScaleAddress = 0x002D9BCC;
    constexpr uint32_t kLensPositionRuntimeScaleMultiplierAddress = 0x002D9BD8;
    constexpr uint32_t kLensPositionPrimarySourceRuntimeScaleArgument = 0x0172;
    constexpr uint32_t kLensPositionProjectionScaleXAddress = 0x00484E24;
    constexpr uint32_t kLensPositionProjectionScaleYAddress = 0x00484E28;
    constexpr uint32_t kLensPositionProjectionBaseYAddress = 0x00484E2C;
    constexpr uint32_t kLensVisibilityMinStepAddress = 0x002D9BB8;
    constexpr uint32_t kLensVisibilityScaleAddress = 0x002D9BBC;
    constexpr uint32_t kLensVisibilityVisibleTargetAddress = 0x002D9BC0;
    constexpr uint32_t kLensVisibilityScreenMaxXAddress = 0x002D9BC8;
    constexpr uint32_t kLensPositionPrimarySourceProducerFunctionAddress = 0x0045945C;
    constexpr uint32_t kLensPositionPrimarySourceProducerCallsiteAddress = 0x002E2D28;
    constexpr uint32_t kLensPositionPrimarySourceOffsetUpdateFunctionAddress = 0x004594E0;
    constexpr uint32_t kLensPositionPrimarySourceBaseCameraXPlayOffset = 0x01B8;
    constexpr uint32_t kLensPositionPrimarySourceBaseCameraYPlayOffset = 0x01BC;
    constexpr uint32_t kLensPositionPrimarySourceBaseCameraZPlayOffset = 0x01C0;
    constexpr uint32_t kLensPositionPrimarySourceKankyoOffsetXPlayOffset = 0x3194;
    constexpr uint32_t kLensPositionPrimarySourceKankyoOffsetYPlayOffset = 0x3198;
    constexpr uint32_t kLensPositionPrimarySourceKankyoOffsetZPlayOffset = 0x319C;
    constexpr uint32_t kLensPositionPrimarySourceActiveAngleStateAddress = 0x00587958;
    constexpr uint32_t kLensPositionPrimarySourceActiveAngleHalfwordOffset = 0x0C;
    constexpr uint32_t kLensPositionPrimarySourceScaleXAddress = 0x004596B0;
    constexpr uint32_t kLensPositionPrimarySourceScaleYAddress = 0x004596B8;
    constexpr uint32_t kLensPositionPrimarySourceScaleZAddress = 0x004596BC;
    constexpr uint32_t kLensPositionPrimarySourceRadiusPointerAddress = 0x004596B4;
    constexpr uint32_t kLensPositionPrimarySourceRadiusValueOffset = 0x1C;
    constexpr uint32_t kOverlayGeometryTableAddress = 0x005311BC;
    constexpr uint32_t kOverlayColorByteScaleAddress = 0x0045F784;
    constexpr uint32_t kOverlayBaseAlphaAddress = 0x0045F788;
    constexpr uint32_t kOverlayDepthThresholdAddress = 0x0045F78C;
    constexpr uint32_t kOverlayDepthFadeScaleAddress = 0x0045F790;
    constexpr uint32_t kOverlayGeometryCoordinatePairCount = 9;
    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    const auto& lensRuntimeList = bridgeContract.LensflareRuntimeList;

    auto readCodeFloat = [&](uint32_t runtimeAddress) -> float {
        const auto offset = NativeRenderRuntimeAddressToFileOffset(runtimeAddress, codeBase, codeBin.size());
        if (!offset.has_value() || !NativeRenderCanRead(codeBin, *offset, 4)) {
            return 0.0f;
        }
        return NativeRenderReadLeFloat(codeBin, *offset);
    };

    auto readCodeU32 = [&](uint32_t runtimeAddress) -> uint32_t {
        const auto offset = NativeRenderRuntimeAddressToFileOffset(runtimeAddress, codeBase, codeBin.size());
        if (!offset.has_value() || !NativeRenderCanRead(codeBin, *offset, 4)) {
            return 0;
        }
        return NativeRenderReadLeU32(codeBin, *offset);
    };

    Oot3dNativeKankyoLensEffectState state;
    state.EffectStateBuilderFunctionAddress = kEffectStateBuilderFunctionAddress;
    state.EffectTypeBytePlayOffset = kEffectTypeBytePlayOffset;
    state.EffectModeBytePlayOffset = kEffectModeBytePlayOffset;
    state.EffectDescriptorBasePlayOffset = kEffectDescriptorBasePlayOffset;
    state.EffectDescriptorTypeSlotOffset = kEffectDescriptorTypeSlotOffset;
    state.EffectDescriptorSubmitCallbackSlotOffset = kEffectDescriptorSubmitCallbackSlotOffset;
    state.FlashSubmitFunctionAddress = kFlashSubmitFunctionAddress;
    state.ColorBytesOverlayHelperAddress = kColorBytesOverlayHelperAddress;
    state.PackedRgbaOverlayHelperAddress = kPackedRgbaOverlayHelperAddress;
    state.ColorBytesOverlayHelperAssignmentLiteralAddress =
        kColorBytesOverlayHelperAssignmentLiteralAddress;
    state.PackedRgbaOverlayHelperAssignmentLiteralAddress =
        kPackedRgbaOverlayHelperAssignmentLiteralAddress;
    state.ColorBytesOverlayEffectTypePrimary = 0x00;
    state.ColorBytesOverlayEffectTypeAlternate = 0x08;
    state.PackedRgbaOverlayEffectTypeRangeStart = 0x20;
    state.PackedRgbaOverlayEffectTypeRangeEnd = 0x3F;
    state.ImmediateSubmitHelperAddress = kImmediateSubmitHelperAddress;
    state.SchedulerSubmitHelperAddress = kSchedulerSubmitHelperAddress;
    state.LensRuntimeListKankyoOffset = lensRuntimeList.KankyoListOffset;
    state.LensRuntimeDrawAddress = lensRuntimeList.DrawAddress;
    state.LensRuntimeDrawHelperAddress = lensRuntimeList.DrawHelperAddress;
    state.LensRuntimePrimaryBuilderAddress = lensRuntimeList.PrimaryBuilderAddress;
    state.LensRuntimeSubmitQueueAddress = lensRuntimeList.SubmitQueueAddress;
    state.LensRuntimeSubmitRecordWriterAddress = lensRuntimeList.SubmitRecordWriterAddress;
    state.LensRuntimeQuadBatchDrawMethodAddress = lensRuntimeList.QuadBatchRuntimeDrawMethodAddress;
    state.LensRuntimeQuadBatchNativeVerticesPerQuad = lensRuntimeList.QuadBatchNativeVerticesPerQuad;
    state.LensRuntimePrimaryElementSubmitIndex = lensRuntimeList.PrimaryElementSubmitIndex;
    state.LensRuntimeTerminalElementSubmitIndex = lensRuntimeList.TerminalElementSubmitIndex;
    state.LensRuntimeVisibleFlagMask = lensRuntimeList.RuntimeObjectVisibleFlagMask;
    state.LensPositionProducerFunctionAddress = kLensPositionProducerFunctionAddress;
    state.LensPositionProjectionHelperAddress = kLensPositionProjectionHelperAddress;
    state.LensPositionViewProjectionHelperAddress = kLensPositionViewProjectionHelperAddress;
    state.LensPositionViewProjectionMatrixPlayOffset =
        kLensPositionViewProjectionMatrixPlayOffset;
    state.LensPositionGameplayDrawFunctionAddress = kLensPositionGameplayDrawFunctionAddress;
    state.LensPositionViewSourceProducerFunctionAddress =
        kLensPositionViewSourceProducerFunctionAddress;
    state.LensPositionViewSourceProducerCallsiteAddress =
        kLensPositionViewSourceProducerCallsiteAddress;
    state.LensPositionViewPrepFunctionAddress = kLensPositionViewPrepFunctionAddress;
    state.LensPositionViewPrepCallsiteAddress = kLensPositionViewPrepCallsiteAddress;
    state.LensPositionViewUpdateFunctionAddress = kLensPositionViewUpdateFunctionAddress;
    state.LensPositionViewUpdateCallsiteAddress = kLensPositionViewUpdateCallsiteAddress;
    state.LensPositionViewProjectionSourceCopyHelperAddress =
        kLensPositionViewProjectionSourceCopyHelperAddress;
    state.LensPositionViewProjectionSourceCopyCallsiteAddress =
        kLensPositionViewProjectionSourceCopyCallsiteAddress;
    state.LensPositionViewProjectionComposeCopyHelperAddress =
        kLensPositionViewProjectionComposeCopyHelperAddress;
    state.LensPositionViewProjectionComposeCopyCallsiteAddress =
        kLensPositionViewProjectionComposeCopyCallsiteAddress;
    state.LensPositionViewProjectionComposeHelperAddress =
        kLensPositionViewProjectionComposeHelperAddress;
    state.LensPositionViewProjectionComposeCallsiteAddress =
        kLensPositionViewProjectionComposeCallsiteAddress;
    state.LensPositionViewStructPlayOffset = kLensPositionViewStructPlayOffset;
    state.LensPositionViewProjectionSourceMatrixPlayOffset =
        kLensPositionViewProjectionSourceMatrixPlayOffset;
    state.LensPositionViewProjectionComposeMatrixPlayOffset =
        kLensPositionViewProjectionComposeMatrixPlayOffset;
    state.LensPositionViewProjectionMatrixWordCount =
        kLensPositionViewProjectionMatrixWordCount;
    state.LensPositionViewProjectionComposeMatrixWordCount =
        kLensPositionViewProjectionComposeMatrixWordCount;
    state.LensPositionViewProjectionIdentityRowPatched = true;
    state.LensPositionScreenCenterXAddress = kLensPositionScreenCenterXAddress;
    state.LensPositionScreenCenterYAddress = kLensPositionScreenCenterYAddress;
    state.LensPositionBaseZAddress = kLensPositionBaseZAddress;
    state.LensPositionDirectionScaleAddress = kLensPositionDirectionScaleAddress;
    state.LensPositionDistanceLimitScaleAddress = kLensPositionDistanceLimitScaleAddress;
    state.LensPositionRuntimeScaleMultiplierAddress =
        kLensPositionRuntimeScaleMultiplierAddress;
    state.LensPositionRuntimeScaleArgument =
        kLensPositionPrimarySourceRuntimeScaleArgument;
    state.LensPositionProjectionScaleXAddress = kLensPositionProjectionScaleXAddress;
    state.LensPositionProjectionScaleYAddress = kLensPositionProjectionScaleYAddress;
    state.LensPositionProjectionBaseYAddress = kLensPositionProjectionBaseYAddress;
    state.LensPositionScreenCenterX = readCodeFloat(kLensPositionScreenCenterXAddress);
    state.LensPositionScreenCenterY = readCodeFloat(kLensPositionScreenCenterYAddress);
    state.LensPositionBaseZ = readCodeFloat(kLensPositionBaseZAddress);
    state.LensPositionDirectionScale = readCodeFloat(kLensPositionDirectionScaleAddress);
    state.LensPositionDistanceLimitScale =
        readCodeFloat(kLensPositionDistanceLimitScaleAddress);
    state.LensPositionRuntimeScaleMultiplier =
        readCodeFloat(kLensPositionRuntimeScaleMultiplierAddress);
    state.LensPositionProjectionScaleX = readCodeFloat(kLensPositionProjectionScaleXAddress);
    state.LensPositionProjectionScaleY = readCodeFloat(kLensPositionProjectionScaleYAddress);
    state.LensPositionProjectionBaseY = readCodeFloat(kLensPositionProjectionBaseYAddress);
    state.LensVisibilityMinStepAddress = kLensVisibilityMinStepAddress;
    state.LensVisibilityScaleAddress = kLensVisibilityScaleAddress;
    state.LensVisibilityVisibleTargetAddress = kLensVisibilityVisibleTargetAddress;
    state.LensVisibilityScreenMaxXAddress = kLensVisibilityScreenMaxXAddress;
    state.LensVisibilityMinStep = readCodeFloat(kLensVisibilityMinStepAddress);
    state.LensVisibilityMaxStep = state.LensVisibilityMinStep;
    state.LensVisibilityScale = readCodeFloat(kLensVisibilityScaleAddress);
    state.LensVisibilityVisibleTarget = readCodeFloat(kLensVisibilityVisibleTargetAddress);
    state.LensVisibilityScreenMaxX = readCodeFloat(kLensVisibilityScreenMaxXAddress);
    state.LensVisibilityScreenMaxY = state.LensPositionScreenCenterY * 2.0f;
    state.NativeLensVisibilityScreenGateResolved =
        state.LensVisibilityMinStep > 0.0f &&
        state.LensVisibilityScale > 0.0f &&
        state.LensVisibilityVisibleTarget > 0.0f &&
        state.LensVisibilityScreenMaxX == state.LensPositionScreenCenterX * 2.0f &&
        state.LensVisibilityScreenMaxY == state.LensPositionScreenCenterY * 2.0f;
    state.LensPositionPrimarySourceProducerFunctionAddress =
        kLensPositionPrimarySourceProducerFunctionAddress;
    state.LensPositionPrimarySourceProducerCallsiteAddress =
        kLensPositionPrimarySourceProducerCallsiteAddress;
    state.LensPositionPrimarySourceOffsetUpdateFunctionAddress =
        kLensPositionPrimarySourceOffsetUpdateFunctionAddress;
    state.LensPositionPrimarySourceBaseCameraXPlayOffset =
        kLensPositionPrimarySourceBaseCameraXPlayOffset;
    state.LensPositionPrimarySourceBaseCameraYPlayOffset =
        kLensPositionPrimarySourceBaseCameraYPlayOffset;
    state.LensPositionPrimarySourceBaseCameraZPlayOffset =
        kLensPositionPrimarySourceBaseCameraZPlayOffset;
    state.LensPositionPrimarySourceKankyoOffsetXPlayOffset =
        kLensPositionPrimarySourceKankyoOffsetXPlayOffset;
    state.LensPositionPrimarySourceKankyoOffsetYPlayOffset =
        kLensPositionPrimarySourceKankyoOffsetYPlayOffset;
    state.LensPositionPrimarySourceKankyoOffsetZPlayOffset =
        kLensPositionPrimarySourceKankyoOffsetZPlayOffset;
    state.LensPositionPrimarySourceActiveAngleStateAddress =
        kLensPositionPrimarySourceActiveAngleStateAddress;
    state.LensPositionPrimarySourceActiveAngleHalfwordOffset =
        kLensPositionPrimarySourceActiveAngleHalfwordOffset;
    state.LensPositionPrimarySourceScaleXAddress = kLensPositionPrimarySourceScaleXAddress;
    state.LensPositionPrimarySourceScaleYAddress = kLensPositionPrimarySourceScaleYAddress;
    state.LensPositionPrimarySourceScaleZAddress = kLensPositionPrimarySourceScaleZAddress;
    state.LensPositionPrimarySourceRadiusPointerAddress =
        kLensPositionPrimarySourceRadiusPointerAddress;
    state.LensPositionPrimarySourceScaleX = readCodeFloat(kLensPositionPrimarySourceScaleXAddress);
    state.LensPositionPrimarySourceScaleY = readCodeFloat(kLensPositionPrimarySourceScaleYAddress);
    state.LensPositionPrimarySourceScaleZ = readCodeFloat(kLensPositionPrimarySourceScaleZAddress);
    const uint32_t radiusSourceAddress =
        readCodeU32(kLensPositionPrimarySourceRadiusPointerAddress);
    state.LensPositionPrimarySourceRadiusValueAddress =
        radiusSourceAddress == 0 ? 0 : radiusSourceAddress + kLensPositionPrimarySourceRadiusValueOffset;
    state.LensPositionPrimarySourceRadius =
        state.LensPositionPrimarySourceRadiusValueAddress == 0
            ? 0.0f
            : readCodeFloat(state.LensPositionPrimarySourceRadiusValueAddress);
    state.NativeLensPositionProducerResolved =
        state.LensPositionScreenCenterX == 200.0f &&
        state.LensPositionScreenCenterY == 120.0f &&
        state.LensPositionBaseZ == 0.0f &&
        state.LensPositionDirectionScale == 2.0f &&
        state.LensPositionDistanceLimitScale > 0.0f &&
        state.LensPositionRuntimeScaleMultiplier > 0.0f &&
        state.LensPositionRuntimeScaleArgument == 0x0172 &&
        state.LensPositionProjectionScaleX == 200.0f &&
        state.LensPositionProjectionScaleY == -120.0f &&
        state.LensPositionProjectionBaseY == 120.0f;
    state.NativeLensPositionRuntimeScaleResolved =
        state.NativeLensPositionProducerResolved &&
        state.LensPositionRuntimeScaleMultiplier > 0.0f &&
        state.LensPositionRuntimeScaleArgument != 0;
    state.LensPositionViewProjectionSourceResolved =
        state.LensPositionGameplayDrawFunctionAddress == 0x002E25F0 &&
        state.LensPositionViewSourceProducerFunctionAddress == 0x00464B2C &&
        state.LensPositionViewSourceProducerCallsiteAddress == 0x002E2674 &&
        state.LensPositionViewPrepFunctionAddress == 0x002DE690 &&
        state.LensPositionViewPrepCallsiteAddress == 0x002E2688 &&
        state.LensPositionViewUpdateFunctionAddress == 0x00471BA4 &&
        state.LensPositionViewUpdateCallsiteAddress == 0x002E2694 &&
        state.LensPositionViewProjectionSourceCopyHelperAddress == 0x00324744 &&
        state.LensPositionViewProjectionSourceCopyCallsiteAddress == 0x002E26A8 &&
        state.LensPositionViewProjectionComposeCopyHelperAddress == 0x00372224 &&
        state.LensPositionViewProjectionComposeCopyCallsiteAddress == 0x002E26B4 &&
        state.LensPositionViewProjectionComposeHelperAddress == 0x002D9688 &&
        state.LensPositionViewProjectionComposeCallsiteAddress == 0x002E26E0 &&
        state.LensPositionViewStructPlayOffset == 0x0188 &&
        state.LensPositionViewProjectionSourceMatrixPlayOffset == 0x01DC &&
        state.LensPositionViewProjectionComposeMatrixPlayOffset == 0x029C &&
        state.LensPositionViewProjectionMatrixWordCount == 16 &&
        state.LensPositionViewProjectionComposeMatrixWordCount == 12 &&
        state.LensPositionViewProjectionIdentityRowPatched;
    state.NativeLensPositionRuntimeInputResolved = false;
    state.NativeLensPositionRuntimeInputStatus =
        state.LensPositionViewProjectionSourceResolved
            ? "native play+0x5bb4 view/projection source chain is resolved from Gameplay_Draw: "
              "play+0x1dc is copied to play+0x5bb4, play+0x29c is copied to the stack, "
              "the identity row is patched, and FUN_002d9688 composes the runtime matrix; "
              "host-side frame materialization of those native camera matrices is still pending"
            : "native lens position producer requires play+0x5bb4 view/projection matrix fields "
              "consumed by FUN_00368cc0 before exact runtime centers can be materialized";
    state.NativePrimitiveColorPass = 4;
    state.NativePrimitiveAlphaPass = 6;
    state.NativePrimitiveVertexCount = 9;
    state.OverlayGeometryTableAddress = kOverlayGeometryTableAddress;
    state.OverlayColorByteScale = readCodeFloat(kOverlayColorByteScaleAddress);
    state.OverlayBaseAlpha = readCodeFloat(kOverlayBaseAlphaAddress);
    state.OverlayDepthThreshold = readCodeFloat(kOverlayDepthThresholdAddress);
    state.OverlayDepthFadeScale = readCodeFloat(kOverlayDepthFadeScaleAddress);
    state.OverlayHelperCallbackMappingResolved =
        readCodeU32(kColorBytesOverlayHelperAssignmentLiteralAddress) ==
            kColorBytesOverlayHelperAddress &&
        readCodeU32(kPackedRgbaOverlayHelperAssignmentLiteralAddress) ==
            kPackedRgbaOverlayHelperAddress;

    const auto geometryTableOffset =
        NativeRenderRuntimeAddressToFileOffset(kOverlayGeometryTableAddress, codeBase, codeBin.size());
    if (geometryTableOffset.has_value() && NativeRenderCanRead(codeBin, *geometryTableOffset, 4)) {
        state.OverlayGeometryTableHeaderWord = NativeRenderReadLeU32(codeBin, *geometryTableOffset);
        for (uint32_t i = 0; i < kOverlayGeometryCoordinatePairCount; ++i) {
            const size_t pairOffset =
                *geometryTableOffset + sizeof(uint32_t) + static_cast<size_t>(i) * sizeof(float) * 2;
            if (!NativeRenderCanRead(codeBin, pairOffset, sizeof(float) * 2)) {
                break;
            }
            const float x = NativeRenderReadLeFloat(codeBin, pairOffset);
            const float y = NativeRenderReadLeFloat(codeBin, pairOffset + sizeof(float));
            state.OverlayGeometryTableValues.push_back(x);
            state.OverlayGeometryTableValues.push_back(y);
            state.OverlayGeometryCoordinatePairs.push_back({ i, x, y });
        }
    }

    const auto* lensTbd = NativeRenderFindKankyoCommonTbdState(background, "lensflare_info.tbd");
    if (lensTbd == nullptr) {
        state.SourceKind = "oot3d_kankyo_lens_effect_input";
        state.SourceStatus = "lensflare_info.tbd is not resolved from kankyo_common.zar";
        return state;
    }

    state.Available = true;
    state.LensTbdEntryName = lensTbd->EntryName;
    const auto* scaleMin = NativeRenderFindKankyoCommonTbdRecord(*lensTbd, "LensScaleMin");
    const auto* scaleMax = NativeRenderFindKankyoCommonTbdRecord(*lensTbd, "LensScaleMax");
    const auto* offset = NativeRenderFindKankyoCommonTbdRecord(*lensTbd, "LensOffset");
    const auto* depth = NativeRenderFindKankyoCommonTbdRecord(*lensTbd, "LensDepth");
    const auto* halation = NativeRenderFindKankyoCommonTbdRecord(*lensTbd, "LensHalationColor");
    const bool recordsResolved =
        lensTbd->RecordTableResolved &&
        scaleMin != nullptr && scaleMax != nullptr && offset != nullptr && depth != nullptr &&
        halation != nullptr &&
        scaleMin->ValueCountMatchesPayload && scaleMax->ValueCountMatchesPayload &&
        offset->ValueCountMatchesPayload && depth->ValueCountMatchesPayload &&
        !scaleMin->FloatValues.empty() && !scaleMax->FloatValues.empty() &&
        !offset->FloatValues.empty() && !depth->FloatValues.empty() &&
        !halation->Rgba8Values.empty();
    state.TbdRecordsResolved = recordsResolved;
    if (recordsResolved) {
        const size_t elementCount =
            std::min({ scaleMin->FloatValues.size(), scaleMax->FloatValues.size(),
                       offset->FloatValues.size(), depth->FloatValues.size() });
        state.ElementCount = static_cast<uint32_t>(
            std::min<size_t>(elementCount, std::numeric_limits<uint32_t>::max()));
        for (uint32_t i = 0; i < state.ElementCount; ++i) {
            state.Elements.push_back({
                i,
                scaleMin->FloatValues[i],
                scaleMax->FloatValues[i],
                offset->FloatValues[i],
                depth->FloatValues[i],
            });
        }
        for (const auto& color : halation->Rgba8Values) {
            state.HalationColors.push_back({ color.R, color.G, color.B, color.A });
        }
    }

    const bool hasSunTexture = NativeRenderFindKankyoExtraCtxbName(background, "cloud_sun", state.SunTextureName);
    const bool hasLensTexture =
        NativeRenderFindKankyoExtraCtxbName(background, "cloud_lensflare", state.LensflareTextureName);
    state.TextureInputsResolved = hasSunTexture && hasLensTexture;
    state.ReadyForBackendInput = state.TbdRecordsResolved && state.TextureInputsResolved;
    state.NativeRuntimeListResolved = lensRuntimeList.InitResolved && lensRuntimeList.DrawSlotsResolved &&
                                      lensRuntimeList.RuntimeVtableMethodsResolved;
    state.NativeBackendBufferHelpersResolved = lensRuntimeList.RuntimeBackendBufferHelpersResolved;
    state.NativeBackendInputPlanResolved =
        state.ReadyForBackendInput && state.NativeRuntimeListResolved &&
        state.NativeBackendBufferHelpersResolved &&
        state.LensRuntimeQuadBatchNativeVerticesPerQuad > 0 &&
        !state.HalationColors.empty();
    state.NativeVisibleBackendSubmitResolved = lensRuntimeList.BackendSubmitResolved;
    state.NativeSubmitMappingResolved = state.NativeVisibleBackendSubmitResolved;
    const Oot3dNativeRenderTexture* primitiveTexture =
        !state.LensflareTextureName.empty()
            ? NativeRenderFindKankyoExtraCtxbTexture(background, state.LensflareTextureName)
            : NativeRenderFindKankyoExtraCtxbTexture(background, state.SunTextureName);
    const Oot3dNativeRenderTexture* terminalTexture =
        NativeRenderFindKankyoExtraCtxbTexture(background, state.SunTextureName);
    state.PrimitiveBackendInput =
        NativeRenderBuildKankyoPrimitiveBackendInputState(
            state, bridgeContract, primitiveTexture, terminalTexture);
    state.NativeVisibleBackendSubmitResolved =
        state.PrimitiveBackendInput.VisibleBackendSubmitResolved;
    state.NativeSubmitMappingResolved =
        state.PrimitiveBackendInput.Runtime28CEnqueueDispatchResolved &&
        state.NativeVisibleBackendSubmitResolved;
    state.NativeBackendPlanSourceStatus =
        state.NativeBackendInputPlanResolved
            ? "native lensflare backend input plan resolved from code.bin runtime list: "
              "kankyo+0x0ec list, FUN_00484f5c/FUN_002c5314 draw route, "
              "FUN_002d5124 builder, FUN_002c517c submit queue, FUN_002c1ae8 submit record writer, "
              "quad-batch draw method FUN_003fc2f8, buffer helpers FUN_00333270/003331ec/00333070, "
              "and native lensflare_info.tbd/CTXB payloads"
            : "native lensflare backend input plan incomplete; verify runtime list contract, "
              "TBD records, and CTXB texture inputs";
    state.SourceKind = "oot3d_kankyo_common_lensflare_tbd_and_fogy_ctxb_inputs";
    state.SourceStatus =
        state.ReadyForBackendInput
            ? "native lensflare TBD records and Fogy sun/lens CTXB inputs are resolved; "
              "code.bin z_kankyo initializes lensflare_info.tbd through provider entry 0 at "
              "kankyo+0x26c/play+0x0ed0, and FUN_002d97e4 consumes Lens* records 0..4; "
              "FUN_002d5124 initializes the kankyo+0xec runtime list from native CTXB rows, and "
              "FUN_00484f5c/FUN_002c5314 consume slots +0x28/+0x2c for the 13 lens elements; "
              "runtime factories FUN_0034897c/FUN_00340d00 resolve base/quad-batch draw vtables; "
              "FUN_003fc2f8 reaches native quad-batch buffer helpers FUN_00333270/003331ec/00333070 "
              "and vertex-count setter FUN_00333294; "
              "backend input planning and the OpenGL visible submit are resolved from these payloads; "
              "the fullscreen flash/overlay callback remains a separate environmental-effect route"
            : "native lensflare effect input is incomplete; verify TBD records and Fogy sun/lens CTXB names";
    return state;
}

bool NativeRenderUsesResolvedRuntimeEnvironmentFogColor(
    const Oot3dNativePicaLightingRenderState& lighting) {
    return lighting.ResolvedRuntimeLightSetting.Available &&
           lighting.ResolvedRuntimeLightSetting.UsedForRender &&
           lighting.ResolvedRuntimeLightSetting.RuntimeFinalFogColorUsedForRender;
}

ColorRgba8 NativeRenderResolvedEnvironmentFogColor(
    const Oot3dNativePicaLightingRenderState& lighting) {
    if (lighting.ActorVsLightPacket.PicaFogColorAvailable) {
        return lighting.ActorVsLightPacket.PicaFogColor;
    }
    if (NativeRenderUsesResolvedRuntimeEnvironmentFogColor(lighting)) {
        return lighting.ResolvedRuntimeLightSetting.FogColor;
    }
    return { 0, 0, 0, 255 };
}

std::string NativeRenderResolvedEnvironmentFogColorSource(
    const Oot3dNativePicaLightingRenderState& lighting) {
    if (lighting.ActorVsLightPacket.PicaFogColorAvailable) {
        return lighting.ActorVsLightPacket.PicaFogColorSource;
    }
    if (NativeRenderUsesResolvedRuntimeEnvironmentFogColor(lighting)) {
        const auto& resolved = lighting.ResolvedRuntimeLightSetting;
        std::ostringstream source;
        if (resolved.RuntimeFogColorAddendResolved) {
            source << "oot3d_runtime_final_fog_color_play_"
                   << HexU32(resolved.FinalFogColorPlayOffset, 4)
                   << "_from_state_preaddend_"
                   << HexU32(resolved.FogPreAddendStateOffset, 2)
                   << "_plus_s16_addend_state_"
                   << HexU32(resolved.FogColorAddendStateOffset, 2);
        } else {
            source << "oot3d_runtime_fog_preaddend_record_offset_"
                   << HexU32(resolved.FogPreAddendRecordOffset, 2)
                   << "_to_state_"
                   << HexU32(resolved.FogPreAddendStateOffset, 2)
                   << "_pending_s16_addend_state_"
                   << HexU32(resolved.FogColorAddendStateOffset, 2)
                   << "_final_play_"
                   << HexU32(resolved.FinalFogColorPlayOffset, 4);
        }
        if (!resolved.Branch.empty()) {
            source << "_" << resolved.Branch;
        }
        return source.str();
    }
    if (lighting.ResolvedRuntimeLightSetting.Available) {
        std::ostringstream source;
        source << "oot3d_runtime_light_settings_rgb_u8_offset_"
               << HexU32(NativeZsiLightSettingsRecordLayout().RuntimeFogColorOffset, 2)
               << "_diagnostic";
        return source.str();
    }
    return {};
}

Oot3dNativePicaFogState BuildOot3dNativePicaFogState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeFogRuntimeDefaults* runtimeDefaults) {
    constexpr uint32_t kSourceColorScaleROffset = 0x24;
    constexpr uint32_t kSourceColorScaleGOffset = 0x28;
    constexpr uint32_t kSourceColorScaleBOffset = 0x2C;
    constexpr uint32_t kSourceNearS32Offset = 0x34;
    constexpr uint32_t kSourceFarS32Offset = 0x38;
    constexpr uint32_t kSourceRebuildGateOffset = 0x41;
    constexpr uint32_t kSourceCurveModeOffset = 0x42;

    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    const auto& emit = bridgeContract.MaterialScalarEmit;
    const auto& lightLayout = bridgeContract.ZsiLightSettingsRecord;
    const uint32_t codeBase = lightLayout.RuntimeTransitionTableCodeBase;

    Oot3dNativePicaFogState fog;
    fog.ColorRegister = kPicaRegFogColor;
    fog.LutIndexRegister = kPicaRegFogLutIndex;
    fog.LutDataRegisterBase = kPicaRegFogLutData0;
    fog.ModeRaw = kPicaFogModeFog;
    fog.Mode = kPicaFogModeFog;
    fog.FogPayloadRuntimeUpdateAddress = emit.FogPayloadRuntimeUpdateAddress;
    fog.FogPayloadBuildAddress = emit.FogPayloadBuildAddress;
    fog.FogPayloadDefaultSourceAddress = emit.FogPayloadDefaultSourceAddress;
    fog.FogPayloadSourceFloat0Offset = emit.FogPayloadSourceFloat0Offset;
    fog.FogPayloadSourceFloat1Offset = emit.FogPayloadSourceFloat1Offset;
    fog.FogPayloadPackedTableOffset = emit.FogPayloadPackedTableOffset;
    fog.FogPayloadPackedTableEntryCount = emit.FogPayloadPackedTableEntryCount;
    fog.CameraFarPlayOffset = lightLayout.RuntimeCameraFarPlayOffset;
    fog.FogFarPlayOffset = lightLayout.RuntimeFogFarPlayOffset;
    fog.FogNearPlayOffset = lightLayout.RuntimeFogNearPlayOffset;
    fog.ProjectionMatrixPlayOffset = lightLayout.RuntimeProjectionMatrixPlayOffset;
    fog.ViewInitAddress = lightLayout.RuntimeViewInitAddress;
    fog.ViewDefaultNearLiteralAddress = lightLayout.RuntimeViewDefaultNearLiteralAddress;
    fog.ViewDefaultFarLiteralAddress = lightLayout.RuntimeViewDefaultFarLiteralAddress;
    fog.ViewUpdateAddress = lightLayout.RuntimeViewUpdateAddress;
    fog.ProjectionBuildAddress = lightLayout.RuntimeProjectionBuildAddress;
    fog.SceneProjectionFarLiteralAddress =
        lightLayout.RuntimeSceneProjectionFarLiteralAddress;
    fog.SceneProjectionMatrixOffset = lightLayout.RuntimeSceneProjectionMatrixOffset;
    fog.SourceKind =
        "oot3d_code_bin_fog_res_updater_material_scalar_pica_fog";
    fog.LutSource =
        "decomp_support z_fog_material_scalar.c imports code.bin FUN_002d4554 view-term copy, "
        "FUN_0047fd44 template apply, FUN_002cdbfc curve builder, and FUN_004c062c packed "
        "PICA Fog::LutEntry value/difference table";

    if (!NativeRenderUsesResolvedRuntimeEnvironmentFogColor(lighting) &&
        !lighting.ActorVsLightPacket.PicaFogColorAvailable) {
        fog.BlockedReason = "native_pica_actor_packet_fog_color_not_resolved";
        return fog;
    }

    fog.Color = NativeRenderResolvedEnvironmentFogColor(lighting);
    fog.ColorSource = NativeRenderResolvedEnvironmentFogColorSource(lighting);
    if (NativeRenderUsesResolvedRuntimeEnvironmentFogColor(lighting)) {
        const auto& resolved = lighting.ResolvedRuntimeLightSetting;
        fog.PreAddendColor = resolved.FogPreAddendColor;
        fog.ColorAddendI16 = resolved.FogColorAddendI16;
        fog.FinalColor = resolved.FinalFogColor;
        fog.RuntimeFinalFogColorFormulaResolved = resolved.RuntimeFinalFogColorFormulaResolved;
        fog.RuntimeFogColorAddendResolved = resolved.RuntimeFogColorAddendResolved;
        fog.RuntimeFinalFogColorUsedForRender = resolved.RuntimeFinalFogColorUsedForRender;
        fog.PreAddendRecordOffset = resolved.FogPreAddendRecordOffset;
        fog.PreAddendStateOffset = resolved.FogPreAddendStateOffset;
        fog.ColorAddendStateOffset = resolved.FogColorAddendStateOffset;
        fog.FinalOutputOffset = resolved.FinalFogColorOutputOffset;
        fog.FinalPlayOffset = resolved.FinalFogColorPlayOffset;
        fog.RuntimeFogDistanceContractResolved = resolved.RuntimeFogDistanceContractResolved;
        fog.RuntimeFogDistanceAddendsResolved = resolved.RuntimeFogDistanceAddendsResolved;
        fog.RuntimeFogDistancesUsedForRender = resolved.RuntimeFogDistancesUsedForRender;
    } else {
        fog.PreAddendColor = fog.Color;
        fog.FinalColor = fog.Color;
    }
    fog.FogEnabled = true;
    fog.FogFlip = false;
    fog.ShaderSupported = true;
    fog.LutShaderSupported = true;

    if (runtimeDefaults != nullptr && runtimeDefaults->Available) {
        fog.SourceRgbScaleR = runtimeDefaults->SourceRgbScaleR;
        fog.SourceRgbScaleG = runtimeDefaults->SourceRgbScaleG;
        fog.SourceRgbScaleB = runtimeDefaults->SourceRgbScaleB;
        fog.SourceNear = runtimeDefaults->SourceNear;
        fog.SourceFar = runtimeDefaults->SourceFar;
        fog.SourceRebuildGate = runtimeDefaults->SourceRebuildGate;
        fog.SourceCurveMode = runtimeDefaults->SourceCurveMode;
        fog.ProjectionNear = runtimeDefaults->ProjectionNear;
        fog.ProjectionFar = runtimeDefaults->ProjectionFar;
        fog.SceneProjectionFar = runtimeDefaults->SceneProjectionFar;
        fog.SceneProjectionFarDecoded = std::isfinite(fog.SceneProjectionFar) &&
                                        fog.SceneProjectionFar > fog.ProjectionNear;
        if (fog.SceneProjectionFarDecoded) {
            fog.ProjectionFar = fog.SceneProjectionFar;
        }
        fog.CodeBinSourceDecoded = true;
        fog.SourceKind = runtimeDefaults->SourceKind;
    } else {
        const auto codeBinPath = NativeRenderResolveCodeBinPath(scene);
        if (codeBinPath.empty()) {
            fog.BlockedReason = "code_bin_path_not_resolved_for_native_fog_source";
            return fog;
        }

        const auto& code = NativeRenderReadBinaryFile(codeBinPath);
        if (code.empty()) {
            fog.BlockedReason = "code_bin_could_not_be_read_for_native_fog_source";
            return fog;
        }

    const auto sourceOffset =
        NativeRenderRuntimeAddressToFileOffset(emit.FogPayloadDefaultSourceAddress, codeBase, code.size());
    if (!sourceOffset.has_value()) {
        fog.BlockedReason = "fog_payload_default_source_address_outside_code_bin";
        return fog;
    }
    if (!NativeRenderCanRead(code, *sourceOffset + kSourceCurveModeOffset, 1)) {
        fog.BlockedReason = "fog_payload_default_source_structure_truncated";
        return fog;
    }

    fog.SourceRgbScaleR = NativeRenderReadLeFloat(code, *sourceOffset + kSourceColorScaleROffset);
    fog.SourceRgbScaleG = NativeRenderReadLeFloat(code, *sourceOffset + kSourceColorScaleGOffset);
    fog.SourceRgbScaleB = NativeRenderReadLeFloat(code, *sourceOffset + kSourceColorScaleBOffset);
    fog.SourceNear = static_cast<float>(
        static_cast<int32_t>(NativeRenderReadLeU32(code, *sourceOffset + kSourceNearS32Offset)));
    fog.SourceFar = static_cast<float>(
        static_cast<int32_t>(NativeRenderReadLeU32(code, *sourceOffset + kSourceFarS32Offset)));
    fog.SourceRebuildGate = NativeRenderReadU8(code, *sourceOffset + kSourceRebuildGateOffset);
    fog.SourceCurveMode = NativeRenderReadU8(code, *sourceOffset + kSourceCurveModeOffset);
    const auto projectionNearOffset = NativeRenderRuntimeAddressToFileOffset(
        lightLayout.RuntimeViewDefaultNearLiteralAddress, codeBase, code.size());
    const auto projectionFarOffset = NativeRenderRuntimeAddressToFileOffset(
        lightLayout.RuntimeViewDefaultFarLiteralAddress, codeBase, code.size());
    const auto sceneProjectionFarOffset = NativeRenderRuntimeAddressToFileOffset(
        lightLayout.RuntimeSceneProjectionFarLiteralAddress, codeBase, code.size());
    if (projectionNearOffset.has_value() && NativeRenderCanRead(code, *projectionNearOffset, 4)) {
        fog.ProjectionNear = NativeRenderReadLeFloat(code, *projectionNearOffset);
    }
    if (projectionFarOffset.has_value() && NativeRenderCanRead(code, *projectionFarOffset, 4)) {
        fog.ProjectionFar = NativeRenderReadLeFloat(code, *projectionFarOffset);
    }
    if (sceneProjectionFarOffset.has_value() &&
        NativeRenderCanRead(code, *sceneProjectionFarOffset, 4)) {
        fog.SceneProjectionFar = NativeRenderReadLeFloat(code, *sceneProjectionFarOffset);
        fog.SceneProjectionFarDecoded =
            std::isfinite(fog.SceneProjectionFar) && fog.SceneProjectionFar > fog.ProjectionNear;
        if (fog.SceneProjectionFarDecoded) {
            fog.ProjectionFar = fog.SceneProjectionFar;
        }
    }
        fog.CodeBinSourceDecoded = true;
    }
    if (NativeRenderUsesResolvedRuntimeEnvironmentFogColor(lighting)) {
        const auto& resolved = lighting.ResolvedRuntimeLightSetting;
        if (resolved.RuntimeFogDistancesUsedForRender) {
            fog.SourceNear = static_cast<float>(resolved.FogNear);
            fog.SourceFar = static_cast<float>(resolved.FogFar);
            fog.RuntimeFogDistancesUsedForRender = true;
        }
        if (resolved.RuntimeCameraFarUsedForRender) {
            fog.RuntimeCameraFar = static_cast<float>(resolved.CameraFar);
            fog.ProjectionFar = fog.RuntimeCameraFar;
        }
    }
    if (fog.SceneProjectionFarDecoded) {
        fog.ProjectionFar = fog.SceneProjectionFar;
    }
    fog.ProjectionRangeAvailable =
        std::isfinite(fog.ProjectionNear) && std::isfinite(fog.ProjectionFar) &&
        fog.ProjectionNear > 0.0f && fog.ProjectionFar > fog.ProjectionNear;
    fog.Available = fog.SourceFar > fog.SourceNear && fog.ProjectionRangeAvailable;
    fog.SourceStatus =
        fog.RuntimeFogDistancesUsedForRender
            ? "decoded runtime camera/fog distances from native ZSI 0x1c light-setting records via "
              "0045dd50, View_Init projection near, and FUN_00471ba4 scene-matrix far literal "
              "from code.bin; backend passes the active "
              "projection matrix through imported 002d4554/002cdbfc/004c062c to build the "
              "128-entry PICA fog LUT"
            : "decoded FogResUpdater default source and View_Init projection range from code.bin; "
              "backend uses the imported 002d4554/002cdbfc/004c062c fog scalar path";
    if (!fog.Available) {
        fog.BlockedReason = !fog.ProjectionRangeAvailable
                                ? "native_view_projection_near_far_invalid"
                                : "native_runtime_fog_near_far_invalid";
    }
    return fog;
}

Oot3dNativeEnvironmentBackgroundState BuildOot3dNativeEnvironmentBackgroundState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativePicaLightingRenderState& lighting,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    constexpr int kSkyboxSettingsCommandId = 0x11;
    constexpr int kSpecialFilesCommandId = 0x07;
    constexpr uint32_t kCodeBinSkyboxRecordTablePointerAddress = 0x002E4DC8;
    constexpr uint32_t kCodeBinKankyoScheduleTablePointerAddress = 0x002E4DDC;
    constexpr uint32_t kCodeBinKankyoClockStatePointerAddress = 0x002E4DD4;
    constexpr uint32_t kCodeBinKankyoClockHalfwordOffset = 0xA8;
    constexpr uint32_t kCodeBinSkyboxRecordSizeBytes = 0x50;
    constexpr uint32_t kCodeBinSkyboxRecordRomPathMaxBytes = 0x40;
    constexpr uint32_t kCodeBinSkyboxRecordParam44Offset = 0x44;
    constexpr uint32_t kCodeBinSkyboxRecordParam48Offset = 0x48;
    constexpr uint32_t kCodeBinSkyboxRecordParam4COffset = 0x4C;
    constexpr uint32_t kCodeBinKankyoScheduleModeStrideBytes = 0x48;
    constexpr uint32_t kCodeBinKankyoScheduleEntrySizeBytes = 0x08;
    constexpr uint32_t kCodeBinKankyoScheduleEntryCount = 9;
    constexpr uint32_t kCodeBinKankyoDrawScaleAddress = 0x0047D1D0;
    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    const uint32_t codeBase = bridgeContract.ZsiLightSettingsRecord.RuntimeTransitionTableCodeBase;

    Oot3dNativeEnvironmentBackgroundState background;
    background.ActiveSetupIndex = scene.NativePicaLighting.ActiveSetupIndex;
    Oot3dNativeDemoAssetGraph targetSceneAssetGraph;
    const auto* backgroundSceneCommands = &scene.AssetGraph.SceneCommands;
    if (lighting.ResolvedRuntimeLightSetting.DirectTargetRecordBranchApplied &&
        !lighting.ResolvedRuntimeLightSetting.DirectTargetSceneMatchesActiveScene &&
        !lighting.ResolvedRuntimeLightSetting.DirectTargetSceneZsiPath.empty() &&
        std::filesystem::is_regular_file(lighting.ResolvedRuntimeLightSetting.DirectTargetSceneZsiPath) &&
        lighting.ResolvedRuntimeLightSetting.TargetSetupIndex >= 0) {
        targetSceneAssetGraph = DecodeOot3dNativeDemoAssetGraphForRuntime(
            lighting.ResolvedRuntimeLightSetting.DirectTargetSceneZsiPath, {}, scene.ManifestPath, {},
            lighting.ResolvedRuntimeLightSetting.TargetSetupIndex);
        if (!targetSceneAssetGraph.SceneCommands.empty()) {
            backgroundSceneCommands = &targetSceneAssetGraph.SceneCommands;
            background.ActiveSetupIndex = lighting.ResolvedRuntimeLightSetting.TargetSetupIndex;
        }
    }
    if (runtimeEnvironment != nullptr && runtimeEnvironment->TimeResolved) {
        background.RuntimeEnvironmentTimeInputAvailable = true;
        background.RuntimeEnvironmentDayTime = runtimeEnvironment->DayTime;
        background.RuntimeEnvironmentSkyboxTime = runtimeEnvironment->SkyboxTime;
        background.RuntimeEnvironmentTimeStartFrame = runtimeEnvironment->TimeStartFrame;
        background.RuntimeEnvironmentTimeSourceKind = runtimeEnvironment->SourceKind;
        background.RuntimeEnvironmentTimeSourceStatus = runtimeEnvironment->SourceStatus;
    }

    if (const auto* skyboxCommand =
            FindSceneCommandForSetup(*backgroundSceneCommands, background.ActiveSetupIndex, kSkyboxSettingsCommandId)) {
        background.SkyboxCommandOffset = skyboxCommand->Offset;
        background.SkyboxCommandArgument = static_cast<int>(skyboxCommand->Argument);
        background.SkyboxCommandParameter = skyboxCommand->Parameter;
    }
    if (const auto* specialFilesCommand =
            FindSceneCommandForSetup(*backgroundSceneCommands, background.ActiveSetupIndex, kSpecialFilesCommandId)) {
        background.SpecialFilesCommandOffset = specialFilesCommand->Offset;
        background.SpecialFilesCommandArgument = static_cast<int>(specialFilesCommand->Argument);
        background.SpecialFilesCommandParameter = specialFilesCommand->Parameter;
    }

    const int skyboxId = background.SkyboxCommandArgument >= 0 ? (background.SkyboxCommandArgument & 0xFF) : -1;
    const bool runtimeEnvironmentStateResolved =
        lighting.ResolvedRuntimeLightSetting.Available &&
        lighting.ResolvedRuntimeLightSetting.UsedForRender &&
        lighting.ResolvedRuntimeLightSetting.TransitionTableBranchApplied;

    if (skyboxId > 0 && lighting.ResolvedRuntimeLightSetting.Available) {
        background.Available = true;
        background.SourceKind = "oot3d_scene_skybox_native_kankyo_archive";
        background.ClearColor = NativeRenderResolvedEnvironmentFogColor(lighting);

        const auto codeBinPath = NativeRenderResolveCodeBinPath(scene);
        const auto& codeBin = NativeRenderReadBinaryFile(codeBinPath);
        const auto readRuntimeU32 = [&](uint32_t runtimeAddress) -> std::optional<uint32_t> {
            const auto offset =
                NativeRenderRuntimeAddressToFileOffset(runtimeAddress, codeBase, codeBin.size());
            if (!offset.has_value() || !NativeRenderCanRead(codeBin, *offset, 4)) {
                return std::nullopt;
            }
            return NativeRenderReadLeU32(codeBin, *offset);
        };

        const auto recordTableAddress = readRuntimeU32(kCodeBinSkyboxRecordTablePointerAddress);
        const auto scheduleTableAddress = readRuntimeU32(kCodeBinKankyoScheduleTablePointerAddress);
        const auto clockStateAddress = readRuntimeU32(kCodeBinKankyoClockStatePointerAddress);
        const auto drawScaleOffset =
            NativeRenderRuntimeAddressToFileOffset(kCodeBinKankyoDrawScaleAddress, codeBase, codeBin.size());
        background.NativeKankyoDrawScaleAddress = kCodeBinKankyoDrawScaleAddress;
        if (drawScaleOffset.has_value() && NativeRenderCanRead(codeBin, *drawScaleOffset, 4)) {
            const float drawScale = NativeRenderReadLeFloat(codeBin, *drawScaleOffset);
            if (std::isfinite(drawScale) && drawScale > 0.0f) {
                background.NativeKankyoDrawScale = drawScale;
            }
        }
        if (recordTableAddress.has_value() && scheduleTableAddress.has_value() &&
            clockStateAddress.has_value()) {
            background.NativeKankyoSkyboxRecordTableAddress = *recordTableAddress;
            background.NativeKankyoScheduleTableAddress = *scheduleTableAddress;
            background.NativeKankyoClockStateAddress = *clockStateAddress;
            background.NativeKankyoClockHalfwordOffset = kCodeBinKankyoClockHalfwordOffset;
            background.NativeKankyoSkyboxRecordAddress =
                *recordTableAddress + static_cast<uint32_t>(skyboxId) * kCodeBinSkyboxRecordSizeBytes;

            const auto recordOffset = NativeRenderRuntimeAddressToFileOffset(
                background.NativeKankyoSkyboxRecordAddress, codeBase, codeBin.size());
            if (recordOffset.has_value() &&
                NativeRenderCanRead(codeBin, *recordOffset, kCodeBinSkyboxRecordSizeBytes)) {
                background.NativeKankyoRomPath =
                    NativeRenderReadCString(codeBin, *recordOffset, kCodeBinSkyboxRecordRomPathMaxBytes);
                background.NativeKankyoRecordParam44 =
                    static_cast<int>(NativeRenderReadLeU32(
                        codeBin, *recordOffset + kCodeBinSkyboxRecordParam44Offset));
                background.NativeKankyoRecordParam48 =
                    static_cast<int>(NativeRenderReadLeU32(
                        codeBin, *recordOffset + kCodeBinSkyboxRecordParam48Offset));
                background.NativeKankyoRecordParam4C =
                    static_cast<int>(NativeRenderReadLeU32(
                        codeBin, *recordOffset + kCodeBinSkyboxRecordParam4COffset));
                background.NativeKankyoArchivePath =
                    NativeRenderResolveRomPath(scene, background.NativeKankyoRomPath);
                background.NativeKankyoArchiveAvailable =
                    std::filesystem::is_regular_file(background.NativeKankyoArchivePath);

                const int scheduleMode =
                    lighting.ResolvedRuntimeLightSetting.CurrentMode >= 0
                        ? lighting.ResolvedRuntimeLightSetting.CurrentMode
                        : scene.NativePicaLighting.NativeRuntimeTransitionGlobalFallbackMode;
                background.NativeKankyoScheduleMode = scheduleMode;
                const bool useRuntimeSkyboxTime =
                    runtimeEnvironment != nullptr && runtimeEnvironment->TimeResolved;
                const int activeAngle =
                    useRuntimeSkyboxTime
                        ? static_cast<int>(runtimeEnvironment->SkyboxTime)
                        : (lighting.ResolvedRuntimeLightSetting.ActiveAngle >= 0
                               ? (lighting.ResolvedRuntimeLightSetting.ActiveAngle & 0xFFFF)
                               : 0);
                background.NativeKankyoScheduleActiveAngle = activeAngle;
                background.NativeKankyoScheduleActiveAngleSource =
                    useRuntimeSkyboxTime
                        ? "oot3d_cutscene_settime_skybox_time_playstate_input"
                        : lighting.ResolvedRuntimeLightSetting.ActiveAngleSource;
                if (scheduleMode >= 0) {
                    const uint32_t modeTableAddress =
                        *scheduleTableAddress +
                        static_cast<uint32_t>(scheduleMode) * kCodeBinKankyoScheduleModeStrideBytes;
                    for (uint32_t entryIndex = 0; entryIndex < kCodeBinKankyoScheduleEntryCount; ++entryIndex) {
                        const uint32_t entryAddress =
                            modeTableAddress + entryIndex * kCodeBinKankyoScheduleEntrySizeBytes;
                        const auto entryOffset =
                            NativeRenderRuntimeAddressToFileOffset(entryAddress, codeBase, codeBin.size());
                        if (!entryOffset.has_value() ||
                            !NativeRenderCanRead(codeBin, *entryOffset, kCodeBinKankyoScheduleEntrySizeBytes)) {
                            continue;
                        }
                        const int startAngle = NativeRenderReadLeU16(codeBin, *entryOffset + 0x00);
                        const int endAngle = NativeRenderReadLeU16(codeBin, *entryOffset + 0x02);
                        if (startAngle <= activeAngle &&
                            (activeAngle < endAngle || endAngle == 0xFFFF)) {
                            const int blendFlag = codeBin[*entryOffset + 0x04];
                            background.NativeKankyoScheduleEntryIndex =
                                static_cast<int>(entryIndex);
                            background.NativeKankyoCurrentProfileIndex =
                                static_cast<int>(codeBin[*entryOffset + 0x05]);
                            background.NativeKankyoNextProfileIndex =
                                static_cast<int>(codeBin[*entryOffset + 0x06]);
                            background.NativeKankyoBlendAlpha = 0;
                            if (blendFlag != 0 && endAngle != startAngle) {
                                const double weight =
                                    Clamp01(1.0 -
                                            static_cast<double>(endAngle - activeAngle) /
                                                static_cast<double>(endAngle - startAngle));
                                background.NativeKankyoBlendAlpha =
                                    std::clamp(static_cast<int>(std::round(weight * 255.0)), 0, 255);
                            }
                            break;
                        }
                    }
                }

                const auto kankyoCmbSelection = NativeRenderSelectKankyoCmbNames(
                    background.NativeKankyoArchivePath,
                    background.NativeKankyoCurrentProfileIndex,
                    background.NativeKankyoNextProfileIndex,
                    background.NativeKankyoRecordParam44,
                    background.NativeKankyoRecordParam48,
                    background.NativeKankyoRecordParam4C);
                background.NativeKankyoSelectedCmbNames = kankyoCmbSelection.Names;
                background.NativeKankyoSelectedCmbProfileIndices =
                    kankyoCmbSelection.ProfileIndices;
                background.NativeKankyoSelectedCmbTypeLocalIndices =
                    kankyoCmbSelection.TypeLocalIndices;
                background.NativeKankyoSelectedCmbLayerIndices =
                    kankyoCmbSelection.LayerIndices;
                background.NativeKankyoSelectedCmabNames =
                    NativeRenderSelectKankyoCmabNames(background.NativeKankyoArchivePath);
                const auto extraCmbs = NativeRenderSelectKankyoExtraCmbNames(
                    background.NativeKankyoArchivePath,
                    background.NativeKankyoSelectedCmbNames,
                    skyboxId,
                    background.NativeKankyoCurrentProfileIndex);
                background.NativeKankyoExtraDrawCmbTypeLocalIndices = extraCmbs.TypeLocalIndices;
                background.NativeKankyoExtraCmbNames = extraCmbs.Names;
                const auto extraCtxbs =
                    NativeRenderSelectKankyoExtraCtxbs(background.NativeKankyoArchivePath);
                background.NativeKankyoExtraDrawCtxbTypeLocalIndices =
                    extraCtxbs.TypeLocalIndices;
                background.NativeKankyoExtraCtxbNames = extraCtxbs.Names;
                background.NativeKankyoExtraCtxbDescriptorSlots =
                    extraCtxbs.DescriptorSlots;
                background.NativeKankyoExtraCtxbTextures =
                    extraCtxbs.RenderTextures;
                background.NativeKankyoExtraDrawRouteDecoded =
                    !background.NativeKankyoExtraCmbNames.empty() ||
                    !background.NativeKankyoExtraCtxbNames.empty();
                background.NativeKankyoExtraDrawCount =
                    static_cast<uint32_t>(std::min<size_t>(
                        background.NativeKankyoExtraCmbNames.size(),
                        std::numeric_limits<uint32_t>::max()));
                NativeRenderResolveKankyoRuntimeCtxbStates(scene, background, bridgeContract);
                NativeRenderResolveKankyoCommonTbdStates(scene, background, bridgeContract);
                background.NativeKankyoMoon =
                    NativeRenderResolveKankyoMoonState(background);
                background.NativeKankyoSunHalo =
                    NativeRenderResolveKankyoSunHaloState(background, codeBin, codeBase);
                background.NativeKankyoLensEffect =
                    NativeRenderResolveKankyoLensEffectState(background, codeBin, codeBase);
                background.NativeKankyoDrawRouteDecoded =
                    background.NativeKankyoArchiveAvailable &&
                    background.NativeKankyoScheduleEntryIndex >= 0 &&
                    !background.NativeKankyoSelectedCmbNames.empty();
            }
        }

        if (background.NativeKankyoDrawRouteDecoded) {
            std::ostringstream status;
            status << "skybox_settings id 0x" << std::hex << std::nouppercase
                   << std::setw(2) << std::setfill('0') << skyboxId << std::dec
                   << " enters the native Gameplay_Draw kankyo route: code.bin record table "
                   << HexU32(background.NativeKankyoSkyboxRecordTableAddress, 8)
                   << " selects " << background.NativeKankyoRomPath
                   << ", schedule table "
                   << HexU32(background.NativeKankyoScheduleTableAddress, 8)
                   << " selects profile "
                   << background.NativeKankyoCurrentProfileIndex
                   << " with native record profile_count/layer_count/core_cmb_count "
                   << background.NativeKankyoRecordParam44 << "/"
                   << background.NativeKankyoRecordParam48 << "/"
                   << background.NativeKankyoRecordParam4C
                   << ", and the selected native CMB background models are queued for render; "
                   << "CMAB material animation is applied when present, and native CTXB/TBD "
                   << "support payloads remain exposed as diagnostics for later effect passes";
            background.SourceStatus =
                status.str();
        } else if (runtimeEnvironmentStateResolved) {
            std::ostringstream status;
            status << "skybox_settings id 0x" << std::hex << std::nouppercase
                   << std::setw(2) << std::setfill('0') << skyboxId << std::dec
                   << " has native kankyo table evidence";
            if (!background.NativeKankyoRomPath.empty()) {
                status << " for " << background.NativeKankyoRomPath;
            }
            status << ", but the archive/profile CMB selection is not fully resolved; preserving "
                   << "the resolved runtime environment color only as diagnostics";
            background.SourceStatus =
                status.str();
        } else {
            std::ostringstream status;
            status << "skybox_settings id 0x" << std::hex << std::nouppercase
                   << std::setw(2) << std::setfill('0') << skyboxId << std::dec
                   << " has a native kankyo route candidate, but the runtime environment "
                   << "mode/angle selector is not resolved yet";
            background.SourceStatus =
                status.str();
        }
        return background;
    }

    if (lighting.ResolvedRuntimeLightSetting.Available) {
        background.Available = true;
        background.UsedForRender = runtimeEnvironmentStateResolved;
        background.ClearColor = NativeRenderResolvedEnvironmentFogColor(lighting);
        background.SourceKind = "oot3d_runtime_environment_fog_color_clear";
        if (runtimeEnvironmentStateResolved) {
            background.SourceStatus =
                "clear color from the resolved code.bin runtime environment fog color; skybox/kankyo "
                "draw selection remains pending for this skybox id";
        } else {
            background.SourceStatus =
                "candidate clear color from the code.bin runtime environment fog color is preserved for "
                "diagnostics, but not applied until the native runtime mode/angle selector is resolved";
        }
    }
    return background;
}

bool BlendOot3dNativeKankyoProfileRenderModels(
    Oot3dNativeRenderModel& base, const Oot3dNativeRenderModel& current,
    const Oot3dNativeRenderModel& next,
    int32_t currentProfileIndex, int32_t nextProfileIndex, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (base.Batches.size() != current.Batches.size() ||
        base.Batches.size() != next.Batches.size()) {
        return false;
    }
    for (size_t batchIndex = 0; batchIndex < base.Batches.size(); ++batchIndex) {
        const auto& baseBatch = base.Batches[batchIndex];
        const auto& currentBatch = current.Batches[batchIndex];
        const auto& nextBatch = next.Batches[batchIndex];
        if (baseBatch.MeshIndex != currentBatch.MeshIndex ||
            baseBatch.ShapeIndex != currentBatch.ShapeIndex ||
            baseBatch.PrimitiveIndex != currentBatch.PrimitiveIndex ||
            currentBatch.MeshIndex != nextBatch.MeshIndex ||
            currentBatch.ShapeIndex != nextBatch.ShapeIndex ||
            currentBatch.PrimitiveIndex != nextBatch.PrimitiveIndex ||
            baseBatch.Vertices.size() != currentBatch.Vertices.size() ||
            currentBatch.Vertices.size() != nextBatch.Vertices.size()) {
            return false;
        }
        for (size_t vertexIndex = 0; vertexIndex < baseBatch.Vertices.size(); ++vertexIndex) {
            const auto& baseVertex = baseBatch.Vertices[vertexIndex];
            const auto& currentVertex = currentBatch.Vertices[vertexIndex];
            const auto& nextVertex = nextBatch.Vertices[vertexIndex];
            if (baseVertex.NativeSourceVertexIndexAvailable !=
                    currentVertex.NativeSourceVertexIndexAvailable ||
                baseVertex.NativeSourceVertexIndexAvailable !=
                    nextVertex.NativeSourceVertexIndexAvailable ||
                (baseVertex.NativeSourceVertexIndexAvailable &&
                 (baseVertex.NativeSourceVertexIndex != currentVertex.NativeSourceVertexIndex ||
                  baseVertex.NativeSourceVertexIndex != nextVertex.NativeSourceVertexIndex))) {
                return false;
            }
        }
    }

    const auto blendByte = [weight](uint8_t currentValue, uint8_t nextValue) {
        const double value = static_cast<double>(currentValue) +
                             (static_cast<double>(nextValue) - currentValue) * weight;
        return static_cast<uint8_t>(std::clamp(static_cast<int>(value), 0, 255));
    };
    for (size_t batchIndex = 0; batchIndex < base.Batches.size(); ++batchIndex) {
        auto& baseBatch = base.Batches[batchIndex];
        const auto& currentBatch = current.Batches[batchIndex];
        const auto& nextBatch = next.Batches[batchIndex];
        for (size_t vertexIndex = 0; vertexIndex < baseBatch.Vertices.size(); ++vertexIndex) {
            auto& baseVertex = baseBatch.Vertices[vertexIndex];
            const auto& currentVertex = currentBatch.Vertices[vertexIndex];
            const auto& nextVertex = nextBatch.Vertices[vertexIndex];
            baseVertex.Color = {
                blendByte(currentVertex.Color.R, nextVertex.Color.R),
                blendByte(currentVertex.Color.G, nextVertex.Color.G),
                blendByte(currentVertex.Color.B, nextVertex.Color.B),
                blendByte(currentVertex.Color.A, nextVertex.Color.A),
            };
            baseVertex.NativeColorAvailable =
                currentVertex.NativeColorAvailable && nextVertex.NativeColorAvailable;
        }
    }
    base.NativeKankyoProfileAttributeBlendApplied = true;
    base.NativeKankyoProfileBlendCurrentIndex = currentProfileIndex;
    base.NativeKankyoProfileBlendNextIndex = nextProfileIndex;
    base.NativeKankyoProfileBlendWeight = weight;
    base.NativeKankyoProfileAttributeBlendSource =
        "oot3d_code_bin_FUN_002d56e0_group_base_cmb_FUN_002d5468_FUN_002d4ad4_dynamic_color_buffer";
    return true;
}

uint8_t ResolveOot3dNativeKankyoSpecialChildAlpha(
    int currentSelector, int nextSelector, int blendAlpha) {
    if (currentSelector < 0 || nextSelector < 0) {
        return 255;
    }
    if ((currentSelector >> 2) != (nextSelector >> 2)) {
        return (nextSelector & 3) == 3 ? 255 : 0;
    }

    const int alpha = std::clamp(blendAlpha, 0, 255);
    const bool currentVisible = (currentSelector & 3) == 3;
    const bool nextVisible = (nextSelector & 3) == 3;
    if (currentVisible && nextVisible) {
        return 255;
    }
    if (currentVisible) {
        return static_cast<uint8_t>(255 - alpha);
    }
    if (nextVisible) {
        return static_cast<uint8_t>(alpha);
    }
    return 0;
}

uint8_t ResolveOot3dNativeKankyoSecondaryCloudAlpha(
    uint32_t typeLocalIndex, int currentSelector, int nextSelector, int blendAlpha) {
    int layerProfileGroup = -1;
    if (typeLocalIndex == 0x23) {
        layerProfileGroup = 0;
    } else if (typeLocalIndex == 0x24) {
        layerProfileGroup = 2;
    } else {
        return 255;
    }

    if (currentSelector < 0 || nextSelector < 0 || blendAlpha < 0) {
        return 255;
    }

    const int currentProfileGroup = currentSelector >> 2;
    const int nextProfileGroup = nextSelector >> 2;
    if (currentProfileGroup == nextProfileGroup || (nextSelector & 3) != 3) {
        return 0;
    }

    // FUN_002d5468 stores the secondary-cloud enable scalar at +0x1F4. FUN_002d9664
    // multiplies it by the current/next profile-group weights when the groups differ.
    const int alpha = std::clamp(blendAlpha, 0, 255);
    if (layerProfileGroup == currentProfileGroup) {
        return static_cast<uint8_t>(255 - alpha);
    }
    if (layerProfileGroup == nextProfileGroup) {
        return static_cast<uint8_t>(alpha);
    }
    return 0;
}

std::vector<Oot3dNativeRenderModel> BuildOot3dNativeEnvironmentModels(
    Oot3dNativeEnvironmentBackgroundState& background,
    float materialAnimationFrame) {
    struct KankyoMaterialAnimation {
        std::string Name;
        CmabMaterialAnimation Animation;
    };
    struct KankyoCmbLayer {
        std::string Name;
        int ProfileIndex = -1;
        uint32_t TypeLocalIndex = 0;
        int LayerIndex = -1;
        Oot3dNativeKankyoModelRole Role = Oot3dNativeKankyoModelRole::None;
    };
    struct KankyoCachedCmbModel {
        CmbModel Model;
        Oot3dNativeRenderModel BaseRenderModel;
        bool Loaded = false;
    };

    std::vector<Oot3dNativeRenderModel> models;
    background.NativeKankyoModelLoadErrors.clear();
    if (!background.NativeKankyoDrawRouteDecoded ||
        !std::filesystem::is_regular_file(background.NativeKankyoArchivePath)) {
        return models;
    }
    static std::map<std::string, CmabMaterialAnimation> materialAnimationCache;
    static std::map<std::string, KankyoCachedCmbModel> cmbModelCache;
    std::vector<KankyoMaterialAnimation> materialAnimations;
    for (const auto& cmabName : background.NativeKankyoSelectedCmabNames) {
        try {
            const std::string cacheKey = background.NativeKankyoArchivePath.string() + "!" + cmabName;
            auto cacheIt = materialAnimationCache.find(cacheKey);
            if (cacheIt == materialAnimationCache.end()) {
                const auto bytes = NativeRenderReadZarFile(background.NativeKankyoArchivePath, cmabName);
                cacheIt =
                    materialAnimationCache
                        .emplace(cacheKey,
                                 ParseCmabMaterialAnimationBytes(
                                     bytes, background.NativeKankyoArchivePath.string() + "!" + cmabName))
                        .first;
            }
            materialAnimations.push_back({
                cmabName,
                cacheIt->second,
            });
        } catch (const std::exception&) {
        }
    }
    background.NativeKankyoMaterialAnimationCount =
        static_cast<uint32_t>(std::min<size_t>(materialAnimations.size(),
                                               std::numeric_limits<uint32_t>::max()));
    background.NativeKankyoMaterialAnimationFrame = materialAnimationFrame;

    std::vector<KankyoCmbLayer> cmbLayers;
    for (size_t i = 0; i < background.NativeKankyoSelectedCmbNames.size(); ++i) {
        const int layerIndex =
            i < background.NativeKankyoSelectedCmbLayerIndices.size()
                ? background.NativeKankyoSelectedCmbLayerIndices[i]
                : -1;
        cmbLayers.push_back({
            background.NativeKankyoSelectedCmbNames[i],
            i < background.NativeKankyoSelectedCmbProfileIndices.size()
                ? background.NativeKankyoSelectedCmbProfileIndices[i]
                : -1,
            i < background.NativeKankyoSelectedCmbTypeLocalIndices.size()
                ? background.NativeKankyoSelectedCmbTypeLocalIndices[i]
                : 0,
            layerIndex,
            layerIndex == 0
                ? Oot3dNativeKankyoModelRole::SkyBackground
                : (layerIndex == 1
                       ? Oot3dNativeKankyoModelRole::Cloud
                       : Oot3dNativeKankyoModelRole::None),
        });
    }
    for (size_t i = 0; i < background.NativeKankyoExtraCmbNames.size(); ++i) {
        const auto& extraCmbName = background.NativeKankyoExtraCmbNames[i];
        const auto layerIt =
            std::find_if(cmbLayers.begin(), cmbLayers.end(), [&](const KankyoCmbLayer& layer) {
                return layer.Name == extraCmbName;
            });
        if (layerIt == cmbLayers.end()) {
            const std::string stem = NativeRenderArchiveStem(extraCmbName);
            const auto role = stem.find("star") != std::string::npos
                                  ? Oot3dNativeKankyoModelRole::Star
                                  : (stem.find("kumo") != std::string::npos
                                         ? Oot3dNativeKankyoModelRole::Cloud
                                         : Oot3dNativeKankyoModelRole::Sun);
            const uint32_t typeLocalIndex =
                i < background.NativeKankyoExtraDrawCmbTypeLocalIndices.size()
                    ? background.NativeKankyoExtraDrawCmbTypeLocalIndices[i]
                    : 0;
            cmbLayers.push_back({
                extraCmbName, -1, typeLocalIndex, -1, role,
            });
        }
    }

    const bool sameRuntimeProfileGroup =
        background.NativeKankyoCurrentProfileIndex >= 0 &&
        background.NativeKankyoNextProfileIndex >= 0 &&
        background.NativeKankyoCurrentProfileIndex != background.NativeKankyoNextProfileIndex &&
        (background.NativeKankyoCurrentProfileIndex >> 2) ==
            (background.NativeKankyoNextProfileIndex >> 2);

    const auto resolveLayerAlpha = [&](const KankyoCmbLayer& layer) -> int {
        if (layer.Role == Oot3dNativeKankyoModelRole::Star) {
            return ResolveOot3dNativeKankyoSpecialChildAlpha(
                background.NativeKankyoCurrentProfileIndex,
                background.NativeKankyoNextProfileIndex,
                background.NativeKankyoBlendAlpha);
        }
        if (layer.TypeLocalIndex == 0x23 || layer.TypeLocalIndex == 0x24) {
            return ResolveOot3dNativeKankyoSecondaryCloudAlpha(
                layer.TypeLocalIndex,
                background.NativeKankyoCurrentProfileIndex,
                background.NativeKankyoNextProfileIndex,
                background.NativeKankyoBlendAlpha);
        }
        const int profileIndex = layer.ProfileIndex;
        if (sameRuntimeProfileGroup) {
            return 255;
        }
        if (profileIndex < 0 ||
            background.NativeKankyoCurrentProfileIndex < 0 ||
            background.NativeKankyoNextProfileIndex < 0 ||
            background.NativeKankyoBlendAlpha < 0 ||
            background.NativeKankyoCurrentProfileIndex == background.NativeKankyoNextProfileIndex) {
            return 255;
        }
        const int blendAlpha = std::clamp(background.NativeKankyoBlendAlpha, 0, 255);
        if (profileIndex == background.NativeKankyoCurrentProfileIndex) {
            return 255;
        }
        if (profileIndex == background.NativeKankyoNextProfileIndex) {
            return blendAlpha;
        }
        return 255;
    };

    const auto layerBlendSource = [&]() {
        std::ostringstream source;
        source << "oot3d_code_bin_kankyo_schedule_profile_overlay_blend"
               << ":mode=" << background.NativeKankyoScheduleMode
               << ":entry=" << background.NativeKankyoScheduleEntryIndex
               << ":current_profile=" << background.NativeKankyoCurrentProfileIndex
               << ":next_profile=" << background.NativeKankyoNextProfileIndex
               << ":blend_alpha=" << background.NativeKankyoBlendAlpha;
        return source.str();
    }();

    for (const auto& layer : cmbLayers) {
        const auto& cmbName = layer.Name;
        const int layerAlpha = resolveLayerAlpha(layer);
        if (layerAlpha <= 0) {
            continue;
        }
        try {
            std::ostringstream cacheKey;
            cacheKey << background.NativeKankyoArchivePath.string() << "!" << cmbName
                     << ";draw_scale=" << background.NativeKankyoDrawScale;
            auto& cachedModel = cmbModelCache[cacheKey.str()];
            if (!cachedModel.Loaded) {
                const auto bytes = NativeRenderReadZarFile(background.NativeKankyoArchivePath, cmbName);
                cachedModel.Model = ParseCmbModelBytes(
                    bytes, background.NativeKankyoArchivePath.string() + "!" + cmbName);
                cachedModel.BaseRenderModel = BuildOot3dNativeRenderModel(
                    cachedModel.Model, { {}, background.NativeKankyoDrawScale, false });
                cachedModel.Loaded = !cachedModel.BaseRenderModel.Batches.empty();
            }
            if (!cachedModel.Loaded) {
                continue;
            }
            auto renderModel = cachedModel.BaseRenderModel;
            renderModel.NativeKankyoProfileIndex = layer.ProfileIndex;
            renderModel.NativeKankyoRole = layer.Role;
            renderModel.NativeKankyoLayerAlpha = layerAlpha;
            const bool specialChildAlphaApplied =
                layer.Role == Oot3dNativeKankyoModelRole::Star &&
                background.NativeKankyoCurrentProfileIndex >= 0 &&
                background.NativeKankyoNextProfileIndex >= 0;
            renderModel.NativeKankyoLayerBlendAlphaApplied =
                specialChildAlphaApplied ||
                (!sameRuntimeProfileGroup &&
                 layer.ProfileIndex >= 0 &&
                 background.NativeKankyoCurrentProfileIndex != background.NativeKankyoNextProfileIndex &&
                 background.NativeKankyoBlendAlpha >= 0);
            renderModel.NativeKankyoLayerBlendAlphaSource =
                specialChildAlphaApplied
                    ? "oot3d_code_bin_FUN_002d5468_special_child_selector_alpha"
                    : (renderModel.NativeKankyoLayerBlendAlphaApplied ? layerBlendSource : std::string{});
            if (renderModel.NativeKankyoLayerBlendAlphaApplied && layerAlpha < 255) {
                const uint8_t layerAlphaU8 = static_cast<uint8_t>(std::clamp(layerAlpha, 0, 255));
                for (auto& batch : renderModel.Batches) {
                    batch.Material.NativeRuntimeVertexAlphaBlend = true;
                    batch.Material.NativeKankyoLayerBlendAlphaApplied = true;
                    batch.Material.NativeKankyoLayerBlendAlpha = layerAlphaU8;
                    batch.Material.NativeKankyoLayerBlendAlphaSource = layerBlendSource;
                    batch.Material.NativeBlendStateEnabled = true;
                    batch.Material.NativeBlendStateSupported = true;
                    batch.Material.NativeBlendFactorsSupported = true;
                    batch.Material.NativeBlendEquationSupported = true;
                    for (auto& vertex : batch.Vertices) {
                        const int alpha =
                            static_cast<int>(std::lround(
                                static_cast<double>(vertex.Color.A) *
                                static_cast<double>(layerAlphaU8) / 255.0));
                        vertex.Color.A = static_cast<uint8_t>(std::clamp(alpha, 0, 255));
                    }
                }
            }
            if (!materialAnimations.empty()) {
                std::vector<CmabMaterialAnimation> applicableMaterialAnimations;
                for (const auto& materialAnimation : materialAnimations) {
                    if (Oot3dNativeKankyoCmabAppliesToCmb(materialAnimation.Name, cmbName)) {
                        applicableMaterialAnimations.push_back(materialAnimation.Animation);
                    }
                }
                if (!applicableMaterialAnimations.empty()) {
                    const size_t applied = ApplyOot3dNativeRenderModelMaterialAnimationFrame(
                        renderModel, cachedModel.Model, applicableMaterialAnimations, materialAnimationFrame);
                    background.NativeKankyoMaterialAnimationAppliedBatchCount +=
                        static_cast<uint32_t>(std::min<size_t>(
                            applied,
                            static_cast<size_t>(std::numeric_limits<uint32_t>::max() -
                                                background.NativeKankyoMaterialAnimationAppliedBatchCount)));
                }
            }
            renderModel.Name = "kankyo:" + renderModel.Name;
            renderModel.Source = background.NativeKankyoArchivePath.string() + "!" + cmbName;
            renderModel.Bounds = Oot3dNativeRenderModelWorldBounds(renderModel);
            models.push_back(std::move(renderModel));
        } catch (const std::exception& exception) {
            background.NativeKankyoModelLoadErrors.push_back(
                cmbName + ": " + exception.what());
        }
    }

    if (sameRuntimeProfileGroup) {
        const float weight = static_cast<float>(
            std::clamp(background.NativeKankyoBlendAlpha, 0, 255)) / 255.0f;
        const int baseProfileIndex =
            (background.NativeKankyoCurrentProfileIndex >> 2) << 2;
        for (const auto role : {
                 Oot3dNativeKankyoModelRole::SkyBackground,
                 Oot3dNativeKankyoModelRole::Cloud,
             }) {
            const auto baseIt = std::find_if(models.begin(), models.end(), [&](const auto& model) {
                return model.NativeKankyoRole == role &&
                       model.NativeKankyoProfileIndex == baseProfileIndex;
            });
            const auto currentIt = std::find_if(models.begin(), models.end(), [&](const auto& model) {
                return model.NativeKankyoRole == role &&
                       model.NativeKankyoProfileIndex ==
                           background.NativeKankyoCurrentProfileIndex;
            });
            const auto nextIt = std::find_if(models.begin(), models.end(), [&](const auto& model) {
                return model.NativeKankyoRole == role &&
                       model.NativeKankyoProfileIndex ==
                           background.NativeKankyoNextProfileIndex;
            });
            if (baseIt == models.end() || currentIt == models.end() || nextIt == models.end()) {
                continue;
            }
            if (BlendOot3dNativeKankyoProfileRenderModels(
                    *baseIt, *currentIt, *nextIt,
                    background.NativeKankyoCurrentProfileIndex,
                    background.NativeKankyoNextProfileIndex, weight)) {
                models.erase(
                    std::remove_if(models.begin(), models.end(), [&](const auto& model) {
                        return model.NativeKankyoRole == role &&
                               model.NativeKankyoProfileIndex >= 0 &&
                               model.NativeKankyoProfileIndex != baseProfileIndex;
                    }),
                    models.end());
            }
        }
    }
    return models;
}

void ApplyNativePicaFogRuntimeListBinding(Oot3dNativeRenderModel& model) {
    for (auto& batch : model.Batches) {
        if (!batch.Material.NativeMaterialAvailable ||
            !batch.Material.NativeRuntimeMaterialLaneDecoded) {
            continue;
        }
        batch.Material.NativePicaFogOverrideDecoded = true;
        batch.Material.NativePicaFogEnabled = true;
        batch.Material.NativePicaFogOverrideSource =
            "oot3d_Gameplay_Draw_002e25f0_002d960c_play_0x4c30_0x500c_runtime_material_list_binding";
    }
}

Oot3dNativeDemoRenderScene BuildOot3dNativeDemoRenderScene(const Oot3dNativeDemoScene& scene) {
    return BuildOot3dNativeDemoRenderScene(scene, scene.LinkStandingPose, scene.LinkSkinTransforms);
}

Oot3dNativeDemoRenderScene BuildOot3dNativeDemoRenderScene(const Oot3dNativeDemoScene& scene,
                                                           const CsabPose& linkPose,
                                                           const std::vector<Matrix4f>& linkSkinTransforms,
                                                           float materialAnimationFrame,
                                                           const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
                                                           bool includeGameplayLink,
                                                           Oot3dNativeActorVisualSelection actorVisualSelection) {
    Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = BuildOot3dNativeRenderModel(scene.RoomModel);
    ApplyNativePicaFogRuntimeListBinding(renderScene.Room);
    NativeDemoExpandBoundsByBounds(renderScene.Bounds, Oot3dNativeRenderModelWorldBounds(renderScene.Room));
    if (includeGameplayLink) {
        renderScene.Link =
            BuildOot3dNativeDemoLinkRenderModel(scene, linkPose, linkSkinTransforms, materialAnimationFrame);
        NativeDemoExpandBoundsByBounds(renderScene.Bounds, Oot3dNativeRenderModelWorldBounds(renderScene.Link));
    }
    renderScene.ActorVisuals.reserve(scene.ActorVisualInstances.size());
    for (const auto& instance : scene.ActorVisualInstances) {
        if (actorVisualSelection == Oot3dNativeActorVisualSelection::NativeBehaviorResolved &&
            !instance.NativeVisualBehaviorResolved) {
            continue;
        }
        if (instance.ModelIndex >= scene.ActorVisualModels.size()) {
            continue;
        }
        const auto& visualModel = scene.ActorVisualModels[instance.ModelIndex];
        auto renderModel = BuildOot3dNativeRenderModel(visualModel.Model, { {}, 1.0, false });
        renderModel.Name = instance.ActorName + "#" + std::to_string(instance.ActorEntryIndex) + ":" + renderModel.Name;
        renderModel.Source = instance.ArchivePath.string() + "!" + instance.CmbName;
        auto rotation = instance.Rotation;
        rotation.Y += static_cast<double>(instance.RotationYStepS16PerTick) *
                      static_cast<double>(materialAnimationFrame);
        renderModel.ModelToWorld =
            BuildOot3dNativeRenderActorEntryTransform(instance.Scale, rotation, instance.Position);
        renderModel.Bounds = Oot3dNativeRenderModelWorldBounds(renderModel);
        NativeDemoExpandBoundsByBounds(renderScene.Bounds, renderModel.Bounds);
        renderScene.ActorVisuals.push_back(std::move(renderModel));
    }
    if (includeGameplayLink) {
        renderScene.LinkActorShadow = BuildOot3dNativeInitialLinkActorShadowState(scene);
    }
    RefreshOot3dNativeDemoRenderSceneRuntimeEnvironment(
        scene, renderScene, runtimeEnvironment, materialAnimationFrame);
    return renderScene;
}

Oot3dNativePicaLightingSemanticPlan CompileOot3dNativePicaLightingSemanticPlan(
    const nlohmann::json& semantics) {
    Oot3dNativePicaLightingSemanticPlan plan;
    if (!semantics.is_object()) {
        return plan;
    }
    plan.Format = JsonStringValue(semantics, "format");
    plan.SourceKind = JsonStringValue(semantics, "source_kind");
    plan.Layout = JsonStringValue(semantics, "layout");
    plan.RecordSize = JsonIntValue(semantics, "record_size", -1);
    const auto* engineModes = JsonObjectChild(semantics, "engine_modes");
    if (engineModes == nullptr) {
        return plan;
    }
    const auto* mode = JsonObjectChild(*engineModes, plan.Template.Mode.c_str());
    if (mode == nullptr) {
        return plan;
    }

    auto& lighting = plan.Template;
    lighting.SemanticTableAvailable = true;
    lighting.SourceKind = plan.SourceKind;
    lighting.RecordSelector = JsonStringValue(*mode, "record_selector");
    lighting.RecordSelectorSource = JsonStringValue(*mode, "record_selector_source");
    lighting.AmbientGroupIndex = JsonIntValue(*mode, "ambient_group_index", -1);
    lighting.DiffuseGroupIndex = JsonIntValue(*mode, "diffuse_group_index", -1);
    lighting.AmbientColorSource = JsonStringValue(*mode, "ambient_color_source");
    lighting.DiffuseColorSource = JsonStringValue(*mode, "diffuse_color_source");
    lighting.Light1ColorSource = JsonStringValue(*mode, "light1_color_source");
    lighting.AmbientColorOffset = JsonIntValue(*mode, "ambient_color_offset", -1);
    lighting.DiffuseColorOffset = JsonIntValue(*mode, "diffuse_color_offset", -1);
    lighting.Light1ColorOffset = JsonIntValue(*mode, "light1_color_offset", -1);
    lighting.VertexColorFormula = JsonStringValue(*mode, "vertex_color_formula");
    lighting.DirectionalFormula = JsonStringValue(*mode, "directional_formula");
    lighting.TextureCombiner = JsonStringValue(*mode, "texture_combiner");
    lighting.TexturedBaseColorSource = JsonStringValue(*mode, "textured_base_color_source");
    lighting.MaterialLightingEnableSource = JsonStringValue(*mode, "material_lighting_enable_source");
    lighting.MaterialLightingDeferredSource = JsonStringValue(*mode, "material_lighting_deferred_source");
    lighting.MaterialVertexHemisphereModelScope =
        JsonStringValue(*mode, "material_vertex_hemisphere_model_scope");
    lighting.MaterialVertexHemisphereLightingSource =
        JsonStringValue(*mode, "material_vertex_hemisphere_lighting_source");
    lighting.VertexHemisphereLightingFormula =
        JsonStringValue(*mode, "vertex_hemisphere_lighting_formula");
    lighting.VertexHemisphereLightingReference =
        JsonStringValue(*mode, "vertex_hemisphere_lighting_reference");
    lighting.VertexHemisphereLightColorMode =
        JsonStringValue(*mode, "vertex_hemisphere_light_color_mode");
    lighting.MaterialEmissionSource = JsonStringValue(*mode, "material_emission_source");
    lighting.MaterialAmbientSource = JsonStringValue(*mode, "material_ambient_source");
    lighting.MaterialDiffuseSource = JsonStringValue(*mode, "material_diffuse_source");
    lighting.Light0VectorGroupIndex = JsonIntValue(*mode, "light0_vector_group_index", -1);
    lighting.Light0VectorSource = JsonStringValue(*mode, "light0_vector_source");
    lighting.Light0VectorOffset = JsonIntValue(*mode, "light0_vector_offset", -1);
    lighting.Light1VectorGroupIndex = JsonIntValue(*mode, "light1_vector_group_index", -1);
    lighting.Light1VectorSource = JsonStringValue(*mode, "light1_vector_source");
    lighting.Light1VectorOffset = JsonIntValue(*mode, "light1_vector_offset", -1);
    lighting.ModulateTexturedBatches = JsonBoolValue(*mode, "modulate_textured_batches", true);
    lighting.PreserveVertexAlpha = JsonBoolValue(*mode, "preserve_vertex_alpha", true);
    plan.RuntimeTransitionActiveAngle = JsonIntValue(*mode, "runtime_transition_active_angle", -1);
    plan.RuntimeTransitionCurrentMode = JsonIntValue(*mode, "runtime_transition_current_mode", -1);
    plan.RuntimeTransitionTargetMode = JsonIntValue(*mode, "runtime_transition_target_mode", -1);
    plan.RuntimeTransitionModeBlendActive =
        JsonBoolValue(*mode, "runtime_transition_mode_blend_active", false);
    plan.RuntimeTransitionModeBlendWeight =
        Clamp01(JsonDoubleValue(*mode, "runtime_transition_mode_blend_weight", 0.0));
    plan.VertexHemisphereUniformTraceSource =
        JsonStringValue(*mode, "vertex_hemisphere_uniform_trace_source");
    plan.Available = plan.Format == "oot3d_pica_lighting_semantics_v1" &&
                     !plan.SourceKind.empty() && !plan.Layout.empty() && plan.RecordSize > 0 &&
                     !lighting.RecordSelector.empty() && !lighting.VertexColorFormula.empty();
    return plan;
}

Oot3dNativePicaLightingRenderState BuildOot3dNativePicaLightingRenderState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    return BuildOot3dNativePicaLightingRenderState(
        scene, CompileOot3dNativePicaLightingSemanticPlan(scene.NativePicaLightingSemantics),
        runtimeEnvironment);
}

Oot3dNativePicaLightingRenderState BuildOot3dNativePicaLightingRenderState(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativePicaLightingSemanticPlan& semanticPlan,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    Oot3dNativePicaLightingRenderState lighting = semanticPlan.Template;
    lighting.CmbVShaderLightingAccumulatorDecoded =
        NativeCmbVShaderHasLightingAccumulator(scene);
    if (lighting.CmbVShaderLightingAccumulatorDecoded) {
        lighting.CmbVShaderLightingAccumulatorSlotCount = 3;
        lighting.CmbVShaderLightingAccumulatorSource =
            "oot3d_CmbVShader_shbin_instructions_0x130_0x1e0";
    }
    lighting.SemanticTableAvailable = semanticPlan.Available;
    lighting.SourceKind = semanticPlan.SourceKind;
    lighting.ActiveSetupIndex = scene.NativePicaLighting.ActiveSetupIndex;
    if (runtimeEnvironment != nullptr && runtimeEnvironment->TimeResolved) {
        lighting.RuntimeEnvironmentTimeInputAvailable = true;
        lighting.RuntimeEnvironmentDayTime = runtimeEnvironment->DayTime;
        lighting.RuntimeEnvironmentSkyboxTime = runtimeEnvironment->SkyboxTime;
        lighting.RuntimeEnvironmentTimeStartFrame = runtimeEnvironment->TimeStartFrame;
        lighting.RuntimeEnvironmentTimeSourceKind = runtimeEnvironment->SourceKind;
        lighting.RuntimeEnvironmentTimeSourceStatus = runtimeEnvironment->SourceStatus;
    }
    if (runtimeEnvironment != nullptr && runtimeEnvironment->LightModeResolved) {
        lighting.RuntimeEnvironmentLightModeInputAvailable = true;
        lighting.RuntimeEnvironmentLightModeCurrent = runtimeEnvironment->LightModeCurrent;
        lighting.RuntimeEnvironmentLightModeTarget = runtimeEnvironment->LightModeTarget;
        lighting.RuntimeEnvironmentLightModeBlendActive = runtimeEnvironment->LightModeBlendActive;
        lighting.RuntimeEnvironmentLightModeBlendRemaining =
            runtimeEnvironment->LightModeBlendRemaining;
        lighting.RuntimeEnvironmentLightModeBlendDuration =
            runtimeEnvironment->LightModeBlendDuration;
        lighting.RuntimeEnvironmentLightModeBlendWeight =
            runtimeEnvironment->LightModeBlendWeight;
        lighting.RuntimeEnvironmentLightModeStartFrame = runtimeEnvironment->LightModeStartFrame;
        lighting.RuntimeEnvironmentLightModeSourceActionId =
            runtimeEnvironment->LightModeSourceActionId;
        lighting.RuntimeEnvironmentLightModeSourceKind = runtimeEnvironment->LightModeSourceKind;
        lighting.RuntimeEnvironmentLightModeSourceStatus =
            runtimeEnvironment->LightModeSourceStatus;
    }
    if (runtimeEnvironment != nullptr && runtimeEnvironment->LightSettingResolved) {
        lighting.RuntimeEnvironmentLightSettingInputAvailable = true;
        lighting.RuntimeEnvironmentLightSettingRawIndex = runtimeEnvironment->LightSettingRawIndex;
        lighting.RuntimeEnvironmentLightSettingTarget = runtimeEnvironment->LightSettingTarget;
        lighting.RuntimeEnvironmentLightSettingSetupIndex = runtimeEnvironment->LightSettingSetupIndex;
        lighting.RuntimeEnvironmentLightSettingStartFrame = runtimeEnvironment->LightSettingStartFrame;
        lighting.RuntimeEnvironmentLightSettingPlayTargetOffset =
            runtimeEnvironment->LightSettingPlayTargetOffset;
        lighting.RuntimeEnvironmentLightSettingPlayBlendWeightOffset =
            runtimeEnvironment->LightSettingPlayBlendWeightOffset;
        lighting.RuntimeEnvironmentLightSettingScenePath = runtimeEnvironment->LightSettingScenePath;
        lighting.RuntimeEnvironmentLightSettingSourceKind = runtimeEnvironment->LightSettingSourceKind;
        lighting.RuntimeEnvironmentLightSettingSourceStatus =
            runtimeEnvironment->LightSettingSourceStatus;
    }

    if (!scene.NativePicaLighting.Available || !semanticPlan.Available) {
        return lighting;
    }

    if (semanticPlan.Layout != scene.NativePicaLighting.SelectedLightSettingsLayout) {
        return lighting;
    }
    const int tableRecordSize = semanticPlan.RecordSize;
    if (tableRecordSize <= 0) {
        return lighting;
    }
    lighting.FloorPolygonIndex = scene.PlayerStart.FloorPolygonIndex;
    lighting.FloorSurfaceType = scene.PlayerStart.FloorSurfaceType;
    lighting.FloorLightSettingRawIndex = scene.PlayerStart.FloorLightSettingRawIndex;
    lighting.FloorLightSettingIndex = scene.PlayerStart.FloorLightSettingIndex;
    DecodeVertexHemisphereUniformTrace(
        scene, semanticPlan.VertexHemisphereUniformTraceSource, lighting);

    const bool usesPlayerFloorRecordSelector =
        lighting.RecordSelector == "active_setup_record_from_player_floor_light_setting_index";
    const bool hasRuntimeEnvironmentRecordSelector =
        runtimeEnvironment != nullptr &&
        (runtimeEnvironment->TimeResolved || runtimeEnvironment->LightModeResolved ||
         runtimeEnvironment->LightSettingResolved);
    if (usesPlayerFloorRecordSelector) {
        if (lighting.RecordSelectorSource !=
            "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting") {
            return lighting;
        }
        if (lighting.FloorLightSettingIndex < 0 && !hasRuntimeEnvironmentRecordSelector) {
            return lighting;
        }
    }

    const auto* record =
        SelectPicaLightingRecord(scene.NativePicaLighting, lighting.RecordSelector,
                                 lighting.FloorLightSettingIndex);
    if (record == nullptr && usesPlayerFloorRecordSelector && hasRuntimeEnvironmentRecordSelector) {
        record = SelectPicaLightingRecord(
            scene.NativePicaLighting, "first_active_setup_record", -1);
        if (record != nullptr) {
            lighting.RecordSelector =
                "active_setup_runtime_environment_record_when_player_floor_unavailable";
            lighting.RecordSelectorSource =
                "code_bin_0045dd50_runtime_mode_angle_or_direct_light_setting_scene_state";
        }
    }
    if (record == nullptr || record->EntrySize != tableRecordSize) {
        return lighting;
    }

    lighting.RecordIndex = record->Index;
    lighting.RecordOffset = record->Offset;
    lighting.ResolvedRuntimeLightSetting =
        BuildResolvedRuntimeLightSetting(
            scene, semanticPlan.RuntimeTransitionActiveAngle,
            semanticPlan.RuntimeTransitionCurrentMode,
            semanticPlan.RuntimeTransitionTargetMode,
            semanticPlan.RuntimeTransitionModeBlendActive,
            semanticPlan.RuntimeTransitionModeBlendWeight, *record, runtimeEnvironment);
    lighting.ActorVsLightPacket = BuildActorVsLightPacketStateFromRecord(
        scene.NativePicaLighting, *record, lighting.ResolvedRuntimeLightSetting);
    if (!lighting.AmbientColorSource.empty() || !lighting.DiffuseColorSource.empty() ||
        !lighting.Light1ColorSource.empty()) {
        if (!IsSupportedLightSettingsColorSource(lighting.AmbientColorSource) ||
            !IsSupportedLightSettingsColorSource(lighting.DiffuseColorSource) ||
            !CanReadRecordBytes(*record, lighting.AmbientColorOffset, 3) ||
            !CanReadRecordBytes(*record, lighting.DiffuseColorOffset, 3)) {
            return lighting;
        }
        if (!lighting.Light1ColorSource.empty() &&
            (!IsSupportedLightSettingsColorSource(lighting.Light1ColorSource) ||
             !CanReadRecordBytes(*record, lighting.Light1ColorOffset, 3))) {
            return lighting;
        }
        const bool useResolvedRuntimeLightSetting =
            lighting.ResolvedRuntimeLightSetting.Available &&
            lighting.AmbientColorSource == "oot3d_runtime_light_settings_rgb_u8" &&
            lighting.DiffuseColorSource == "oot3d_runtime_light_settings_rgb_u8" &&
            (lighting.Light1ColorSource.empty() ||
             lighting.Light1ColorSource == "oot3d_runtime_light_settings_rgb_u8") &&
            lighting.AmbientColorOffset == static_cast<int>(NativeZsiLightSettingsRecordLayout().RuntimeAmbientColorOffset) &&
            lighting.DiffuseColorOffset == static_cast<int>(NativeZsiLightSettingsRecordLayout().RuntimeLight0ColorOffset) &&
            (lighting.Light1ColorSource.empty() ||
             lighting.Light1ColorOffset == static_cast<int>(NativeZsiLightSettingsRecordLayout().RuntimeLight1ColorOffset));
        if (useResolvedRuntimeLightSetting) {
            lighting.AmbientColor = lighting.ResolvedRuntimeLightSetting.AmbientColor;
            lighting.DiffuseColor = lighting.ResolvedRuntimeLightSetting.Light0Color;
            if (!lighting.Light1ColorSource.empty()) {
                lighting.Light1Color = lighting.ResolvedRuntimeLightSetting.Light1Color;
            }
        } else {
            lighting.AmbientColor = ColorFromLightSettingsRecord(
                *record, lighting.AmbientColorOffset, lighting.AmbientColorSource);
            lighting.DiffuseColor = ColorFromLightSettingsRecord(
                *record, lighting.DiffuseColorOffset, lighting.DiffuseColorSource);
            if (!lighting.Light1ColorSource.empty()) {
                lighting.Light1Color = ColorFromLightSettingsRecord(
                    *record, lighting.Light1ColorOffset, lighting.Light1ColorSource);
            }
        }
    } else {
        if (lighting.AmbientGroupIndex < 0 || lighting.DiffuseGroupIndex < 0 ||
            static_cast<size_t>(lighting.AmbientGroupIndex) >= record->ByteGroups.size() ||
            static_cast<size_t>(lighting.DiffuseGroupIndex) >= record->ByteGroups.size()) {
            return lighting;
        }
        lighting.AmbientColor = ColorFromPicaByteGroup(record->ByteGroups[lighting.AmbientGroupIndex]);
        lighting.DiffuseColor = ColorFromPicaByteGroup(record->ByteGroups[lighting.DiffuseGroupIndex]);
    }
    if (lighting.TextureCombiner != "texel0_times_vertex_color") {
        return lighting;
    }
    if (lighting.TexturedBaseColorSource != "constant_white_until_material_combiner_route_decoded") {
        return lighting;
    }
    if (!MaterialLightingEnableSourceSupported(lighting.MaterialLightingEnableSource) ||
        !MaterialColorSourcesSupported(lighting)) {
        return lighting;
    }
    if (lighting.VertexColorFormula !=
        "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb / 255, 0, 255) / 255)") {
        return lighting;
    }
    lighting.VertexModulationColor = CombinePicaLightingVertexModulationColor(
        lighting.AmbientColor, lighting.DiffuseColor, { 0, 0, 0, 255 }, { 255, 255, 255, 255 },
        { 255, 255, 255, 255 }, lighting.VertexColorFormula);
    if (!lighting.DirectionalFormula.empty()) {
        const bool oneLightDirectionalFormula =
            lighting.DirectionalFormula ==
            "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb * max(dot(normal, light0), 0) / 255, 0, 255) / 255)";
        const bool twoLightDirectionalFormula =
            lighting.DirectionalFormula ==
            "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)";
        if (!oneLightDirectionalFormula && !twoLightDirectionalFormula) {
            return lighting;
        }
        const bool useResolvedRuntimeLight0Vector =
            lighting.ResolvedRuntimeLightSetting.Available &&
            lighting.Light0VectorSource == "oot3d_runtime_light_settings_signed_vec3_normalized" &&
            lighting.Light0VectorOffset == static_cast<int>(NativeZsiLightSettingsRecordLayout().RuntimeLight0DirectionOffset);
        if (useResolvedRuntimeLight0Vector) {
            lighting.Light0Vector = lighting.ResolvedRuntimeLightSetting.Light0Vector;
        } else if (IsSupportedLightSettingsVectorSource(lighting.Light0VectorSource)) {
            if (!CanReadRecordBytes(*record, lighting.Light0VectorOffset, 3)) {
                return lighting;
            }
            lighting.Light0Vector = LightVectorFromLightSettingsRecord(
                *record, lighting.Light0VectorOffset, lighting.Light0VectorSource);
        } else {
            if (lighting.Light0VectorGroupIndex < 0 ||
                static_cast<size_t>(lighting.Light0VectorGroupIndex) >= record->ByteGroups.size() ||
                (lighting.Light0VectorSource != "negated_signed_rgb_normalized" &&
                 lighting.Light0VectorSource != "signed_rgb_normalized")) {
                return lighting;
            }
            lighting.Light0Vector =
                LightVectorFromPicaByteGroup(record->ByteGroups[lighting.Light0VectorGroupIndex],
                                             lighting.Light0VectorSource);
        }
        lighting.DirectionalLightCount = 1;
        if (twoLightDirectionalFormula) {
            if (lighting.Light1ColorSource.empty()) {
                return lighting;
            }
            const bool useResolvedRuntimeLight1Vector =
                lighting.ResolvedRuntimeLightSetting.Available &&
                lighting.Light1VectorSource == "oot3d_runtime_light_settings_signed_vec3_normalized" &&
                lighting.Light1VectorOffset == static_cast<int>(NativeZsiLightSettingsRecordLayout().RuntimeLight1DirectionOffset);
            if (useResolvedRuntimeLight1Vector) {
                lighting.Light1Vector = lighting.ResolvedRuntimeLightSetting.Light1Vector;
            } else if (IsSupportedLightSettingsVectorSource(lighting.Light1VectorSource)) {
                if (!CanReadRecordBytes(*record, lighting.Light1VectorOffset, 3)) {
                    return lighting;
                }
                lighting.Light1Vector = LightVectorFromLightSettingsRecord(
                    *record, lighting.Light1VectorOffset, lighting.Light1VectorSource);
            } else {
                if (lighting.Light1VectorGroupIndex < 0 ||
                    static_cast<size_t>(lighting.Light1VectorGroupIndex) >= record->ByteGroups.size() ||
                    (lighting.Light1VectorSource != "negated_signed_rgb_normalized" &&
                     lighting.Light1VectorSource != "signed_rgb_normalized")) {
                    return lighting;
                }
                lighting.Light1Vector =
                    LightVectorFromPicaByteGroup(record->ByteGroups[lighting.Light1VectorGroupIndex],
                                                 lighting.Light1VectorSource);
            }
            lighting.DirectionalLightCount = 2;
        }
    }
    const bool vertexHemisphereReferenceSupported =
        lighting.VertexHemisphereLightingReference ==
            "azahar_pica_fragment_lighting_diffuse_sum_equation_without_lut_specular_or_shadow_texture" ||
        lighting.VertexHemisphereLightingReference ==
            "azahar_pica_frame_vs_uniform_f82_f84_diffuse_accumulator_for_native_cmb_skeleton_vertex_output";
    const bool vertexHemisphereLightColorModeSupported =
        lighting.VertexHemisphereLightColorMode ==
            "neutral_max_rgb_intensity_for_neutral_cmb_material_colors" ||
        lighting.VertexHemisphereLightColorMode ==
            "azahar_vs_uniform_trace_with_neutral_max_record_fallback" ||
        lighting.VertexHemisphereLightColorMode ==
            "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors" ||
        lighting.VertexHemisphereLightColorMode ==
            "oot3d_zsi_actor_vs_light_packet_colors_with_runtime_environment_vector_until_final_writer_resolved" ||
        lighting.VertexHemisphereLightColorMode ==
            "oot3d_zsi_actor_vs_light_packet_colors_with_env_record_vector_until_final_writer_resolved";
    const bool vertexHemisphereModelScopeSupported =
        lighting.MaterialVertexHemisphereModelScope ==
            "native_cmb_skeleton_transform_instance_models_with_native_normals" ||
        lighting.MaterialVertexHemisphereModelScope ==
            "native_cmb_transform_instance_or_static_baked_models_with_native_normals";
    const bool vertexHemisphereFormulaSupported =
        lighting.VertexHemisphereLightingFormula ==
            "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_skeleton_transform_instance_models_with_native_normals" ||
        lighting.VertexHemisphereLightingFormula ==
            "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_transform_instance_or_static_baked_models_with_native_normals";
    lighting.VertexHemisphereLightingSupported =
        lighting.MaterialLightingDeferredSource ==
            "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" &&
        vertexHemisphereModelScopeSupported &&
        lighting.MaterialVertexHemisphereLightingSource ==
            "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" &&
        vertexHemisphereFormulaSupported &&
        vertexHemisphereReferenceSupported &&
        vertexHemisphereLightColorModeSupported;
    if (lighting.VertexHemisphereLightingSupported &&
        VertexHemisphereModeUsesActorVsVectorPacket(lighting)) {
        const bool useActorVsColorPacket =
            VertexHemisphereModeUsesActorVsColorPacket(lighting) &&
            lighting.ActorVsLightPacket.Available;
        if (useActorVsColorPacket) {
            lighting.VertexHemisphereRuntimeColorSource = lighting.ActorVsLightPacket.ColorSource;
        } else if (lighting.ResolvedRuntimeLightSetting.RuntimeFinalAmbientColorUsedForRender ||
                   lighting.ResolvedRuntimeLightSetting.RuntimeFinalLightColorUsedForRender) {
            lighting.VertexHemisphereRuntimeColorSource =
                "active_0045dd50_final_runtime_environment_rgb_after_color_addends";
        } else {
            lighting.VertexHemisphereRuntimeColorSource =
                "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends";
        }
        lighting.VertexHemisphereRuntimeVectorSource =
            lighting.ActorVsLightPacket.VectorOriginResolved
                ? (lighting.ActorVsLightPacket.CompactPayloadSourceResolved
                       ? "selected_0045dd50_runtime_actor_vs_compact_payload_dir_s8"
                       : "selected_0045dd50_runtime_environment_signed_vec3")
                : "selected_0045dd50_runtime_environment_signed_vec3_pending_vector_origin";
        lighting.VertexHemisphereUniformTraceUsedAsRuntimeSource = false;
        if (useActorVsColorPacket) {
            lighting.VertexHemisphereAmbientColor = lighting.ActorVsLightPacket.AmbientColor;
            lighting.VertexHemisphereDiffuseColor = lighting.ActorVsLightPacket.Diffuse0Color;
            lighting.VertexHemisphereSecondaryColor = lighting.ActorVsLightPacket.Diffuse1Color;
        } else {
            lighting.VertexHemisphereAmbientColor = lighting.AmbientColor;
            lighting.VertexHemisphereDiffuseColor = lighting.DiffuseColor;
            lighting.VertexHemisphereSecondaryColor = lighting.Light1Color;
        }
    }
    const auto& bridgeContract = NativeKankyoRuntimeBridgeLayout();
    lighting.RuntimeUvTransform = BuildRuntimeUvTransformState(
        bridgeContract.LightingRegisterEmitter, lighting.VertexHemisphereLightingSupported);
    lighting.RuntimeSubmitDescriptor = BuildRuntimeSubmitDescriptorState(
        bridgeContract.LightingRegisterEmitter, false);
    lighting.Available = true;
    return lighting;
}

Oot3dNativePicaShadowState BuildOot3dNativePicaShadowState(
    const Oot3dNativeDemoScene& scene, const Oot3dNativePicaLightingRenderState& lighting) {
    Oot3dNativePicaShadowState shadow;
    shadow.SemanticTableAvailable = scene.NativePicaLightingSemanticsAvailable;
    shadow.DirectionalLightCount = lighting.DirectionalLightCount;

    if (!scene.NativePicaLightingSemanticsAvailable || !scene.NativePicaLightingSemantics.is_object()) {
        return shadow;
    }

    const auto* shadowEnvironment =
        JsonObjectChild(scene.NativePicaLightingSemantics, "shadow_environment");
    if (shadowEnvironment == nullptr) {
        return shadow;
    }

    shadow.SourceKind = JsonStringValue(*shadowEnvironment, "source_kind");
    const auto mode = JsonStringValue(*shadowEnvironment, "mode");
    if (!mode.empty()) {
        shadow.Mode = mode;
    }
    shadow.Status = JsonStringValue(*shadowEnvironment, "status");

    const auto* lightEnv = JsonObjectChild(*shadowEnvironment, "light_env");
    const auto* fragmentLightSource =
        JsonObjectChild(*shadowEnvironment, "fragment_light_source");
    const auto* textureShadow = JsonObjectChild(*shadowEnvironment, "texture_shadow");
    const auto* shaderEquation = JsonObjectChild(*shadowEnvironment, "shader_equation");
    const auto* shadowTextureSampling =
        JsonObjectChild(*shadowEnvironment, "shadow_texture_sampling");
    const auto* engineRoute = JsonObjectChild(*shadowEnvironment, "engine_route");
    const auto* shadow2dBackendPass =
        JsonObjectChild(*shadowEnvironment, "shadow2d_backend_pass");
    if (lightEnv == nullptr || fragmentLightSource == nullptr || textureShadow == nullptr ||
        shaderEquation == nullptr || shadowTextureSampling == nullptr || engineRoute == nullptr ||
        shadow2dBackendPass == nullptr) {
        return shadow;
    }

    shadow.LightEnvShadowAlphaName = JsonStringValue(*lightEnv, "shadow_alpha_name");
    shadow.LightEnvShadowSelectorName = JsonStringValue(*lightEnv, "shadow_selector_name");
    shadow.LightEnvShadowPrimaryName = JsonStringValue(*lightEnv, "shadow_primary_name");
    shadow.LightEnvShadowSecondaryName = JsonStringValue(*lightEnv, "shadow_secondary_name");
    shadow.FragmentLightShadowedNamePattern =
        JsonStringValue(*fragmentLightSource, "shadowed_name_pattern");
    shadow.FragmentLightShadowedRegisterCount =
        static_cast<uint32_t>(JsonIntValue(*fragmentLightSource, "shadowed_register_count", 0));
    shadow.TextureShadowZScaleName = JsonStringValue(*textureShadow, "shadow_z_scale_name");
    shadow.TextureShadowZBiasName = JsonStringValue(*textureShadow, "shadow_z_bias_name");
    shadow.ShaderEquationSourceKind = JsonStringValue(*shaderEquation, "source_kind");
    shadow.EnableShadowSource = JsonStringValue(*shaderEquation, "enable_shadow_source");
    shadow.ShadowSelectorSource = JsonStringValue(*shaderEquation, "shadow_selector_source");
    shadow.ShadowInvertSource = JsonStringValue(*shaderEquation, "shadow_invert_source");
    shadow.ShadowPrimarySource = JsonStringValue(*shaderEquation, "shadow_primary_source");
    shadow.ShadowSecondarySource = JsonStringValue(*shaderEquation, "shadow_secondary_source");
    shadow.ShadowAlphaSource = JsonStringValue(*shaderEquation, "shadow_alpha_source");
    shadow.PerLightShadowEnableSource =
        JsonStringValue(*shaderEquation, "per_light_shadow_enable_source");
    shadow.ShadowSampleSource = JsonStringValue(*shaderEquation, "shadow_sample_source");
    shadow.ShadowDefaultFormula = JsonStringValue(*shaderEquation, "shadow_default_formula");
    shadow.ShadowInvertFormula = JsonStringValue(*shaderEquation, "shadow_invert_formula");
    shadow.PrimaryRgbFormula = JsonStringValue(*shaderEquation, "primary_rgb_formula");
    shadow.SecondaryRgbFormula = JsonStringValue(*shaderEquation, "secondary_rgb_formula");
    shadow.AlphaFormula = JsonStringValue(*shaderEquation, "alpha_formula");
    shadow.ShadowTextureSamplingSourceKind =
        JsonStringValue(*shadowTextureSampling, "source_kind");
    shadow.ShadowTextureTypes = JsonStringArrayValue(*shadowTextureSampling, "texture_types");
    shadow.ShadowTextureOrthographicSource =
        JsonStringValue(*shadowTextureSampling, "orthographic_source");
    shadow.ShadowTextureBiasSource =
        JsonStringValue(*shadowTextureSampling, "shadow_texture_bias_source");
    shadow.ShadowTextureZFormula = JsonStringValue(*shadowTextureSampling, "z_formula");
    shadow.ShadowTextureCompareSource =
        JsonStringValue(*shadowTextureSampling, "compare_source");
    shadow.ShadowTextureFilter = JsonStringValue(*shadowTextureSampling, "filter");
    shadow.ShadowMapFormat = JsonStringValue(*shadowTextureSampling, "shadow_map_format");
    shadow.Shadow2dEncodedDepthDecodeSource =
        JsonStringValue(*shadowTextureSampling, "encoded_depth_decode_source");
    shadow.Shadow2dEncodedDepthBits =
        static_cast<uint32_t>(JsonIntValue(*shadowTextureSampling, "encoded_depth_bits", 0));
    shadow.Shadow2dEncodedAlphaBits =
        static_cast<uint32_t>(JsonIntValue(*shadowTextureSampling, "encoded_alpha_bits", 0));
    shadow.Shadow2dBiasShift =
        static_cast<uint32_t>(JsonIntValue(*shadowTextureSampling, "shadow_texture_bias_shift", 0));
    shadow.Shadow2dFilterTapCount =
        static_cast<uint32_t>(JsonIntValue(*shadowTextureSampling, "sample_filter_tap_count", 0));
    shadow.Shadow2dFilterResultChannelCount =
        static_cast<uint32_t>(JsonIntValue(*shadowTextureSampling, "filter_result_channel_count", 0));
    shadow.Shadow2dOutOfBoundsResult =
        JsonStringValue(*shadowTextureSampling, "out_of_bounds_result");
    shadow.Shadow2dFilterInterpolationSource =
        JsonStringValue(*shadowTextureSampling, "filter_interpolation_source");
    shadow.ShaderRouteSource = JsonStringValue(*engineRoute, "shader_route_source");
    shadow.ShadowMapSource = JsonStringValue(*engineRoute, "shadow_map_source");
    shadow.Shadow2dBackendPassRequestSource = shadow.ShadowMapSource;
    shadow.ShaderRouteApplication = JsonStringValue(*engineRoute, "shader_route_application");
    shadow.ShadowCasterScope = JsonStringValue(*engineRoute, "shadow_caster_scope");
    shadow.ShadowLightVectorSource = JsonStringValue(*engineRoute, "shadow_light_vector_source");
    shadow.ShadowOcclusionFormula = JsonStringValue(*engineRoute, "shadow_occlusion_formula");
    shadow.ShadowLightContributionFormula =
        JsonStringValue(*engineRoute, "shadow_light_contribution_formula");
    shadow.PendingRoute = JsonStringValue(*engineRoute, "pending_route");
    shadow.SelfShadowShaderRouteDecoded =
        JsonBoolValue(*engineRoute, "self_shadow_shader_route_decoded", false);
    shadow.UsesRuntimeN64AssetSubstitution =
        JsonBoolValue(*engineRoute, "uses_runtime_n64_asset_substitution", true);
    shadow.Shadow2dVisualPassSourceKind =
        JsonStringValue(*shadow2dBackendPass, "source_kind");
    shadow.Shadow2dVisualPassRenderTargetFormat =
        JsonStringValue(*shadow2dBackendPass, "render_target_format");
    shadow.Shadow2dMaterialTextureProjectionInputSource =
        JsonStringValue(*shadow2dBackendPass, "material_texture_projection_input_source");
    shadow.Shadow2dTexCoord0WInputSource =
        JsonStringValue(*shadow2dBackendPass, "texcoord0_w_input_source");
    const bool nativeCmbVShaderTexCoord0WOutputDecoded =
        NativeCmbVShaderHasTexCoord0WOutput(scene);
    shadow.Shadow2dProjectionRegisterValueSource =
        JsonStringValue(*shadow2dBackendPass, "projection_register_value_source");
    shadow.Shadow2dVisualPassApplication =
        JsonStringValue(*shadow2dBackendPass, "visual_pass_application");
    shadow.Shadow2dVisualPassBlockedReason =
        JsonStringValue(*shadow2dBackendPass, "visual_blocked_reason");
    shadow.Shadow2dProjectionRegisterValuesDecoded =
        JsonBoolValue(*shadow2dBackendPass, "projection_register_values_decoded", false);
    shadow.Shadow2dVisualPassUsesRuntimeN64AssetSubstitution =
        JsonBoolValue(*shadow2dBackendPass, "uses_runtime_n64_asset_substitution", true);

    const auto decodedShadowProjection =
        DecodeOot3dNativePicaShadowProjectionRegisters(scene);
    shadow.Shadow2dProjectionRegisterTraceAvailable =
        decodedShadowProjection.TraceAvailable;
    shadow.Shadow2dProjectionRegisterTraceSourceKind =
        decodedShadowProjection.TraceSourceKind;
    shadow.Shadow2dProjectionRegisterTraceFormat =
        decodedShadowProjection.TraceFormat;
    shadow.Shadow2dDmpShadowZUniformsDecoded =
        decodedShadowProjection.DmpShadowZUniformsDecoded;
    shadow.Shadow2dDmpPerspectiveShadowDecoded =
        decodedShadowProjection.DmpPerspectiveShadowDecoded;
    shadow.Shadow2dDmpPerspectiveShadow =
        decodedShadowProjection.DmpPerspectiveShadow;
    shadow.Shadow2dPicaTextureShadowRegisterDecoded =
        decodedShadowProjection.TextureShadowRegisterDecoded;
    shadow.Shadow2dPicaFramebufferShadowRegisterDecoded =
        decodedShadowProjection.FramebufferShadowRegisterDecoded;
    shadow.Shadow2dShaderRouteRegisterTraceDecoded =
        decodedShadowProjection.ShaderRouteRegisterTraceDecoded;
    shadow.Shadow2dFragmentLightingEnableDecoded =
        decodedShadowProjection.FragmentLightingEnableDecoded;
    shadow.Shadow2dLightingConfig0Decoded =
        decodedShadowProjection.LightingConfig0Decoded;
    shadow.Shadow2dLightingConfig1Decoded =
        decodedShadowProjection.LightingConfig1Decoded;
    shadow.Shadow2dShadowTextureParamDecoded =
        decodedShadowProjection.ShadowTextureParamDecoded;
    shadow.Shadow2dShadowTextureDimDecoded =
        decodedShadowProjection.ShadowTextureDimDecoded;
    shadow.Shadow2dShaderRouteMatchesPrimaryRgbShadowTerm =
        decodedShadowProjection.ShaderRouteMatchesPrimaryRgbShadowTerm;
    shadow.Shadow2dProjectionRegisterDecodeSource =
        decodedShadowProjection.DecodeSource;
    shadow.Shadow2dShaderRouteDecodeSource =
        decodedShadowProjection.ShaderRouteDecodeSource;
    shadow.Shadow2dPicaTextureShadowRegisterIndex = kPicaRegTexunit0Shadow;
    shadow.Shadow2dPicaFramebufferShadowRegisterIndex = kPicaRegFragopShadow;
    shadow.Shadow2dPicaTextureShadowRegisterRaw =
        decodedShadowProjection.TextureShadowRegisterRaw;
    shadow.Shadow2dPicaFramebufferShadowRegisterRaw =
        decodedShadowProjection.FramebufferShadowRegisterRaw;
    shadow.Shadow2dTextureShadowOrthographic =
        decodedShadowProjection.TextureShadowOrthographic;
    shadow.Shadow2dTextureShadowRawBias =
        decodedShadowProjection.TextureShadowRawBias;
    shadow.Shadow2dTextureShadowCompareBias =
        decodedShadowProjection.TextureShadowCompareBias;
    shadow.Shadow2dFramebufferShadowConstantRaw =
        decodedShadowProjection.FramebufferShadowConstantRaw;
    shadow.Shadow2dFramebufferShadowLinearRaw =
        decodedShadowProjection.FramebufferShadowLinearRaw;
    shadow.Shadow2dLightingConfig0Raw =
        decodedShadowProjection.LightingConfig0Raw;
    shadow.Shadow2dLightingConfig1Raw =
        decodedShadowProjection.LightingConfig1Raw;
    shadow.Shadow2dShadowSelector =
        decodedShadowProjection.ShadowSelector;
    shadow.Shadow2dPerLightShadowEnableMask =
        decodedShadowProjection.PerLightShadowEnableMask;
    shadow.Shadow2dShadowTextureParamRegisterIndex =
        decodedShadowProjection.ShadowTextureParamRegisterIndex;
    shadow.Shadow2dShadowTextureParamRaw =
        decodedShadowProjection.ShadowTextureParamRaw;
    shadow.Shadow2dShadowTextureType =
        decodedShadowProjection.ShadowTextureType;
    shadow.Shadow2dShadowTextureDimRegisterIndex =
        decodedShadowProjection.ShadowTextureDimRegisterIndex;
    shadow.Shadow2dShadowTextureDimRaw =
        decodedShadowProjection.ShadowTextureDimRaw;
    shadow.Shadow2dShadowTextureWidth =
        decodedShadowProjection.ShadowTextureWidth;
    shadow.Shadow2dShadowTextureHeight =
        decodedShadowProjection.ShadowTextureHeight;
    shadow.Shadow2dDmpShadowZBias = decodedShadowProjection.DmpShadowZBias;
    shadow.Shadow2dDmpShadowZScale = decodedShadowProjection.DmpShadowZScale;
    shadow.Shadow2dFramebufferShadowConstant =
        decodedShadowProjection.FramebufferShadowConstant;
    shadow.Shadow2dFramebufferShadowLinear =
        decodedShadowProjection.FramebufferShadowLinear;
    shadow.Shadow2dFragmentLightingEnabled =
        decodedShadowProjection.FragmentLightingEnabled;
    shadow.Shadow2dLightingEnableShadow =
        decodedShadowProjection.LightingEnableShadow;
    shadow.Shadow2dLightingShadowPrimary =
        decodedShadowProjection.LightingShadowPrimary;
    shadow.Shadow2dLightingShadowSecondary =
        decodedShadowProjection.LightingShadowSecondary;
    shadow.Shadow2dLightingShadowInvert =
        decodedShadowProjection.LightingShadowInvert;
    shadow.Shadow2dLightingShadowAlpha =
        decodedShadowProjection.LightingShadowAlpha;
    shadow.Shadow2dShadowTextureIsShadow2d =
        decodedShadowProjection.ShadowTextureIsShadow2d;
    if (decodedShadowProjection.DmpShadowZUniformsDecoded ||
        decodedShadowProjection.TextureShadowRegisterDecoded) {
        shadow.Shadow2dProjectionRegisterValuesDecoded = true;
        shadow.Shadow2dProjectionRegisterValueSource =
            decodedShadowProjection.DecodeSource;
    }
    if (decodedShadowProjection.ShaderRouteMatchesPrimaryRgbShadowTerm) {
        shadow.SelfShadowShaderRouteDecoded = true;
        shadow.ShaderRouteSource = decodedShadowProjection.ShaderRouteDecodeSource;
    }
    if (shadow.Shadow2dShadowTextureDimDecoded &&
        shadow.Shadow2dShadowTextureWidth > 0 &&
        shadow.Shadow2dShadowTextureHeight > 0) {
        shadow.Shadow2dBackendShadowMapRenderTargetDecoded = true;
        shadow.Shadow2dBackendShadowMapRenderTargetSource =
            "oot3d_native_pica_shadow_texture_dimension_register_trace";
    }
    const bool shadow2dBlockedOnNativeRuntimeValueSource =
        shadow.Shadow2dVisualPassBlockedReason ==
            "shadow2d_shadow_z_scale_bias_register_values_not_decoded" ||
        shadow.Shadow2dVisualPassBlockedReason ==
            "shadow2d_fragop_shadow_flush_and_active_shadow_z_scale_bias_values_not_decoded" ||
        shadow.Shadow2dVisualPassBlockedReason ==
            "shadow2d_default_or_inactive_for_current_fixture";
    if (shadow.Shadow2dProjectionRegisterValuesDecoded &&
        shadow2dBlockedOnNativeRuntimeValueSource) {
        if (!shadow.SelfShadowShaderRouteDecoded) {
            shadow.Shadow2dVisualPassBlockedReason =
                "shadow2d_self_shadow_shader_route_not_decoded";
        } else if (!shadow.Shadow2dBackendShadowMapRenderTargetDecoded) {
            shadow.Shadow2dVisualPassBlockedReason =
                "shadow2d_shadow_texture_dimension_register_not_decoded";
        } else {
            shadow.Shadow2dVisualPassBlockedReason =
                "shadow2d_backend_shadow_map_depth_encode_shader_not_implemented";
        }
    }
    if (decodedShadowProjection.ShaderRouteRegisterTraceDecoded) {
        shadow.Shadow2dShaderRouteTraceStatus =
            decodedShadowProjection.ShaderRouteMatchesPrimaryRgbShadowTerm
                ? "shadow2d_primary_rgb_shadow_term_active"
                : "shadow2d_primary_rgb_shadow_term_inactive";
        if (!decodedShadowProjection.ShaderRouteMatchesPrimaryRgbShadowTerm) {
            shadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm = true;
            shadow.Shadow2dVisualPassBlockedReason =
                "shadow2d_shader_route_register_trace_primary_rgb_term_inactive";
        }
    }

    shadow.LightEnvShadowRegistersSupported =
        shadow.LightEnvShadowAlphaName == "dmp_LightEnv.shadowAlpha" &&
        shadow.LightEnvShadowSelectorName == "dmp_LightEnv.shadowSelector" &&
        shadow.LightEnvShadowPrimaryName == "dmp_LightEnv.shadowPrimary" &&
        shadow.LightEnvShadowSecondaryName == "dmp_LightEnv.shadowSecondary";
    shadow.FragmentLightShadowFlagsSupported =
        shadow.FragmentLightShadowedNamePattern == "dmp_FragmentLightSource[%d].shadowed" &&
        shadow.FragmentLightShadowedRegisterCount == 8;
    shadow.ShadowTextureProjectionRegistersSupported =
        shadow.TextureShadowZScaleName == "dmp_Texture[0].shadowZScale" &&
        shadow.TextureShadowZBiasName == "dmp_Texture[0].shadowZBias";
    const bool supportsShadow2d =
        std::find(shadow.ShadowTextureTypes.begin(), shadow.ShadowTextureTypes.end(),
                  "Pica::TexturingRegs::TextureConfig::Shadow2D") !=
        shadow.ShadowTextureTypes.end();
    const bool supportsShadowCube =
        std::find(shadow.ShadowTextureTypes.begin(), shadow.ShadowTextureTypes.end(),
                  "Pica::TexturingRegs::TextureConfig::ShadowCube") !=
        shadow.ShadowTextureTypes.end();
    shadow.Shadow2dTextureTypeSupported = supportsShadow2d;
    shadow.ShaderEquationSemanticsSupported =
        shadow.ShaderEquationSourceKind == "azahar_pica_fragment_lighting_shadow_equation" &&
        shadow.EnableShadowSource == "Pica::LightingRegs::config0.enable_shadow" &&
        shadow.ShadowSelectorSource == "Pica::LightingRegs::config0.shadow_selector" &&
        shadow.ShadowInvertSource == "Pica::LightingRegs::config0.shadow_invert" &&
        shadow.ShadowPrimarySource == "Pica::LightingRegs::config0.shadow_primary" &&
        shadow.ShadowSecondarySource == "Pica::LightingRegs::config0.shadow_secondary" &&
        shadow.ShadowAlphaSource == "Pica::LightingRegs::config0.shadow_alpha" &&
        shadow.PerLightShadowEnableSource ==
            "not Pica::LightingRegs::config1.disable_shadow[light_index]" &&
        shadow.ShadowSampleSource == "sampleTexUnit[shadow_selector]" &&
        !shadow.ShadowDefaultFormula.empty() &&
        !shadow.ShadowInvertFormula.empty() &&
        !shadow.PrimaryRgbFormula.empty() &&
        !shadow.SecondaryRgbFormula.empty() &&
        !shadow.AlphaFormula.empty();
    shadow.ShadowTextureSamplingSemanticsSupported =
        shadow.ShadowTextureSamplingSourceKind ==
            "azahar_pica_shadow_texture_sampling_equation" &&
        supportsShadow2d &&
        supportsShadowCube &&
        shadow.ShadowTextureOrthographicSource == "Pica::TexturingRegs::shadow.orthographic" &&
        shadow.ShadowTextureBiasSource == "Pica::TexturingRegs::shadow.bias << 1" &&
        !shadow.ShadowTextureZFormula.empty() &&
        shadow.ShadowTextureCompareSource ==
            "CompareShadow(DecodeShadow(encoded_shadow_pixel), z)" &&
        shadow.ShadowTextureFilter ==
            "2x2_neighbor_compare_results_returned_as_rgba_shadow_vector" &&
        shadow.ShadowMapFormat == "r32ui_encoded_shadow_depth_reference";
    shadow.Shadow2dEncodedDepthCompareSupported =
        shadow.ShadowTextureSamplingSemanticsSupported &&
        shadow.Shadow2dEncodedDepthDecodeSource ==
            "DecodeShadow(pixel) -> depth24 = pixel >> 8, alpha8 = pixel & 0xFF" &&
        shadow.Shadow2dEncodedDepthBits == 24 &&
        shadow.Shadow2dEncodedAlphaBits == 8 &&
        shadow.Shadow2dBiasShift == 1 &&
        shadow.Shadow2dFilterTapCount == 4 &&
        shadow.Shadow2dFilterResultChannelCount == 4 &&
        shadow.Shadow2dOutOfBoundsResult == "lit_1_0" &&
        shadow.Shadow2dFilterInterpolationSource ==
            "bilinear_mix_of_2x2_compare_results";
    shadow.PrimaryRgbShadowTermSupported =
        shadow.ShaderRouteApplication == "pica_primary_rgb_diffuse_sum_shadow_term" &&
        shadow.PrimaryRgbFormula == "diffuse_sum.rgb *= shadow.rgb when shadow_primary and per-light shadow_enable are true";
    shadow.NativeGeometryOcclusionSupported = false;
    shadow.ShadowLightVectorSourceSupported =
        ShadowLightVectorSourceSupported(shadow.ShadowLightVectorSource);
    shadow.FullPrimaryLightContributionShadowSupported =
        shadow.ShadowLightContributionFormula ==
        "pica_shadow_map_multiplies_native_cmb_vertex_hemisphere_material_ambient_light_ambient_plus_material_diffuse_light_diffuse_dot";
    shadow.Shadow2dBackendPassRequestSupported =
        shadow.ShadowTextureProjectionRegistersSupported &&
        shadow.ShadowTextureSamplingSemanticsSupported &&
        shadow.Shadow2dTextureTypeSupported &&
        shadow.ShadowMapSource == "oot3d_pica_shadow_texture_projection_registers" &&
        shadow.PendingRoute == "native_pica_shadow2d_texture_projection_backend_pass_pending" &&
        shadow.ShadowMapFormat == "r32ui_encoded_shadow_depth_reference" &&
        shadow.ShadowTextureCompareSource == "CompareShadow(DecodeShadow(encoded_shadow_pixel), z)" &&
        shadow.ShadowTextureFilter == "2x2_neighbor_compare_results_returned_as_rgba_shadow_vector";
    const bool shadow2dProjectionRegisterValueSourceSupported =
        shadow.Shadow2dProjectionRegisterValueSource ==
            "pending_native_pica_shadow_z_scale_bias_register_decode" ||
        shadow.Shadow2dProjectionRegisterValueSource ==
            "oot3d_codebin_default_shadow2d_inactive_or_validation_pending" ||
        shadow.Shadow2dProjectionRegisterValueSource ==
            "oot3d_native_dmp_shadow_z_uniform_trace" ||
        shadow.Shadow2dProjectionRegisterValueSource ==
            "oot3d_native_pica_shadow_register_trace" ||
        shadow.Shadow2dProjectionRegisterValueSource ==
            "oot3d_native_dmp_shadow_z_uniforms_and_pica_shadow_register_trace";
    shadow.Shadow2dVisualPassRequestSupported =
        shadow.Shadow2dBackendPassRequestSupported &&
        shadow.Shadow2dEncodedDepthCompareSupported &&
        shadow.Shadow2dVisualPassSourceKind == "oot3d_pica_shadow2d_visual_pass_contract" &&
        shadow.Shadow2dVisualPassRenderTargetFormat == shadow.ShadowMapFormat &&
        shadow2dProjectionRegisterValueSourceSupported &&
        shadow.Shadow2dVisualPassApplication ==
            "sample_shadow2d_encoded_depth_in_fragment_primary_rgb_shadow_term" &&
        (!shadow.Shadow2dVisualPassBlockedReason.empty() ||
         shadow.Shadow2dProjectionRegisterValuesDecoded) &&
        !shadow.Shadow2dVisualPassUsesRuntimeN64AssetSubstitution &&
        !shadow.UsesRuntimeN64AssetSubstitution;
    shadow.Shadow2dTexCoord0WInputSupported =
        shadow.Shadow2dVisualPassRequestSupported &&
        nativeCmbVShaderTexCoord0WOutputDecoded &&
        shadow.Shadow2dTexCoord0WInputSource ==
            "oot3d_cmb_vshader_shbin_output_texcoord0_w";
    shadow.Shadow2dMaterialTextureProjectionInputSupported =
        shadow.Shadow2dVisualPassRequestSupported &&
        shadow.Shadow2dTexCoord0WInputSupported &&
        shadow.Shadow2dMaterialTextureProjectionInputSource ==
            "oot3d_cmb_material_texture_coord0_plus_pica_texcoord0_w";
    shadow.SelfShadowCandidateRouteSupported =
        shadow.SemanticTableAvailable &&
        shadow.SourceKind == "oot3d_pica_lightenv_shadow_semantics" &&
        shadow.Mode == "native_pica_self_shadow" &&
        shadow.LightEnvShadowRegistersSupported &&
        shadow.FragmentLightShadowFlagsSupported &&
        shadow.ShadowTextureProjectionRegistersSupported &&
        shadow.ShaderEquationSemanticsSupported &&
        shadow.ShadowTextureSamplingSemanticsSupported &&
        shadow.PrimaryRgbShadowTermSupported &&
        shadow.FullPrimaryLightContributionShadowSupported &&
        shadow.Shadow2dBackendPassRequestSupported &&
        shadow.Shadow2dVisualPassRequestSupported &&
        shadow.Shadow2dMaterialTextureProjectionInputSupported &&
        shadow.Shadow2dTexCoord0WInputSupported &&
        !shadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm &&
        shadow.ShadowCasterScope == "native_cmb_skeleton_transform_instance_models_with_native_normals" &&
        shadow.ShadowLightVectorSourceSupported &&
        !shadow.Shadow2dVisualPassUsesRuntimeN64AssetSubstitution &&
        !shadow.UsesRuntimeN64AssetSubstitution;
    shadow.Shadow2dVisualPassReady =
        shadow.Shadow2dVisualPassRequestSupported &&
        shadow.Shadow2dProjectionRegisterValuesDecoded &&
        shadow.SelfShadowShaderRouteDecoded &&
        shadow.Shadow2dBackendShadowMapRenderTargetDecoded &&
        shadow.Shadow2dBackendShadowMapPassImplemented &&
        shadow.Shadow2dVisualPassBlockedReason.empty();

    shadow.Available =
        shadow.SemanticTableAvailable &&
        shadow.SourceKind == "oot3d_pica_lightenv_shadow_semantics" &&
        shadow.Mode == "native_pica_self_shadow" &&
        shadow.LightEnvShadowRegistersSupported &&
        shadow.FragmentLightShadowFlagsSupported &&
        shadow.ShadowTextureProjectionRegistersSupported &&
        shadow.ShaderEquationSemanticsSupported &&
        shadow.ShadowTextureSamplingSemanticsSupported &&
        shadow.PrimaryRgbShadowTermSupported &&
        shadow.FullPrimaryLightContributionShadowSupported &&
        shadow.Shadow2dBackendPassRequestSupported &&
        shadow.Shadow2dVisualPassRequestSupported &&
        !shadow.ShaderRouteSource.empty() &&
        !shadow.ShadowMapSource.empty() &&
        !shadow.PendingRoute.empty() &&
        !shadow.UsesRuntimeN64AssetSubstitution;
    return shadow;
}

void ApplyOot3dNativePicaLighting(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    const Oot3dNativePicaLightingSemanticPlan* semanticPlan) {
    renderScene.PicaLighting = semanticPlan != nullptr
                                   ? BuildOot3dNativePicaLightingRenderState(
                                         scene, *semanticPlan, runtimeEnvironment)
                                   : BuildOot3dNativePicaLightingRenderState(
                                         scene, runtimeEnvironment);
    renderScene.PicaShadow = BuildOot3dNativePicaShadowState(scene, renderScene.PicaLighting);
    if (!renderScene.PicaLighting.Available) {
        return;
    }

    ApplyPicaLightingToModel(renderScene.PicaLighting, renderScene.PicaShadow, renderScene.Room);
    for (auto& roomModel : renderScene.AdditionalRoomModels) {
        ApplyPicaLightingToModel(renderScene.PicaLighting, renderScene.PicaShadow, roomModel);
    }
    ApplyPicaLightingToModel(renderScene.PicaLighting, renderScene.PicaShadow, renderScene.Link);
    for (auto& actorVisual : renderScene.ActorVisuals) {
        ApplyPicaLightingToModel(renderScene.PicaLighting, renderScene.PicaShadow, actorVisual);
    }

    renderScene.PicaLighting.AppliedBatchCount = 0;
    renderScene.PicaLighting.AppliedTexturedBatchCount = 0;
    renderScene.PicaLighting.AppliedVertexCount = 0;
    renderScene.PicaLighting.DirectionalLightingApplied = false;
    renderScene.PicaLighting.NativeNormalVertexCount = 0;
    renderScene.PicaShadow.CandidateBatchCount = 0;
    renderScene.PicaShadow.AppliedBatchCount = 0;
    renderScene.PicaShadow.AppliedVertexCount = 0;
    renderScene.PicaShadow.OccludedVertexCount = 0;
    CountPicaLightingModelApplication(renderScene.PicaLighting, renderScene.Room);
    for (const auto& roomModel : renderScene.AdditionalRoomModels) {
        CountPicaLightingModelApplication(renderScene.PicaLighting, roomModel);
    }
    CountPicaLightingModelApplication(renderScene.PicaLighting, renderScene.Link);
    for (const auto& actorVisual : renderScene.ActorVisuals) {
        CountPicaLightingModelApplication(renderScene.PicaLighting, actorVisual);
    }
    CountPicaSelfShadowModelApplication(renderScene.PicaShadow, renderScene.Room);
    for (const auto& roomModel : renderScene.AdditionalRoomModels) {
        CountPicaSelfShadowModelApplication(renderScene.PicaShadow, roomModel);
    }
    CountPicaSelfShadowModelApplication(renderScene.PicaShadow, renderScene.Link);
    for (const auto& actorVisual : renderScene.ActorVisuals) {
        CountPicaSelfShadowModelApplication(renderScene.PicaShadow, actorVisual);
    }
}

void RefreshOot3dNativeDemoRoomMaterialAnimations(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    float materialAnimationFrame) {
    renderScene.NativeRoomMaterialAnimationCount =
        static_cast<uint32_t>(std::min<size_t>(scene.RoomMaterialAnimations.size(),
                                               std::numeric_limits<uint32_t>::max()));
    renderScene.NativeRoomMaterialAnimationFrame = materialAnimationFrame;
    renderScene.NativeRoomMaterialAnimationAppliedBatchCount =
        static_cast<uint32_t>(std::min<size_t>(
            ApplyOot3dNativeRenderModelMaterialAnimationFrame(
                renderScene.Room, scene.RoomModel, scene.RoomMaterialAnimations,
                materialAnimationFrame),
            std::numeric_limits<uint32_t>::max()));
}

Oot3dNativePicaViewportState BuildOot3dNativePicaViewportState(
    const Oot3dNativeEnvironmentBackgroundState& background) {
    const auto& lens = background.NativeKankyoLensEffect;
    Oot3dNativePicaViewportState viewport;
    viewport.SourceKind =
        "oot3d_code_bin_FUN_00484db4_clip_to_native_top_screen_viewport";
    viewport.HalfWidthAddress = lens.LensPositionProjectionScaleXAddress;
    viewport.HalfHeightAddress = lens.LensPositionProjectionScaleYAddress;
    viewport.HalfWidth = std::abs(lens.LensPositionProjectionScaleX);
    viewport.HalfHeight = std::abs(lens.LensPositionProjectionScaleY);
    viewport.CodeBinSourceDecoded =
        viewport.HalfWidthAddress != 0 && viewport.HalfHeightAddress != 0 &&
        std::isfinite(viewport.HalfWidth) && std::isfinite(viewport.HalfHeight) &&
        viewport.HalfWidth > 0.0f && viewport.HalfHeight > 0.0f;
    if (viewport.CodeBinSourceDecoded) {
        viewport.Aspect = viewport.HalfWidth / viewport.HalfHeight;
        viewport.Available = std::isfinite(viewport.Aspect) && viewport.Aspect > 0.0f;
    }
    viewport.SourceStatus =
        viewport.Available
            ? "native projection aspect resolved from the code.bin clip-to-screen half-width and half-height constants consumed by FUN_00484db4"
            : "native projection aspect unavailable because the code.bin clip-to-screen constants were not resolved";
    return viewport;
}

double ResolveOot3dNativePicaProjectionAspect(
    const Oot3dNativeDemoRenderScene& scene, double fallbackAspect) {
    return scene.PicaViewport.Available && std::isfinite(scene.PicaViewport.Aspect) &&
                   scene.PicaViewport.Aspect > 0.0f
               ? static_cast<double>(scene.PicaViewport.Aspect)
               : fallbackAspect;
}

void RefreshOot3dNativeDemoRenderSceneRuntimeEnvironment(
    const Oot3dNativeDemoScene& scene,
    Oot3dNativeDemoRenderScene& renderScene,
    const Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame) {
    RefreshOot3dNativeDemoRoomMaterialAnimations(scene, renderScene, materialAnimationFrame);
    ApplyOot3dNativePicaLighting(scene, renderScene, runtimeEnvironment);
    renderScene.PicaFog = BuildOot3dNativePicaFogState(scene, renderScene.PicaLighting);
    renderScene.EnvironmentBackground =
        BuildOot3dNativeEnvironmentBackgroundState(scene, renderScene.PicaLighting, runtimeEnvironment);
    renderScene.PicaViewport =
        BuildOot3dNativePicaViewportState(renderScene.EnvironmentBackground);
    renderScene.EnvironmentModels =
        BuildOot3dNativeEnvironmentModels(renderScene.EnvironmentBackground, materialAnimationFrame);
    for (auto& environmentModel : renderScene.EnvironmentModels) {
        ApplyTextureEnvProgramOnlyToModel(environmentModel);
    }
    if (renderScene.EnvironmentBackground.NativeKankyoDrawRouteDecoded) {
        renderScene.EnvironmentBackground.UsedForRender = !renderScene.EnvironmentModels.empty();
        if (renderScene.EnvironmentModels.empty()) {
            renderScene.EnvironmentBackground.SourceStatus +=
                "; selected native kankyo CMBs could not be parsed into render models";
        }
    }
}

void ApplyOot3dNativePicaLightingToActorVisualRange(
    Oot3dNativeDemoRenderScene& renderScene,
    size_t firstActorVisual,
    size_t actorVisualCount,
    bool collectDiagnostics) {
    if (!renderScene.PicaLighting.Available || firstActorVisual >= renderScene.ActorVisuals.size()) {
        return;
    }

    const size_t endActorVisual =
        std::min(renderScene.ActorVisuals.size(), firstActorVisual + actorVisualCount);
    for (size_t actorVisualIndex = firstActorVisual; actorVisualIndex < endActorVisual; ++actorVisualIndex) {
        ApplyPicaLightingToModel(
            renderScene.PicaLighting, renderScene.PicaShadow,
            renderScene.ActorVisuals[actorVisualIndex], collectDiagnostics);
    }
}

void ApplyOot3dNativePicaLightingToLink(
    Oot3dNativeDemoRenderScene& renderScene,
    bool collectDiagnostics) {
    if (!renderScene.PicaLighting.Available) {
        return;
    }
    ApplyPicaLightingToModel(
        renderScene.PicaLighting, renderScene.PicaShadow, renderScene.Link,
        collectDiagnostics);
}

void ApplyOot3dNativePicaLightingStateToModel(
    const Oot3dNativePicaLightingRenderState& lighting,
    Oot3dNativeRenderModel& model,
    bool preserveMaterialState,
    bool collectDiagnostics) {
    Oot3dNativePicaShadowState noShadow;
    if (!preserveMaterialState) {
        ApplyPicaLightingToModel(lighting, noShadow, model, collectDiagnostics);
        model.NativeRuntimePicaLightingBaked = lighting.Available;
        return;
    }

    std::vector<Oot3dNativeRenderMaterialState> originalMaterials;
    std::vector<std::vector<ColorRgba8>> originalVertexColors;
    originalMaterials.reserve(model.Batches.size());
    originalVertexColors.reserve(model.Batches.size());
    for (const auto& batch : model.Batches) {
        originalMaterials.push_back(batch.Material);
        std::vector<ColorRgba8> colors;
        colors.reserve(batch.Vertices.size());
        for (const auto& vertex : batch.Vertices) {
            colors.push_back(vertex.Color);
        }
        originalVertexColors.push_back(std::move(colors));
    }

    ApplyPicaLightingToModel(lighting, noShadow, model, collectDiagnostics);
    for (size_t batchIndex = 0; batchIndex < model.Batches.size(); ++batchIndex) {
        auto& batch = model.Batches[batchIndex];
        const auto& originalMaterial = originalMaterials[batchIndex];
        const bool receivesRuntimeVertexLight =
            originalMaterial.VertexLightingEnabled || originalMaterial.HemisphereLightingEnabled;
        for (size_t vertexIndex = 0; vertexIndex < batch.Vertices.size(); ++vertexIndex) {
            const auto originalColor = originalVertexColors[batchIndex][vertexIndex];
            if (!receivesRuntimeVertexLight) {
                batch.Vertices[vertexIndex].Color = originalColor;
            } else {
                batch.Vertices[vertexIndex].Color.A = originalColor.A;
            }
        }
        batch.Material = originalMaterial;
        ApplyTextureEnvShaderCoverage(batch.Material);
        if (batch.Material.Textured &&
            batch.Material.TextureEnvProgram.ColorShaderPathApplied) {
            batch.Material.VertexColorModulatesTexture =
                batch.Material.TextureEnvProgram.UsesPrimaryColor;
        }
    }
    model.NativeRuntimePicaLightingBaked = lighting.Available;
}

Oot3dNativePicaLightingDebugState BuildOot3dNativePicaLightingDebugState(const Oot3dNativeDemoScene& scene) {
    Oot3dNativePicaLightingDebugState debug;
    debug.SemanticTableAvailable = scene.NativePicaLightingSemanticsAvailable;
    debug.SourceKind = scene.NativePicaLightingSemanticsSourceKind;
    debug.ActiveSetupIndex = scene.NativePicaLighting.ActiveSetupIndex;

    if (!scene.NativePicaLighting.Available || !scene.NativePicaLightingSemanticsAvailable ||
        !scene.NativePicaLightingSemantics.is_object()) {
        return debug;
    }

    const auto& table = scene.NativePicaLightingSemantics;
    if (JsonStringValue(table, "layout") != scene.NativePicaLighting.SelectedLightSettingsLayout) {
        return debug;
    }
    const int tableRecordSize = JsonIntValue(table, "record_size", -1);
    if (tableRecordSize <= 0) {
        return debug;
    }

    const auto* debugModes = JsonObjectChild(table, "debug_modes");
    if (debugModes == nullptr) {
        return debug;
    }
    const auto* mode = JsonObjectChild(*debugModes, debug.Mode.c_str());
    if (mode == nullptr) {
        return debug;
    }

    debug.RecordSelector = JsonStringValue(*mode, "record_selector");
    debug.AmbientGroupIndex = JsonIntValue(*mode, "ambient_group_index", -1);
    debug.DiffuseGroupIndex = JsonIntValue(*mode, "diffuse_group_index", -1);
    debug.AmbientColorSource = JsonStringValue(*mode, "ambient_color_source");
    debug.DiffuseColorSource = JsonStringValue(*mode, "diffuse_color_source");
    debug.AmbientColorOffset = JsonIntValue(*mode, "ambient_color_offset", -1);
    debug.DiffuseColorOffset = JsonIntValue(*mode, "diffuse_color_offset", -1);
    debug.ForceUntexturedBatches = JsonBoolValue(*mode, "force_untextured_batches", false);
    debug.PreserveVertexAlpha = JsonBoolValue(*mode, "preserve_vertex_alpha", true);

    const auto* record = SelectPicaLightingRecord(scene.NativePicaLighting, debug.RecordSelector, -1);
    if (record == nullptr || record->EntrySize != tableRecordSize) {
        return debug;
    }

    debug.RecordIndex = record->Index;
    debug.RecordOffset = record->Offset;
    if (!debug.AmbientColorSource.empty() || !debug.DiffuseColorSource.empty()) {
        if (!IsSupportedLightSettingsColorSource(debug.AmbientColorSource) ||
            !IsSupportedLightSettingsColorSource(debug.DiffuseColorSource) ||
            !CanReadRecordBytes(*record, debug.AmbientColorOffset, 3) ||
            !CanReadRecordBytes(*record, debug.DiffuseColorOffset, 3)) {
            return debug;
        }
        debug.AmbientColor = ColorFromLightSettingsRecord(
            *record, debug.AmbientColorOffset, debug.AmbientColorSource);
        debug.DiffuseColor = ColorFromLightSettingsRecord(
            *record, debug.DiffuseColorOffset, debug.DiffuseColorSource);
    } else {
        if (debug.AmbientGroupIndex < 0 || debug.DiffuseGroupIndex < 0 ||
            static_cast<size_t>(debug.AmbientGroupIndex) >= record->ByteGroups.size() ||
            static_cast<size_t>(debug.DiffuseGroupIndex) >= record->ByteGroups.size()) {
            return debug;
        }
        debug.AmbientColor = ColorFromPicaByteGroup(record->ByteGroups[debug.AmbientGroupIndex]);
        debug.DiffuseColor = ColorFromPicaByteGroup(record->ByteGroups[debug.DiffuseGroupIndex]);
    }
    const auto formula = JsonStringValue(*mode, "debug_color_formula");
    if (formula != "clamp((ambient.rgb + diffuse.rgb) * 0.5)") {
        return debug;
    }
    debug.DebugColor = CombinePicaLightingDebugColor(debug.AmbientColor, debug.DiffuseColor, formula);
    debug.Available = true;
    return debug;
}

void ApplyOot3dNativePicaLightingDebug(const Oot3dNativeDemoScene& scene,
                                       Oot3dNativeDemoRenderScene& renderScene) {
    renderScene.PicaLightingDebug = BuildOot3dNativePicaLightingDebugState(scene);
    if (!renderScene.PicaLightingDebug.Available) {
        return;
    }

    ApplyPicaLightingDebugToModel(renderScene.PicaLightingDebug, renderScene.Room);
    for (auto& roomModel : renderScene.AdditionalRoomModels) {
        ApplyPicaLightingDebugToModel(renderScene.PicaLightingDebug, roomModel);
    }
    ApplyPicaLightingDebugToModel(renderScene.PicaLightingDebug, renderScene.Link);
    for (auto& actorVisual : renderScene.ActorVisuals) {
        ApplyPicaLightingDebugToModel(renderScene.PicaLightingDebug, actorVisual);
    }

    renderScene.PicaLightingDebug.AppliedBatchCount = 0;
    renderScene.PicaLightingDebug.AppliedVertexCount = 0;
    CountPicaLightingDebugModelApplication(renderScene.PicaLightingDebug, renderScene.Room);
    for (const auto& roomModel : renderScene.AdditionalRoomModels) {
        CountPicaLightingDebugModelApplication(renderScene.PicaLightingDebug, roomModel);
    }
    CountPicaLightingDebugModelApplication(renderScene.PicaLightingDebug, renderScene.Link);
    for (const auto& actorVisual : renderScene.ActorVisuals) {
        CountPicaLightingDebugModelApplication(renderScene.PicaLightingDebug, actorVisual);
    }
}

Oot3dNativeDemoRenderScene BuildOot3dNativeDemoPicaLightingDebugRenderScene(const Oot3dNativeDemoScene& scene) {
    return BuildOot3dNativeDemoPicaLightingDebugRenderScene(scene, scene.LinkStandingPose, scene.LinkSkinTransforms);
}

Oot3dNativeDemoRenderScene BuildOot3dNativeDemoPicaLightingDebugRenderScene(
    const Oot3dNativeDemoScene& scene,
    const CsabPose& linkPose,
    const std::vector<Matrix4f>& linkSkinTransforms) {
    auto renderScene = BuildOot3dNativeDemoRenderScene(scene, linkPose, linkSkinTransforms);
    ApplyOot3dNativePicaLightingDebug(scene, renderScene);
    return renderScene;
}

Matrix4f BuildOot3dNativeRenderScaleTranslateTransform(double scale, Oot3dDemoVec3 translation) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    matrix.M[0][0] = static_cast<float>(scale);
    matrix.M[1][1] = static_cast<float>(scale);
    matrix.M[2][2] = static_cast<float>(scale);
    matrix.M[0][3] = static_cast<float>(translation.X);
    matrix.M[1][3] = static_cast<float>(translation.Y);
    matrix.M[2][3] = static_cast<float>(translation.Z);
    return matrix;
}

Matrix4f BuildOot3dNativeRenderScaleYawTranslateTransform(double scale, double yawRadians,
                                                          Oot3dDemoVec3 translation) {
    Matrix4f matrix = Oot3dNativeRenderIdentityMatrix();
    const double cosYaw = std::cos(yawRadians);
    const double sinYaw = std::sin(yawRadians);
    matrix.M[0][0] = static_cast<float>(cosYaw * scale);
    matrix.M[0][2] = static_cast<float>(sinYaw * scale);
    matrix.M[1][1] = static_cast<float>(scale);
    matrix.M[2][0] = static_cast<float>(-sinYaw * scale);
    matrix.M[2][2] = static_cast<float>(cosYaw * scale);
    matrix.M[0][3] = static_cast<float>(translation.X);
    matrix.M[1][3] = static_cast<float>(translation.Y);
    matrix.M[2][3] = static_cast<float>(translation.Z);
    return matrix;
}

Matrix4f BuildOot3dNativeRenderActorEntryTransform(double scale, Oot3dDemoVec3 rotationS16,
                                                   Oot3dDemoVec3 translation) {
    const double x = rotationS16.X * kOot3dS16AngleToRadians;
    const double y = rotationS16.Y * kOot3dS16AngleToRadians;
    const double z = rotationS16.Z * kOot3dS16AngleToRadians;
    const auto rotateZyx = MultiplyMatrices(RotateZMatrix(z), MultiplyMatrices(RotateYMatrix(y), RotateXMatrix(x)));
    return MultiplyMatrices(TranslateMatrix(translation), MultiplyMatrices(rotateZyx, ScaleMatrix(scale)));
}

Oot3dDemoBounds Oot3dNativeRenderTransformBounds(const Oot3dDemoBounds& bounds, const Matrix4f& transform) {
    Oot3dDemoBounds transformed;
    if (!bounds.Valid) {
        return transformed;
    }

    for (size_t x = 0; x < 2; ++x) {
        for (size_t y = 0; y < 2; ++y) {
            for (size_t z = 0; z < 2; ++z) {
                const Oot3dDemoVec3 point{
                    x == 0 ? bounds.Min.X : bounds.Max.X,
                    y == 0 ? bounds.Min.Y : bounds.Max.Y,
                    z == 0 ? bounds.Min.Z : bounds.Max.Z,
                };
                const double tx = transform.M[0][0] * point.X + transform.M[0][1] * point.Y +
                                  transform.M[0][2] * point.Z + transform.M[0][3];
                const double ty = transform.M[1][0] * point.X + transform.M[1][1] * point.Y +
                                  transform.M[1][2] * point.Z + transform.M[1][3];
                const double tz = transform.M[2][0] * point.X + transform.M[2][1] * point.Y +
                                  transform.M[2][2] * point.Z + transform.M[2][3];
                const double tw = transform.M[3][0] * point.X + transform.M[3][1] * point.Y +
                                  transform.M[3][2] * point.Z + transform.M[3][3];
                const double invW = std::abs(tw) > 0.000001 ? 1.0 / tw : 1.0;
                ExpandBoundsByPoint(transformed, { tx * invW, ty * invW, tz * invW });
            }
        }
    }
    return transformed;
}

Oot3dDemoVec3 NativeRenderBoundsCenter(const Oot3dDemoBounds& bounds) {
    return {
        (bounds.Min.X + bounds.Max.X) * 0.5,
        (bounds.Min.Y + bounds.Max.Y) * 0.5,
        (bounds.Min.Z + bounds.Max.Z) * 0.5,
    };
}

Oot3dNativeKankyoRuntime28CProjectionSample NativeRenderProjectKankyoLensDiagnosticPoint(
    const Matrix4f& viewProjectionMatrix, std::string sourceKind,
    Oot3dDemoVec3 worldPosition, const Oot3dNativeKankyoLensEffectState& lensEffect) {
    Oot3dNativeKankyoRuntime28CProjectionSample sample;
    sample.SourceKind = std::move(sourceKind);
    sample.WorldPosition = worldPosition;
    sample.ClipX = static_cast<float>(
        viewProjectionMatrix.M[0][0] * worldPosition.X +
        viewProjectionMatrix.M[0][1] * worldPosition.Y +
        viewProjectionMatrix.M[0][2] * worldPosition.Z +
        viewProjectionMatrix.M[0][3]);
    sample.ClipY = static_cast<float>(
        viewProjectionMatrix.M[1][0] * worldPosition.X +
        viewProjectionMatrix.M[1][1] * worldPosition.Y +
        viewProjectionMatrix.M[1][2] * worldPosition.Z +
        viewProjectionMatrix.M[1][3]);
    sample.ClipZ = static_cast<float>(
        viewProjectionMatrix.M[2][0] * worldPosition.X +
        viewProjectionMatrix.M[2][1] * worldPosition.Y +
        viewProjectionMatrix.M[2][2] * worldPosition.Z +
        viewProjectionMatrix.M[2][3]);
    sample.ClipW = static_cast<float>(
        viewProjectionMatrix.M[3][0] * worldPosition.X +
        viewProjectionMatrix.M[3][1] * worldPosition.Y +
        viewProjectionMatrix.M[3][2] * worldPosition.Z +
        viewProjectionMatrix.M[3][3]);

    sample.NativeWClampApplied = sample.ClipW < 1.0f;
    sample.InverseW = sample.NativeWClampApplied ? 1.0f : 1.0f / sample.ClipW;
    const auto nativeS16ScreenFloat = [](float value) {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return static_cast<float>(static_cast<int16_t>(static_cast<int32_t>(value)));
    };
    sample.ScreenX = nativeS16ScreenFloat(
        lensEffect.LensPositionScreenCenterX +
        sample.ClipX * sample.InverseW * lensEffect.LensPositionProjectionScaleX);
    sample.ScreenY = nativeS16ScreenFloat(
        lensEffect.LensPositionProjectionBaseY +
        sample.ClipY * sample.InverseW * lensEffect.LensPositionProjectionScaleY);
    return sample;
}

std::vector<Oot3dNativeKankyoRuntime28CProjectionSample> NativeRenderBuildKankyoLensProjectionSamples(
    const Oot3dNativeDemoRenderScene& scene, const Matrix4f& viewProjectionMatrix,
    const Oot3dNativeKankyoLensEffectState& lensEffect) {
    std::vector<Oot3dNativeKankyoRuntime28CProjectionSample> samples;
    if (scene.Bounds.Valid) {
        samples.push_back(NativeRenderProjectKankyoLensDiagnosticPoint(
            viewProjectionMatrix, "current_render_scene_bounds_center",
            NativeRenderBoundsCenter(scene.Bounds), lensEffect));
    }
    const auto roomBounds = Oot3dNativeRenderModelWorldBounds(scene.Room);
    if (roomBounds.Valid) {
        samples.push_back(NativeRenderProjectKankyoLensDiagnosticPoint(
            viewProjectionMatrix, "current_room_bounds_center",
            NativeRenderBoundsCenter(roomBounds), lensEffect));
    }
    for (const auto& roomModel : scene.AdditionalRoomModels) {
        const auto bounds = Oot3dNativeRenderModelWorldBounds(roomModel);
        if (bounds.Valid) {
            samples.push_back(NativeRenderProjectKankyoLensDiagnosticPoint(
                viewProjectionMatrix, "additional_room_bounds_center",
                NativeRenderBoundsCenter(bounds), lensEffect));
        }
    }
    const auto linkBounds = Oot3dNativeRenderModelWorldBounds(scene.Link);
    if (linkBounds.Valid) {
        samples.push_back(NativeRenderProjectKankyoLensDiagnosticPoint(
            viewProjectionMatrix, "current_link_bounds_center",
            NativeRenderBoundsCenter(linkBounds), lensEffect));
    }
    return samples;
}

std::vector<Oot3dNativeKankyoRuntime28CPositionRecord>
NativeRenderBuildKankyoLensRuntimePositionRecords(
    const Oot3dNativeKankyoLensEffectState& lensEffect,
    const Oot3dNativeKankyoRuntime28CProjectionSample& sourceProjection,
    Oot3dDemoVec3 sourceWorldPosition, Oot3dDemoVec3 kankyoSourceOffset,
    uint32_t positionRecordStrideBytes) {
    std::vector<Oot3dNativeKankyoRuntime28CPositionRecord> records;
    if (positionRecordStrideBytes == 0) {
        return records;
    }

    const float directionX =
        (lensEffect.LensPositionScreenCenterX - sourceProjection.ScreenX) *
        lensEffect.LensPositionDirectionScale;
    const float directionY =
        (lensEffect.LensPositionScreenCenterY - sourceProjection.ScreenY) *
        lensEffect.LensPositionDirectionScale;
    const float deltaX = lensEffect.LensPositionScreenCenterX - sourceProjection.ScreenX;
    const float deltaY = lensEffect.LensPositionScreenCenterY - sourceProjection.ScreenY;
    const float deltaZ = lensEffect.LensPositionBaseZ;
    float distanceFactor =
        (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) *
        lensEffect.LensPositionDistanceLimitScale;
    if (!std::isfinite(distanceFactor)) {
        distanceFactor = 0.0f;
    }
    distanceFactor = std::clamp(distanceFactor, 0.0f, 1.0f);
    const float runtimeScaleMultiplier =
        static_cast<float>(lensEffect.LensPositionRuntimeScaleArgument) *
        lensEffect.LensPositionRuntimeScaleMultiplier;
    for (const auto& element : lensEffect.Elements) {
        const uint32_t visibleBatchIndex =
            static_cast<uint32_t>(std::min<size_t>(
                records.size(), std::numeric_limits<uint32_t>::max()));
        const float interpolatedScale =
            element.ScaleMin + (element.ScaleMax - element.ScaleMin) * distanceFactor;
        const float runtimeScale = interpolatedScale * runtimeScaleMultiplier;
        records.push_back({
            visibleBatchIndex,
            element.Index,
            element.Index * positionRecordStrideBytes,
            element.Offset,
            element.Depth,
            sourceWorldPosition,
            kankyoSourceOffset,
            {
                sourceProjection.ScreenX + directionX * element.Offset,
                sourceProjection.ScreenY + directionY * element.Offset,
                element.Depth,
            },
            sourceProjection,
            distanceFactor,
            interpolatedScale,
            runtimeScale,
            lensEffect.LensPositionRuntimeScaleArgument,
            lensEffect.LensPositionRuntimeScaleMultiplier,
        });
    }
    return records;
}

Vec3f NativeRenderTransformPosition(const Matrix4f& transform, const Vec3f& position) {
    const double x = transform.M[0][0] * position.X + transform.M[0][1] * position.Y +
                     transform.M[0][2] * position.Z + transform.M[0][3];
    const double y = transform.M[1][0] * position.X + transform.M[1][1] * position.Y +
                     transform.M[1][2] * position.Z + transform.M[1][3];
    const double z = transform.M[2][0] * position.X + transform.M[2][1] * position.Y +
                     transform.M[2][2] * position.Z + transform.M[2][3];
    const double w = transform.M[3][0] * position.X + transform.M[3][1] * position.Y +
                     transform.M[3][2] * position.Z + transform.M[3][3];
    const double inverseW = std::abs(w) > 0.000001 ? 1.0 / w : 1.0;
    return {
        static_cast<float>(x * inverseW),
        static_cast<float>(y * inverseW),
        static_cast<float>(z * inverseW),
    };
}

Vec3f NativeRenderCross(Vec3f lhs, Vec3f rhs) {
    return {
        lhs.Y * rhs.Z - lhs.Z * rhs.Y,
        lhs.Z * rhs.X - lhs.X * rhs.Z,
        lhs.X * rhs.Y - lhs.Y * rhs.X,
    };
}

std::optional<double> NativeRenderRayTriangleDistance(
    Vec3f origin, Vec3f direction, Vec3f a, Vec3f b, Vec3f c) {
    constexpr double epsilon = 0.000001;
    const Vec3f edge1{ b.X - a.X, b.Y - a.Y, b.Z - a.Z };
    const Vec3f edge2{ c.X - a.X, c.Y - a.Y, c.Z - a.Z };
    const Vec3f p = NativeRenderCross(direction, edge2);
    const double determinant = Dot(edge1, p);
    if (std::abs(determinant) <= epsilon) {
        return std::nullopt;
    }
    const double inverseDeterminant = 1.0 / determinant;
    const Vec3f t{ origin.X - a.X, origin.Y - a.Y, origin.Z - a.Z };
    const double u = Dot(t, p) * inverseDeterminant;
    if (u < 0.0 || u > 1.0) {
        return std::nullopt;
    }
    const Vec3f q = NativeRenderCross(t, edge1);
    const double v = Dot(direction, q) * inverseDeterminant;
    if (v < 0.0 || u + v > 1.0) {
        return std::nullopt;
    }
    const double distance = Dot(edge2, q) * inverseDeterminant;
    return distance > epsilon ? std::optional<double>{ distance } : std::nullopt;
}

struct NativeRenderKankyoLensOcclusionHit {
    bool Resolved = false;
    bool Occluded = false;
    float Distance = 0.0f;
    std::string ModelName;
};

NativeRenderKankyoLensOcclusionHit NativeRenderResolveKankyoLensSceneOcclusion(
    const Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 sourceWorldPosition) {
    NativeRenderKankyoLensOcclusionHit hit;
    const Vec3f origin{
        static_cast<float>(cameraEye.X),
        static_cast<float>(cameraEye.Y),
        static_cast<float>(cameraEye.Z),
    };
    const Vec3f source{
        static_cast<float>(sourceWorldPosition.X),
        static_cast<float>(sourceWorldPosition.Y),
        static_cast<float>(sourceWorldPosition.Z),
    };
    const Vec3f delta{ source.X - origin.X, source.Y - origin.Y, source.Z - origin.Z };
    const double sourceDistance = std::sqrt(Dot(delta, delta));
    if (!(sourceDistance > 0.000001)) {
        return hit;
    }
    const Vec3f direction{
        static_cast<float>(delta.X / sourceDistance),
        static_cast<float>(delta.Y / sourceDistance),
        static_cast<float>(delta.Z / sourceDistance),
    };

    double closestDistance = sourceDistance;
    const auto testModel = [&](const Oot3dNativeRenderModel& model) {
        for (const auto& batch : model.Batches) {
            if (!batch.Material.DepthWrite) {
                continue;
            }
            for (size_t vertex = 0; vertex + 2 < batch.Vertices.size(); vertex += 3) {
                const Vec3f a = NativeRenderTransformPosition(
                    model.ModelToWorld, batch.Vertices[vertex + 0].Position);
                const Vec3f b = NativeRenderTransformPosition(
                    model.ModelToWorld, batch.Vertices[vertex + 1].Position);
                const Vec3f c = NativeRenderTransformPosition(
                    model.ModelToWorld, batch.Vertices[vertex + 2].Position);
                const auto distance = NativeRenderRayTriangleDistance(
                    origin, direction, a, b, c);
                if (distance.has_value() && *distance < closestDistance &&
                    *distance < sourceDistance - 0.01) {
                    closestDistance = *distance;
                    hit.Occluded = true;
                    hit.Distance = static_cast<float>(*distance);
                    hit.ModelName = model.Name;
                }
            }
            hit.Resolved = true;
        }
    };

    testModel(scene.Room);
    for (const auto& roomModel : scene.AdditionalRoomModels) {
        testModel(roomModel);
    }
    for (const auto& actor : scene.ActorVisuals) {
        testModel(actor);
    }
    testModel(scene.Link);
    return hit;
}

void MaterializeOot3dNativeKankyoLensRuntimeViewProjection(
    Oot3dNativeDemoRenderScene& scene, const Matrix4f& viewProjectionMatrix,
    Oot3dDemoVec3 cameraEye, std::string sourceKind, std::string sourceStatus) {
    auto& lensEffect = scene.EnvironmentBackground.NativeKankyoLensEffect;
    if (!lensEffect.LensPositionViewProjectionSourceResolved ||
        !lensEffect.NativeLensPositionProducerResolved) {
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixMaterialized = false;
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixStatus =
            "native play+0x5bb4 source chain or FUN_002d97e4 producer constants are not resolved; "
            "runtime matrix was not materialized";
        return;
    }

    if (sourceKind.empty()) {
        sourceKind = "current_frame_native_view_projection_candidate";
    }
    if (sourceStatus.empty()) {
        sourceStatus =
            "current-frame view/projection matrix materialized as the native play+0x5bb4 candidate";
    }

    lensEffect.NativeLensPositionRuntimeViewProjectionMatrixMaterialized = true;
    lensEffect.NativeLensPositionRuntimeViewProjectionMatrixSourceKind = sourceKind;
    lensEffect.NativeLensPositionRuntimeViewProjectionMatrixStatus = sourceStatus;
    lensEffect.NativeLensPositionRuntimeViewProjectionMatrix = viewProjectionMatrix;
    lensEffect.NativeLensPositionProjectionSamples =
        NativeRenderBuildKankyoLensProjectionSamples(scene, viewProjectionMatrix, lensEffect);

    int activeAngle = -1;
    std::string activeAngleSource;
    if (scene.EnvironmentBackground.NativeKankyoScheduleActiveAngle >= 0) {
        activeAngle = scene.EnvironmentBackground.NativeKankyoScheduleActiveAngle & 0xFFFF;
        activeAngleSource = scene.EnvironmentBackground.NativeKankyoScheduleActiveAngleSource;
    } else if (scene.EnvironmentBackground.RuntimeEnvironmentTimeInputAvailable) {
        activeAngle = scene.EnvironmentBackground.RuntimeEnvironmentSkyboxTime;
        activeAngleSource = scene.EnvironmentBackground.RuntimeEnvironmentTimeSourceKind;
    }
    lensEffect.LensPositionPrimarySourceActiveAngle = activeAngle;
    lensEffect.LensPositionPrimarySourceActiveAngleSource = activeAngleSource;
    lensEffect.NativeLensPositionPrimarySourceBaseCamera = cameraEye;
    lensEffect.NativeLensPositionPrimarySourceKankyoOffset = {};
    lensEffect.NativeLensPositionPrimarySourceWorldPosition = cameraEye;
    lensEffect.NativeLensPositionPrimarySourceVectorResolved = false;
    lensEffect.NativeLensPositionRuntimeInputResolved = false;
    lensEffect.NativeLensPositionRuntimePositionRecords.clear();
    if (activeAngle < 0) {
        lensEffect.NativeLensPositionPrimarySourceVectorStatus =
            "native primary lens source vector requires the active kankyo angle from "
            "play+0x3194/0x3198/0x319c offset update";
        lensEffect.NativeLensPositionRuntimeInputStatus =
            "native Gameplay_Draw play+0x5bb4 matrix is materialized, but the active "
            "kankyo angle required by FUN_004594e0 is not resolved";
    } else {
        int angleMinusBias = (activeAngle - 0x8000) & 0xFFFF;
        if (angleMinusBias >= 0x8000) {
            angleMinusBias -= 0x10000;
        }
        const double radians =
            static_cast<double>(angleMinusBias) * kOot3dS16AngleToRadians;
        lensEffect.NativeLensPositionPrimarySourceKankyoOffset = {
            static_cast<float>(
                std::sin(radians) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceScaleX) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceRadius)),
            static_cast<float>(
                std::cos(radians) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceScaleY) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceRadius)),
            static_cast<float>(
                std::cos(radians) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceScaleZ) *
                static_cast<double>(lensEffect.LensPositionPrimarySourceRadius)),
        };
        lensEffect.NativeLensPositionPrimarySourceWorldPosition = {
            cameraEye.X + lensEffect.NativeLensPositionPrimarySourceKankyoOffset.X,
            cameraEye.Y + lensEffect.NativeLensPositionPrimarySourceKankyoOffset.Y,
            cameraEye.Z + lensEffect.NativeLensPositionPrimarySourceKankyoOffset.Z,
        };
        auto sourceProjection = NativeRenderProjectKankyoLensDiagnosticPoint(
            viewProjectionMatrix,
            "native_primary_lens_source_play_0x1b8_plus_kankyo_0x3194",
            lensEffect.NativeLensPositionPrimarySourceWorldPosition, lensEffect);
        lensEffect.NativeLensPositionProjectionSamples.push_back(sourceProjection);

        lensEffect.NativeLensVisibilitySourceInViewport =
            lensEffect.NativeLensVisibilityScreenGateResolved &&
            std::isfinite(sourceProjection.ScreenX) &&
            std::isfinite(sourceProjection.ScreenY) &&
            std::isfinite(sourceProjection.ClipZ) &&
            sourceProjection.ScreenX >= 0.0f &&
            sourceProjection.ScreenX < lensEffect.LensVisibilityScreenMaxX &&
            sourceProjection.ScreenY >= 0.0f &&
            sourceProjection.ScreenY < lensEffect.LensVisibilityScreenMaxY &&
            sourceProjection.ClipZ >= 0.0f;
        const auto occlusion =
            lensEffect.NativeLensVisibilitySourceInViewport
                ? NativeRenderResolveKankyoLensSceneOcclusion(
                      scene, cameraEye,
                      lensEffect.NativeLensPositionPrimarySourceWorldPosition)
                : NativeRenderKankyoLensOcclusionHit{};
        lensEffect.NativeLensVisibilitySceneOcclusionResolved =
            !lensEffect.NativeLensVisibilitySourceInViewport || occlusion.Resolved;
        lensEffect.NativeLensVisibilitySourceOccluded =
            lensEffect.NativeLensVisibilitySourceInViewport && occlusion.Occluded;
        lensEffect.LensVisibilityOccluderDistance = occlusion.Distance;
        lensEffect.LensVisibilityOccluderModel = occlusion.ModelName;
        lensEffect.LensVisibilityTarget =
            lensEffect.NativeLensVisibilitySourceInViewport &&
                    !lensEffect.NativeLensVisibilitySourceOccluded
                ? lensEffect.LensVisibilityVisibleTarget
                : 0.0f;
        if (!lensEffect.NativeLensVisibilityScreenGateResolved) {
            lensEffect.NativeLensVisibilitySourceStatus =
                "native FUN_002d97e4 screen/depth visibility constants are incomplete";
        } else if (!lensEffect.NativeLensVisibilitySourceInViewport) {
            lensEffect.NativeLensVisibilitySourceStatus =
                "native FUN_002d97e4 source projection is outside 400x240 or has negative depth";
        } else if (lensEffect.NativeLensVisibilitySourceOccluded) {
            lensEffect.NativeLensVisibilitySourceStatus =
                "native source visibility predicate is blocked by depth-writing scene geometry";
        } else {
            lensEffect.NativeLensVisibilitySourceStatus =
                "native FUN_002d97e4 source projection is visible and unobstructed";
        }

        const uint32_t positionStride =
            lensEffect.PrimitiveBackendInput.Runtime28CPositionRecordStrideBytes;
        lensEffect.NativeLensPositionRuntimePositionRecords =
            NativeRenderBuildKankyoLensRuntimePositionRecords(
                lensEffect, sourceProjection,
                lensEffect.NativeLensPositionPrimarySourceWorldPosition,
                lensEffect.NativeLensPositionPrimarySourceKankyoOffset,
                positionStride);
        lensEffect.NativeLensPositionPrimarySourceVectorResolved =
            positionStride != 0 &&
            !lensEffect.NativeLensPositionRuntimePositionRecords.empty();
        lensEffect.NativeLensPositionRuntimeInputResolved =
            lensEffect.NativeLensPositionPrimarySourceVectorResolved &&
            lensEffect.NativeLensPositionRuntimePositionRecords.size() ==
                lensEffect.Elements.size();
        lensEffect.NativeLensPositionPrimarySourceVectorStatus =
            lensEffect.NativeLensPositionPrimarySourceVectorResolved
                ? "native primary lens source vector materialized from Gameplay_Draw "
                  "callsite 0x002e2d28: camera eye play+0x1b8/0x1bc/0x1c0 plus "
                  "FUN_004594e0 kankyo offsets play+0x3194/0x3198/0x319c"
                : "native primary lens source vector was identified, but runtime "
                  "position-record stride or Lens* elements are incomplete";
        lensEffect.NativeLensPositionRuntimeInputStatus =
            lensEffect.NativeLensPositionRuntimeInputResolved
                ? "native Gameplay_Draw play+0x5bb4 view/projection matrix, "
                  "FUN_0045945c source vector, and FUN_002d97e4 13 runtime center "
                  "records are materialized from code.bin constants and Lens* TBD records"
                : "native Gameplay_Draw play+0x5bb4 matrix is materialized, but "
                  "FUN_002d97e4 runtime center records are incomplete";
    }

    auto& input = lensEffect.PrimitiveBackendInput;
    input.Runtime28CPositionViewProjectionRuntimeMatrixMaterialized =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixMaterialized;
    input.Runtime28CPositionViewProjectionRuntimeMatrixSourceKind =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixSourceKind;
    input.Runtime28CPositionViewProjectionRuntimeMatrixStatus =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrixStatus;
    input.Runtime28CPositionViewProjectionRuntimeMatrix =
        lensEffect.NativeLensPositionRuntimeViewProjectionMatrix;
    input.Runtime28CPositionProjectionSamples =
        lensEffect.NativeLensPositionProjectionSamples;
    input.Runtime28CPositionRecords =
        lensEffect.NativeLensPositionRuntimePositionRecords;
    input.Runtime28CPositionRuntimeInputResolved =
        lensEffect.NativeLensPositionRuntimeInputResolved;
    input.Runtime28CPositionRuntimeScaleResolved =
        lensEffect.NativeLensPositionRuntimeScaleResolved;
    input.Runtime28CPositionRuntimeScaleArgument =
        lensEffect.LensPositionRuntimeScaleArgument;
    input.Runtime28CPositionRuntimeScaleMultiplierAddress =
        lensEffect.LensPositionRuntimeScaleMultiplierAddress;
    input.Runtime28CPositionRuntimeScaleMultiplier =
        lensEffect.LensPositionRuntimeScaleMultiplier;
    input.Runtime28CPositionRuntimeInputBlockedReason =
        lensEffect.NativeLensPositionRuntimeInputResolved
            ? ""
            : lensEffect.NativeLensPositionRuntimeInputStatus;
    input.NativeLensVisibilityScreenGateResolved =
        lensEffect.NativeLensVisibilityScreenGateResolved;
    input.NativeLensVisibilitySceneOcclusionResolved =
        lensEffect.NativeLensVisibilitySceneOcclusionResolved;
    input.NativeLensVisibilitySourceInViewport =
        lensEffect.NativeLensVisibilitySourceInViewport;
    input.NativeLensVisibilitySourceOccluded =
        lensEffect.NativeLensVisibilitySourceOccluded;
    input.NativeLensVisibilityTarget = lensEffect.LensVisibilityTarget;
    input.NativeLensVisibilityVisibleTarget =
        lensEffect.LensVisibilityVisibleTarget;
    input.NativeLensVisibilityScale = lensEffect.LensVisibilityScale;
    input.NativeLensVisibilityMaxStep = lensEffect.LensVisibilityMaxStep;
    input.NativeLensVisibilityMinStep = lensEffect.LensVisibilityMinStep;
    input.NativeLensVisibilityScreenMaxX = lensEffect.LensVisibilityScreenMaxX;
    input.NativeLensVisibilityScreenMaxY = lensEffect.LensVisibilityScreenMaxY;
    input.NativeLensVisibilityOccluderDistance =
        lensEffect.LensVisibilityOccluderDistance;
    input.NativeLensVisibilityOccluderModel =
        lensEffect.LensVisibilityOccluderModel;
    input.NativeLensVisibilitySourceStatus =
        lensEffect.NativeLensVisibilitySourceStatus;
    input.ReadyForBackendRender =
        input.ReadyForBackendInput &&
        input.Runtime28CPositionRuntimeInputResolved &&
        input.Runtime28CPositionRuntimeScaleResolved &&
        input.NativePicaAlphaBlendSemanticsResolved &&
        input.VisibleBackendSubmitResolved;
    if (input.ReadyForBackendRender) {
        input.SourceStatus =
            "native kankyo primitive packet, quad-batch lanes, runtime centers, "
            "output-merger blend, and host visible backend submit are resolved";
        input.VisibleBackendBlockedReason.clear();
    } else if (input.ReadyForBackendInput && !input.Runtime28CPositionRuntimeInputResolved) {
        input.SourceStatus =
            "native kankyo primitive packet, quad-batch backend input, lens position constants, "
            "and current-frame play+0x5bb4 view/projection materialization are resolved; exact "
            "runtime center records still require native source-vector materialization before "
            "visible backend submit";
        input.VisibleBackendBlockedReason =
            "native_lens_position_runtime_source_vectors_not_materialized";
    } else if (input.ReadyForBackendInput &&
               input.Runtime28CPositionRuntimeInputResolved &&
               !input.NativePicaAlphaBlendSemanticsResolved) {
        input.SourceStatus =
            "native kankyo primitive packet, quad-batch backend input, lens position "
            "constants, current-frame play+0x5bb4 matrix, and FUN_002d97e4 runtime center "
            "records are resolved; visible backend submit is waiting for decoded CTXB "
            "texture input before native output-merger blend can be applied";
        input.VisibleBackendBlockedReason =
            "native_kankyo_primitive_decoded_texture_input_required_for_output_merger_blend";
    } else if (input.ReadyForBackendInput &&
               input.Runtime28CPositionRuntimeInputResolved &&
               !input.VisibleBackendSubmitResolved) {
        input.SourceStatus =
            "native kankyo primitive packet, quad-batch backend input, lens position "
            "constants, current-frame play+0x5bb4 matrix, and FUN_002d97e4 runtime "
            "center records are resolved; final visible backend submit is still pending";
        input.VisibleBackendBlockedReason =
            "native_effect_primitive_submit_backend_not_yet_bound_to_opengl_renderer";
    }
}

void MaterializeOot3dNativeKankyoMoonRuntime(
    Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 cameraTarget, Oot3dDemoVec3 cameraUp) {
    auto& background = scene.EnvironmentBackground;
    auto& moon = background.NativeKankyoMoon;
    scene.MoonModels.clear();
    moon.NativeVisibleBackendSubmitResolved = false;
    moon.RuntimeTransformResolved = false;
    for (auto& layer : moon.Layers) {
        layer.RuntimeMaterialized = false;
    }
    if (!moon.ReadyForBackendInput || moon.Layers.size() != moon.LayerCount) {
        return;
    }

    int activeAngle = -1;
    if (background.RuntimeEnvironmentTimeInputAvailable) {
        activeAngle = background.RuntimeEnvironmentSkyboxTime;
    } else if (background.NativeKankyoScheduleActiveAngle >= 0) {
        activeAngle = background.NativeKankyoScheduleActiveAngle & 0xFFFF;
    }
    if (activeAngle < 0) {
        moon.SourceStatus =
            "native moon textures and init route are resolved, but the current environment clock "
            "angle required by FUN_004594e0 is unavailable";
        return;
    }

    const auto subtract = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{ lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z };
    };
    const auto add = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{ lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z };
    };
    const auto scale = [](Oot3dDemoVec3 value, float amount) {
        return Oot3dDemoVec3{ value.X * amount, value.Y * amount, value.Z * amount };
    };
    const auto cross = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{
            lhs.Y * rhs.Z - lhs.Z * rhs.Y,
            lhs.Z * rhs.X - lhs.X * rhs.Z,
            lhs.X * rhs.Y - lhs.Y * rhs.X,
        };
    };
    const auto normalize = [](Oot3dDemoVec3 value) {
        const double length = std::sqrt(
            static_cast<double>(value.X) * value.X +
            static_cast<double>(value.Y) * value.Y +
            static_cast<double>(value.Z) * value.Z);
        if (!(length > std::numeric_limits<double>::epsilon())) {
            return Oot3dDemoVec3{};
        }
        const float inverse = static_cast<float>(1.0 / length);
        return Oot3dDemoVec3{ value.X * inverse, value.Y * inverse, value.Z * inverse };
    };

    int signedAngle = (activeAngle - 0x8000) & 0xFFFF;
    if (signedAngle >= 0x8000) {
        signedAngle -= 0x10000;
    }
    const double radians = static_cast<double>(signedAngle) * kOot3dS16AngleToRadians;
    const Oot3dDemoVec3 celestialVector = {
        static_cast<float>(std::sin(radians) * -120.0 * 25.0),
        static_cast<float>(std::cos(radians) * 120.0 * 25.0),
        static_cast<float>(std::cos(radians) * 20.0 * 25.0),
    };
    const Oot3dDemoVec3 moonDirection = scale(celestialVector, -1.0f);
    const Oot3dDemoVec3 directionNormal = normalize(moonDirection);
    Oot3dDemoVec3 forward = normalize(subtract(cameraTarget, cameraEye));
    Oot3dDemoVec3 right = normalize(cross(forward, cameraUp));
    Oot3dDemoVec3 billboardUp = normalize(cross(right, forward));
    if (right.X == 0.0f && right.Y == 0.0f && right.Z == 0.0f) {
        right = { 1.0f, 0.0f, 0.0f };
    }
    if (billboardUp.X == 0.0f && billboardUp.Y == 0.0f && billboardUp.Z == 0.0f) {
        billboardUp = { 0.0f, 1.0f, 0.0f };
    }

    const Oot3dDemoVec3 baseCenter = add(cameraEye, moonDirection);
    constexpr std::array<uint32_t, 3> submitOrder = { 1, 0, 2 };
    constexpr std::array<float, 3> layerScales = { 640.0f, 1280.0f, 1280.0f };
    constexpr std::array<float, 3> uvMaxima = { 1.0f, 2.0f, 2.0f };
    constexpr std::array<float, 3> directionOffsets = { 0.0f, 100.0f, -100.0f };

    for (const uint32_t layerIndex : submitOrder) {
        auto& layer = moon.Layers[layerIndex];
        const auto* texture = NativeRenderFindKankyoExtraCtxbTexture(background, layer.TextureName);
        if (texture == nullptr || !texture->Rgba8Decoded || texture->Rgba8.empty()) {
            scene.MoonModels.clear();
            moon.SourceStatus =
                "native moon runtime transform is resolved, but a decoded CTXB layer payload is missing";
            return;
        }

        const Oot3dDemoVec3 center = add(baseCenter, scale(directionNormal, directionOffsets[layerIndex]));
        const float billboardScale = layerScales[layerIndex];
        const float billboardHalfExtent = billboardScale * layer.GeometryTemplateHalfExtent;
        const Oot3dDemoVec3 rightExtent = scale(right, billboardHalfExtent);
        const Oot3dDemoVec3 upExtent = scale(billboardUp, billboardHalfExtent);
        const std::array<Oot3dDemoVec3, 4> corners = {
            subtract(subtract(center, rightExtent), upExtent),
            add(subtract(center, rightExtent), upExtent),
            subtract(add(center, rightExtent), upExtent),
            add(add(center, rightExtent), upExtent),
        };
        const float uvMax = uvMaxima[layerIndex];
        const std::array<Vec2f, 4> uvs = {
            Vec2f{ 0.0f, uvMax }, Vec2f{ 0.0f, 0.0f },
            Vec2f{ uvMax, uvMax }, Vec2f{ uvMax, 0.0f },
        };
        constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 2, 1, 3 };

        Oot3dNativeRenderModel model;
        model.Source = background.NativeKankyoArchivePath.string() + "!" + layer.TextureName;
        model.Name = "kankyo:moon:" + layer.TextureName;
        model.Textures.push_back(*texture);
        Oot3dNativeRenderBatch batch;
        batch.Material.Textured = true;
        batch.Material.TextureIndex = 0;
        batch.Material.TextureBindingSource = "oot3d_code_bin_FUN_002d4f10_ctxb_layer";
        batch.Material.TextureHasNativeAlpha = texture->HasNativeAlpha;
        batch.Material.NativeSamplerStateDecoded = true;
        batch.Material.NativeSamplerMinFilter = static_cast<uint16_t>(layer.MinMagFilter);
        batch.Material.NativeSamplerMagFilter = static_cast<uint16_t>(layer.MinMagFilter);
        batch.Material.NativeSamplerWrapS = static_cast<uint16_t>(layer.WrapS);
        batch.Material.NativeSamplerWrapT = static_cast<uint16_t>(layer.WrapT);
        batch.Material.NativeSamplerStateSource = "oot3d_code_bin_FUN_002d4f10_sampler_state";
        batch.Material.CmbCullFace = 3;
        batch.Material.PicaCullMode = Oot3dNativePicaCullMode::KeepAll;
        batch.Material.DepthTest = false;
        batch.Material.DepthWrite = false;
        batch.Material.NativePicaFogOverrideDecoded = true;
        batch.Material.NativePicaFogEnabled = false;
        batch.Material.NativePicaFogOverrideSource =
            "oot3d_pica_moon_draw_register_snapshot_GPREG_TEXENV_UPDATE_BUFFER_fog_disabled";
        batch.Material.NativeRenderStateDecoded = true;
        batch.Material.NativeBlendStateEnabled = true;
        batch.Material.NativeBlendStateSupported = true;
        batch.Material.NativeBlendFactorsSupported = true;
        batch.Material.NativeBlendEquationSupported = true;
        batch.Material.BlendMode = 1;
        batch.Material.BlendSrc = 0x0302;
        batch.Material.BlendDst = layerIndex == 0 ? 0x0303 : 0x0001;
        batch.Material.BlendEquation = 0x8006;
        batch.Material.ColorBlendSrc = 0x0302;
        batch.Material.ColorBlendDst = layerIndex == 0 ? 0x0303 : 0x0001;
        batch.Material.ColorBlendEquation = 0x8006;
        batch.Material.AlphaTest = layerIndex == 0;
        batch.Material.AlphaFunction = layerIndex == 0 ? 0x0204 : 0;
        batch.Material.AlphaReference = 0;
        for (const uint32_t index : indices) {
            Oot3dNativeRenderVertex vertex;
            vertex.Position = {
                static_cast<float>(corners[index].X),
                static_cast<float>(corners[index].Y),
                static_cast<float>(corners[index].Z),
            };
            vertex.Normal = {
                static_cast<float>(-forward.X),
                static_cast<float>(-forward.Y),
                static_cast<float>(-forward.Z),
            };
            vertex.Uv0 = uvs[index];
            vertex.NativeSourceUv0 = uvs[index];
            vertex.NativeSourceUv0Available = true;
            vertex.Color = { 255, 255, 255, 255 };
            vertex.NativeColorAvailable = true;
            batch.Vertices.push_back(vertex);
            ExpandBounds(model.LocalBounds, vertex.Position);
        }
        model.Batches.push_back(std::move(batch));
        model.Bounds = Oot3dNativeRenderModelWorldBounds(model);
        scene.MoonModels.push_back(std::move(model));

        layer.RuntimeMaterialized = true;
        layer.RuntimeWorldCenter = center;
        layer.RuntimeScale = billboardScale;
        layer.RuntimeUvMax = uvMax;
        layer.RuntimeAdditiveBlend = layerIndex != 0;
        layer.RuntimeAlphaTest = layerIndex == 0;
    }

    moon.RuntimeTransformResolved = scene.MoonModels.size() == moon.LayerCount;
    moon.NativeVisibleBackendSubmitResolved = moon.RuntimeTransformResolved;
    moon.RuntimeActiveAngle = static_cast<uint16_t>(activeAngle);
    moon.RuntimeCelestialVector = celestialVector;
    moon.RuntimeMoonDirection = moonDirection;
    moon.RuntimeTransformSource =
        "oot3d_code_bin_FUN_004594e0_to_FUN_0047d600_to_FUN_002ce904";
    moon.SourceStatus =
        "three native moon CTXB layers are materialized in FUN_002ce904 submit order with "
        "camera-facing transforms, clock-derived position, native scales, UV ranges, samplers, "
        "depth state, alpha test, and PICA output-merger blend";
}

void NativeRenderMaterializeKankyoSunHaloRuntime(
    Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 cameraTarget, Oot3dDemoVec3 cameraUp) {
    auto& background = scene.EnvironmentBackground;
    auto& halo = background.NativeKankyoSunHalo;
    scene.EnvironmentModels.erase(
        std::remove_if(scene.EnvironmentModels.begin(), scene.EnvironmentModels.end(),
                       [](const auto& model) {
                           return model.NativeKankyoRole ==
                                  Oot3dNativeKankyoModelRole::SunHalo;
                       }),
        scene.EnvironmentModels.end());
    halo.RuntimeTransformResolved = false;
    halo.NativeVisibleBackendSubmitResolved = false;
    halo.RuntimeLayerAlphas.clear();
    halo.RuntimeLayerTextureNames.clear();
    if (!halo.ReadyForBackendInput) {
        return;
    }

    int activeAngle = -1;
    if (background.RuntimeEnvironmentTimeInputAvailable) {
        activeAngle = background.RuntimeEnvironmentSkyboxTime;
    } else if (background.NativeKankyoScheduleActiveAngle >= 0) {
        activeAngle = background.NativeKankyoScheduleActiveAngle & 0xFFFF;
    }
    if (activeAngle < 0) {
        halo.SourceStatus =
            "native profile-selected sun inputs are resolved, but the active kankyo clock angle is unavailable";
        return;
    }
    const auto subtract = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{ lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z };
    };
    const auto add = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{ lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z };
    };
    const auto scale = [](Oot3dDemoVec3 value, double amount) {
        return Oot3dDemoVec3{ value.X * amount, value.Y * amount, value.Z * amount };
    };
    const auto cross = [](Oot3dDemoVec3 lhs, Oot3dDemoVec3 rhs) {
        return Oot3dDemoVec3{
            lhs.Y * rhs.Z - lhs.Z * rhs.Y,
            lhs.Z * rhs.X - lhs.X * rhs.Z,
            lhs.X * rhs.Y - lhs.Y * rhs.X,
        };
    };
    const auto normalize = [](Oot3dDemoVec3 value) {
        const double length = std::sqrt(
            value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        if (!(length > std::numeric_limits<double>::epsilon())) {
            return Oot3dDemoVec3{};
        }
        return Oot3dDemoVec3{
            value.X / length, value.Y / length, value.Z / length,
        };
    };

    int signedAngle = (activeAngle - 0x8000) & 0xFFFF;
    if (signedAngle >= 0x8000) {
        signedAngle -= 0x10000;
    }
    const double radians = static_cast<double>(signedAngle) * kOot3dS16AngleToRadians;
    halo.RuntimeCelestialVector = {
        std::sin(radians) * static_cast<double>(halo.CelestialScaleX) *
            static_cast<double>(halo.CelestialRadius),
        std::cos(radians) * static_cast<double>(halo.CelestialScaleY) *
            static_cast<double>(halo.CelestialRadius),
        std::cos(radians) * static_cast<double>(halo.CelestialScaleZ) *
            static_cast<double>(halo.CelestialRadius),
    };
    halo.RuntimeWorldBasePosition = add(
        cameraEye, scale(halo.RuntimeCelestialVector, halo.PositionScale));
    halo.RuntimeActiveAngle = static_cast<uint16_t>(activeAngle);

    Oot3dDemoVec3 forward = normalize(subtract(cameraTarget, cameraEye));
    Oot3dDemoVec3 right = normalize(cross(forward, cameraUp));
    Oot3dDemoVec3 billboardUp = normalize(cross(right, forward));
    if (right.X == 0.0 && right.Y == 0.0 && right.Z == 0.0) {
        right = { 1.0, 0.0, 0.0 };
    }
    if (billboardUp.X == 0.0 && billboardUp.Y == 0.0 && billboardUp.Z == 0.0) {
        billboardUp = { 0.0, 1.0, 0.0 };
    }
    const Oot3dDemoVec3 rightExtent = scale(right, halo.BillboardScale);
    const Oot3dDemoVec3 upExtent = scale(billboardUp, halo.BillboardScale);
    const std::array<Oot3dDemoVec3, 4> corners = {
        add(halo.RuntimeWorldBasePosition, rightExtent),
        add(add(halo.RuntimeWorldBasePosition, rightExtent), upExtent),
        halo.RuntimeWorldBasePosition,
        add(halo.RuntimeWorldBasePosition, upExtent),
    };
    constexpr std::array<Vec2f, 4> uvs = {
        Vec2f{ 0.0f, 1.0f }, Vec2f{ 0.0f, 0.0f },
        Vec2f{ 1.0f, 1.0f }, Vec2f{ 1.0f, 0.0f },
    };
    constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 2, 1, 3 };

    struct RuntimeLayer {
        int ProfileIndex = -1;
        int ProfileGroupIndex = -1;
        float Alpha = 1.0f;
    };
    std::vector<RuntimeLayer> layers;
    const int currentProfileGroup =
        background.NativeKankyoCurrentProfileIndex >= 0
            ? background.NativeKankyoCurrentProfileIndex >> 2
            : -1;
    const int nextProfileGroup =
        background.NativeKankyoNextProfileIndex >= 0
            ? background.NativeKankyoNextProfileIndex >> 2
            : -1;
    if (background.NativeKankyoCurrentProfileIndex >= 0 &&
        background.NativeKankyoNextProfileIndex >= 0 &&
        currentProfileGroup != nextProfileGroup &&
        background.NativeKankyoBlendAlpha >= 0) {
        const float targetAlpha = static_cast<float>(
            std::clamp(background.NativeKankyoBlendAlpha, 0, 255)) / 255.0f;
        layers.push_back({ background.NativeKankyoCurrentProfileIndex,
                           currentProfileGroup,
                           1.0f - targetAlpha });
        layers.push_back({ background.NativeKankyoNextProfileIndex,
                           nextProfileGroup, targetAlpha });
    } else {
        layers.push_back({ background.NativeKankyoCurrentProfileIndex,
                           currentProfileGroup, 1.0f });
    }

    for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const auto& layer = layers[layerIndex];
        if (!(layer.Alpha > 0.0f)) {
            continue;
        }
        const auto profileTextureIt = std::find_if(
            halo.ProfileTextures.begin(), halo.ProfileTextures.end(),
            [&](const auto& profileTexture) {
                return static_cast<int>(profileTexture.ProfileGroupIndex) ==
                       layer.ProfileGroupIndex;
            });
        if (profileTextureIt == halo.ProfileTextures.end() ||
            !profileTextureIt->TextureInputResolved) {
            continue;
        }
        const auto* texture = NativeRenderFindKankyoExtraCtxbTexture(
            background, profileTextureIt->TextureName);
        if (texture == nullptr || !texture->Rgba8Decoded || texture->Rgba8.empty()) {
            continue;
        }
        const uint8_t alpha = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(layer.Alpha * 255.0f)), 0, 255));
        Oot3dNativeRenderModel model;
        model.Source = background.NativeKankyoArchivePath.string() + "!" +
                       profileTextureIt->TextureName;
        model.Name = "kankyo:sun_halo:" + profileTextureIt->TextureName + ":layer" +
                     std::to_string(layerIndex);
        model.NativeKankyoRole = Oot3dNativeKankyoModelRole::SunHalo;
        model.NativeKankyoProfileIndex = layer.ProfileIndex;
        model.NativeKankyoRuntimeTransformMaterialized = true;
        model.NativeKankyoRuntimeTransformSource =
            "oot3d_code_bin_FUN_004594e0_FUN_0047d578_FUN_002cebd0_FUN_003f96bc";
        model.Textures.push_back(*texture);

        Oot3dNativeRenderBatch batch;
        batch.Material.Textured = true;
        batch.Material.TextureIndex = 0;
        batch.Material.TextureBindingSource =
            "oot3d_kankyo_native_profile_sun_ctxb_runtime_object";
        batch.Material.TextureHasNativeAlpha = texture->HasNativeAlpha;
        batch.Material.VertexColorModulatesTexture = true;
        batch.Material.NativeSamplerStateDecoded = true;
        batch.Material.NativeSamplerMinFilter =
            static_cast<uint16_t>(halo.MinMagFilter);
        batch.Material.NativeSamplerMagFilter =
            static_cast<uint16_t>(halo.MinMagFilter);
        batch.Material.NativeSamplerWrapS = static_cast<uint16_t>(halo.WrapS);
        batch.Material.NativeSamplerWrapT = static_cast<uint16_t>(halo.WrapT);
        batch.Material.NativeSamplerStateSource =
            "oot3d_code_bin_FUN_002d5124_FUN_00348a64_profile_sun_sampler";
        batch.Material.NativeRuntimeVertexAlphaBlend = layer.Alpha < 1.0f;
        batch.Material.NativeKankyoLayerBlendAlphaApplied = layers.size() > 1;
        batch.Material.NativeKankyoLayerBlendAlpha = alpha;
        batch.Material.NativeKankyoLayerBlendAlphaSource =
            "oot3d_code_bin_FUN_0047d578_current_target_group_s0_alpha";
        batch.Material.CmbCullFace = 3;
        batch.Material.PicaCullMode = Oot3dNativePicaCullMode::KeepAll;
        batch.Material.DepthTest = false;
        batch.Material.DepthWrite = false;
        batch.Material.NativePicaFogOverrideDecoded = true;
        batch.Material.NativePicaFogEnabled = false;
        batch.Material.NativePicaFogOverrideSource =
            "oot3d_pica_FUN_003f96bc_profile_sun_draw_fog_disabled";
        batch.Material.NativeRenderStateDecoded = true;
        batch.Material.NativeBlendStateEnabled = true;
        batch.Material.NativeBlendStateSupported = true;
        batch.Material.NativeBlendFactorsSupported = true;
        batch.Material.NativeBlendEquationSupported = true;
        batch.Material.BlendMode = 1;
        batch.Material.BlendSrc = 0x0302;
        batch.Material.BlendDst = 0x0001;
        batch.Material.BlendEquation = 0x8006;
        batch.Material.ColorBlendSrc = 0x0302;
        batch.Material.ColorBlendDst = 0x0001;
        batch.Material.ColorBlendEquation = 0x8006;
        for (const uint32_t index : indices) {
            Oot3dNativeRenderVertex vertex;
            vertex.Position = {
                static_cast<float>(corners[index].X),
                static_cast<float>(corners[index].Y),
                static_cast<float>(corners[index].Z),
            };
            vertex.Normal = {
                static_cast<float>(-forward.X),
                static_cast<float>(-forward.Y),
                static_cast<float>(-forward.Z),
            };
            vertex.Uv0 = uvs[index];
            vertex.NativeSourceUv0 = uvs[index];
            vertex.NativeSourceUv0Available = true;
            vertex.Color = { 255, 255, 255, alpha };
            vertex.NativeColorAvailable = true;
            batch.Vertices.push_back(vertex);
            ExpandBounds(model.LocalBounds, vertex.Position);
        }
        model.Batches.push_back(std::move(batch));
        model.Bounds = Oot3dNativeRenderModelWorldBounds(model);
        scene.EnvironmentModels.push_back(std::move(model));
        halo.RuntimeLayerAlphas.push_back(layer.Alpha);
        halo.RuntimeLayerTextureNames.push_back(profileTextureIt->TextureName);
    }

    halo.RuntimeTransformResolved = !halo.RuntimeLayerAlphas.empty();
    halo.NativeVisibleBackendSubmitResolved = halo.RuntimeTransformResolved;
    halo.RuntimeTransformSource =
        "oot3d_code_bin_FUN_002d5124_FUN_002d50e8_FUN_004594e0_FUN_0047d578_FUN_002cebd0_profile_ctxb";
    halo.SourceStatus = halo.RuntimeTransformResolved
                            ? "native independent sun billboard materialized from the code.bin "
                              "profile group, CTXB selector table, solar-vector, profile alpha, "
                              "scale, sampler, and PICA blend semantics"
                            : "native sun runtime produced no visible profile CTXB layer";
}

void MaterializeOot3dNativeKankyoSkyRuntime(
    Oot3dNativeDemoRenderScene& scene, Oot3dDemoVec3 cameraEye,
    Oot3dDemoVec3 cameraTarget, Oot3dDemoVec3 cameraUp) {
    auto& background = scene.EnvironmentBackground;
    for (auto& model : scene.EnvironmentModels) {
        if (model.NativeKankyoRole != Oot3dNativeKankyoModelRole::None) {
            for (auto& batch : model.Batches) {
                batch.Material.NativePicaFogOverrideDecoded = true;
                batch.Material.NativePicaFogEnabled = false;
                batch.Material.NativePicaFogOverrideSource =
                    "oot3d_code_bin_FUN_002e47c8_FUN_002d4c30_kankyo_cmb_route_without_002d960c_fog_material_binding";
            }
        }
        if (model.NativeKankyoRole != Oot3dNativeKankyoModelRole::SkyBackground &&
            model.NativeKankyoRole != Oot3dNativeKankyoModelRole::Cloud &&
            model.NativeKankyoRole != Oot3dNativeKankyoModelRole::Star) {
            continue;
        }
        const double scale = std::sqrt(
            static_cast<double>(model.ModelToWorld.M[0][0]) * model.ModelToWorld.M[0][0] +
            static_cast<double>(model.ModelToWorld.M[1][0]) * model.ModelToWorld.M[1][0] +
            static_cast<double>(model.ModelToWorld.M[2][0]) * model.ModelToWorld.M[2][0]);
        model.ModelToWorld = BuildOot3dNativeRenderScaleTranslateTransform(scale, cameraEye);
        model.Bounds = Oot3dNativeRenderModelWorldBounds(model);
        model.NativeKankyoRuntimeTransformMaterialized = true;
        model.NativeKankyoRuntimeTransformSource =
            "oot3d_pica_f20_f22_view_rotation_times_scale_from_camera_relative_model_translation";
    }

    NativeRenderMaterializeKankyoSunHaloRuntime(
        scene, cameraEye, cameraTarget, cameraUp);

    int activeAngle = -1;
    if (background.RuntimeEnvironmentTimeInputAvailable) {
        activeAngle = background.RuntimeEnvironmentSkyboxTime;
    } else if (background.NativeKankyoScheduleActiveAngle >= 0) {
        activeAngle = background.NativeKankyoScheduleActiveAngle & 0xFFFF;
    }
    if (activeAngle < 0) {
        return;
    }
    int signedAngle = (activeAngle - 0x8000) & 0xFFFF;
    if (signedAngle >= 0x8000) {
        signedAngle -= 0x10000;
    }
    const double radians = static_cast<double>(signedAngle) * kOot3dS16AngleToRadians;
    const Oot3dDemoVec3 celestialDirection = {
        std::sin(radians) * -120.0 * 25.0,
        std::cos(radians) * 120.0 * 25.0,
        std::cos(radians) * 20.0 * 25.0,
    };
    const double directionLength = std::sqrt(
        celestialDirection.X * celestialDirection.X +
        celestialDirection.Y * celestialDirection.Y +
        celestialDirection.Z * celestialDirection.Z);
    if (!(directionLength > std::numeric_limits<double>::epsilon())) {
        return;
    }
    const Oot3dDemoVec3 targetDirection = {
        -celestialDirection.X / directionLength,
        -celestialDirection.Y / directionLength,
        -celestialDirection.Z / directionLength,
    };
    constexpr Oot3dDemoVec3 localForward = { 0.0, -1.0, 0.0 };
    Oot3dDemoVec3 axis = {
        localForward.Y * targetDirection.Z - localForward.Z * targetDirection.Y,
        localForward.Z * targetDirection.X - localForward.X * targetDirection.Z,
        localForward.X * targetDirection.Y - localForward.Y * targetDirection.X,
    };
    double axisLength = std::sqrt(axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z);
    const double cosine = std::clamp(
        localForward.X * targetDirection.X + localForward.Y * targetDirection.Y +
            localForward.Z * targetDirection.Z,
        -1.0, 1.0);
    if (!(axisLength > std::numeric_limits<double>::epsilon())) {
        axis = cosine < 0.0 ? Oot3dDemoVec3{ 1.0, 0.0, 0.0 }
                            : Oot3dDemoVec3{ 0.0, 1.0, 0.0 };
        axisLength = 1.0;
    }
    axis.X /= axisLength;
    axis.Y /= axisLength;
    axis.Z /= axisLength;
    const double angle = std::acos(cosine);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double oneMinusC = 1.0 - c;
    Matrix4f rotation = Oot3dNativeRenderIdentityMatrix();
    rotation.M[0][0] = static_cast<float>(c + axis.X * axis.X * oneMinusC);
    rotation.M[0][1] = static_cast<float>(axis.X * axis.Y * oneMinusC - axis.Z * s);
    rotation.M[0][2] = static_cast<float>(axis.X * axis.Z * oneMinusC + axis.Y * s);
    rotation.M[1][0] = static_cast<float>(axis.Y * axis.X * oneMinusC + axis.Z * s);
    rotation.M[1][1] = static_cast<float>(c + axis.Y * axis.Y * oneMinusC);
    rotation.M[1][2] = static_cast<float>(axis.Y * axis.Z * oneMinusC - axis.X * s);
    rotation.M[2][0] = static_cast<float>(axis.Z * axis.X * oneMinusC - axis.Y * s);
    rotation.M[2][1] = static_cast<float>(axis.Z * axis.Y * oneMinusC + axis.X * s);
    rotation.M[2][2] = static_cast<float>(c + axis.Z * axis.Z * oneMinusC);

    constexpr double nativeSunScale = 20.0;
    const Oot3dDemoVec3 center = {
        cameraEye.X + celestialDirection.X,
        cameraEye.Y + celestialDirection.Y,
        cameraEye.Z + celestialDirection.Z,
    };
    const Matrix4f transform = MultiplyMatrices(
        TranslateMatrix(center), MultiplyMatrices(rotation, ScaleMatrix(nativeSunScale)));
    for (auto& model : scene.EnvironmentModels) {
        if (model.NativeKankyoRole != Oot3dNativeKankyoModelRole::Sun) {
            continue;
        }
        model.ModelToWorld = transform;
        model.Bounds = Oot3dNativeRenderModelWorldBounds(model);
        model.NativeKankyoRuntimeTransformMaterialized = true;
        model.NativeKankyoRuntimeTransformSource =
            "oot3d_code_bin_FUN_004594e0_FUN_0047d578_FUN_002cebd0";
    }
}

Oot3dDemoBounds Oot3dNativeRenderModelWorldBounds(const Oot3dNativeRenderModel& model) {
    return Oot3dNativeRenderTransformBounds(model.LocalBounds, model.ModelToWorld);
}

nlohmann::json Oot3dNativeRenderModelSummaryToJson(const Oot3dNativeRenderModel& model) {
    const auto worldBounds = Oot3dNativeRenderModelWorldBounds(model);
    nlohmann::json visibleResourceIds = nlohmann::json::array();
    for (size_t resourceId = 0; resourceId < model.NativeCmbResourceVisibility.size(); ++resourceId) {
        if (model.NativeCmbResourceVisibility[resourceId] != 0) {
            visibleResourceIds.push_back(resourceId);
        }
    }
    size_t alphaBatchCount = 0;
    size_t rigidBatchCount = 0;
    size_t skinnedBatchCount = 0;
    size_t depthTestBatchCount = 0;
    size_t depthWriteBatchCount = 0;
    size_t picaLightingBatchCount = 0;
    size_t picaLightingTexturedBatchCount = 0;
    size_t picaDirectionalLightingBatchCount = 0;
    size_t picaVertexLightingAppliedBatchCount = 0;
    size_t picaHemisphereLightingAppliedBatchCount = 0;
    size_t picaVertexOrHemisphereLightingAppliedBatchCount = 0;
    size_t picaVertexLightingDeferredBatchCount = 0;
    size_t picaHemisphereLightingDeferredBatchCount = 0;
    size_t picaVertexOrHemisphereLightingDeferredBatchCount = 0;
    size_t picaVertexHemisphereVectorResolvedBatchCount = 0;
    size_t picaVertexHemisphereVectorPendingBatchCount = 0;
    size_t picaEffectiveMaterialDiffuseAmbientBatchCount = 0;
    size_t picaSelfShadowCandidateBatchCount = 0;
    size_t picaSelfShadowAppliedBatchCount = 0;
    size_t picaSelfShadowVertexCount = 0;
    size_t picaSelfShadowOccludedVertexCount = 0;
    size_t picaShadow2dMaterialTextureProjectionCandidateBatchCount = 0;
    size_t picaShadow2dMaterialTextureProjectionDecodedBatchCount = 0;
    size_t picaShadow2dTexCoord0WInputCandidateBatchCount = 0;
    size_t picaShadow2dTexCoord0WInputDecodedBatchCount = 0;
    size_t picaShadow2dNativeMaterialLaneDecodedBatchCount = 0;
    size_t picaUnlitTextureEnvRouteDecodedBatchCount = 0;
    size_t picaUnlitTextureEnvRouteAppliedBatchCount = 0;
    size_t picaUnlitTextureEnvRouteMissingNativeColorBatchCount = 0;
    size_t vertexColorTextureModulatedBatchCount = 0;
    size_t picaLightingVertexCount = 0;
    size_t nativeNormalAvailableBatchCount = 0;
    size_t nativeNormalBatchCount = 0;
    size_t nativeNormalPartialBatchCount = 0;
    size_t nativeNormalVertexCount = 0;
    size_t nativeMissingNormalVertexCount = 0;
    size_t nativeColorAvailableBatchCount = 0;
    size_t nativeColorBatchCount = 0;
    size_t nativeColorPartialBatchCount = 0;
    size_t nativeColorVertexCount = 0;
    size_t nativeMissingColorVertexCount = 0;
    size_t nativeMaterialBatchCount = 0;
    size_t nativeMaterialAnimationAppliedBatchCount = 0;
    size_t nativeMaterialAnimationTextureAppliedBatchCount = 0;
    size_t nativeMaterialAnimationColorAppliedBatchCount = 0;
    size_t nativeMaterialAnimationTextureTransformAppliedBatchCount = 0;
    size_t nativeRuntimeMaterialColorOverrideBatchCount = 0;
    size_t nativeMaterialLightingCompleteBatchCount = 0;
    size_t nativeMaterialLightingIncompleteBatchCount = 0;
    size_t nativeRuntimeMaterialLaneDecodedBatchCount = 0;
    size_t nativeMaterialPostMaterialTextureEnvTableDecodedBatchCount = 0;
    size_t nativeMaterialPostMaterialTextureEnvStageResolvedBatchCount = 0;
    size_t nativeMaterialColorDecodedBatchCount = 0;
    size_t nativeMaterialLightingBlockDecodedBatchCount = 0;
    size_t nativeMaterialPicaLutInputBatchCount = 0;
    size_t nativeMaterialPicaLutInputCompleteBatchCount = 0;
    size_t nativeMaterialPicaLutInputFragmentLightingBatchCount = 0;
    size_t nativeMaterialPicaLutInputEvaluationPendingBatchCount = 0;
    size_t nativeMaterialPicaLutInputEvaluationAppliedBatchCount = 0;
    size_t nativeMaterialPicaBumpModeAvailableBatchCount = 0;
    size_t nativeMaterialPicaBumpModeRecognizedBatchCount = 0;
    size_t nativeMaterialPicaBumpModeActiveBatchCount = 0;
    size_t nativeMaterialPicaBumpModeBackendPendingBatchCount = 0;
    size_t nativeMaterialLightingFlag3AvailableBatchCount = 0;
    size_t nativeMaterialLightingFlag3ActiveBatchCount = 0;
    size_t nativeMaterialLightingFlag3BackendPendingBatchCount = 0;
    size_t rawTextureStageSelectorBatchCount = 0;
    size_t rawTextureStageBindingBatchCount = 0;
    size_t rawTextureStageCandidateWindowBatchCount = 0;
    size_t rawTextureStageCandidateNonzeroBatchCount = 0;
    size_t textureEnvDecodedBatchCount = 0;
    size_t textureEnvRawSizeBatchCount = 0;
    size_t textureEnvStageIndexCoveredBatchCount = 0;
    size_t textureEnvMultiStageBatchCount = 0;
    size_t textureEnvColorShaderSupportedBatchCount = 0;
    size_t textureEnvColorShaderAppliedBatchCount = 0;
    size_t textureEnvFallbackTextureLightModulationBatchCount = 0;
    size_t textureEnvShaderPendingBatchCount = 0;
    size_t textureEnvProgramDecodedBatchCount = 0;
    size_t textureEnvProgramRgbRouteDecodedBatchCount = 0;
    size_t textureEnvProgramUsesPreviousBatchCount = 0;
    size_t textureEnvProgramUsesConstantColorBatchCount = 0;
    size_t textureEnvProgramUsesSecondaryTextureBatchCount = 0;
    size_t textureEnvProgramRequiresMultiStageBatchCount = 0;
    size_t textureEnvProgramRequiresMultiTextureBatchCount = 0;
    size_t textureEnvProgramRequiresConstantColorSelectionBatchCount = 0;
    size_t textureEnvProgramRequiresTextureColorAddBatchCount = 0;
    size_t textureEnvProgramColorShaderSupportedBatchCount = 0;
    size_t textureEnvProgramColorShaderAppliedBatchCount = 0;
    size_t textureEnvProgramFallbackTextureLightModulationBatchCount = 0;
    size_t textureEnvProgramVertexColorConstantBatchCount = 0;
    size_t textureEnvProgramTextureColorAddendBatchCount = 0;
    size_t textureEnvProgramTexture1ColorAddBatchCount = 0;
    size_t textureEnvProgramTexture1ColorMultiplyBatchCount = 0;
    size_t textureEnvProgramTextureColorMultiplierBatchCount = 0;
    size_t nativeMaterialCombinerPendingBatchCount = 0;
    size_t nativeMaterialLightingEnabledBatchCount = 0;
    size_t nativeMaterialFragmentLightingEnabledBatchCount = 0;
    size_t nativeMaterialVertexLightingEnabledBatchCount = 0;
    size_t nativeMaterialHemisphereLightingEnabledBatchCount = 0;
    size_t nativeMaterialHemisphereOcclusionEnabledBatchCount = 0;
    size_t nativeMaterialCmbLightingFlagEnabledBatchCount = 0;
    size_t nativeMaterialRenderStateDecodedBatchCount = 0;
    size_t nativeMaterialCullEnabledBatchCount = 0;
    size_t nativeMaterialAlphaTestBatchCount = 0;
    size_t nativeMaterialBlendStateEnabledBatchCount = 0;
    size_t nativeMaterialBlendStateSupportedBatchCount = 0;
    size_t nativeMaterialFragmentLightingConfigBatchCount = 0;
    size_t nativeMaterialFragmentLightingConfigCompleteBatchCount = 0;
    size_t nativeMaterialFragmentLightingConfigRuntimeOverridePendingBatchCount = 0;
    size_t nativeMaterialFragmentLightingConfigKnownPayloadWordCount = 0;
    size_t nativeKankyoLayerBlendAlphaBatchCount = 0;
    nlohmann::json nativeMaterials = nlohmann::json::array();
    std::set<int32_t> nativeMaterialIndices;
    for (const auto& batch : model.Batches) {
        if (batch.Material.AlphaTest || batch.Material.TextureHasNativeAlpha) {
            ++alphaBatchCount;
        }
        if (batch.SkinningMode == 0) {
            ++rigidBatchCount;
        }
        if (batch.SkinningMode == 1 || batch.SkinningMode == 2) {
            ++skinnedBatchCount;
        }
        if (batch.Material.DepthTest) {
            ++depthTestBatchCount;
        }
        if (batch.Material.DepthWrite) {
            ++depthWriteBatchCount;
        }
        if (batch.Material.NativePicaLightingApplied) {
            ++picaLightingBatchCount;
            picaLightingVertexCount += batch.Vertices.size();
            if (batch.Material.Textured) {
                ++picaLightingTexturedBatchCount;
            }
        }
        if (batch.Material.NativePicaDirectionalLightingApplied) {
            ++picaDirectionalLightingBatchCount;
        }
        if (batch.Material.NativePicaVertexLightingApplied) {
            ++picaVertexLightingAppliedBatchCount;
        }
        if (batch.Material.NativePicaHemisphereLightingApplied) {
            ++picaHemisphereLightingAppliedBatchCount;
        }
        if (batch.Material.NativePicaVertexLightingApplied ||
            batch.Material.NativePicaHemisphereLightingApplied) {
            ++picaVertexOrHemisphereLightingAppliedBatchCount;
        }
        if (batch.Material.NativePicaVertexLightingDeferred) {
            ++picaVertexLightingDeferredBatchCount;
        }
        if (batch.Material.NativePicaHemisphereLightingDeferred) {
            ++picaHemisphereLightingDeferredBatchCount;
        }
        if (batch.Material.NativePicaVertexLightingDeferred ||
            batch.Material.NativePicaHemisphereLightingDeferred) {
            ++picaVertexOrHemisphereLightingDeferredBatchCount;
        }
        if (batch.Material.NativePicaVertexHemisphereVectorResolved) {
            ++picaVertexHemisphereVectorResolvedBatchCount;
        }
        if (batch.Material.NativePicaVertexHemisphereVectorPending) {
            ++picaVertexHemisphereVectorPendingBatchCount;
        }
        if (batch.Material.NativePicaEffectiveMaterialDiffuseUsesAmbient) {
            ++picaEffectiveMaterialDiffuseAmbientBatchCount;
        }
        if (batch.Material.NativePicaSelfShadowCandidate) {
            ++picaSelfShadowCandidateBatchCount;
        }
        if (batch.Material.NativePicaShadow2dMaterialTextureProjectionInputCandidate) {
            ++picaShadow2dMaterialTextureProjectionCandidateBatchCount;
        }
        if (batch.Material.NativePicaShadow2dMaterialTextureProjectionInputDecoded) {
            ++picaShadow2dMaterialTextureProjectionDecodedBatchCount;
        }
        if (batch.Material.NativePicaShadow2dTexCoord0WInputCandidate) {
            ++picaShadow2dTexCoord0WInputCandidateBatchCount;
        }
        if (batch.Material.NativePicaShadow2dTexCoord0WInputDecoded) {
            ++picaShadow2dTexCoord0WInputDecodedBatchCount;
        }
        if (batch.Material.NativeRuntimeMaterialLaneDecoded) {
            ++nativeRuntimeMaterialLaneDecodedBatchCount;
        }
        if (batch.Material.PostMaterialTextureEnvTableDecoded &&
            batch.Material.PostMaterialTextureEnvTableDerivedFromLanePointer) {
            ++nativeMaterialPostMaterialTextureEnvTableDecodedBatchCount;
        }
        if (batch.Material.PostMaterialTextureEnvTableDecoded &&
            batch.Material.TextureEnvStageIndicesCovered) {
            ++nativeMaterialPostMaterialTextureEnvStageResolvedBatchCount;
        }
        if (batch.Material.NativePicaSelfShadowCandidate &&
            batch.Material.NativeRuntimeMaterialLaneDecoded) {
            ++picaShadow2dNativeMaterialLaneDecodedBatchCount;
        }
        if (batch.Material.NativePicaSelfShadowApplied) {
            ++picaSelfShadowAppliedBatchCount;
            picaSelfShadowVertexCount += batch.Material.NativePicaSelfShadowVertexCount;
            picaSelfShadowOccludedVertexCount += batch.Material.NativePicaSelfShadowOccludedVertexCount;
        }
        if (batch.Material.NativePicaUnlitTextureEnvRouteDecoded) {
            ++picaUnlitTextureEnvRouteDecodedBatchCount;
            if (!batch.Material.NativePicaUnlitTextureEnvRouteNativeColorAvailable) {
                ++picaUnlitTextureEnvRouteMissingNativeColorBatchCount;
            }
        }
        if (batch.Material.NativePicaUnlitTextureEnvRouteApplied) {
            ++picaUnlitTextureEnvRouteAppliedBatchCount;
        }
        size_t batchNativeNormalVertexCount = 0;
        size_t batchNativeColorVertexCount = 0;
        for (const auto& vertex : batch.Vertices) {
            if (vertex.NativeNormalAvailable) {
                ++batchNativeNormalVertexCount;
            }
            if (vertex.NativeColorAvailable) {
                ++batchNativeColorVertexCount;
            }
        }
        nativeNormalVertexCount += batchNativeNormalVertexCount;
        nativeMissingNormalVertexCount += batch.Vertices.size() - batchNativeNormalVertexCount;
        if (batchNativeNormalVertexCount > 0) {
            ++nativeNormalAvailableBatchCount;
        }
        if (batchNativeNormalVertexCount == batch.Vertices.size() && !batch.Vertices.empty()) {
            ++nativeNormalBatchCount;
        } else if (batchNativeNormalVertexCount > 0) {
            ++nativeNormalPartialBatchCount;
        }
        nativeColorVertexCount += batchNativeColorVertexCount;
        nativeMissingColorVertexCount += batch.Vertices.size() - batchNativeColorVertexCount;
        if (batchNativeColorVertexCount > 0) {
            ++nativeColorAvailableBatchCount;
        }
        if (batchNativeColorVertexCount == batch.Vertices.size() && !batch.Vertices.empty()) {
            ++nativeColorBatchCount;
        } else if (batchNativeColorVertexCount > 0) {
            ++nativeColorPartialBatchCount;
        }
        if (batch.Material.Textured && batch.Material.VertexColorModulatesTexture) {
            ++vertexColorTextureModulatedBatchCount;
        }
        if (batch.Material.NativeMaterialAvailable) {
            ++nativeMaterialBatchCount;
            if (NativeMaterialLightingComplete(batch.Material)) {
                ++nativeMaterialLightingCompleteBatchCount;
            } else {
                ++nativeMaterialLightingIncompleteBatchCount;
            }
            if (nativeMaterialIndices.insert(batch.MaterialIndex).second) {
                nativeMaterials.push_back(NativeMaterialStateJson(batch.MaterialIndex, batch.Material));
            }
        }
        if (batch.Material.NativeMaterialAnimationApplied) {
            ++nativeMaterialAnimationAppliedBatchCount;
        }
        if (batch.Material.NativeMaterialAnimationTextureApplied) {
            ++nativeMaterialAnimationTextureAppliedBatchCount;
        }
        if (batch.Material.NativeMaterialAnimationColorApplied) {
            ++nativeMaterialAnimationColorAppliedBatchCount;
        }
        if (batch.Material.NativeMaterialAnimationTextureTransformApplied) {
            ++nativeMaterialAnimationTextureTransformAppliedBatchCount;
        }
        if (batch.Material.NativeRuntimeMaterialColorOverrideApplied) {
            ++nativeRuntimeMaterialColorOverrideBatchCount;
        }
        if (batch.Material.NativeKankyoLayerBlendAlphaApplied) {
            ++nativeKankyoLayerBlendAlphaBatchCount;
        }
        if (!ColorRgbIsZero(batch.Material.DiffuseColor)) {
            ++nativeMaterialLightingEnabledBatchCount;
        }
        if (batch.Material.FragmentLightingEnabled) {
            ++nativeMaterialFragmentLightingEnabledBatchCount;
        }
        if (batch.Material.VertexLightingEnabled) {
            ++nativeMaterialVertexLightingEnabledBatchCount;
        }
        if (batch.Material.HemisphereLightingEnabled) {
            ++nativeMaterialHemisphereLightingEnabledBatchCount;
        }
        if (batch.Material.HemisphereOcclusionEnabled) {
            ++nativeMaterialHemisphereOcclusionEnabledBatchCount;
        }
        if (batch.Material.FragmentLightingEnabled || batch.Material.VertexLightingEnabled ||
            batch.Material.HemisphereLightingEnabled) {
            ++nativeMaterialCmbLightingFlagEnabledBatchCount;
        }
        if (batch.Material.NativeRenderStateDecoded) {
            ++nativeMaterialRenderStateDecodedBatchCount;
        }
        if (batch.Material.PicaCullMode != Oot3dNativePicaCullMode::KeepAll) {
            ++nativeMaterialCullEnabledBatchCount;
        }
        if (batch.Material.AlphaTest) {
            ++nativeMaterialAlphaTestBatchCount;
        }
        if (batch.Material.NativeBlendStateEnabled) {
            ++nativeMaterialBlendStateEnabledBatchCount;
        }
        if (batch.Material.NativeBlendStateSupported) {
            ++nativeMaterialBlendStateSupportedBatchCount;
        }
        if (batch.Material.MaterialColorsDecoded) {
            ++nativeMaterialColorDecodedBatchCount;
        }
        if (batch.Material.MaterialLightingBlock.Decoded) {
            ++nativeMaterialLightingBlockDecodedBatchCount;
        }
        if (batch.Material.MaterialPicaLutInput.Available) {
            ++nativeMaterialPicaLutInputBatchCount;
            if (batch.Material.MaterialPicaLutInput.Complete) {
                ++nativeMaterialPicaLutInputCompleteBatchCount;
            }
        }
        if (batch.Material.NativePicaMaterialLutInputPacketUsedForFragmentLighting) {
            ++nativeMaterialPicaLutInputFragmentLightingBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputEvaluationPending) {
            ++nativeMaterialPicaLutInputEvaluationPendingBatchCount;
        }
        if (batch.Material.NativePicaMaterialLutInputEvaluationApplied) {
            ++nativeMaterialPicaLutInputEvaluationAppliedBatchCount;
        }
        if (batch.Material.NativePicaBumpModeAvailable) {
            ++nativeMaterialPicaBumpModeAvailableBatchCount;
        }
        if (batch.Material.NativePicaBumpModeRecognized) {
            ++nativeMaterialPicaBumpModeRecognizedBatchCount;
        }
        if (batch.Material.NativePicaBumpModeActive) {
            ++nativeMaterialPicaBumpModeActiveBatchCount;
        }
        if (batch.Material.NativePicaBumpModeBackendPending) {
            ++nativeMaterialPicaBumpModeBackendPendingBatchCount;
        }
        if (batch.Material.NativeMaterialLightingFlag3Available) {
            ++nativeMaterialLightingFlag3AvailableBatchCount;
        }
        if (batch.Material.NativeMaterialLightingFlag3Active) {
            ++nativeMaterialLightingFlag3ActiveBatchCount;
        }
        if (batch.Material.NativeMaterialLightingFlag3BackendPending) {
            ++nativeMaterialLightingFlag3BackendPendingBatchCount;
        }
        if (batch.Material.MaterialFragmentLightingConfig.Available) {
            ++nativeMaterialFragmentLightingConfigBatchCount;
            nativeMaterialFragmentLightingConfigKnownPayloadWordCount +=
                batch.Material.MaterialFragmentLightingConfig.KnownPayloadWordCount;
            if (batch.Material.MaterialFragmentLightingConfig.Complete) {
                ++nativeMaterialFragmentLightingConfigCompleteBatchCount;
            }
        }
        if (batch.Material.NativePicaFragmentLightingConfigRuntimeOverridePending) {
            ++nativeMaterialFragmentLightingConfigRuntimeOverridePendingBatchCount;
        }
        if (batch.Material.RawTextureStageSelectorDecoded) {
            ++rawTextureStageSelectorBatchCount;
        }
        if (batch.Material.TextureBindingResolvedFromRawStageSelector) {
            ++rawTextureStageBindingBatchCount;
        }
        if (batch.Material.NativeMaterialRawSize >= kOot3dCmbMaterialTextureStageCandidateEnd) {
            ++rawTextureStageCandidateWindowBatchCount;
        }
        if (!batch.Material.NativeMaterialTextureStageCandidateNonzeroWords.empty()) {
            ++rawTextureStageCandidateNonzeroBatchCount;
        }
        if (batch.Material.TextureEnv.Decoded) {
            ++textureEnvDecodedBatchCount;
        }
        if (batch.Material.TextureEnv.RawSize == kOot3dCmbMaterialTextureEnvSize) {
            ++textureEnvRawSizeBatchCount;
        }
        if (batch.Material.TextureEnvStageIndicesCovered) {
            ++textureEnvStageIndexCoveredBatchCount;
        }
        if (batch.Material.TextureEnvStageRecordCount > 1) {
            ++textureEnvMultiStageBatchCount;
        }
        if (batch.Material.TextureEnv.ColorShaderPathSupported) {
            ++textureEnvColorShaderSupportedBatchCount;
        }
        if (batch.Material.TextureEnv.ColorShaderPathApplied) {
            ++textureEnvColorShaderAppliedBatchCount;
        }
        if (batch.Material.TextureEnv.FallbackTextureLightModulationUsed) {
            ++textureEnvFallbackTextureLightModulationBatchCount;
        }
        if (batch.Material.NativeMaterialCombinerRequiresDecoder) {
            ++textureEnvShaderPendingBatchCount;
        }
        if (batch.Material.TextureEnvProgram.Decoded) {
            ++textureEnvProgramDecodedBatchCount;
        }
        if (batch.Material.TextureEnvProgram.RgbRouteDecoded) {
            ++textureEnvProgramRgbRouteDecodedBatchCount;
        }
        if (batch.Material.TextureEnvProgram.UsesPrevious) {
            ++textureEnvProgramUsesPreviousBatchCount;
        }
        if (batch.Material.TextureEnvProgram.UsesConstantColor) {
            ++textureEnvProgramUsesConstantColorBatchCount;
        }
        if (batch.Material.TextureEnvProgram.UsesTexture1 ||
            batch.Material.TextureEnvProgram.UsesTexture2 ||
            batch.Material.TextureEnvProgram.UsesTexture3) {
            ++textureEnvProgramUsesSecondaryTextureBatchCount;
        }
        if (batch.Material.TextureEnvProgram.RequiresMultiStageEvaluation) {
            ++textureEnvProgramRequiresMultiStageBatchCount;
        }
        if (batch.Material.TextureEnvProgram.RequiresMultiTextureSampling) {
            ++textureEnvProgramRequiresMultiTextureBatchCount;
        }
        if (batch.Material.TextureEnvProgram.RequiresConstantColorSelection) {
            ++textureEnvProgramRequiresConstantColorSelectionBatchCount;
        }
        if (batch.Material.TextureEnvProgram.RequiresTextureColorAdd) {
            ++textureEnvProgramRequiresTextureColorAddBatchCount;
        }
        if (batch.Material.TextureEnvProgram.ColorShaderPathSupported) {
            ++textureEnvProgramColorShaderSupportedBatchCount;
        }
        if (batch.Material.TextureEnvProgram.ColorShaderPathApplied) {
            ++textureEnvProgramColorShaderAppliedBatchCount;
        }
        if (batch.Material.TextureEnvProgram.FallbackTextureLightModulationUsed) {
            ++textureEnvProgramFallbackTextureLightModulationBatchCount;
        }
        if (batch.Material.TextureEnvProgram.VertexColorConstantStageCount > 0) {
            ++textureEnvProgramVertexColorConstantBatchCount;
        }
        if (batch.Material.TextureEnvProgram.TextureColorAddendResolved) {
            ++textureEnvProgramTextureColorAddendBatchCount;
        }
        if (batch.Material.TextureEnvProgram.Texture1ColorAddResolved) {
            ++textureEnvProgramTexture1ColorAddBatchCount;
        }
        if (batch.Material.TextureEnvProgram.Texture1ColorMultiplyResolved) {
            ++textureEnvProgramTexture1ColorMultiplyBatchCount;
        }
        if (batch.Material.TextureEnvProgram.TextureColorMultiplierResolved) {
            ++textureEnvProgramTextureColorMultiplierBatchCount;
        }
        if (batch.Material.NativeMaterialCombinerRequiresDecoder) {
            ++nativeMaterialCombinerPendingBatchCount;
        }
    }

    const auto kankyoRole = [&]() -> std::string_view {
        switch (model.NativeKankyoRole) {
            case Oot3dNativeKankyoModelRole::SkyBackground:
                return "sky_background";
            case Oot3dNativeKankyoModelRole::Sun:
                return "sun";
            case Oot3dNativeKankyoModelRole::Cloud:
                return "cloud";
            case Oot3dNativeKankyoModelRole::Star:
                return "star";
            case Oot3dNativeKankyoModelRole::SunHalo:
                return "sun_halo";
            default:
                return "none";
        }
    }();

    return {
        { "source", model.Source },
        { "name", model.Name },
        { "diagnostics", model.Diagnostics },
        { "texture_count", model.Textures.size() },
        { "native_textures", NativeTexturesJson(model.Textures) },
        { "native_material_texture_env_table_record_count", model.TextureEnvTableRecordCount },
        { "native_cmb_lut_chunk_decoded", model.Luts.Decoded },
        { "native_cmb_lut_record_count", model.Luts.Records.size() },
        { "native_cmb_lut_declared_record_count", model.Luts.Count },
        { "native_pica_cmb_lut_asset_decode_contract_available",
          model.Luts.NativeAssetDecodeContractAvailable },
        { "native_pica_cmb_lut_final_shader_semantic_resolved",
          model.Luts.FinalShaderSemanticResolved },
        { "native_pica_cmb_lut_shader_evaluation_pending",
          model.Luts.Decoded && !model.Luts.Records.empty() && !model.Luts.FinalShaderSemanticResolved },
        { "native_cmb_luts", NativeRenderLutSectionJson(model.Luts) },
        { "uploadable_texture_count", model.UploadableTextureCount() },
        { "transform_baked_into_vertices", model.TransformBakedIntoVertices },
        { "native_cmb_skeleton_bone_count", model.NativeCmbSkeletonBoneCount },
        { "native_cmb_mesh_pass_split_index_decoded",
          model.NativeCmbMeshPassSplitIndexDecoded },
        { "native_cmb_mesh_pass_split_index", model.NativeCmbMeshPassSplitIndex },
        { "native_kankyo_profile_index", model.NativeKankyoProfileIndex },
        { "native_kankyo_model_role", kankyoRole },
        { "native_kankyo_layer_alpha", model.NativeKankyoLayerAlpha },
        { "native_kankyo_layer_blend_alpha_applied", model.NativeKankyoLayerBlendAlphaApplied },
        { "native_kankyo_layer_blend_alpha_source", model.NativeKankyoLayerBlendAlphaSource },
        { "native_kankyo_layer_blend_alpha_batch_count", nativeKankyoLayerBlendAlphaBatchCount },
        { "native_kankyo_profile_attribute_blend_applied",
          model.NativeKankyoProfileAttributeBlendApplied },
        { "native_kankyo_profile_blend_current_index",
          model.NativeKankyoProfileBlendCurrentIndex },
        { "native_kankyo_profile_blend_next_index",
          model.NativeKankyoProfileBlendNextIndex },
        { "native_kankyo_profile_blend_weight", model.NativeKankyoProfileBlendWeight },
        { "native_kankyo_profile_attribute_blend_source",
          model.NativeKankyoProfileAttributeBlendSource },
        { "native_kankyo_runtime_transform_materialized",
          model.NativeKankyoRuntimeTransformMaterialized },
        { "native_kankyo_runtime_transform_source",
          model.NativeKankyoRuntimeTransformSource },
        { "model_to_world", MatrixJson(model.ModelToWorld) },
        { "batch_count", model.Batches.size() },
        { "average_vertex_color", ColorJson(AverageModelVertexColor(model)) },
        { "textured_batch_count", model.TexturedBatchCount() },
        { "alpha_batch_count", alphaBatchCount },
        { "rigid_batch_count", rigidBatchCount },
        { "skinned_batch_count", skinnedBatchCount },
        { "depth_test_batch_count", depthTestBatchCount },
        { "depth_write_batch_count", depthWriteBatchCount },
        { "native_pica_lighting_batch_count", picaLightingBatchCount },
        { "native_pica_lighting_textured_batch_count", picaLightingTexturedBatchCount },
        { "native_pica_directional_lighting_batch_count", picaDirectionalLightingBatchCount },
        { "native_pica_vertex_lighting_applied_batch_count", picaVertexLightingAppliedBatchCount },
        { "native_pica_hemisphere_lighting_applied_batch_count",
          picaHemisphereLightingAppliedBatchCount },
        { "native_pica_vertex_or_hemisphere_lighting_applied_batch_count",
          picaVertexOrHemisphereLightingAppliedBatchCount },
        { "native_pica_vertex_lighting_deferred_batch_count", picaVertexLightingDeferredBatchCount },
        { "native_pica_hemisphere_lighting_deferred_batch_count", picaHemisphereLightingDeferredBatchCount },
        { "native_pica_vertex_or_hemisphere_lighting_deferred_batch_count",
          picaVertexOrHemisphereLightingDeferredBatchCount },
        { "native_pica_vertex_hemisphere_vector_resolved_batch_count",
          picaVertexHemisphereVectorResolvedBatchCount },
        { "native_pica_vertex_hemisphere_vector_pending_batch_count",
          picaVertexHemisphereVectorPendingBatchCount },
        { "native_pica_effective_material_diffuse_ambient_batch_count",
          picaEffectiveMaterialDiffuseAmbientBatchCount },
        { "native_pica_self_shadow_candidate_batch_count", picaSelfShadowCandidateBatchCount },
        { "native_pica_shadow2d_material_texture_projection_candidate_batch_count",
          picaShadow2dMaterialTextureProjectionCandidateBatchCount },
        { "native_pica_shadow2d_material_texture_projection_decoded_batch_count",
          picaShadow2dMaterialTextureProjectionDecodedBatchCount },
        { "native_pica_shadow2d_texcoord0_w_input_candidate_batch_count",
          picaShadow2dTexCoord0WInputCandidateBatchCount },
        { "native_pica_shadow2d_texcoord0_w_input_decoded_batch_count",
          picaShadow2dTexCoord0WInputDecodedBatchCount },
        { "native_pica_shadow2d_native_material_lane_decoded_batch_count",
          picaShadow2dNativeMaterialLaneDecodedBatchCount },
        { "native_pica_self_shadow_applied_batch_count", picaSelfShadowAppliedBatchCount },
        { "native_pica_self_shadow_vertex_count", picaSelfShadowVertexCount },
        { "native_pica_self_shadow_occluded_vertex_count", picaSelfShadowOccludedVertexCount },
        { "native_pica_unlit_texture_env_route_decoded_batch_count",
          picaUnlitTextureEnvRouteDecodedBatchCount },
        { "native_pica_unlit_texture_env_route_applied_batch_count",
          picaUnlitTextureEnvRouteAppliedBatchCount },
        { "native_pica_unlit_texture_env_route_missing_native_color_batch_count",
          picaUnlitTextureEnvRouteMissingNativeColorBatchCount },
        { "vertex_color_texture_modulated_batch_count", vertexColorTextureModulatedBatchCount },
        { "native_pica_lighting_vertex_count", picaLightingVertexCount },
        { "native_normal_available_batch_count", nativeNormalAvailableBatchCount },
        { "native_normal_batch_count", nativeNormalBatchCount },
        { "native_normal_partial_batch_count", nativeNormalPartialBatchCount },
        { "native_normal_vertex_count", nativeNormalVertexCount },
        { "native_missing_normal_vertex_count", nativeMissingNormalVertexCount },
        { "native_color_available_batch_count", nativeColorAvailableBatchCount },
        { "native_color_batch_count", nativeColorBatchCount },
        { "native_color_partial_batch_count", nativeColorPartialBatchCount },
        { "native_color_vertex_count", nativeColorVertexCount },
        { "native_missing_color_vertex_count", nativeMissingColorVertexCount },
        { "native_material_batch_count", nativeMaterialBatchCount },
        { "native_material_animation_applied_batch_count",
          nativeMaterialAnimationAppliedBatchCount },
        { "native_material_animation_texture_applied_batch_count",
          nativeMaterialAnimationTextureAppliedBatchCount },
        { "native_material_animation_color_applied_batch_count",
          nativeMaterialAnimationColorAppliedBatchCount },
        { "native_material_animation_texture_transform_applied_batch_count",
          nativeMaterialAnimationTextureTransformAppliedBatchCount },
        { "native_runtime_material_color_override_batch_count",
          nativeRuntimeMaterialColorOverrideBatchCount },
        { "native_material_lighting_complete_batch_count",
          nativeMaterialLightingCompleteBatchCount },
        { "native_material_lighting_incomplete_batch_count",
          nativeMaterialLightingIncompleteBatchCount },
        { "native_runtime_material_lane_decoded_batch_count",
          nativeRuntimeMaterialLaneDecodedBatchCount },
        { "native_material_post_material_texture_env_table_decoded_batch_count",
          nativeMaterialPostMaterialTextureEnvTableDecodedBatchCount },
        { "native_material_post_material_texture_env_stage_resolved_batch_count",
          nativeMaterialPostMaterialTextureEnvStageResolvedBatchCount },
        { "native_material_lighting_enabled_batch_count", nativeMaterialLightingEnabledBatchCount },
        { "native_material_fragment_lighting_enabled_batch_count",
          nativeMaterialFragmentLightingEnabledBatchCount },
        { "native_material_vertex_lighting_enabled_batch_count",
          nativeMaterialVertexLightingEnabledBatchCount },
        { "native_material_hemisphere_lighting_enabled_batch_count",
          nativeMaterialHemisphereLightingEnabledBatchCount },
        { "native_material_hemisphere_occlusion_enabled_batch_count",
          nativeMaterialHemisphereOcclusionEnabledBatchCount },
        { "native_material_cmb_lighting_flag_enabled_batch_count",
          nativeMaterialCmbLightingFlagEnabledBatchCount },
        { "native_material_color_decoded_batch_count", nativeMaterialColorDecodedBatchCount },
        { "native_material_lighting_block_decoded_batch_count",
          nativeMaterialLightingBlockDecodedBatchCount },
        { "native_material_pica_lut_input_batch_count",
          nativeMaterialPicaLutInputBatchCount },
        { "native_material_pica_lut_input_complete_batch_count",
          nativeMaterialPicaLutInputCompleteBatchCount },
        { "native_material_pica_lut_input_fragment_lighting_batch_count",
          nativeMaterialPicaLutInputFragmentLightingBatchCount },
        { "native_material_pica_lut_input_evaluation_pending_batch_count",
          nativeMaterialPicaLutInputEvaluationPendingBatchCount },
        { "native_material_pica_lut_input_evaluation_applied_batch_count",
          nativeMaterialPicaLutInputEvaluationAppliedBatchCount },
        { "native_material_pica_bump_mode_available_batch_count",
          nativeMaterialPicaBumpModeAvailableBatchCount },
        { "native_material_pica_bump_mode_recognized_batch_count",
          nativeMaterialPicaBumpModeRecognizedBatchCount },
        { "native_material_pica_bump_mode_active_batch_count",
          nativeMaterialPicaBumpModeActiveBatchCount },
        { "native_material_pica_bump_mode_backend_pending_batch_count",
          nativeMaterialPicaBumpModeBackendPendingBatchCount },
        { "native_material_lighting_flag3_available_batch_count",
          nativeMaterialLightingFlag3AvailableBatchCount },
        { "native_material_lighting_flag3_active_batch_count",
          nativeMaterialLightingFlag3ActiveBatchCount },
        { "native_material_lighting_flag3_backend_pending_batch_count",
          nativeMaterialLightingFlag3BackendPendingBatchCount },
        { "native_material_fragment_lighting_config_batch_count",
          nativeMaterialFragmentLightingConfigBatchCount },
        { "native_material_fragment_lighting_config_complete_batch_count",
          nativeMaterialFragmentLightingConfigCompleteBatchCount },
        { "native_material_fragment_lighting_config_runtime_override_pending_batch_count",
          nativeMaterialFragmentLightingConfigRuntimeOverridePendingBatchCount },
        { "native_material_fragment_lighting_config_known_payload_word_count",
          nativeMaterialFragmentLightingConfigKnownPayloadWordCount },
        { "native_material_render_state_decoded_batch_count", nativeMaterialRenderStateDecodedBatchCount },
        { "native_material_cull_enabled_batch_count", nativeMaterialCullEnabledBatchCount },
        { "native_material_alpha_test_batch_count", nativeMaterialAlphaTestBatchCount },
        { "native_material_blend_state_enabled_batch_count", nativeMaterialBlendStateEnabledBatchCount },
        { "native_material_blend_state_supported_batch_count", nativeMaterialBlendStateSupportedBatchCount },
        { "native_material_raw_texture_stage_selector_batch_count", rawTextureStageSelectorBatchCount },
        { "native_material_raw_texture_stage_binding_batch_count", rawTextureStageBindingBatchCount },
        { "native_material_texture_stage_candidate_window_batch_count", rawTextureStageCandidateWindowBatchCount },
        { "native_material_texture_stage_candidate_nonzero_batch_count", rawTextureStageCandidateNonzeroBatchCount },
        { "native_material_texture_env_decoded_batch_count", textureEnvDecodedBatchCount },
        { "native_material_texture_env_raw_size_batch_count", textureEnvRawSizeBatchCount },
        { "native_material_texture_env_stage_index_covered_batch_count", textureEnvStageIndexCoveredBatchCount },
        { "native_material_texture_env_multi_stage_batch_count", textureEnvMultiStageBatchCount },
        { "native_material_texture_env_color_shader_supported_batch_count", textureEnvColorShaderSupportedBatchCount },
        { "native_material_texture_env_color_shader_applied_batch_count", textureEnvColorShaderAppliedBatchCount },
        { "native_material_texture_env_fallback_texture_light_modulation_batch_count",
          textureEnvFallbackTextureLightModulationBatchCount },
        { "native_material_texture_env_shader_pending_batch_count", textureEnvShaderPendingBatchCount },
        { "native_material_texture_env_program_decoded_batch_count", textureEnvProgramDecodedBatchCount },
        { "native_material_texture_env_program_rgb_route_decoded_batch_count",
          textureEnvProgramRgbRouteDecodedBatchCount },
        { "native_material_texture_env_program_uses_previous_batch_count",
          textureEnvProgramUsesPreviousBatchCount },
        { "native_material_texture_env_program_uses_constant_color_batch_count",
          textureEnvProgramUsesConstantColorBatchCount },
        { "native_material_texture_env_program_uses_secondary_texture_batch_count",
          textureEnvProgramUsesSecondaryTextureBatchCount },
        { "native_material_texture_env_program_requires_multi_stage_batch_count",
          textureEnvProgramRequiresMultiStageBatchCount },
        { "native_material_texture_env_program_requires_multi_texture_batch_count",
          textureEnvProgramRequiresMultiTextureBatchCount },
        { "native_material_texture_env_program_requires_constant_color_selection_batch_count",
          textureEnvProgramRequiresConstantColorSelectionBatchCount },
        { "native_material_texture_env_program_requires_texture_color_add_batch_count",
          textureEnvProgramRequiresTextureColorAddBatchCount },
        { "native_material_texture_env_program_color_shader_supported_batch_count",
          textureEnvProgramColorShaderSupportedBatchCount },
        { "native_material_texture_env_program_color_shader_applied_batch_count",
          textureEnvProgramColorShaderAppliedBatchCount },
        { "native_material_texture_env_program_fallback_texture_light_modulation_batch_count",
          textureEnvProgramFallbackTextureLightModulationBatchCount },
        { "native_material_texture_env_program_vertex_color_constant_batch_count",
          textureEnvProgramVertexColorConstantBatchCount },
        { "native_material_texture_env_program_texture_color_addend_batch_count",
          textureEnvProgramTextureColorAddendBatchCount },
        { "native_material_texture_env_program_texture1_color_add_batch_count",
          textureEnvProgramTexture1ColorAddBatchCount },
        { "native_material_texture_env_program_texture1_color_multiply_batch_count",
          textureEnvProgramTexture1ColorMultiplyBatchCount },
        { "native_material_texture_env_program_texture_color_multiplier_batch_count",
          textureEnvProgramTextureColorMultiplierBatchCount },
        { "native_material_combiner_pending_batch_count", nativeMaterialCombinerPendingBatchCount },
        { "native_materials", nativeMaterials },
        { "native_cmb_resource_visibility_applied", model.NativeCmbResourceVisibilityApplied },
        { "native_cmb_resource_visibility_count", model.NativeCmbResourceVisibility.size() },
        { "native_cmb_visible_resource_ids", visibleResourceIds },
        { "native_batches", NativeRenderBatchesSummaryJson(model) },
        { "triangle_count", model.TriangleCount() },
        { "vertex_count", model.VertexCount() },
        { "local_bounds", BoundsJson(model.LocalBounds) },
        { "bounds", BoundsJson(worldBounds) },
    };
}

nlohmann::json Oot3dNativeActorShadowStateToJson(const Oot3dNativeActorShadowState& shadow) {
    return {
        { "available", shadow.Available },
        { "actor_shape_state_supported", shadow.ActorShapeStateSupported },
        { "receiver_from_native_collision", shadow.ReceiverFromNativeCollision },
        { "native_blend_route_supported", shadow.NativeBlendRouteSupported },
        { "uses_runtime_n64_asset_substitution", shadow.UsesRuntimeN64AssetSubstitution },
        { "foot_shadow_draw_supported", shadow.FootShadowDrawSupported },
        { "foot_contact_pair_projection_supported", shadow.FootContactPairProjectionSupported },
        { "source_kind", shadow.SourceKind },
        { "actor", shadow.Actor },
        { "draw_function", shadow.DrawFunction },
        { "shape_source", shadow.ShapeSource },
        { "receiver_source", shadow.ReceiverSource },
        { "blend_source", shadow.BlendSource },
        { "pending_route", shadow.PendingRoute },
        { "shape_y_offset", shadow.ShapeYOffset },
        { "shape_shadow_scale", shadow.ShapeShadowScale },
        { "shape_shadow_alpha", static_cast<int>(shadow.ShapeShadowAlpha) },
        { "floor_polygon_index", shadow.FloorPolygonIndex },
        { "floor_surface_type", shadow.FloorSurfaceType },
        { "floor_light_setting_raw_index", shadow.FloorLightSettingRawIndex },
        { "floor_light_setting_index", shadow.FloorLightSettingIndex },
        { "actor_floor_height", shadow.ActorFloorHeight },
        { "actor_distance_to_floor", shadow.ActorDistanceToFloor },
        { "actor_position", Vec3Json(shadow.ActorPosition) },
        { "receiver_position", Vec3Json(shadow.ReceiverPosition) },
        { "receiver_normal", Vec3Json(shadow.ReceiverNormal) },
    };
}

nlohmann::json Oot3dNativePicaShadowStateToJson(const Oot3dNativePicaShadowState& shadow) {
    return {
        { "available", shadow.Available },
        { "semantic_table_available", shadow.SemanticTableAvailable },
        { "light_env_shadow_registers_supported", shadow.LightEnvShadowRegistersSupported },
        { "fragment_light_shadow_flags_supported", shadow.FragmentLightShadowFlagsSupported },
        { "shadow_texture_projection_registers_supported",
          shadow.ShadowTextureProjectionRegistersSupported },
        { "shader_equation_semantics_supported", shadow.ShaderEquationSemanticsSupported },
        { "shadow_texture_sampling_semantics_supported",
          shadow.ShadowTextureSamplingSemanticsSupported },
        { "primary_rgb_shadow_term_supported", shadow.PrimaryRgbShadowTermSupported },
        { "shadow_light_vector_source_supported",
          shadow.ShadowLightVectorSourceSupported },
        { "self_shadow_candidate_route_supported",
          shadow.SelfShadowCandidateRouteSupported },
        { "native_geometry_occlusion_supported", shadow.NativeGeometryOcclusionSupported },
        { "full_primary_light_contribution_shadow_supported",
          shadow.FullPrimaryLightContributionShadowSupported },
        { "shadow2d_texture_type_supported", shadow.Shadow2dTextureTypeSupported },
        { "shadow2d_backend_pass_request_supported",
          shadow.Shadow2dBackendPassRequestSupported },
        { "shadow2d_visual_pass_request_supported",
          shadow.Shadow2dVisualPassRequestSupported },
        { "shadow2d_material_texture_projection_input_supported",
          shadow.Shadow2dMaterialTextureProjectionInputSupported },
        { "shadow2d_texcoord0_w_input_supported",
          shadow.Shadow2dTexCoord0WInputSupported },
        { "shadow2d_encoded_depth_compare_supported",
          shadow.Shadow2dEncodedDepthCompareSupported },
        { "shadow2d_projection_register_values_decoded",
          shadow.Shadow2dProjectionRegisterValuesDecoded },
        { "shadow2d_projection_register_trace_available",
          shadow.Shadow2dProjectionRegisterTraceAvailable },
        { "shadow2d_dmp_shadow_z_uniforms_decoded",
          shadow.Shadow2dDmpShadowZUniformsDecoded },
        { "shadow2d_dmp_perspective_shadow_decoded",
          shadow.Shadow2dDmpPerspectiveShadowDecoded },
        { "shadow2d_pica_texture_shadow_register_decoded",
          shadow.Shadow2dPicaTextureShadowRegisterDecoded },
        { "shadow2d_pica_framebuffer_shadow_register_decoded",
          shadow.Shadow2dPicaFramebufferShadowRegisterDecoded },
        { "shadow2d_shader_route_register_trace_decoded",
          shadow.Shadow2dShaderRouteRegisterTraceDecoded },
        { "shadow2d_fragment_lighting_enable_decoded",
          shadow.Shadow2dFragmentLightingEnableDecoded },
        { "shadow2d_lighting_config0_decoded",
          shadow.Shadow2dLightingConfig0Decoded },
        { "shadow2d_lighting_config1_decoded",
          shadow.Shadow2dLightingConfig1Decoded },
        { "shadow2d_shadow_texture_param_decoded",
          shadow.Shadow2dShadowTextureParamDecoded },
        { "shadow2d_shadow_texture_dim_decoded",
          shadow.Shadow2dShadowTextureDimDecoded },
        { "shadow2d_shader_route_matches_primary_rgb_shadow_term",
          shadow.Shadow2dShaderRouteMatchesPrimaryRgbShadowTerm },
        { "shadow2d_shader_route_trace_disables_primary_rgb_shadow_term",
          shadow.Shadow2dShaderRouteTraceDisablesPrimaryRgbShadowTerm },
        { "shadow2d_backend_shadow_map_render_target_decoded",
          shadow.Shadow2dBackendShadowMapRenderTargetDecoded },
        { "shadow2d_backend_shadow_map_pass_implemented",
          shadow.Shadow2dBackendShadowMapPassImplemented },
        { "shadow2d_visual_pass_ready", shadow.Shadow2dVisualPassReady },
        { "shadow2d_visual_pass_uses_runtime_n64_asset_substitution",
          shadow.Shadow2dVisualPassUsesRuntimeN64AssetSubstitution },
        { "self_shadow_shader_route_decoded", shadow.SelfShadowShaderRouteDecoded },
        { "uses_runtime_n64_asset_substitution", shadow.UsesRuntimeN64AssetSubstitution },
        { "source_kind", shadow.SourceKind },
        { "mode", shadow.Mode },
        { "status", shadow.Status },
        { "light_env_shadow_alpha_name", shadow.LightEnvShadowAlphaName },
        { "light_env_shadow_selector_name", shadow.LightEnvShadowSelectorName },
        { "light_env_shadow_primary_name", shadow.LightEnvShadowPrimaryName },
        { "light_env_shadow_secondary_name", shadow.LightEnvShadowSecondaryName },
        { "fragment_light_shadowed_name_pattern", shadow.FragmentLightShadowedNamePattern },
        { "fragment_light_shadowed_register_count", shadow.FragmentLightShadowedRegisterCount },
        { "texture_shadow_z_scale_name", shadow.TextureShadowZScaleName },
        { "texture_shadow_z_bias_name", shadow.TextureShadowZBiasName },
        { "shader_equation_source_kind", shadow.ShaderEquationSourceKind },
        { "enable_shadow_source", shadow.EnableShadowSource },
        { "shadow_selector_source", shadow.ShadowSelectorSource },
        { "shadow_invert_source", shadow.ShadowInvertSource },
        { "shadow_primary_source", shadow.ShadowPrimarySource },
        { "shadow_secondary_source", shadow.ShadowSecondarySource },
        { "shadow_alpha_source", shadow.ShadowAlphaSource },
        { "per_light_shadow_enable_source", shadow.PerLightShadowEnableSource },
        { "shadow_sample_source", shadow.ShadowSampleSource },
        { "shadow_default_formula", shadow.ShadowDefaultFormula },
        { "shadow_invert_formula", shadow.ShadowInvertFormula },
        { "primary_rgb_formula", shadow.PrimaryRgbFormula },
        { "secondary_rgb_formula", shadow.SecondaryRgbFormula },
        { "alpha_formula", shadow.AlphaFormula },
        { "shadow_texture_sampling_source_kind", shadow.ShadowTextureSamplingSourceKind },
        { "shadow_texture_types", shadow.ShadowTextureTypes },
        { "shadow_texture_orthographic_source", shadow.ShadowTextureOrthographicSource },
        { "shadow_texture_bias_source", shadow.ShadowTextureBiasSource },
        { "shadow_texture_z_formula", shadow.ShadowTextureZFormula },
        { "shadow_texture_compare_source", shadow.ShadowTextureCompareSource },
        { "shadow_texture_filter", shadow.ShadowTextureFilter },
        { "shadow_map_format", shadow.ShadowMapFormat },
        { "shadow2d_encoded_depth_decode_source",
          shadow.Shadow2dEncodedDepthDecodeSource },
        { "shadow2d_encoded_depth_bits", shadow.Shadow2dEncodedDepthBits },
        { "shadow2d_encoded_alpha_bits", shadow.Shadow2dEncodedAlphaBits },
        { "shadow2d_bias_shift", shadow.Shadow2dBiasShift },
        { "shadow2d_filter_tap_count", shadow.Shadow2dFilterTapCount },
        { "shadow2d_filter_result_channel_count",
          shadow.Shadow2dFilterResultChannelCount },
        { "shadow2d_pica_texture_shadow_register_index",
          shadow.Shadow2dPicaTextureShadowRegisterIndex },
        { "shadow2d_pica_framebuffer_shadow_register_index",
          shadow.Shadow2dPicaFramebufferShadowRegisterIndex },
        { "shadow2d_pica_texture_shadow_register_raw",
          shadow.Shadow2dPicaTextureShadowRegisterRaw },
        { "shadow2d_pica_framebuffer_shadow_register_raw",
          shadow.Shadow2dPicaFramebufferShadowRegisterRaw },
        { "shadow2d_texture_shadow_orthographic",
          shadow.Shadow2dTextureShadowOrthographic },
        { "shadow2d_texture_shadow_raw_bias",
          shadow.Shadow2dTextureShadowRawBias },
        { "shadow2d_texture_shadow_compare_bias",
          shadow.Shadow2dTextureShadowCompareBias },
        { "shadow2d_framebuffer_shadow_constant_raw",
          shadow.Shadow2dFramebufferShadowConstantRaw },
        { "shadow2d_framebuffer_shadow_linear_raw",
          shadow.Shadow2dFramebufferShadowLinearRaw },
        { "shadow2d_lighting_config0_raw", shadow.Shadow2dLightingConfig0Raw },
        { "shadow2d_lighting_config1_raw", shadow.Shadow2dLightingConfig1Raw },
        { "shadow2d_shadow_selector", shadow.Shadow2dShadowSelector },
        { "shadow2d_per_light_shadow_enable_mask",
          shadow.Shadow2dPerLightShadowEnableMask },
        { "shadow2d_shadow_texture_param_register_index",
          shadow.Shadow2dShadowTextureParamRegisterIndex },
        { "shadow2d_shadow_texture_param_raw",
          shadow.Shadow2dShadowTextureParamRaw },
        { "shadow2d_shadow_texture_type", shadow.Shadow2dShadowTextureType },
        { "shadow2d_shadow_texture_dim_register_index",
          shadow.Shadow2dShadowTextureDimRegisterIndex },
        { "shadow2d_shadow_texture_dim_raw",
          shadow.Shadow2dShadowTextureDimRaw },
        { "shadow2d_shadow_texture_width", shadow.Shadow2dShadowTextureWidth },
        { "shadow2d_shadow_texture_height", shadow.Shadow2dShadowTextureHeight },
        { "shadow2d_dmp_perspective_shadow", shadow.Shadow2dDmpPerspectiveShadow },
        { "shadow2d_fragment_lighting_enabled",
          shadow.Shadow2dFragmentLightingEnabled },
        { "shadow2d_lighting_enable_shadow", shadow.Shadow2dLightingEnableShadow },
        { "shadow2d_lighting_shadow_primary", shadow.Shadow2dLightingShadowPrimary },
        { "shadow2d_lighting_shadow_secondary",
          shadow.Shadow2dLightingShadowSecondary },
        { "shadow2d_lighting_shadow_invert", shadow.Shadow2dLightingShadowInvert },
        { "shadow2d_lighting_shadow_alpha", shadow.Shadow2dLightingShadowAlpha },
        { "shadow2d_shadow_texture_is_shadow2d",
          shadow.Shadow2dShadowTextureIsShadow2d },
        { "shadow2d_dmp_shadow_z_bias", shadow.Shadow2dDmpShadowZBias },
        { "shadow2d_dmp_shadow_z_scale", shadow.Shadow2dDmpShadowZScale },
        { "shadow2d_framebuffer_shadow_constant",
          shadow.Shadow2dFramebufferShadowConstant },
        { "shadow2d_framebuffer_shadow_linear",
          shadow.Shadow2dFramebufferShadowLinear },
        { "shadow2d_out_of_bounds_result", shadow.Shadow2dOutOfBoundsResult },
        { "shadow2d_filter_interpolation_source",
          shadow.Shadow2dFilterInterpolationSource },
        { "shader_route_source", shadow.ShaderRouteSource },
        { "shadow_map_source", shadow.ShadowMapSource },
        { "shadow2d_backend_pass_request_source",
          shadow.Shadow2dBackendPassRequestSource },
        { "shadow2d_visual_pass_source_kind", shadow.Shadow2dVisualPassSourceKind },
        { "shadow2d_visual_pass_render_target_format",
          shadow.Shadow2dVisualPassRenderTargetFormat },
        { "shadow2d_material_texture_projection_input_source",
          shadow.Shadow2dMaterialTextureProjectionInputSource },
        { "shadow2d_texcoord0_w_input_source",
          shadow.Shadow2dTexCoord0WInputSource },
        { "shadow2d_projection_register_value_source",
          shadow.Shadow2dProjectionRegisterValueSource },
        { "shadow2d_projection_register_trace_source_kind",
          shadow.Shadow2dProjectionRegisterTraceSourceKind },
        { "shadow2d_projection_register_trace_format",
          shadow.Shadow2dProjectionRegisterTraceFormat },
        { "shadow2d_projection_register_decode_source",
          shadow.Shadow2dProjectionRegisterDecodeSource },
        { "shadow2d_shader_route_decode_source",
          shadow.Shadow2dShaderRouteDecodeSource },
        { "shadow2d_shader_route_trace_status",
          shadow.Shadow2dShaderRouteTraceStatus },
        { "shadow2d_backend_shadow_map_render_target_source",
          shadow.Shadow2dBackendShadowMapRenderTargetSource },
        { "shadow2d_visual_pass_application", shadow.Shadow2dVisualPassApplication },
        { "shadow2d_visual_pass_blocked_reason",
          shadow.Shadow2dVisualPassBlockedReason },
        { "shader_route_application", shadow.ShaderRouteApplication },
        { "shadow_caster_scope", shadow.ShadowCasterScope },
        { "shadow_light_vector_source", shadow.ShadowLightVectorSource },
        { "shadow_occlusion_formula", shadow.ShadowOcclusionFormula },
        { "shadow_light_contribution_formula", shadow.ShadowLightContributionFormula },
        { "pending_route", shadow.PendingRoute },
        { "directional_light_count", shadow.DirectionalLightCount },
        { "candidate_batch_count", shadow.CandidateBatchCount },
        { "material_texture_projection_candidate_batch_count",
          shadow.MaterialTextureProjectionCandidateBatchCount },
        { "material_texture_projection_decoded_batch_count",
          shadow.MaterialTextureProjectionDecodedBatchCount },
        { "texcoord0_w_input_candidate_batch_count",
          shadow.TexCoord0WInputCandidateBatchCount },
        { "texcoord0_w_input_decoded_batch_count",
          shadow.TexCoord0WInputDecodedBatchCount },
        { "native_material_lane_decoded_batch_count",
          shadow.NativeMaterialLaneDecodedBatchCount },
        { "applied_batch_count", shadow.AppliedBatchCount },
        { "applied_vertex_count", shadow.AppliedVertexCount },
        { "occluded_vertex_count", shadow.OccludedVertexCount },
    };
}

nlohmann::json U32ArrayJson(const std::array<uint32_t, 4>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(value, 3));
    }
    return out;
}

nlohmann::json U32ArrayJson(const std::array<uint32_t, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(value, 3));
    }
    return out;
}

nlohmann::json U32ArrayJson(const std::array<uint32_t, 2>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(value, 3));
    }
    return out;
}

nlohmann::json U32ArrayJson(const std::array<uint32_t, 6>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(value, 3));
    }
    return out;
}

nlohmann::json I32ArrayJson(const std::array<int, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(value);
    }
    return out;
}

nlohmann::json F32ArrayJson(const std::array<float, 3>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(value);
    }
    return out;
}

nlohmann::json U8HexArrayJson(const std::array<uint8_t, 6>& values) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto value : values) {
        out.push_back(HexU32(static_cast<uint32_t>(value), 2));
    }
    return out;
}

nlohmann::json Oot3dNativePicaActorVsLightPacketStateToJson(
    const Oot3dNativePicaActorVsLightPacketState& packet) {
    return {
        { "available", packet.Available },
        { "color_packet_available", packet.ColorPacketAvailable },
        { "vector_layout_resolved", packet.VectorLayoutResolved },
        { "vector_origin_resolved", packet.VectorOriginResolved },
        { "final_packet_writer_resolved", packet.FinalPacketWriterResolved },
        { "runtime_final_packet_emitter_resolved", packet.RuntimeFinalPacketEmitterResolved },
        { "compact_payload_source_resolved", packet.CompactPayloadSourceResolved },
        { "source_kind", packet.SourceKind },
        { "source_status", packet.SourceStatus },
        { "color_source", packet.ColorSource },
        { "ambient_color_source", packet.AmbientColorSource },
        { "diffuse0_color_source", packet.Diffuse0ColorSource },
        { "diffuse1_color_source", packet.Diffuse1ColorSource },
        { "pica_fog_color_available", packet.PicaFogColorAvailable },
        { "pica_fog_color_source", packet.PicaFogColorSource },
        { "pica_fog_color_offset", HexU32(packet.PicaFogColorOffset, 2) },
        { "pica_fog_color", ColorJson(packet.PicaFogColor) },
        { "vector_layout_source", packet.VectorLayoutSource },
        { "vector_source_status", packet.VectorSourceStatus },
        { "compact_payload_source", packet.CompactPayloadSource },
        { "compact_payload_direction_source", packet.CompactPayloadDirectionSource },
        { "compact_payload_color_source", packet.CompactPayloadColorSource },
        { "selection_source", packet.SelectionSource },
        { "runtime_transition_color_blend_applied",
          packet.RuntimeTransitionColorBlendApplied },
        { "runtime_transition_mode_color_blend_applied",
          packet.RuntimeTransitionModeColorBlendApplied },
        { "record_selection_index_delta", packet.RecordSelectionIndexDelta },
        { "packet_prep_address", HexU32(packet.PacketPrepAddress, 8) },
        { "runtime_light_packet_pack_address", HexU32(packet.RuntimeLightPacketPackAddress, 8) },
        { "runtime_final_packet_emitter_address", HexU32(packet.RuntimeFinalPacketEmitterAddress, 8) },
        { "compact_payload_producer_address", HexU32(packet.CompactPayloadProducerAddress, 8) },
        { "compact_payload_consumer_handler_address",
          HexU32(packet.CompactPayloadConsumerHandlerAddress, 8) },
        { "runtime_final_packet_word_count", packet.RuntimeFinalPacketWordCount },
        { "runtime_final_packet_register_base", HexU32(packet.RuntimeFinalPacketRegisterBase, 3) },
        { "slot_count", packet.SlotCount },
        { "slot_stride_bytes", packet.SlotStrideBytes },
        { "compact_payload_slot_count", packet.CompactPayloadSlotCount },
        { "compact_payload_slot_stride_bytes", packet.CompactPayloadSlotStrideBytes },
        { "compact_payload_slot_offsets", U32ArrayJson(packet.CompactPayloadSlotOffsets) },
        { "compact_payload_direction_offset", HexU32(packet.CompactPayloadDirectionOffset, 2) },
        { "compact_payload_color_offset", HexU32(packet.CompactPayloadColorOffset, 2) },
        { "compact_payload_size_bytes", packet.CompactPayloadSizeBytes },
        { "compact_payload_direction_source_offsets",
          U32ArrayJson(packet.CompactPayloadDirectionSourceOffsets) },
        { "compact_payload_color_source_offsets",
          U32ArrayJson(packet.CompactPayloadColorSourceOffsets) },
        { "compact_payload_direction_angle_bias",
          HexU32(packet.CompactPayloadDirectionAngleBias, 4) },
        { "compact_payload_direction_scales",
          F32ArrayJson(packet.CompactPayloadDirectionScales) },
        { "source_color_payload_offsets", U32ArrayJson(packet.SourceColorPayloadOffsets) },
        { "source_vector_offsets", U32ArrayJson(packet.SourceVectorOffsets) },
        { "enable_intensity_offset", HexU32(packet.EnableIntensityOffset, 3) },
        { "prepared_vector_offsets", U32ArrayJson(packet.PreparedVectorOffsets) },
        { "prepared_intensity_offset", HexU32(packet.PreparedIntensityOffset, 3) },
        { "runtime_source_vector_static_update_address",
          HexU32(packet.RuntimeSourceVectorStaticUpdateAddress, 8) },
        { "runtime_source_vector_dynamic_submit_callback_address",
          HexU32(packet.RuntimeSourceVectorDynamicSubmitCallbackAddress, 8) },
        { "runtime_source_vector_dynamic_transform_update_skip_flag_mask",
          HexU32(packet.RuntimeSourceVectorDynamicTransformUpdateSkipFlagMask, 2) },
        { "runtime_source_vector_static_update_skip_flag_mask",
          HexU32(packet.RuntimeSourceVectorStaticUpdateSkipFlagMask, 2) },
        { "runtime_source_vector_committed_copy_skip_flag_mask",
          HexU32(packet.RuntimeSourceVectorCommittedCopySkipFlagMask, 2) },
        { "runtime_source_vector_working_block_offset",
          HexU32(packet.RuntimeSourceVectorWorkingBlockOffset, 3) },
        { "runtime_source_vector_base_vector_offsets",
          U32ArrayJson(packet.RuntimeSourceVectorBaseVectorOffsets) },
        { "runtime_source_vector_working_vector_seed_offsets",
          U32ArrayJson(packet.RuntimeSourceVectorWorkingVectorSeedOffsets) },
        { "runtime_source_vector_working_packet_source_vector_offsets",
          U32ArrayJson(packet.RuntimeSourceVectorWorkingPacketSourceVectorOffsets) },
        { "runtime_source_vector_working_block_word_count",
          packet.RuntimeSourceVectorWorkingBlockWordCount },
        { "runtime_source_vector_committed_block_offset",
          HexU32(packet.RuntimeSourceVectorCommittedBlockOffset, 3) },
        { "runtime_source_vector_committed_block_word_count",
          packet.RuntimeSourceVectorCommittedBlockWordCount },
        { "runtime_light_packet_pack_source_prepared_vector_base_offset",
          HexU32(packet.RuntimeLightPacketPackSourcePreparedVectorBaseOffset, 3) },
        { "runtime_light_packet_pack_source_prepared_intensity_offset",
          HexU32(packet.RuntimeLightPacketPackSourcePreparedIntensityOffset, 3) },
        { "runtime_light_packet_pack_required_prepared_intensity_word",
          HexU32(packet.RuntimeLightPacketPackRequiredPreparedIntensityWord, 8) },
        { "runtime_light_packet_pack_output_record_layout_resolved",
          packet.RuntimeLightPacketPackOutputRecordLayoutResolved },
        { "runtime_light_packet_pack_output_feeds_final_upload_emitter",
          packet.RuntimeLightPacketPackOutputFeedsFinalUploadEmitter },
        { "runtime_light_packet_pack_negates_prepared_vector",
          packet.RuntimeLightPacketPackNegatesPreparedVector },
        { "runtime_light_packet_pack_output_final_record_direction_packed_word_offsets",
          U32ArrayJson(packet.RuntimeLightPacketPackOutputFinalRecordDirectionPackedWordOffsets) },
        { "runtime_final_upload_copied_word_source_offsets",
          U32ArrayJson(packet.RuntimeFinalUploadCopiedWordSourceOffsets) },
        { "runtime_final_upload_copied_word_packet_word_indices",
          U32ArrayJson(packet.RuntimeFinalUploadCopiedWordPacketWordIndices) },
        { "environment_record_index", packet.EnvironmentRecordIndex },
        { "environment_record_offset", packet.EnvironmentRecordOffset },
        { "record_index", packet.RecordIndex },
        { "record_offset", packet.RecordOffset },
        { "runtime_transition_color_from_record_index",
          packet.RuntimeTransitionColorFromRecordIndex },
        { "runtime_transition_color_to_record_index",
          packet.RuntimeTransitionColorToRecordIndex },
        { "runtime_transition_color_target_from_record_index",
          packet.RuntimeTransitionColorTargetFromRecordIndex },
        { "runtime_transition_color_target_to_record_index",
          packet.RuntimeTransitionColorTargetToRecordIndex },
        { "runtime_transition_color_angle_weight",
          packet.RuntimeTransitionColorAngleWeight },
        { "runtime_transition_color_mode_weight",
          packet.RuntimeTransitionColorModeWeight },
        { "compact_payload_active_angle", packet.CompactPayloadActiveAngle },
        { "ambient_color", ColorJson(packet.AmbientColor) },
        { "diffuse0_color", ColorJson(packet.Diffuse0Color) },
        { "diffuse1_color", ColorJson(packet.Diffuse1Color) },
        { "compact_payload_slot0_direction", Vec3Json(packet.CompactPayloadSlot0Direction) },
        { "compact_payload_slot1_direction", Vec3Json(packet.CompactPayloadSlot1Direction) },
        { "compact_payload_slot0_direction_s8",
          I32ArrayJson(packet.CompactPayloadSlot0DirectionS8) },
        { "compact_payload_slot1_direction_s8",
          I32ArrayJson(packet.CompactPayloadSlot1DirectionS8) },
        { "compact_payload_slot0_bytes", U8HexArrayJson(packet.CompactPayloadSlot0Bytes) },
        { "compact_payload_slot1_bytes", U8HexArrayJson(packet.CompactPayloadSlot1Bytes) },
    };
}

nlohmann::json Oot3dNativePicaResolvedRuntimeLightSettingToJson(
    const Oot3dNativePicaResolvedRuntimeLightSetting& resolved) {
    return {
        { "available", resolved.Available },
        { "used_for_render", resolved.UsedForRender },
        { "runtime_color_addend_contract_resolved", resolved.RuntimeColorAddendContractResolved },
        { "runtime_ambient_color_addend_resolved", resolved.RuntimeAmbientColorAddendResolved },
        { "runtime_light_color_addend_resolved", resolved.RuntimeLightColorAddendResolved },
        { "runtime_final_ambient_color_formula_resolved",
          resolved.RuntimeFinalAmbientColorFormulaResolved },
        { "runtime_final_light_color_formula_resolved",
          resolved.RuntimeFinalLightColorFormulaResolved },
        { "runtime_final_ambient_color_used_for_render",
          resolved.RuntimeFinalAmbientColorUsedForRender },
        { "runtime_final_light_color_used_for_render",
          resolved.RuntimeFinalLightColorUsedForRender },
        { "runtime_fog_color_addend_resolved", resolved.RuntimeFogColorAddendResolved },
        { "runtime_final_fog_color_formula_resolved", resolved.RuntimeFinalFogColorFormulaResolved },
        { "runtime_final_fog_color_used_for_render", resolved.RuntimeFinalFogColorUsedForRender },
        { "runtime_fog_distance_contract_resolved", resolved.RuntimeFogDistanceContractResolved },
        { "runtime_fog_distance_addends_resolved", resolved.RuntimeFogDistanceAddendsResolved },
        { "runtime_camera_far_used_for_render", resolved.RuntimeCameraFarUsedForRender },
        { "runtime_fog_distances_used_for_render", resolved.RuntimeFogDistancesUsedForRender },
        { "transition_table_available", resolved.TransitionTableAvailable },
        { "transition_table_branch_supported", resolved.TransitionTableBranchSupported },
        { "transition_table_branch_applied", resolved.TransitionTableBranchApplied },
        { "direct_current_record_branch_applied", resolved.DirectCurrentRecordBranchApplied },
        { "direct_target_record_branch_applied", resolved.DirectTargetRecordBranchApplied },
        { "mode_blend_active", resolved.ModeBlendActive },
        { "source_kind", resolved.SourceKind },
        { "branch", resolved.Branch },
        { "source_status", resolved.SourceStatus },
        { "runtime_color_addend_source", resolved.RuntimeColorAddendSource },
        { "runtime_final_ambient_color_source", resolved.RuntimeFinalAmbientColorSource },
        { "runtime_final_ambient_color_status", resolved.RuntimeFinalAmbientColorStatus },
        { "runtime_final_light_color_source", resolved.RuntimeFinalLightColorSource },
        { "runtime_final_light_color_status", resolved.RuntimeFinalLightColorStatus },
        { "runtime_final_fog_color_source", resolved.RuntimeFinalFogColorSource },
        { "runtime_final_fog_color_status", resolved.RuntimeFinalFogColorStatus },
        { "runtime_fog_distance_source", resolved.RuntimeFogDistanceSource },
        { "runtime_fog_distance_status", resolved.RuntimeFogDistanceStatus },
        { "active_angle_source", resolved.ActiveAngleSource },
        { "mode_state_source", resolved.ModeStateSource },
        { "direct_target_scene_path", resolved.DirectTargetScenePath },
        { "direct_target_scene_zsi_path", resolved.DirectTargetSceneZsiPath.generic_string() },
        { "direct_target_scene_source_status", resolved.DirectTargetSceneSourceStatus },
        { "direct_target_scene_matches_active_scene", resolved.DirectTargetSceneMatchesActiveScene },
        { "direct_target_scene_lighting_decoded", resolved.DirectTargetSceneLightingDecoded },
        { "active_angle_bootstrapped_from_player_start",
          resolved.ActiveAngleBootstrappedFromPlayerStart },
        { "mode_state_bootstrapped_from_code_bin_fallback",
          resolved.ModeStateBootstrappedFromCodeBinFallback },
        { "active_setup_index", resolved.ActiveSetupIndex },
        { "active_angle", resolved.ActiveAngle },
        { "current_mode", resolved.CurrentMode },
        { "target_mode", resolved.TargetMode },
        { "current_entry_index", resolved.CurrentEntryIndex },
        { "target_entry_index", resolved.TargetEntryIndex },
        { "current_from_light_setting_index", resolved.CurrentFromLightSettingIndex },
        { "current_to_light_setting_index", resolved.CurrentToLightSettingIndex },
        { "target_from_light_setting_index", resolved.TargetFromLightSettingIndex },
        { "target_to_light_setting_index", resolved.TargetToLightSettingIndex },
        { "current_record_index", resolved.CurrentRecordIndex },
        { "previous_record_index", resolved.PreviousRecordIndex },
        { "target_record_index", resolved.TargetRecordIndex },
        { "target_setup_index", resolved.TargetSetupIndex },
        { "target_light_setting_raw_index", resolved.TargetLightSettingRawIndex },
        { "target_light_setting_index", resolved.TargetLightSettingIndex },
        { "current_record_offset", resolved.CurrentRecordOffset },
        { "previous_record_offset", resolved.PreviousRecordOffset },
        { "target_record_offset", resolved.TargetRecordOffset },
        { "active_angle_working_state_address", HexU32(resolved.ActiveAngleWorkingStateAddress, 8) },
        { "active_angle_working_halfword_offset", HexU32(resolved.ActiveAngleWorkingHalfwordOffset, 2) },
        { "active_angle_output_state_address", HexU32(resolved.ActiveAngleOutputStateAddress, 8) },
        { "active_angle_output_halfword_offset", HexU32(resolved.ActiveAngleOutputHalfwordOffset, 2) },
        { "mode_state_base_play_offset", HexU32(resolved.ModeStateBasePlayOffset, 4) },
        { "mode_current_relative_offset", HexU32(resolved.ModeCurrentRelativeOffset, 2) },
        { "mode_target_relative_offset", HexU32(resolved.ModeTargetRelativeOffset, 2) },
        { "mode_blend_active_relative_offset", HexU32(resolved.ModeBlendActiveRelativeOffset, 2) },
        { "mode_blend_remaining_halfword_relative_offset",
          HexU32(resolved.ModeBlendRemainingHalfwordRelativeOffset, 2) },
        { "mode_blend_duration_halfword_relative_offset",
          HexU32(resolved.ModeBlendDurationHalfwordRelativeOffset, 2) },
        { "ambient_preaddend_record_offset", HexU32(resolved.AmbientPreAddendRecordOffset, 2) },
        { "light0_preaddend_record_offset", HexU32(resolved.Light0PreAddendRecordOffset, 2) },
        { "light1_preaddend_record_offset", HexU32(resolved.Light1PreAddendRecordOffset, 2) },
        { "ambient_preaddend_state_offset", HexU32(resolved.AmbientPreAddendStateOffset, 2) },
        { "light0_preaddend_state_offset", HexU32(resolved.Light0PreAddendStateOffset, 2) },
        { "light1_preaddend_state_offset", HexU32(resolved.Light1PreAddendStateOffset, 2) },
        { "ambient_color_addend_state_offset", HexU32(resolved.AmbientColorAddendStateOffset, 2) },
        { "light_color_addend_state_offset", HexU32(resolved.LightColorAddendStateOffset, 2) },
        { "final_ambient_color_output_offset", HexU32(resolved.FinalAmbientColorOutputOffset, 2) },
        { "final_ambient_color_play_offset", HexU32(resolved.FinalAmbientColorPlayOffset, 4) },
        { "light0_final_payload_color_state_offset",
          HexU32(resolved.Light0FinalPayloadColorStateOffset, 2) },
        { "light1_final_payload_color_state_offset",
          HexU32(resolved.Light1FinalPayloadColorStateOffset, 2) },
        { "fog_preaddend_record_offset", HexU32(resolved.FogPreAddendRecordOffset, 2) },
        { "fog_preaddend_state_offset", HexU32(resolved.FogPreAddendStateOffset, 2) },
        { "fog_color_addend_state_offset", HexU32(resolved.FogColorAddendStateOffset, 2) },
        { "final_fog_color_output_offset", HexU32(resolved.FinalFogColorOutputOffset, 2) },
        { "final_fog_color_play_offset", HexU32(resolved.FinalFogColorPlayOffset, 4) },
        { "camera_far_record_offset", HexU32(resolved.CameraFarRecordOffset, 2) },
        { "fog_far_record_offset", HexU32(resolved.FogFarRecordOffset, 2) },
        { "fog_near_record_offset", HexU32(resolved.FogNearRecordOffset, 2) },
        { "camera_far_output_offset", HexU32(resolved.CameraFarOutputOffset, 2) },
        { "fog_far_output_offset", HexU32(resolved.FogFarOutputOffset, 2) },
        { "fog_near_output_offset", HexU32(resolved.FogNearOutputOffset, 2) },
        { "camera_far_play_offset", HexU32(resolved.CameraFarPlayOffset, 4) },
        { "fog_far_play_offset", HexU32(resolved.FogFarPlayOffset, 4) },
        { "fog_near_play_offset", HexU32(resolved.FogNearPlayOffset, 4) },
        { "fog_near_addend_state_offset", HexU32(resolved.FogNearAddendStateOffset, 2) },
        { "fog_far_addend_state_offset", HexU32(resolved.FogFarAddendStateOffset, 2) },
        { "angle_weight", resolved.AngleWeight },
        { "mode_blend_weight", resolved.ModeBlendWeight },
        { "direct_blend_weight", resolved.DirectBlendWeight },
        { "ambient_color", ColorJson(resolved.AmbientColor) },
        { "light0_vector", Vec3Json(resolved.Light0Vector) },
        { "light0_color", ColorJson(resolved.Light0Color) },
        { "light1_vector", Vec3Json(resolved.Light1Vector) },
        { "light1_color", ColorJson(resolved.Light1Color) },
        { "ambient_preaddend_color", ColorJson(resolved.AmbientPreAddendColor) },
        { "light0_preaddend_color", ColorJson(resolved.Light0PreAddendColor) },
        { "light1_preaddend_color", ColorJson(resolved.Light1PreAddendColor) },
        { "ambient_color_addend_i16", I32ArrayJson(resolved.AmbientColorAddendI16) },
        { "light_color_addend_i16", I32ArrayJson(resolved.LightColorAddendI16) },
        { "fog_preaddend_color", ColorJson(resolved.FogPreAddendColor) },
        { "fog_color_addend_i16", I32ArrayJson(resolved.FogColorAddendI16) },
        { "final_fog_color", ColorJson(resolved.FinalFogColor) },
        { "fog_color", ColorJson(resolved.FogColor) },
        { "camera_far", resolved.CameraFar },
        { "fog_far_preaddend", resolved.FogFarPreAddend },
        { "fog_far_addend", resolved.FogFarAddend },
        { "fog_far", resolved.FogFar },
        { "fog_near_interpolated", resolved.FogNearInterpolated },
        { "fog_near_preaddend", resolved.FogNearPreAddend },
        { "fog_near_addend", resolved.FogNearAddend },
        { "fog_near", resolved.FogNear },
    };
}

nlohmann::json Oot3dNativePicaRuntimeUvTransformStateToJson(
    const Oot3dNativePicaRuntimeUvTransformState& state) {
    return {
        { "available", state.Available },
        { "used_for_render", state.UsedForRender },
        { "uploads_native_pica_vsh_uniforms", state.UploadsNativePicaVshUniforms },
        { "directly_writes_packet_prep_source", state.DirectlyWritesPacketPrepSource },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "runtime_submit_function_address", state.RuntimeSubmitFunctionAddress },
        { "runtime_record_texture_light_function_address",
          state.RuntimeRecordTextureLightFunctionAddress },
        { "runtime_uv_transform_build_function_address",
          state.RuntimeUvTransformBuildFunctionAddress },
        { "runtime_uv_transform_source_builder_address",
          state.RuntimeUvTransformSourceBuilderAddress },
        { "runtime_uv_transform_copy_helper_address", state.RuntimeUvTransformCopyHelperAddress },
        { "runtime_uv_transform_primary_upload_helper_address",
          state.RuntimeUvTransformPrimaryUploadHelperAddress },
        { "runtime_uv_transform_secondary_upload_helper_address",
          state.RuntimeUvTransformSecondaryUploadHelperAddress },
        { "source_count_offset", state.SourceCountOffset },
        { "source_record_base_offset", state.SourceRecordBaseOffset },
        { "source_record_stride_bytes", state.SourceRecordStrideBytes },
        { "output_slot_count", state.OutputSlotCount },
        { "output_slot_stride_bytes", state.OutputSlotStrideBytes },
        { "output_word_count", state.OutputWordCount },
        { "primary_upload_register", state.PrimaryUploadRegister },
        { "primary_upload_word_count", state.PrimaryUploadWordCount },
        { "secondary_upload_register_base", state.SecondaryUploadRegisterBase },
        { "secondary_upload_word_count", state.SecondaryUploadWordCount },
        { "first_slot_override_owner_offset", state.FirstSlotOverrideOwnerOffset },
        { "first_slot_override_gate_byte_offset", state.FirstSlotOverrideGateByteOffset },
        { "first_slot_override_source_pointer_offset",
          state.FirstSlotOverrideSourcePointerOffset },
        { "first_slot_override_payload_offset", state.FirstSlotOverridePayloadOffset },
    };
}

nlohmann::json Oot3dNativePicaRuntimeSubmitDescriptorStateToJson(
    const Oot3dNativePicaRuntimeSubmitDescriptorState& state) {
    return {
        { "available", state.Available },
        { "used_for_render", state.UsedForRender },
        { "directly_writes_packet_prep_source", state.DirectlyWritesPacketPrepSource },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "runtime_submit_function_address", state.RuntimeSubmitFunctionAddress },
        { "runtime_record_texture_light_function_address",
          state.RuntimeRecordTextureLightFunctionAddress },
        { "descriptor_pointer_word_index", state.DescriptorPointerWordIndex },
        { "texture_object_pointer_word_index", state.TextureObjectPointerWordIndex },
        { "light_record_table_pointer_word_index", state.LightRecordTablePointerWordIndex },
        { "color_0_byte_offset", state.Color0ByteOffset },
        { "color_1_byte_offset", state.Color1ByteOffset },
        { "color_component_count", state.ColorComponentCount },
        { "active_light_slot_count_offset", state.ActiveLightSlotCountOffset },
        { "active_light_slot_index_table_offset", state.ActiveLightSlotIndexTableOffset },
        { "active_light_slot_index_stride_bytes", state.ActiveLightSlotIndexStrideBytes },
        { "light_record_stride_bytes", state.LightRecordStrideBytes },
        { "light_record_color_op_0_halfword_offset",
          state.LightRecordColorOp0HalfwordOffset },
        { "light_record_color_op_1_halfword_offset",
          state.LightRecordColorOp1HalfwordOffset },
        { "light_record_disabled_color_op_value", state.LightRecordDisabledColorOpValue },
        { "light_slot_count", state.LightSlotCount },
        { "texture_light_count_offset", state.TextureLightCountOffset },
        { "texture_light_uv_source_count_offset", state.TextureLightUvSourceCountOffset },
        { "texture_light_texture_ref_base_offset", state.TextureLightTextureRefBaseOffset },
        { "texture_light_source_record_base_offset", state.TextureLightSourceRecordBaseOffset },
        { "texture_light_record_stride_bytes", state.TextureLightRecordStrideBytes },
        { "texture_light_output_texture_id_halfword_offset",
          state.TextureLightOutputTextureIdHalfwordOffset },
        { "texture_light_output_sampler_word_base_offset",
          state.TextureLightOutputSamplerWordBaseOffset },
        { "texture_light_output_mode_word_base_offset",
          state.TextureLightOutputModeWordBaseOffset },
        { "texture_light_output_word_stride_bytes", state.TextureLightOutputWordStrideBytes },
    };
}

nlohmann::json Oot3dNativePicaFogLutWordsJson(const Oot3dNativePicaFogState& fog, size_t limit) {
    nlohmann::json words = nlohmann::json::array();
    const size_t count = std::min<size_t>(fog.LutWordCount, fog.LutWords.size());
    const size_t outCount = std::min(count, limit);
    for (size_t i = 0; i < outCount; ++i) {
        words.push_back(HexU32(fog.LutWords[i], 8));
    }
    return words;
}

nlohmann::json Oot3dNativePicaFogStateToJson(const Oot3dNativePicaFogState& fog) {
    return {
        { "available", fog.Available },
        { "used_for_render", fog.UsedForRender },
        { "code_bin_source_decoded", fog.CodeBinSourceDecoded },
        { "shader_supported", fog.ShaderSupported },
        { "lut_shader_supported", fog.LutShaderSupported },
        { "fog_enabled", fog.FogEnabled },
        { "fog_flip", fog.FogFlip },
        { "runtime_final_fog_color_formula_resolved", fog.RuntimeFinalFogColorFormulaResolved },
        { "runtime_fog_color_addend_resolved", fog.RuntimeFogColorAddendResolved },
        { "runtime_final_fog_color_used_for_render", fog.RuntimeFinalFogColorUsedForRender },
        { "runtime_fog_distance_contract_resolved", fog.RuntimeFogDistanceContractResolved },
        { "runtime_fog_distance_addends_resolved", fog.RuntimeFogDistanceAddendsResolved },
        { "runtime_fog_distances_used_for_render", fog.RuntimeFogDistancesUsedForRender },
        { "projection_range_available", fog.ProjectionRangeAvailable },
        { "scene_projection_far_decoded", fog.SceneProjectionFarDecoded },
        { "source_kind", fog.SourceKind },
        { "source_status", fog.SourceStatus },
        { "color_source", fog.ColorSource },
        { "lut_source", fog.LutSource },
        { "blocked_reason", fog.BlockedReason },
        { "color", ColorJson(fog.Color) },
        { "preaddend_color", ColorJson(fog.PreAddendColor) },
        { "color_addend_i16", I32ArrayJson(fog.ColorAddendI16) },
        { "final_color", ColorJson(fog.FinalColor) },
        { "mode_raw", HexU32(fog.ModeRaw, 8) },
        { "mode", fog.Mode },
        { "color_register", HexU32(fog.ColorRegister, 3) },
        { "preaddend_record_offset", HexU32(fog.PreAddendRecordOffset, 2) },
        { "preaddend_state_offset", HexU32(fog.PreAddendStateOffset, 2) },
        { "color_addend_state_offset", HexU32(fog.ColorAddendStateOffset, 2) },
        { "final_output_offset", HexU32(fog.FinalOutputOffset, 2) },
        { "final_play_offset", HexU32(fog.FinalPlayOffset, 4) },
        { "camera_far_play_offset", HexU32(fog.CameraFarPlayOffset, 4) },
        { "fog_far_play_offset", HexU32(fog.FogFarPlayOffset, 4) },
        { "fog_near_play_offset", HexU32(fog.FogNearPlayOffset, 4) },
        { "projection_matrix_play_offset", HexU32(fog.ProjectionMatrixPlayOffset, 4) },
        { "view_init_address", HexU32(fog.ViewInitAddress, 8) },
        { "view_default_near_literal_address", HexU32(fog.ViewDefaultNearLiteralAddress, 8) },
        { "view_default_far_literal_address", HexU32(fog.ViewDefaultFarLiteralAddress, 8) },
        { "view_update_address", HexU32(fog.ViewUpdateAddress, 8) },
        { "projection_build_address", HexU32(fog.ProjectionBuildAddress, 8) },
        { "scene_projection_far_literal_address",
          HexU32(fog.SceneProjectionFarLiteralAddress, 8) },
        { "scene_projection_matrix_offset", HexU32(fog.SceneProjectionMatrixOffset, 3) },
        { "lut_index_register", HexU32(fog.LutIndexRegister, 3) },
        { "lut_data_register_base", HexU32(fog.LutDataRegisterBase, 3) },
        { "fog_payload_runtime_update_address", HexU32(fog.FogPayloadRuntimeUpdateAddress, 8) },
        { "fog_payload_build_address", HexU32(fog.FogPayloadBuildAddress, 8) },
        { "fog_payload_default_source_address", HexU32(fog.FogPayloadDefaultSourceAddress, 8) },
        { "fog_payload_source_float_0_offset", HexU32(fog.FogPayloadSourceFloat0Offset, 3) },
        { "fog_payload_source_float_1_offset", HexU32(fog.FogPayloadSourceFloat1Offset, 3) },
        { "fog_payload_packed_table_offset", HexU32(fog.FogPayloadPackedTableOffset, 3) },
        { "fog_payload_packed_table_entry_count", fog.FogPayloadPackedTableEntryCount },
        { "source_rgb_scale_r", fog.SourceRgbScaleR },
        { "source_rgb_scale_g", fog.SourceRgbScaleG },
        { "source_rgb_scale_b", fog.SourceRgbScaleB },
        { "source_near", fog.SourceNear },
        { "source_far", fog.SourceFar },
        { "projection_near", fog.ProjectionNear },
        { "projection_far", fog.ProjectionFar },
        { "runtime_camera_far", fog.RuntimeCameraFar },
        { "scene_projection_far", fog.SceneProjectionFar },
        { "source_rebuild_gate", static_cast<int>(fog.SourceRebuildGate) },
        { "source_curve_mode", static_cast<int>(fog.SourceCurveMode) },
        { "lut_word_count", fog.LutWordCount },
        { "lut_words_first16", Oot3dNativePicaFogLutWordsJson(fog, 16) },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativePicaLightingRenderStateToJson(const Oot3dNativePicaLightingRenderState& lighting) {
    return {
        { "available", lighting.Available },
        { "semantic_table_available", lighting.SemanticTableAvailable },
        { "source_kind", lighting.SourceKind },
        { "mode", lighting.Mode },
        { "record_selector", lighting.RecordSelector },
        { "record_selector_source", lighting.RecordSelectorSource },
        { "vertex_color_formula", lighting.VertexColorFormula },
        { "directional_formula", lighting.DirectionalFormula },
        { "texture_combiner", lighting.TextureCombiner },
        { "textured_base_color_source", lighting.TexturedBaseColorSource },
        { "ambient_color_source", lighting.AmbientColorSource },
        { "diffuse_color_source", lighting.DiffuseColorSource },
        { "light1_color_source", lighting.Light1ColorSource },
        { "ambient_color_offset", lighting.AmbientColorOffset },
        { "diffuse_color_offset", lighting.DiffuseColorOffset },
        { "light1_color_offset", lighting.Light1ColorOffset },
        { "material_lighting_enable_source", lighting.MaterialLightingEnableSource },
        { "material_lighting_deferred_source", lighting.MaterialLightingDeferredSource },
        { "material_vertex_hemisphere_model_scope", lighting.MaterialVertexHemisphereModelScope },
        { "material_vertex_hemisphere_lighting_source",
          lighting.MaterialVertexHemisphereLightingSource },
        { "vertex_hemisphere_lighting_formula", lighting.VertexHemisphereLightingFormula },
        { "vertex_hemisphere_lighting_reference", lighting.VertexHemisphereLightingReference },
        { "vertex_hemisphere_light_color_mode", lighting.VertexHemisphereLightColorMode },
        { "material_emission_source", lighting.MaterialEmissionSource },
        { "material_ambient_source", lighting.MaterialAmbientSource },
        { "material_diffuse_source", lighting.MaterialDiffuseSource },
        { "light0_vector_group_index", lighting.Light0VectorGroupIndex },
        { "light0_vector_source", lighting.Light0VectorSource },
        { "directional_vectors_use_model_space", lighting.DirectionalVectorsUseModelSpace },
        { "directional_vector_space_source", lighting.DirectionalVectorSpaceSource },
        { "light0_vector_offset", lighting.Light0VectorOffset },
        { "light0_vector", Vec3Json(lighting.Light0Vector) },
        { "light1_vector_group_index", lighting.Light1VectorGroupIndex },
        { "light1_vector_source", lighting.Light1VectorSource },
        { "light1_vector_offset", lighting.Light1VectorOffset },
        { "light1_vector", Vec3Json(lighting.Light1Vector) },
        { "runtime_environment_time_input_available",
          lighting.RuntimeEnvironmentTimeInputAvailable },
        { "runtime_environment_day_time", lighting.RuntimeEnvironmentDayTime },
        { "runtime_environment_skybox_time", lighting.RuntimeEnvironmentSkyboxTime },
        { "runtime_environment_time_start_frame",
          lighting.RuntimeEnvironmentTimeStartFrame },
        { "runtime_environment_time_source_kind",
          lighting.RuntimeEnvironmentTimeSourceKind },
        { "runtime_environment_time_source_status",
          lighting.RuntimeEnvironmentTimeSourceStatus },
        { "runtime_environment_light_mode_input_available",
          lighting.RuntimeEnvironmentLightModeInputAvailable },
        { "runtime_environment_light_mode_current",
          lighting.RuntimeEnvironmentLightModeCurrent },
        { "runtime_environment_light_mode_target",
          lighting.RuntimeEnvironmentLightModeTarget },
        { "runtime_environment_light_mode_blend_active",
          lighting.RuntimeEnvironmentLightModeBlendActive },
        { "runtime_environment_light_mode_blend_remaining",
          lighting.RuntimeEnvironmentLightModeBlendRemaining },
        { "runtime_environment_light_mode_blend_duration",
          lighting.RuntimeEnvironmentLightModeBlendDuration },
        { "runtime_environment_light_mode_blend_weight",
          lighting.RuntimeEnvironmentLightModeBlendWeight },
        { "runtime_environment_light_mode_start_frame",
          lighting.RuntimeEnvironmentLightModeStartFrame },
        { "runtime_environment_light_mode_source_action_id",
          lighting.RuntimeEnvironmentLightModeSourceActionId },
        { "runtime_environment_light_mode_source_kind",
          lighting.RuntimeEnvironmentLightModeSourceKind },
        { "runtime_environment_light_mode_source_status",
          lighting.RuntimeEnvironmentLightModeSourceStatus },
        { "runtime_environment_light_setting_input_available",
          lighting.RuntimeEnvironmentLightSettingInputAvailable },
        { "runtime_environment_light_setting_raw_index",
          lighting.RuntimeEnvironmentLightSettingRawIndex },
        { "runtime_environment_light_setting_target",
          lighting.RuntimeEnvironmentLightSettingTarget },
        { "runtime_environment_light_setting_setup_index",
          lighting.RuntimeEnvironmentLightSettingSetupIndex },
        { "runtime_environment_light_setting_start_frame",
          lighting.RuntimeEnvironmentLightSettingStartFrame },
        { "runtime_environment_light_setting_play_target_offset",
          HexU32(lighting.RuntimeEnvironmentLightSettingPlayTargetOffset, 4) },
        { "runtime_environment_light_setting_play_blend_weight_offset",
          HexU32(lighting.RuntimeEnvironmentLightSettingPlayBlendWeightOffset, 4) },
        { "runtime_environment_light_setting_scene_path",
          lighting.RuntimeEnvironmentLightSettingScenePath },
        { "runtime_environment_light_setting_source_kind",
          lighting.RuntimeEnvironmentLightSettingSourceKind },
        { "runtime_environment_light_setting_source_status",
          lighting.RuntimeEnvironmentLightSettingSourceStatus },
        { "active_setup_index", lighting.ActiveSetupIndex },
        { "record_index", lighting.RecordIndex },
        { "record_offset", lighting.RecordOffset },
        { "floor_polygon_index", lighting.FloorPolygonIndex },
        { "floor_surface_type", lighting.FloorSurfaceType },
        { "floor_light_setting_raw_index", lighting.FloorLightSettingRawIndex },
        { "floor_light_setting_index", lighting.FloorLightSettingIndex },
        { "ambient_group_index", lighting.AmbientGroupIndex },
        { "diffuse_group_index", lighting.DiffuseGroupIndex },
        { "ambient_color", ColorJson(lighting.AmbientColor) },
        { "diffuse_color", ColorJson(lighting.DiffuseColor) },
        { "light0_color", ColorJson(lighting.DiffuseColor) },
        { "light1_color", ColorJson(lighting.Light1Color) },
        { "resolved_runtime_light_setting",
          Oot3dNativePicaResolvedRuntimeLightSettingToJson(lighting.ResolvedRuntimeLightSetting) },
        { "actor_vs_light_packet", Oot3dNativePicaActorVsLightPacketStateToJson(lighting.ActorVsLightPacket) },
        { "runtime_uv_transform",
          Oot3dNativePicaRuntimeUvTransformStateToJson(lighting.RuntimeUvTransform) },
        { "runtime_submit_descriptor",
          Oot3dNativePicaRuntimeSubmitDescriptorStateToJson(lighting.RuntimeSubmitDescriptor) },
        { "vertex_hemisphere_uniform_trace_decoded",
          lighting.VertexHemisphereUniformTraceDecoded },
        { "vertex_hemisphere_uniform_trace_used_as_runtime_source",
          lighting.VertexHemisphereUniformTraceUsedAsRuntimeSource },
        { "vertex_hemisphere_uniform_trace_source_kind",
          lighting.VertexHemisphereUniformTraceSourceKind },
        { "vertex_hemisphere_uniform_trace_format",
          lighting.VertexHemisphereUniformTraceFormat },
        { "vertex_hemisphere_uniform_trace_formula",
          lighting.VertexHemisphereUniformTraceFormula },
        { "vertex_hemisphere_uniform_trace_draw_index",
          lighting.VertexHemisphereUniformTraceDrawIndex },
        { "vertex_hemisphere_uniform_trace_candidate_draw_count",
          lighting.VertexHemisphereUniformTraceCandidateDrawCount },
        { "vertex_hemisphere_uniform_trace_candidate_vertex_count",
          lighting.VertexHemisphereUniformTraceCandidateVertexCount },
        { "vertex_hemisphere_ambient_uniform_index",
          lighting.VertexHemisphereAmbientUniformIndex },
        { "vertex_hemisphere_diffuse_uniform_index",
          lighting.VertexHemisphereDiffuseUniformIndex },
        { "vertex_hemisphere_light_vector_uniform_index",
          lighting.VertexHemisphereLightVectorUniformIndex },
        { "vertex_hemisphere_ambient_color",
          ColorJson(lighting.VertexHemisphereAmbientColor) },
        { "vertex_hemisphere_diffuse_color",
          ColorJson(lighting.VertexHemisphereDiffuseColor) },
        { "vertex_hemisphere_secondary_color",
          ColorJson(lighting.VertexHemisphereSecondaryColor) },
        { "vertex_hemisphere_light_vector",
          Vec3Json(lighting.VertexHemisphereLightVector) },
        { "vertex_hemisphere_negated_light_vector",
          Vec3Json(lighting.VertexHemisphereNegatedLightVector) },
        { "vertex_hemisphere_world_light_vector_decoded",
          lighting.VertexHemisphereWorldLightVectorDecoded },
        { "vertex_hemisphere_world_light_vector",
          Vec3Json(lighting.VertexHemisphereWorldLightVector) },
        { "vertex_hemisphere_world_negated_light_vector",
          Vec3Json(lighting.VertexHemisphereWorldNegatedLightVector) },
        { "vertex_hemisphere_runtime_color_source",
          lighting.VertexHemisphereRuntimeColorSource },
        { "vertex_hemisphere_runtime_vector_source",
          lighting.VertexHemisphereRuntimeVectorSource },
        { "vertex_modulation_color", ColorJson(lighting.VertexModulationColor) },
        { "directional_light_count", lighting.DirectionalLightCount },
        { "cmb_vshader_lighting_accumulator_decoded",
          lighting.CmbVShaderLightingAccumulatorDecoded },
        { "cmb_vshader_lighting_accumulator_slot_count",
          lighting.CmbVShaderLightingAccumulatorSlotCount },
        { "cmb_vshader_lighting_accumulator_source",
          lighting.CmbVShaderLightingAccumulatorSource },
        { "directional_lighting_applied", lighting.DirectionalLightingApplied },
        { "vertex_hemisphere_lighting_supported", lighting.VertexHemisphereLightingSupported },
        { "native_normal_vertex_count", lighting.NativeNormalVertexCount },
        { "modulate_textured_batches", lighting.ModulateTexturedBatches },
        { "preserve_vertex_alpha", lighting.PreserveVertexAlpha },
        { "applied_batch_count", lighting.AppliedBatchCount },
        { "applied_textured_batch_count", lighting.AppliedTexturedBatchCount },
        { "applied_vertex_count", lighting.AppliedVertexCount },
        { "applied_vertex_lighting_batch_count", lighting.AppliedVertexLightingBatchCount },
        { "applied_hemisphere_lighting_batch_count", lighting.AppliedHemisphereLightingBatchCount },
        { "material_lut_input_packet_available_batch_count",
          lighting.MaterialLutInputPacketAvailableBatchCount },
        { "material_lut_input_packet_complete_batch_count",
          lighting.MaterialLutInputPacketCompleteBatchCount },
        { "material_lut_input_fragment_lighting_batch_count",
          lighting.MaterialLutInputFragmentLightingBatchCount },
        { "material_lut_input_evaluation_applied_batch_count",
          lighting.MaterialLutInputEvaluationAppliedBatchCount },
        { "material_lut_input_evaluation_pending_batch_count",
          lighting.MaterialLutInputEvaluationPendingBatchCount },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativePicaLightingDebugStateToJson(const Oot3dNativePicaLightingDebugState& debug) {
    return {
        { "available", debug.Available },
        { "semantic_table_available", debug.SemanticTableAvailable },
        { "source_kind", debug.SourceKind },
        { "mode", debug.Mode },
        { "record_selector", debug.RecordSelector },
        { "active_setup_index", debug.ActiveSetupIndex },
        { "record_index", debug.RecordIndex },
        { "record_offset", debug.RecordOffset },
        { "ambient_group_index", debug.AmbientGroupIndex },
        { "diffuse_group_index", debug.DiffuseGroupIndex },
        { "ambient_color_source", debug.AmbientColorSource },
        { "diffuse_color_source", debug.DiffuseColorSource },
        { "ambient_color_offset", debug.AmbientColorOffset },
        { "diffuse_color_offset", debug.DiffuseColorOffset },
        { "ambient_color", ColorJson(debug.AmbientColor) },
        { "diffuse_color", ColorJson(debug.DiffuseColor) },
        { "debug_color", ColorJson(debug.DebugColor) },
        { "force_untextured_batches", debug.ForceUntexturedBatches },
        { "preserve_vertex_alpha", debug.PreserveVertexAlpha },
        { "applied_batch_count", debug.AppliedBatchCount },
        { "applied_vertex_count", debug.AppliedVertexCount },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeRenderCtxbDescriptorSlotToJson(const NativeCtxbDescriptorSlot& slot) {
    return {
        { "slot_index", slot.SlotIndex },
        { "slot_stride_bytes", slot.SlotStrideBytes },
        { "slot_record_base_offset", HexU32(slot.SlotRecordBaseOffset, 3) },
        { "source_texture_header_base_offset", HexU32(slot.SourceTextureHeaderBaseOffset, 2) },
        { "source_payload_pointer_field_offset", HexU32(slot.SourcePayloadPointerFieldOffset, 2) },
        { "texture_header_parameter", slot.TextureHeaderParameter },
        { "width", slot.Width },
        { "height", slot.Height },
        { "texture_format", HexU32(slot.TextureFormat, 4) },
        { "data_type", HexU32(slot.DataType, 4) },
        { "packed_format_data_type", HexU32(slot.PackedFormatDataType, 8) },
        { "payload_size", slot.PayloadSize },
    };
}

nlohmann::json Oot3dNativeKankyoRuntimeBindingSlotToJson(
    const NativeKankyoRuntimeBindingSlot& slot) {
    return {
        { "binding_index", slot.BindingIndex },
        { "slot_index", slot.SlotIndex },
        { "subresource_id_start", HexU32(slot.SubresourceIdStart, 2) },
        { "subresource_id_end", HexU32(slot.SubresourceIdEnd, 2) },
        { "initial_subresource_id", HexU32(slot.InitialSubresourceId, 2) },
        { "descriptor_object_offset", HexU32(slot.DescriptorObjectOffset, 3) },
        { "descriptor_slot", slot.DescriptorSlot },
        { "runtime_instance_offset", HexU32(slot.RuntimeInstanceOffset, 3) },
        { "runtime_helper_address", HexU32(slot.RuntimeHelperAddress, 8) },
        { "runtime_callsite_address", HexU32(slot.RuntimeCallsiteAddress, 8) },
        { "shares_runtime_instance_across_descriptor_slots",
          slot.SharesRuntimeInstanceAcrossDescriptorSlots },
        { "uses_dynamic_subresource_selector", slot.UsesDynamicSubresourceSelector },
        { "uses_submit_manager", slot.UsesSubmitManager },
        { "uses_render_record_scheduler", slot.UsesRenderRecordScheduler },
    };
}

nlohmann::json Oot3dNativeKankyoRuntimeCtxbStateToJson(
    const Oot3dNativeKankyoRuntimeCtxbState& state) {
    nlohmann::json runtimeSlots = nlohmann::json::array();
    for (const auto& slot : state.RuntimeBindingSlots) {
        runtimeSlots.push_back(Oot3dNativeKankyoRuntimeBindingSlotToJson(slot));
    }

    return {
        { "resolved", state.Resolved },
        { "native_subresource_id", HexU32(state.NativeSubresourceId, 2) },
        { "effect_class", state.EffectClass },
        { "archive_path", state.ArchivePath.string() },
        { "entry_index", state.EntryIndex },
        { "entry_name", state.EntryName },
        { "entry_type_local_index", HexU32(state.EntryTypeLocalIndex, 2) },
        { "entry_offset", HexU32(state.EntryOffset, 8) },
        { "entry_size", state.EntrySize },
        { "width", state.Width },
        { "height", state.Height },
        { "texture_format", HexU32(state.TextureFormat, 4) },
        { "data_type", HexU32(state.DataType, 4) },
        { "payload_size", state.PayloadSize },
        { "descriptor_slot", Oot3dNativeRenderCtxbDescriptorSlotToJson(state.DescriptorSlot) },
        { "runtime_binding_slot_count", state.RuntimeBindingSlots.size() },
        { "runtime_binding_slots", runtimeSlots },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeKankyoCommonTbdRgba8ToJson(
    const Oot3dNativeKankyoCommonTbdRgba8& color) {
    return {
        { "r", static_cast<uint32_t>(color.R) },
        { "g", static_cast<uint32_t>(color.G) },
        { "b", static_cast<uint32_t>(color.B) },
        { "a", static_cast<uint32_t>(color.A) },
    };
}

nlohmann::json Oot3dNativeKankyoCommonTbdRecordToJson(
    const Oot3dNativeKankyoCommonTbdRecord& record) {
    nlohmann::json byteValues = nlohmann::json::array();
    for (const uint8_t value : record.ByteValues) {
        byteValues.push_back(static_cast<uint32_t>(value));
    }

    nlohmann::json rgbaValues = nlohmann::json::array();
    for (const auto& color : record.Rgba8Values) {
        rgbaValues.push_back(Oot3dNativeKankyoCommonTbdRgba8ToJson(color));
    }

    return {
        { "resolved", record.Resolved },
        { "record_index", record.RecordIndex },
        { "record_offset", HexU32(record.RecordOffset, 8) },
        { "name", record.Name },
        { "native_index", record.NativeIndex },
        { "record_size", record.RecordSize },
        { "type", record.Type },
        { "value_count", record.ValueCount },
        { "payload_offset", HexU32(record.PayloadOffset, 8) },
        { "payload_size", record.PayloadSize },
        { "record_size_matches", record.RecordSizeMatches },
        { "value_count_matches_payload", record.ValueCountMatchesPayload },
        { "float_values", record.FloatValues },
        { "byte_values", byteValues },
        { "rgba8_values", rgbaValues },
        { "payload_first_32_bytes_hex", record.PayloadFirst32BytesHex },
        { "source_status", record.SourceStatus },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeKankyoCommonTbdStateToJson(
    const Oot3dNativeKankyoCommonTbdState& state) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : state.Records) {
        records.push_back(Oot3dNativeKankyoCommonTbdRecordToJson(record));
    }

    return {
        { "resolved", state.Resolved },
        { "archive_path", state.ArchivePath.string() },
        { "entry_index", state.EntryIndex },
        { "entry_name", state.EntryName },
        { "entry_type_local_index", HexU32(state.EntryTypeLocalIndex, 2) },
        { "entry_offset", HexU32(state.EntryOffset, 8) },
        { "entry_size", state.EntrySize },
        { "magic", HexU32(state.Magic, 8) },
        { "version", state.Version },
        { "declared_size", state.DeclaredSize },
        { "entry_count", state.EntryCount },
        { "declared_size_matches", state.DeclaredSizeMatches },
        { "record_table_resolved", state.RecordTableResolved },
        { "resolved_record_count", state.ResolvedRecordCount },
        { "records", records },
        { "first_32_bytes_hex", state.First32BytesHex },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeKankyoLensEffectElementToJson(
    const Oot3dNativeKankyoLensEffectElement& element) {
    return {
        { "index", element.Index },
        { "scale_min", element.ScaleMin },
        { "scale_max", element.ScaleMax },
        { "offset", element.Offset },
        { "depth", element.Depth },
    };
}

nlohmann::json Oot3dNativeKankyoOverlayGeometryCoordToJson(
    const Oot3dNativeKankyoOverlayGeometryCoord& coord) {
    return {
        { "index", coord.Index },
        { "x", coord.X },
        { "y", coord.Y },
    };
}

nlohmann::json Oot3dNativeKankyoPrimitiveBackendVertexInputToJson(
    const Oot3dNativeKankyoPrimitiveBackendVertexInput& vertex) {
    return {
        { "index", vertex.Index },
        { "x", vertex.X },
        { "y", vertex.Y },
        { "color", ColorJson(vertex.Color) },
    };
}

nlohmann::json Oot3dNativeKankyoRuntime28CVisibleBatchInputToJson(
    const Oot3dNativeKankyoRuntime28CVisibleBatchInput& batch) {
    return {
        { "visible_batch_index", batch.VisibleBatchIndex },
        { "source_element_index", batch.SourceElementIndex },
        { "lens_scale_min", batch.LensScaleMin },
        { "lens_scale_max", batch.LensScaleMax },
        { "lens_offset", batch.LensOffset },
        { "lens_depth", batch.LensDepth },
        { "lens_halation_color", ColorJson(batch.LensHalationColor) },
    };
}

nlohmann::json Oot3dNativeKankyoRuntime28CQuadLaneVertexInputToJson(
    const Oot3dNativeKankyoRuntime28CQuadLaneVertexInput& vertex) {
    return {
        { "visible_batch_index", vertex.VisibleBatchIndex },
        { "source_element_index", vertex.SourceElementIndex },
        { "corner_index", vertex.CornerIndex },
        { "position_record_byte_offset", HexU32(vertex.PositionRecordByteOffset, 3) },
        { "matrix_record_byte_offset", HexU32(vertex.MatrixRecordByteOffset, 3) },
        { "color_record_byte_offset", HexU32(vertex.ColorRecordByteOffset, 3) },
        { "texcoord_record_byte_offset", HexU32(vertex.TexcoordRecordByteOffset, 3) },
        { "texcoord_corner_base_u_offset", HexU32(vertex.TexcoordCornerBaseUOffset, 3) },
        { "texcoord_corner_base_v_offset", HexU32(vertex.TexcoordCornerBaseVOffset, 3) },
    };
}

nlohmann::json Oot3dNativeKankyoRuntime28CProjectionSampleToJson(
    const Oot3dNativeKankyoRuntime28CProjectionSample& sample) {
    return {
        { "source_kind", sample.SourceKind },
        { "world_position", DemoVec3Json(sample.WorldPosition) },
        { "clip", {
              { "x", sample.ClipX },
              { "y", sample.ClipY },
              { "z", sample.ClipZ },
              { "w", sample.ClipW },
          } },
        { "inverse_w", sample.InverseW },
        { "native_w_clamp_applied", sample.NativeWClampApplied },
        { "screen", { { "x", sample.ScreenX }, { "y", sample.ScreenY } } },
    };
}

nlohmann::json Oot3dNativeKankyoRuntime28CPositionRecordToJson(
    const Oot3dNativeKankyoRuntime28CPositionRecord& record) {
    return {
        { "visible_batch_index", record.VisibleBatchIndex },
        { "source_element_index", record.SourceElementIndex },
        { "position_record_byte_offset", HexU32(record.PositionRecordByteOffset, 3) },
        { "lens_offset", record.LensOffset },
        { "lens_depth", record.LensDepth },
        { "source_world_position", DemoVec3Json(record.SourceWorldPosition) },
        { "kankyo_source_offset", DemoVec3Json(record.KankyoSourceOffset) },
        { "runtime_position", DemoVec3Json(record.RuntimePosition) },
        { "projection", Oot3dNativeKankyoRuntime28CProjectionSampleToJson(record.Projection) },
        { "runtime_distance_factor", record.RuntimeDistanceFactor },
        { "runtime_interpolated_scale", record.RuntimeInterpolatedScale },
        { "runtime_scale", record.RuntimeScale },
        { "runtime_scale_argument", record.RuntimeScaleArgument },
        { "runtime_scale_multiplier", record.RuntimeScaleMultiplier },
    };
}

nlohmann::json Oot3dNativeKankyoPrimitiveBackendInputStateToJson(
    const Oot3dNativeKankyoPrimitiveBackendInputState& input) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const auto& vertex : input.OverlayVertices) {
        vertices.push_back(Oot3dNativeKankyoPrimitiveBackendVertexInputToJson(vertex));
    }
    nlohmann::json runtimeBatches = nlohmann::json::array();
    for (const auto& batch : input.Runtime28CVisibleBatches) {
        runtimeBatches.push_back(Oot3dNativeKankyoRuntime28CVisibleBatchInputToJson(batch));
    }
    nlohmann::json runtimeQuadLaneVertices = nlohmann::json::array();
    for (const auto& vertex : input.Runtime28CQuadLaneVertices) {
        runtimeQuadLaneVertices.push_back(
            Oot3dNativeKankyoRuntime28CQuadLaneVertexInputToJson(vertex));
    }
    nlohmann::json projectionSamples = nlohmann::json::array();
    for (const auto& sample : input.Runtime28CPositionProjectionSamples) {
        projectionSamples.push_back(
            Oot3dNativeKankyoRuntime28CProjectionSampleToJson(sample));
    }
    nlohmann::json positionRecords = nlohmann::json::array();
    for (const auto& record : input.Runtime28CPositionRecords) {
        positionRecords.push_back(
            Oot3dNativeKankyoRuntime28CPositionRecordToJson(record));
    }
    return {
        { "available", input.Available },
        { "primitive_packet_bridge_resolved", input.PrimitivePacketBridgeResolved },
        { "attribute_mask_packet_resolved", input.AttributeMaskPacketResolved },
        { "quad_batch_lane_plan_resolved", input.QuadBatchLanePlanResolved },
        { "runtime28c_particle_batch_contract_resolved",
          input.Runtime28CParticleBatchContractResolved },
        { "runtime28c_enqueue_dispatch_resolved",
          input.Runtime28CEnqueueDispatchResolved },
        { "runtime28c_position_producer_resolved",
          input.Runtime28CPositionProducerResolved },
        { "runtime28c_position_runtime_input_resolved",
          input.Runtime28CPositionRuntimeInputResolved },
        { "runtime28c_position_runtime_scale_resolved",
          input.Runtime28CPositionRuntimeScaleResolved },
        { "runtime28c_visible_batch_inputs_resolved",
          input.Runtime28CVisibleBatchInputsResolved },
        { "runtime28c_quad_lane_materialized",
          input.Runtime28CQuadLaneMaterialized },
        { "texture_input_resolved", input.TextureInputResolved },
        { "decoded_texture_input_resolved", input.DecodedTextureInputResolved },
        { "terminal_texture_input_resolved", input.TerminalTextureInputResolved },
        { "terminal_decoded_texture_input_resolved",
          input.TerminalDecodedTextureInputResolved },
        { "terminal_element_input_resolved", input.TerminalElementInputResolved },
        { "texture_has_native_alpha", input.TextureHasNativeAlpha },
        { "native_pica_alpha_blend_semantics_required",
          input.NativePicaAlphaBlendSemanticsRequired },
        { "native_pica_alpha_blend_semantics_resolved",
          input.NativePicaAlphaBlendSemanticsResolved },
        { "geometry_input_resolved", input.GeometryInputResolved },
        { "color_input_resolved", input.ColorInputResolved },
        { "ready_for_backend_input", input.ReadyForBackendInput },
        { "visible_backend_submit_resolved", input.VisibleBackendSubmitResolved },
        { "ready_for_backend_render", input.ReadyForBackendRender },
        { "material_scalar_runtime_list_binder_resolved",
          input.MaterialScalarRuntimeListBinderResolved },
        { "material_scalar_runtime_list_source_matches_fog_source",
          input.MaterialScalarRuntimeListSourceMatchesFogSource },
        { "material_scalar_runtime_list_packet_resolver_resolved",
          input.MaterialScalarRuntimeListPacketResolverResolved },
        { "material_scalar_gameplay_draw_order_resolved",
          input.MaterialScalarGameplayDrawOrderResolved },
        { "source_kind", input.SourceKind },
        { "source_status", input.SourceStatus },
        { "material_scalar_runtime_list_source_status",
          input.MaterialScalarRuntimeListSourceStatus },
        { "native_pica_alpha_blend_semantics_source_status",
          input.NativePicaAlphaBlendSemanticsSourceStatus },
        { "visible_backend_blocked_reason", input.VisibleBackendBlockedReason },
        { "runtime28c_position_runtime_input_blocked_reason",
          input.Runtime28CPositionRuntimeInputBlockedReason },
        { "material_scalar_gameplay_draw_function_address",
          HexU32(input.MaterialScalarGameplayDrawFunctionAddress, 8) },
        { "material_scalar_gameplay_fog_update_callsite_address",
          HexU32(input.MaterialScalarGameplayFogUpdateCallsiteAddress, 8) },
        { "material_scalar_gameplay_runtime_list_0_bind_callsite_address",
          HexU32(input.MaterialScalarGameplayRuntimeList0BindCallsiteAddress, 8) },
        { "material_scalar_gameplay_runtime_list_1_bind_callsite_address",
          HexU32(input.MaterialScalarGameplayRuntimeList1BindCallsiteAddress, 8) },
        { "material_scalar_gameplay_view_play_offset",
          HexU32(input.MaterialScalarGameplayViewPlayOffset, 4) },
        { "material_scalar_gameplay_final_fog_rgb_play_offset",
          HexU32(input.MaterialScalarGameplayFinalFogRgbPlayOffset, 4) },
        { "material_scalar_gameplay_fog_far_play_offset",
          HexU32(input.MaterialScalarGameplayFogFarPlayOffset, 4) },
        { "material_scalar_gameplay_fog_near_play_offset",
          HexU32(input.MaterialScalarGameplayFogNearPlayOffset, 4) },
        { "material_scalar_gameplay_fog_alpha_immediate",
          HexU32(input.MaterialScalarGameplayFogAlphaImmediate, 2) },
        { "material_scalar_runtime_list_binder_address",
          HexU32(input.MaterialScalarRuntimeListBinderAddress, 8) },
        { "material_scalar_runtime_list_binder_source_play_offset",
          HexU32(input.MaterialScalarRuntimeListBinderSourcePlayOffset, 4) },
        { "material_scalar_runtime_list_0_play_offset",
          HexU32(input.MaterialScalarRuntimeList0PlayOffset, 4) },
        { "material_scalar_runtime_list_1_play_offset",
          HexU32(input.MaterialScalarRuntimeList1PlayOffset, 4) },
        { "material_scalar_runtime_list_count_byte_offset",
          HexU32(input.MaterialScalarRuntimeListCountByteOffset, 2) },
        { "material_scalar_runtime_list_entry_stride_bytes",
          input.MaterialScalarRuntimeListEntryStrideBytes },
        { "material_scalar_runtime_list_entry_runtime_object_pointer_offset",
          HexU32(input.MaterialScalarRuntimeListEntryRuntimeObjectPointerOffset, 2) },
        { "material_scalar_runtime_object_packet_resolver_address",
          HexU32(input.MaterialScalarRuntimeObjectPacketResolverAddress, 8) },
        { "material_scalar_runtime_object_packet_owner_pointer_offset",
          HexU32(input.MaterialScalarRuntimeObjectPacketOwnerPointerOffset, 2) },
        { "material_scalar_runtime_object_packet_owner_material_packet_offset",
          HexU32(input.MaterialScalarRuntimeObjectPacketOwnerMaterialPacketOffset, 2) },
        { "texture_name", input.TextureName },
        { "texture", NativeTextureJson(input.Texture) },
        { "terminal_texture_name", input.TerminalTextureName },
        { "terminal_texture", NativeTextureJson(input.TerminalTexture) },
        { "primary_batch_last_element_index", input.PrimaryBatchLastElementIndex },
        { "terminal_element_index", input.TerminalElementIndex },
        { "terminal_native_vertex_count", input.TerminalNativeVertexCount },
        { "terminal_submit_element_count", input.TerminalSubmitElementCount },
        { "native_lens_visibility_screen_gate_resolved",
          input.NativeLensVisibilityScreenGateResolved },
        { "native_lens_visibility_scene_occlusion_resolved",
          input.NativeLensVisibilitySceneOcclusionResolved },
        { "native_lens_visibility_source_in_viewport",
          input.NativeLensVisibilitySourceInViewport },
        { "native_lens_visibility_source_occluded",
          input.NativeLensVisibilitySourceOccluded },
        { "native_lens_visibility_target", input.NativeLensVisibilityTarget },
        { "native_lens_visibility_visible_target",
          input.NativeLensVisibilityVisibleTarget },
        { "native_lens_visibility_scale", input.NativeLensVisibilityScale },
        { "native_lens_visibility_max_step", input.NativeLensVisibilityMaxStep },
        { "native_lens_visibility_min_step", input.NativeLensVisibilityMinStep },
        { "native_lens_visibility_screen_max_x",
          input.NativeLensVisibilityScreenMaxX },
        { "native_lens_visibility_screen_max_y",
          input.NativeLensVisibilityScreenMaxY },
        { "native_lens_visibility_occluder_distance",
          input.NativeLensVisibilityOccluderDistance },
        { "native_lens_visibility_occluder_model",
          input.NativeLensVisibilityOccluderModel },
        { "native_lens_visibility_source_status",
          input.NativeLensVisibilitySourceStatus },
        { "runtime28c_position_producer_function_address",
          HexU32(input.Runtime28CPositionProducerFunctionAddress, 8) },
        { "runtime28c_position_projection_helper_address",
          HexU32(input.Runtime28CPositionProjectionHelperAddress, 8) },
        { "runtime28c_position_view_projection_helper_address",
          HexU32(input.Runtime28CPositionViewProjectionHelperAddress, 8) },
        { "runtime28c_position_view_projection_matrix_play_offset",
          HexU32(input.Runtime28CPositionViewProjectionMatrixPlayOffset, 4) },
        { "runtime28c_position_view_projection_source_resolved",
          input.Runtime28CPositionViewProjectionSourceResolved },
        { "runtime28c_position_gameplay_draw_function_address",
          HexU32(input.Runtime28CPositionGameplayDrawFunctionAddress, 8) },
        { "runtime28c_position_view_source_producer_function_address",
          HexU32(input.Runtime28CPositionViewSourceProducerFunctionAddress, 8) },
        { "runtime28c_position_view_source_producer_callsite_address",
          HexU32(input.Runtime28CPositionViewSourceProducerCallsiteAddress, 8) },
        { "runtime28c_position_view_prep_function_address",
          HexU32(input.Runtime28CPositionViewPrepFunctionAddress, 8) },
        { "runtime28c_position_view_prep_callsite_address",
          HexU32(input.Runtime28CPositionViewPrepCallsiteAddress, 8) },
        { "runtime28c_position_view_update_function_address",
          HexU32(input.Runtime28CPositionViewUpdateFunctionAddress, 8) },
        { "runtime28c_position_view_update_callsite_address",
          HexU32(input.Runtime28CPositionViewUpdateCallsiteAddress, 8) },
        { "runtime28c_position_view_projection_source_copy_helper_address",
          HexU32(input.Runtime28CPositionViewProjectionSourceCopyHelperAddress, 8) },
        { "runtime28c_position_view_projection_source_copy_callsite_address",
          HexU32(input.Runtime28CPositionViewProjectionSourceCopyCallsiteAddress, 8) },
        { "runtime28c_position_view_projection_compose_copy_helper_address",
          HexU32(input.Runtime28CPositionViewProjectionComposeCopyHelperAddress, 8) },
        { "runtime28c_position_view_projection_compose_copy_callsite_address",
          HexU32(input.Runtime28CPositionViewProjectionComposeCopyCallsiteAddress, 8) },
        { "runtime28c_position_view_projection_compose_helper_address",
          HexU32(input.Runtime28CPositionViewProjectionComposeHelperAddress, 8) },
        { "runtime28c_position_view_projection_compose_callsite_address",
          HexU32(input.Runtime28CPositionViewProjectionComposeCallsiteAddress, 8) },
        { "runtime28c_position_view_struct_play_offset",
          HexU32(input.Runtime28CPositionViewStructPlayOffset, 4) },
        { "runtime28c_position_view_projection_source_matrix_play_offset",
          HexU32(input.Runtime28CPositionViewProjectionSourceMatrixPlayOffset, 4) },
        { "runtime28c_position_view_projection_compose_matrix_play_offset",
          HexU32(input.Runtime28CPositionViewProjectionComposeMatrixPlayOffset, 4) },
        { "runtime28c_position_view_projection_matrix_word_count",
          input.Runtime28CPositionViewProjectionMatrixWordCount },
        { "runtime28c_position_view_projection_compose_matrix_word_count",
          input.Runtime28CPositionViewProjectionComposeMatrixWordCount },
        { "runtime28c_position_view_projection_identity_row_patched",
          input.Runtime28CPositionViewProjectionIdentityRowPatched },
        { "runtime28c_position_view_projection_runtime_matrix_materialized",
          input.Runtime28CPositionViewProjectionRuntimeMatrixMaterialized },
        { "runtime28c_position_view_projection_runtime_matrix_source_kind",
          input.Runtime28CPositionViewProjectionRuntimeMatrixSourceKind },
        { "runtime28c_position_view_projection_runtime_matrix_status",
          input.Runtime28CPositionViewProjectionRuntimeMatrixStatus },
        { "runtime28c_position_view_projection_runtime_matrix",
          MatrixJson(input.Runtime28CPositionViewProjectionRuntimeMatrix) },
        { "runtime28c_position_projection_samples", projectionSamples },
        { "runtime28c_position_record_count",
          input.Runtime28CPositionRecords.size() },
        { "runtime28c_position_records", positionRecords },
        { "runtime28c_position_screen_center_x",
          input.Runtime28CPositionScreenCenterX },
        { "runtime28c_position_screen_center_y",
          input.Runtime28CPositionScreenCenterY },
        { "runtime28c_position_base_z",
          input.Runtime28CPositionBaseZ },
        { "runtime28c_position_direction_scale",
          input.Runtime28CPositionDirectionScale },
        { "runtime28c_position_distance_limit_scale",
          input.Runtime28CPositionDistanceLimitScale },
        { "runtime28c_position_runtime_scale_argument",
          input.Runtime28CPositionRuntimeScaleArgument },
        { "runtime28c_position_runtime_scale_multiplier_address",
          HexU32(input.Runtime28CPositionRuntimeScaleMultiplierAddress, 8) },
        { "runtime28c_position_runtime_scale_multiplier",
          input.Runtime28CPositionRuntimeScaleMultiplier },
        { "runtime28c_position_projection_scale_x",
          input.Runtime28CPositionProjectionScaleX },
        { "runtime28c_position_projection_scale_y",
          input.Runtime28CPositionProjectionScaleY },
        { "runtime28c_position_projection_base_y",
          input.Runtime28CPositionProjectionBaseY },
        { "primitive_draw_packet_function_address",
          HexU32(input.PrimitiveDrawPacketFunctionAddress, 8) },
        { "primitive_draw_attribute_mask_function_address",
          HexU32(input.PrimitiveDrawAttributeMaskFunctionAddress, 8) },
        { "primitive_draw_command_commit_address",
          HexU32(input.PrimitiveDrawCommandCommitAddress, 8) },
        { "primitive_draw_packet_word_count", input.PrimitiveDrawPacketWordCount },
        { "primitive_draw_index_element_type",
          HexU32(input.PrimitiveDrawIndexElementType, 4) },
        { "primitive_draw_effect_stack_index_base",
          input.PrimitiveDrawEffectStackIndexBase },
        { "primitive_draw_attribute_mask_header_word",
          HexU32(input.PrimitiveDrawAttributeMaskHeaderWord, 8) },
        { "primitive_draw_attribute_mask_payload_or_mask",
          HexU32(input.PrimitiveDrawAttributeMaskPayloadOrMask, 8) },
        { "runtime_draw_count_offset", HexU32(input.RuntimeDrawCountOffset, 3) },
        { "quad_batch_expanded_vertex_count", input.QuadBatchExpandedVertexCount },
        { "quad_batch_visible_element_count", input.QuadBatchVisibleElementCount },
        { "quad_batch_native_vertices_per_quad", input.QuadBatchNativeVerticesPerQuad },
        { "runtime28c_quad_vertex_count", input.Runtime28CQuadVertexCount },
        { "runtime28c_draw_count_per_visible_batch",
          input.Runtime28CDrawCountPerVisibleBatch },
        { "runtime28c_position_record_stride_bytes",
          input.Runtime28CPositionRecordStrideBytes },
        { "runtime28c_matrix_record_stride_bytes",
          input.Runtime28CMatrixRecordStrideBytes },
        { "runtime28c_local_vector_record_stride_bytes",
          input.Runtime28CLocalVectorRecordStrideBytes },
        { "runtime28c_color_record_stride_bytes",
          input.Runtime28CColorRecordStrideBytes },
        { "runtime28c_texcoord_record_stride_bytes",
          input.Runtime28CTexcoordRecordStrideBytes },
        { "runtime28c_batch_capacity_offset",
          HexU32(input.Runtime28CBatchCapacityOffset, 3) },
        { "runtime28c_local_vector_array_pointer_offset",
          HexU32(input.Runtime28CLocalVectorArrayPointerOffset, 3) },
        { "runtime28c_quad_lane_vertex_input_count",
          input.Runtime28CQuadLaneVertexInputCount },
        { "runtime28c_draw_count", input.Runtime28CDrawCount },
        { "runtime28c_queued_element_count",
          input.Runtime28CQueuedElementCount },
        { "runtime28c_special_submit_element_count",
          input.Runtime28CSpecialSubmitElementCount },
        { "runtime28c_special_submit_start_element_index",
          input.Runtime28CSpecialSubmitStartElementIndex },
        { "runtime28c_special_submit_end_element_index",
          input.Runtime28CSpecialSubmitEndElementIndex },
        { "runtime28c_primary_submit_queue_index_base",
          HexU32(input.Runtime28CPrimarySubmitQueueIndexBase, 2) },
        { "runtime28c_terminal_submit_queue_index_base",
          HexU32(input.Runtime28CTerminalSubmitQueueIndexBase, 2) },
        { "overlay_primitive_vertex_count", input.OverlayPrimitiveVertexCount },
        { "color_pass_draw_mode_raw", input.ColorPassDrawModeRaw },
        { "color_pass_primitive_value", input.ColorPassPrimitiveValue },
        { "alpha_pass_draw_mode_raw", input.AlphaPassDrawModeRaw },
        { "alpha_pass_primitive_value", input.AlphaPassPrimitiveValue },
        { "primitive_draw_packet_literal_words", input.PrimitiveDrawPacketLiteralWords },
        { "runtime28c_visible_batches", runtimeBatches },
        { "terminal_element_input",
          Oot3dNativeKankyoRuntime28CVisibleBatchInputToJson(
              input.TerminalElementInput) },
        { "runtime28c_quad_lane_vertices", runtimeQuadLaneVertices },
        { "overlay_vertices", vertices },
    };
}

nlohmann::json Oot3dNativeKankyoMoonStateToJson(
    const Oot3dNativeKankyoMoonState& state) {
    nlohmann::json layers = nlohmann::json::array();
    for (const auto& layer : state.Layers) {
        layers.push_back({
            { "ctxb_type_local_index", layer.CtxbTypeLocalIndex },
            { "texture_name", layer.TextureName },
            { "width", layer.Width },
            { "height", layer.Height },
            { "texture_format", HexU32(layer.TextureFormat, 4) },
            { "data_type", HexU32(layer.DataType, 4) },
            { "geometry_template_index", layer.GeometryTemplateIndex },
            { "geometry_template_half_extent", layer.GeometryTemplateHalfExtent },
            { "runtime_object_template_index", layer.RuntimeObjectTemplateIndex },
            { "min_mag_filter", HexU32(layer.MinMagFilter, 4) },
            { "wrap_s", HexU32(layer.WrapS, 4) },
            { "wrap_t", HexU32(layer.WrapT, 4) },
            { "runtime_materialized", layer.RuntimeMaterialized },
            { "runtime_world_center", DemoVec3Json(layer.RuntimeWorldCenter) },
            { "runtime_scale", layer.RuntimeScale },
            { "runtime_uv_max", layer.RuntimeUvMax },
            { "runtime_additive_blend", layer.RuntimeAdditiveBlend },
            { "runtime_alpha_test", layer.RuntimeAlphaTest },
        });
    }
    return {
        { "available", state.Available },
        { "native_init_resolved", state.NativeInitResolved },
        { "texture_inputs_resolved", state.TextureInputsResolved },
        { "ready_for_backend_input", state.ReadyForBackendInput },
        { "native_visible_backend_submit_resolved", state.NativeVisibleBackendSubmitResolved },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "sky_context_init_function_address", HexU32(state.SkyContextInitFunctionAddress, 8) },
        { "moon_init_function_address", HexU32(state.MoonInitFunctionAddress, 8) },
        { "moon_init_callsite_address", HexU32(state.MoonInitCallsiteAddress, 8) },
        { "ctxb_base_type_local_index", state.CtxbBaseTypeLocalIndex },
        { "layer_count", state.LayerCount },
        { "runtime_transform_resolved", state.RuntimeTransformResolved },
        { "runtime_active_angle", state.RuntimeActiveAngle },
        { "runtime_celestial_vector", DemoVec3Json(state.RuntimeCelestialVector) },
        { "runtime_moon_direction", DemoVec3Json(state.RuntimeMoonDirection) },
        { "runtime_transform_source", state.RuntimeTransformSource },
        { "layers", layers },
    };
}

nlohmann::json Oot3dNativeKankyoSunHaloStateToJson(
    const Oot3dNativeKankyoSunHaloState& state) {
    nlohmann::json profileTextures = nlohmann::json::array();
    for (const auto& texture : state.ProfileTextures) {
        profileTextures.push_back({
            { "texture_input_resolved", texture.TextureInputResolved },
            { "profile_group_index", texture.ProfileGroupIndex },
            { "ctxb_type_local_index", texture.CtxbTypeLocalIndex },
            { "texture_name", texture.TextureName },
            { "width", texture.Width },
            { "height", texture.Height },
            { "texture_format", HexU32(texture.TextureFormat, 4) },
            { "data_type", HexU32(texture.DataType, 4) },
        });
    }
    return {
        { "available", state.Available },
        { "native_route_resolved", state.NativeRouteResolved },
        { "texture_input_resolved", state.TextureInputResolved },
        { "sampler_state_resolved", state.SamplerStateResolved },
        { "ready_for_backend_input", state.ReadyForBackendInput },
        { "runtime_transform_resolved", state.RuntimeTransformResolved },
        { "native_visible_backend_submit_resolved",
          state.NativeVisibleBackendSubmitResolved },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "texture_name", state.TextureName },
        { "width", state.Width },
        { "height", state.Height },
        { "texture_format", HexU32(state.TextureFormat, 4) },
        { "data_type", HexU32(state.DataType, 4) },
        { "celestial_vector_producer_function_address",
          HexU32(state.CelestialVectorProducerFunctionAddress, 8) },
        { "group_submit_function_address",
          HexU32(state.GroupSubmitFunctionAddress, 8) },
        { "billboard_producer_function_address",
          HexU32(state.BillboardProducerFunctionAddress, 8) },
        { "runtime_object_draw_function_address",
          HexU32(state.RuntimeObjectDrawFunctionAddress, 8) },
        { "celestial_scale_x_address", HexU32(state.CelestialScaleXAddress, 8) },
        { "celestial_scale_y_address", HexU32(state.CelestialScaleYAddress, 8) },
        { "celestial_scale_z_address", HexU32(state.CelestialScaleZAddress, 8) },
        { "celestial_radius_pointer_address",
          HexU32(state.CelestialRadiusPointerAddress, 8) },
        { "celestial_radius_value_address",
          HexU32(state.CelestialRadiusValueAddress, 8) },
        { "position_scale_address", HexU32(state.PositionScaleAddress, 8) },
        { "billboard_scale_address", HexU32(state.BillboardScaleAddress, 8) },
        { "sampler_wrap_address", HexU32(state.SamplerWrapAddress, 8) },
        { "sampler_filter_address", HexU32(state.SamplerFilterAddress, 8) },
        { "celestial_scale_x", state.CelestialScaleX },
        { "celestial_scale_y", state.CelestialScaleY },
        { "celestial_scale_z", state.CelestialScaleZ },
        { "celestial_radius", state.CelestialRadius },
        { "position_scale", state.PositionScale },
        { "billboard_scale", state.BillboardScale },
        { "min_mag_filter", HexU32(state.MinMagFilter, 4) },
        { "wrap_s", HexU32(state.WrapS, 4) },
        { "wrap_t", HexU32(state.WrapT, 4) },
        { "runtime_active_angle", state.RuntimeActiveAngle },
        { "runtime_celestial_vector", DemoVec3Json(state.RuntimeCelestialVector) },
        { "runtime_world_base_position",
          DemoVec3Json(state.RuntimeWorldBasePosition) },
        { "runtime_layer_alphas", state.RuntimeLayerAlphas },
        { "runtime_layer_texture_names", state.RuntimeLayerTextureNames },
        { "profile_textures", profileTextures },
        { "runtime_transform_source", state.RuntimeTransformSource },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeKankyoLensEffectStateToJson(
    const Oot3dNativeKankyoLensEffectState& state) {
    nlohmann::json elements = nlohmann::json::array();
    for (const auto& element : state.Elements) {
        elements.push_back(Oot3dNativeKankyoLensEffectElementToJson(element));
    }
    nlohmann::json halationColors = nlohmann::json::array();
    for (const auto& color : state.HalationColors) {
        halationColors.push_back(ColorJson(color));
    }
    nlohmann::json geometryCoordinatePairs = nlohmann::json::array();
    for (const auto& coord : state.OverlayGeometryCoordinatePairs) {
        geometryCoordinatePairs.push_back(Oot3dNativeKankyoOverlayGeometryCoordToJson(coord));
    }
    nlohmann::json projectionSamples = nlohmann::json::array();
    for (const auto& sample : state.NativeLensPositionProjectionSamples) {
        projectionSamples.push_back(
            Oot3dNativeKankyoRuntime28CProjectionSampleToJson(sample));
    }
    nlohmann::json positionRecords = nlohmann::json::array();
    for (const auto& record : state.NativeLensPositionRuntimePositionRecords) {
        positionRecords.push_back(
            Oot3dNativeKankyoRuntime28CPositionRecordToJson(record));
    }

    return {
        { "available", state.Available },
        { "tbd_records_resolved", state.TbdRecordsResolved },
        { "texture_inputs_resolved", state.TextureInputsResolved },
        { "ready_for_backend_input", state.ReadyForBackendInput },
        { "native_submit_mapping_resolved", state.NativeSubmitMappingResolved },
        { "overlay_helper_callback_mapping_resolved", state.OverlayHelperCallbackMappingResolved },
        { "native_runtime_list_resolved", state.NativeRuntimeListResolved },
        { "native_backend_buffer_helpers_resolved", state.NativeBackendBufferHelpersResolved },
        { "native_backend_input_plan_resolved", state.NativeBackendInputPlanResolved },
        { "native_visible_backend_submit_resolved", state.NativeVisibleBackendSubmitResolved },
        { "native_lens_position_producer_resolved", state.NativeLensPositionProducerResolved },
        { "lens_position_view_projection_source_resolved",
          state.LensPositionViewProjectionSourceResolved },
        { "native_lens_position_runtime_input_resolved",
          state.NativeLensPositionRuntimeInputResolved },
        { "native_lens_position_runtime_scale_resolved",
          state.NativeLensPositionRuntimeScaleResolved },
        { "native_lens_visibility_screen_gate_resolved",
          state.NativeLensVisibilityScreenGateResolved },
        { "native_lens_visibility_scene_occlusion_resolved",
          state.NativeLensVisibilitySceneOcclusionResolved },
        { "native_lens_visibility_source_in_viewport",
          state.NativeLensVisibilitySourceInViewport },
        { "native_lens_visibility_source_occluded",
          state.NativeLensVisibilitySourceOccluded },
        { "native_lens_position_primary_source_vector_resolved",
          state.NativeLensPositionPrimarySourceVectorResolved },
        { "source_kind", state.SourceKind },
        { "source_status", state.SourceStatus },
        { "native_backend_plan_source_status", state.NativeBackendPlanSourceStatus },
        { "native_lens_position_runtime_input_status",
          state.NativeLensPositionRuntimeInputStatus },
        { "native_lens_visibility_source_status",
          state.NativeLensVisibilitySourceStatus },
        { "native_lens_position_primary_source_vector_status",
          state.NativeLensPositionPrimarySourceVectorStatus },
        { "lens_tbd_entry_name", state.LensTbdEntryName },
        { "sun_texture_name", state.SunTextureName },
        { "lensflare_texture_name", state.LensflareTextureName },
        { "element_count", state.ElementCount },
        { "elements", elements },
        { "halation_colors", halationColors },
        { "effect_state_builder_function_address", HexU32(state.EffectStateBuilderFunctionAddress, 8) },
        { "effect_type_byte_play_offset", HexU32(state.EffectTypeBytePlayOffset, 4) },
        { "effect_mode_byte_play_offset", HexU32(state.EffectModeBytePlayOffset, 4) },
        { "effect_descriptor_base_play_offset", HexU32(state.EffectDescriptorBasePlayOffset, 4) },
        { "effect_descriptor_type_slot_offset", HexU32(state.EffectDescriptorTypeSlotOffset, 4) },
        { "effect_descriptor_submit_callback_slot_offset",
          HexU32(state.EffectDescriptorSubmitCallbackSlotOffset, 4) },
        { "flash_submit_function_address", HexU32(state.FlashSubmitFunctionAddress, 8) },
        { "color_bytes_overlay_helper_address", HexU32(state.ColorBytesOverlayHelperAddress, 8) },
        { "packed_rgba_overlay_helper_address", HexU32(state.PackedRgbaOverlayHelperAddress, 8) },
        { "color_bytes_overlay_helper_assignment_literal_address",
          HexU32(state.ColorBytesOverlayHelperAssignmentLiteralAddress, 8) },
        { "packed_rgba_overlay_helper_assignment_literal_address",
          HexU32(state.PackedRgbaOverlayHelperAssignmentLiteralAddress, 8) },
        { "color_bytes_overlay_effect_type_primary",
          HexU32(state.ColorBytesOverlayEffectTypePrimary, 2) },
        { "color_bytes_overlay_effect_type_alternate",
          HexU32(state.ColorBytesOverlayEffectTypeAlternate, 2) },
        { "packed_rgba_overlay_effect_type_range_start",
          HexU32(state.PackedRgbaOverlayEffectTypeRangeStart, 2) },
        { "packed_rgba_overlay_effect_type_range_end",
          HexU32(state.PackedRgbaOverlayEffectTypeRangeEnd, 2) },
        { "immediate_submit_helper_address", HexU32(state.ImmediateSubmitHelperAddress, 8) },
        { "scheduler_submit_helper_address", HexU32(state.SchedulerSubmitHelperAddress, 8) },
        { "lens_runtime_list_kankyo_offset", HexU32(state.LensRuntimeListKankyoOffset, 4) },
        { "lens_runtime_draw_address", HexU32(state.LensRuntimeDrawAddress, 8) },
        { "lens_runtime_draw_helper_address", HexU32(state.LensRuntimeDrawHelperAddress, 8) },
        { "lens_runtime_primary_builder_address", HexU32(state.LensRuntimePrimaryBuilderAddress, 8) },
        { "lens_runtime_submit_queue_address", HexU32(state.LensRuntimeSubmitQueueAddress, 8) },
        { "lens_runtime_submit_record_writer_address",
          HexU32(state.LensRuntimeSubmitRecordWriterAddress, 8) },
        { "lens_runtime_quad_batch_draw_method_address",
          HexU32(state.LensRuntimeQuadBatchDrawMethodAddress, 8) },
        { "lens_runtime_quad_batch_native_vertices_per_quad",
          state.LensRuntimeQuadBatchNativeVerticesPerQuad },
        { "lens_runtime_primary_element_submit_index",
          HexU32(state.LensRuntimePrimaryElementSubmitIndex, 2) },
        { "lens_runtime_terminal_element_submit_index",
          HexU32(state.LensRuntimeTerminalElementSubmitIndex, 2) },
        { "lens_runtime_visible_flag_mask", HexU32(state.LensRuntimeVisibleFlagMask, 8) },
        { "lens_position_producer_function_address",
          HexU32(state.LensPositionProducerFunctionAddress, 8) },
        { "lens_position_projection_helper_address",
          HexU32(state.LensPositionProjectionHelperAddress, 8) },
        { "lens_position_view_projection_helper_address",
          HexU32(state.LensPositionViewProjectionHelperAddress, 8) },
        { "lens_position_view_projection_matrix_play_offset",
          HexU32(state.LensPositionViewProjectionMatrixPlayOffset, 4) },
        { "lens_position_gameplay_draw_function_address",
          HexU32(state.LensPositionGameplayDrawFunctionAddress, 8) },
        { "lens_position_view_source_producer_function_address",
          HexU32(state.LensPositionViewSourceProducerFunctionAddress, 8) },
        { "lens_position_view_source_producer_callsite_address",
          HexU32(state.LensPositionViewSourceProducerCallsiteAddress, 8) },
        { "lens_position_view_prep_function_address",
          HexU32(state.LensPositionViewPrepFunctionAddress, 8) },
        { "lens_position_view_prep_callsite_address",
          HexU32(state.LensPositionViewPrepCallsiteAddress, 8) },
        { "lens_position_view_update_function_address",
          HexU32(state.LensPositionViewUpdateFunctionAddress, 8) },
        { "lens_position_view_update_callsite_address",
          HexU32(state.LensPositionViewUpdateCallsiteAddress, 8) },
        { "lens_position_view_projection_source_copy_helper_address",
          HexU32(state.LensPositionViewProjectionSourceCopyHelperAddress, 8) },
        { "lens_position_view_projection_source_copy_callsite_address",
          HexU32(state.LensPositionViewProjectionSourceCopyCallsiteAddress, 8) },
        { "lens_position_view_projection_compose_copy_helper_address",
          HexU32(state.LensPositionViewProjectionComposeCopyHelperAddress, 8) },
        { "lens_position_view_projection_compose_copy_callsite_address",
          HexU32(state.LensPositionViewProjectionComposeCopyCallsiteAddress, 8) },
        { "lens_position_view_projection_compose_helper_address",
          HexU32(state.LensPositionViewProjectionComposeHelperAddress, 8) },
        { "lens_position_view_projection_compose_callsite_address",
          HexU32(state.LensPositionViewProjectionComposeCallsiteAddress, 8) },
        { "lens_position_view_struct_play_offset",
          HexU32(state.LensPositionViewStructPlayOffset, 4) },
        { "lens_position_view_projection_source_matrix_play_offset",
          HexU32(state.LensPositionViewProjectionSourceMatrixPlayOffset, 4) },
        { "lens_position_view_projection_compose_matrix_play_offset",
          HexU32(state.LensPositionViewProjectionComposeMatrixPlayOffset, 4) },
        { "lens_position_view_projection_matrix_word_count",
          state.LensPositionViewProjectionMatrixWordCount },
        { "lens_position_view_projection_compose_matrix_word_count",
          state.LensPositionViewProjectionComposeMatrixWordCount },
        { "lens_position_view_projection_identity_row_patched",
          state.LensPositionViewProjectionIdentityRowPatched },
        { "native_lens_position_runtime_view_projection_matrix_materialized",
          state.NativeLensPositionRuntimeViewProjectionMatrixMaterialized },
        { "native_lens_position_runtime_view_projection_matrix_source_kind",
          state.NativeLensPositionRuntimeViewProjectionMatrixSourceKind },
        { "native_lens_position_runtime_view_projection_matrix_status",
          state.NativeLensPositionRuntimeViewProjectionMatrixStatus },
        { "native_lens_position_runtime_view_projection_matrix",
          MatrixJson(state.NativeLensPositionRuntimeViewProjectionMatrix) },
        { "native_lens_position_projection_samples", projectionSamples },
        { "lens_position_primary_source_producer_function_address",
          HexU32(state.LensPositionPrimarySourceProducerFunctionAddress, 8) },
        { "lens_position_primary_source_producer_callsite_address",
          HexU32(state.LensPositionPrimarySourceProducerCallsiteAddress, 8) },
        { "lens_position_primary_source_offset_update_function_address",
          HexU32(state.LensPositionPrimarySourceOffsetUpdateFunctionAddress, 8) },
        { "lens_position_primary_source_base_camera_x_play_offset",
          HexU32(state.LensPositionPrimarySourceBaseCameraXPlayOffset, 4) },
        { "lens_position_primary_source_base_camera_y_play_offset",
          HexU32(state.LensPositionPrimarySourceBaseCameraYPlayOffset, 4) },
        { "lens_position_primary_source_base_camera_z_play_offset",
          HexU32(state.LensPositionPrimarySourceBaseCameraZPlayOffset, 4) },
        { "lens_position_primary_source_kankyo_offset_x_play_offset",
          HexU32(state.LensPositionPrimarySourceKankyoOffsetXPlayOffset, 4) },
        { "lens_position_primary_source_kankyo_offset_y_play_offset",
          HexU32(state.LensPositionPrimarySourceKankyoOffsetYPlayOffset, 4) },
        { "lens_position_primary_source_kankyo_offset_z_play_offset",
          HexU32(state.LensPositionPrimarySourceKankyoOffsetZPlayOffset, 4) },
        { "lens_position_primary_source_active_angle_state_address",
          HexU32(state.LensPositionPrimarySourceActiveAngleStateAddress, 8) },
        { "lens_position_primary_source_active_angle_halfword_offset",
          HexU32(state.LensPositionPrimarySourceActiveAngleHalfwordOffset, 2) },
        { "lens_position_primary_source_scale_x_address",
          HexU32(state.LensPositionPrimarySourceScaleXAddress, 8) },
        { "lens_position_primary_source_scale_y_address",
          HexU32(state.LensPositionPrimarySourceScaleYAddress, 8) },
        { "lens_position_primary_source_scale_z_address",
          HexU32(state.LensPositionPrimarySourceScaleZAddress, 8) },
        { "lens_position_primary_source_radius_pointer_address",
          HexU32(state.LensPositionPrimarySourceRadiusPointerAddress, 8) },
        { "lens_position_primary_source_radius_value_address",
          HexU32(state.LensPositionPrimarySourceRadiusValueAddress, 8) },
        { "lens_position_primary_source_scale_x",
          state.LensPositionPrimarySourceScaleX },
        { "lens_position_primary_source_scale_y",
          state.LensPositionPrimarySourceScaleY },
        { "lens_position_primary_source_scale_z",
          state.LensPositionPrimarySourceScaleZ },
        { "lens_position_primary_source_radius",
          state.LensPositionPrimarySourceRadius },
        { "lens_position_primary_source_active_angle",
          state.LensPositionPrimarySourceActiveAngle },
        { "lens_position_primary_source_active_angle_source",
          state.LensPositionPrimarySourceActiveAngleSource },
        { "native_lens_position_primary_source_base_camera",
          DemoVec3Json(state.NativeLensPositionPrimarySourceBaseCamera) },
        { "native_lens_position_primary_source_kankyo_offset",
          DemoVec3Json(state.NativeLensPositionPrimarySourceKankyoOffset) },
        { "native_lens_position_primary_source_world_position",
          DemoVec3Json(state.NativeLensPositionPrimarySourceWorldPosition) },
        { "native_lens_position_runtime_position_record_count",
          state.NativeLensPositionRuntimePositionRecords.size() },
        { "native_lens_position_runtime_position_records", positionRecords },
        { "lens_position_screen_center_x_address",
          HexU32(state.LensPositionScreenCenterXAddress, 8) },
        { "lens_position_screen_center_y_address",
          HexU32(state.LensPositionScreenCenterYAddress, 8) },
        { "lens_position_base_z_address",
          HexU32(state.LensPositionBaseZAddress, 8) },
        { "lens_position_direction_scale_address",
          HexU32(state.LensPositionDirectionScaleAddress, 8) },
        { "lens_position_distance_limit_scale_address",
          HexU32(state.LensPositionDistanceLimitScaleAddress, 8) },
        { "lens_position_runtime_scale_multiplier_address",
          HexU32(state.LensPositionRuntimeScaleMultiplierAddress, 8) },
        { "lens_position_runtime_scale_argument",
          state.LensPositionRuntimeScaleArgument },
        { "lens_position_projection_scale_x_address",
          HexU32(state.LensPositionProjectionScaleXAddress, 8) },
        { "lens_position_projection_scale_y_address",
          HexU32(state.LensPositionProjectionScaleYAddress, 8) },
        { "lens_position_projection_base_y_address",
          HexU32(state.LensPositionProjectionBaseYAddress, 8) },
        { "lens_position_screen_center_x", state.LensPositionScreenCenterX },
        { "lens_position_screen_center_y", state.LensPositionScreenCenterY },
        { "lens_position_base_z", state.LensPositionBaseZ },
        { "lens_position_direction_scale", state.LensPositionDirectionScale },
        { "lens_position_distance_limit_scale", state.LensPositionDistanceLimitScale },
        { "lens_position_runtime_scale_multiplier",
          state.LensPositionRuntimeScaleMultiplier },
        { "lens_position_projection_scale_x", state.LensPositionProjectionScaleX },
        { "lens_position_projection_scale_y", state.LensPositionProjectionScaleY },
        { "lens_position_projection_base_y", state.LensPositionProjectionBaseY },
        { "lens_visibility_min_step_address",
          HexU32(state.LensVisibilityMinStepAddress, 8) },
        { "lens_visibility_scale_address",
          HexU32(state.LensVisibilityScaleAddress, 8) },
        { "lens_visibility_visible_target_address",
          HexU32(state.LensVisibilityVisibleTargetAddress, 8) },
        { "lens_visibility_screen_max_x_address",
          HexU32(state.LensVisibilityScreenMaxXAddress, 8) },
        { "lens_visibility_min_step", state.LensVisibilityMinStep },
        { "lens_visibility_max_step", state.LensVisibilityMaxStep },
        { "lens_visibility_scale", state.LensVisibilityScale },
        { "lens_visibility_visible_target", state.LensVisibilityVisibleTarget },
        { "lens_visibility_target", state.LensVisibilityTarget },
        { "lens_visibility_screen_max_x", state.LensVisibilityScreenMaxX },
        { "lens_visibility_screen_max_y", state.LensVisibilityScreenMaxY },
        { "lens_visibility_occluder_distance",
          state.LensVisibilityOccluderDistance },
        { "lens_visibility_occluder_model", state.LensVisibilityOccluderModel },
        { "native_primitive_color_pass", state.NativePrimitiveColorPass },
        { "native_primitive_alpha_pass", state.NativePrimitiveAlphaPass },
        { "native_primitive_vertex_count", state.NativePrimitiveVertexCount },
        { "overlay_geometry_table_address", HexU32(state.OverlayGeometryTableAddress, 8) },
        { "overlay_geometry_table_header_word", HexU32(state.OverlayGeometryTableHeaderWord, 8) },
        { "overlay_color_byte_scale", state.OverlayColorByteScale },
        { "overlay_base_alpha", state.OverlayBaseAlpha },
        { "overlay_depth_threshold", state.OverlayDepthThreshold },
        { "overlay_depth_fade_scale", state.OverlayDepthFadeScale },
        { "overlay_geometry_table_values", state.OverlayGeometryTableValues },
        { "overlay_geometry_coordinate_pairs", geometryCoordinatePairs },
        { "primitive_backend_input",
          Oot3dNativeKankyoPrimitiveBackendInputStateToJson(state.PrimitiveBackendInput) },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json NativeKankyoCtxbDescriptorSlotsToJson(
    const std::vector<NativeCtxbDescriptorSlot>& slots) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& slot : slots) {
        out.push_back(Oot3dNativeRenderCtxbDescriptorSlotToJson(slot));
    }
    return out;
}

nlohmann::json NativeKankyoTypeLocalIndicesToJson(const std::vector<uint32_t>& indices) {
    nlohmann::json out = nlohmann::json::array();
    for (const uint32_t index : indices) {
        out.push_back(HexU32(index, 2));
    }
    return out;
}

nlohmann::json Oot3dNativeEnvironmentBackgroundStateToJson(
    const Oot3dNativeEnvironmentBackgroundState& background) {
    nlohmann::json runtimeCtxbs = nlohmann::json::array();
    for (const auto& ctxb : background.NativeKankyoRuntimeCtxbStates) {
        runtimeCtxbs.push_back(Oot3dNativeKankyoRuntimeCtxbStateToJson(ctxb));
    }
    nlohmann::json commonTbds = nlohmann::json::array();
    for (const auto& tbd : background.NativeKankyoCommonTbdStates) {
        commonTbds.push_back(Oot3dNativeKankyoCommonTbdStateToJson(tbd));
    }

    return {
        { "available", background.Available },
        { "used_for_render", background.UsedForRender },
        { "runtime_environment_time_input_available",
          background.RuntimeEnvironmentTimeInputAvailable },
        { "runtime_environment_day_time", background.RuntimeEnvironmentDayTime },
        { "runtime_environment_skybox_time", background.RuntimeEnvironmentSkyboxTime },
        { "runtime_environment_time_start_frame",
          background.RuntimeEnvironmentTimeStartFrame },
        { "runtime_environment_time_source_kind",
          background.RuntimeEnvironmentTimeSourceKind },
        { "runtime_environment_time_source_status",
          background.RuntimeEnvironmentTimeSourceStatus },
        { "native_kankyo_draw_route_decoded", background.NativeKankyoDrawRouteDecoded },
        { "native_kankyo_archive_available", background.NativeKankyoArchiveAvailable },
        { "source_kind", background.SourceKind },
        { "source_status", background.SourceStatus },
        { "native_kankyo_rom_path", background.NativeKankyoRomPath },
        { "native_kankyo_archive_path", background.NativeKankyoArchivePath.string() },
        { "native_kankyo_skybox_record_table_address", background.NativeKankyoSkyboxRecordTableAddress },
        { "native_kankyo_skybox_record_address", background.NativeKankyoSkyboxRecordAddress },
        { "native_kankyo_schedule_table_address", background.NativeKankyoScheduleTableAddress },
        { "native_kankyo_clock_state_address", background.NativeKankyoClockStateAddress },
        { "native_kankyo_clock_halfword_offset", background.NativeKankyoClockHalfwordOffset },
        { "native_kankyo_draw_scale_address", background.NativeKankyoDrawScaleAddress },
        { "native_kankyo_draw_scale", background.NativeKankyoDrawScale },
        { "native_kankyo_schedule_mode", background.NativeKankyoScheduleMode },
        { "native_kankyo_schedule_active_angle", background.NativeKankyoScheduleActiveAngle },
        { "native_kankyo_schedule_active_angle_source",
          background.NativeKankyoScheduleActiveAngleSource },
        { "native_kankyo_schedule_entry_index", background.NativeKankyoScheduleEntryIndex },
        { "native_kankyo_current_profile_index", background.NativeKankyoCurrentProfileIndex },
        { "native_kankyo_next_profile_index", background.NativeKankyoNextProfileIndex },
        { "native_kankyo_blend_alpha", background.NativeKankyoBlendAlpha },
        { "native_kankyo_record_param44", background.NativeKankyoRecordParam44 },
        { "native_kankyo_record_param48", background.NativeKankyoRecordParam48 },
        { "native_kankyo_record_param4c", background.NativeKankyoRecordParam4C },
        { "native_kankyo_selected_cmb_names", background.NativeKankyoSelectedCmbNames },
        { "native_kankyo_selected_cmb_profile_indices",
          background.NativeKankyoSelectedCmbProfileIndices },
        { "native_kankyo_selected_cmb_type_local_indices",
          NativeKankyoTypeLocalIndicesToJson(
              background.NativeKankyoSelectedCmbTypeLocalIndices) },
        { "native_kankyo_selected_cmb_layer_indices",
          background.NativeKankyoSelectedCmbLayerIndices },
        { "native_kankyo_selected_cmab_names", background.NativeKankyoSelectedCmabNames },
        { "native_kankyo_extra_draw_route_decoded",
          background.NativeKankyoExtraDrawRouteDecoded },
        { "native_kankyo_extra_draw_callsite_address",
          HexU32(background.NativeKankyoExtraDrawCallsiteAddress, 8) },
        { "native_kankyo_extra_draw_function_address",
          HexU32(background.NativeKankyoExtraDrawFunctionAddress, 8) },
        { "native_kankyo_extra_draw_count",
          background.NativeKankyoExtraDrawCount },
        { "native_kankyo_extra_cmb_names",
          background.NativeKankyoExtraCmbNames },
        { "native_kankyo_extra_cmb_type_local_indices",
          NativeKankyoTypeLocalIndicesToJson(
              background.NativeKankyoExtraDrawCmbTypeLocalIndices) },
        { "native_kankyo_model_load_errors",
          background.NativeKankyoModelLoadErrors },
        { "native_kankyo_extra_ctxb_names",
          background.NativeKankyoExtraCtxbNames },
        { "native_kankyo_extra_ctxb_type_local_indices",
          NativeKankyoTypeLocalIndicesToJson(
              background.NativeKankyoExtraDrawCtxbTypeLocalIndices) },
        { "native_kankyo_extra_ctxb_descriptor_slots",
          NativeKankyoCtxbDescriptorSlotsToJson(
              background.NativeKankyoExtraCtxbDescriptorSlots) },
        { "native_kankyo_extra_ctxb_textures",
          NativeTexturesJson(background.NativeKankyoExtraCtxbTextures) },
        { "native_kankyo_skybox_1d_extra_draw_route_decoded",
          background.NativeKankyoExtraDrawRouteDecoded },
        { "native_kankyo_skybox_1d_extra_draw_callsite_address",
          HexU32(background.NativeKankyoExtraDrawCallsiteAddress, 8) },
        { "native_kankyo_skybox_1d_extra_draw_function_address",
          HexU32(background.NativeKankyoExtraDrawFunctionAddress, 8) },
        { "native_kankyo_skybox_1d_extra_draw_count",
          background.NativeKankyoExtraDrawCount },
        { "native_kankyo_skybox_1d_extra_cmb_names",
          background.NativeKankyoExtraCmbNames },
        { "native_kankyo_skybox_1d_extra_ctxb_names",
          background.NativeKankyoExtraCtxbNames },
        { "native_kankyo_skybox_1d_extra_ctxb_type_local_indices",
          NativeKankyoTypeLocalIndicesToJson(
              background.NativeKankyoExtraDrawCtxbTypeLocalIndices) },
        { "native_kankyo_skybox_1d_extra_ctxb_descriptor_slots",
          NativeKankyoCtxbDescriptorSlotsToJson(
              background.NativeKankyoExtraCtxbDescriptorSlots) },
        { "native_kankyo_skybox_1d_extra_ctxb_textures",
          NativeTexturesJson(background.NativeKankyoExtraCtxbTextures) },
        { "native_kankyo_runtime_ctxb_set_resolved",
          background.NativeKankyoRuntimeCtxbSetResolved },
        { "native_kankyo_gameplay_keep_archive_path",
          background.NativeKankyoGameplayKeepArchivePath.string() },
        { "native_kankyo_runtime_ctxb_states", runtimeCtxbs },
        { "native_kankyo_common_archive_available",
          background.NativeKankyoCommonArchiveAvailable },
        { "native_kankyo_common_archive_path",
          background.NativeKankyoCommonArchivePath.string() },
        { "native_kankyo_common_tbd_support_resolved",
          background.NativeKankyoCommonTbdSupportResolved },
        { "native_kankyo_common_tbd_states", commonTbds },
        { "native_kankyo_moon",
          Oot3dNativeKankyoMoonStateToJson(background.NativeKankyoMoon) },
        { "native_kankyo_sun_halo",
          Oot3dNativeKankyoSunHaloStateToJson(background.NativeKankyoSunHalo) },
        { "native_kankyo_lens_effect",
          Oot3dNativeKankyoLensEffectStateToJson(background.NativeKankyoLensEffect) },
        { "native_kankyo_material_animation_count", background.NativeKankyoMaterialAnimationCount },
        { "native_kankyo_material_animation_applied_batch_count",
          background.NativeKankyoMaterialAnimationAppliedBatchCount },
        { "native_kankyo_material_animation_frame", background.NativeKankyoMaterialAnimationFrame },
        { "clear_color", ColorJson(background.ClearColor) },
        { "active_setup_index", background.ActiveSetupIndex },
        { "skybox_command_offset", background.SkyboxCommandOffset },
        { "skybox_command_argument", background.SkyboxCommandArgument },
        { "skybox_command_parameter", background.SkyboxCommandParameter },
        { "skybox_id", background.SkyboxCommandArgument >= 0 ? (background.SkyboxCommandArgument & 0xFF) : -1 },
        { "special_files_command_offset", background.SpecialFilesCommandOffset },
        { "special_files_command_argument", background.SpecialFilesCommandArgument },
        { "special_files_command_parameter", background.SpecialFilesCommandParameter },
        { "uses_runtime_n64_asset_substitution", false },
    };
}

nlohmann::json Oot3dNativeDemoRenderSceneSummaryToJson(const Oot3dNativeDemoRenderScene& scene) {
    Oot3dDemoBounds bounds;
    NativeDemoExpandBoundsByBounds(bounds, Oot3dNativeRenderModelWorldBounds(scene.Room));
    nlohmann::json additionalRoomModels = nlohmann::json::array();
    for (const auto& roomModel : scene.AdditionalRoomModels) {
        NativeDemoExpandBoundsByBounds(bounds, Oot3dNativeRenderModelWorldBounds(roomModel));
        additionalRoomModels.push_back(Oot3dNativeRenderModelSummaryToJson(roomModel));
    }
    NativeDemoExpandBoundsByBounds(bounds, Oot3dNativeRenderModelWorldBounds(scene.Link));
    nlohmann::json environmentModels = nlohmann::json::array();
    for (const auto& environmentModel : scene.EnvironmentModels) {
        environmentModels.push_back(Oot3dNativeRenderModelSummaryToJson(environmentModel));
    }
    nlohmann::json moonModels = nlohmann::json::array();
    for (const auto& moonModel : scene.MoonModels) {
        moonModels.push_back(Oot3dNativeRenderModelSummaryToJson(moonModel));
    }
    nlohmann::json actorVisuals = nlohmann::json::array();
    for (const auto& actorVisual : scene.ActorVisuals) {
        NativeDemoExpandBoundsByBounds(bounds, Oot3dNativeRenderModelWorldBounds(actorVisual));
        actorVisuals.push_back(Oot3dNativeRenderModelSummaryToJson(actorVisual));
    }
    return {
        { "format", "oot3d_native_render_scene_v1" },
        { "room", Oot3dNativeRenderModelSummaryToJson(scene.Room) },
        { "additional_room_model_count", scene.AdditionalRoomModels.size() },
        { "additional_room_models", additionalRoomModels },
        { "native_room_material_animation_count", scene.NativeRoomMaterialAnimationCount },
        { "native_room_material_animation_applied_batch_count",
          scene.NativeRoomMaterialAnimationAppliedBatchCount },
        { "native_room_material_animation_frame", scene.NativeRoomMaterialAnimationFrame },
        { "link_child", Oot3dNativeRenderModelSummaryToJson(scene.Link) },
        { "environment_model_count", scene.EnvironmentModels.size() },
        { "environment_models", environmentModels },
        { "moon_model_count", scene.MoonModels.size() },
        { "moon_models", moonModels },
        { "native_actor_visual_count", scene.ActorVisuals.size() },
        { "native_actor_visuals", actorVisuals },
        { "pica_lighting", Oot3dNativePicaLightingRenderStateToJson(scene.PicaLighting) },
        { "pica_fog", Oot3dNativePicaFogStateToJson(scene.PicaFog) },
        { "pica_viewport",
          {
              { "available", scene.PicaViewport.Available },
              { "code_bin_source_decoded", scene.PicaViewport.CodeBinSourceDecoded },
              { "source_kind", scene.PicaViewport.SourceKind },
              { "source_status", scene.PicaViewport.SourceStatus },
              { "half_width_address", HexU32(scene.PicaViewport.HalfWidthAddress, 8) },
              { "half_height_address", HexU32(scene.PicaViewport.HalfHeightAddress, 8) },
              { "half_width", scene.PicaViewport.HalfWidth },
              { "half_height", scene.PicaViewport.HalfHeight },
              { "aspect", scene.PicaViewport.Aspect },
          } },
        { "pica_lighting_debug", Oot3dNativePicaLightingDebugStateToJson(scene.PicaLightingDebug) },
        { "pica_shadow", Oot3dNativePicaShadowStateToJson(scene.PicaShadow) },
        { "link_actor_shadow", Oot3dNativeActorShadowStateToJson(scene.LinkActorShadow) },
        { "environment_background", Oot3dNativeEnvironmentBackgroundStateToJson(scene.EnvironmentBackground) },
        { "bounds", BoundsJson(bounds.Valid ? bounds : scene.Bounds) },
    };
}

} // namespace ThreeDsRecomp::Oot3d
