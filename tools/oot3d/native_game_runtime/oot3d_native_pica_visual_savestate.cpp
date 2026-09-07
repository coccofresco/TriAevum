#include "oot3d_native_pica_visual_savestate.h"

#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kVisualReplayMagicV1 = 0x31525650U; // PVR1
constexpr uint32_t kVisualReplayMagicV2 = 0x32525650U; // PVR2
constexpr uint32_t kVisualReplayMagicV3 = 0x33525650U; // PVR3
constexpr uint32_t kVisualReplayMagicV4 = 0x34525650U; // PVR4
constexpr uint32_t kVisualReplayMagicV5 = 0x35525650U; // PVR5
constexpr uint32_t kVisualReplayMagicV6 = 0x36525650U; // PVR6
constexpr uint32_t kVisualReplayMagicV7 = 0x37525650U; // PVR7
constexpr uint32_t kVisualReplayMagicV8 = 0x38525650U; // PVR8
constexpr uint32_t kVisualReplayMagicV9 = 0x39525650U; // PVR9
constexpr uint32_t kMaximumPlans = 4096U;
constexpr uint32_t kMaximumResources = 4096U;
constexpr uint32_t kMaximumShaderBytes = 4U << 20U;
constexpr uint32_t kMaximumResourceBytes = 64U << 20U;

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
}

class Writer final {
  public:
    void U8(uint8_t value) { mBytes.push_back(value); }
    void Bool(bool value) { U8(value ? 1U : 0U); }
    void U16(uint16_t value) {
        U8(static_cast<uint8_t>(value));
        U8(static_cast<uint8_t>(value >> 8U));
    }
    void I16(int16_t value) { U16(static_cast<uint16_t>(value)); }
    void U32(uint32_t value) {
        for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
            U8(static_cast<uint8_t>(value >> shift));
        }
    }
    void I32(int32_t value) { U32(static_cast<uint32_t>(value)); }
    void U64(uint64_t value) {
        for (uint32_t shift = 0U; shift < 64U; shift += 8U) {
            U8(static_cast<uint8_t>(value >> shift));
        }
    }
    void Float(float value) { U32(std::bit_cast<uint32_t>(value)); }
    void Double(double value) { U64(std::bit_cast<uint64_t>(value)); }
    void Bytes(std::span<const uint8_t> bytes) {
        U32(static_cast<uint32_t>(bytes.size()));
        mBytes.insert(mBytes.end(), bytes.begin(), bytes.end());
    }
    void String(std::string_view value) {
        Bytes({reinterpret_cast<const uint8_t*>(value.data()),
               value.size()});
    }
    std::vector<uint8_t> Take() { return std::move(mBytes); }

  private:
    std::vector<uint8_t> mBytes;
};

class Reader final {
  public:
    explicit Reader(std::span<const uint8_t> bytes) : mBytes(bytes) {}

    bool U8(uint8_t& value) {
        if (mOffset == mBytes.size()) return false;
        value = mBytes[mOffset++];
        return true;
    }
    bool Bool(bool& value) {
        uint8_t encoded = 0U;
        if (!U8(encoded) || encoded > 1U) return false;
        value = encoded != 0U;
        return true;
    }
    bool U16(uint16_t& value) {
        uint8_t low = 0U;
        uint8_t high = 0U;
        if (!U8(low) || !U8(high)) return false;
        value = static_cast<uint16_t>(low | (uint16_t{high} << 8U));
        return true;
    }
    bool I16(int16_t& value) {
        uint16_t encoded = 0U;
        if (!U16(encoded)) return false;
        value = static_cast<int16_t>(encoded);
        return true;
    }
    bool U32(uint32_t& value) {
        value = 0U;
        for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
            uint8_t byte = 0U;
            if (!U8(byte)) return false;
            value |= static_cast<uint32_t>(byte) << shift;
        }
        return true;
    }
    bool I32(int32_t& value) {
        uint32_t encoded = 0U;
        if (!U32(encoded)) return false;
        value = static_cast<int32_t>(encoded);
        return true;
    }
    bool U64(uint64_t& value) {
        value = 0U;
        for (uint32_t shift = 0U; shift < 64U; shift += 8U) {
            uint8_t byte = 0U;
            if (!U8(byte)) return false;
            value |= static_cast<uint64_t>(byte) << shift;
        }
        return true;
    }
    bool Float(float& value) {
        uint32_t encoded = 0U;
        if (!U32(encoded)) return false;
        value = std::bit_cast<float>(encoded);
        return true;
    }
    bool Double(double& value) {
        uint64_t encoded = 0U;
        if (!U64(encoded)) return false;
        value = std::bit_cast<double>(encoded);
        return true;
    }
    bool Bytes(std::vector<uint8_t>& value, uint32_t maximum) {
        uint32_t size = 0U;
        if (!U32(size) || size > maximum ||
            size > mBytes.size() - mOffset) {
            return false;
        }
        value.assign(mBytes.begin() + mOffset,
                     mBytes.begin() + mOffset + size);
        mOffset += size;
        return true;
    }
    bool String(std::string& value, uint32_t maximum) {
        std::vector<uint8_t> bytes;
        if (!Bytes(bytes, maximum)) return false;
        value.assign(reinterpret_cast<const char*>(bytes.data()),
                     bytes.size());
        return true;
    }
    bool Done() const { return mOffset == mBytes.size(); }

  private:
    std::span<const uint8_t> mBytes;
    size_t mOffset = 0U;
};

template <typename Enum>
void WriteEnum(Writer& writer, Enum value) {
    writer.U8(static_cast<uint8_t>(value));
}

template <typename Enum>
bool ReadEnum(Reader& reader, Enum& value, uint8_t maximum) {
    uint8_t encoded = 0U;
    if (!reader.U8(encoded) || encoded > maximum) return false;
    value = static_cast<Enum>(encoded);
    return true;
}

void WriteTransfer(Writer& writer,
                   const Oot3dPicaDisplayTransferSubmission& value) {
    writer.U64(value.CompletionId);
    writer.U64(value.AfterDrawSubmissionId);
    writer.U32(value.InputPhysicalAddress);
    writer.U32(value.OutputPhysicalAddress);
    writer.U32(value.Transfer.InputAddress);
    writer.U32(value.Transfer.OutputAddress);
    writer.U32(value.Transfer.InputSize);
    writer.U32(value.Transfer.OutputSize);
    writer.U32(value.Transfer.Flags);
    writer.Bool(value.SignalInterrupt);
}

bool ReadTransfer(Reader& reader,
                  Oot3dPicaDisplayTransferSubmission& value) {
    return reader.U64(value.CompletionId) &&
           reader.U64(value.AfterDrawSubmissionId) &&
           reader.U32(value.InputPhysicalAddress) &&
           reader.U32(value.OutputPhysicalAddress) &&
           reader.U32(value.Transfer.InputAddress) &&
           reader.U32(value.Transfer.OutputAddress) &&
           reader.U32(value.Transfer.InputSize) &&
           reader.U32(value.Transfer.OutputSize) &&
           reader.U32(value.Transfer.Flags) &&
           reader.Bool(value.SignalInterrupt);
}

void WriteFill(Writer& writer, const Oot3dPicaMemoryFillSubmission& value) {
    writer.U64(value.CompletionId);
    writer.U64(value.BeforeDrawSubmissionId);
    writer.U32(value.StartPhysicalAddress);
    writer.U32(value.EndPhysicalAddress);
    writer.U32(value.Value);
    writer.U16(value.Control);
    writer.Bool(value.Interrupt.has_value());
    if (value.Interrupt.has_value()) WriteEnum(writer, *value.Interrupt);
}

bool ReadFill(Reader& reader, Oot3dPicaMemoryFillSubmission& value) {
    bool hasInterrupt = false;
    if (!reader.U64(value.CompletionId) ||
        !reader.U64(value.BeforeDrawSubmissionId) ||
        !reader.U32(value.StartPhysicalAddress) ||
        !reader.U32(value.EndPhysicalAddress) ||
        !reader.U32(value.Value) || !reader.U16(value.Control) ||
        !reader.Bool(hasInterrupt)) {
        return false;
    }
    if (hasInterrupt) {
        Oot3dPicaInterruptId interrupt{};
        if (!ReadEnum(reader, interrupt, 6U)) return false;
        value.Interrupt = interrupt;
    }
    return true;
}

void WriteTextureState(Writer& writer, const Oot3dPicaTextureState& value) {
    writer.Bool(value.Enabled);
    writer.U16(value.Width);
    writer.U16(value.Height);
    writer.U32(value.PhysicalAddress);
    writer.U8(value.Format);
    writer.U8(value.Type);
    writer.U8(value.WrapS);
    writer.U8(value.WrapT);
    writer.Bool(value.MinLinear);
    writer.Bool(value.MagLinear);
    writer.Bool(value.MipLinear);
    writer.I16(value.LodBiasRaw);
    writer.U8(value.MinMipLevel);
    writer.U8(value.MaxMipLevel);
}

bool ReadTextureState(Reader& reader, Oot3dPicaTextureState& value,
                      bool extended) {
    if (!(reader.Bool(value.Enabled) && reader.U16(value.Width) &&
           reader.U16(value.Height) && reader.U32(value.PhysicalAddress) &&
           reader.U8(value.Format) && reader.U8(value.Type) &&
           reader.U8(value.WrapS) && reader.U8(value.WrapT) &&
           reader.Bool(value.MinLinear) && reader.Bool(value.MagLinear))) {
        return false;
    }
    return !extended ||
           (reader.Bool(value.MipLinear) && reader.I16(value.LodBiasRaw) &&
            reader.U8(value.MinMipLevel) && reader.U8(value.MaxMipLevel));
}

void WriteDrawState(Writer& writer, const Oot3dPicaDecodedDrawState& value) {
    const auto& vertex = value.VertexInput;
    writer.U32(vertex.PhysicalBaseAddress);
    writer.U8(vertex.AttributeCount);
    for (const auto& attribute : vertex.Attributes) {
        WriteEnum(writer, attribute.Format);
        writer.U8(attribute.ComponentCount);
        writer.Bool(attribute.Default);
    }
    for (const auto& loader : vertex.Loaders) {
        writer.U32(loader.DataOffset);
        writer.U32(loader.PhysicalAddress);
        writer.U8(loader.ByteStride);
        writer.U8(loader.ComponentCount);
        for (const uint8_t component : loader.Components) writer.U8(component);
    }
    writer.Bool(vertex.Indexed);
    writer.Bool(vertex.IndicesAre16Bit);
    writer.U32(vertex.IndexPhysicalAddress);
    writer.U32(vertex.VertexCount);
    writer.U32(vertex.VertexOffset);
    for (const auto& texture : value.Textures) {
        WriteTextureState(writer, texture);
    }
    writer.Bool(value.Texture2UsesCoordinate1);
    const auto& framebuffer = value.Framebuffer;
    writer.U32(framebuffer.ColorPhysicalAddress);
    writer.U32(framebuffer.DepthPhysicalAddress);
    writer.U16(framebuffer.Width);
    writer.U16(framebuffer.Height);
    writer.U8(framebuffer.ColorFormat);
    writer.U8(framebuffer.DepthFormat);
    writer.Bool(framebuffer.Flipped);
    writer.Bool(framebuffer.ColorWriteEnabled);
    writer.Bool(framebuffer.DepthStencilWriteEnabled);
    const auto& viewport = value.Viewport;
    writer.Float(viewport.HalfWidth);
    writer.Float(viewport.HalfHeight);
    writer.Float(viewport.DepthRange);
    writer.Float(viewport.NearPlane);
    writer.I16(viewport.CornerX);
    writer.I16(viewport.CornerY);
    writer.Bool(viewport.ZBuffering);
    WriteEnum(writer, value.Scissor.Mode);
    writer.U16(value.Scissor.X1);
    writer.U16(value.Scissor.Y1);
    writer.U16(value.Scissor.X2);
    writer.U16(value.Scissor.Y2);
    WriteEnum(writer, value.CullMode);
    const auto& output = value.OutputMerger;
    writer.U8(output.FragmentOperationMode);
    writer.U8(output.ColorWriteMask);
    WriteEnum(writer, output.LogicOperation);
    writer.Bool(output.Depth.TestEnabled);
    writer.Bool(output.Depth.WriteEnabled);
    WriteEnum(writer, output.Depth.Compare);
    writer.Bool(output.Stencil.Enabled);
    WriteEnum(writer, output.Stencil.Compare);
    writer.U8(output.Stencil.Reference);
    writer.U8(output.Stencil.CompareMask);
    writer.U8(output.Stencil.WriteMask);
    WriteEnum(writer, output.Stencil.Fail);
    WriteEnum(writer, output.Stencil.DepthFail);
    WriteEnum(writer, output.Stencil.Pass);
    writer.Bool(output.Blend.Enabled);
    WriteEnum(writer, output.Blend.ColorEquation);
    WriteEnum(writer, output.Blend.AlphaEquation);
    WriteEnum(writer, output.Blend.SourceColor);
    WriteEnum(writer, output.Blend.DestinationColor);
    WriteEnum(writer, output.Blend.SourceAlpha);
    WriteEnum(writer, output.Blend.DestinationAlpha);
    for (const float component : output.Blend.ConstantColor) {
        writer.Float(component);
    }
    const auto& shader = value.ShaderInterface;
    writer.Bool(shader.GeometryShaderEnabled);
    writer.U16(shader.VertexMainOffset);
    writer.U8(shader.MaximumInputAttribute);
    for (const uint8_t input : shader.InputRegisterByAttribute) writer.U8(input);
    writer.U16(shader.OutputMask);
    WriteEnum(writer, value.Topology);
}

bool ReadDrawState(Reader& reader, Oot3dPicaDecodedDrawState& value,
                   bool extended) {
    auto& vertex = value.VertexInput;
    if (!reader.U32(vertex.PhysicalBaseAddress) ||
        !reader.U8(vertex.AttributeCount) || vertex.AttributeCount > 16U) {
        return false;
    }
    for (auto& attribute : vertex.Attributes) {
        if (!ReadEnum(reader, attribute.Format, 3U) ||
            !reader.U8(attribute.ComponentCount) ||
            attribute.ComponentCount == 0U || attribute.ComponentCount > 4U ||
            !reader.Bool(attribute.Default)) {
            return false;
        }
    }
    for (auto& loader : vertex.Loaders) {
        if (!reader.U32(loader.DataOffset) ||
            !reader.U32(loader.PhysicalAddress) ||
            !reader.U8(loader.ByteStride) ||
            !reader.U8(loader.ComponentCount) ||
            loader.ComponentCount > loader.Components.size()) {
            return false;
        }
        for (auto& component : loader.Components) {
            if (!reader.U8(component)) return false;
        }
    }
    if (!reader.Bool(vertex.Indexed) ||
        !reader.Bool(vertex.IndicesAre16Bit) ||
        !reader.U32(vertex.IndexPhysicalAddress) ||
        !reader.U32(vertex.VertexCount) ||
        !reader.U32(vertex.VertexOffset)) {
        return false;
    }
    for (auto& texture : value.Textures) {
        if (!ReadTextureState(reader, texture, extended)) return false;
    }
    if (!reader.Bool(value.Texture2UsesCoordinate1)) return false;
    auto& framebuffer = value.Framebuffer;
    if (!reader.U32(framebuffer.ColorPhysicalAddress) ||
        !reader.U32(framebuffer.DepthPhysicalAddress) ||
        !reader.U16(framebuffer.Width) ||
        !reader.U16(framebuffer.Height) ||
        !reader.U8(framebuffer.ColorFormat) ||
        !reader.U8(framebuffer.DepthFormat) ||
        !reader.Bool(framebuffer.Flipped) ||
        !reader.Bool(framebuffer.ColorWriteEnabled) ||
        !reader.Bool(framebuffer.DepthStencilWriteEnabled)) {
        return false;
    }
    auto& viewport = value.Viewport;
    if (!reader.Float(viewport.HalfWidth) ||
        !reader.Float(viewport.HalfHeight) ||
        !reader.Float(viewport.DepthRange) ||
        !reader.Float(viewport.NearPlane) ||
        !reader.I16(viewport.CornerX) || !reader.I16(viewport.CornerY) ||
        !reader.Bool(viewport.ZBuffering) ||
        !ReadEnum(reader, value.Scissor.Mode, 3U) ||
        !reader.U16(value.Scissor.X1) || !reader.U16(value.Scissor.Y1) ||
        !reader.U16(value.Scissor.X2) || !reader.U16(value.Scissor.Y2) ||
        !ReadEnum(reader, value.CullMode, 2U)) {
        return false;
    }
    auto& output = value.OutputMerger;
    if (!reader.U8(output.FragmentOperationMode) ||
        !reader.U8(output.ColorWriteMask) ||
        !ReadEnum(reader, output.LogicOperation, 15U) ||
        !reader.Bool(output.Depth.TestEnabled) ||
        !reader.Bool(output.Depth.WriteEnabled) ||
        !ReadEnum(reader, output.Depth.Compare, 7U) ||
        !reader.Bool(output.Stencil.Enabled) ||
        !ReadEnum(reader, output.Stencil.Compare, 7U) ||
        !reader.U8(output.Stencil.Reference) ||
        !reader.U8(output.Stencil.CompareMask) ||
        !reader.U8(output.Stencil.WriteMask) ||
        !ReadEnum(reader, output.Stencil.Fail, 7U) ||
        !ReadEnum(reader, output.Stencil.DepthFail, 7U) ||
        !ReadEnum(reader, output.Stencil.Pass, 7U) ||
        !reader.Bool(output.Blend.Enabled) ||
        !ReadEnum(reader, output.Blend.ColorEquation, 4U) ||
        !ReadEnum(reader, output.Blend.AlphaEquation, 4U) ||
        !ReadEnum(reader, output.Blend.SourceColor, 14U) ||
        !ReadEnum(reader, output.Blend.DestinationColor, 14U) ||
        !ReadEnum(reader, output.Blend.SourceAlpha, 14U) ||
        !ReadEnum(reader, output.Blend.DestinationAlpha, 14U)) {
        return false;
    }
    for (auto& component : output.Blend.ConstantColor) {
        if (!reader.Float(component)) return false;
    }
    auto& shader = value.ShaderInterface;
    if (!reader.Bool(shader.GeometryShaderEnabled) ||
        !reader.U16(shader.VertexMainOffset) ||
        !reader.U8(shader.MaximumInputAttribute) ||
        shader.MaximumInputAttribute > 15U) {
        return false;
    }
    for (auto& input : shader.InputRegisterByAttribute) {
        if (!reader.U8(input)) return false;
    }
    return reader.U16(shader.OutputMask) &&
           ReadEnum(reader, value.Topology, 3U);
}

void WriteVertexUniforms(Writer& writer,
                         const Oot3dPicaVertexUniformState& value) {
    writer.U32(value.BooleanMask);
    for (const auto& vector : value.Integers) {
        for (const uint32_t component : vector) writer.U32(component);
    }
    for (const auto& vector : value.Floats) {
        for (const float component : vector) writer.Float(component);
    }
}

bool ReadVertexUniforms(Reader& reader,
                        Oot3dPicaVertexUniformState& value) {
    if (!reader.U32(value.BooleanMask)) return false;
    for (auto& vector : value.Integers) {
        for (auto& component : vector) {
            if (!reader.U32(component)) return false;
        }
    }
    for (auto& vector : value.Floats) {
        for (auto& component : vector) {
            if (!reader.Float(component)) return false;
        }
    }
    return true;
}

void WriteFragmentUniforms(Writer& writer,
                           const Oot3dPicaFragmentUniformState& value) {
    for (const auto& vector : value.TevConstants) {
        for (const float component : vector) writer.Float(component);
    }
    for (const float component : value.CombinerBufferColor) {
        writer.Float(component);
    }
    writer.I32(value.AlphaReference);
    for (const float component : value.FogColor) writer.Float(component);
    for (const auto& pair : value.FogLut) {
        writer.Float(pair[0]);
        writer.Float(pair[1]);
    }
    for (const float bias : value.TextureLodBias) writer.Float(bias);
    for (const auto& vectors :
         {&value.Lighting.Specular0, &value.Lighting.Specular1,
          &value.Lighting.Diffuse, &value.Lighting.Ambient,
          &value.Lighting.Position, &value.Lighting.SpotDirection,
          &value.Lighting.Attenuation}) {
        for (const auto& vector : *vectors) {
            for (const float component : vector) writer.Float(component);
        }
    }
    for (const float component : value.Lighting.GlobalAmbient) {
        writer.Float(component);
    }
    writer.I32(value.ShadowTextureBias);
    writer.I32(value.ShadowOrthographic);
    writer.Float(value.ShadowBiasConstant);
    writer.Float(value.ShadowBiasLinear);
}

bool ReadFragmentUniforms(Reader& reader,
                          Oot3dPicaFragmentUniformState& value,
                          bool extended, bool fragmentLighting,
                          bool shadowUniforms) {
    for (auto& vector : value.TevConstants) {
        for (auto& component : vector) {
            if (!reader.Float(component)) return false;
        }
    }
    for (auto& component : value.CombinerBufferColor) {
        if (!reader.Float(component)) return false;
    }
    if (!reader.I32(value.AlphaReference)) return false;
    for (auto& component : value.FogColor) {
        if (!reader.Float(component)) return false;
    }
    for (auto& pair : value.FogLut) {
        if (!reader.Float(pair[0]) || !reader.Float(pair[1])) return false;
    }
    if (!extended) return true;
    for (auto& bias : value.TextureLodBias) {
        if (!reader.Float(bias)) return false;
    }
    if (!fragmentLighting) return true;
    for (auto* vectors : {&value.Lighting.Specular0,
                          &value.Lighting.Specular1,
                          &value.Lighting.Diffuse,
                          &value.Lighting.Ambient,
                          &value.Lighting.Position,
                          &value.Lighting.SpotDirection,
                          &value.Lighting.Attenuation}) {
        for (auto& vector : *vectors) {
            for (auto& component : vector) {
                if (!reader.Float(component)) return false;
            }
        }
    }
    for (auto& component : value.Lighting.GlobalAmbient) {
        if (!reader.Float(component)) return false;
    }
    if (!shadowUniforms) return true;
    if (!reader.I32(value.ShadowTextureBias) ||
        !reader.I32(value.ShadowOrthographic) ||
        !reader.Float(value.ShadowBiasConstant) ||
        !reader.Float(value.ShadowBiasLinear)) {
        return false;
    }
    return true;
}

using LightingLutDictionary = std::map<
    uint64_t, std::shared_ptr<const Oot3dPicaLightingLutState>>;

void CollectLightingLuts(
    const std::vector<Oot3dPicaVulkanDrawPlan>& plans,
    LightingLutDictionary& dictionary) {
    for (const auto& plan : plans) {
        if (plan.LightingLuts == nullptr) continue;
        const auto& lightingLuts = *plan.LightingLuts;
        const uint64_t contentHash =
            ComputeOot3dPicaLightingLutContentHash(lightingLuts);
        if (!lightingLuts.ContentHashAvailable || contentHash == 0U ||
            lightingLuts.ContentHash != contentHash) {
            throw std::runtime_error(
                "native PICA lighting LUT identity is invalid");
        }
        const auto [found, inserted] = dictionary.emplace(
            contentHash, plan.LightingLuts);
        if (!inserted &&
            found->second->PackedEntries != lightingLuts.PackedEntries) {
            throw std::runtime_error(
                "native PICA lighting LUT hash collision");
        }
    }
}

LightingLutDictionary BuildLightingLutDictionary(
    const Oot3dPicaVisualReplayState& value) {
    LightingLutDictionary dictionary;
    CollectLightingLuts(value.Scheduler.Accumulator.PendingDraws, dictionary);
    if (value.PreviousFrame.has_value()) {
        CollectLightingLuts(value.PreviousFrame->Draws, dictionary);
    }
    if (value.LatestFrame.has_value()) {
        CollectLightingLuts(value.LatestFrame->Draws, dictionary);
    }
    if (dictionary.size() > kMaximumResources) {
        throw std::runtime_error(
            "native PICA lighting LUT dictionary is too large");
    }
    return dictionary;
}

void WriteLightingLutDictionary(
    Writer& writer, const LightingLutDictionary& dictionary) {
    writer.U32(static_cast<uint32_t>(dictionary.size()));
    for (const auto& [contentHash, lightingLuts] : dictionary) {
        writer.U64(contentHash);
        for (const uint32_t entry : lightingLuts->PackedEntries) {
            writer.U32(entry);
        }
    }
}

bool ReadLightingLutDictionary(
    Reader& reader, LightingLutDictionary& dictionary) {
    uint32_t count = 0U;
    if (!reader.U32(count) || count > kMaximumResources) return false;
    for (uint32_t index = 0U; index < count; ++index) {
        uint64_t contentHash = 0U;
        auto lightingLuts = std::make_shared<Oot3dPicaLightingLutState>();
        if (!reader.U64(contentHash) || contentHash == 0U) return false;
        for (auto& entry : lightingLuts->PackedEntries) {
            if (!reader.U32(entry)) return false;
        }
        if (ComputeOot3dPicaLightingLutContentHash(*lightingLuts) !=
            contentHash) {
            return false;
        }
        lightingLuts->ContentHash = contentHash;
        lightingLuts->ContentHashAvailable = true;
        if (!dictionary.emplace(contentHash, std::move(lightingLuts)).second) {
            return false;
        }
    }
    return true;
}

void WriteFragmentLightingLayout(
    Writer& writer,
    const Oot3d::Renderer::PicaFragmentLightingLayout& value) {
    writer.Bool(value.Available());
    if (!value.Available()) {
        return;
    }
    writer.U8(value.ActiveLightCount);
    for (const uint8_t nativeIndex : value.LightPermutation) {
        writer.U8(nativeIndex);
    }
    for (const auto& light : value.Lights) {
        writer.Bool(light.Directional);
        writer.Bool(light.TwoSidedDiffuse);
        writer.Bool(light.GeometricFactor0);
        writer.Bool(light.GeometricFactor1);
        writer.Bool(light.ShadowEnabled);
        writer.Bool(light.SpotAttenuationEnabled);
        writer.Bool(light.DistanceAttenuationEnabled);
    }
    for (const auto& sampler : value.LutSamplers) {
        WriteEnum(writer, sampler.Input);
        writer.Float(sampler.Scale);
        writer.Bool(sampler.AbsoluteInput);
    }
    writer.U8(value.EnvironmentConfiguration);
    writer.U8(value.FresnelSelector);
    writer.U8(value.BumpTextureUnit);
    writer.U8(value.ShadowTextureUnit);
    WriteEnum(writer, value.BumpMode);
    writer.Bool(value.ClampHighlights);
    writer.Bool(value.RecalculateBumpVectors);
    writer.Bool(value.ShadowFactorEnabled);
    writer.Bool(value.ShadowPrimary);
    writer.Bool(value.ShadowSecondary);
    writer.Bool(value.ShadowAlpha);
    writer.Bool(value.InvertShadow);
}

bool ReadFragmentLightingLayout(
    Reader& reader,
    Oot3d::Renderer::PicaFragmentLightingLayout& value) {
    bool available = false;
    if (!reader.Bool(available)) {
        return false;
    }
    value = {};
    if (!available) {
        return true;
    }
    value.SchemaVersion =
        Oot3d::Renderer::kPicaFragmentLightingLayoutSchemaVersion;
    if (!reader.U8(value.ActiveLightCount)) {
        return false;
    }
    for (auto& nativeIndex : value.LightPermutation) {
        if (!reader.U8(nativeIndex)) {
            return false;
        }
    }
    for (auto& light : value.Lights) {
        if (!reader.Bool(light.Directional) ||
            !reader.Bool(light.TwoSidedDiffuse) ||
            !reader.Bool(light.GeometricFactor0) ||
            !reader.Bool(light.GeometricFactor1) ||
            !reader.Bool(light.ShadowEnabled) ||
            !reader.Bool(light.SpotAttenuationEnabled) ||
            !reader.Bool(light.DistanceAttenuationEnabled)) {
            return false;
        }
    }
    for (auto& sampler : value.LutSamplers) {
        if (!ReadEnum(reader, sampler.Input, 5U) ||
            !reader.Float(sampler.Scale) ||
            !std::isfinite(sampler.Scale) ||
            !reader.Bool(sampler.AbsoluteInput)) {
            return false;
        }
    }
    if (!reader.U8(value.EnvironmentConfiguration) ||
        !reader.U8(value.FresnelSelector) ||
        !reader.U8(value.BumpTextureUnit) ||
        !reader.U8(value.ShadowTextureUnit) ||
        !ReadEnum(reader, value.BumpMode, 2U) ||
        !reader.Bool(value.ClampHighlights) ||
        !reader.Bool(value.RecalculateBumpVectors) ||
        !reader.Bool(value.ShadowFactorEnabled) ||
        !reader.Bool(value.ShadowPrimary) ||
        !reader.Bool(value.ShadowSecondary) ||
        !reader.Bool(value.ShadowAlpha) ||
        !reader.Bool(value.InvertShadow)) {
        return false;
    }
    return value.Valid();
}

void WritePlan(Writer& writer, const Oot3dPicaVulkanDrawPlan& value,
               const LightingLutDictionary& lightingLuts) {
    writer.U64(value.SubmissionId);
    writer.U32(value.CommandListAddress);
    writer.U32(value.CommandListOffsetWords);
    writer.Bool(value.FragmentFeatures.FragmentLightingEnabled);
    writer.Bool(value.FragmentFeatures.ProceduralTextureEnabled);
    writer.Bool(value.FragmentFeatures.ProceduralTextureReferenced);
    writer.Bool(value.FragmentFeatures.FogEnabled);
    writer.Bool(value.FragmentFeatures.FogFlip);
    writer.Bool(value.FragmentFeatures.GasEnabled);
    writer.U8(value.FragmentFeatures.FogMode);
    writer.U8(value.FragmentFeatures.Texture0Type);
    writer.U8(value.FragmentFeatures.EnabledTextureMask);
    writer.U8(value.FragmentFeatures.ReferencedTextureMask);
    writer.U32(value.FragmentFeatures.UnsupportedFeatureMask);
    WriteFragmentLightingLayout(writer,
                                value.FragmentFeatures.FragmentLighting);
    writer.U64(value.VertexShader.StateKey);
    writer.String(value.ResolvedVertexShaderSource());
    WriteVertexUniforms(writer, value.VertexShader.Uniforms);
    writer.U64(value.FragmentShader.StateKey);
    writer.String(value.ResolvedFragmentShaderSource());
    WriteFragmentUniforms(writer, value.FragmentShader.Uniforms);
    WriteDrawState(writer, value.State);
    writer.U32(static_cast<uint32_t>(value.VertexBindings.size()));
    for (const auto& binding : value.VertexBindings) {
        writer.U8(binding.Binding);
        writer.U16(binding.ByteStride);
        WriteEnum(writer, binding.InputRate);
        writer.Bytes(binding.ResolvedBytes());
        writer.U32(binding.SourcePhysicalAddress);
        writer.U64(binding.ContentVersion);
        writer.Bool(binding.ContentVersionAvailable);
    }
    writer.U32(static_cast<uint32_t>(value.VertexAttributes.size()));
    for (const auto& attribute : value.VertexAttributes) {
        writer.U8(attribute.Location);
        writer.U8(attribute.Binding);
        WriteEnum(writer, attribute.Format);
        writer.U8(attribute.ComponentCount);
        writer.U16(attribute.ByteOffset);
    }
    writer.Bytes(value.ResolvedIndexBytes());
    writer.U32(value.IndexPhysicalAddress);
    writer.U64(value.IndexContentVersion);
    writer.Bool(value.IndexContentVersionAvailable);
    writer.Bool(value.Indexed);
    writer.Bool(value.IndicesAre16Bit);
    writer.I32(value.BaseVertex);
    writer.U32(value.VertexCount);
    writer.U32(static_cast<uint32_t>(value.Textures.size()));
    for (const auto& texture : value.Textures) {
        writer.U8(texture.Slot);
        WriteTextureState(writer, texture.State);
        writer.Bytes(texture.ResolvedNativeBytes());
        writer.U64(texture.NativeContentHash);
        writer.Bool(texture.NativeContentHashAvailable);
    }
    writer.U64(value.GeometryIdentity);
    writer.U64(value.GeometryContentVersion);
    writer.Bool(value.GeometryIdentityAvailable);
    WriteEnum(writer, value.CompositionDomain);
    WriteEnum(writer, value.Composition.Layer);
    WriteEnum(writer, value.Composition.Provenance);
    writer.U32(value.Composition.SourcePc);
    writer.U32(value.Composition.NativeValue);
    writer.Bool(value.LightingLuts != nullptr);
    if (value.LightingLuts != nullptr) {
        const uint64_t contentHash = value.LightingLuts->ContentHash;
        if (!lightingLuts.contains(contentHash)) {
            throw std::runtime_error(
                "native PICA lighting LUT reference is missing");
        }
        writer.U64(contentHash);
    }
}

bool ReadPlan(Reader& reader, Oot3dPicaVulkanDrawPlan& value,
              bool extended, bool typedFeatures,
              bool typedCompositionDomain,
              bool fragmentLighting, bool lightingLutReferences,
              bool shadowUniforms, bool fragmentLightingLayout,
              bool typedCompositionLayer,
              const LightingLutDictionary& lightingLuts) {
    if (!reader.U64(value.SubmissionId) || value.SubmissionId == 0U ||
        !reader.U32(value.CommandListAddress) ||
        !reader.U32(value.CommandListOffsetWords)) {
        return false;
    }
    if (typedFeatures &&
        (!reader.Bool(value.FragmentFeatures.FragmentLightingEnabled) ||
         !reader.Bool(value.FragmentFeatures.ProceduralTextureEnabled) ||
         !reader.Bool(value.FragmentFeatures.ProceduralTextureReferenced) ||
         !reader.Bool(value.FragmentFeatures.FogEnabled) ||
         !reader.Bool(value.FragmentFeatures.FogFlip) ||
         !reader.Bool(value.FragmentFeatures.GasEnabled) ||
         !reader.U8(value.FragmentFeatures.FogMode) ||
         value.FragmentFeatures.FogMode > 7U ||
         !reader.U8(value.FragmentFeatures.Texture0Type) ||
         !reader.U8(value.FragmentFeatures.EnabledTextureMask) ||
         !reader.U8(value.FragmentFeatures.ReferencedTextureMask) ||
         !reader.U32(value.FragmentFeatures.UnsupportedFeatureMask))) {
        return false;
    }
    if (fragmentLightingLayout &&
        !ReadFragmentLightingLayout(
            reader, value.FragmentFeatures.FragmentLighting)) {
        return false;
    }
    if (!reader.U64(value.VertexShader.StateKey) ||
        !reader.String(value.VertexShader.Source, kMaximumShaderBytes) ||
        !ReadVertexUniforms(reader, value.VertexShader.Uniforms) ||
        !reader.U64(value.FragmentShader.StateKey) ||
        !reader.String(value.FragmentShader.Source, kMaximumShaderBytes) ||
        !ReadFragmentUniforms(reader, value.FragmentShader.Uniforms,
                              extended, fragmentLighting, shadowUniforms) ||
        !ReadDrawState(reader, value.State, extended)) {
        return false;
    }
    value.VertexShader.SourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            value.VertexShader.Source);
    value.FragmentShader.SourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(
            value.FragmentShader.Source);
    uint32_t bindingCount = 0U;
    if (!reader.U32(bindingCount) || bindingCount > 16U) return false;
    value.VertexBindings.resize(bindingCount);
    for (auto& binding : value.VertexBindings) {
        if (!reader.U8(binding.Binding) || binding.Binding >= 16U ||
            !reader.U16(binding.ByteStride) ||
            !ReadEnum(reader, binding.InputRate, 1U) ||
            !reader.Bytes(binding.Bytes, kMaximumResourceBytes) ||
            !reader.U32(binding.SourcePhysicalAddress) ||
            !reader.U64(binding.ContentVersion) ||
            !reader.Bool(binding.ContentVersionAvailable)) {
            return false;
        }
    }
    uint32_t attributeCount = 0U;
    if (!reader.U32(attributeCount) || attributeCount > 16U) return false;
    value.VertexAttributes.resize(attributeCount);
    for (auto& attribute : value.VertexAttributes) {
        if (!reader.U8(attribute.Location) || attribute.Location >= 16U ||
            !reader.U8(attribute.Binding) || attribute.Binding >= 16U ||
            !ReadEnum(reader, attribute.Format, 3U) ||
            !reader.U8(attribute.ComponentCount) ||
            attribute.ComponentCount == 0U ||
            attribute.ComponentCount > 4U ||
            !reader.U16(attribute.ByteOffset)) {
            return false;
        }
    }
    if (!reader.Bytes(value.IndexBytes, kMaximumResourceBytes) ||
        !reader.U32(value.IndexPhysicalAddress) ||
        !reader.U64(value.IndexContentVersion) ||
        !reader.Bool(value.IndexContentVersionAvailable) ||
        !reader.Bool(value.Indexed) ||
        !reader.Bool(value.IndicesAre16Bit) ||
        !reader.I32(value.BaseVertex) || !reader.U32(value.VertexCount)) {
        return false;
    }
    uint32_t textureCount = 0U;
    if (!reader.U32(textureCount) || textureCount > 3U) return false;
    value.Textures.resize(textureCount);
    for (auto& texture : value.Textures) {
        if (!reader.U8(texture.Slot) || texture.Slot >= 3U ||
            !ReadTextureState(reader, texture.State, extended) ||
            !reader.Bytes(texture.NativeBytes, kMaximumResourceBytes) ||
            !reader.U64(texture.NativeContentHash) ||
            !reader.Bool(texture.NativeContentHashAvailable) ||
            !ResolveOot3dPicaTextureContentIdentity(texture)) {
            return false;
        }
    }
    if (!reader.U64(value.GeometryIdentity) ||
        !reader.U64(value.GeometryContentVersion) ||
        !reader.Bool(value.GeometryIdentityAvailable)) {
        return false;
    }
    if (!typedCompositionDomain) {
        value.CompositionDomain = Oot3dPicaCompositionDomain::Unknown;
    } else if (!ReadEnum(reader, value.CompositionDomain, 2U)) {
        return false;
    }
    if (!typedCompositionLayer) {
        value.Composition = {};
    } else if (!ReadEnum(reader, value.Composition.Layer, 4U) ||
               !ReadEnum(reader, value.Composition.Provenance, 3U) ||
               !reader.U32(value.Composition.SourcePc) ||
               !reader.U32(value.Composition.NativeValue)) {
        return false;
    }
    if (!lightingLutReferences) return true;
    bool hasLightingLuts = false;
    if (!reader.Bool(hasLightingLuts)) return false;
    if (!hasLightingLuts) return true;
    uint64_t contentHash = 0U;
    if (!reader.U64(contentHash)) return false;
    const auto found = lightingLuts.find(contentHash);
    if (found == lightingLuts.end()) return false;
    value.LightingLuts = found->second;
    return true;
}

void WritePlans(Writer& writer,
                const std::vector<Oot3dPicaVulkanDrawPlan>& values,
                const LightingLutDictionary& lightingLuts) {
    writer.U32(static_cast<uint32_t>(values.size()));
    for (const auto& value : values) {
        WritePlan(writer, value, lightingLuts);
    }
}

bool ReadPlans(Reader& reader,
               std::vector<Oot3dPicaVulkanDrawPlan>& values,
               bool extended, bool typedFeatures,
               bool typedCompositionDomain,
               bool fragmentLighting, bool lightingLutReferences,
               bool shadowUniforms, bool fragmentLightingLayout,
               bool typedCompositionLayer,
               const LightingLutDictionary& lightingLuts) {
    uint32_t count = 0U;
    if (!reader.U32(count) || count > kMaximumPlans) return false;
    values.resize(count);
    for (auto& value : values) {
        if (!ReadPlan(reader, value, extended, typedFeatures,
                      typedCompositionDomain, fragmentLighting,
                      lightingLutReferences, shadowUniforms,
                      fragmentLightingLayout, typedCompositionLayer,
                      lightingLuts)) return false;
    }
    return true;
}

void WriteFills(Writer& writer,
                const std::vector<Oot3dPicaMemoryFillSubmission>& values) {
    writer.U32(static_cast<uint32_t>(values.size()));
    for (const auto& value : values) WriteFill(writer, value);
}

bool ReadFills(Reader& reader,
               std::vector<Oot3dPicaMemoryFillSubmission>& values) {
    uint32_t count = 0U;
    if (!reader.U32(count) || count > kMaximumResources) return false;
    values.resize(count);
    for (auto& value : values) {
        if (!ReadFill(reader, value)) return false;
    }
    return true;
}

void WriteTransfers(
    Writer& writer,
    const std::vector<Oot3dPicaDisplayTransferSubmission>& values) {
    writer.U32(static_cast<uint32_t>(values.size()));
    for (const auto& value : values) WriteTransfer(writer, value);
}

bool ReadTransfers(
    Reader& reader,
    std::vector<Oot3dPicaDisplayTransferSubmission>& values) {
    uint32_t count = 0U;
    if (!reader.U32(count) || count > kMaximumResources) return false;
    values.resize(count);
    for (auto& value : values) {
        if (!ReadTransfer(reader, value)) return false;
    }
    return true;
}

void WriteFrame(Writer& writer, const Oot3dPicaVisualFrame& value,
                const LightingLutDictionary& lightingLuts) {
    writer.U64(value.Sequence);
    WriteTransfer(writer, value.TopTransfer);
    WritePlans(writer, value.Draws, lightingLuts);
    writer.U32(static_cast<uint32_t>(value.StrictDrawIdentities.size()));
    for (const uint64_t identity : value.StrictDrawIdentities) {
        writer.U64(identity);
    }
    WriteFills(writer, value.MemoryFills);
    WriteTransfers(writer, value.DisplayTransfers);
}

bool ReadFrame(Reader& reader, Oot3dPicaVisualFrame& value,
               bool extended, bool typedFeatures,
               bool typedCompositionDomain,
               bool fragmentLighting, bool lightingLutReferences,
               bool shadowUniforms, bool fragmentLightingLayout,
               bool typedCompositionLayer,
               const LightingLutDictionary& lightingLuts) {
    if (!reader.U64(value.Sequence) || value.Sequence == 0U ||
        !ReadTransfer(reader, value.TopTransfer) ||
        !ReadPlans(reader, value.Draws, extended, typedFeatures,
                   typedCompositionDomain, fragmentLighting,
                   lightingLutReferences, shadowUniforms,
                   fragmentLightingLayout, typedCompositionLayer,
                   lightingLuts)) {
        return false;
    }
    uint32_t identityCount = 0U;
    if (!reader.U32(identityCount) || identityCount != value.Draws.size()) {
        return false;
    }
    value.StrictDrawIdentities.resize(identityCount);
    for (auto& identity : value.StrictDrawIdentities) {
        if (!reader.U64(identity)) return false;
    }
    return ReadFills(reader, value.MemoryFills) &&
           ReadTransfers(reader, value.DisplayTransfers);
}

void WriteOptionalFrame(Writer& writer,
                        const std::optional<Oot3dPicaVisualFrame>& value,
                        const LightingLutDictionary& lightingLuts) {
    writer.Bool(value.has_value());
    if (value.has_value()) WriteFrame(writer, *value, lightingLuts);
}

bool ReadOptionalFrame(Reader& reader,
                       std::optional<Oot3dPicaVisualFrame>& value,
                       bool extended, bool typedFeatures,
                       bool typedCompositionDomain, bool fragmentLighting,
                       bool lightingLutReferences, bool shadowUniforms,
                       bool fragmentLightingLayout,
                       bool typedCompositionLayer,
                       const LightingLutDictionary& lightingLuts) {
    bool available = false;
    if (!reader.Bool(available)) return false;
    if (!available) {
        value.reset();
        return true;
    }
    Oot3dPicaVisualFrame frame;
    if (!ReadFrame(reader, frame, extended, typedFeatures,
                   typedCompositionDomain, fragmentLighting,
                   lightingLutReferences, shadowUniforms,
                   fragmentLightingLayout, typedCompositionLayer,
                   lightingLuts)) return false;
    value = std::move(frame);
    return true;
}

void WriteReplayState(Writer& writer,
                      const Oot3dPicaVisualReplayState& value) {
    const auto lightingLuts = BuildLightingLutDictionary(value);
    writer.U32(kVisualReplayMagicV9);
    WriteLightingLutDictionary(writer, lightingLuts);
    const auto& accumulator = value.Scheduler.Accumulator;
    writer.U64(accumulator.NextSequence);
    WritePlans(writer, accumulator.PendingDraws, lightingLuts);
    WriteFills(writer, accumulator.PendingMemoryFills);
    WriteTransfers(writer, accumulator.PendingDisplayTransfers);
    writer.U32(static_cast<uint32_t>(
        value.Scheduler.SnapshotCompletions.size()));
    for (const auto& [target, completion] :
         value.Scheduler.SnapshotCompletions) {
        writer.U64(target.first);
        writer.U32(target.second);
        writer.U64(completion);
    }
    writer.U32(static_cast<uint32_t>(
        value.Continuity.AcceptedDeltas.size()));
    for (const double delta : value.Continuity.AcceptedDeltas) {
        writer.Double(delta);
    }
    WriteOptionalFrame(writer, value.PreviousFrame, lightingLuts);
    WriteOptionalFrame(writer, value.LatestFrame, lightingLuts);
    writer.Bool(value.LatestTransitionContinuous);
    writer.U32(static_cast<uint32_t>(
        value.DisplayTransfersByOutput.size()));
    for (const auto& [output, transfer] : value.DisplayTransfersByOutput) {
        writer.U32(output);
        WriteTransfer(writer, transfer);
    }
    writer.U64(value.LastSelectedTopTransferCompletionId);
    writer.U64(value.LastSubmittedDrawId);
}

bool ReadReplayState(Reader& reader, Oot3dPicaVisualReplayState& value) {
    uint32_t magic = 0U;
    auto& accumulator = value.Scheduler.Accumulator;
    if (!reader.U32(magic) ||
        (magic != kVisualReplayMagicV1 &&
         magic != kVisualReplayMagicV2 &&
         magic != kVisualReplayMagicV3 &&
         magic != kVisualReplayMagicV4 &&
         magic != kVisualReplayMagicV5 &&
         magic != kVisualReplayMagicV6 &&
         magic != kVisualReplayMagicV7 &&
         magic != kVisualReplayMagicV8 &&
         magic != kVisualReplayMagicV9)) {
        return false;
    }
    const bool extended = magic != kVisualReplayMagicV1;
    const bool typedFeatures =
        magic == kVisualReplayMagicV3 || magic == kVisualReplayMagicV4 ||
        magic == kVisualReplayMagicV5 || magic == kVisualReplayMagicV6 ||
        magic == kVisualReplayMagicV7 || magic == kVisualReplayMagicV8 ||
        magic == kVisualReplayMagicV9;
    const bool typedCompositionDomain =
        magic == kVisualReplayMagicV4 || magic == kVisualReplayMagicV5 ||
        magic == kVisualReplayMagicV6 || magic == kVisualReplayMagicV7 ||
        magic == kVisualReplayMagicV8 || magic == kVisualReplayMagicV9;
    const bool fragmentLighting =
        magic == kVisualReplayMagicV5 || magic == kVisualReplayMagicV6 ||
        magic == kVisualReplayMagicV7 || magic == kVisualReplayMagicV8 ||
        magic == kVisualReplayMagicV9;
    const bool lightingLutReferences =
        magic == kVisualReplayMagicV6 || magic == kVisualReplayMagicV7 ||
        magic == kVisualReplayMagicV8 || magic == kVisualReplayMagicV9;
    const bool shadowUniforms =
        magic == kVisualReplayMagicV7 || magic == kVisualReplayMagicV8 ||
        magic == kVisualReplayMagicV9;
    const bool fragmentLightingLayout =
        magic == kVisualReplayMagicV8 || magic == kVisualReplayMagicV9;
    const bool typedCompositionLayer = magic == kVisualReplayMagicV9;
    LightingLutDictionary lightingLuts;
    if (lightingLutReferences &&
        !ReadLightingLutDictionary(reader, lightingLuts)) {
        return false;
    }
    if (
        !reader.U64(accumulator.NextSequence) ||
        accumulator.NextSequence == 0U ||
        !ReadPlans(reader, accumulator.PendingDraws, extended,
                   typedFeatures, typedCompositionDomain, fragmentLighting,
                   lightingLutReferences, shadowUniforms,
                   fragmentLightingLayout, typedCompositionLayer,
                   lightingLuts) ||
        !ReadFills(reader, accumulator.PendingMemoryFills) ||
        !ReadTransfers(reader, accumulator.PendingDisplayTransfers)) {
        return false;
    }
    uint32_t snapshotCount = 0U;
    if (!reader.U32(snapshotCount) || snapshotCount > kMaximumResources) {
        return false;
    }
    for (uint32_t index = 0U; index < snapshotCount; ++index) {
        uint64_t nameSpace = 0U;
        uint32_t address = 0U;
        uint64_t completion = 0U;
        if (!reader.U64(nameSpace) || !reader.U32(address) ||
            !reader.U64(completion) || completion == 0U ||
            !value.Scheduler.SnapshotCompletions
                 .emplace(std::pair{nameSpace, address}, completion)
                 .second) {
            return false;
        }
    }
    uint32_t deltaCount = 0U;
    if (!reader.U32(deltaCount) || deltaCount > 31U) return false;
    value.Continuity.AcceptedDeltas.resize(deltaCount);
    for (auto& delta : value.Continuity.AcceptedDeltas) {
        if (!reader.Double(delta) || !std::isfinite(delta) || delta < 0.0) {
            return false;
        }
    }
    if (!ReadOptionalFrame(reader, value.PreviousFrame, extended,
                           typedFeatures, typedCompositionDomain,
                           fragmentLighting, lightingLutReferences,
                           shadowUniforms, fragmentLightingLayout,
                           typedCompositionLayer,
                           lightingLuts) ||
        !ReadOptionalFrame(reader, value.LatestFrame, extended,
                           typedFeatures, typedCompositionDomain,
                           fragmentLighting, lightingLutReferences,
                           shadowUniforms, fragmentLightingLayout,
                           typedCompositionLayer,
                           lightingLuts) ||
        !reader.Bool(value.LatestTransitionContinuous)) {
        return false;
    }
    if (value.LatestTransitionContinuous &&
        (!value.PreviousFrame.has_value() ||
         !value.LatestFrame.has_value())) {
        return false;
    }
    uint32_t displayCount = 0U;
    if (!reader.U32(displayCount) || displayCount > kMaximumResources) {
        return false;
    }
    for (uint32_t index = 0U; index < displayCount; ++index) {
        uint32_t output = 0U;
        Oot3dPicaDisplayTransferSubmission transfer;
        if (!reader.U32(output) || output == 0U ||
            !ReadTransfer(reader, transfer) ||
            transfer.Transfer.OutputAddress != output ||
            !value.DisplayTransfersByOutput.emplace(output, transfer).second) {
            return false;
        }
    }
    return reader.U64(value.LastSelectedTopTransferCompletionId) &&
           reader.U64(value.LastSubmittedDrawId);
}

} // namespace

bool EncodeOot3dPicaVisualReplayState(
    const Oot3dPicaVisualReplayState& state,
    std::vector<uint8_t>& output, std::string* error) {
    try {
        Writer writer;
        WriteReplayState(writer, state);
        output = writer.Take();
        if (output.empty()) {
            SetError(error, "native PICA visual replay state is empty");
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        output.clear();
        SetError(error, exception.what());
        return false;
    }
}

bool DecodeOot3dPicaVisualReplayState(
    std::span<const uint8_t> bytes,
    Oot3dPicaVisualReplayState& state, std::string* error) {
    Oot3dPicaVisualReplayState decoded;
    Reader reader(bytes);
    if (!ReadReplayState(reader, decoded) || !reader.Done()) {
        SetError(error, "native PICA visual replay state is incompatible");
        return false;
    }
    state = std::move(decoded);
    return true;
}

} // namespace Oot3dNativeGame
