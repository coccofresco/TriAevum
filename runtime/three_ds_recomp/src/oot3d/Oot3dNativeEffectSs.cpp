#include "three_ds_recomp/oot3d/Oot3dNativeEffectSs.h"

#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr uint16_t kPicaTextureRgba = 0x6752;
constexpr uint16_t kPicaTextureAlpha = 0x6756;
constexpr uint16_t kPicaTextureLuminanceAlpha = 0x6758;
constexpr uint16_t kPicaTextureEtc1A4 = 0x675B;

class CodeView {
  public:
    CodeView(std::vector<uint8_t> bytes, uint32_t base) : mBytes(std::move(bytes)), mBase(base) {
    }

    uint8_t U8(uint32_t address) const {
        return mBytes.at(Offset(address, 1));
    }

    uint16_t U16(uint32_t address) const {
        const size_t offset = Offset(address, 2);
        return static_cast<uint16_t>(mBytes[offset]) |
               static_cast<uint16_t>(mBytes[offset + 1] << 8);
    }

    uint32_t U32(uint32_t address) const {
        const size_t offset = Offset(address, 4);
        return static_cast<uint32_t>(mBytes[offset]) |
               (static_cast<uint32_t>(mBytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(mBytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(mBytes[offset + 3]) << 24);
    }

    float F32(uint32_t address) const {
        return std::bit_cast<float>(U32(address));
    }

    ColorRgba8 Color(uint32_t address) const {
        return { U8(address), U8(address + 1), U8(address + 2), U8(address + 3) };
    }

    std::string AsciiZ(uint32_t address, size_t maxLength) const {
        const size_t offset = Offset(address, maxLength);
        size_t length = 0;
        while (length < maxLength && mBytes[offset + length] != 0) {
            ++length;
        }
        if (length == maxLength) {
            throw std::runtime_error("OOT3D code string is not terminated inside its table entry");
        }
        return std::string(reinterpret_cast<const char*>(mBytes.data() + offset), length);
    }

  private:
    size_t Offset(uint32_t address, size_t size) const {
        if (address < mBase) {
            throw std::runtime_error("OOT3D code address precedes code.bin base");
        }
        const size_t offset = static_cast<size_t>(address - mBase);
        if (offset > mBytes.size() || size > mBytes.size() - offset) {
            throw std::runtime_error("OOT3D code address is outside code.bin");
        }
        return offset;
    }

    std::vector<uint8_t> mBytes;
    uint32_t mBase = 0;
};

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open OOT3D code.bin: " + path.string());
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("OOT3D code.bin is empty: " + path.string());
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("could not read OOT3D code.bin: " + path.string());
    }
    return bytes;
}

uint32_t RotateRight(uint32_t value, uint32_t shift) {
    shift &= 31u;
    return shift == 0 ? value : (value >> shift) | (value << (32u - shift));
}

uint32_t DecodeArmImmediate(uint32_t instruction) {
    if ((instruction & (1u << 25)) == 0) {
        throw std::runtime_error("expected ARM data-processing immediate instruction");
    }
    return RotateRight(instruction & 0xFFu, ((instruction >> 8) & 0xFu) * 2u);
}

uint32_t DecodeArmRegisterShiftAmount(uint32_t instruction) {
    if ((instruction & (1u << 25)) != 0 || (instruction & (1u << 4)) != 0) {
        throw std::runtime_error("expected ARM data-processing immediate-shift register instruction");
    }
    return (instruction >> 7) & 0x1Fu;
}

uint64_t Fnv1a64(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool TextureFormatHasAlpha(uint16_t format) {
    return format == kPicaTextureRgba || format == kPicaTextureAlpha ||
           format == kPicaTextureLuminanceAlpha || format == kPicaTextureEtc1A4;
}

Oot3dNativeRenderTexture BuildRenderTexture(const CtxbTexture& texture, uint32_t sourceIndex) {
    Oot3dNativeRenderTexture renderTexture;
    renderTexture.SourceIndex = sourceIndex;
    renderTexture.Name = texture.Name;
    renderTexture.Width = texture.Width;
    renderTexture.Height = texture.Height;
    renderTexture.TextureFormat = texture.TextureFormat;
    renderTexture.DataType = texture.DataType;
    renderTexture.Rgba8Decoded = texture.Rgba8Decoded;
    renderTexture.HasNativeAlpha = TextureFormatHasAlpha(texture.TextureFormat);
    renderTexture.Rgba8 = texture.Rgba8;
    renderTexture.Rgba8ByteCount = renderTexture.Rgba8.size();
    renderTexture.Rgba8HashAvailable = renderTexture.Rgba8Decoded;
    renderTexture.Rgba8Hash = renderTexture.Rgba8HashAvailable ? Fnv1a64(renderTexture.Rgba8) : 0;
    return renderTexture;
}

std::filesystem::path ResolveNativeRomPath(const std::filesystem::path& romfsRoot,
                                           std::string_view nativePath) {
    constexpr std::string_view prefix = "rom:/";
    if (!nativePath.starts_with(prefix)) {
        throw std::runtime_error("OOT3D EffectSs object archive path is not a native rom:/ path");
    }
    const std::filesystem::path relativePath(std::string(nativePath.substr(prefix.size())));
    if (relativePath.empty() || relativePath.is_absolute()) {
        throw std::runtime_error("OOT3D EffectSs object archive path is invalid");
    }
    for (const auto& component : relativePath) {
        if (component == "..") {
            throw std::runtime_error("OOT3D EffectSs object archive path escapes ROMFS");
        }
    }
    return romfsRoot / relativePath;
}

float FinitePositive(float value, const char* role) {
    if (!std::isfinite(value) || value <= 0.0f) {
        throw std::runtime_error(std::string("invalid native EffectSs ") + role);
    }
    return value;
}

void ExpandBounds(Oot3dDemoBounds& bounds, Vec3f point) {
    if (!bounds.Valid) {
        bounds.Min = { point.X, point.Y, point.Z };
        bounds.Max = bounds.Min;
        bounds.Valid = true;
        return;
    }
    bounds.Min.X = std::min(bounds.Min.X, static_cast<double>(point.X));
    bounds.Min.Y = std::min(bounds.Min.Y, static_cast<double>(point.Y));
    bounds.Min.Z = std::min(bounds.Min.Z, static_cast<double>(point.Z));
    bounds.Max.X = std::max(bounds.Max.X, static_cast<double>(point.X));
    bounds.Max.Y = std::max(bounds.Max.Y, static_cast<double>(point.Y));
    bounds.Max.Z = std::max(bounds.Max.Z, static_cast<double>(point.Z));
}

Vec3f Add(Vec3f left, Vec3f right) {
    return { left.X + right.X, left.Y + right.Y, left.Z + right.Z };
}

Vec3f Subtract(Vec3f left, Vec3f right) {
    return { left.X - right.X, left.Y - right.Y, left.Z - right.Z };
}

Vec3f Scale(Vec3f value, float scale) {
    return { value.X * scale, value.Y * scale, value.Z * scale };
}

Vec3f Normalize(Vec3f value) {
    const float length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    if (!std::isfinite(length) || length <= 0.000001f) {
        return {};
    }
    return Scale(value, 1.0f / length);
}

uint8_t ClampColorDifference(uint8_t primary, uint8_t environment) {
    return primary > environment ? static_cast<uint8_t>(primary - environment) : 0;
}

} // namespace

Oot3dNativeEffectSsCodeLayout Oot3dNativeEffectSsEuCodeLayout() {
    Oot3dNativeEffectSsCodeLayout layout;
    layout.CodeBase = 0x00100000;
    layout.EffectTypeTablePointerLiteralAddress = 0x00342E70;
    layout.EffectInitUpdateTablePointerLiteralAddress = 0x001F25F8;
    layout.EffectInitDrawFunctionPointerLiteralAddress = 0x001F25FC;
    layout.EffectInitAlternateDrawFunctionPointerLiteralAddress = 0x001F2608;
    layout.EffectInitColorRandomRangeAddress = 0x001F2600;
    layout.EffectInitColorRandomOffsetAddress = 0x001F2604;
    layout.ResourceArchiveTablePointerLiteralAddress = 0x004A378C;
    layout.ResourceBindingTablePointerLiteralAddress = 0x004A3798;
    layout.ParentResourceTablePointerLiteralAddress = 0x00449254;
    layout.ObjectArchivePathTableAddress = 0x0053CD38;
    layout.ObjectArchivePathStride = 0x44;
    layout.ObjectArchivePathFirstId = 1;
    layout.DustColorsPointerLiteralAddress = 0x00357B2C;
    layout.DrawGlobalColorNormalizeScaleAddress = 0x003562B4;
    layout.DrawGlobalColorStatePointerLiteralAddress = 0x003562BC;
    layout.DrawGlobalColorWriterBiasInstructionAddress = 0x00479F30;
    layout.DrawGlobalColorWriterClampAddress = 0x0047A2E4;
    layout.SceneSkyboxDefaultColorScaleAddress = 0x0038A1B8;
    layout.SceneSkyboxByteColorScaleAddress = 0x0038A1BC;
    layout.DrawScaleFactorAddress = 0x00290750;
    layout.DrawScaleMultiplierAddress = 0x00290754;
    layout.SpriteTemplatePositionPointerLiteralAddress = 0x00348F18;
    layout.AtlasCellUAddress = 0x00290760;
    layout.AtlasCellVAddress = 0x0029076C;
    layout.UpdateAccelerationSpanAddress = 0x0029090C;
    layout.UpdateRateScaleAddress = 0x00290910;
    layout.GlobalUpdateRateInstructionAddress = 0x00416FF0;
    layout.UpdateRandomCenterAddress = 0x00290914;
    layout.TextureFrameCountAddress = 0x00290918;
    layout.TextureFrameMaxAddress = 0x00290920;
    layout.FadeThresholdInstructionAddress = 0x0029049C;
    layout.RandomStatePointerLiteralAddress = 0x00375A08;
    layout.RandomMultiplierAddress = 0x00375A0C;
    layout.RandomIncrementAddress = 0x00375A10;
    layout.HorseDustScaleRandomRangeAddress = 0x0014B304;
    layout.HorseDustScaleBaseInstructionAddress = 0x0014BC6C;
    layout.HorseDustScaleStepRandomRangeAddress = 0x0014BCCC;
    layout.HorseDustScaleStepBaseInstructionAddress = 0x0014BC4C;
    layout.HorseDustDurationRandomRangeAddress = 0x0014B788;
    layout.HorseDustDurationBaseInstructionAddress = 0x0014BC34;
    layout.DustDurationToLifeScaleAddress = 0x00357B20;
    layout.DustDurationToLifeBiasAddress = 0x00357B28;
    layout.HorseDustEffectFlagsInstructionAddress = 0x00357ACC;
    layout.HorseHoofOffsetXzAddress = 0x002329B0;
    layout.HorseHoofOffsetYAddress = 0x002329B4;
    layout.HorseGallopAnimationCompareInstructionAddresses = { 0x002329E0, 0x002329E4 };
    layout.HorseContactSpanAddresses = { 0x002329CC, 0x002329CC, 0x00232D90, 0x00232D90 };
    layout.HorseContactLowerBoundInstructionAddresses = {
        std::array<uint32_t, 2>{ 0x002329F0, 0x002329F4 },
        std::array<uint32_t, 2>{ 0x00232A40, 0x00232A44 },
        std::array<uint32_t, 2>{ 0x00232A94, 0x00232A98 },
        std::array<uint32_t, 2>{ 0x00232AAC, 0x00232AB0 },
    };
    layout.HorseContactFlagInstructionAddresses = { 0x00232A0C, 0x00232A5C, 0x00232C5C, 0x00232C20 };
    layout.HorseContactNodeInstructionAddresses = { 0x00232A18, 0x00232A68, 0x00232C68, 0x00232C2C };
    layout.HorseContactJitterFloatAddresses = { 0x002329B0, 0x002329D0, 0x002329D0, 0x002329D0 };
    layout.HorseLastContactSpanMvnInstructionAddress = 0x00232AA8;
    return layout;
}

Oot3dNativeEffectSsDustProfile DecodeOot3dNativeEffectSsDustProfile(
    const std::filesystem::path& codeBinPath, const std::filesystem::path& romfsRoot,
    const Oot3dNativeEffectSsCodeLayout& layout) {
    const CodeView code(ReadBinaryFile(codeBinPath), layout.CodeBase);
    Oot3dNativeEffectSsDustProfile profile;
    profile.EffectType = 0;
    profile.EffectTypeTableAddress = code.U32(layout.EffectTypeTablePointerLiteralAddress);
    profile.DescriptorAddress = code.U32(profile.EffectTypeTableAddress + profile.EffectType * 4u);
    if (code.U32(profile.DescriptorAddress) != profile.EffectType) {
        throw std::runtime_error("OOT3D EffectSs type-0 descriptor identity mismatch");
    }
    profile.InitFunctionAddress = code.U32(profile.DescriptorAddress + 4u);
    profile.DescriptorFlags = code.U32(profile.DescriptorAddress + 8u);
    profile.DependencyStartIndex = code.U32(profile.DescriptorAddress + 12u);
    profile.DependencyCount = code.U32(profile.DescriptorAddress + 16u);
    if (profile.InitFunctionAddress == 0 || profile.DependencyCount == 0) {
        throw std::runtime_error("OOT3D EffectSs type-0 descriptor is incomplete");
    }

    const uint32_t updateTable = code.U32(layout.EffectInitUpdateTablePointerLiteralAddress);
    profile.UpdateFunctionAddresses = { code.U32(updateTable), code.U32(updateTable + 4u) };
    profile.DrawFunctionAddress = code.U32(layout.EffectInitDrawFunctionPointerLiteralAddress);
    profile.AlternateDrawFunctionAddress = code.U32(layout.EffectInitAlternateDrawFunctionPointerLiteralAddress);
    profile.ColorRandomRange = code.F32(layout.EffectInitColorRandomRangeAddress);
    profile.ColorRandomOffset = code.F32(layout.EffectInitColorRandomOffsetAddress);

    const uint32_t archiveTable = code.U32(layout.ResourceArchiveTablePointerLiteralAddress);
    const uint32_t bindingTable = code.U32(layout.ResourceBindingTablePointerLiteralAddress);
    const uint32_t firstArchiveKey = code.U32(archiveTable + profile.DependencyStartIndex * 4u);
    const uint32_t bindingAddress = code.U32(bindingTable + profile.DependencyStartIndex * 4u);
    if (firstArchiveKey == 0 || bindingAddress == 0) {
        throw std::runtime_error("OOT3D EffectSs type-0 resource dependency is unresolved");
    }
    for (uint32_t index = 0; index < profile.DependencyCount; ++index) {
        if (code.U32(bindingTable + (profile.DependencyStartIndex + index) * 4u) != bindingAddress) {
            throw std::runtime_error("OOT3D EffectSs type-0 dependencies do not share the decoded binding");
        }
    }

    profile.ResourceBindingIndex = code.U8(bindingAddress);
    if (code.U8(bindingAddress + 1u) != 0xFFu) {
        throw std::runtime_error("OOT3D EffectSs type-0 secondary resource slot is not the native sentinel");
    }
    profile.SamplerMinFilter = code.U16(bindingAddress + 4u);
    profile.SamplerMagFilter = code.U16(bindingAddress + 8u);
    profile.SamplerWrapS = code.U16(bindingAddress + 12u);
    profile.SamplerWrapT = code.U16(bindingAddress + 16u);

    const uint32_t parentResourceTable = code.U32(layout.ParentResourceTablePointerLiteralAddress);
    const uint32_t parentEntryAddress = parentResourceTable + profile.ResourceBindingIndex * 8u;
    profile.ObjectId = static_cast<uint16_t>(code.U32(parentEntryAddress));
    profile.CtxbTypeLocalIndex = static_cast<uint16_t>(code.U32(parentEntryAddress + 4u));
    if (profile.ObjectId < layout.ObjectArchivePathFirstId || layout.ObjectArchivePathStride == 0) {
        throw std::runtime_error("OOT3D EffectSs object id is outside the native archive path table");
    }
    const uint32_t objectPathAddress =
        layout.ObjectArchivePathTableAddress +
        (profile.ObjectId - layout.ObjectArchivePathFirstId) * layout.ObjectArchivePathStride;
    profile.ObjectArchiveRomPath = code.AsciiZ(objectPathAddress, layout.ObjectArchivePathStride);
    if (profile.ObjectArchiveRomPath.empty()) {
        throw std::runtime_error("OOT3D EffectSs object id maps to an empty native archive path slot");
    }
    profile.ObjectArchivePath = ResolveNativeRomPath(romfsRoot, profile.ObjectArchiveRomPath);

    const auto archive = ParseZarArchiveFile(profile.ObjectArchivePath);
    const std::array<uint32_t, 1> requestedCtxb = { profile.CtxbTypeLocalIndex };
    const auto entries = ResolveZarTypeLocalEntries(archive, "ctxb", requestedCtxb);
    if (entries.size() != 1) {
        throw std::runtime_error("OOT3D EffectSs type-0 CTXB binding did not resolve exactly one native entry");
    }
    const auto textureBytes = ExtractZarFileBytes(profile.ObjectArchivePath, entries.front().Name);
    const auto texture = ParseCtxbTextureBytes(
        textureBytes, profile.ObjectArchivePath.string() + "!" + entries.front().Name);
    if (!texture.Rgba8Decoded || texture.Rgba8.empty()) {
        throw std::runtime_error("OOT3D EffectSs type-0 CTXB payload did not decode");
    }
    profile.TextureName = entries.front().Name;
    profile.Texture = BuildRenderTexture(texture, profile.CtxbTypeLocalIndex);
    profile.TextureLoaded = true;

    const uint32_t environmentColorAddress = code.U32(layout.DustColorsPointerLiteralAddress);
    profile.PrimaryColor = code.Color(environmentColorAddress - 4u);
    profile.EnvironmentColor = code.Color(environmentColorAddress);
    profile.GlobalEnvironmentColorStateAddress =
        code.U32(layout.DrawGlobalColorStatePointerLiteralAddress);
    profile.GlobalEnvironmentColorBias = static_cast<uint8_t>(DecodeArmImmediate(
        code.U32(layout.DrawGlobalColorWriterBiasInstructionAddress)));
    profile.GlobalEnvironmentColorNormalizeScale = FinitePositive(
        code.F32(layout.DrawGlobalColorNormalizeScaleAddress),
        "EffectSs global color normalize scale");
    profile.GlobalEnvironmentColorClamp = FinitePositive(
        code.F32(layout.DrawGlobalColorWriterClampAddress),
        "EffectSs global color clamp");
    profile.SceneSkyboxDefaultColorScale = FinitePositive(
        code.F32(layout.SceneSkyboxDefaultColorScaleAddress),
        "scene skybox default color scale");
    profile.SceneSkyboxByteColorScale = FinitePositive(
        code.F32(layout.SceneSkyboxByteColorScaleAddress),
        "scene skybox byte color scale");
    profile.RenderScale = FinitePositive(code.F32(layout.DrawScaleFactorAddress), "draw scale factor") *
                          FinitePositive(code.F32(layout.DrawScaleMultiplierAddress), "draw scale multiplier");
    profile.SpriteTemplatePositionAddress =
        code.U32(layout.SpriteTemplatePositionPointerLiteralAddress);
    float spriteMinX = std::numeric_limits<float>::max();
    float spriteMaxX = std::numeric_limits<float>::lowest();
    float spriteMinY = std::numeric_limits<float>::max();
    float spriteMaxY = std::numeric_limits<float>::lowest();
    for (uint32_t vertex = 0; vertex < 4; ++vertex) {
        const uint32_t positionAddress = profile.SpriteTemplatePositionAddress + vertex * 12u;
        const Vec3f position = {
            code.F32(positionAddress),
            code.F32(positionAddress + 4u),
            code.F32(positionAddress + 8u),
        };
        if (!std::isfinite(position.X) || !std::isfinite(position.Y) ||
            !std::isfinite(position.Z)) {
            throw std::runtime_error("OOT3D EffectSs sprite template contains a non-finite position");
        }
        spriteMinX = std::min(spriteMinX, position.X);
        spriteMaxX = std::max(spriteMaxX, position.X);
        spriteMinY = std::min(spriteMinY, position.Y);
        spriteMaxY = std::max(spriteMaxY, position.Y);
    }
    const float spriteWidth = spriteMaxX - spriteMinX;
    const float spriteHeight = spriteMaxY - spriteMinY;
    if (!std::isfinite(spriteWidth) || !std::isfinite(spriteHeight) || spriteWidth <= 0.0f ||
        spriteHeight <= 0.0f || std::abs(spriteWidth - spriteHeight) > 0.000001f ||
        std::abs(spriteMinX + spriteMaxX) > 0.000001f ||
        std::abs(spriteMinY + spriteMaxY) > 0.000001f) {
        throw std::runtime_error("OOT3D EffectSs sprite template is not a centered square");
    }
    profile.SpriteTemplateHalfExtent = spriteWidth * 0.5f;
    profile.AccelerationRandomSpan =
        FinitePositive(code.F32(layout.UpdateAccelerationSpanAddress), "acceleration random span");
    profile.UpdateRateScale = FinitePositive(code.F32(layout.UpdateRateScaleAddress), "update-rate scale");
    profile.AccelerationRandomCenter = code.F32(layout.UpdateRandomCenterAddress);
    profile.GlobalUpdateRate = DecodeArmImmediate(
        code.U32(layout.GlobalUpdateRateInstructionAddress));
    if (profile.GlobalUpdateRate == 0) {
        throw std::runtime_error("OOT3D EffectSs global update rate decoded as zero");
    }
    const float atlasCellU = FinitePositive(std::abs(code.F32(layout.AtlasCellUAddress)), "atlas U cell");
    const float atlasCellV = FinitePositive(std::abs(code.F32(layout.AtlasCellVAddress)), "atlas V cell");
    profile.AtlasColumns = static_cast<uint32_t>(std::lround(1.0f / atlasCellU));
    profile.AtlasRows = static_cast<uint32_t>(std::lround(1.0f / atlasCellV));
    profile.TextureFrameCount = static_cast<uint32_t>(std::lround(code.F32(layout.TextureFrameCountAddress)));
    const uint32_t textureFrameMax = static_cast<uint32_t>(std::lround(code.F32(layout.TextureFrameMaxAddress)));
    if (profile.AtlasColumns * profile.AtlasRows != profile.TextureFrameCount ||
        textureFrameMax + 1u != profile.TextureFrameCount) {
        throw std::runtime_error("OOT3D EffectSs type-0 atlas dimensions do not match its update callback");
    }
    profile.FadeTickCount = DecodeArmImmediate(code.U32(layout.FadeThresholdInstructionAddress));

    const uint32_t randomStateAddress = code.U32(layout.RandomStatePointerLiteralAddress);
    profile.RandomInitialState = code.U32(randomStateAddress);
    profile.RandomMultiplier = code.U32(layout.RandomMultiplierAddress);
    profile.RandomIncrement = code.U32(layout.RandomIncrementAddress);
    if (profile.RandomMultiplier == 0) {
        throw std::runtime_error("OOT3D EffectSs random LCG multiplier decoded as zero");
    }

    profile.Decoded = true;
    profile.SourceStatus =
        "decoded_from_oot3d_code_bin_EffectSs_type0_descriptor_resource_and_object_archive_tables";
    return profile;
}

Oot3dNativeHorseDustEmitterProfile DecodeOot3dNativeHorseDustEmitterProfile(
    const std::filesystem::path& codeBinPath, const Oot3dNativeEffectSsCodeLayout& layout) {
    const CodeView code(ReadBinaryFile(codeBinPath), layout.CodeBase);
    Oot3dNativeHorseDustEmitterProfile profile;
    auto& spawn = profile.DustSpawn;
    spawn.Flags = static_cast<uint16_t>(DecodeArmImmediate(
        code.U32(layout.HorseDustEffectFlagsInstructionAddress)));
    spawn.ScaleBase = static_cast<int16_t>(
        DecodeArmImmediate(code.U32(layout.HorseDustScaleBaseInstructionAddress)));
    spawn.ScaleRandomRange = code.F32(layout.HorseDustScaleRandomRangeAddress);
    spawn.ScaleStepBase = static_cast<int16_t>(
        DecodeArmImmediate(code.U32(layout.HorseDustScaleStepBaseInstructionAddress)));
    spawn.ScaleStepRandomRange = code.F32(layout.HorseDustScaleStepRandomRangeAddress);
    spawn.DurationBase = static_cast<int16_t>(
        DecodeArmImmediate(code.U32(layout.HorseDustDurationBaseInstructionAddress)));
    spawn.DurationRandomRange = code.F32(layout.HorseDustDurationRandomRangeAddress);
    spawn.DurationToLifeScale = FinitePositive(
        code.F32(layout.DustDurationToLifeScaleAddress), "EffectSs dust duration-to-life scale");
    spawn.DurationToLifeBias = code.F32(layout.DustDurationToLifeBiasAddress);
    if (!std::isfinite(spawn.ScaleRandomRange) || spawn.ScaleRandomRange < 0.0f ||
        !std::isfinite(spawn.ScaleStepRandomRange) || spawn.ScaleStepRandomRange < 0.0f ||
        !std::isfinite(spawn.DurationRandomRange) || spawn.DurationRandomRange < 0.0f ||
        !std::isfinite(spawn.DurationToLifeBias)) {
        throw std::runtime_error("OOT3D EnHorse dust spawn arguments are invalid");
    }
    spawn.Decoded = true;
    spawn.SourceStatus =
        "decoded_from_oot3d_EnHorse_draw_EffectSsDust_Spawn_call_arguments_and_duration_conversion";
    for (size_t index = 0; index < profile.GallopAnimationIndices.size(); ++index) {
        profile.GallopAnimationIndices[index] = static_cast<uint16_t>(DecodeArmImmediate(
            code.U32(layout.HorseGallopAnimationCompareInstructionAddresses[index])));
    }
    const float hoofOffsetXz = code.F32(layout.HorseHoofOffsetXzAddress);
    profile.HoofLocalOffset = { hoofOffsetXz, code.F32(layout.HorseHoofOffsetYAddress), hoofOffsetXz };

    for (size_t index = 0; index < profile.Contacts.size(); ++index) {
        const auto& lowerInstructions = layout.HorseContactLowerBoundInstructionAddresses[index];
        const uint32_t lowerBits = DecodeArmImmediate(code.U32(lowerInstructions[0])) +
                                   DecodeArmImmediate(code.U32(lowerInstructions[1]));
        uint32_t spanBits = code.U32(layout.HorseContactSpanAddresses[index]);
        if (index + 1u == profile.Contacts.size()) {
            const uint32_t shift = DecodeArmRegisterShiftAmount(
                code.U32(layout.HorseLastContactSpanMvnInstructionAddress));
            spanBits = ~(spanBits << shift);
        }
        auto& contact = profile.Contacts[index];
        contact.FrameMinExclusive = std::bit_cast<float>(lowerBits - 1u);
        contact.FrameMaxExclusive = std::bit_cast<float>(lowerBits + spanBits);
        contact.Flag = static_cast<uint16_t>(DecodeArmImmediate(
            code.U32(layout.HorseContactFlagInstructionAddresses[index])));
        contact.SkeletonNodeIndex = static_cast<uint16_t>(DecodeArmImmediate(
            code.U32(layout.HorseContactNodeInstructionAddresses[index])));
        contact.PositionJitter = code.F32(layout.HorseContactJitterFloatAddresses[index]);
        if (!std::isfinite(contact.FrameMinExclusive) ||
            !std::isfinite(contact.FrameMaxExclusive) ||
            contact.FrameMaxExclusive <= contact.FrameMinExclusive ||
            !std::isfinite(contact.PositionJitter) || contact.PositionJitter < 0.0f) {
            throw std::runtime_error("OOT3D EnHorse hoof-dust contact window did not decode");
        }
    }
    profile.Decoded = true;
    profile.SourceStatus =
        "decoded_from_oot3d_code_bin_EnHorse_draw_gallop_contacts_nodes_and_EffectSsDust_spawn_contract";
    return profile;
}

void ResetOot3dNativeEffectSsRuntime(Oot3dNativeEffectSsRuntime& runtime,
                                     const Oot3dNativeEffectSsDustProfile& profile) {
    runtime.DustProfile = profile;
    runtime.RandomState = profile.RandomInitialState;
    runtime.LastUpdateTick = -1;
    runtime.Particles.clear();
}

float Oot3dNativeEffectSsNextRandom(Oot3dNativeEffectSsRuntime& runtime) {
    runtime.RandomState = runtime.RandomState * runtime.DustProfile.RandomMultiplier +
                          runtime.DustProfile.RandomIncrement;
    const uint32_t bits = (runtime.RandomState >> 9) | 0x3F800000u;
    return std::bit_cast<float>(bits) - 1.0f;
}

float Oot3dNativeEffectSsNextRandomCentered(Oot3dNativeEffectSsRuntime& runtime,
                                           float halfExtent) {
    return Oot3dNativeEffectSsNextRandom(runtime) * (halfExtent * 2.0f) - halfExtent;
}

void SpawnOot3dNativeEffectSsDust(Oot3dNativeEffectSsRuntime& runtime,
                                 const Oot3dNativeEffectSsDustSpawnProfile& spawnProfile,
                                 Vec3f position,
                                 Vec3f velocity, Vec3f acceleration) {
    const auto& profile = runtime.DustProfile;
    if (!profile.Decoded || !profile.TextureLoaded || !spawnProfile.Decoded ||
        profile.GlobalUpdateRate == 0) {
        return;
    }
    Oot3dNativeEffectSsParticle particle;
    particle.Position = position;
    particle.Velocity = velocity;
    particle.Acceleration = acceleration;
    particle.Scale = static_cast<int16_t>(spawnProfile.ScaleBase + static_cast<int16_t>(
        Oot3dNativeEffectSsNextRandom(runtime) * spawnProfile.ScaleRandomRange));
    particle.ScaleStep = static_cast<int16_t>(spawnProfile.ScaleStepBase + static_cast<int16_t>(
        Oot3dNativeEffectSsNextRandom(runtime) * spawnProfile.ScaleStepRandomRange));
    const int16_t duration = static_cast<int16_t>(
        spawnProfile.DurationBase + static_cast<int16_t>(
            Oot3dNativeEffectSsNextRandom(runtime) * spawnProfile.DurationRandomRange));
    particle.InitialLife = static_cast<int16_t>(
        static_cast<int>(static_cast<float>(duration) * spawnProfile.DurationToLifeScale /
                         static_cast<float>(profile.GlobalUpdateRate) +
                         spawnProfile.DurationToLifeBias));
    particle.RemainingLife = particle.InitialLife;
    particle.Flags = spawnProfile.Flags;
    particle.PrimaryColor = profile.PrimaryColor;
    particle.EnvironmentColor = profile.EnvironmentColor;
    if ((particle.Flags & 4u) != 0 && profile.ColorRandomRange > 0.0f) {
        const int colorOffset = static_cast<int>(
            Oot3dNativeEffectSsNextRandom(runtime) * profile.ColorRandomRange -
            profile.ColorRandomOffset);
        const auto offsetChannel = [colorOffset](uint8_t channel) {
            return static_cast<uint8_t>(std::clamp(static_cast<int>(channel) + colorOffset, 0, 255));
        };
        particle.PrimaryColor.R = offsetChannel(particle.PrimaryColor.R);
        particle.PrimaryColor.G = offsetChannel(particle.PrimaryColor.G);
        particle.PrimaryColor.B = offsetChannel(particle.PrimaryColor.B);
    }
    runtime.Particles.push_back(particle);
}

void UpdateOot3dNativeEffectSs(Oot3dNativeEffectSsRuntime& runtime) {
    const auto& profile = runtime.DustProfile;
    if (!profile.Decoded) {
        return;
    }
    for (auto& particle : runtime.Particles) {
        if (particle.RemainingLife <= 0) {
            continue;
        }
        particle.Position = Add(particle.Position, particle.Velocity);
        particle.Velocity = Add(particle.Velocity, particle.Acceleration);
        --particle.RemainingLife;
        if (particle.RemainingLife <= 0) {
            continue;
        }

        const float accelerationSpan = profile.AccelerationRandomSpan *
                                       static_cast<float>(profile.GlobalUpdateRate) *
                                       profile.UpdateRateScale;
        particle.Acceleration.X = accelerationSpan *
            (Oot3dNativeEffectSsNextRandom(runtime) - profile.AccelerationRandomCenter);
        particle.Acceleration.Z = accelerationSpan *
            (Oot3dNativeEffectSsNextRandom(runtime) - profile.AccelerationRandomCenter);
        const int initialLife = particle.InitialLife;
        const int remainingLife = particle.RemainingLife;
        const int textureFrameMax = static_cast<int>(profile.TextureFrameCount) - 1;
        int textureFrame = textureFrameMax;
        if ((particle.Flags & 0x30u) == 0) {
            if (initialLife >= remainingLife && remainingLife >= initialLife - textureFrameMax) {
                textureFrame = initialLife < 5
                                   ? static_cast<int>(
                                         (static_cast<float>(profile.TextureFrameCount) /
                                          static_cast<float>(initialLife)) *
                                         static_cast<float>(initialLife - remainingLife - 1))
                                   : initialLife - remainingLife - 1;
            }
        } else if (initialLife > 0) {
            textureFrame = static_cast<int>(
                (1.0f - static_cast<float>(remainingLife + 1) /
                            static_cast<float>(initialLife)) *
                static_cast<float>(textureFrameMax));
        }
        particle.TextureFrame = static_cast<uint16_t>(
            std::clamp(textureFrame, 0, textureFrameMax));
        const float scaleStep = static_cast<float>(particle.ScaleStep) *
                                static_cast<float>(profile.GlobalUpdateRate) *
                                profile.UpdateRateScale;
        particle.Scale = static_cast<int16_t>(particle.Scale + static_cast<int16_t>(scaleStep + 0.5f));
    }
    std::erase_if(runtime.Particles, [](const auto& particle) { return particle.RemainingLife <= 0; });
}

Oot3dNativeEffectSsRenderEnvironment ResolveOot3dNativeEffectSsRenderEnvironment(
    const Oot3dNativeEffectSsDustProfile& profile, ColorRgba8 ambientColor,
    uint32_t skyboxCommandArgument) {
    Oot3dNativeEffectSsRenderEnvironment environment;
    environment.AmbientColor = ambientColor;
    const uint8_t sceneColorScaleByte = static_cast<uint8_t>(skyboxCommandArgument >> 24);
    environment.SceneColorScale =
        sceneColorScaleByte == 0
            ? profile.SceneSkyboxDefaultColorScale
            : static_cast<float>(sceneColorScaleByte) * profile.SceneSkyboxByteColorScale;
    const auto channelMultiplier = [&](uint8_t channel) {
        const float scaled = std::trunc(static_cast<float>(channel) * environment.SceneColorScale);
        const float biased = std::min(
            scaled + static_cast<float>(profile.GlobalEnvironmentColorBias),
            profile.GlobalEnvironmentColorClamp);
        return biased * profile.GlobalEnvironmentColorNormalizeScale;
    };
    environment.GlobalColorMultiplier = {
        channelMultiplier(ambientColor.R),
        channelMultiplier(ambientColor.G),
        channelMultiplier(ambientColor.B),
    };
    environment.Resolved = true;
    environment.SourceStatus =
        "decoded_from_oot3d_00479e90_effect_global_color_and_scene_command_0x11_scale";
    return environment;
}

Oot3dNativeRenderModel MaterializeOot3dNativeEffectSsDust(
    const Oot3dNativeEffectSsRuntime& runtime, Vec3f cameraRight, Vec3f cameraUp,
    const Oot3dNativeEffectSsRenderEnvironment* environment) {
    Oot3dNativeRenderModel model;
    const auto& profile = runtime.DustProfile;
    if (!profile.Decoded || !profile.TextureLoaded || runtime.Particles.empty()) {
        return model;
    }
    cameraRight = Normalize(cameraRight);
    cameraUp = Normalize(cameraUp);
    if ((cameraRight.X == 0.0f && cameraRight.Y == 0.0f && cameraRight.Z == 0.0f) ||
        (cameraUp.X == 0.0f && cameraUp.Y == 0.0f && cameraUp.Z == 0.0f)) {
        return model;
    }

    model.Source = profile.ObjectArchivePath.string() + "!" + profile.TextureName +
                   ";" + profile.SourceStatus;
    model.Name = "effect_ss:type0:dust";
    model.TransformBakedIntoVertices = true;
    model.Textures.push_back(profile.Texture);
    Oot3dNativeRenderBatch batch;
    batch.Material.Textured = true;
    batch.Material.TextureIndex = 0;
    batch.Material.TextureBindingSource = "oot3d_EffectSs_type0_native_dependency_binding";
    batch.Material.TextureHasNativeAlpha = profile.Texture.HasNativeAlpha;
    batch.Material.VertexColorModulatesTexture = true;
    batch.Material.NativeRuntimeVertexAlphaBlend = true;
    batch.Material.NativeSamplerStateDecoded = true;
    batch.Material.NativeSamplerMinFilter = profile.SamplerMinFilter;
    batch.Material.NativeSamplerMagFilter = profile.SamplerMagFilter;
    batch.Material.NativeSamplerWrapS = profile.SamplerWrapS;
    batch.Material.NativeSamplerWrapT = profile.SamplerWrapT;
    batch.Material.NativeSamplerStateSource = "oot3d_EffectSs_resource_binding_sampler";
    batch.Material.TextureMapperSamplerStates[0] = {
        true, profile.SamplerMinFilter, profile.SamplerMagFilter,
        profile.SamplerWrapS, profile.SamplerWrapT, 0.0f,
        "oot3d_EffectSs_resource_binding_sampler",
    };
    batch.Material.CmbCullFace = 3;
    batch.Material.PicaCullMode = Oot3dNativePicaCullMode::KeepAll;
    batch.Material.DepthTest = true;
    batch.Material.DepthWrite = false;
    batch.Material.NativePicaFogOverrideDecoded = true;
    batch.Material.NativePicaFogEnabled = false;
    batch.Material.NativePicaFogOverrideSource =
        "oot3d_EffectSs_type0_sprite_draw_path_without_scene_fog";
    batch.Material.NativeRenderStateDecoded = true;
    batch.Material.NativeBlendStateEnabled = true;
    batch.Material.NativeBlendStateSupported = true;
    batch.Material.NativeBlendFactorsSupported = true;
    batch.Material.NativeBlendEquationSupported = true;
    batch.Material.BlendMode = 1;
    batch.Material.BlendSrc = 0x0302;
    batch.Material.BlendDst = 0x0303;
    batch.Material.BlendEquation = 0x8006;
    batch.Material.ColorBlendSrc = 0x0302;
    batch.Material.ColorBlendDst = 0x0303;
    batch.Material.ColorBlendEquation = 0x8006;
    auto& textureEnv = batch.Material.TextureEnvProgram;
    textureEnv.Decoded = true;
    textureEnv.StageCount = 1;
    textureEnv.ColorShaderPath = "effect_ss_texture_primary_plus_environment";
    textureEnv.ColorShaderPathSupported = true;
    textureEnv.ColorShaderPathApplied = true;
    textureEnv.RgbCombinesKnown = true;
    textureEnv.RgbSourcesKnown = true;
    textureEnv.RgbOperandsKnown = true;
    textureEnv.UsesPrimaryColor = true;
    textureEnv.UsesConstantColor = true;
    textureEnv.UsesTexture0 = true;
    textureEnv.RgbRouteDecoded = true;
    textureEnv.TextureColorAddendResolved = true;
    textureEnv.TextureColorAddStageCount = 1;
    const auto modulateColorChannel = [](uint8_t channel, float multiplier) {
        return static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::trunc(static_cast<float>(channel) * multiplier)), 0, 255));
    };
    textureEnv.TextureColorAddUsesTexture0Alpha = true;
    textureEnv.Texture0PrimaryColorAlphaModulateResolved = true;
    textureEnv.Texture0PrimaryColorAlphaModulateStageCount = 1;
    textureEnv.Texture0PrimaryColorAlphaModulateSource =
        "oot3d_EffectSs_type0_texture_alpha_times_primary_alpha";

    constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 2, 1, 3 };
    const auto appendRoute = [&](bool usesGlobalEnvironmentColor) {
        Oot3dNativeRenderBatch routeBatch = batch;
        const Vec3f colorMultiplier =
            usesGlobalEnvironmentColor && environment != nullptr && environment->Resolved
                ? environment->GlobalColorMultiplier
                : Vec3f{ 1.0f, 1.0f, 1.0f };
        routeBatch.Material.TextureEnvProgram.TextureColorAddend = {
            modulateColorChannel(profile.EnvironmentColor.R, colorMultiplier.X),
            modulateColorChannel(profile.EnvironmentColor.G, colorMultiplier.Y),
            modulateColorChannel(profile.EnvironmentColor.B, colorMultiplier.Z),
            profile.EnvironmentColor.A,
        };
        for (const auto& particle : runtime.Particles) {
            if (particle.RemainingLife <= 0 ||
                ((particle.Flags & 1u) != 0) != usesGlobalEnvironmentColor) {
                continue;
            }
            const float extent = static_cast<float>(particle.Scale) * profile.RenderScale *
                                 profile.SpriteTemplateHalfExtent;
            const Vec3f rightExtent = Scale(cameraRight, extent);
            const Vec3f upExtent = Scale(cameraUp, extent);
            const std::array<Vec3f, 4> corners = {
                Subtract(Subtract(particle.Position, rightExtent), upExtent),
                Add(Subtract(particle.Position, rightExtent), upExtent),
                Subtract(Add(particle.Position, rightExtent), upExtent),
                Add(Add(particle.Position, rightExtent), upExtent),
            };
            const uint32_t frame =
                std::min<uint32_t>(particle.TextureFrame, profile.TextureFrameCount - 1u);
            const uint32_t column = frame % profile.AtlasColumns;
            const uint32_t row = frame / profile.AtlasColumns;
            const float u0 = static_cast<float>(column) / static_cast<float>(profile.AtlasColumns);
            const float u1 = static_cast<float>(column + 1u) / static_cast<float>(profile.AtlasColumns);
            const float v0 = static_cast<float>(row) / static_cast<float>(profile.AtlasRows);
            const float v1 = static_cast<float>(row + 1u) / static_cast<float>(profile.AtlasRows);
            const std::array<Vec2f, 4> nativeUvs = {
                Vec2f{ u0, v1 }, Vec2f{ u0, v0 }, Vec2f{ u1, v1 }, Vec2f{ u1, v0 },
            };
            const std::array<Vec2f, 4> renderUvs = {
                Vec2f{ u0, 1.0f - v1 }, Vec2f{ u0, 1.0f - v0 },
                Vec2f{ u1, 1.0f - v1 }, Vec2f{ u1, 1.0f - v0 },
            };
            const ColorRgba8 textureMultiplierColor = {
                modulateColorChannel(
                    ClampColorDifference(particle.PrimaryColor.R, particle.EnvironmentColor.R),
                    colorMultiplier.X),
                modulateColorChannel(
                    ClampColorDifference(particle.PrimaryColor.G, particle.EnvironmentColor.G),
                    colorMultiplier.Y),
                modulateColorChannel(
                    ClampColorDifference(particle.PrimaryColor.B, particle.EnvironmentColor.B),
                    colorMultiplier.Z),
                255,
            };
            uint8_t alpha = particle.PrimaryColor.A;
            if (profile.FadeTickCount > 0 &&
                particle.RemainingLife < static_cast<int16_t>(profile.FadeTickCount)) {
                alpha = static_cast<uint8_t>(std::clamp(
                    static_cast<int>(particle.PrimaryColor.A) * particle.RemainingLife /
                        static_cast<int>(profile.FadeTickCount),
                    0, 255));
            }
            for (const uint32_t index : indices) {
                Oot3dNativeRenderVertex vertex;
                vertex.Position = corners[index];
                vertex.Uv0 = renderUvs[index];
                vertex.NativeSourceUv0 = nativeUvs[index];
                vertex.NativeSourceUv0Available = true;
                vertex.Color = textureMultiplierColor;
                vertex.Color.A = alpha;
                vertex.NativeColorAvailable = true;
                routeBatch.Vertices.push_back(vertex);
                ExpandBounds(model.LocalBounds, vertex.Position);
            }
        }
        if (!routeBatch.Vertices.empty()) {
            model.Batches.push_back(std::move(routeBatch));
        }
    };
    appendRoute(false);
    appendRoute(true);
    model.Bounds = Oot3dNativeRenderModelWorldBounds(model);
    return model;
}

bool Oot3dNativeHorseDustAnimationMatches(const Oot3dNativeHorseDustEmitterProfile& profile,
                                          uint16_t animationIndex) {
    return profile.Decoded &&
           std::find(profile.GallopAnimationIndices.begin(), profile.GallopAnimationIndices.end(),
                     animationIndex) != profile.GallopAnimationIndices.end();
}

const Oot3dNativeHorseDustContact* Oot3dNativeHorseDustContactAtFrame(
    const Oot3dNativeHorseDustEmitterProfile& profile, uint16_t animationIndex,
    float animationFrame) {
    if (!Oot3dNativeHorseDustAnimationMatches(profile, animationIndex)) {
        return nullptr;
    }
    for (const auto& contact : profile.Contacts) {
        if (animationFrame > contact.FrameMinExclusive && animationFrame < contact.FrameMaxExclusive) {
            return &contact;
        }
    }
    return nullptr;
}

} // namespace ThreeDsRecomp::Oot3d
