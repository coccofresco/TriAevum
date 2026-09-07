#include "fast/oot3d/smaa_lookup_data.h"

#include "fast/oot3d/smaa_lookup_payload.h"

#include <zlib.h>

#include <array>
#include <limits>
#include <span>

namespace Fast::Oot3d {
namespace {

constexpr size_t kAreaBytes =
    static_cast<size_t>(SmaaLookupData::AreaWidth) *
    SmaaLookupData::AreaHeight * 4U;
constexpr size_t kSearchBytes =
    static_cast<size_t>(SmaaLookupData::SearchWidth) *
    SmaaLookupData::SearchHeight * 4U;

int Base64Value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

std::vector<uint8_t> DecodeBase64(std::span<const char> encoded) {
    std::vector<uint8_t> result;
    result.reserve(encoded.size() * 3U / 4U);
    uint32_t accumulator = 0U;
    uint32_t bitCount = 0U;
    for (const char value : encoded) {
        if (value == '=') break;
        const int decoded = Base64Value(value);
        if (decoded < 0) {
            if (value == ' ' || value == '\t' ||
                value == '\r' || value == '\n') {
                continue;
            }
            return {};
        }
        accumulator =
            (accumulator << 6U) | static_cast<uint32_t>(decoded);
        bitCount += 6U;
        if (bitCount >= 8U) {
            bitCount -= 8U;
            result.push_back(static_cast<uint8_t>(
                accumulator >> bitCount));
            if (bitCount == 0U) {
                accumulator = 0U;
            } else {
                accumulator &= (1U << bitCount) - 1U;
            }
        }
    }
    return result;
}

} // namespace

bool SmaaLookupData::Valid() const noexcept {
    return AreaRgba8.size() == kAreaBytes &&
        SearchRgba8.size() == kSearchBytes;
}

SmaaLookupData DecodeSmaaLookupData() noexcept {
    try {
        const auto compressed = DecodeBase64(
            std::span<const char>(
                kSmaaLookupRgbaZlibBase64,
                sizeof(kSmaaLookupRgbaZlibBase64) - 1U));
        if (compressed.empty() ||
            compressed.size() >
                std::numeric_limits<uLong>::max()) {
            return {};
        }
        std::vector<uint8_t> decoded(kAreaBytes + kSearchBytes);
        uLongf decodedBytes =
            static_cast<uLongf>(decoded.size());
        if (uncompress(
                decoded.data(), &decodedBytes,
                compressed.data(),
                static_cast<uLong>(compressed.size())) != Z_OK ||
            decodedBytes != decoded.size()) {
            return {};
        }
        SmaaLookupData result;
        result.AreaRgba8.assign(
            decoded.begin(), decoded.begin() + kAreaBytes);
        result.SearchRgba8.assign(
            decoded.begin() + kAreaBytes, decoded.end());
        return result;
    } catch (...) {
        return {};
    }
}

} // namespace Fast::Oot3d
