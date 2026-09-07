#include "oot3d_native_pica_draw_state.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint16_t kVertexBaseRegister = 0x200U;
constexpr uint16_t kVertexFormatLowRegister = 0x201U;
constexpr uint16_t kVertexFormatHighRegister = 0x202U;
constexpr uint16_t kVertexLoaderBeginRegister = 0x203U;
constexpr uint16_t kIndexArrayRegister = 0x227U;
constexpr uint16_t kVertexCountRegister = 0x228U;
constexpr uint16_t kUseGeometryShaderRegister = 0x229U;
constexpr uint16_t kVertexOffsetRegister = 0x22AU;
constexpr uint16_t kTriangleTopologyRegister = 0x25EU;
constexpr uint16_t kTextureMainConfigRegister = 0x080U;
constexpr uint16_t kCullModeRegister = 0x040U;
constexpr uint16_t kViewportHalfWidthRegister = 0x041U;
constexpr uint16_t kViewportHalfHeightRegister = 0x043U;
constexpr uint16_t kViewportDepthRangeRegister = 0x04DU;
constexpr uint16_t kViewportNearPlaneRegister = 0x04EU;
constexpr uint16_t kScissorModeRegister = 0x065U;
constexpr uint16_t kScissorBeginRegister = 0x066U;
constexpr uint16_t kScissorEndRegister = 0x067U;
constexpr uint16_t kViewportCornerRegister = 0x068U;
constexpr uint16_t kDepthMapRegister = 0x06DU;
constexpr uint16_t kFragmentOperationRegister = 0x100U;
constexpr uint16_t kBlendFunctionRegister = 0x101U;
constexpr uint16_t kLogicOperationRegister = 0x102U;
constexpr uint16_t kBlendConstantRegister = 0x103U;
constexpr uint16_t kStencilFunctionRegister = 0x105U;
constexpr uint16_t kStencilOperationRegister = 0x106U;
constexpr uint16_t kDepthColorMaskRegister = 0x107U;
constexpr uint16_t kFramebufferAllowColorWriteRegister = 0x113U;
constexpr uint16_t kFramebufferAllowDepthWriteRegister = 0x115U;
constexpr uint16_t kFramebufferDepthFormatRegister = 0x116U;
constexpr uint16_t kFramebufferColorFormatRegister = 0x117U;
constexpr uint16_t kFramebufferDepthAddressRegister = 0x11CU;
constexpr uint16_t kFramebufferColorAddressRegister = 0x11DU;
constexpr uint16_t kFramebufferDimensionsRegister = 0x11EU;
constexpr uint16_t kVertexShaderInputConfigRegister = 0x2B9U;
constexpr uint16_t kVertexShaderMainOffsetRegister = 0x2BAU;
constexpr uint16_t kVertexShaderInputMapLowRegister = 0x2BBU;
constexpr uint16_t kVertexShaderInputMapHighRegister = 0x2BCU;
constexpr uint16_t kVertexShaderOutputMaskRegister = 0x2BDU;

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool AddAddress(uint32_t base, uint32_t offset, uint32_t& result) {
    if (offset > std::numeric_limits<uint32_t>::max() - base) {
        return false;
    }
    result = base + offset;
    return true;
}

uint32_t DecodeScaledAddress(uint32_t value) {
    return (value & 0x0FFFFFFFU) * 8U;
}

float DecodeFloat24(uint32_t raw) {
    const uint32_t mantissa = raw & 0xFFFFU;
    uint32_t exponent = (raw >> 16U) & 0x7FU;
    const uint32_t sign = (raw >> 23U) << 31U;
    uint32_t ieee = sign;
    if ((raw & 0x7FFFFFU) != 0U) {
        exponent = exponent == 0x7FU ? 0xFFU : exponent + 64U;
        ieee |= mantissa << 7U;
        ieee |= exponent << 23U;
    }
    return std::bit_cast<float>(ieee);
}

int16_t DecodeSigned10(uint32_t value) {
    value &= 0x3FFU;
    return static_cast<int16_t>((value & 0x200U) != 0U
                                    ? static_cast<int32_t>(value) - 0x400
                                    : static_cast<int32_t>(value));
}

int16_t DecodeSigned13(uint32_t value) {
    value &= 0x1FFFU;
    return static_cast<int16_t>((value & 0x1000U) != 0U
                                    ? static_cast<int32_t>(value) - 0x2000
                                    : static_cast<int32_t>(value));
}

std::array<float, 4> DecodeColor(uint32_t value) {
    return {
        static_cast<float>(value & 0xFFU) / 255.0F,
        static_cast<float>((value >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>((value >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((value >> 24U) & 0xFFU) / 255.0F,
    };
}

Oot3dPicaTextureState DecodeTexture(
    const std::array<uint32_t, 0x300>& registers, size_t textureIndex) {
    static constexpr std::array<uint16_t, 3> kTextureConfigRegisters{
        0x081U, 0x091U, 0x099U};
    static constexpr std::array<uint16_t, 3> kTextureFormatRegisters{
        0x08EU, 0x096U, 0x09EU};
    const uint16_t base = kTextureConfigRegisters[textureIndex];
    const uint32_t dimensions = registers[base + 1U];
    const uint32_t configuration = registers[base + 2U];
    const uint32_t lod = registers[base + 3U];
    Oot3dPicaTextureState result;
    result.Enabled =
        (registers[kTextureMainConfigRegister] & (1U << textureIndex)) != 0U;
    result.Height = static_cast<uint16_t>(dimensions & 0x7FFU);
    result.Width = static_cast<uint16_t>((dimensions >> 16U) & 0x7FFU);
    result.PhysicalAddress = DecodeScaledAddress(registers[base + 4U]);
    result.Format =
        static_cast<uint8_t>(registers[kTextureFormatRegisters[textureIndex]] & 0xFU);
    result.MagLinear = (configuration & (1U << 1U)) != 0U;
    result.MinLinear = (configuration & (1U << 2U)) != 0U;
    result.MipLinear = (configuration & (1U << 24U)) != 0U;
    result.WrapT = static_cast<uint8_t>((configuration >> 8U) & 7U);
    result.WrapS = static_cast<uint8_t>((configuration >> 12U) & 7U);
    result.Type = static_cast<uint8_t>((configuration >> 28U) & 7U);
    result.LodBiasRaw = DecodeSigned13(lod);
    result.MaxMipLevel = static_cast<uint8_t>((lod >> 16U) & 0xFU);
    result.MinMipLevel = static_cast<uint8_t>((lod >> 24U) & 0xFU);
    return result;
}

} // namespace

uint8_t Oot3dPicaTextureMipLevelCount(
    const Oot3dPicaTextureState& texture) noexcept {
    if (texture.Width == 0U || texture.Height == 0U) {
        return 0U;
    }
    if (texture.Type == 2U) {
        return 1U;
    }
    uint32_t width = texture.Width;
    uint32_t height = texture.Height;
    uint8_t levels = 1U;
    while (width > 8U && height > 8U) {
        ++levels;
        width >>= 1U;
        height >>= 1U;
    }
    return std::min<uint8_t>(
        levels, static_cast<uint8_t>(texture.MaxMipLevel + 1U));
}

std::optional<size_t> Oot3dPicaTextureMipLevelByteSize(
    const Oot3dPicaTextureState& texture, uint8_t level) noexcept {
    static constexpr std::array<uint8_t, 14> kNibblesPerPixel{
        8, 6, 4, 4, 4, 4, 4, 2, 2, 2, 1, 1, 1, 2};
    if (texture.Width == 0U || texture.Height == 0U || level >= 8U) {
        return std::nullopt;
    }
    if (texture.Type == 2U) {
        if (level != 0U) {
            return std::nullopt;
        }
        const size_t tiledWidth =
            (static_cast<size_t>(texture.Width) + 7U) & ~size_t{7U};
        const size_t tiledHeight =
            (static_cast<size_t>(texture.Height) + 7U) & ~size_t{7U};
        return tiledWidth * tiledHeight * sizeof(uint32_t);
    }
    if (texture.Format >= kNibblesPerPixel.size()) {
        return std::nullopt;
    }
    const size_t width = std::max<size_t>(8U, texture.Width >> level);
    const size_t height = std::max<size_t>(8U, texture.Height >> level);
    const size_t tiledWidth = (width + 7U) & ~size_t{7U};
    const size_t tiledHeight = (height + 7U) & ~size_t{7U};
    const size_t texels = tiledWidth * tiledHeight;
    return (texels * kNibblesPerPixel[texture.Format] + 1U) / 2U;
}

std::optional<size_t> Oot3dPicaTextureMipChainByteSize(
    const Oot3dPicaTextureState& texture) noexcept {
    const uint8_t levels = Oot3dPicaTextureMipLevelCount(texture);
    if (levels == 0U) {
        return std::nullopt;
    }
    size_t total = 0U;
    for (uint8_t level = 0U; level < levels; ++level) {
        const auto bytes = Oot3dPicaTextureMipLevelByteSize(texture, level);
        if (!bytes.has_value() ||
            total > std::numeric_limits<size_t>::max() - *bytes) {
            return std::nullopt;
        }
        total += *bytes;
    }
    return total;
}

bool DecodeOot3dPicaDrawState(const Oot3dPicaDrawPacket& packet,
                              Oot3dPicaDecodedDrawState& state,
                              std::string* error) {
    const auto& registers = packet.Registers;
    Oot3dPicaDecodedDrawState decoded;
    auto& vertex = decoded.VertexInput;
    vertex.PhysicalBaseAddress =
        ((registers[kVertexBaseRegister] >> 1U) & 0x0FFFFFFFU) * 16U;
    vertex.AttributeCount = static_cast<uint8_t>(
        ((registers[kVertexFormatHighRegister] >> 28U) & 0xFU) + 1U);
    const uint32_t attributeMask =
        (registers[kVertexFormatHighRegister] >> 16U) & 0xFFFU;
    for (size_t index = 0; index < vertex.Attributes.size(); ++index) {
        const uint32_t descriptor =
            index < 8U
                ? registers[kVertexFormatLowRegister] >> (index * 4U)
                : registers[kVertexFormatHighRegister] >> ((index - 8U) * 4U);
        auto& attribute = vertex.Attributes[index];
        attribute.Format =
            static_cast<Oot3dPicaVertexFormat>(descriptor & 3U);
        attribute.ComponentCount =
            static_cast<uint8_t>(((descriptor >> 2U) & 3U) + 1U);
        attribute.Default =
            index >= 12U || (attributeMask & (1U << index)) != 0U;
    }
    for (size_t loaderIndex = 0; loaderIndex < vertex.Loaders.size();
         ++loaderIndex) {
        const uint16_t base = static_cast<uint16_t>(
            kVertexLoaderBeginRegister + loaderIndex * 3U);
        auto& loader = vertex.Loaders[loaderIndex];
        loader.DataOffset = registers[base] & 0x0FFFFFFFU;
        if (!AddAddress(vertex.PhysicalBaseAddress, loader.DataOffset,
                        loader.PhysicalAddress)) {
            SetError(error, "PICA vertex loader address overflows");
            return false;
        }
        loader.ByteStride =
            static_cast<uint8_t>((registers[base + 2U] >> 16U) & 0xFFU);
        loader.ComponentCount =
            static_cast<uint8_t>((registers[base + 2U] >> 28U) & 0xFU);
        if (loader.ComponentCount > loader.Components.size()) {
            SetError(error, "PICA vertex loader component count exceeds hardware limit");
            return false;
        }
        const uint64_t componentMap =
            static_cast<uint64_t>(registers[base + 1U]) |
            (static_cast<uint64_t>(registers[base + 2U] & 0xFFFFU) << 32U);
        for (size_t component = 0; component < loader.Components.size();
             ++component) {
            loader.Components[component] =
                static_cast<uint8_t>((componentMap >> (component * 4U)) & 0xFU);
        }
    }
    vertex.Indexed = packet.Indexed;
    vertex.IndicesAre16Bit =
        (registers[kIndexArrayRegister] & 0x80000000U) != 0U;
    if (!AddAddress(vertex.PhysicalBaseAddress,
                    registers[kIndexArrayRegister] & 0x0FFFFFFFU,
                    vertex.IndexPhysicalAddress)) {
        SetError(error, "PICA index buffer address overflows");
        return false;
    }
    vertex.VertexCount = registers[kVertexCountRegister];
    vertex.VertexOffset = registers[kVertexOffsetRegister];

    for (size_t index = 0; index < decoded.Textures.size(); ++index) {
        decoded.Textures[index] = DecodeTexture(registers, index);
    }
    decoded.Texture2UsesCoordinate1 =
        (registers[kTextureMainConfigRegister] & (1U << 13U)) != 0U;

    auto& framebuffer = decoded.Framebuffer;
    framebuffer.ColorPhysicalAddress =
        DecodeScaledAddress(registers[kFramebufferColorAddressRegister]);
    framebuffer.DepthPhysicalAddress =
        DecodeScaledAddress(registers[kFramebufferDepthAddressRegister]);
    const uint32_t dimensions = registers[kFramebufferDimensionsRegister];
    framebuffer.Width = static_cast<uint16_t>(dimensions & 0x7FFU);
    framebuffer.Height =
        static_cast<uint16_t>(((dimensions >> 12U) & 0x3FFU) + 1U);
    framebuffer.Flipped = (dimensions & (1U << 24U)) == 0U;
    framebuffer.ColorFormat = static_cast<uint8_t>(
        (registers[kFramebufferColorFormatRegister] >> 16U) & 7U);
    framebuffer.DepthFormat = static_cast<uint8_t>(
        registers[kFramebufferDepthFormatRegister] & 3U);
    framebuffer.ColorWriteEnabled =
        (registers[kFramebufferAllowColorWriteRegister] & 0xFU) != 0U;
    framebuffer.DepthStencilWriteEnabled =
        (registers[kFramebufferAllowDepthWriteRegister] & 3U) != 0U;
    if (framebuffer.ColorFormat > 4U ||
        (framebuffer.DepthFormat != 0U && framebuffer.DepthFormat != 2U &&
         framebuffer.DepthFormat != 3U)) {
        SetError(error, "PICA framebuffer uses an invalid native format");
        return false;
    }

    const uint8_t cullMode = static_cast<uint8_t>(
        registers[kCullModeRegister] & 3U);
    if (cullMode > 2U) {
        SetError(error, "PICA rasterizer uses an unknown cull mode");
        return false;
    }
    decoded.CullMode = static_cast<Oot3dPicaCullMode>(cullMode);
    auto& viewport = decoded.Viewport;
    viewport.HalfWidth =
        DecodeFloat24(registers[kViewportHalfWidthRegister] & 0xFFFFFFU);
    viewport.HalfHeight =
        DecodeFloat24(registers[kViewportHalfHeightRegister] & 0xFFFFFFU);
    viewport.DepthRange =
        DecodeFloat24(registers[kViewportDepthRangeRegister] & 0xFFFFFFU);
    viewport.NearPlane =
        DecodeFloat24(registers[kViewportNearPlaneRegister] & 0xFFFFFFU);
    viewport.CornerX = DecodeSigned10(registers[kViewportCornerRegister]);
    viewport.CornerY =
        DecodeSigned10(registers[kViewportCornerRegister] >> 16U);
    viewport.ZBuffering = (registers[kDepthMapRegister] & 1U) != 0U;

    auto& scissor = decoded.Scissor;
    const uint8_t scissorMode = static_cast<uint8_t>(
        registers[kScissorModeRegister] & 3U);
    if (scissorMode == 2U) {
        SetError(error, "PICA rasterizer uses an unknown scissor mode");
        return false;
    }
    scissor.Mode = static_cast<Oot3dPicaScissorMode>(scissorMode);
    scissor.X1 = static_cast<uint16_t>(
        registers[kScissorBeginRegister] & 0x3FFU);
    scissor.Y1 = static_cast<uint16_t>(
        (registers[kScissorBeginRegister] >> 16U) & 0x3FFU);
    scissor.X2 = static_cast<uint16_t>(
        registers[kScissorEndRegister] & 0x3FFU);
    scissor.Y2 = static_cast<uint16_t>(
        (registers[kScissorEndRegister] >> 16U) & 0x3FFU);

    auto& output = decoded.OutputMerger;
    const uint32_t fragmentOperation =
        registers[kFragmentOperationRegister];
    output.FragmentOperationMode =
        static_cast<uint8_t>(fragmentOperation & 3U);
    if (output.FragmentOperationMode == 2U) {
        SetError(error, "PICA output merger uses an unknown fragment mode");
        return false;
    }
    output.Blend.Enabled = (fragmentOperation & (1U << 8U)) != 0U;
    const uint32_t blendFunction = registers[kBlendFunctionRegister];
    output.Blend.ColorEquation = static_cast<Oot3dPicaBlendEquation>(
        blendFunction & 7U);
    output.Blend.AlphaEquation = static_cast<Oot3dPicaBlendEquation>(
        (blendFunction >> 8U) & 7U);
    output.Blend.SourceColor = static_cast<Oot3dPicaBlendFactor>(
        (blendFunction >> 16U) & 0xFU);
    output.Blend.DestinationColor = static_cast<Oot3dPicaBlendFactor>(
        (blendFunction >> 20U) & 0xFU);
    output.Blend.SourceAlpha = static_cast<Oot3dPicaBlendFactor>(
        (blendFunction >> 24U) & 0xFU);
    output.Blend.DestinationAlpha = static_cast<Oot3dPicaBlendFactor>(
        (blendFunction >> 28U) & 0xFU);
    if (static_cast<uint8_t>(output.Blend.ColorEquation) > 4U ||
        static_cast<uint8_t>(output.Blend.AlphaEquation) > 4U ||
        static_cast<uint8_t>(output.Blend.SourceColor) > 14U ||
        static_cast<uint8_t>(output.Blend.DestinationColor) > 14U ||
        static_cast<uint8_t>(output.Blend.SourceAlpha) > 14U ||
        static_cast<uint8_t>(output.Blend.DestinationAlpha) > 14U) {
        SetError(error, "PICA output merger uses an invalid blend state");
        return false;
    }
    output.Blend.ConstantColor = DecodeColor(registers[kBlendConstantRegister]);
    output.LogicOperation = static_cast<Oot3dPicaLogicOperation>(
        registers[kLogicOperationRegister] & 0xFU);
    output.ColorWriteMask = framebuffer.ColorWriteEnabled
                                ? static_cast<uint8_t>(
                                      (registers[kDepthColorMaskRegister] >> 8U) &
                                      0xFU)
                                : 0U;

    const uint32_t depthColorMask = registers[kDepthColorMaskRegister];
    const bool requestedDepthTest = (depthColorMask & 1U) != 0U;
    const bool requestedDepthWrite = (depthColorMask & (1U << 12U)) != 0U;
    output.Depth.TestEnabled = requestedDepthTest || requestedDepthWrite;
    output.Depth.WriteEnabled =
        framebuffer.DepthStencilWriteEnabled && requestedDepthWrite;
    output.Depth.Compare =
        requestedDepthTest
            ? static_cast<Oot3dPicaCompareFunction>(
                  (depthColorMask >> 4U) & 7U)
            : Oot3dPicaCompareFunction::Always;

    const uint32_t stencilFunction = registers[kStencilFunctionRegister];
    const uint32_t stencilOperation = registers[kStencilOperationRegister];
    auto& stencil = output.Stencil;
    stencil.Enabled = (stencilFunction & 1U) != 0U &&
                      framebuffer.DepthFormat == 3U;
    stencil.Compare = static_cast<Oot3dPicaCompareFunction>(
        (stencilFunction >> 4U) & 7U);
    stencil.WriteMask = framebuffer.DepthStencilWriteEnabled
                            ? static_cast<uint8_t>(
                                  (stencilFunction >> 8U) & 0xFFU)
                            : 0U;
    stencil.Reference = static_cast<uint8_t>(
        (stencilFunction >> 16U) & 0xFFU);
    stencil.CompareMask = static_cast<uint8_t>(
        (stencilFunction >> 24U) & 0xFFU);
    stencil.Fail = static_cast<Oot3dPicaStencilAction>(
        stencilOperation & 7U);
    stencil.DepthFail = static_cast<Oot3dPicaStencilAction>(
        (stencilOperation >> 4U) & 7U);
    stencil.Pass = static_cast<Oot3dPicaStencilAction>(
        (stencilOperation >> 8U) & 7U);

    auto& shader = decoded.ShaderInterface;
    shader.GeometryShaderEnabled =
        (registers[kUseGeometryShaderRegister] & 3U) == 2U;
    shader.VertexMainOffset = static_cast<uint16_t>(
        registers[kVertexShaderMainOffsetRegister] & 0xFFFFU);
    shader.MaximumInputAttribute = static_cast<uint8_t>(
        registers[kVertexShaderInputConfigRegister] & 0xFU);
    const uint64_t inputMap =
        registers[kVertexShaderInputMapLowRegister] |
        (static_cast<uint64_t>(registers[kVertexShaderInputMapHighRegister])
         << 32U);
    for (size_t index = 0; index < shader.InputRegisterByAttribute.size();
         ++index) {
        shader.InputRegisterByAttribute[index] =
            static_cast<uint8_t>((inputMap >> (index * 4U)) & 0xFU);
    }
    shader.OutputMask = static_cast<uint16_t>(
        registers[kVertexShaderOutputMaskRegister] & 0xFFFFU);
    decoded.Topology = static_cast<Oot3dPicaPrimitiveTopology>(
        (registers[kTriangleTopologyRegister] >> 8U) & 3U);
    state = std::move(decoded);
    return true;
}

} // namespace Oot3dNativeGame
