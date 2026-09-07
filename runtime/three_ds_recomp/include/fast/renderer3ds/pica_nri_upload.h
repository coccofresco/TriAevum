#pragma once

#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

struct PicaNriUploadRange {
    uint64_t Offset = 0;
    uint64_t Size = 0;
};

// Copies sparse draw ranges at identical offsets. Validation is transactional:
// no destination byte is changed unless every range fits both arenas.
bool CopyPicaNriUploadRanges(
    std::span<const uint8_t> source,
    std::span<uint8_t> destination,
    std::span<const PicaNriUploadRange> ranges,
    uint64_t* copiedBytes = nullptr);

} // namespace Fast::Renderer3ds
