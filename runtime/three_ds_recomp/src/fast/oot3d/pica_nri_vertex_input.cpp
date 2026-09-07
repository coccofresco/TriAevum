#include "fast/oot3d/pica_nri_vertex_input.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace Fast::Oot3d {
namespace {

uint32_t ScalarSize(PicaNriVertexScalar scalar) {
    switch (scalar) {
        case PicaNriVertexScalar::SignedByte:
        case PicaNriVertexScalar::UnsignedByte:
            return 1U;
        case PicaNriVertexScalar::SignedShort:
            return 2U;
        case PicaNriVertexScalar::Float:
            return 4U;
    }
    return 0U;
}

float DecodeScalar(PicaNriVertexScalar scalar, const uint8_t* source) {
    switch (scalar) {
        case PicaNriVertexScalar::SignedByte: {
            int8_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return static_cast<float>(value);
        }
        case PicaNriVertexScalar::UnsignedByte:
            return static_cast<float>(*source);
        case PicaNriVertexScalar::SignedShort: {
            int16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return static_cast<float>(value);
        }
        case PicaNriVertexScalar::Float: {
            float value = 0.0F;
            std::memcpy(&value, source, sizeof(value));
            return value;
        }
    }
    return 0.0F;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
}

} // namespace

PicaNriVertexInputLayout BuildPicaNriVertexInputLayout(
    std::span<const PicaNriSourceVertexBinding> bindings,
    std::span<const PicaNriSourceVertexAttribute> attributes) {
    PicaNriVertexInputLayout result;
    std::set<uint32_t> bindingIds;
    for (const auto& binding : bindings) {
        if (binding.ByteStride == 0U ||
            !bindingIds.insert(binding.Binding).second) {
            result.Error = "invalid or duplicate PICA vertex binding";
            return result;
        }
    }

    std::set<uint32_t> locations;
    result.Attributes.reserve(attributes.size());
    for (const auto& attribute : attributes) {
        const auto binding = std::find_if(
            bindings.begin(), bindings.end(),
            [&](const auto& candidate) {
                return candidate.Binding == attribute.Binding;
            });
        const uint32_t scalarSize = ScalarSize(attribute.Scalar);
        if (binding == bindings.end() || attribute.ComponentCount == 0U ||
            attribute.ComponentCount > 4U || scalarSize == 0U ||
            attribute.ByteOffset +
                    scalarSize * attribute.ComponentCount >
                binding->ByteStride ||
            !locations.insert(attribute.Location).second) {
            result.Error = "invalid PICA vertex attribute";
            return result;
        }
    }

    for (const auto& binding : bindings) {
        std::vector<PicaNriSourceVertexAttribute> local;
        for (const auto& attribute : attributes)
            if (attribute.Binding == binding.Binding)
                local.push_back(attribute);
        if (local.empty())
            continue;
        std::sort(local.begin(), local.end(),
                  [](const auto& left, const auto& right) {
                      return left.Location < right.Location;
                  });
        const uint32_t packedStride =
            static_cast<uint32_t>(local.size()) * 4U * sizeof(float);
        result.Bindings.push_back(
            {binding.Binding, packedStride, binding.PerInstance});
        for (size_t index = 0; index < local.size(); ++index) {
            const auto& attribute = local[index];
            result.Attributes.push_back(
                {attribute.Location, attribute.Binding,
                 static_cast<uint32_t>(index) * 4U *
                     static_cast<uint32_t>(sizeof(float)),
                 attribute.Scalar, attribute.ComponentCount,
                 attribute.ByteOffset});
        }
    }
    if (result.Attributes.size() != attributes.size()) {
        result.Error = "PICA vertex layout lost an attribute";
        result.Bindings.clear();
        result.Attributes.clear();
    }
    return result;
}

bool RepackPicaNriVertexBinding(
    const PicaNriSourceVertexBinding& sourceBinding,
    std::span<const PicaNriPackedVertexAttribute> packedAttributes,
    uint32_t packedStride, std::span<const uint8_t> source,
    std::vector<uint8_t>& packed, std::string* error) {
    packed.clear();
    if (sourceBinding.ByteStride == 0U || packedStride == 0U ||
        source.empty() ||
        source.size() % sourceBinding.ByteStride != 0U) {
        SetError(error, "invalid PICA vertex binding byte range");
        return false;
    }
    for (const auto& attribute : packedAttributes) {
        if (attribute.Binding != sourceBinding.Binding ||
            attribute.ByteOffset + 4U * sizeof(float) > packedStride) {
            SetError(error, "packed PICA vertex attribute is out of bounds");
            return false;
        }
    }

    const size_t elementCount =
        source.size() / sourceBinding.ByteStride;
    packed.resize(elementCount * packedStride);
    for (size_t element = 0; element < elementCount; ++element) {
        const uint8_t* sourceElement =
            source.data() + element * sourceBinding.ByteStride;
        uint8_t* packedElement =
            packed.data() + element * packedStride;
        for (const auto& attribute : packedAttributes) {
            std::array<float, 4> value{0.0F, 0.0F, 0.0F, 1.0F};
            const uint32_t scalarSize = ScalarSize(attribute.SourceScalar);
            if (scalarSize == 0U ||
                attribute.SourceByteOffset +
                        scalarSize * attribute.SourceComponentCount >
                    sourceBinding.ByteStride) {
                SetError(error,
                         "source PICA vertex attribute is out of bounds");
                packed.clear();
                return false;
            }
            for (uint32_t component = 0;
                 component < attribute.SourceComponentCount; ++component) {
                value[component] = DecodeScalar(
                    attribute.SourceScalar,
                    sourceElement + attribute.SourceByteOffset +
                        component * scalarSize);
            }
            std::memcpy(packedElement + attribute.ByteOffset,
                        value.data(), sizeof(value));
        }
    }
    return true;
}

bool BuildPicaNriPackedVertexStreams(
    std::span<const PicaNriSourceVertexStream> streams,
    std::span<const PicaNriSourceVertexAttribute> attributes,
    std::vector<PicaNriPackedVertexStream>& packed,
    std::string* error) {
    packed.clear();
    if (streams.empty() || attributes.empty()) {
        SetError(error, "PICA vertex streams and attributes are required");
        return false;
    }

    std::vector<PicaNriSourceVertexBinding> bindings;
    bindings.reserve(streams.size());
    for (const auto& stream : streams)
        bindings.push_back(stream.Binding);
    const auto layout =
        BuildPicaNriVertexInputLayout(bindings, attributes);
    if (!layout.Valid()) {
        SetError(error, layout.Error);
        return false;
    }

    packed.reserve(layout.Bindings.size());
    for (const auto& packedBinding : layout.Bindings) {
        const auto source = std::find_if(
            streams.begin(), streams.end(), [&](const auto& candidate) {
                return candidate.Binding.Binding == packedBinding.Binding;
            });
        if (source == streams.end()) {
            SetError(error, "PICA vertex stream is missing");
            packed.clear();
            return false;
        }
        std::vector<PicaNriPackedVertexAttribute> localAttributes;
        for (const auto& attribute : layout.Attributes)
            if (attribute.Binding == packedBinding.Binding)
                localAttributes.push_back(attribute);
        PicaNriPackedVertexStream result{
            packedBinding.Binding, packedBinding.ByteStride, {}};
        if (!RepackPicaNriVertexBinding(
                source->Binding, localAttributes,
                packedBinding.ByteStride, source->Bytes,
                result.Bytes, error)) {
            packed.clear();
            return false;
        }
        packed.push_back(std::move(result));
    }
    return true;
}

} // namespace Fast::Oot3d
