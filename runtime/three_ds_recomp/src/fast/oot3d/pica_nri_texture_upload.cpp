#include "fast/oot3d/pica_nri_texture_upload.h"

#include <cstring>
#include <limits>

namespace Fast::Oot3d {
namespace {

std::optional<uint64_t> AlignUp(uint64_t value, uint64_t alignment) {
    if (alignment == 0U) return std::nullopt;
    const uint64_t remainder = value % alignment;
    if (remainder == 0U) return value;
    const uint64_t increment = alignment - remainder;
    if (value > std::numeric_limits<uint64_t>::max() - increment)
        return std::nullopt;
    return value + increment;
}

} // namespace

std::optional<PicaNriTextureUploadLayout>
PlanPicaNriTextureUpload(
    uint64_t currentOffset, uint64_t capacity,
    uint32_t width, uint32_t height, uint32_t bytesPerPixel,
    uint32_t rowAlignment, uint32_t sliceAlignment) {
    if (width == 0U || height == 0U || bytesPerPixel == 0U ||
        rowAlignment == 0U || sliceAlignment == 0U)
        return std::nullopt;
    const uint64_t rowBytes =
        static_cast<uint64_t>(width) * bytesPerPixel;
    const auto rowPitch = AlignUp(rowBytes, rowAlignment);
    if (!rowPitch.has_value() ||
        *rowPitch > std::numeric_limits<uint32_t>::max() ||
        *rowPitch > std::numeric_limits<uint64_t>::max() / height)
        return std::nullopt;
    const auto slicePitch =
        AlignUp(*rowPitch * height, sliceAlignment);
    const auto offset = AlignUp(currentOffset, sliceAlignment);
    if (!slicePitch.has_value() || !offset.has_value() ||
        *slicePitch > std::numeric_limits<uint32_t>::max() ||
        *offset > capacity || *slicePitch > capacity - *offset)
        return std::nullopt;
    return PicaNriTextureUploadLayout{
        *offset, static_cast<uint32_t>(*rowPitch),
        static_cast<uint32_t>(*slicePitch), *offset + *slicePitch};
}

bool PackPicaNriTextureUpload(
    std::span<const uint8_t> pixels,
    uint32_t width, uint32_t height, uint32_t bytesPerPixel,
    const PicaNriTextureUploadLayout& layout,
    std::span<uint8_t> destination) {
    if (width == 0U || height == 0U || bytesPerPixel == 0U)
        return false;
    const uint64_t rowBytes =
        static_cast<uint64_t>(width) * bytesPerPixel;
    if (rowBytes > std::numeric_limits<uint64_t>::max() / height)
        return false;
    const uint64_t sourceSize = rowBytes * height;
    if (rowBytes > layout.RowPitch ||
        sourceSize != pixels.size() ||
        layout.Offset > destination.size() ||
        layout.RequiredSize > destination.size() ||
        layout.RequiredSize < layout.Offset ||
        layout.SlicePitch >
            layout.RequiredSize - layout.Offset ||
        static_cast<uint64_t>(layout.RowPitch) * height >
            layout.SlicePitch)
        return false;
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(
            destination.data() + layout.Offset +
                static_cast<uint64_t>(row) * layout.RowPitch,
            pixels.data() + static_cast<uint64_t>(row) * rowBytes,
            static_cast<size_t>(rowBytes));
    }
    return true;
}

} // namespace Fast::Oot3d
