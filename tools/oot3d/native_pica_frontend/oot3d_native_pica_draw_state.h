#pragma once

#include "oot3d_native_pica_frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Oot3dNativeGame {

enum class Oot3dPicaVertexFormat : uint8_t {
    SignedByte = 0,
    UnsignedByte = 1,
    SignedShort = 2,
    Float = 3,
};

enum class Oot3dPicaPrimitiveTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip = 1,
    TriangleFan = 2,
    GeometryShader = 3,
};

enum class Oot3dPicaCullMode : uint8_t {
    KeepAll = 0,
    KeepClockwise = 1,
    KeepCounterClockwise = 2,
};

enum class Oot3dPicaScissorMode : uint8_t {
    Disabled = 0,
    Exclude = 1,
    Include = 3,
};

enum class Oot3dPicaCompareFunction : uint8_t {
    Never = 0,
    Always = 1,
    Equal = 2,
    NotEqual = 3,
    Less = 4,
    LessOrEqual = 5,
    Greater = 6,
    GreaterOrEqual = 7,
};

enum class Oot3dPicaStencilAction : uint8_t {
    Keep = 0,
    Zero = 1,
    Replace = 2,
    Increment = 3,
    Decrement = 4,
    Invert = 5,
    IncrementWrap = 6,
    DecrementWrap = 7,
};

enum class Oot3dPicaBlendEquation : uint8_t {
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Minimum = 3,
    Maximum = 4,
};

enum class Oot3dPicaBlendFactor : uint8_t {
    Zero = 0,
    One = 1,
    SourceColor = 2,
    OneMinusSourceColor = 3,
    DestinationColor = 4,
    OneMinusDestinationColor = 5,
    SourceAlpha = 6,
    OneMinusSourceAlpha = 7,
    DestinationAlpha = 8,
    OneMinusDestinationAlpha = 9,
    ConstantColor = 10,
    OneMinusConstantColor = 11,
    ConstantAlpha = 12,
    OneMinusConstantAlpha = 13,
    SourceAlphaSaturate = 14,
};

enum class Oot3dPicaLogicOperation : uint8_t {
    Clear = 0,
    And = 1,
    AndReverse = 2,
    Copy = 3,
    Set = 4,
    CopyInverted = 5,
    NoOp = 6,
    Invert = 7,
    Nand = 8,
    Or = 9,
    Nor = 10,
    Xor = 11,
    Equivalent = 12,
    AndInverted = 13,
    OrReverse = 14,
    OrInverted = 15,
};

struct Oot3dPicaVertexAttributeState {
    Oot3dPicaVertexFormat Format = Oot3dPicaVertexFormat::SignedByte;
    uint8_t ComponentCount = 1;
    bool Default = true;
};

struct Oot3dPicaVertexLoaderState {
    uint32_t DataOffset = 0;
    uint32_t PhysicalAddress = 0;
    uint8_t ByteStride = 0;
    uint8_t ComponentCount = 0;
    std::array<uint8_t, 12> Components{};
};

struct Oot3dPicaVertexInputState {
    uint32_t PhysicalBaseAddress = 0;
    uint8_t AttributeCount = 0;
    std::array<Oot3dPicaVertexAttributeState, 16> Attributes{};
    std::array<Oot3dPicaVertexLoaderState, 12> Loaders{};
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    uint32_t IndexPhysicalAddress = 0;
    uint32_t VertexCount = 0;
    uint32_t VertexOffset = 0;
};

struct Oot3dPicaTextureState {
    bool Enabled = false;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint32_t PhysicalAddress = 0;
    uint8_t Format = 0;
    uint8_t Type = 0;
    uint8_t WrapS = 0;
    uint8_t WrapT = 0;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
};

uint8_t Oot3dPicaTextureMipLevelCount(
    const Oot3dPicaTextureState& texture) noexcept;

std::optional<size_t> Oot3dPicaTextureMipLevelByteSize(
    const Oot3dPicaTextureState& texture, uint8_t level) noexcept;

std::optional<size_t> Oot3dPicaTextureMipChainByteSize(
    const Oot3dPicaTextureState& texture) noexcept;

struct Oot3dPicaFramebufferState {
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t ColorFormat = 0;
    uint8_t DepthFormat = 0;
    bool Flipped = false;
    bool ColorWriteEnabled = false;
    bool DepthStencilWriteEnabled = false;
};

struct Oot3dPicaViewportState {
    float HalfWidth = 0.0F;
    float HalfHeight = 0.0F;
    float DepthRange = 0.0F;
    float NearPlane = 0.0F;
    int16_t CornerX = 0;
    int16_t CornerY = 0;
    bool ZBuffering = false;
};

struct Oot3dPicaScissorState {
    Oot3dPicaScissorMode Mode = Oot3dPicaScissorMode::Disabled;
    uint16_t X1 = 0;
    uint16_t Y1 = 0;
    uint16_t X2 = 0;
    uint16_t Y2 = 0;
};

struct Oot3dPicaStencilState {
    bool Enabled = false;
    Oot3dPicaCompareFunction Compare = Oot3dPicaCompareFunction::Always;
    uint8_t Reference = 0;
    uint8_t CompareMask = 0;
    uint8_t WriteMask = 0;
    Oot3dPicaStencilAction Fail = Oot3dPicaStencilAction::Keep;
    Oot3dPicaStencilAction DepthFail = Oot3dPicaStencilAction::Keep;
    Oot3dPicaStencilAction Pass = Oot3dPicaStencilAction::Keep;
};

struct Oot3dPicaDepthState {
    bool TestEnabled = false;
    bool WriteEnabled = false;
    Oot3dPicaCompareFunction Compare = Oot3dPicaCompareFunction::Always;
};

struct Oot3dPicaBlendState {
    bool Enabled = false;
    Oot3dPicaBlendEquation ColorEquation = Oot3dPicaBlendEquation::Add;
    Oot3dPicaBlendEquation AlphaEquation = Oot3dPicaBlendEquation::Add;
    Oot3dPicaBlendFactor SourceColor = Oot3dPicaBlendFactor::One;
    Oot3dPicaBlendFactor DestinationColor = Oot3dPicaBlendFactor::Zero;
    Oot3dPicaBlendFactor SourceAlpha = Oot3dPicaBlendFactor::One;
    Oot3dPicaBlendFactor DestinationAlpha = Oot3dPicaBlendFactor::Zero;
    std::array<float, 4> ConstantColor{};
};

struct Oot3dPicaOutputMergerState {
    uint8_t FragmentOperationMode = 0;
    uint8_t ColorWriteMask = 0;
    Oot3dPicaLogicOperation LogicOperation = Oot3dPicaLogicOperation::Copy;
    Oot3dPicaDepthState Depth;
    Oot3dPicaStencilState Stencil;
    Oot3dPicaBlendState Blend;
};

struct Oot3dPicaShaderInterfaceState {
    bool GeometryShaderEnabled = false;
    uint16_t VertexMainOffset = 0;
    uint8_t MaximumInputAttribute = 0;
    std::array<uint8_t, 16> InputRegisterByAttribute{};
    uint16_t OutputMask = 0;
};

struct Oot3dPicaDecodedDrawState {
    Oot3dPicaVertexInputState VertexInput;
    std::array<Oot3dPicaTextureState, 3> Textures{};
    bool Texture2UsesCoordinate1 = false;
    Oot3dPicaFramebufferState Framebuffer;
    Oot3dPicaViewportState Viewport;
    Oot3dPicaScissorState Scissor;
    Oot3dPicaCullMode CullMode = Oot3dPicaCullMode::KeepAll;
    Oot3dPicaOutputMergerState OutputMerger;
    Oot3dPicaShaderInterfaceState ShaderInterface;
    Oot3dPicaPrimitiveTopology Topology =
        Oot3dPicaPrimitiveTopology::TriangleList;
};

bool DecodeOot3dPicaDrawState(const Oot3dPicaDrawPacket& packet,
                              Oot3dPicaDecodedDrawState& state,
                              std::string* error = nullptr);

} // namespace Oot3dNativeGame
