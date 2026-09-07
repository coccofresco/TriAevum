#include "fast/renderer3ds/pica_nri_upload.h"

#include <cstring>
#include <limits>

namespace Fast::Renderer3ds {

bool CopyPicaNriUploadRanges(
    std::span<const uint8_t> source,
    std::span<uint8_t> destination,
    std::span<const PicaNriUploadRange> ranges,
    uint64_t* copiedBytes) {
    uint64_t total = 0;
    for (const auto& range : ranges) {
        if (range.Size == 0 ||
            range.Offset > source.size() ||
            range.Size > source.size() - range.Offset ||
            range.Offset > destination.size() ||
            range.Size > destination.size() - range.Offset ||
            range.Size > std::numeric_limits<uint64_t>::max() - total) {
            if (copiedBytes != nullptr) *copiedBytes = 0;
            return false;
        }
        total += range.Size;
    }
    for (const auto& range : ranges) {
        std::memcpy(destination.data() + range.Offset,
                    source.data() + range.Offset,
                    static_cast<size_t>(range.Size));
    }
    if (copiedBytes != nullptr) *copiedBytes = total;
    return true;
}

} // namespace Fast::Renderer3ds
