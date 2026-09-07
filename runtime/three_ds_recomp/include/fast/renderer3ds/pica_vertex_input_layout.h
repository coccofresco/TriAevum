#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fast::Renderer3ds {

enum class PicaNriVertexScalar : uint8_t {
    SignedByte,
    UnsignedByte,
    SignedShort,
    Float,
};

struct PicaNriSourceVertexBinding {
    uint32_t Binding = 0;
    uint32_t ByteStride = 0;
    bool PerInstance = false;
};

struct PicaNriSourceVertexAttribute {
    uint32_t Location = 0;
    uint32_t Binding = 0;
    PicaNriVertexScalar Scalar = PicaNriVertexScalar::Float;
    uint8_t ComponentCount = 0;
    uint32_t ByteOffset = 0;
};

struct PicaNriPackedVertexBinding {
    uint32_t Binding = 0;
    uint32_t ByteStride = 0;
    bool PerInstance = false;
};

struct PicaNriPackedVertexAttribute {
    uint32_t Location = 0;
    uint32_t Binding = 0;
    uint32_t ByteOffset = 0;
    PicaNriVertexScalar SourceScalar = PicaNriVertexScalar::Float;
    uint8_t SourceComponentCount = 0;
    uint32_t SourceByteOffset = 0;
};

struct PicaNriSourceVertexStream {
    PicaNriSourceVertexBinding Binding;
    std::span<const uint8_t> Bytes;
};

struct PicaNriPackedVertexStream {
    uint32_t Binding = 0;
    uint32_t ByteStride = 0;
    std::vector<uint8_t> Bytes;
};

struct PicaNriVertexInputLayout {
    std::vector<PicaNriPackedVertexBinding> Bindings;
    std::vector<PicaNriPackedVertexAttribute> Attributes;
    std::string Error;

    [[nodiscard]] bool Valid() const { return Error.empty(); }
};

} // namespace Fast::Renderer3ds
