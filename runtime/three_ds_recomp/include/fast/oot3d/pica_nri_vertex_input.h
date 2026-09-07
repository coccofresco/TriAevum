#pragma once

#include "fast/renderer3ds/pica_vertex_input_layout.h"

namespace Fast::Oot3d {

using ::Fast::Renderer3ds::PicaNriPackedVertexAttribute;
using ::Fast::Renderer3ds::PicaNriPackedVertexBinding;
using ::Fast::Renderer3ds::PicaNriPackedVertexStream;
using ::Fast::Renderer3ds::PicaNriSourceVertexAttribute;
using ::Fast::Renderer3ds::PicaNriSourceVertexBinding;
using ::Fast::Renderer3ds::PicaNriSourceVertexStream;
using ::Fast::Renderer3ds::PicaNriVertexInputLayout;
using ::Fast::Renderer3ds::PicaNriVertexScalar;

PicaNriVertexInputLayout BuildPicaNriVertexInputLayout(
    std::span<const PicaNriSourceVertexBinding> bindings,
    std::span<const PicaNriSourceVertexAttribute> attributes);

bool RepackPicaNriVertexBinding(
    const PicaNriSourceVertexBinding& sourceBinding,
    std::span<const PicaNriPackedVertexAttribute> packedAttributes,
    uint32_t packedStride, std::span<const uint8_t> source,
    std::vector<uint8_t>& packed, std::string* error = nullptr);

bool BuildPicaNriPackedVertexStreams(
    std::span<const PicaNriSourceVertexStream> streams,
    std::span<const PicaNriSourceVertexAttribute> attributes,
    std::vector<PicaNriPackedVertexStream>& packed,
    std::string* error = nullptr);

} // namespace Fast::Oot3d
