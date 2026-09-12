#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_canonical_hash.h"
#include "oot3d_native_pica_fragment_lighting_gen.h"
#include "oot3d_native_pica_proctex.h"
#include "fast/oot3d/pica_fragment_lighting.h"

#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace Oot3dNativeGame {
namespace {

constexpr uint64_t kFnvOffset = CanonicalHashDetail::OffsetBasis;
constexpr uint64_t kFnvPrime = CanonicalHashDetail::Prime;

constexpr std::array<uint16_t, 6> kTevStageBases{
    0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U};

constexpr uint32_t FeatureBit(Oot3dPicaFragmentUnsupportedFeature feature) {
    return static_cast<uint32_t>(feature);
}

bool IsSupportedColorModifier(uint8_t modifier) {
    switch (modifier) {
    case 0x0U:
    case 0x1U:
    case 0x2U:
    case 0x3U:
    case 0x4U:
    case 0x5U:
    case 0x8U:
    case 0x9U:
    case 0xCU:
    case 0xDU:
        return true;
    default:
        return false;
    }
}

bool IsSupportedTevSource(uint8_t source) {
    return source <= 0x6U || source == 0xDU || source == 0xEU ||
           source == 0xFU;
}

class CanonicalHashBuilder {
  public:
    explicit CanonicalHashBuilder(std::string_view domain) {
        AppendU32(kOot3dPicaProgramDescriptorSchemaVersion);
        AppendU32(static_cast<uint32_t>(domain.size()));
        AppendBytes(std::as_bytes(std::span(domain)));
    }

    void AppendU8(uint8_t value) {
        mHash = (mHash ^ value) * kFnvPrime;
    }

    void AppendBool(bool value) {
        AppendU8(value ? 1U : 0U);
    }

    void AppendU16(uint16_t value) {
        AppendU8(static_cast<uint8_t>(value));
        AppendU8(static_cast<uint8_t>(value >> 8U));
    }

    void AppendU32(uint32_t value) {
        mHash = CanonicalHashDetail::AppendWord(mHash, value);
    }

    void AppendU64(uint64_t value) {
        for (uint32_t shift = 0; shift < 64U; shift += 8U) {
            AppendU8(static_cast<uint8_t>(value >> shift));
        }
    }

    void AppendS16(int16_t value) {
        AppendU16(std::bit_cast<uint16_t>(value));
    }

    void AppendFloat(float value) {
        AppendU32(std::bit_cast<uint32_t>(value));
    }

    void AppendBytes(std::span<const std::byte> bytes) {
        for (const std::byte byte : bytes) {
            AppendU8(std::to_integer<uint8_t>(byte));
        }
    }

    template <size_t Size>
    void AppendWords(const std::array<uint32_t, Size>& words,
                     size_t count = Size) {
        AppendU32(static_cast<uint32_t>(count));
        for (size_t index = 0; index < count; ++index) {
            AppendU32(words[index]);
        }
    }

    uint64_t Finish() const {
        return mHash == 0U ? 1U : mHash;
    }

  private:
    uint64_t mHash = kFnvOffset;
};

void AppendShaderProgram(CanonicalHashBuilder& hash,
                         const Oot3dPicaShaderState& shader) {
    hash.AppendWords(shader.Program.Values(), shader.ProgramWordCount);
    hash.AppendWords(shader.Swizzles.Values(), shader.SwizzleWordCount);
}

uint64_t BuildVertexProgramIdImpl(const Oot3dPicaDrawPacket& packet,
                                  const Oot3dPicaDecodedDrawState& state) {
    CanonicalHashBuilder hash("oot3d.pica.vertex_program");
    AppendShaderProgram(hash, packet.VertexShader);
    hash.AppendBool(state.ShaderInterface.GeometryShaderEnabled);
    if (state.ShaderInterface.GeometryShaderEnabled) {
        AppendShaderProgram(hash, packet.GeometryShader);
    }
    hash.AppendU16(state.ShaderInterface.VertexMainOffset);
    hash.AppendU8(state.ShaderInterface.MaximumInputAttribute);
    for (const uint8_t input :
         state.ShaderInterface.InputRegisterByAttribute) {
        hash.AppendU8(input);
    }
    hash.AppendU16(state.ShaderInterface.OutputMask);
    for (uint16_t reg = 0x04FU; reg <= 0x056U; ++reg) {
        hash.AppendU32(packet.Registers[reg]);
    }
    // Shader mode and input-map registers consumed by the decompiler.
    for (const uint16_t reg :
         std::array<uint16_t, 4>{0x229U, 0x2B9U, 0x2BAU, 0x2BDU}) {
        hash.AppendU32(packet.Registers[reg]);
    }
    return hash.Finish();
}

uint64_t BuildFragmentProgramId(const Oot3dPicaDrawPacket& packet,
                                const Oot3dPicaDecodedDrawState& state) {
    CanonicalHashBuilder hash("oot3d.pica.fragment_program");
    hash.AppendU32(packet.Registers[0x080U]);
    hash.AppendBool(state.Texture2UsesCoordinate1);
    for (const auto& texture : state.Textures) {
        hash.AppendBool(texture.Enabled);
        hash.AppendU8(texture.Type);
    }

    // Fragment-lighting, procedural-texture and fog modes alter generated
    // shader control flow. Data tables and colors remain dynamic state.
    hash.AppendU32(packet.Registers[0x08FU]);
    for (uint16_t reg = 0x0A8U; reg <= 0x0AFU; ++reg) {
        hash.AppendU32(packet.Registers[reg]);
    }
    const auto lighting =
        Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
    const bool shadowProcTexReferenced =
        lighting.Enabled && lighting.ShadowFactorEnabled &&
        lighting.ShadowTextureUnit == 3U;
    if ((packet.Registers[0x080U] & (1U << 10U)) != 0U &&
        (Oot3dPicaReferencesProceduralTexture(packet) ||
         shadowProcTexReferenced)) {
        hash.AppendWords(packet.ProcTexLuts.Noise);
        hash.AppendWords(packet.ProcTexLuts.ColorMap);
        hash.AppendWords(packet.ProcTexLuts.AlphaMap);
        hash.AppendWords(packet.ProcTexLuts.Color);
        hash.AppendWords(packet.ProcTexLuts.ColorDifference);
    }
    hash.AppendU32(packet.Registers[0x0E0U]);
    hash.AppendU32(packet.Registers[0x104U] & 0x00000071U);
    hash.AppendU32(packet.Registers[0x100U] & 3U);

    for (const uint16_t base : kTevStageBases) {
        hash.AppendU32(packet.Registers[base]);
        hash.AppendU32(packet.Registers[base + 1U]);
        hash.AppendU32(packet.Registers[base + 2U]);
        hash.AppendU32(packet.Registers[base + 4U]);
    }

    // This mirrors the structural portion of Azahar's LightConfig: global
    // mode/LUT routing plus each light's feature-enable word. Color,
    // direction, position and LUT samples are uniform data.
    for (const uint16_t reg : std::array<uint16_t, 7>{
             0x1C2U, 0x1C3U, 0x1C4U, 0x1C6U,
             0x1D0U, 0x1D1U, 0x1D2U}) {
        hash.AppendU32(packet.Registers[reg]);
    }
    hash.AppendU32(packet.Registers[0x1D9U]);
    for (uint16_t light = 0; light < 8U; ++light) {
        hash.AppendU32(packet.Registers[0x140U + light * 0x10U + 9U]);
    }
    return hash.Finish();
}

uint64_t BuildRasterStateId(const Oot3dPicaDrawPacket& packet,
                            const Oot3dPicaDecodedDrawState& state) {
    CanonicalHashBuilder hash("oot3d.pica.raster_state");
    hash.AppendU8(static_cast<uint8_t>(state.Topology));
    hash.AppendU8(static_cast<uint8_t>(state.CullMode));
    hash.AppendU8(state.VertexInput.AttributeCount);
    for (const auto& attribute : state.VertexInput.Attributes) {
        hash.AppendU8(static_cast<uint8_t>(attribute.Format));
        hash.AppendU8(attribute.ComponentCount);
        hash.AppendBool(attribute.Default);
    }
    for (const auto& loader : state.VertexInput.Loaders) {
        hash.AppendU8(loader.ByteStride);
        hash.AppendU8(loader.ComponentCount);
        for (const uint8_t component : loader.Components) {
            hash.AppendU8(component);
        }
    }
    hash.AppendBool(state.VertexInput.Indexed);
    hash.AppendBool(state.VertexInput.IndicesAre16Bit);
    hash.AppendU8(state.Framebuffer.ColorFormat);
    hash.AppendU8(state.Framebuffer.DepthFormat);
    hash.AppendBool(state.Framebuffer.Flipped);
    hash.AppendBool(state.Framebuffer.ColorWriteEnabled);
    hash.AppendBool(state.Framebuffer.DepthStencilWriteEnabled);
    hash.AppendU8(static_cast<uint8_t>(state.Scissor.Mode));
    hash.AppendU8(state.OutputMerger.FragmentOperationMode);
    hash.AppendU8(state.OutputMerger.ColorWriteMask);
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.LogicOperation));
    hash.AppendBool(state.OutputMerger.Depth.TestEnabled);
    hash.AppendBool(state.OutputMerger.Depth.WriteEnabled);
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Depth.Compare));
    hash.AppendBool(state.OutputMerger.Stencil.Enabled);
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Stencil.Compare));
    hash.AppendU8(state.OutputMerger.Stencil.CompareMask);
    hash.AppendU8(state.OutputMerger.Stencil.WriteMask);
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Stencil.Fail));
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Stencil.DepthFail));
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Stencil.Pass));
    hash.AppendBool(state.OutputMerger.Blend.Enabled);
    hash.AppendU8(
        static_cast<uint8_t>(state.OutputMerger.Blend.ColorEquation));
    hash.AppendU8(
        static_cast<uint8_t>(state.OutputMerger.Blend.AlphaEquation));
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Blend.SourceColor));
    hash.AppendU8(
        static_cast<uint8_t>(state.OutputMerger.Blend.DestinationColor));
    hash.AppendU8(static_cast<uint8_t>(state.OutputMerger.Blend.SourceAlpha));
    hash.AppendU8(
        static_cast<uint8_t>(state.OutputMerger.Blend.DestinationAlpha));
    for (const auto& texture : state.Textures) {
        hash.AppendU8(texture.WrapS);
        hash.AppendU8(texture.WrapT);
        hash.AppendBool(texture.MinLinear);
        hash.AppendBool(texture.MagLinear);
    }
    // Preserve raw output-merger encodings as a collision guard while the
    // decoded contract grows to cover remaining PICA modes.
    for (uint16_t reg = 0x100U; reg <= 0x107U; ++reg) {
        hash.AppendU32(packet.Registers[reg]);
    }
    return hash.Finish();
}

uint64_t BuildPipelineId(uint64_t vertexProgramId,
                         uint64_t fragmentProgramId,
                         uint64_t rasterStateId) {
    CanonicalHashBuilder hash("oot3d.pica.pipeline");
    hash.AppendU64(vertexProgramId);
    hash.AppendU64(fragmentProgramId);
    hash.AppendU64(rasterStateId);
    return hash.Finish();
}

uint64_t BuildDynamicStateId(const Oot3dPicaDrawPacket& packet,
                             const Oot3dPicaDecodedDrawState& state) {
    CanonicalHashBuilder hash("oot3d.pica.dynamic_state");
    hash.AppendU32(state.VertexInput.PhysicalBaseAddress);
    hash.AppendU32(state.VertexInput.IndexPhysicalAddress);
    hash.AppendU32(state.VertexInput.VertexCount);
    hash.AppendU32(state.VertexInput.VertexOffset);
    for (const auto& loader : state.VertexInput.Loaders) {
        hash.AppendU32(loader.DataOffset);
        hash.AppendU32(loader.PhysicalAddress);
    }
    for (const auto& texture : state.Textures) {
        hash.AppendU32(texture.PhysicalAddress);
        hash.AppendU16(texture.Width);
        hash.AppendU16(texture.Height);
        hash.AppendU8(texture.Format);
    }
    hash.AppendU32(state.Framebuffer.ColorPhysicalAddress);
    hash.AppendU32(state.Framebuffer.DepthPhysicalAddress);
    hash.AppendU16(state.Framebuffer.Width);
    hash.AppendU16(state.Framebuffer.Height);
    hash.AppendFloat(state.Viewport.HalfWidth);
    hash.AppendFloat(state.Viewport.HalfHeight);
    hash.AppendFloat(state.Viewport.DepthRange);
    hash.AppendFloat(state.Viewport.NearPlane);
    hash.AppendS16(state.Viewport.CornerX);
    hash.AppendS16(state.Viewport.CornerY);
    hash.AppendBool(state.Viewport.ZBuffering);
    hash.AppendU16(state.Scissor.X1);
    hash.AppendU16(state.Scissor.Y1);
    hash.AppendU16(state.Scissor.X2);
    hash.AppendU16(state.Scissor.Y2);
    hash.AppendU8(state.OutputMerger.Stencil.Reference);
    for (const float component : state.OutputMerger.Blend.ConstantColor) {
        hash.AppendFloat(component);
    }

    for (const bool value : packet.VertexShader.BooleanUniforms) {
        hash.AppendBool(value);
    }
    for (const auto& row : packet.VertexShader.IntegerUniforms) {
        for (const uint8_t value : row) {
            hash.AppendU8(value);
        }
    }
    for (const auto& row : packet.VertexShader.FloatUniforms) {
        for (const float value : row) {
            hash.AppendFloat(value);
        }
    }
    for (const auto& attribute : packet.DefaultAttributes) {
        for (const float value : attribute) {
            hash.AppendFloat(value);
        }
    }
    for (const uint16_t base : kTevStageBases) {
        hash.AppendU32(packet.Registers[base + 3U]);
    }
    hash.AppendU32(packet.Registers[0x0E1U]);
    hash.AppendU32(packet.Registers[0x0FDU]);
    hash.AppendU32(packet.Registers[0x08BU]);
    hash.AppendU32(packet.Registers[0x130U]);
    hash.AppendWords(packet.FogLut);

    // Lighting values and lookup-table data are uniforms. Hash the complete
    // native ranges so traces detect missing state before every field has a
    // typed representation in the renderer contract.
    for (uint16_t reg = 0x140U; reg <= 0x1D9U; ++reg) {
        hash.AppendU32(packet.Registers[reg]);
    }
    if (packet.LightingLuts != nullptr &&
        packet.LightingLuts->ContentHashAvailable) {
        hash.AppendU64(packet.LightingLuts->ContentHash);
    }
    return hash.Finish();
}

uint64_t BuildFullRegisterStateId(const Oot3dPicaDrawPacket& packet) {
    CanonicalHashBuilder hash("oot3d.pica.register_file");
    hash.AppendWords(packet.Registers);
    if (packet.LightingLuts != nullptr &&
        packet.LightingLuts->ContentHashAvailable) {
        hash.AppendU64(packet.LightingLuts->ContentHash);
    }
    return hash.Finish();
}

} // namespace

uint64_t HashOot3dPicaCanonicalBytes(std::span<const uint8_t> bytes) {
    const uint64_t hash = CanonicalHashDetail::AppendBytes(kFnvOffset, bytes);
    return hash == 0U ? 1U : hash;
}

std::string FormatOot3dPicaCanonicalId(uint64_t id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << id;
    return stream.str();
}

Oot3dPicaFragmentFeatureSet AnalyzeOot3dPicaFragmentFeatures(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state) {
    Oot3dPicaFragmentFeatureSet result;
    result.SurfaceColorResponse = Fast::Renderer3ds::DecodePicaSurfaceColorResponse(packet.Registers);
    result.FragmentLightingEnabled = Oot3dPicaFragmentLightingEnabled(packet);
    result.FragmentLighting =
        BuildOot3dPicaFragmentLightingLayout(packet);
    result.ProceduralTextureEnabled =
        (packet.Registers[0x080U] & (1U << 10U)) != 0U;
    result.FogMode = static_cast<uint8_t>(packet.Registers[0x0E0U] & 7U);
    result.FogEnabled = result.FogMode == 5U;
    result.FogFlip =
        result.FogEnabled &&
        (packet.Registers[0x0E0U] & (1U << 16U)) != 0U;
    result.GasEnabled = result.FogMode == 7U;
    result.Texture0Type = state.Textures[0].Type;
    for (size_t texture = 0; texture < state.Textures.size(); ++texture) {
        if (state.Textures[texture].Enabled) {
            result.EnabledTextureMask |= static_cast<uint8_t>(1U << texture);
        }
    }

    const auto lighting =
        Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
    if (lighting.Enabled &&
        lighting.BumpMode != Fast::Oot3d::PicaLightingBumpMode::None &&
        lighting.BumpTextureUnit < state.Textures.size()) {
        result.ReferencedTextureMask |= static_cast<uint8_t>(
            1U << lighting.BumpTextureUnit);
    }
    if (lighting.Enabled && lighting.ShadowFactorEnabled) {
        result.ReferencedTextureMask |= static_cast<uint8_t>(
            1U << lighting.ShadowTextureUnit);
    }

    bool tevEncodingSupported = true;
    for (const uint16_t base : kTevStageBases) {
        const uint32_t sources = packet.Registers[base];
        const uint32_t modifiers = packet.Registers[base + 1U];
        const uint32_t operations = packet.Registers[base + 2U];
        for (size_t input = 0; input < 3U; ++input) {
            const uint8_t colorSource = static_cast<uint8_t>(
                (sources >> (input * 4U)) & 0xFU);
            const uint8_t alphaSource = static_cast<uint8_t>(
                (sources >> (16U + input * 4U)) & 0xFU);
            for (const uint8_t source : {colorSource, alphaSource}) {
                tevEncodingSupported &= IsSupportedTevSource(source);
                if (source >= 0x3U && source <= 0x6U) {
                    result.ReferencedTextureMask |=
                        static_cast<uint8_t>(1U << (source - 0x3U));
                }
            }
            tevEncodingSupported &= IsSupportedColorModifier(
                static_cast<uint8_t>((modifiers >> (input * 4U)) & 0xFU));
        }
        const uint8_t colorOperation =
            static_cast<uint8_t>(operations & 0xFU);
        const uint8_t alphaOperation =
            static_cast<uint8_t>((operations >> 16U) & 0xFU);
        tevEncodingSupported &= colorOperation <= 9U;
        tevEncodingSupported &=
            alphaOperation <= 5U || alphaOperation == 8U ||
            alphaOperation == 9U || colorOperation == 7U;
    }
    result.ProceduralTextureReferenced =
        (result.ReferencedTextureMask & (1U << 3U)) != 0U;

    if (result.FragmentLightingEnabled &&
        !Oot3dPicaFragmentLightingConfigurationSupported(packet)) {
        result.UnsupportedFeatureMask |= FeatureBit(
            Oot3dPicaFragmentUnsupportedFeature::FragmentLighting);
    }
    if (result.ProceduralTextureReferenced &&
        result.ProceduralTextureEnabled &&
        !Oot3dPicaProceduralTextureConfigurationSupported(packet)) {
        result.UnsupportedFeatureMask |= FeatureBit(
            Oot3dPicaFragmentUnsupportedFeature::ProceduralTexture);
    }
    if ((result.ReferencedTextureMask & 1U) != 0U &&
        state.Textures[0].Enabled) {
        switch (result.Texture0Type) {
        case 0U:
        case 2U:
        case 3U:
        case 5U:
            break;
        case 1U:
            result.UnsupportedFeatureMask |= FeatureBit(
                Oot3dPicaFragmentUnsupportedFeature::TextureCube);
            break;
        case 4U:
            result.UnsupportedFeatureMask |= FeatureBit(
                Oot3dPicaFragmentUnsupportedFeature::ShadowCube);
            break;
        default:
            result.UnsupportedFeatureMask |= FeatureBit(
                Oot3dPicaFragmentUnsupportedFeature::TevEncoding);
            break;
        }
    }
    if (result.GasEnabled) {
        result.UnsupportedFeatureMask |=
            FeatureBit(Oot3dPicaFragmentUnsupportedFeature::Gas);
    } else if (result.FogMode != 0U && !result.FogEnabled) {
        result.UnsupportedFeatureMask |= FeatureBit(
            Oot3dPicaFragmentUnsupportedFeature::InvalidFogMode);
    }
    if (!tevEncodingSupported) {
        result.UnsupportedFeatureMask |= FeatureBit(
            Oot3dPicaFragmentUnsupportedFeature::TevEncoding);
    }
    return result;
}

Oot3dPicaCanonicalDrawIdentity BuildOot3dPicaCanonicalDrawIdentity(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state) {
    return BuildOot3dPicaCanonicalDrawIdentity(
        packet, state, BuildVertexProgramIdImpl(packet, state));
}

uint64_t BuildOot3dPicaCanonicalVertexProgramId(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state) {
    return BuildVertexProgramIdImpl(packet, state);
}

Oot3dPicaCanonicalDrawIdentity BuildOot3dPicaCanonicalDrawIdentity(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    uint64_t vertexProgramId) {
    const uint64_t fragmentProgramId =
        BuildFragmentProgramId(packet, state);
    const uint64_t rasterStateId = BuildRasterStateId(packet, state);
    return {
        kOot3dPicaProgramDescriptorSchemaVersion,
        vertexProgramId,
        fragmentProgramId,
        rasterStateId,
        BuildPipelineId(vertexProgramId, fragmentProgramId, rasterStateId),
        BuildDynamicStateId(packet, state),
        BuildFullRegisterStateId(packet),
    };
}

} // namespace Oot3dNativeGame
