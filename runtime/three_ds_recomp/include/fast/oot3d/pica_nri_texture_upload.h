#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace Fast::Oot3d {

struct PicaNriTextureUploadLayout {
    uint64_t Offset = 0;
    uint32_t RowPitch = 0;
    uint32_t SlicePitch = 0;
    uint64_t RequiredSize = 0;
};

std::optional<PicaNriTextureUploadLayout>
PlanPicaNriTextureUpload(
    uint64_t currentOffset, uint64_t capacity,
    uint32_t width, uint32_t height, uint32_t bytesPerPixel,
    uint32_t rowAlignment, uint32_t sliceAlignment);

bool PackPicaNriTextureUpload(
    std::span<const uint8_t> pixels,
    uint32_t width, uint32_t height, uint32_t bytesPerPixel,
    const PicaNriTextureUploadLayout& layout,
    std::span<uint8_t> destination);

} // namespace Fast::Oot3d
