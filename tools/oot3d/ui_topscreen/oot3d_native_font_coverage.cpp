#include "oot3d_native_font_coverage.h"

#include <algorithm>
#include <map>
#include <utility>

namespace Oot3dNativeGame {
namespace {
constexpr std::size_t kMaximumBytes = 32U * 1024U * 1024U;
std::uint32_t U16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U);
}
struct FontView {
    std::uint32_t Width, Height, Offset, Stride;
    std::map<std::uint32_t, std::uint32_t> Characters;
};
bool Parse(std::span<const std::uint8_t> bytes, FontView& view) {
    if (bytes.size() < 16 || bytes.size() > kMaximumBytes ||
        !std::equal(bytes.begin(), bytes.begin() + 4, "QBF1") ||
        bytes[12] != 4 || bytes[15] != 2) return false;
    const auto count = U16(bytes, 4), glyphs = U16(bytes, 6);
    view.Width = bytes[13];
    view.Height = bytes[14];
    view.Offset = 16 + count * 8;
    view.Stride = view.Width * view.Height / 2;
    if (!count || !glyphs || !view.Width || !view.Height ||
        view.Width % 8 || view.Height % 8 ||
        view.Offset + static_cast<std::uint64_t>(glyphs) * view.Stride != bytes.size()) return false;
    std::uint32_t previous = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto code = U16(bytes, 16 + index * 8);
        const auto glyph = U16(bytes, 18 + index * 8);
        if ((index && code <= previous) || glyph >= glyphs) return false;
        view.Characters.emplace(code, view.Offset + glyph * view.Stride);
        previous = code;
    }
    return view.Characters.contains(U16(bytes, 8));
}
} // namespace

bool NativeFontCoverage::Load(std::span<const std::uint8_t> native,
                              std::span<const std::uint8_t> replacement,
                              std::string* error) {
    const auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    FontView original{}, hd{};
    if (!Parse(native, original) || !Parse(replacement, hd))
        return fail("invalid or unsupported QBF coverage");
    if (hd.Width % original.Width || hd.Height % original.Height ||
        hd.Width / original.Width != hd.Height / original.Height ||
        hd.Width < original.Width) return fail("nonuniform QBF coverage density");
    NativeFontCoverage candidate;
    candidate.mWidth = original.Width;
    candidate.mHeight = original.Height;
    candidate.mDensity = hd.Width / original.Width;
    for (const auto& [code, offset] : original.Characters) {
        const auto entry = hd.Characters.find(code);
        if (entry == hd.Characters.end()) return fail("missing native QBF character");
        std::vector<std::uint8_t> alpha(hd.Width * hd.Height);
        for (std::uint32_t index = 0; index < hd.Stride; ++index) {
            const auto value = replacement[entry->second + index];
            alpha[index * 2] = (value >> 4U) * 17;
            alpha[index * 2 + 1] = (value & 15U) * 17;
        }
        const auto [existing, inserted] = candidate.mGlyphs.emplace(offset, alpha);
        if (!inserted && existing->second != alpha) return fail("ambiguous native QBF glyph alias");
    }
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}

std::span<const std::uint8_t> NativeFontCoverage::Glyph(std::uint32_t offset) const {
    const auto found = mGlyphs.find(offset);
    return found == mGlyphs.end() ? std::span<const std::uint8_t>{} : found->second;
}

bool NativeFontCoverageAtlas::Reset(std::uint64_t epoch, std::uint32_t width,
                                    std::uint32_t height, std::uint32_t density) {
    if (!epoch || !width || !height || !density || width > 4096 || height > 4096 || density > 32 ||
        static_cast<std::uint64_t>(width) * height * density * density > kMaximumBytes) return false;
    auto storage = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(width) * height * density * density, 0);
    mEpoch = epoch;
    ++mGeneration;
    mWidth = width;
    mHeight = height;
    mDensity = density;
    mCoverage = std::move(storage);
    return true;
}

bool NativeFontCoverageAtlas::Fits(std::uint32_t x, std::uint32_t y,
                                  std::uint32_t width, std::uint32_t height) const {
    return mCoverage && x <= mWidth && y <= mHeight && width <= mWidth - x && height <= mHeight - y;
}

void NativeFontCoverageAtlas::MakeWritable() {
    if (mCoverage.use_count() > 1) mCoverage = std::make_shared<std::vector<std::uint8_t>>(*mCoverage);
    ++mGeneration;
}

bool NativeFontCoverageAtlas::Blit(const NativeFontCoverage& font, std::uint32_t offset,
                                  std::uint32_t x, std::uint32_t y) {
    const auto glyph = font.Glyph(offset);
    if (glyph.empty() || font.Density() != mDensity ||
        !Fits(x, y, font.LogicalWidth(), font.LogicalHeight())) return false;
    const auto rowBytes = font.LogicalWidth() * mDensity;
    const auto rows = font.LogicalHeight() * mDensity;
    const auto stride = mWidth * mDensity;
    const auto start = y * mDensity * stride + x * mDensity;
    bool differs = false;
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto source = glyph.subspan(row * rowBytes, rowBytes);
        differs |= !std::equal(source.begin(), source.end(), mCoverage->begin() + start + row * stride);
    }
    if (!differs) return true;
    MakeWritable();
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto source = glyph.subspan(row * rowBytes, rowBytes);
        std::copy(source.begin(), source.end(), mCoverage->begin() + start + row * stride);
    }
    return true;
}

bool NativeFontCoverageAtlas::Clear(std::uint32_t x, std::uint32_t y,
                                   std::uint32_t width, std::uint32_t height) {
    if (!Fits(x, y, width, height)) return false;
    const auto stride = mWidth * mDensity;
    bool differs = false;
    for (std::uint32_t row = 0; row < height * mDensity; ++row) {
        const auto first = mCoverage->begin() + (y * mDensity + row) * stride + x * mDensity;
        differs |= std::any_of(first, first + width * mDensity, [](auto value) { return value != 0; });
    }
    if (!differs) return true;
    MakeWritable();
    for (std::uint32_t row = 0; row < height * mDensity; ++row) {
        const auto first = mCoverage->begin() + (y * mDensity + row) * stride + x * mDensity;
        std::fill(first, first + width * mDensity, 0);
    }
    return true;
}

NativeFontAtlasSnapshot NativeFontCoverageAtlas::Snapshot() const {
    return {mEpoch, mGeneration, mWidth * mDensity, mHeight * mDensity, mCoverage};
}
} // namespace Oot3dNativeGame
