#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"
#include "oot3d/renderer/pica_texture_decode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <set>
#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr uint16_t kPicaS8 = 0x1400;
constexpr uint16_t kPicaU8 = 0x1401;
constexpr uint16_t kPicaS16 = 0x1402;
constexpr uint16_t kPicaU16 = 0x1403;
constexpr uint16_t kPicaS32 = 0x1404;
constexpr uint16_t kPicaU32 = 0x1405;
constexpr uint16_t kPicaF32 = 0x1406;
constexpr uint16_t kPicaUnsignedByte44 = 0x6760;
constexpr uint16_t kPicaUnsigned4Bits = 0x6761;
constexpr uint16_t kPicaUnsignedShort4444 = 0x8033;
constexpr uint16_t kPicaUnsignedShort5551 = 0x8034;
constexpr uint16_t kPicaUnsignedShort565 = 0x8363;

constexpr uint16_t kPicaTextureRgba = 0x6752;
constexpr uint16_t kPicaTextureRgb = 0x6754;
constexpr uint16_t kPicaTextureAlpha = 0x6756;
constexpr uint16_t kPicaTextureLuminance = 0x6757;
constexpr uint16_t kPicaTextureLuminanceAlpha = 0x6758;
constexpr uint16_t kPicaTextureEtc1 = 0x675A;
constexpr uint16_t kPicaTextureEtc1A4 = 0x675B;

constexpr uint16_t kAttrPosition = kOot3dCmbAttributePosition;
constexpr uint16_t kAttrNormal = kOot3dCmbAttributeNormal;
constexpr uint16_t kAttrColor = kOot3dCmbAttributeColor;
constexpr uint16_t kAttrUv0 = kOot3dCmbAttributeUv0;
constexpr uint16_t kSkinningIndexSlot = 6;
constexpr uint16_t kSkinningWeightSlot = 7;

constexpr uint32_t kCsabFrameCountOffset = 0x28;
constexpr uint32_t kCsabAnimatedBoneCountOffset = 0x30;
constexpr uint32_t kCsabSkeletonBoneCountOffset = 0x34;
constexpr uint32_t kCsabBoneTableOffset = 0x38;
constexpr uint32_t kCsabNodeOffsetBase = 0x18;
constexpr uint32_t kCsabAnodHeaderSize = 0x1C;
constexpr uint32_t kCsabAnodChannelOffsetTableOffset = 0x08;
constexpr uint32_t kCsabAnodChannelOffsetCount = 10;
constexpr uint32_t kCsabAnodChannelBlockHeaderSize = 0x08;
constexpr uint32_t kCsabAnodChannelBlockKeyHeaderSize = 0x10;
constexpr uint32_t kCsabAnodF32KeySize = 0x10;
constexpr uint32_t kCsabAnodS16KeySize = 0x08;
constexpr float kCsabAnodHermiteIntervalScale = 1.0f / 30.0f;
constexpr float kCsabAnodS16RotationScale = 3.14159265358979323846f / 32768.0f;
constexpr uint32_t kCtxbTextureHeaderSize = 0x48;
constexpr uint32_t kCtxbTexChunkOffset = 0x18;
constexpr uint32_t kCtxbPayloadOffset = 0x48;
constexpr uint32_t kNativeCtxbDescriptorSlotBaseOffset = 0x13C;
constexpr uint32_t kNativeCtxbDescriptorSlotStrideBytes = 0x30;
constexpr uint32_t kNativeCtxbSourceTextureHeaderBaseOffset = 0x24;
constexpr uint32_t kNativeCtxbSourcePayloadPointerFieldOffset = 0x4C;
constexpr uint32_t kCmbLutHeaderSize = 0x10;
constexpr uint32_t kCmbLutSourceSampleCount = 0x101;
constexpr uint32_t kCmbLutPackedValueCount = 0x100;
constexpr uint32_t kCmbLutLinearPointStride = 0x08;
constexpr uint32_t kCmbLutHermitePointStride = 0x10;
constexpr float kCmbLutClampMinimum = 0.0f;
constexpr uint32_t kCmabStringTableOffsetCandidate = 0x18;
constexpr uint32_t kCmabTextureDataOffsetCandidate = 0x1C;
constexpr uint32_t kCmabTexturePayloadOffsetCandidate = 0x30;
constexpr uint32_t kShbinDvlbHeaderSize = 0x08;
constexpr uint32_t kShbinDvlpHeaderSize = 0x1C;
constexpr uint32_t kShbinDvleHeaderSize = 0x40;
constexpr uint32_t kShbinSwizzleInfoSize = 0x08;
constexpr uint32_t kShbinConstantInfoSize = 0x14;
constexpr uint32_t kShbinOutputRegisterInfoSize = 0x08;
constexpr uint32_t kShbinUniformInfoBasicSize = 0x08;

struct BinaryView {
    std::span<const uint8_t> Bytes;
    std::string Source;

    void Require(size_t offset, size_t size) const {
        if (offset > Bytes.size() || size > Bytes.size() - offset) {
            throw std::runtime_error(Source + ": read outside file");
        }
    }

    bool HasMagic(size_t offset, std::string_view magic) const {
        if (offset + magic.size() > Bytes.size()) {
            return false;
        }
        for (size_t i = 0; i < magic.size(); ++i) {
            if (Bytes[offset + i] != static_cast<uint8_t>(magic[i])) {
                return false;
            }
        }
        return true;
    }

    uint8_t U8(size_t offset) const {
        Require(offset, 1);
        return Bytes[offset];
    }

    int8_t S8(size_t offset) const {
        return static_cast<int8_t>(U8(offset));
    }

    uint16_t U16(size_t offset) const {
        Require(offset, 2);
        return static_cast<uint16_t>(Bytes[offset]) | (static_cast<uint16_t>(Bytes[offset + 1]) << 8);
    }

    int16_t S16(size_t offset) const {
        return static_cast<int16_t>(U16(offset));
    }

    uint32_t U32(size_t offset) const {
        Require(offset, 4);
        return static_cast<uint32_t>(Bytes[offset]) | (static_cast<uint32_t>(Bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(Bytes[offset + 2]) << 16) | (static_cast<uint32_t>(Bytes[offset + 3]) << 24);
    }

    uint64_t U64(size_t offset) const {
        const uint64_t lo = U32(offset);
        const uint64_t hi = U32(offset + 4);
        return lo | (hi << 32);
    }

    int32_t S32(size_t offset) const {
        return static_cast<int32_t>(U32(offset));
    }

    float F32(size_t offset) const {
        const auto raw = U32(offset);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    std::string Cstr(size_t offset, size_t size) const {
        Require(offset, size);
        size_t length = 0;
        while (length < size && Bytes[offset + length] != 0) {
            ++length;
        }
        return std::string(reinterpret_cast<const char*>(Bytes.data() + offset), length);
    }
};

struct VertexList {
    uint32_t Offset = 0;
    float Scale = 1.0f;
    uint16_t DataType = 0;
    uint16_t Mode = 0;
    std::array<float, 4> Constant{};
};

struct VertexListData {
    uint32_t Length = 0;
    uint32_t Offset = 0;
};

enum class AttributeKind {
    Missing,
    Constant,
    Data,
};

struct VertexAttributeInfo {
    uint16_t Slot = 0;
    AttributeKind Kind = AttributeKind::Missing;
    uint16_t Width = 0;
    uint16_t DataType = 0;
    float Scale = 1.0f;
    uint16_t Mode = 0;
    uint32_t Offset = 0;
    uint32_t ByteCount = 0;
    size_t Start = 0;
    std::array<float, 4> Constants{};
};

struct ChannelOffset {
    uint16_t Slot = 0;
    uint16_t Offset = 0;
};

enum class CsabChannelBlockClass {
    Unknown,
    F32Constant,
    S16Constant,
    F32Keys,
    S16Keys,
};

struct CsabChannelBlockInfo {
    CsabChannelBlockClass Class = CsabChannelBlockClass::Unknown;
    uint32_t KeyCount = 0;
};

struct CsabKey {
    uint32_t Frame = 0;
    float Value = 0.0f;
    float Incoming = 0.0f;
    float Outgoing = 0.0f;
};

struct CsabFrameChannels {
    std::map<uint32_t, std::map<uint16_t, float>> ValuesByNode;
    uint32_t SampledChannelValueCount = 0;
    uint32_t FiniteChannelValueCount = 0;
    uint32_t NonF32ChannelBlockCount = 0;
};

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes;
    for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
        bytes.push_back(static_cast<uint8_t>(*it));
    }
    return bytes;
}

std::string ShbinOutputSemanticName(uint16_t type) {
    switch (type) {
        case 0:
            return "out.pos";
        case 1:
            return "out.quat";
        case 2:
            return "out.col";
        case 3:
            return "out.tex0";
        case 4:
            return "out.tex0w";
        case 5:
            return "out.tex1";
        case 6:
            return "out.tex2";
        case 8:
            return "out.view";
        default:
            return "out.unk";
    }
}

std::string ReadBoundedCString(const BinaryView& view, size_t offset, size_t end) {
    if (offset >= end || end > view.Bytes.size()) {
        throw std::runtime_error(view.Source + ": SHBIN symbol string is outside its table");
    }
    return view.Cstr(offset, end - offset);
}

std::string ReadShbinSymbol(const BinaryView& view, size_t symbolTableOffset, size_t symbolTableSize,
                            uint32_t symbolOffset) {
    if (symbolOffset >= symbolTableSize) {
        throw std::runtime_error(view.Source + ": SHBIN symbol offset is outside its table");
    }
    return ReadBoundedCString(view, symbolTableOffset + symbolOffset, symbolTableOffset + symbolTableSize);
}

uint8_t ToU8(float value) {
    value = std::clamp(value, 0.0f, 255.0f);
    return static_cast<uint8_t>(std::lround(value));
}

uint8_t ToColorU8(float value) {
    // CMB attribute scales produce normalized PICA color inputs and can slightly exceed 1.0.
    return ToU8(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

size_t DataTypeSize(uint16_t dataType) {
    switch (dataType) {
        case kPicaS8:
        case kPicaU8:
            return 1;
        case kPicaS16:
        case kPicaU16:
            return 2;
        case kPicaS32:
        case kPicaU32:
        case kPicaF32:
            return 4;
        default:
            throw std::runtime_error("unsupported PICA data type");
    }
}

float ReadScalar(const BinaryView& view, size_t offset, uint16_t dataType) {
    switch (dataType) {
        case kPicaS8:
            return static_cast<float>(view.S8(offset));
        case kPicaU8:
            return static_cast<float>(view.U8(offset));
        case kPicaS16:
            return static_cast<float>(view.S16(offset));
        case kPicaU16:
            return static_cast<float>(view.U16(offset));
        case kPicaS32:
            return static_cast<float>(view.S32(offset));
        case kPicaU32:
            return static_cast<float>(view.U32(offset));
        case kPicaF32:
            return view.F32(offset);
        default:
            throw std::runtime_error(view.Source + ": unsupported scalar data type");
    }
}

uint8_t Expand4To8(uint8_t value) {
    return static_cast<uint8_t>(((value << 4) | value) & 0xFF);
}

uint8_t Expand5To8(uint8_t value) {
    return static_cast<uint8_t>(((value << 3) | (value >> 2)) & 0xFF);
}

uint8_t Expand6To8(uint8_t value) {
    return static_cast<uint8_t>(((value << 2) | (value >> 4)) & 0xFF);
}

int SignExtend(uint32_t value, uint32_t bits) {
    const uint32_t signBit = 1U << (bits - 1);
    return static_cast<int>((value ^ signBit) - signBit);
}

uint8_t ClampU8(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

uint32_t Morton8(uint32_t x, uint32_t y) {
    return ((x & 0x1) << 0) | ((y & 0x1) << 1) | ((x & 0x2) << 1) | ((y & 0x2) << 2) |
           ((x & 0x4) << 2) | ((y & 0x4) << 3);
}

uint32_t AlignedTextureDimension(uint32_t value) {
    return std::max<uint32_t>(8, (value + 7) & ~7U);
}

std::optional<size_t> EncodedTextureLevelSize(uint32_t width, uint32_t height,
                                              uint16_t textureFormat, uint16_t dataType) {
    if (width == 0 || height == 0) {
        return std::nullopt;
    }

    const size_t pixelCount = static_cast<size_t>(AlignedTextureDimension(width)) *
                              AlignedTextureDimension(height);
    if (textureFormat == kPicaTextureEtc1 && dataType == 0) {
        return pixelCount / 2;
    }
    if (textureFormat == kPicaTextureEtc1A4 && dataType == 0) {
        return pixelCount;
    }
    if (textureFormat == kPicaTextureRgba) {
        if (dataType == kPicaU8) {
            return pixelCount * 4;
        }
        if (dataType == kPicaUnsignedShort4444 || dataType == kPicaUnsignedShort5551) {
            return pixelCount * 2;
        }
    }
    if (textureFormat == kPicaTextureRgb) {
        if (dataType == kPicaU8) {
            return pixelCount * 3;
        }
        if (dataType == kPicaUnsignedShort565) {
            return pixelCount * 2;
        }
    }
    if (textureFormat == kPicaTextureAlpha || textureFormat == kPicaTextureLuminance) {
        if (dataType == kPicaU8) {
            return pixelCount;
        }
        if (dataType == kPicaUnsigned4Bits) {
            return pixelCount / 2;
        }
    }
    if (textureFormat == kPicaTextureLuminanceAlpha) {
        if (dataType == kPicaU8) {
            return pixelCount * 2;
        }
        if (dataType == kPicaUnsignedByte44) {
            return pixelCount;
        }
    }
    return std::nullopt;
}

uint32_t MaximumTextureMipLevelCount(uint32_t width, uint32_t height) {
    uint32_t count = 1;
    while (width > 1 || height > 1) {
        width = std::max<uint32_t>(1, width / 2);
        height = std::max<uint32_t>(1, height / 2);
        ++count;
    }
    return count;
}

uint16_t ReadU16Le(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("texture data is truncated");
    }
    return static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

std::vector<uint8_t> DetileCtrTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                                      uint32_t bytesPerPixel) {
    const uint32_t alignedWidth = AlignedTextureDimension(width);
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * bytesPerPixel);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tileX = x / 8;
            const uint32_t tileY = y / 8;
            const uint32_t inTileX = x % 8;
            const uint32_t inTileY = y % 8;
            const uint32_t tileIndex = tileY * (alignedWidth / 8) + tileX;
            const uint32_t srcPixel = tileIndex * 64 + Morton8(inTileX, inTileY);
            const size_t src = static_cast<size_t>(srcPixel) * bytesPerPixel;
            const size_t dst = (static_cast<size_t>(y) * width + x) * bytesPerPixel;
            if (src + bytesPerPixel > data.size()) {
                throw std::runtime_error("tiled texture data is truncated");
            }
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(src), bytesPerPixel,
                        out.begin() + static_cast<std::ptrdiff_t>(dst));
        }
    }
    return out;
}

std::vector<uint8_t> DetileCtrTexture4Bpp(const std::vector<uint8_t>& data, uint32_t width, uint32_t height) {
    const uint32_t alignedWidth = AlignedTextureDimension(width);
    std::vector<uint8_t> out(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tileX = x / 8;
            const uint32_t tileY = y / 8;
            const uint32_t srcPixel = (tileY * (alignedWidth / 8) + tileX) * 64 + Morton8(x % 8, y % 8);
            const size_t src = srcPixel / 2;
            if (src >= data.size()) {
                throw std::runtime_error("tiled 4bpp texture data is truncated");
            }
            uint8_t value = data[src];
            value = (srcPixel & 1) != 0 ? static_cast<uint8_t>(value >> 4) : static_cast<uint8_t>(value & 0x0F);
            out[static_cast<size_t>(y) * width + x] = value;
        }
    }
    return out;
}

void PushRgba(std::vector<uint8_t>& out, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    out.push_back(r);
    out.push_back(g);
    out.push_back(b);
    out.push_back(a);
}

std::vector<uint8_t> DecodeRgb565ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const uint16_t value = ReadU16Le(data, i * 2);
        PushRgba(out, Expand5To8((value >> 11) & 0x1F), Expand6To8((value >> 5) & 0x3F),
                 Expand5To8(value & 0x1F), 255);
    }
    return out;
}

std::vector<uint8_t> DecodeRgba5551ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        if (static_cast<size_t>(i) * 2 + 2 > data.size()) {
            throw std::runtime_error("RGBA5551 texture data is truncated");
        }
        const uint16_t value = (static_cast<uint16_t>(data[i * 2]) << 8) | data[i * 2 + 1];
        PushRgba(out, Expand5To8((value >> 11) & 0x1F), Expand5To8((value >> 6) & 0x1F),
                 Expand5To8((value >> 1) & 0x1F), (value & 1) != 0 ? 255 : 0);
    }
    return out;
}

std::vector<uint8_t> DecodeRgba4444ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const uint16_t value = ReadU16Le(data, i * 2);
        PushRgba(out, Expand4To8((value >> 12) & 0x0F), Expand4To8((value >> 8) & 0x0F),
                 Expand4To8((value >> 4) & 0x0F), Expand4To8(value & 0x0F));
    }
    return out;
}

std::vector<uint8_t> DecodeRgba8ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < static_cast<size_t>(pixelCount) * 4) {
        throw std::runtime_error("RGBA8 texture data is truncated");
    }
    return std::vector<uint8_t>(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(pixelCount * 4));
}

std::vector<uint8_t> DecodeRgb8ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < static_cast<size_t>(pixelCount) * 3) {
        throw std::runtime_error("RGB8 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        PushRgba(out, data[i * 3], data[i * 3 + 1], data[i * 3 + 2], 255);
    }
    return out;
}

std::vector<uint8_t> DecodeA8ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < pixelCount) {
        throw std::runtime_error("A8 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        PushRgba(out, 255, 255, 255, data[i]);
    }
    return out;
}

std::vector<uint8_t> DecodeL8ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < pixelCount) {
        throw std::runtime_error("L8 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        PushRgba(out, data[i], data[i], data[i], 255);
    }
    return out;
}

std::vector<uint8_t> DecodeL4ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < pixelCount) {
        throw std::runtime_error("L4 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const uint8_t value = static_cast<uint8_t>((data[i] & 0x0F) * 17);
        PushRgba(out, value, value, value, 255);
    }
    return out;
}

std::vector<uint8_t> DecodeA4ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < pixelCount) {
        throw std::runtime_error("A4 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        PushRgba(out, 255, 255, 255, Expand4To8(data[i] & 0x0F));
    }
    return out;
}

std::vector<uint8_t> DecodeLa8ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < static_cast<size_t>(pixelCount) * 2) {
        throw std::runtime_error("LA8 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const uint8_t alpha = data[i * 2];
        const uint8_t luminance = data[i * 2 + 1];
        PushRgba(out, luminance, luminance, luminance, alpha);
    }
    return out;
}

std::vector<uint8_t> DecodeLa4ToRgba8(const std::vector<uint8_t>& data, uint32_t pixelCount) {
    if (data.size() < pixelCount) {
        throw std::runtime_error("LA4 texture data is truncated");
    }
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(pixelCount) * 4);
    for (uint32_t i = 0; i < pixelCount; ++i) {
        const uint8_t luminance = Expand4To8(data[i] >> 4);
        const uint8_t alpha = Expand4To8(data[i] & 0x0F);
        PushRgba(out, luminance, luminance, luminance, alpha);
    }
    return out;
}

const std::array<std::array<int, 2>, 8> kEtc1ModifierTable{ {
    { 2, 8 },
    { 5, 17 },
    { 9, 29 },
    { 13, 42 },
    { 18, 60 },
    { 24, 80 },
    { 33, 106 },
    { 47, 183 },
} };

std::array<uint8_t, 3> SampleEtc1Subtile(uint64_t raw, uint32_t x, uint32_t y) {
    const uint32_t texel = 4 * x + y;
    const bool flip = ((raw >> 32) & 1) != 0;
    const bool differential = ((raw >> 33) & 1) != 0;
    const uint32_t tableIndex2 = static_cast<uint32_t>((raw >> 34) & 0x7);
    const uint32_t tableIndex1 = static_cast<uint32_t>((raw >> 37) & 0x7);
    const uint32_t lookupX = flip ? y : x;

    int r = 0;
    int g = 0;
    int b = 0;
    if (differential) {
        int baseR = static_cast<int>((raw >> 59) & 0x1F);
        int baseG = static_cast<int>((raw >> 51) & 0x1F);
        int baseB = static_cast<int>((raw >> 43) & 0x1F);
        if (lookupX >= 2) {
            baseR += SignExtend(static_cast<uint32_t>((raw >> 56) & 0x7), 3);
            baseG += SignExtend(static_cast<uint32_t>((raw >> 48) & 0x7), 3);
            baseB += SignExtend(static_cast<uint32_t>((raw >> 40) & 0x7), 3);
        }
        r = Expand5To8(static_cast<uint8_t>(baseR & 0x1F));
        g = Expand5To8(static_cast<uint8_t>(baseG & 0x1F));
        b = Expand5To8(static_cast<uint8_t>(baseB & 0x1F));
    } else if (lookupX < 2) {
        r = Expand4To8(static_cast<uint8_t>((raw >> 60) & 0x0F));
        g = Expand4To8(static_cast<uint8_t>((raw >> 52) & 0x0F));
        b = Expand4To8(static_cast<uint8_t>((raw >> 44) & 0x0F));
    } else {
        r = Expand4To8(static_cast<uint8_t>((raw >> 56) & 0x0F));
        g = Expand4To8(static_cast<uint8_t>((raw >> 48) & 0x0F));
        b = Expand4To8(static_cast<uint8_t>((raw >> 40) & 0x0F));
    }

    const uint32_t tableIndex = lookupX < 2 ? tableIndex1 : tableIndex2;
    const uint32_t tableSubIndex = static_cast<uint32_t>((raw >> texel) & 1);
    const bool negation = ((raw >> (16 + texel)) & 1) != 0;
    int modifier = kEtc1ModifierTable[tableIndex][tableSubIndex];
    if (negation) {
        modifier = -modifier;
    }
    return { ClampU8(r + modifier), ClampU8(g + modifier), ClampU8(b + modifier) };
}

uint64_t ReadU64Le(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("ETC1 texture data is truncated");
    }
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    return value;
}

std::vector<uint8_t> DecodeEtc1ToRgba8(const std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                                       bool hasAlpha) {
    const uint32_t subtileSize = hasAlpha ? 16 : 8;
    const uint32_t tileSize = subtileSize * 4;
    const uint32_t tilesX = std::max<uint32_t>(1, (width + 7) / 8);
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tileX = x / 8;
            const uint32_t tileY = y / 8;
            const uint32_t fineX = x % 8;
            const uint32_t fineY = y % 8;
            const uint32_t tileBase = (tileY * tilesX + tileX) * tileSize;
            const uint32_t subtileIndex = (fineX / 4) + 2 * (fineY / 4);
            const uint32_t subtileBase = tileBase + subtileIndex * subtileSize;
            const uint32_t localX = fineX % 4;
            const uint32_t localY = fineY % 4;

            uint8_t alpha = 255;
            uint32_t etcBase = subtileBase;
            if (hasAlpha) {
                const uint64_t packedAlpha = ReadU64Le(data, subtileBase);
                const uint32_t alphaNibble = static_cast<uint32_t>((packedAlpha >> (4 * (localX * 4 + localY))) & 0x0F);
                alpha = Expand4To8(static_cast<uint8_t>(alphaNibble));
                etcBase += 8;
            }
            const auto rgb = SampleEtc1Subtile(ReadU64Le(data, etcBase), localX, localY);
            const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            out[dst + 0] = rgb[0];
            out[dst + 1] = rgb[1];
            out[dst + 2] = rgb[2];
            out[dst + 3] = alpha;
        }
    }
    return out;
}

std::vector<uint8_t> DecodeTextureRgba8(const CmbTexture& texture) {
    const uint32_t width = texture.Width;
    const uint32_t height = texture.Height;
    const uint32_t pixelCount = width * height;
    if (width == 0 || height == 0) {
        return {};
    }
    if (texture.TextureFormat == kPicaTextureRgb && texture.DataType == kPicaUnsignedShort565) {
        return DecodeRgb565ToRgba8(DetileCtrTexture(texture.Data, width, height, 2), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureRgba && texture.DataType == kPicaUnsignedShort5551) {
        return DecodeRgba5551ToRgba8(DetileCtrTexture(texture.Data, width, height, 2), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureRgba && texture.DataType == kPicaUnsignedShort4444) {
        return DecodeRgba4444ToRgba8(DetileCtrTexture(texture.Data, width, height, 2), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureRgba && texture.DataType == kPicaU8) {
        return DecodeRgba8ToRgba8(DetileCtrTexture(texture.Data, width, height, 4), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureRgb && texture.DataType == kPicaU8) {
        return DecodeRgb8ToRgba8(DetileCtrTexture(texture.Data, width, height, 3), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureAlpha && texture.DataType == kPicaU8) {
        return DecodeA8ToRgba8(DetileCtrTexture(texture.Data, width, height, 1), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureAlpha && texture.DataType == kPicaUnsigned4Bits) {
        return DecodeA4ToRgba8(DetileCtrTexture4Bpp(texture.Data, width, height), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureLuminance && texture.DataType == kPicaU8) {
        return DecodeL8ToRgba8(DetileCtrTexture(texture.Data, width, height, 1), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureLuminance && texture.DataType == kPicaUnsigned4Bits) {
        return DecodeL4ToRgba8(DetileCtrTexture4Bpp(texture.Data, width, height), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureLuminanceAlpha && texture.DataType == kPicaU8) {
        return DecodeLa8ToRgba8(DetileCtrTexture(texture.Data, width, height, 2), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureLuminanceAlpha && texture.DataType == kPicaUnsignedByte44) {
        return DecodeLa4ToRgba8(DetileCtrTexture(texture.Data, width, height, 1), pixelCount);
    }
    if (texture.TextureFormat == kPicaTextureEtc1 && texture.DataType == 0) {
        return DecodeEtc1ToRgba8(texture.Data, width, height, false);
    }
    if (texture.TextureFormat == kPicaTextureEtc1A4 && texture.DataType == 0) {
        return DecodeEtc1ToRgba8(texture.Data, width, height, true);
    }
    return {};
}

void DecodeTextureMipChain(CmbTexture& texture) {
    texture.AdditionalMipLevels.clear();
    texture.MipmapCount = std::max<uint32_t>(1, texture.MipmapCount);
    texture.MipmapLayoutDecoded = false;

    const auto baseSize = EncodedTextureLevelSize(texture.Width, texture.Height,
                                                  texture.TextureFormat, texture.DataType);
    if (!baseSize.has_value()) {
        return;
    }
    if (*baseSize > texture.Data.size()) {
        return;
    }
    if (texture.MipmapCount > MaximumTextureMipLevelCount(texture.Width, texture.Height)) {
        return;
    }

    size_t encodedOffset = *baseSize;
    uint32_t width = texture.Width;
    uint32_t height = texture.Height;
    texture.AdditionalMipLevels.reserve(texture.MipmapCount - 1);
    for (uint32_t level = 1; level < texture.MipmapCount; ++level) {
        width = std::max<uint32_t>(1, width / 2);
        height = std::max<uint32_t>(1, height / 2);
        const auto encodedSize = EncodedTextureLevelSize(width, height,
                                                         texture.TextureFormat, texture.DataType);
        if (!encodedSize.has_value() || encodedOffset + *encodedSize > texture.Data.size()) {
            return;
        }

        CmbTexture decodeTexture;
        decodeTexture.Width = static_cast<uint16_t>(width);
        decodeTexture.Height = static_cast<uint16_t>(height);
        decodeTexture.TextureFormat = texture.TextureFormat;
        decodeTexture.DataType = texture.DataType;
        decodeTexture.Data.assign(texture.Data.begin() + static_cast<std::ptrdiff_t>(encodedOffset),
                                  texture.Data.begin() + static_cast<std::ptrdiff_t>(encodedOffset + *encodedSize));

        CmbTextureMipLevel mip;
        mip.Level = level;
        mip.Width = decodeTexture.Width;
        mip.Height = decodeTexture.Height;
        mip.DataOffset = static_cast<uint32_t>(encodedOffset);
        mip.DataSize = static_cast<uint32_t>(*encodedSize);
        mip.Rgba8 = DecodeTextureRgba8(decodeTexture);
        mip.Rgba8Decoded = mip.Rgba8.size() == static_cast<size_t>(mip.Width) * mip.Height * 4;
        texture.AdditionalMipLevels.push_back(std::move(mip));
        encodedOffset += *encodedSize;
    }
    texture.MipmapLayoutDecoded = true;
}

CmbTexture CmbTextureFromCtxb(const CtxbTexture& ctxb) {
    CmbTexture texture;
    texture.Name = ctxb.Name;
    texture.Width = ctxb.Width;
    texture.Height = ctxb.Height;
    texture.TextureFormat = ctxb.TextureFormat;
    texture.DataType = ctxb.DataType;
    texture.DataOffset = ctxb.PayloadOffset;
    texture.DataSize = ctxb.PayloadSize;
    texture.Data = ctxb.Data;
    return texture;
}

std::string TextureNameFromSource(std::string_view source) {
    auto leafStart = source.find_last_of("/\\!");
    std::string leaf = leafStart == std::string_view::npos ? std::string(source) : std::string(source.substr(leafStart + 1));
    auto dot = leaf.find_last_of('.');
    if (dot != std::string::npos && dot != 0) {
        leaf.resize(dot);
    }
    return leaf;
}

VertexListData ParseVld(const BinaryView& view, size_t offset) {
    return { view.U32(offset), view.U32(offset + 4) };
}

VertexList ParseVertexList(const BinaryView& view, size_t offset) {
    VertexList list;
    list.Offset = view.U32(offset);
    list.Scale = view.F32(offset + 4);
    list.DataType = view.U16(offset + 8);
    list.Mode = view.U16(offset + 0x0A);
    for (size_t i = 0; i < list.Constant.size(); ++i) {
        list.Constant[i] = view.F32(offset + 0x0C + i * 4);
    }
    return list;
}

std::vector<std::vector<float>> ReadAttributeValues(const BinaryView& view, size_t vatrOffset,
                                                    const VertexList& vertexList, const VertexListData& data,
                                                    size_t count, size_t components) {
    const auto size = DataTypeSize(vertexList.DataType);
    const auto start = vatrOffset + data.Offset + vertexList.Offset;
    const auto needed = count * components * size;
    if (vertexList.Offset + needed > data.Length) {
        throw std::runtime_error(view.Source + ": vertex attribute exceeds VATR data");
    }

    std::vector<std::vector<float>> rows;
    rows.reserve(count);
    for (size_t itemIndex = 0; itemIndex < count; ++itemIndex) {
        std::vector<float> row;
        row.reserve(components);
        for (size_t componentIndex = 0; componentIndex < components; ++componentIndex) {
            const auto offset = start + (itemIndex * components + componentIndex) * size;
            row.push_back(ReadScalar(view, offset, vertexList.DataType) * vertexList.Scale);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<Vec3f> ReadVec3Attribute(const BinaryView& view, size_t vatrOffset, const VertexList& vertexList,
                                     const VertexListData& data, size_t count, bool enabled, bool constant,
                                     Vec3f defaultValue) {
    if (count == 0) {
        return {};
    }
    if (!enabled) {
        return std::vector<Vec3f>(count, defaultValue);
    }
    if (constant || vertexList.Mode != 0) {
        return std::vector<Vec3f>(count, { vertexList.Constant[0], vertexList.Constant[1], vertexList.Constant[2] });
    }
    if (data.Length == 0) {
        throw std::runtime_error(view.Source + ": required vec3 attribute has no VATR data");
    }

    std::vector<Vec3f> values;
    for (const auto& row : ReadAttributeValues(view, vatrOffset, vertexList, data, count, 3)) {
        values.push_back({ row[0], row[1], row[2] });
    }
    return values;
}

std::vector<Vec2f> ReadVec2Attribute(const BinaryView& view, size_t vatrOffset, const VertexList& vertexList,
                                     const VertexListData& data, size_t count, bool enabled, bool constant,
                                     Vec2f defaultValue) {
    if (count == 0) {
        return {};
    }
    if (!enabled) {
        return std::vector<Vec2f>(count, defaultValue);
    }
    if (constant || vertexList.Mode != 0) {
        return std::vector<Vec2f>(count, { vertexList.Constant[0], vertexList.Constant[1] });
    }
    if (data.Length == 0) {
        throw std::runtime_error(view.Source + ": required vec2 attribute has no VATR data");
    }

    std::vector<Vec2f> values;
    for (const auto& row : ReadAttributeValues(view, vatrOffset, vertexList, data, count, 2)) {
        values.push_back({ row[0], row[1] });
    }
    return values;
}

std::vector<ColorRgba8> ReadColorAttribute(const BinaryView& view, size_t vatrOffset, const VertexList& vertexList,
                                           const VertexListData& data, size_t count, bool enabled, bool constant) {
    if (count == 0) {
        return {};
    }
    if (!enabled) {
        return std::vector<ColorRgba8>(count, { 255, 255, 255, 255 });
    }
    if (constant || vertexList.Mode != 0) {
        return std::vector<ColorRgba8>(count, { ToColorU8(vertexList.Constant[0]), ToColorU8(vertexList.Constant[1]),
                                                ToColorU8(vertexList.Constant[2]), ToColorU8(vertexList.Constant[3]) });
    }
    if (data.Length == 0) {
        return std::vector<ColorRgba8>(count, { 255, 255, 255, 255 });
    }

    std::vector<ColorRgba8> values;
    for (const auto& row : ReadAttributeValues(view, vatrOffset, vertexList, data, count, 4)) {
        values.push_back({ ToColorU8(row[0]), ToColorU8(row[1]), ToColorU8(row[2]), ToColorU8(row[3]) });
    }
    return values;
}

std::vector<uint32_t> ReadIndices(const BinaryView& view, size_t offset, size_t count, uint16_t dataType) {
    const auto size = DataTypeSize(dataType);
    std::vector<uint32_t> indices;
    indices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        indices.push_back(static_cast<uint32_t>(ReadScalar(view, offset + i * size, dataType)));
    }
    return indices;
}

uint16_t InferAttributeWidth(size_t byteCount, size_t vertexCount, uint16_t dataType) {
    if (vertexCount == 0) {
        return 0;
    }
    const auto size = DataTypeSize(dataType);
    uint16_t best = 0;
    bool matched = false;
    for (uint16_t width = 0; width <= 16; ++width) {
        const auto needed = vertexCount * width * size;
        if (needed <= byteCount && byteCount - needed <= 3) {
            best = width;
            matched = true;
        }
    }
    return matched ? best : 0;
}

std::array<VertexListData, 8> ParseVlds(const BinaryView& view, size_t vatrOffset) {
    std::array<VertexListData, 8> data{};
    for (size_t slot = 0; slot < data.size(); ++slot) {
        data[slot] = ParseVld(view, vatrOffset + 0x0C + slot * 8);
    }
    return data;
}

VertexAttributeInfo ReadVertexAttributeInfo(const BinaryView& view, size_t vatrOffset,
                                            const std::vector<size_t>& sepdOffsets, size_t sepdOffset,
                                            uint16_t slot, const std::array<VertexListData, 8>& vlds,
                                            size_t vertexCount) {
    const uint16_t flags = view.U16(sepdOffset + 0x0A);
    const uint16_t autoFlags = view.U16(sepdOffset + 0x106);
    VertexAttributeInfo info;
    info.Slot = slot;
    if ((flags & (1U << slot)) == 0) {
        return info;
    }

    const auto list = ParseVertexList(view, sepdOffset + 0x24 + slot * 0x1C);
    info.DataType = list.DataType;
    info.Scale = list.Scale;
    info.Mode = list.Mode;
    info.Offset = list.Offset;
    info.Constants = list.Constant;
    if ((autoFlags & (1U << slot)) != 0 || list.Mode != 0) {
        info.Kind = AttributeKind::Constant;
        return info;
    }

    std::vector<uint32_t> enabledOffsets;
    for (const auto other : sepdOffsets) {
        const uint16_t otherFlags = view.U16(other + 0x0A);
        const uint16_t otherAutoFlags = view.U16(other + 0x106);
        if ((otherFlags & (1U << slot)) != 0 && (otherAutoFlags & (1U << slot)) == 0) {
            enabledOffsets.push_back(view.U32(other + 0x24 + slot * 0x1C));
        }
    }
    std::sort(enabledOffsets.begin(), enabledOffsets.end());
    enabledOffsets.erase(std::unique(enabledOffsets.begin(), enabledOffsets.end()), enabledOffsets.end());

    uint32_t end = vlds[slot].Length;
    for (auto candidate : enabledOffsets) {
        if (candidate > list.Offset) {
            end = candidate;
            break;
        }
    }
    info.Kind = AttributeKind::Data;
    info.ByteCount = list.Offset <= end ? end - list.Offset : 0;
    info.Width = InferAttributeWidth(info.ByteCount, vertexCount, info.DataType);
    info.Start = vatrOffset + vlds[slot].Offset + list.Offset;
    return info;
}

uint16_t ChooseInfluenceWidth(const VertexAttributeInfo& indexAttr, const VertexAttributeInfo& weightAttr) {
    if (indexAttr.Kind == AttributeKind::Data && weightAttr.Kind == AttributeKind::Data &&
        indexAttr.Width == weightAttr.Width) {
        return indexAttr.Width;
    }
    if (indexAttr.Kind == AttributeKind::Data && weightAttr.Kind == AttributeKind::Constant) {
        return indexAttr.Width;
    }
    if (weightAttr.Kind == AttributeKind::Data && indexAttr.Kind == AttributeKind::Constant) {
        return weightAttr.Width;
    }
    if (indexAttr.Kind == AttributeKind::Constant && weightAttr.Kind == AttributeKind::Constant) {
        for (uint16_t width = 4; width > 0; --width) {
            float sum = 0.0f;
            for (uint16_t i = 0; i < width; ++i) {
                sum += weightAttr.Constants[i];
            }
            bool tailZero = true;
            for (uint16_t i = width; i < 4; ++i) {
                tailZero = tailZero && std::abs(weightAttr.Constants[i]) <= 0.001f;
            }
            if (std::abs(sum - 100.0f) <= 0.001f && tailZero) {
                return width;
            }
        }
        uint16_t width = 0;
        for (auto value : weightAttr.Constants) {
            if (std::abs(value) > 0.001f) {
                ++width;
            }
        }
        return width;
    }
    return 0;
}

std::vector<float> ReadAttributeRow(const BinaryView& view, const VertexAttributeInfo& attr, uint16_t width,
                                    size_t vertexIndex) {
    if (attr.Kind == AttributeKind::Constant) {
        std::vector<float> row;
        row.reserve(width);
        for (uint16_t i = 0; i < width; ++i) {
            row.push_back(attr.Constants[i]);
        }
        return row;
    }
    if (attr.Kind != AttributeKind::Data) {
        throw std::runtime_error(view.Source + ": missing vertex skinning attribute");
    }
    if (attr.DataType != kPicaU8) {
        throw std::runtime_error(view.Source + ": expected U8 skinning attribute data");
    }
    std::vector<float> row;
    row.reserve(width);
    for (uint16_t i = 0; i < width; ++i) {
        row.push_back(static_cast<float>(view.U8(attr.Start + vertexIndex * width + i)) * attr.Scale);
    }
    return row;
}

std::vector<CmbVertexInfluence> VertexInfluencesForIndex(const BinaryView& view, const CmbPrimitive& primitive,
                                                         const VertexAttributeInfo& indexAttr,
                                                         const VertexAttributeInfo& weightAttr,
                                                         uint16_t width, size_t vertexIndex) {
    if (primitive.SkinningMode == 0) {
        if (primitive.BoneIndices.empty()) {
            return {};
        }
        return { { 0, primitive.BoneIndices.front(), 1.0f } };
    }

    auto paletteIndices = ReadAttributeRow(view, indexAttr, width, vertexIndex);
    std::vector<float> weights;
    if (primitive.SkinningMode == 1) {
        weights.assign(width, 1.0f);
    } else {
        weights = ReadAttributeRow(view, weightAttr, width, vertexIndex);
        if (weightAttr.Kind == AttributeKind::Constant) {
            for (auto& weight : weights) {
                weight /= 100.0f;
            }
        }
    }

    std::vector<CmbVertexInfluence> influences;
    influences.reserve(width);
    for (uint16_t i = 0; i < width && i < paletteIndices.size() && i < weights.size(); ++i) {
        const auto paletteIndex = static_cast<uint16_t>(std::max(0, static_cast<int>(std::lround(paletteIndices[i]))));
        if (paletteIndex >= primitive.BoneIndices.size()) {
            continue;
        }
        float weight = weights[i];
        if (weight > 1.5f) {
            weight /= 100.0f;
        }
        if (weight <= 0.000001f) {
            continue;
        }
        influences.push_back({ paletteIndex, primitive.BoneIndices[paletteIndex], weight });
    }
    return influences;
}

void PopulatePrimitiveInfluences(const BinaryView& view, size_t vatrOffset, const std::vector<size_t>& sepdOffsets,
                                 size_t sepdOffset, const std::array<VertexListData, 8>& vlds, size_t vertexCount,
                                 std::vector<CmbPrimitive>& primitives) {
    const auto indexAttr =
        ReadVertexAttributeInfo(view, vatrOffset, sepdOffsets, sepdOffset, kSkinningIndexSlot, vlds, vertexCount);
    const auto weightAttr =
        ReadVertexAttributeInfo(view, vatrOffset, sepdOffsets, sepdOffset, kSkinningWeightSlot, vlds, vertexCount);
    const uint16_t width = ChooseInfluenceWidth(indexAttr, weightAttr);
    for (auto& primitive : primitives) {
        primitive.VertexInfluences.clear();
        primitive.VertexInfluences.resize(vertexCount);
        if (vertexCount == 0) {
            continue;
        }
        if (primitive.SkinningMode == 0) {
            for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                primitive.VertexInfluences[vertexIndex] =
                    VertexInfluencesForIndex(view, primitive, indexAttr, weightAttr, 1, vertexIndex);
            }
        } else if (primitive.SkinningMode == 1) {
            for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                primitive.VertexInfluences[vertexIndex] =
                    VertexInfluencesForIndex(view, primitive, indexAttr, weightAttr, 1, vertexIndex);
            }
        } else if (primitive.SkinningMode == 2 && width > 0) {
            for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                primitive.VertexInfluences[vertexIndex] =
                    VertexInfluencesForIndex(view, primitive, indexAttr, weightAttr, width, vertexIndex);
            }
        }
    }
}

CmbSkeleton ParseSkeleton(const BinaryView& view, size_t offset) {
    if (!view.HasMagic(offset, "skl ")) {
        throw std::runtime_error(view.Source + ": expected SKL chunk");
    }
    CmbSkeleton skeleton;
    skeleton.ChunkSize = view.U32(offset + 0x04);
    const auto boneCount = view.U32(offset + 0x08);
    skeleton.HeaderWord0C = view.U32(offset + 0x0C);
    const auto expectedSize = 0x10 + boneCount * 0x28;
    if (skeleton.ChunkSize != expectedSize) {
        throw std::runtime_error(view.Source + ": SKL chunk size does not match bone count");
    }
    view.Require(offset, skeleton.ChunkSize);

    skeleton.Bones.reserve(boneCount);
    for (uint32_t index = 0; index < boneCount; ++index) {
        const auto base = offset + 0x10 + index * 0x28;
        skeleton.Bones.push_back({
            view.U16(base),
            view.S16(base + 0x02),
            { view.F32(base + 0x04), view.F32(base + 0x08), view.F32(base + 0x0C) },
            { view.F32(base + 0x10), view.F32(base + 0x14), view.F32(base + 0x18) },
            { view.F32(base + 0x1C), view.F32(base + 0x20), view.F32(base + 0x24) },
        });
    }
    return skeleton;
}

size_t CountLutPointsLessOrEqual(const CmbLutRecord& record, float input) {
    size_t count = 0;
    while (count < record.Points.size() && static_cast<float>(record.Points[count].X) <= input) {
        ++count;
    }
    return count;
}

float EvaluateLinearLutRecord(const CmbLutRecord& record, float input) {
    if (record.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (record.Points.size() == 1) {
        return record.Points.front().Value;
    }

    const auto upper = CountLutPointsLessOrEqual(record, input);
    if (upper == 0) {
        return record.Points.front().Value;
    }
    if (upper >= record.Points.size()) {
        return record.Points.back().Value;
    }

    const auto& previous = record.Points[upper - 1];
    const auto& current = record.Points[upper];
    const auto deltaX = static_cast<float>(current.X - previous.X);
    if (deltaX <= 0.0f) {
        return current.Value;
    }
    const auto t = (input - static_cast<float>(previous.X)) / deltaX;
    return previous.Value + (current.Value - previous.Value) * t;
}

float EvaluateStepLutRecord(const CmbLutRecord& record, float input) {
    if (record.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (record.Points.size() == 1) {
        return record.Points.front().Value;
    }

    const auto upper = CountLutPointsLessOrEqual(record, input);
    if (upper == 0) {
        return record.Points.front().Value;
    }
    return record.Points[upper - 1].Value;
}

float EvaluateHermiteLutRecord(const CmbLutRecord& record, float input) {
    if (record.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (record.Points.size() == 1) {
        return record.Points.front().Value;
    }

    const auto upper = CountLutPointsLessOrEqual(record, input);
    if (upper == 0) {
        return kCmbLutClampMinimum;
    }
    if (upper >= record.Points.size()) {
        return record.Points.back().Value;
    }

    const auto& previous = record.Points[upper - 1];
    const auto& current = record.Points[upper];
    const auto previousX = static_cast<float>(previous.X);
    const auto currentX = static_cast<float>(current.X);
    const auto deltaX = currentX - previousX;
    if (deltaX <= 0.0f) {
        return current.Value;
    }

    const auto xFromPrevious = input - previousX;
    const auto t = xFromPrevious / deltaX;
    const auto tMinusOne = t - 1.0f;
    const auto valueDeltaTerm = (current.Value - previous.Value) * (3.0f - 2.0f * t) * t * t;
    const auto tangentTerm =
        xFromPrevious * tMinusOne * (tMinusOne * previous.TangentOut + t * current.TangentIn);
    return previous.Value + valueDeltaTerm + tangentTerm;
}

float EvaluateCmbLutRecord(const CmbLutRecord& record, float input) {
    switch (record.Type) {
        case 1:
            return EvaluateLinearLutRecord(record, input);
        case 2:
            return EvaluateHermiteLutRecord(record, input);
        case 3:
            return EvaluateStepLutRecord(record, input);
        default:
            return kCmbLutClampMinimum;
    }
}

void DecodeCmbLutRecordPayload(CmbLutRecord& record, const BinaryView& view, size_t recordOffset) {
    if (record.Size < kCmbLutHeaderSize) {
        return;
    }

    record.Type = view.U8(recordOffset);
    record.HeaderByte01 = view.U8(recordOffset + 0x01);
    record.HeaderByte02 = view.U8(recordOffset + 0x02);
    record.HeaderByte03 = view.U8(recordOffset + 0x03);
    record.PointCount = view.U32(recordOffset + 0x04);
    record.HeaderWord08 = view.U32(recordOffset + 0x08);
    record.HeaderWord0C = view.U32(recordOffset + 0x0C);

    switch (record.Type) {
        case 1:
        case 3:
            record.PointStrideBytes = kCmbLutLinearPointStride;
            break;
        case 2:
            record.PointStrideBytes = kCmbLutHermitePointStride;
            break;
        default:
            return;
    }

    const auto requiredSize = kCmbLutHeaderSize + static_cast<size_t>(record.PointCount) * record.PointStrideBytes;
    if (requiredSize > record.Size) {
        throw std::runtime_error(view.Source + ": LUTS record point table exceeds record");
    }

    record.Points.reserve(record.PointCount);
    for (uint32_t pointIndex = 0; pointIndex < record.PointCount; ++pointIndex) {
        const auto pointOffset = recordOffset + kCmbLutHeaderSize + pointIndex * record.PointStrideBytes;
        CmbLutRecord::Point point;
        point.X = view.S32(pointOffset + 0x00);
        point.Value = view.F32(pointOffset + 0x04);
        if (record.Type == 2) {
            point.TangentIn = view.F32(pointOffset + 0x08);
            point.TangentOut = view.F32(pointOffset + 0x0C);
        }
        if (!record.Points.empty() && point.X <= record.Points.back().X) {
            throw std::runtime_error(view.Source + ": LUTS record point X values are not ordered");
        }
        record.Points.push_back(point);
    }

    if (record.Points.empty()) {
        return;
    }

    record.Samples.reserve(kCmbLutSourceSampleCount);
    for (uint32_t sample = 0; sample < kCmbLutSourceSampleCount; ++sample) {
        record.Samples.push_back(std::max(EvaluateCmbLutRecord(record, static_cast<float>(sample)),
                                          kCmbLutClampMinimum));
    }

    record.PackedBaseValues.reserve(kCmbLutPackedValueCount);
    record.PackedDeltaValues.reserve(kCmbLutPackedValueCount);
    for (uint32_t sample = 0; sample < kCmbLutPackedValueCount; ++sample) {
        record.PackedBaseValues.push_back(record.Samples[sample]);
        record.PackedDeltaValues.push_back(record.Samples[sample + 1] - record.Samples[sample]);
    }
}

CmbLutSection ParseLuts(const BinaryView& view, size_t offset) {
    CmbLutSection luts;
    if (offset == 0) {
        return luts;
    }
    if (!view.HasMagic(offset, "luts")) {
        throw std::runtime_error(view.Source + ": expected LUTS chunk");
    }

    luts.Decoded = true;
    luts.SourceOffset = static_cast<uint32_t>(offset);
    luts.ChunkSize = view.U32(offset + 0x04);
    luts.Count = view.U32(offset + 0x08);
    luts.HeaderWord0C = view.U32(offset + 0x0C);
    if (luts.ChunkSize < 0x10) {
        throw std::runtime_error(view.Source + ": LUTS chunk is too small");
    }
    view.Require(offset, luts.ChunkSize);
    if (0x10 + static_cast<size_t>(luts.Count) * sizeof(uint32_t) > luts.ChunkSize) {
        throw std::runtime_error(view.Source + ": LUTS record offset table exceeds chunk");
    }

    luts.Records.reserve(luts.Count);
    for (uint32_t index = 0; index < luts.Count; ++index) {
        const uint32_t relativeOffset = view.U32(offset + 0x10 + index * sizeof(uint32_t));
        const size_t minimumRecordOffset = 0x10 + static_cast<size_t>(luts.Count) * sizeof(uint32_t);
        if (relativeOffset < minimumRecordOffset || relativeOffset >= luts.ChunkSize) {
            throw std::runtime_error(view.Source + ": LUTS record offset is outside chunk");
        }
        uint32_t relativeEnd = luts.ChunkSize;
        if (index + 1 < luts.Count) {
            relativeEnd = view.U32(offset + 0x10 + (index + 1) * sizeof(uint32_t));
            if (relativeEnd <= relativeOffset || relativeEnd > luts.ChunkSize) {
                throw std::runtime_error(view.Source + ": LUTS record offsets are not ordered");
            }
        }

        const auto recordOffset = offset + relativeOffset;
        const auto recordSize = relativeEnd - relativeOffset;
        CmbLutRecord record;
        record.Index = index;
        record.SourceOffset = relativeOffset;
        record.Size = recordSize;
        record.RawRecord.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(recordOffset),
                                view.Bytes.begin() + static_cast<std::ptrdiff_t>(recordOffset + recordSize));
        DecodeCmbLutRecordPayload(record, view, recordOffset);
        luts.Records.push_back(std::move(record));
    }

    return luts;
}

std::vector<CmbTexture> ParseTextures(const BinaryView& view, size_t texOffset, size_t textureDataOffset) {
    if (!view.HasMagic(texOffset, "tex ")) {
        throw std::runtime_error(view.Source + ": expected TEX chunk");
    }
    const auto textureCount = view.U32(texOffset + 0x08);
    std::vector<CmbTexture> textures;
    textures.reserve(textureCount);
    for (uint32_t index = 0; index < textureCount; ++index) {
        const auto entry = texOffset + 0x0C + index * 0x24;
        CmbTexture texture;
        texture.Index = index;
        texture.DataSize = view.U32(entry);
        texture.MipmapCount = std::max<uint32_t>(1, view.U16(entry + 0x04));
        texture.Width = view.U16(entry + 0x08);
        texture.Height = view.U16(entry + 0x0A);
        texture.TextureFormat = view.U16(entry + 0x0C);
        texture.DataType = view.U16(entry + 0x0E);
        texture.DataOffset = view.U32(entry + 0x10);
        texture.Name = view.Cstr(entry + 0x14, 0x10);
        const auto dataStart = textureDataOffset + texture.DataOffset;
        view.Require(dataStart, texture.DataSize);
        texture.Data.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(dataStart),
                            view.Bytes.begin() + static_cast<std::ptrdiff_t>(dataStart + texture.DataSize));
        texture.Rgba8 = DecodeTextureRgba8(texture);
        texture.Rgba8Decoded = texture.Rgba8.size() == static_cast<size_t>(texture.Width) * texture.Height * 4;
        DecodeTextureMipChain(texture);
        textures.push_back(std::move(texture));
    }
    return textures;
}

CmbMaterialTextureMapper ReadMaterialTexture(const BinaryView& view, size_t offset) {
    return { view.S16(offset), view.U16(offset + 0x04), view.U16(offset + 0x06), view.U16(offset + 0x08),
             view.U16(offset + 0x0A), view.F32(offset + 0x10) };
}

CmbTextureCoord ReadTextureCoord(const BinaryView& view, size_t offset) {
    return {
        view.U8(offset),
        view.U8(offset + 0x01),
        view.U8(offset + 0x02),
        view.U8(offset + 0x03),
        { view.F32(offset + 0x04), view.F32(offset + 0x08) },
        view.F32(offset + 0x14),
        { view.F32(offset + 0x0C), view.F32(offset + 0x10) },
    };
}

ColorRgba8 ReadColorRgba8(const BinaryView& view, size_t offset) {
    return {
        view.U8(offset),
        view.U8(offset + 0x01),
        view.U8(offset + 0x02),
        view.U8(offset + 0x03),
    };
}

CmbMaterialTextureEnvSetting ReadMaterialTextureEnvSetting(const BinaryView& view, size_t offset, uint32_t index) {
    CmbMaterialTextureEnvSetting textureEnv;
    textureEnv.Index = index;
    textureEnv.SourceOffset = static_cast<uint32_t>(offset);
    textureEnv.Decoded = true;
    textureEnv.RawTextureEnv.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                    view.Bytes.begin() + static_cast<std::ptrdiff_t>(
                                                           offset + kOot3dCmbMaterialTextureEnvSize));
    textureEnv.CombineRgb = view.U16(offset);
    textureEnv.CombineAlpha = view.U16(offset + 0x02);
    textureEnv.ColorScale = view.U16(offset + 0x04);
    textureEnv.AlphaScale = view.U16(offset + 0x06);
    textureEnv.UnknownUshort1 = { textureEnv.ColorScale, textureEnv.AlphaScale };
    textureEnv.UnknownGlConstant = { view.U16(offset + 0x08), view.U16(offset + 0x0A) };
    for (size_t index = 0; index < textureEnv.SourceRgb.size(); ++index) {
        textureEnv.SourceRgb[index] = view.U16(offset + 0x0C + index * sizeof(uint16_t));
        textureEnv.OperandRgb[index] = view.U16(offset + 0x12 + index * sizeof(uint16_t));
        textureEnv.SourceAlpha[index] = view.U16(offset + 0x18 + index * sizeof(uint16_t));
        textureEnv.OperandAlpha[index] = view.U16(offset + 0x1E + index * sizeof(uint16_t));
    }
    textureEnv.UnknownUshort2 = { view.U16(offset + 0x24), view.U16(offset + 0x26) };
    return textureEnv;
}

std::pair<uint32_t, bool> DecodeLinearNativeEnum(uint16_t value, uint16_t base, uint32_t count) {
    if (value >= base && value < base + count) {
        return { static_cast<uint32_t>(value - base), true };
    }
    return { 0, false };
}

uint32_t DecodePicaLutInputAbsDisableBit(uint32_t selector) {
    return selector <= 1 ? 1 - selector : 0;
}

std::pair<uint32_t, bool> DecodePicaLightingConfig(uint16_t value) {
    auto decoded = DecodeLinearNativeEnum(
        value, kOot3dPicaLightingConfigNativeBase, kOot3dPicaLightingConfigNativeCount);
    if (decoded.second) {
        return decoded;
    }
    if (value == kOot3dPicaLightingConfig7Native) {
        return { kOot3dPicaLightingConfig7EncodedValue, true };
    }
    return { 0, false };
}

std::pair<uint32_t, bool> DecodePicaLutScale(uint32_t bits) {
    switch (bits) {
        case 0x3F800000:
            return { 0, true };
        case 0x40000000:
            return { 1, true };
        case 0x40800000:
            return { 2, true };
        case 0x41000000:
            return { 3, true };
        case 0x3E800000:
            return { 6, true };
        case 0x3F000000:
            return { 7, true };
        default:
            return { 0, false };
    }
}

CmbMaterialLightingBlock ReadMaterialLightingBlock(const BinaryView& view, size_t materialBase) {
    CmbMaterialLightingBlock block;
    constexpr size_t sourceOffset = kOot3dCmbMaterialLightingBlockOffset;
    constexpr size_t blockSize = kOot3dCmbMaterialLightingBlockMinSize;
    const size_t base = materialBase + sourceOffset;

    block.Decoded = true;
    block.SourceOffset = static_cast<uint32_t>(sourceOffset);
    block.Size = static_cast<uint32_t>(blockSize);
    block.RawBlock.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(base),
                          view.Bytes.begin() + static_cast<std::ptrdiff_t>(base + blockSize));

    block.PicaBumpTextureUnitRaw = view.U16(base + 0x10);
    auto bumpTextureUnit = DecodeLinearNativeEnum(
        block.PicaBumpTextureUnitRaw,
        kOot3dPicaBumpTextureUnitNativeBase,
        kOot3dPicaBumpTextureUnitNativeCount);
    block.PicaBumpTextureUnit = bumpTextureUnit.first;
    block.PicaBumpTextureUnitRecognized = bumpTextureUnit.second;

    block.PicaBumpModeRaw = view.U16(base + 0x12);
    auto bumpMode = DecodeLinearNativeEnum(
        block.PicaBumpModeRaw, kOot3dPicaBumpModeNativeBase, kOot3dPicaBumpModeNativeCount);
    block.PicaBumpMode = bumpMode.first;
    block.PicaBumpModeRecognized = bumpMode.second;

    block.Flag1Raw = view.U8(base + 0x14);
    block.Flag1 = block.Flag1Raw != 0;

    block.PicaLightingConfigRaw = view.U16(base + 0x18);
    auto lightingConfig = DecodePicaLightingConfig(block.PicaLightingConfigRaw);
    block.PicaLightingConfig = lightingConfig.first;
    block.PicaLightingConfigRecognized = lightingConfig.second;

    block.UnresolvedEnum4Raw = view.U16(base + 0x1C);
    auto enum4 = DecodeLinearNativeEnum(
        block.UnresolvedEnum4Raw,
        kOot3dPica62C0SelectorNativeBase,
        kOot3dPica62C0SelectorNativeCount);
    block.UnresolvedEnum4Encoded = enum4.first;
    block.UnresolvedEnum4Recognized = enum4.second;
    block.Pica62C0SelectorRaw = block.UnresolvedEnum4Raw;
    block.Pica62C0Selector = block.UnresolvedEnum4Encoded;
    block.Pica62C0SelectorRecognized = block.UnresolvedEnum4Recognized;
    block.PicaLutInputAbsD0Raw = block.UnresolvedEnum4Raw;
    block.PicaLutInputAbsD0Selector = block.UnresolvedEnum4Encoded;
    block.PicaLutInputAbsD0SelectorRecognized = block.UnresolvedEnum4Recognized;
    block.PicaLutInputAbsD0DisableBit =
        DecodePicaLutInputAbsDisableBit(block.PicaLutInputAbsD0Selector);
    block.PicaLutInputAbsD0DisableBitResolved = block.PicaLutInputAbsD0SelectorRecognized;

    block.Flag2Raw = view.U8(base + 0x1E);
    block.Flag2 = block.Flag2Raw != 0;
    block.PicaLutInputAbsSpDisableBit =
        DecodePicaLutInputAbsDisableBit(block.Flag1 ? 1 : 0);
    block.PicaLutInputAbsSpDisableBitResolved = true;
    block.PicaLutScaleSp = block.Flag2 ? 1 : 0;
    block.PicaLutScaleSpResolved = true;
    block.Flag3Raw = view.U8(base + 0x1F);
    block.Flag3 = block.Flag3Raw != 0;
    block.Flag4Raw = view.U8(base + 0x20);
    block.Flag4 = block.Flag4Raw != 0;
    block.Flag5Raw = view.U8(base + 0x23);
    block.Flag5 = block.Flag5Raw != 0;
    block.PicaLutInputFr = block.Flag4 ? 1 : 0;
    block.PicaLutInputFrResolved = true;
    block.PicaLutInputAbsFrDisableBit =
        DecodePicaLutInputAbsDisableBit(block.Flag5 ? 1 : 0);
    block.PicaLutInputAbsFrDisableBitResolved = true;
    block.Flag0Raw = view.U8(base + 0x24);
    block.Flag0 = block.Flag0Raw != 0;
    block.PicaLutInputAbsRbDisableBit =
        DecodePicaLutInputAbsDisableBit(block.Flag0 ? 1 : 0);
    block.PicaLutInputAbsRbDisableBitResolved = true;

    block.PicaLutInputRaw = view.U16(base + 0x26);
    auto lutInput = DecodeLinearNativeEnum(
        block.PicaLutInputRaw,
        kOot3dPicaLightingLutInputNativeBase,
        kOot3dPicaLightingLutInputNativeCount);
    block.PicaLutInput = lutInput.first;
    block.PicaLutInputRecognized = lutInput.second;
    block.PicaLutInputRb = block.PicaLutInput;
    block.PicaLutInputRbRecognized = block.PicaLutInputRecognized;

    block.PicaLutScaleSourceBits = view.U32(base + 0x28);
    block.PicaLutScaleSourceValue = view.F32(base + 0x28);
    auto lutScale = DecodePicaLutScale(block.PicaLutScaleSourceBits);
    block.PicaLutScale = lutScale.first;
    block.PicaLutScaleRecognized = lutScale.second;
    block.PicaLutScaleRb = block.PicaLutScale;
    block.PicaLutScaleRbRecognized = block.PicaLutScaleRecognized;
    return block;
}

struct ParsedCmbMaterials {
    std::vector<CmbMaterial> Materials;
    std::vector<CmbMaterialTextureEnvSetting> TextureEnvSettings;
};

ParsedCmbMaterials ParseMaterials(const BinaryView& view, size_t matsOffset, size_t texOffset) {
    if (!view.HasMagic(matsOffset, "mats")) {
        throw std::runtime_error(view.Source + ": expected MATS chunk");
    }
    const auto materialCount = view.U32(matsOffset + 0x08);
    const auto materialsStart = matsOffset + 0x0C;
    const auto materialsEnd = materialsStart + materialCount * kOot3dCmbMaterialSize;
    if (materialsEnd > texOffset) {
        throw std::runtime_error(view.Source + ": MATS table exceeds TEX chunk");
    }
    const auto textureEnvStart = materialsEnd;
    const auto textureEnvBytes = texOffset - textureEnvStart;
    if (textureEnvBytes % kOot3dCmbMaterialTextureEnvSize != 0) {
        throw std::runtime_error(view.Source + ": MATS texture environment table is not record-aligned");
    }
    const auto textureEnvCount = textureEnvBytes / kOot3dCmbMaterialTextureEnvSize;

    ParsedCmbMaterials parsed;
    parsed.TextureEnvSettings.reserve(textureEnvCount);
    for (uint32_t index = 0; index < textureEnvCount; ++index) {
        parsed.TextureEnvSettings.push_back(ReadMaterialTextureEnvSetting(
            view, textureEnvStart + index * kOot3dCmbMaterialTextureEnvSize, index));
    }

    parsed.Materials.reserve(materialCount);
    for (uint32_t index = 0; index < materialCount; ++index) {
        const auto base = materialsStart + index * kOot3dCmbMaterialSize;
        CmbMaterial material;
        material.Index = index;
        material.RawMaterial.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(base),
                                    view.Bytes.begin() + static_cast<std::ptrdiff_t>(base + kOot3dCmbMaterialSize));
        material.LightingBlock = ReadMaterialLightingBlock(view, base);
        material.FragmentLightingEnabled = view.U8(base + 0x00) != 0;
        material.VertexLightingEnabled = view.U8(base + 0x01) != 0;
        material.HemisphereLightingEnabled = view.U8(base + 0x02) != 0;
        material.HemisphereOcclusionEnabled = view.U8(base + 0x03) != 0;
        material.RawTextureStageSelectorDecoded = true;
        material.RawTextureStageCount = view.U32(base + kOot3dCmbMaterialRawTextureStageCountOffset);
        for (size_t slot = 0; slot < material.RawTextureStageSlots.size(); ++slot) {
            material.RawTextureStageSlots[slot] =
                view.S16(base + kOot3dCmbMaterialRawTextureStageIndexOffset + slot * sizeof(uint16_t));
        }
        material.PostMaterialTextureEnvTableDecoded = true;
        material.PostMaterialTextureEnvTableDerivedFromLanePointer = true;
        material.PostMaterialTextureEnvTableSourceOffset = static_cast<uint32_t>(textureEnvStart);
        material.PostMaterialTextureEnvTableRecordSize = kOot3dCmbMaterialTextureEnvSize;
        material.PostMaterialTextureEnvTableRecordCount = static_cast<uint32_t>(textureEnvCount);
        material.CullFace = view.U8(base + 0x04);
        material.PicaCullMode = DecodeOot3dCmbPicaCullMode(material.CullFace);
        material.TextureMappersUsed = view.U32(base + 0x08);
        material.TextureCoordsUsed = view.U32(base + 0x0C);
        for (size_t textureIndex = 0; textureIndex < 3; ++textureIndex) {
            material.TextureMappers[textureIndex] = ReadMaterialTexture(view, base + 0x10 + textureIndex * 0x18);
            material.TextureCoords[textureIndex] = ReadTextureCoord(view, base + 0x58 + textureIndex * 0x18);
        }
        material.MaterialColorsDecoded = true;
        material.EmissionColor = ReadColorRgba8(view, base + 0x0A0);
        material.AmbientColor = ReadColorRgba8(view, base + 0x0A4);
        material.DiffuseColor = ReadColorRgba8(view, base + 0x0A8);
        material.Specular0Color = ReadColorRgba8(view, base + 0x0AC);
        material.Specular1Color = ReadColorRgba8(view, base + 0x0B0);
        for (size_t colorIndex = 0; colorIndex < material.ConstantColors.size(); ++colorIndex) {
            material.ConstantColors[colorIndex] = ReadColorRgba8(view, base + 0x0B4 + colorIndex * 0x04);
        }
        material.AlphaTest = view.U8(base + 0x130) != 0;
        material.AlphaReference = view.U8(base + 0x131);
        material.AlphaFunction = view.U16(base + 0x132);
        material.DepthTest = view.U8(base + 0x134) != 0;
        material.DepthWrite = view.U8(base + 0x135) != 0;
        material.DepthFunction = view.U16(base + 0x136);
        material.BlendMode = view.U32(base + 0x138);
        material.BlendSrc = view.U16(base + 0x13C);
        material.BlendDst = view.U16(base + 0x13E);
        material.BlendEquation = view.U16(base + 0x140);
        material.ColorBlendSrc = view.U16(base + 0x144);
        material.ColorBlendDst = view.U16(base + 0x146);
        material.ColorBlendEquation = view.U16(base + 0x148);
        material.BlendColorAlpha = view.F32(base + 0x158);
        const size_t stageCount = std::min<size_t>(material.RawTextureStageCount, material.RawTextureStageSlots.size());
        for (size_t stage = 0; stage < stageCount; ++stage) {
            const int stageIndex = material.RawTextureStageSlots[stage];
            if (stageIndex >= 0 && static_cast<size_t>(stageIndex) < parsed.TextureEnvSettings.size()) {
                const auto& textureEnv = parsed.TextureEnvSettings[static_cast<size_t>(stageIndex)];
                material.PostMaterialTextureEnvStageResolved[stage] = true;
                material.PostMaterialTextureEnvStageSourceOffsets[stage] = textureEnv.SourceOffset;
                material.TextureEnvStages.push_back(textureEnv);
            }
        }
        if (!material.TextureEnvStages.empty()) {
            material.TextureEnv = material.TextureEnvStages.front();
        }
        parsed.Materials.push_back(material);
    }
    return parsed;
}

std::vector<CmbMesh> ParseMeshes(const BinaryView& view, size_t offset, int16_t& passSplitIndex) {
    if (!view.HasMagic(offset, "mshs")) {
        throw std::runtime_error(view.Source + ": expected MSHS chunk");
    }
    const auto meshCount = view.U32(offset + 0x08);
    passSplitIndex = view.S16(offset + 0x0C);
    if (passSplitIndex < 0 || static_cast<uint32_t>(passSplitIndex) > meshCount) {
        throw std::runtime_error(view.Source + ": CMB MSHS pass split index is out of range");
    }
    std::vector<CmbMesh> meshes;
    meshes.reserve(meshCount);
    for (uint32_t index = 0; index < meshCount; ++index) {
        const auto base = offset + 0x10 + index * 4;
        meshes.push_back({ index, view.U16(base), view.U8(base + 0x02), view.U8(base + 0x03) });
    }
    return meshes;
}

CmbPrimitive ParsePrms(const BinaryView& view, size_t prmsOffset, const std::vector<uint32_t>& allU16Indices,
                       size_t indicesOffset) {
    if (!view.HasMagic(prmsOffset, "prms")) {
        throw std::runtime_error(view.Source + ": expected PRMS chunk");
    }
    if (view.U32(prmsOffset + 0x08) != 1) {
        throw std::runtime_error(view.Source + ": only one PRM chunk per PRMS is supported");
    }

    CmbPrimitive primitive;
    primitive.SkinningMode = view.U16(prmsOffset + 0x0C);
    const auto boneCount = view.U16(prmsOffset + 0x0E);
    const auto boneOffset = prmsOffset + view.U32(prmsOffset + 0x10);
    const auto prmOffset = prmsOffset + view.U32(prmsOffset + 0x14);
    primitive.BoneIndices.reserve(boneCount);
    for (uint16_t index = 0; index < boneCount; ++index) {
        primitive.BoneIndices.push_back(view.U16(boneOffset + index * 2));
    }

    if (!view.HasMagic(prmOffset, "prm ")) {
        throw std::runtime_error(view.Source + ": expected PRM chunk");
    }
    const bool visible = view.U32(prmOffset + 0x08) != 0;
    const auto primitiveMode = view.U32(prmOffset + 0x0C);
    const auto dataType = view.U16(prmOffset + 0x10);
    const auto count = view.U16(prmOffset + 0x14);
    const auto first = view.U16(prmOffset + 0x16);
    if (!visible) {
        return primitive;
    }
    if (primitiveMode != 0) {
        throw std::runtime_error(view.Source + ": only triangle primitive mode 0 is supported");
    }
    if (count % 3 != 0) {
        throw std::runtime_error(view.Source + ": PRM index count is not divisible by three");
    }
    if (dataType == kPicaU16) {
        if (first + count > allU16Indices.size()) {
            throw std::runtime_error(view.Source + ": PRM U16 index range exceeds CMB index table");
        }
        primitive.Indices.insert(primitive.Indices.end(), allU16Indices.begin() + first, allU16Indices.begin() + first + count);
    } else {
        primitive.Indices = ReadIndices(view, indicesOffset + first * DataTypeSize(dataType), count, dataType);
    }
    return primitive;
}

std::vector<CmbShape> ParseShapes(const BinaryView& view, size_t shpOffset, size_t vatrOffset, size_t indicesOffset,
                                  uint32_t indexCount) {
    if (!view.HasMagic(shpOffset, "shp ")) {
        throw std::runtime_error(view.Source + ": expected SHP chunk");
    }
    if (!view.HasMagic(vatrOffset, "vatr")) {
        throw std::runtime_error(view.Source + ": expected VATR chunk");
    }

    const auto shapeCount = view.U32(shpOffset + 0x08);
    const auto allIndices = ReadIndices(view, indicesOffset, indexCount, kPicaU16);
    const auto vlds = ParseVlds(view, vatrOffset);
    const VertexListData positionData = vlds[0];
    const VertexListData normalData = vlds[1];
    const VertexListData colorData = vlds[2];
    const VertexListData uv0Data = vlds[3];

    std::vector<size_t> sepdOffsets;
    sepdOffsets.reserve(shapeCount);
    for (uint32_t index = 0; index < shapeCount; ++index) {
        sepdOffsets.push_back(shpOffset + view.U16(shpOffset + 0x10 + index * 2));
    }

    std::vector<CmbShape> shapes;
    shapes.reserve(shapeCount);
    for (uint32_t index = 0; index < shapeCount; ++index) {
        const auto sepdOffset = sepdOffsets[index];
        if (!view.HasMagic(sepdOffset, "sepd")) {
            throw std::runtime_error(view.Source + ": expected SEPD chunk");
        }

        CmbShape shape;
        shape.Index = index;
        const auto prmsCount = view.U16(sepdOffset + 0x08);
        shape.Flags = view.U16(sepdOffset + 0x0A);
        shape.AutoFlags = view.U16(sepdOffset + 0x106);
        const auto positionList = ParseVertexList(view, sepdOffset + 0x24);
        const auto normalList = ParseVertexList(view, sepdOffset + 0x40);
        const auto colorList = ParseVertexList(view, sepdOffset + 0x5C);
        const auto uv0List = ParseVertexList(view, sepdOffset + 0x78);

        uint32_t maxIndex = 0;
        bool hasIndices = false;
        shape.Primitives.reserve(prmsCount);
        for (uint16_t prmsIndex = 0; prmsIndex < prmsCount; ++prmsIndex) {
            auto primitive = ParsePrms(view, sepdOffset + view.U16(sepdOffset + 0x108 + prmsIndex * 2), allIndices,
                                       indicesOffset);
            for (auto primitiveIndex : primitive.Indices) {
                maxIndex = std::max(maxIndex, primitiveIndex);
                hasIndices = true;
            }
            shape.Primitives.push_back(std::move(primitive));
        }

        const size_t vertexCount = hasIndices ? static_cast<size_t>(maxIndex) + 1 : 0;
        shape.Positions = ReadVec3Attribute(view, vatrOffset, positionList, positionData, vertexCount,
                                            shape.HasAttribute(kAttrPosition),
                                            shape.HasConstantAttribute(kAttrPosition), {});
        shape.Normals = ReadVec3Attribute(view, vatrOffset, normalList, normalData, vertexCount,
                                          shape.HasAttribute(kAttrNormal),
                                          shape.HasConstantAttribute(kAttrNormal), { 0.0f, 0.0f, 1.0f });
        shape.Colors = ReadColorAttribute(view, vatrOffset, colorList, colorData, vertexCount,
                                          shape.HasAttribute(kAttrColor), shape.HasConstantAttribute(kAttrColor));
        shape.Uv0 = ReadVec2Attribute(view, vatrOffset, uv0List, uv0Data, vertexCount,
                                      shape.HasAttribute(kAttrUv0), shape.HasConstantAttribute(kAttrUv0), {});
        PopulatePrimitiveInfluences(view, vatrOffset, sepdOffsets, sepdOffset, vlds, vertexCount, shape.Primitives);
        shapes.push_back(std::move(shape));
    }
    return shapes;
}

void ParseSklm(const BinaryView& view, size_t sklmOffset, size_t vatrOffset, size_t indicesOffset, uint32_t indexCount,
               std::vector<CmbMesh>& meshes, std::vector<CmbShape>& shapes, int16_t& meshPassSplitIndex) {
    if (!view.HasMagic(sklmOffset, "sklm")) {
        throw std::runtime_error(view.Source + ": expected SKLM chunk");
    }
    const auto mshsOffset = sklmOffset + view.U32(sklmOffset + 0x08);
    const auto shpOffset = sklmOffset + view.U32(sklmOffset + 0x0C);
    meshes = ParseMeshes(view, mshsOffset, meshPassSplitIndex);
    shapes = ParseShapes(view, shpOffset, vatrOffset, indicesOffset, indexCount);
}

bool LooksLikeCmbAt(std::span<const uint8_t> bytes, size_t offset) {
    if (offset + 12 > bytes.size()) {
        return false;
    }
    BinaryView view{ bytes, "<zsi>" };
    if (!view.HasMagic(offset, "cmb ")) {
        return false;
    }
    const auto declaredSize = view.U32(offset + 4);
    const auto version = view.U32(offset + 8);
    return declaredSize >= 0x50 && offset + declaredSize <= bytes.size() && version == 6;
}

size_t Align4(size_t value) {
    return (value + 3) & ~static_cast<size_t>(3);
}

std::string ReadCString(const BinaryView& view, size_t offset) {
    size_t end = offset;
    while (end < view.Bytes.size() && view.Bytes[end] != 0) {
        ++end;
    }
    view.Require(offset, end - offset);
    return std::string(reinterpret_cast<const char*>(view.Bytes.data() + offset), end - offset);
}

std::string NormalizeArchiveName(std::string_view value) {
    std::string normalized(value);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

Matrix4f IdentityMatrix() {
    Matrix4f matrix;
    for (size_t i = 0; i < 4; ++i) {
        matrix.M[i][i] = 1.0f;
    }
    return matrix;
}

Matrix4f MatrixScale(const Vec3f& scale) {
    Matrix4f matrix{};
    matrix.M[0][0] = scale.X;
    matrix.M[1][1] = scale.Y;
    matrix.M[2][2] = scale.Z;
    matrix.M[3][3] = 1.0f;
    return matrix;
}

Matrix4f MatrixTranslate(const Vec3f& translation) {
    auto matrix = IdentityMatrix();
    matrix.M[0][3] = translation.X;
    matrix.M[1][3] = translation.Y;
    matrix.M[2][3] = translation.Z;
    return matrix;
}

Matrix4f MatrixRotateX(float angle) {
    auto matrix = IdentityMatrix();
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    matrix.M[1][1] = c;
    matrix.M[1][2] = -s;
    matrix.M[2][1] = s;
    matrix.M[2][2] = c;
    return matrix;
}

Matrix4f MatrixRotateY(float angle) {
    auto matrix = IdentityMatrix();
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    matrix.M[0][0] = c;
    matrix.M[0][2] = s;
    matrix.M[2][0] = -s;
    matrix.M[2][2] = c;
    return matrix;
}

Matrix4f MatrixRotateZ(float angle) {
    auto matrix = IdentityMatrix();
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    matrix.M[0][0] = c;
    matrix.M[0][1] = -s;
    matrix.M[1][0] = s;
    matrix.M[1][1] = c;
    return matrix;
}

Matrix4f MultiplyMatrix(const Matrix4f& left, const Matrix4f& right) {
    Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            for (size_t k = 0; k < 4; ++k) {
                out.M[row][col] += left.M[row][k] * right.M[k][col];
            }
        }
    }
    return out;
}

Matrix4f BoneLocalTransform(const CmbSkeletonBone& bone) {
    return MultiplyMatrix(
        MatrixTranslate(bone.Translation),
        MultiplyMatrix(MatrixRotateZ(bone.Rotation.Z),
                       MultiplyMatrix(MatrixRotateY(bone.Rotation.Y),
                                      MultiplyMatrix(MatrixRotateX(bone.Rotation.X), MatrixScale(bone.Scale)))));
}

std::vector<Matrix4f> ComposeWorldTransforms(const CmbSkeleton& skeleton, const std::vector<Matrix4f>& localTransforms) {
    std::vector<Matrix4f> resolved(skeleton.Bones.size());
    std::vector<bool> hasResolved(skeleton.Bones.size(), false);

    const auto resolve = [&](auto&& self, size_t index) -> Matrix4f {
        if (hasResolved[index]) {
            return resolved[index];
        }
        const auto& bone = skeleton.Bones[index];
        Matrix4f world = localTransforms[index];
        if (bone.ParentIndex >= 0 && static_cast<size_t>(bone.ParentIndex) < skeleton.Bones.size()) {
            world = MultiplyMatrix(self(self, static_cast<size_t>(bone.ParentIndex)), world);
        }
        resolved[index] = world;
        hasResolved[index] = true;
        return world;
    };

    for (size_t i = 0; i < skeleton.Bones.size(); ++i) {
        resolve(resolve, i);
    }
    return resolved;
}

std::vector<Matrix4f> SkeletonWorldTransforms(const CmbSkeleton& skeleton) {
    std::vector<Matrix4f> local;
    local.reserve(skeleton.Bones.size());
    for (const auto& bone : skeleton.Bones) {
        local.push_back(BoneLocalTransform(bone));
    }
    return ComposeWorldTransforms(skeleton, local);
}

Matrix4f MorphSkelAnimeLocalTransform(const Matrix4f& current, const Matrix4f& morph, float morphWeight) {
    const float currentWeight = 1.0f - morphWeight;
    Matrix4f out{};
    for (size_t row = 0; row < 2; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            out.M[row][col] = current.M[row][col] * currentWeight + morph.M[row][col] * morphWeight;
        }
    }
    out.M[2][3] = current.M[2][3] * currentWeight + morph.M[2][3] * morphWeight;

    out.M[2][0] = out.M[0][1] * out.M[1][2] - out.M[0][2] * out.M[1][1];
    out.M[2][1] = out.M[0][2] * out.M[1][0] - out.M[0][0] * out.M[1][2];
    out.M[2][2] = out.M[0][0] * out.M[1][1] - out.M[0][1] * out.M[1][0];

    const auto rowLength = [&out](size_t row) {
        return std::sqrt(out.M[row][0] * out.M[row][0] + out.M[row][1] * out.M[row][1] +
                         out.M[row][2] * out.M[row][2]);
    };
    const float row0Length = rowLength(0);
    const float row2Length = rowLength(2);
    if (row0Length != 0.0f && row2Length != 0.0f) {
        for (size_t col = 0; col < 3; ++col) {
            out.M[0][col] /= row0Length;
            out.M[2][col] /= row2Length;
        }
        out.M[1][0] = out.M[2][1] * out.M[0][2] - out.M[2][2] * out.M[0][1];
        out.M[1][1] = out.M[2][2] * out.M[0][0] - out.M[2][0] * out.M[0][2];
        out.M[1][2] = out.M[2][0] * out.M[0][1] - out.M[2][1] * out.M[0][0];
    }
    out.M[3][3] = 1.0f;
    return out;
}

Vec3f TransformPosition(const Matrix4f& transform, const Vec3f& position) {
    return {
        transform.M[0][0] * position.X + transform.M[0][1] * position.Y + transform.M[0][2] * position.Z +
            transform.M[0][3],
        transform.M[1][0] * position.X + transform.M[1][1] * position.Y + transform.M[1][2] * position.Z +
            transform.M[1][3],
        transform.M[2][0] * position.X + transform.M[2][1] * position.Y + transform.M[2][2] * position.Z +
            transform.M[2][3],
    };
}

Vec3f TransformDirection(const Matrix4f& transform, const Vec3f& direction) {
    Vec3f out{
        transform.M[0][0] * direction.X + transform.M[0][1] * direction.Y + transform.M[0][2] * direction.Z,
        transform.M[1][0] * direction.X + transform.M[1][1] * direction.Y + transform.M[1][2] * direction.Z,
        transform.M[2][0] * direction.X + transform.M[2][1] * direction.Y + transform.M[2][2] * direction.Z,
    };
    const float length = std::sqrt(out.X * out.X + out.Y * out.Y + out.Z * out.Z);
    if (length <= 0.000001f) {
        return direction;
    }
    out.X /= length;
    out.Y /= length;
    out.Z /= length;
    return out;
}

std::vector<ChannelOffset> CsabActiveChannelOffsets(const BinaryView& view, size_t recordStart) {
    std::vector<ChannelOffset> offsets;
    for (uint16_t slot = 0; slot < kCsabAnodChannelOffsetCount; ++slot) {
        const auto offset = view.U16(recordStart + kCsabAnodChannelOffsetTableOffset + slot * 2);
        if (offset != 0) {
            offsets.push_back({ slot, offset });
        }
    }
    return offsets;
}

CsabChannelBlockInfo CsabChannelBlockClass(const BinaryView& view, size_t blockStart, size_t blockEnd) {
    const auto blockSize = blockEnd - blockStart;
    if (blockSize < kCsabAnodChannelBlockHeaderSize) {
        return {};
    }
    const auto blockType = view.U32(blockStart);
    const auto keyCount = view.U32(blockStart + 4);
    if (blockType == 1 && keyCount == 1 && blockSize == 0x18) {
        return { CsabChannelBlockClass::F32Constant, keyCount };
    }
    if (blockType == 1 && keyCount == 1 && blockSize == 0x14) {
        return { CsabChannelBlockClass::S16Constant, keyCount };
    }
    if (blockType == 2 && blockSize == kCsabAnodChannelBlockKeyHeaderSize + keyCount * kCsabAnodF32KeySize) {
        return { CsabChannelBlockClass::F32Keys, keyCount };
    }
    if (blockType == 2 && blockSize == kCsabAnodChannelBlockKeyHeaderSize + keyCount * kCsabAnodS16KeySize) {
        return { CsabChannelBlockClass::S16Keys, keyCount };
    }
    return { CsabChannelBlockClass::Unknown, keyCount };
}

float CsabS16RotationValue(int16_t rawValue) {
    return static_cast<float>(rawValue) * kCsabAnodS16RotationScale;
}

std::vector<CsabKey> CsabF32Keys(const BinaryView& view, size_t blockStart, uint32_t keyCount) {
    std::vector<CsabKey> keys;
    keys.reserve(keyCount);
    for (uint32_t index = 0; index < keyCount; ++index) {
        const auto offset = blockStart + kCsabAnodChannelBlockKeyHeaderSize + index * kCsabAnodF32KeySize;
        keys.push_back({ view.U32(offset), view.F32(offset + 4), view.F32(offset + 8), view.F32(offset + 12) });
    }
    return keys;
}

std::vector<CsabKey> CsabS16Keys(const BinaryView& view, size_t blockStart, uint32_t keyCount) {
    std::vector<CsabKey> keys;
    keys.reserve(keyCount);
    for (uint32_t index = 0; index < keyCount; ++index) {
        const auto offset = blockStart + kCsabAnodChannelBlockKeyHeaderSize + index * kCsabAnodS16KeySize;
        keys.push_back({ view.U16(offset), CsabS16RotationValue(view.S16(offset + 2)),
                         CsabS16RotationValue(view.S16(offset + 4)), CsabS16RotationValue(view.S16(offset + 6)) });
    }
    return keys;
}

float CubicHermite(float t, float interval, float y0, float y1, float m0, float m1) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * y0 + (3.0f * t2 - 2.0f * t3) * y1 +
           (t3 - 2.0f * t2 + t) * m0 * interval + (t3 - t2) * m1 * interval;
}

float UnwrapAngleNear(float reference, float value) {
    constexpr float fullTurn = 6.28318530717958647692f;
    while (value - reference > 3.14159265358979323846f) {
        value -= fullTurn;
    }
    while (value - reference < -3.14159265358979323846f) {
        value += fullTurn;
    }
    return value;
}

float SampleKeyedChannel(const std::vector<CsabKey>& keys, float frame, bool rotation) {
    if (keys.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (frame <= static_cast<float>(keys.front().Frame)) {
        return keys.front().Value;
    }
    if (frame >= static_cast<float>(keys.back().Frame)) {
        return keys.back().Value;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& left = keys[i];
        const auto& right = keys[i + 1];
        if (frame < static_cast<float>(right.Frame)) {
            const float interval = static_cast<float>(right.Frame - left.Frame);
            if (interval <= 0.0f) {
                return left.Value;
            }
            const float t = (frame - static_cast<float>(left.Frame)) / interval;
            const float rightValue = rotation ? UnwrapAngleNear(left.Value, right.Value) : right.Value;
            return CubicHermite(t, interval * kCsabAnodHermiteIntervalScale, left.Value, rightValue, left.Outgoing,
                                right.Incoming);
        }
    }
    return keys.back().Value;
}

bool IsRotationSlot(uint16_t slot) {
    return slot >= 3 && slot <= 5;
}

std::optional<float> SampleCsabChannelBlock(const BinaryView& view, size_t blockStart, size_t blockEnd, float frame,
                                            uint16_t slot) {
    const auto info = CsabChannelBlockClass(view, blockStart, blockEnd);
    switch (info.Class) {
        case CsabChannelBlockClass::F32Constant:
            return view.F32(blockStart + 0x14);
        case CsabChannelBlockClass::F32Keys:
            return SampleKeyedChannel(CsabF32Keys(view, blockStart, info.KeyCount), frame, IsRotationSlot(slot));
        case CsabChannelBlockClass::S16Constant:
            if (IsRotationSlot(slot)) {
                return CsabS16RotationValue(view.S16(blockStart + 0x12));
            }
            return std::nullopt;
        case CsabChannelBlockClass::S16Keys:
            if (IsRotationSlot(slot)) {
                return SampleKeyedChannel(CsabS16Keys(view, blockStart, info.KeyCount), frame, true);
            }
            return std::nullopt;
        case CsabChannelBlockClass::Unknown:
        default:
            return std::nullopt;
    }
}

CsabFrameChannels CsabChannelValuesByNode(const BinaryView& view, const CsabMetadata& metadata, float frame) {
    CsabFrameChannels result;
    for (size_t recordIndex = 0; recordIndex < metadata.NodeOffsets.size(); ++recordIndex) {
        const size_t recordStart = kCsabNodeOffsetBase + metadata.NodeOffsets[recordIndex];
        const size_t nextRecordStart = recordIndex + 1 < metadata.NodeOffsets.size()
                                           ? kCsabNodeOffsetBase + metadata.NodeOffsets[recordIndex + 1]
                                           : view.Bytes.size();
        view.Require(recordStart, kCsabAnodHeaderSize);
        const auto activeOffsets = CsabActiveChannelOffsets(view, recordStart);
        for (size_t activeIndex = 0; activeIndex < activeOffsets.size(); ++activeIndex) {
            const auto slot = activeOffsets[activeIndex].Slot;
            const auto blockStart = recordStart + activeOffsets[activeIndex].Offset;
            const auto blockEnd = activeIndex + 1 < activeOffsets.size()
                                      ? recordStart + activeOffsets[activeIndex + 1].Offset
                                      : nextRecordStart;
            auto value = SampleCsabChannelBlock(view, blockStart, blockEnd, frame, slot);
            if (!value) {
                ++result.NonF32ChannelBlockCount;
                continue;
            }
            result.ValuesByNode[static_cast<uint32_t>(recordIndex)][slot] = *value;
            ++result.SampledChannelValueCount;
            if (std::isfinite(*value)) {
                ++result.FiniteChannelValueCount;
            }
        }
    }
    return result;
}

CmbSkeletonBone PoseBoneFromValues(const CmbSkeletonBone& bone, const std::map<uint16_t, float>& values,
                                   uint8_t channelMask) {
    CmbSkeletonBone pose = bone;
    const bool applyTranslation =
        (channelMask & static_cast<uint8_t>(CsabTransformChannelMask::Translation)) != 0;
    const bool applyRotation =
        (channelMask & static_cast<uint8_t>(CsabTransformChannelMask::Rotation)) != 0;
    const bool applyScale =
        (channelMask & static_cast<uint8_t>(CsabTransformChannelMask::Scale)) != 0;
    if (applyTranslation && values.contains(0)) {
        pose.Translation.X = values.at(0);
    }
    if (applyTranslation && values.contains(1)) {
        pose.Translation.Y = values.at(1);
    }
    if (applyTranslation && values.contains(2)) {
        pose.Translation.Z = values.at(2);
    }
    if (applyRotation && values.contains(3)) {
        pose.Rotation.X = values.at(3);
    }
    if (applyRotation && values.contains(4)) {
        pose.Rotation.Y = values.at(4);
    }
    if (applyRotation && values.contains(5)) {
        pose.Rotation.Z = values.at(5);
    }
    if (applyScale && values.contains(6)) {
        pose.Scale.X = values.at(6);
    }
    if (applyScale && values.contains(7)) {
        pose.Scale.Y = values.at(7);
    }
    if (applyScale && values.contains(8)) {
        pose.Scale.Z = values.at(8);
    }
    return pose;
}

} // namespace

Oot3dNativePicaCullMode DecodeOot3dCmbPicaCullMode(uint8_t cullFace, uint16_t frontFace) {
    // FUN_003FAD68 maps the CMB byte through GL cull-face enums; FUN_00408E24
    // then emits GPUREG_FACECULLING_CONFIG using the active front-face state.
    if (cullFace == 3) {
        return Oot3dNativePicaCullMode::KeepAll;
    }
    if ((cullFace == 0 && frontFace == kOot3dNativeFrontFaceClockwise) ||
        (cullFace == 1 && frontFace == kOot3dNativeFrontFaceCounterClockwise)) {
        return Oot3dNativePicaCullMode::KeepCounterClockwise;
    }
    return Oot3dNativePicaCullMode::KeepClockwise;
}

bool DecodeOot3dPicaTextureRgba8(
    uint8_t nativeFormat, uint16_t width, uint16_t height,
    std::span<const uint8_t> nativeBytes, std::vector<uint8_t>& rgba8,
    std::string* error) {
    return ::Oot3d::Renderer::DecodePicaTextureRgba8(
        nativeFormat, width, height, nativeBytes, rgba8, error);
}

size_t CmbModel::BoneCount() const {
    return Skeleton.Bones.size();
}

size_t CmbModel::PrimitiveCount() const {
    size_t count = 0;
    for (const auto& shape : Shapes) {
        count += shape.Primitives.size();
    }
    return count;
}

size_t CmbModel::TriangleCount() const {
    size_t count = 0;
    for (const auto& shape : Shapes) {
        for (const auto& primitive : shape.Primitives) {
            count += primitive.Indices.size() / 3;
        }
    }
    return count;
}

size_t CmbModel::VertexCount() const {
    size_t count = 0;
    for (const auto& shape : Shapes) {
        count += shape.Positions.size();
    }
    return count;
}

bool CmbModel::IsRigidExportCandidate() const {
    for (const auto& shape : Shapes) {
        for (const auto& primitive : shape.Primitives) {
            if (primitive.SkinningMode != 0 || primitive.BoneIndices.size() != 1 ||
                primitive.BoneIndices.front() >= Skeleton.Bones.size()) {
                return false;
            }
        }
    }
    return true;
}

bool CmbModel::IsStaticCandidate() const {
    return BoneCount() == 1 && IsRigidExportCandidate();
}

CmbModel ParseCmbModelBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, std::move(source) };
    if (!view.HasMagic(0, "cmb ")) {
        throw std::runtime_error(view.Source + ": expected CMB magic");
    }

    CmbModel model;
    model.Source = view.Source;
    model.DeclaredFileSize = view.U32(0x04);
    if (model.DeclaredFileSize > bytes.size()) {
        throw std::runtime_error(view.Source + ": CMB declared size exceeds file length");
    }
    model.Version = view.U32(0x08);
    if (model.Version != 6) {
        throw std::runtime_error(view.Source + ": only OOT3D CMB version 6 is supported");
    }
    model.Name = view.Cstr(0x10, 0x10);

    const auto indexCount = view.U32(0x20);
    const auto sklOffset = view.U32(0x24);
    const auto matsOffset = view.U32(0x28);
    const auto texOffset = view.U32(0x2C);
    const auto sklmOffset = view.U32(0x30);
    const auto lutsOffset = view.U32(0x34);
    const auto vatrOffset = view.U32(0x38);
    const auto indicesOffset = view.U32(0x3C);
    const auto textureDataOffset = view.U32(0x40);

    model.Textures = ParseTextures(view, texOffset, textureDataOffset);
    auto parsedMaterials = ParseMaterials(view, matsOffset, texOffset);
    model.TextureEnvSettings = std::move(parsedMaterials.TextureEnvSettings);
    model.Materials = std::move(parsedMaterials.Materials);
    model.Luts = ParseLuts(view, lutsOffset);
    ParseSklm(view, sklmOffset, vatrOffset, indicesOffset, indexCount, model.Meshes, model.Shapes,
              model.MeshPassSplitIndex);
    model.MeshPassSplitIndexDecoded = true;
    model.Skeleton = ParseSkeleton(view, sklOffset);
    return model;
}

CmbModel ParseCmbModelFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseCmbModelBytes(bytes, path.string());
}

std::vector<uint32_t> FindMagicOffsets(const BinaryView& view, std::string_view magic) {
    std::vector<uint32_t> offsets;
    if (magic.empty() || magic.size() > view.Bytes.size()) {
        return offsets;
    }
    for (size_t offset = 0; offset + magic.size() <= view.Bytes.size(); ++offset) {
        if (view.HasMagic(offset, magic)) {
            offsets.push_back(static_cast<uint32_t>(offset));
        }
    }
    return offsets;
}

std::vector<std::string> ParseCmabStringTable(const BinaryView& view, uint32_t offset) {
    if (offset == 0 || offset + 8 > view.Bytes.size() || !view.HasMagic(offset, "strt")) {
        return {};
    }
    const uint32_t stringCount = view.U32(offset + 0x04);
    const size_t offsetsStart = offset + 0x08;
    const size_t offsetsEnd = offsetsStart + static_cast<size_t>(stringCount) * sizeof(uint32_t);
    if (offsetsEnd > view.Bytes.size()) {
        throw std::runtime_error(view.Source + ": CMAB string offset table exceeds file");
    }
    const size_t stringDataStart = offsetsEnd;

    std::vector<std::string> names;
    names.reserve(stringCount);
    for (uint32_t index = 0; index < stringCount; ++index) {
        const uint32_t relativeOffset = view.U32(offsetsStart + index * sizeof(uint32_t));
        const size_t stringOffset = stringDataStart + relativeOffset;
        if (stringOffset >= view.Bytes.size()) {
            throw std::runtime_error(view.Source + ": CMAB string offset is outside file");
        }
        size_t end = stringOffset;
        while (end < view.Bytes.size() && view.Bytes[end] != 0) {
            ++end;
        }
        if (end >= view.Bytes.size()) {
            throw std::runtime_error(view.Source + ": CMAB string is unterminated");
        }
        names.emplace_back(reinterpret_cast<const char*>(view.Bytes.data() + stringOffset), end - stringOffset);
    }
    return names;
}

uint32_t ResolveCmabTxptOffset(const BinaryView& view, uint32_t candidateOffset) {
    if (candidateOffset != 0 && candidateOffset + 4 <= view.Bytes.size() &&
        view.HasMagic(candidateOffset, "txpt")) {
        return candidateOffset;
    }

    const auto offsets = FindMagicOffsets(view, "txpt");
    if (!offsets.empty()) {
        return offsets.front();
    }
    return 0;
}

std::vector<CmbTexture> ParseCmabEmbeddedTextures(const BinaryView& view, uint32_t txptOffset,
                                                  uint32_t textureDataOffset,
                                                  const std::vector<std::string>& stringTableNames) {
    if (txptOffset == 0 || txptOffset + 8 > view.Bytes.size() || !view.HasMagic(txptOffset, "txpt")) {
        return {};
    }

    const uint32_t textureCount = view.U32(txptOffset + 0x04);
    const size_t recordsStart = txptOffset + 0x08;
    const size_t recordsEnd = recordsStart + static_cast<size_t>(textureCount) * kOot3dCmabTxptRecordSize;
    if (recordsEnd > view.Bytes.size()) {
        throw std::runtime_error(view.Source + ": CMAB TXPT records exceed file");
    }
    if (textureDataOffset > view.Bytes.size()) {
        throw std::runtime_error(view.Source + ": CMAB texture data base is outside file");
    }

    std::vector<CmbTexture> textures;
    textures.reserve(textureCount);
    for (uint32_t index = 0; index < textureCount; ++index) {
        const size_t recordOffset = recordsStart + static_cast<size_t>(index) * kOot3dCmabTxptRecordSize;
        CmbTexture texture;
        texture.Index = index;
        texture.DataSize = view.U32(recordOffset + 0x00);
        texture.MipmapCount = std::max<uint32_t>(1, view.U16(recordOffset + 0x04));
        texture.Width = view.U16(recordOffset + 0x08);
        texture.Height = view.U16(recordOffset + 0x0A);
        texture.TextureFormat = view.U16(recordOffset + 0x0C);
        texture.DataType = view.U16(recordOffset + 0x0E);
        texture.DataOffset = view.U32(recordOffset + 0x10);
        if (index < stringTableNames.size()) {
            texture.Name = stringTableNames[index];
        }

        const size_t dataStart = static_cast<size_t>(textureDataOffset) + texture.DataOffset;
        view.Require(dataStart, texture.DataSize);
        texture.Data.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(dataStart),
                            view.Bytes.begin() + static_cast<std::ptrdiff_t>(dataStart + texture.DataSize));
        texture.Rgba8 = DecodeTextureRgba8(texture);
        texture.Rgba8Decoded = texture.Rgba8.size() == static_cast<size_t>(texture.Width) * texture.Height * 4;
        DecodeTextureMipChain(texture);
        textures.push_back(std::move(texture));
    }
    return textures;
}

std::string CmabNativeValueKind(uint32_t nativeType) {
    switch (nativeType) {
        case 1:
            return "transform_vec2_gate_1_plus_selector";
        case 2:
            return "texture_frame_int_gate_7_plus_stage";
        case 3:
            return "material_color_vec4_gate_0";
        case 4:
            return "constant_color_vec4_gate_0a_plus_selector";
        case 5:
            return "transform_scalar_gate_4_plus_selector";
        default:
            return {};
    }
}

uint32_t CmabNativeChannelOffsetTableOffset(uint32_t nativeType) {
    switch (nativeType) {
        case 1:
        case 2:
        case 4:
        case 5:
            return 0x10;
        case 3:
            return 0x0C;
        default:
            return 0;
    }
}

uint32_t CmabNativeChannelOffsetCount(uint32_t nativeType) {
    switch (nativeType) {
        case 1:
            return 2;
        case 2:
        case 5:
            return 1;
        case 3:
        case 4:
            return 4;
        default:
            return 0;
    }
}

bool CmabNativeTargetSelectorPresent(uint32_t nativeType) {
    switch (nativeType) {
        case 1:
        case 2:
        case 4:
        case 5:
            return true;
        default:
            return false;
    }
}

std::vector<CmabNativeChannelOffset> DecodeCmabNativeChannelOffsets(const BinaryView& view,
                                                                    uint32_t recordOffset,
                                                                    uint32_t recordSize,
                                                                    uint32_t nativeType) {
    const uint32_t tableOffset = CmabNativeChannelOffsetTableOffset(nativeType);
    const uint32_t channelCount = CmabNativeChannelOffsetCount(nativeType);
    if (tableOffset == 0 || channelCount == 0) {
        return {};
    }

    std::vector<CmabNativeChannelOffset> offsets;
    offsets.reserve(channelCount);
    for (uint32_t componentIndex = 0; componentIndex < channelCount; ++componentIndex) {
        const uint32_t entryOffset = tableOffset + componentIndex * sizeof(uint16_t);
        if (entryOffset + sizeof(uint16_t) > recordSize) {
            break;
        }

        CmabNativeChannelOffset channelOffset;
        channelOffset.ComponentIndex = componentIndex;
        channelOffset.TableOffset = entryOffset;
        channelOffset.RelativeOffset = view.S16(static_cast<size_t>(recordOffset) + entryOffset);
        channelOffset.Present = channelOffset.RelativeOffset != 0;
        offsets.push_back(channelOffset);
    }
    return offsets;
}

uint32_t CmabSourceCurvePointStride(uint8_t type) {
    switch (type) {
        case 1:
        case 3:
            return kCmbLutLinearPointStride;
        case 2:
            return kCmbLutHermitePointStride;
        default:
            return 0;
    }
}

size_t CountCmabSourceCurvePointsLessOrEqual(const CmabSourceCurve& curve, float input) {
    size_t count = 0;
    while (count < curve.Points.size() && static_cast<float>(curve.Points[count].Frame) <= input) {
        ++count;
    }
    return count;
}

float EvaluateCmabSourceCurveHermiteSegment(const CmabSourceCurvePoint& previous,
                                            const CmabSourceCurvePoint& current,
                                            float previousFrame, float currentFrame, float input) {
    const auto deltaX = currentFrame - previousFrame;
    if (deltaX <= 0.0f) {
        return current.Value;
    }

    const auto xFromPrevious = input - previousFrame;
    const auto t = xFromPrevious / deltaX;
    const auto tMinusOne = t - 1.0f;
    const auto valueDeltaTerm = (current.Value - previous.Value) * (3.0f - 2.0f * t) * t * t;
    const auto tangentTerm =
        xFromPrevious * tMinusOne * (tMinusOne * previous.TangentOut + t * current.TangentIn);
    return previous.Value + valueDeltaTerm + tangentTerm;
}

float EvaluateCmabLinearSourceCurve(const CmabSourceCurve& curve, float input) {
    if (curve.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (curve.Points.size() == 1) {
        return curve.Points.front().Value;
    }

    const auto upper = CountCmabSourceCurvePointsLessOrEqual(curve, input);
    if (upper == 0) {
        return curve.Points.front().Value;
    }
    if (upper >= curve.Points.size()) {
        return curve.Points.back().Value;
    }

    const auto& previous = curve.Points[upper - 1];
    const auto& current = curve.Points[upper];
    const auto previousFrame = static_cast<float>(previous.Frame);
    const auto currentFrame = static_cast<float>(current.Frame);
    const auto deltaX = currentFrame - previousFrame;
    if (deltaX <= 0.0f) {
        return current.Value;
    }

    const auto t = (input - previousFrame) / deltaX;
    return previous.Value + (current.Value - previous.Value) * t;
}

float EvaluateCmabStepSourceCurve(const CmabSourceCurve& curve, float input) {
    if (curve.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (curve.Points.size() == 1) {
        return curve.Points.front().Value;
    }

    const auto upper = CountCmabSourceCurvePointsLessOrEqual(curve, input);
    if (upper == 0) {
        return curve.Points.front().Value;
    }
    return curve.Points[upper - 1].Value;
}

float EvaluateCmabHermiteSourceCurve(const CmabSourceCurve& curve, float input) {
    if (curve.Points.empty()) {
        return kCmbLutClampMinimum;
    }
    if (curve.Points.size() == 1) {
        return curve.Points.front().Value;
    }

    float sampleFrame = input;
    const float loopEnd = static_cast<float>(curve.HeaderWord0C);
    if (curve.WrapEnabled && (sampleFrame < 0.0f || sampleFrame > loopEnd)) {
        const auto& previous = curve.Points.back();
        const auto& current = curve.Points.front();
        const float loopPeriod = loopEnd + 1.0f;
        if (sampleFrame < 0.0f) {
            sampleFrame += loopPeriod;
        }
        return EvaluateCmabSourceCurveHermiteSegment(
            previous, current, static_cast<float>(previous.Frame),
            static_cast<float>(current.Frame) + loopPeriod, sampleFrame);
    }

    const auto upper = CountCmabSourceCurvePointsLessOrEqual(curve, sampleFrame);
    if (upper == 0) {
        return kCmbLutClampMinimum;
    }
    if (upper >= curve.Points.size()) {
        return curve.Points.back().Value;
    }

    const auto& previous = curve.Points[upper - 1];
    const auto& current = curve.Points[upper];
    return EvaluateCmabSourceCurveHermiteSegment(previous, current, static_cast<float>(previous.Frame),
                                                static_cast<float>(current.Frame), sampleFrame);
}

float SampleCmabSourceCurveImpl(const CmabSourceCurve& curve, float frame) {
    if (!curve.Decoded || !std::isfinite(frame)) {
        return kCmbLutClampMinimum;
    }

    switch (curve.Type) {
        case 1:
            return EvaluateCmabLinearSourceCurve(curve, frame);
        case 2:
            return EvaluateCmabHermiteSourceCurve(curve, frame);
        case 3:
            return EvaluateCmabStepSourceCurve(curve, frame);
        default:
            return kCmbLutClampMinimum;
    }
}

std::vector<CmabSourceCurve> DecodeCmabNativeSourceCurves(const BinaryView& view,
                                                          uint32_t recordOffset,
                                                          uint32_t recordSize,
                                                          const std::vector<CmabNativeChannelOffset>& channelOffsets) {
    std::vector<uint32_t> presentOffsets;
    presentOffsets.reserve(channelOffsets.size());
    for (const auto& channelOffset : channelOffsets) {
        if (channelOffset.Present && channelOffset.RelativeOffset > 0) {
            const auto offset = static_cast<uint32_t>(channelOffset.RelativeOffset);
            if (offset < recordSize) {
                presentOffsets.push_back(offset);
            }
        }
    }
    std::sort(presentOffsets.begin(), presentOffsets.end());
    presentOffsets.erase(std::unique(presentOffsets.begin(), presentOffsets.end()), presentOffsets.end());

    std::vector<CmabSourceCurve> curves;
    curves.reserve(std::count_if(channelOffsets.begin(), channelOffsets.end(),
                                 [](const auto& channelOffset) { return channelOffset.Present; }));
    for (const auto& channelOffset : channelOffsets) {
        if (!channelOffset.Present) {
            continue;
        }

        CmabSourceCurve curve;
        curve.ComponentIndex = channelOffset.ComponentIndex;
        curve.WrapEnabled = channelOffset.ComponentIndex != 0;
        if (channelOffset.RelativeOffset <= 0) {
            curve.Status = "source_curve_offset_out_of_bounds";
            curves.push_back(std::move(curve));
            continue;
        }

        const auto curveOffset = static_cast<uint32_t>(channelOffset.RelativeOffset);
        curve.Offset = curveOffset;
        uint32_t curveEnd = recordSize;
        const auto nextOffset = std::upper_bound(presentOffsets.begin(), presentOffsets.end(), curveOffset);
        if (nextOffset != presentOffsets.end()) {
            curveEnd = *nextOffset;
        }
        if (curveOffset >= recordSize || curveEnd <= curveOffset ||
            static_cast<size_t>(curveOffset) + kCmbLutHeaderSize > recordSize) {
            curve.Status = "source_curve_offset_out_of_bounds";
            curves.push_back(std::move(curve));
            continue;
        }

        curve.Size = curveEnd - curveOffset;
        const size_t absoluteCurveOffset = static_cast<size_t>(recordOffset) + curveOffset;
        curve.Type = view.U8(absoluteCurveOffset);
        curve.HeaderByte01 = view.U8(absoluteCurveOffset + 0x01);
        curve.HeaderByte02 = view.U8(absoluteCurveOffset + 0x02);
        curve.HeaderByte03 = view.U8(absoluteCurveOffset + 0x03);
        curve.PointCount = view.U32(absoluteCurveOffset + 0x04);
        curve.HeaderWord08 = view.U32(absoluteCurveOffset + 0x08);
        curve.HeaderWord0C = view.U32(absoluteCurveOffset + 0x0C);
        curve.PointStrideBytes = CmabSourceCurvePointStride(curve.Type);
        if (curve.PointStrideBytes == 0) {
            curve.Status = "source_curve_unsupported_type";
            curves.push_back(std::move(curve));
            continue;
        }

        const size_t requiredSize = kCmbLutHeaderSize + static_cast<size_t>(curve.PointCount) * curve.PointStrideBytes;
        if (requiredSize > curve.Size || absoluteCurveOffset + requiredSize > view.Bytes.size()) {
            curve.Status = "source_curve_points_out_of_bounds";
            curves.push_back(std::move(curve));
            continue;
        }

        curve.Points.reserve(curve.PointCount);
        bool ordered = true;
        for (uint32_t pointIndex = 0; pointIndex < curve.PointCount; ++pointIndex) {
            const size_t pointOffset = absoluteCurveOffset + kCmbLutHeaderSize +
                                       static_cast<size_t>(pointIndex) * curve.PointStrideBytes;
            CmabSourceCurvePoint point;
            point.Frame = view.S32(pointOffset);
            point.Value = view.F32(pointOffset + 0x04);
            if (curve.Type == 2) {
                point.TangentIn = view.F32(pointOffset + 0x08);
                point.TangentOut = view.F32(pointOffset + 0x0C);
            }
            if (!curve.Points.empty() && point.Frame <= curve.Points.back().Frame) {
                ordered = false;
            }
            curve.Points.push_back(point);
        }
        if (!ordered) {
            curve.Status = "source_curve_unordered_points";
            curves.push_back(std::move(curve));
            continue;
        }

        curve.Decoded = true;
        curve.Status = "decoded";
        curve.SampleFrame0 = SampleCmabSourceCurveImpl(curve, 0.0f);
        curve.SampleFrame0Valid = true;
        curves.push_back(std::move(curve));
    }
    return curves;
}

std::vector<CmabScalarTrack> DecodeCmabScalarTracks(const BinaryView& view, uint32_t recordOffset,
                                                    uint32_t recordSize,
                                                    const std::array<uint32_t, 2>& trackOffsets) {
    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (uint32_t componentIndex = 0; componentIndex < trackOffsets.size(); ++componentIndex) {
        const uint32_t trackOffset = trackOffsets[componentIndex];
        if (trackOffset == 0 || trackOffset >= recordSize ||
            static_cast<size_t>(trackOffset) + 0x10 > recordSize) {
            continue;
        }
        candidates.emplace_back(trackOffset, componentIndex);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const auto& left, const auto& right) {
                                     return left.first == right.first && left.second == right.second;
                                 }),
                     candidates.end());

    std::vector<CmabScalarTrack> tracks;
    tracks.reserve(candidates.size());
    for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        const uint32_t trackOffset = candidates[candidateIndex].first;
        uint32_t trackEnd = recordSize;
        for (size_t nextIndex = candidateIndex + 1; nextIndex < candidates.size(); ++nextIndex) {
            if (candidates[nextIndex].first > trackOffset) {
                trackEnd = candidates[nextIndex].first;
                break;
            }
        }
        if (trackEnd <= trackOffset) {
            continue;
        }

        const size_t absoluteTrackOffset = static_cast<size_t>(recordOffset) + trackOffset;
        CmabScalarTrack track;
        track.ComponentIndex = candidates[candidateIndex].second;
        track.Offset = trackOffset;
        track.Size = trackEnd - trackOffset;
        for (size_t word = 0; word < track.HeaderWords.size(); ++word) {
            track.HeaderWords[word] = view.U32(absoluteTrackOffset + word * sizeof(uint32_t));
        }
        track.KeyframeCountCandidate = track.HeaderWords[1];
        track.LastFrameCandidate = track.HeaderWords[3];

        const size_t keyframeStart = absoluteTrackOffset + 0x10;
        const size_t keyframeEnd =
            keyframeStart + static_cast<size_t>(track.KeyframeCountCandidate) *
                                kOot3dCmabMmadScalarKeyframeSize;
        const size_t absoluteTrackEnd = static_cast<size_t>(recordOffset) + trackEnd;
        if (keyframeEnd == absoluteTrackEnd && keyframeEnd <= view.Bytes.size()) {
            track.KeyframesDecoded = true;
            track.Keyframes.reserve(track.KeyframeCountCandidate);
            for (uint32_t key = 0; key < track.KeyframeCountCandidate; ++key) {
                const size_t keyOffset = keyframeStart +
                                         static_cast<size_t>(key) *
                                             kOot3dCmabMmadScalarKeyframeSize;
                CmabScalarKeyframe keyframe;
                keyframe.Frame = view.U32(keyOffset);
                keyframe.ValueBits = view.U32(keyOffset + 0x04);
                keyframe.Value = view.F32(keyOffset + 0x04);
                track.Keyframes.push_back(keyframe);
            }
        }

        tracks.push_back(std::move(track));
    }
    return tracks;
}

std::vector<uint32_t> ParseCmabMadsRecordOffsets(const BinaryView& view, uint32_t madsOffset,
                                                 uint32_t madsRecordCount) {
    std::vector<uint32_t> offsets;
    if (madsRecordCount == 0) {
        return offsets;
    }

    view.Require(static_cast<size_t>(madsOffset) + 0x08, static_cast<size_t>(madsRecordCount) * sizeof(uint32_t));
    offsets.reserve(madsRecordCount);
    for (uint32_t index = 0; index < madsRecordCount; ++index) {
        const uint32_t relativeOffset = view.U32(static_cast<size_t>(madsOffset) + 0x08 + index * sizeof(uint32_t));
        offsets.push_back(madsOffset + relativeOffset);
    }
    return offsets;
}

std::vector<uint32_t> ResolveCmabMmadRecordOffsets(const BinaryView& view,
                                                   const std::vector<uint32_t>& madsRecordOffsets) {
    if (!madsRecordOffsets.empty() &&
        std::all_of(madsRecordOffsets.begin(), madsRecordOffsets.end(),
                    [&](const auto offset) { return offset + 4 <= view.Bytes.size() && view.HasMagic(offset, "mmad"); })) {
        return madsRecordOffsets;
    }
    return FindMagicOffsets(view, "mmad");
}

std::vector<CmabMmadRecord> ParseCmabMmadRecords(const BinaryView& view,
                                                 const std::vector<uint32_t>& madsRecordOffsets,
                                                 uint32_t texturePayloadOffset, uint32_t stringTableOffset) {
    auto offsets = ResolveCmabMmadRecordOffsets(view, madsRecordOffsets);
    std::vector<uint32_t> boundaryOffsets = offsets;
    for (const auto txptOffset : FindMagicOffsets(view, "txpt")) {
        boundaryOffsets.push_back(txptOffset);
    }
    if (texturePayloadOffset != 0 && texturePayloadOffset < view.Bytes.size() &&
        view.HasMagic(texturePayloadOffset, "txpt")) {
        boundaryOffsets.push_back(texturePayloadOffset);
    }
    if (stringTableOffset != 0 && stringTableOffset < view.Bytes.size() && view.HasMagic(stringTableOffset, "strt")) {
        boundaryOffsets.push_back(stringTableOffset);
    }
    std::sort(boundaryOffsets.begin(), boundaryOffsets.end());
    boundaryOffsets.erase(std::unique(boundaryOffsets.begin(), boundaryOffsets.end()), boundaryOffsets.end());

    std::vector<CmabMmadRecord> records;
    records.reserve(offsets.size());
    for (uint32_t index = 0; index < offsets.size(); ++index) {
        const uint32_t offset = offsets[index];
        view.Require(offset, 4);

        uint32_t endOffset = static_cast<uint32_t>(view.Bytes.size());
        const auto nextBoundary = std::upper_bound(boundaryOffsets.begin(), boundaryOffsets.end(), offset);
        if (nextBoundary != boundaryOffsets.end()) {
            endOffset = *nextBoundary;
        }
        if (endOffset <= offset) {
            throw std::runtime_error(view.Source + ": CMAB MMAD record has invalid bounds");
        }

        CmabMmadRecord record;
        record.Index = index;
        record.Offset = offset;
        if (index < madsRecordOffsets.size()) {
            record.MadsRecordOffset = madsRecordOffsets[index];
            record.MadsRecordOffsetMatched = record.MadsRecordOffset == record.Offset;
        }
        record.Size = endOffset - offset;
        record.RawRecord.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                view.Bytes.begin() + static_cast<std::ptrdiff_t>(endOffset));
        if (record.Size < kOot3dCmabMmadHeaderSize ||
            static_cast<size_t>(offset) + kOot3dCmabMmadHeaderSize > view.Bytes.size()) {
            records.push_back(std::move(record));
            continue;
        }

        record.HeaderDecoded = true;
        for (size_t word = 0; word < record.HeaderWords.size(); ++word) {
            record.HeaderWords[word] = view.U32(offset + 0x04 + word * sizeof(uint32_t));
        }
        record.NativeType = record.HeaderWords[0] & 0xFFu;
        record.NativeTypeFactorySupported = record.NativeType >= 1 && record.NativeType <= 5;
        record.TargetMaterialIndex = record.HeaderWords[1];
        record.TargetComponentOrStageIndex = record.HeaderWords[2];
        record.TargetSelectorPresent = CmabNativeTargetSelectorPresent(record.NativeType);
        if (record.TargetSelectorPresent) {
            record.TargetSelector = view.S16(offset + 0x0C);
        }
        record.NativeValueKind = CmabNativeValueKind(record.NativeType);
        record.NativeChannelOffsetTableOffset = CmabNativeChannelOffsetTableOffset(record.NativeType);
        record.NativeChannelOffsetCount = CmabNativeChannelOffsetCount(record.NativeType);
        record.NativeChannelOffsets =
            DecodeCmabNativeChannelOffsets(view, offset, record.Size, record.NativeType);
        record.NativeSourceCurves =
            DecodeCmabNativeSourceCurves(view, offset, record.Size, record.NativeChannelOffsets);
        record.KeyframeCountCandidate = record.HeaderWords[5];
        record.LastFrameCandidate = record.HeaderWords[7];
        record.ScalarTrackOffsets = { record.HeaderWords[3] & 0xFFFFu, record.HeaderWords[3] >> 16 };
        record.ScalarTracks = DecodeCmabScalarTracks(view, offset, record.Size, record.ScalarTrackOffsets);

        const auto decodedTrackCount =
            std::count_if(record.ScalarTracks.begin(), record.ScalarTracks.end(),
                          [](const auto& track) { return track.KeyframesDecoded; });
        if (decodedTrackCount == 1 && record.ScalarTracks.size() == 1 &&
            record.ScalarTracks.front().Offset == 0x14 &&
            static_cast<size_t>(record.ScalarTracks.front().Offset) +
                    record.ScalarTracks.front().Size ==
                record.Size) {
            const auto& scalarTrack = record.ScalarTracks.front();
            record.ScalarKeyframesDecoded = true;
            record.KeyframeCountCandidate = scalarTrack.KeyframeCountCandidate;
            record.LastFrameCandidate = scalarTrack.LastFrameCandidate;
            record.ScalarKeyframes = scalarTrack.Keyframes;
        }

        records.push_back(std::move(record));
    }
    return records;
}

CmabMaterialAnimation ParseCmabMaterialAnimationBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, std::move(source) };
    if (!view.HasMagic(0, "cmab")) {
        throw std::runtime_error(view.Source + ": expected CMAB magic");
    }

    CmabMaterialAnimation animation;
    animation.Source = view.Source;
    animation.Version = view.U32(0x04);
    animation.DeclaredFileSize = view.U32(0x08);
    if (animation.DeclaredFileSize > bytes.size()) {
        throw std::runtime_error(view.Source + ": CMAB declared size exceeds file length");
    }
    animation.HeaderWord0C = view.U32(0x0C);
    animation.HeaderWord10 = view.U32(0x10);
    animation.HeaderWord14 = view.U32(0x14);
    animation.StringTableOffset = view.U32(kCmabStringTableOffsetCandidate);
    animation.TextureDataOffset = view.U32(kCmabTextureDataOffsetCandidate);
    animation.HeaderWord20 = view.U32(0x20);
    animation.FrameCountCandidate = view.U32(0x24);
    animation.LoopModeCandidate = view.U32(0x28);
    animation.HeaderWord2C = view.U32(0x2C);
    animation.TexturePayloadOffsetCandidate = view.U32(kCmabTexturePayloadOffsetCandidate);
    animation.TexturePayloadOffset = ResolveCmabTxptOffset(view, animation.TexturePayloadOffsetCandidate);
    animation.MadsOffset = kOot3dCmabHeaderMadsOffset;
    if (!view.HasMagic(animation.MadsOffset, "mads")) {
        throw std::runtime_error(view.Source + ": expected CMAB MADS chunk at 0x34");
    }
    animation.MadsRecordCount = view.U32(animation.MadsOffset + 0x04);
    animation.MadsStride = view.U32(animation.MadsOffset + 0x08);
    animation.MadsRecordOffsets = ParseCmabMadsRecordOffsets(view, animation.MadsOffset, animation.MadsRecordCount);
    animation.StringTableNames = ParseCmabStringTable(view, animation.StringTableOffset);
    animation.MmadRecords = ParseCmabMmadRecords(view, animation.MadsRecordOffsets, animation.TexturePayloadOffset,
                                                 animation.StringTableOffset);
    animation.EmbeddedTextures = ParseCmabEmbeddedTextures(view, animation.TexturePayloadOffset,
                                                           animation.TextureDataOffset,
                                                           animation.StringTableNames);
    animation.TexturePayloadDecoded = !animation.EmbeddedTextures.empty();
    return animation;
}

CmabMaterialAnimation ParseCmabMaterialAnimationFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseCmabMaterialAnimationBytes(bytes, path.string());
}

FacebMaterialFrameTrack ParseFacebMaterialFrameTrackBytes(std::span<const uint8_t> bytes, std::string source) {
    constexpr size_t kFacebHeaderSize = 0x08;
    constexpr size_t kFacebEventSize = 0x04;

    BinaryView view{ bytes, std::move(source) };
    if (!view.HasMagic(0, std::string_view("fkb\x01", 4))) {
        throw std::runtime_error(view.Source + ": expected FACEB fkb01 magic");
    }
    view.Require(0, kFacebHeaderSize);

    FacebMaterialFrameTrack track;
    track.Source = view.Source;
    track.EntryCount = view.U32(0x04);
    track.ExpectedSize = static_cast<uint32_t>(kFacebHeaderSize +
                                               static_cast<size_t>(track.EntryCount) * kFacebEventSize);
    if (track.ExpectedSize > bytes.size()) {
        throw std::runtime_error(view.Source + ": FACEB declared entries exceed file length");
    }
    track.SizeMatchesEntryCount = track.ExpectedSize == bytes.size();
    track.Events.reserve(track.EntryCount);
    for (uint32_t index = 0; index < track.EntryCount; ++index) {
        const size_t offset = kFacebHeaderSize + static_cast<size_t>(index) * kFacebEventSize;
        FacebMaterialFrameEvent event;
        event.Index = index;
        event.Frame = view.U16(offset);
        event.EyeIndex = view.U8(offset + 0x02);
        event.MouthIndex = view.U8(offset + 0x03);
        track.Events.push_back(event);
    }
    return track;
}

FacebMaterialFrameTrack ParseFacebMaterialFrameTrackFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseFacebMaterialFrameTrackBytes(bytes, path.string());
}

FacebMaterialFrameSelection SampleFacebMaterialFrameTrack(const FacebMaterialFrameTrack& track,
                                                          float frame, uint8_t holdValue) {
    FacebMaterialFrameSelection selection;
    selection.HoldValue = holdValue;
    const float effectiveFrame = std::isfinite(frame) ? std::max(frame, 0.0f) : 0.0f;
    for (const auto& event : track.Events) {
        if (static_cast<float>(event.Frame) > effectiveFrame) {
            break;
        }
        if (event.EyeIndex != holdValue) {
            selection.EyeSelected = true;
            selection.EyeIndex = event.EyeIndex;
        }
        if (event.MouthIndex != holdValue) {
            selection.MouthSelected = true;
            selection.MouthIndex = event.MouthIndex;
        }
    }
    return selection;
}

namespace {

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

int32_t FindTextureIndexByName(const std::map<std::string, int32_t>& indexByName, const std::string& name) {
    const auto found = indexByName.find(NormalizeArchiveName(name));
    if (found == indexByName.end()) {
        return -1;
    }
    return found->second;
}

void PushUnique(std::vector<int32_t>& values, int32_t value) {
    if (value < 0) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::string CmabTextureRole(const std::vector<std::string>& textureNames) {
    if (textureNames.empty()) {
        return "material";
    }

    bool allEye = true;
    bool allMouth = true;
    for (const auto& name : textureNames) {
        const auto normalized = NormalizeArchiveName(name);
        allEye = allEye && StartsWith(normalized, "c_eye");
        allMouth = allMouth && StartsWith(normalized, "c_mouth");
    }
    if (allEye) {
        return "eye";
    }
    if (allMouth) {
        return "mouth";
    }
    return "material";
}

std::vector<CmabTextureSwapFrameBinding> BuildTextureSwapFrames(const CmabMmadRecord& record,
                                                                const CmabMaterialAnimationBinding& binding) {
    constexpr float kIntegralFrameEpsilon = 0.0001f;

    if (!record.ScalarKeyframesDecoded || record.ScalarKeyframes.empty()) {
        return {};
    }

    std::vector<CmabTextureSwapFrameBinding> frames;
    frames.reserve(record.ScalarKeyframes.size());
    for (uint32_t keyframeIndex = 0; keyframeIndex < record.ScalarKeyframes.size(); ++keyframeIndex) {
        const auto& keyframe = record.ScalarKeyframes[keyframeIndex];
        const float rounded = std::round(keyframe.Value);
        if (std::abs(keyframe.Value - rounded) > kIntegralFrameEpsilon || rounded < 0.0f ||
            rounded > static_cast<float>(std::numeric_limits<int32_t>::max())) {
            return {};
        }

        const auto textureFrameIndex = static_cast<int32_t>(rounded);
        if (textureFrameIndex < 0 || static_cast<size_t>(textureFrameIndex) >= binding.TextureNames.size()) {
            return {};
        }

        CmabTextureSwapFrameBinding frame;
        frame.KeyframeIndex = keyframeIndex;
        frame.Frame = keyframe.Frame;
        frame.Value = keyframe.Value;
        frame.TextureFrameIndex = textureFrameIndex;
        frame.TextureName = binding.TextureNames[static_cast<size_t>(textureFrameIndex)];
        frame.EmbeddedTextureIndex = binding.EmbeddedTextureIndices[static_cast<size_t>(textureFrameIndex)];
        frame.TargetTextureIndex = binding.TargetTextureIndices[static_cast<size_t>(textureFrameIndex)];
        frames.push_back(std::move(frame));
    }
    return frames;
}

} // namespace

float SampleCmabSourceCurve(const CmabSourceCurve& curve, float frame) {
    return SampleCmabSourceCurveImpl(curve, frame);
}

CmabMaterialAnimationBinding BuildCmabMaterialAnimationBinding(const CmbModel& targetModel,
                                                              const CmabMaterialAnimation& animation) {
    CmabMaterialAnimationBinding binding;
    binding.Source = animation.Source;
    binding.TargetModelSource = targetModel.Source;
    binding.FrameCountCandidate = animation.FrameCountCandidate;
    binding.LoopModeCandidate = animation.LoopModeCandidate;
    binding.TextureNames = animation.StringTableNames;
    binding.Role = CmabTextureRole(binding.TextureNames);

    if (binding.TextureNames.empty()) {
        binding.Status = "no_texture_names";
        return binding;
    }

    std::map<std::string, int32_t> targetTextureIndexByName;
    for (uint32_t textureIndex = 0; textureIndex < targetModel.Textures.size(); ++textureIndex) {
        const auto& texture = targetModel.Textures[textureIndex];
        if (!texture.Name.empty()) {
            targetTextureIndexByName[NormalizeArchiveName(texture.Name)] = static_cast<int32_t>(textureIndex);
        }
    }

    std::map<std::string, int32_t> embeddedTextureIndexByName;
    for (uint32_t textureIndex = 0; textureIndex < animation.EmbeddedTextures.size(); ++textureIndex) {
        const auto& texture = animation.EmbeddedTextures[textureIndex];
        if (!texture.Name.empty()) {
            embeddedTextureIndexByName[NormalizeArchiveName(texture.Name)] = static_cast<int32_t>(textureIndex);
        }
    }

    binding.TargetTextureIndices.reserve(binding.TextureNames.size());
    binding.EmbeddedTextureIndices.reserve(binding.TextureNames.size());
    std::set<int32_t> targetTextureIndexSet;
    for (const auto& textureName : binding.TextureNames) {
        const int32_t targetTextureIndex = FindTextureIndexByName(targetTextureIndexByName, textureName);
        const int32_t embeddedTextureIndex = FindTextureIndexByName(embeddedTextureIndexByName, textureName);
        binding.TargetTextureIndices.push_back(targetTextureIndex);
        binding.EmbeddedTextureIndices.push_back(embeddedTextureIndex);
        if (targetTextureIndex >= 0) {
            targetTextureIndexSet.insert(targetTextureIndex);
        }
    }

    for (const auto& material : targetModel.Materials) {
        const size_t mapperCount = std::min<size_t>(material.TextureMappersUsed, std::size(material.TextureMappers));
        for (size_t mapperIndex = 0; mapperIndex < mapperCount; ++mapperIndex) {
            const int32_t textureIndex = material.TextureMappers[mapperIndex].TextureIndex;
            if (textureIndex >= 0 && targetTextureIndexSet.contains(textureIndex)) {
                PushUnique(binding.TargetMaterialIndices, static_cast<int32_t>(material.Index));
                PushUnique(binding.NativeRuntimeMaterialLaneIndices, static_cast<int32_t>(material.Index));
                break;
            }
        }
    }
    binding.TargetResolved = !binding.TargetMaterialIndices.empty();
    binding.RuntimeLaneBindingDecoded = binding.TargetResolved;

    for (uint32_t recordIndex = 0; recordIndex < animation.MmadRecords.size(); ++recordIndex) {
        auto frames = BuildTextureSwapFrames(animation.MmadRecords[recordIndex], binding);
        if (!frames.empty()) {
            binding.MmadRecordIndex = static_cast<int32_t>(recordIndex);
            binding.TextureSwapFrames = std::move(frames);
            binding.TextureSwapTrackDecoded = true;
            break;
        }
    }

    binding.TextureSwapFramesResolved =
        binding.TextureSwapTrackDecoded &&
        std::all_of(binding.TextureSwapFrames.begin(), binding.TextureSwapFrames.end(), [](const auto& frame) {
            return frame.EmbeddedTextureIndex >= 0 || frame.TargetTextureIndex >= 0;
        });

    if (!binding.TargetResolved) {
        binding.Status = "no_target_material_lane";
    } else if (!binding.TextureSwapTrackDecoded) {
        binding.Status = "no_scalar_texture_swap_track";
    } else if (!binding.TextureSwapFramesResolved) {
        binding.Status = "unresolved_texture_frames";
    } else {
        binding.Status = "ready";
    }
    return binding;
}

ShbinShaderBinary ParseShbinShaderBinaryBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, std::move(source) };
    if (!view.HasMagic(0, "DVLB")) {
        throw std::runtime_error(view.Source + ": expected SHBIN DVLB magic");
    }
    view.Require(0, kShbinDvlbHeaderSize);

    ShbinShaderBinary shader;
    shader.Source = view.Source;
    shader.ProgramCount = view.U32(0x04);
    if (shader.ProgramCount == 0) {
        throw std::runtime_error(view.Source + ": SHBIN program count is zero");
    }

    auto checkedAbsolute = [&](size_t base, uint32_t relative) -> size_t {
        const auto absolute = static_cast<uint64_t>(base) + relative;
        if (absolute > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error(view.Source + ": SHBIN offset overflow");
        }
        return static_cast<size_t>(absolute);
    };

    std::vector<uint32_t> dvleOffsets;
    dvleOffsets.reserve(shader.ProgramCount);
    for (uint32_t i = 0; i < shader.ProgramCount; ++i) {
        const auto tableOffset = kShbinDvlbHeaderSize + static_cast<size_t>(i) * sizeof(uint32_t);
        dvleOffsets.push_back(view.U32(tableOffset));
    }

    const auto dvlpOffset64 = static_cast<uint64_t>(kShbinDvlbHeaderSize) +
                              static_cast<uint64_t>(shader.ProgramCount) * sizeof(uint32_t);
    if (dvlpOffset64 > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(view.Source + ": SHBIN DVLP offset overflow");
    }
    shader.DvlpOffset = static_cast<uint32_t>(dvlpOffset64);
    const auto dvlpOffset = static_cast<size_t>(shader.DvlpOffset);
    view.Require(dvlpOffset, kShbinDvlpHeaderSize);
    if (!view.HasMagic(dvlpOffset, "DVLP")) {
        throw std::runtime_error(view.Source + ": expected SHBIN DVLP magic");
    }

    shader.DvlpVersion = view.U32(dvlpOffset + 0x04);
    shader.BinaryOffset = view.U32(dvlpOffset + 0x08);
    shader.BinarySizeWords = view.U32(dvlpOffset + 0x0C);
    shader.SwizzleInfoOffset = view.U32(dvlpOffset + 0x10);
    shader.SwizzleInfoCount = view.U32(dvlpOffset + 0x14);
    shader.FilenameSymbolOffset = view.U32(dvlpOffset + 0x18);

    const auto binaryOffset = checkedAbsolute(dvlpOffset, shader.BinaryOffset);
    const auto binaryByteSize64 = static_cast<uint64_t>(shader.BinarySizeWords) * sizeof(uint32_t);
    if (binaryByteSize64 > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(view.Source + ": SHBIN program code size overflow");
    }
    const auto binaryByteSize = static_cast<size_t>(binaryByteSize64);
    view.Require(binaryOffset, binaryByteSize);
    shader.ProgramCode.reserve(shader.BinarySizeWords);
    for (uint32_t i = 0; i < shader.BinarySizeWords; ++i) {
        shader.ProgramCode.push_back(view.U32(binaryOffset + static_cast<size_t>(i) * sizeof(uint32_t)));
    }

    const auto swizzleOffset = checkedAbsolute(dvlpOffset, shader.SwizzleInfoOffset);
    const auto swizzleByteSize64 = static_cast<uint64_t>(shader.SwizzleInfoCount) * kShbinSwizzleInfoSize;
    if (swizzleByteSize64 > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(view.Source + ": SHBIN swizzle table size overflow");
    }
    view.Require(swizzleOffset, static_cast<size_t>(swizzleByteSize64));
    shader.Swizzles.reserve(shader.SwizzleInfoCount);
    for (uint32_t i = 0; i < shader.SwizzleInfoCount; ++i) {
        const auto offset = swizzleOffset + static_cast<size_t>(i) * kShbinSwizzleInfoSize;
        ShbinSwizzleInfo swizzle;
        swizzle.Pattern = view.U32(offset + 0x00);
        swizzle.Unknown = view.U32(offset + 0x04);
        shader.Swizzles.push_back(swizzle);
    }

    size_t filenameOffset = checkedAbsolute(dvlpOffset, shader.FilenameSymbolOffset);
    shader.Filenames.reserve(shader.ProgramCount);
    for (uint32_t i = 0; i < shader.ProgramCount; ++i) {
        auto filename = ReadBoundedCString(view, filenameOffset, view.Bytes.size());
        filenameOffset += filename.size() + 1;
        shader.Filenames.push_back(std::move(filename));
    }

    shader.Programs.reserve(shader.ProgramCount);
    for (uint32_t programIndex = 0; programIndex < shader.ProgramCount; ++programIndex) {
        const auto dvleOffset = static_cast<size_t>(dvleOffsets[programIndex]);
        view.Require(dvleOffset, kShbinDvleHeaderSize);
        if (!view.HasMagic(dvleOffset, "DVLE")) {
            throw std::runtime_error(view.Source + ": expected SHBIN DVLE magic");
        }

        ShbinProgram program;
        program.DvleOffset = dvleOffsets[programIndex];
        program.ShaderType = view.U8(dvleOffset + 0x06);
        program.MainOffsetWords = view.U32(dvleOffset + 0x08);
        program.EndMainOffsetWords = view.U32(dvleOffset + 0x0C);

        const auto constantTableOffset = checkedAbsolute(dvleOffset, view.U32(dvleOffset + 0x18));
        const auto constantTableSize = view.U32(dvleOffset + 0x1C);
        const auto outputTableOffset = checkedAbsolute(dvleOffset, view.U32(dvleOffset + 0x28));
        const auto outputTableSize = view.U32(dvleOffset + 0x2C);
        const auto uniformTableOffset = checkedAbsolute(dvleOffset, view.U32(dvleOffset + 0x30));
        const auto uniformTableSize = view.U32(dvleOffset + 0x34);
        const auto symbolTableOffset = checkedAbsolute(dvleOffset, view.U32(dvleOffset + 0x38));
        const auto symbolTableSize = view.U32(dvleOffset + 0x3C);
        view.Require(symbolTableOffset, symbolTableSize);

        const auto constantTableByteSize64 = static_cast<uint64_t>(constantTableSize) * kShbinConstantInfoSize;
        if (constantTableByteSize64 > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error(view.Source + ": SHBIN constant table size overflow");
        }
        view.Require(constantTableOffset, static_cast<size_t>(constantTableByteSize64));
        program.Constants.reserve(constantTableSize);
        for (uint32_t i = 0; i < constantTableSize; ++i) {
            const auto offset = constantTableOffset + static_cast<size_t>(i) * kShbinConstantInfoSize;
            ShbinConstantInfo constant;
            constant.RawTypeAndRegister = view.U32(offset + 0x00);
            constant.Type = constant.RawTypeAndRegister & 0x03;
            constant.RegisterId = (constant.RawTypeAndRegister >> 16) & 0xFF;
            constant.Values = { view.U32(offset + 0x04), view.U32(offset + 0x08), view.U32(offset + 0x0C),
                                view.U32(offset + 0x10) };
            program.Constants.push_back(constant);
        }

        const auto outputTableByteSize64 = static_cast<uint64_t>(outputTableSize) * kShbinOutputRegisterInfoSize;
        if (outputTableByteSize64 > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error(view.Source + ": SHBIN output register table size overflow");
        }
        view.Require(outputTableOffset, static_cast<size_t>(outputTableByteSize64));
        program.Outputs.reserve(outputTableSize);
        for (uint32_t i = 0; i < outputTableSize; ++i) {
            const auto offset = outputTableOffset + static_cast<size_t>(i) * kShbinOutputRegisterInfoSize;
            ShbinOutputRegisterInfo output;
            output.Raw = view.U64(offset);
            output.Type = static_cast<uint16_t>(output.Raw & 0xFFFF);
            output.RegisterId = static_cast<uint16_t>((output.Raw >> 16) & 0xFFFF);
            output.ComponentMask = static_cast<uint8_t>((output.Raw >> 32) & 0x0F);
            output.Descriptor = static_cast<uint32_t>((output.Raw >> 32) & 0xFFFFFFFF);
            output.SemanticName = ShbinOutputSemanticName(output.Type);
            program.Outputs.push_back(std::move(output));
        }

        const auto uniformTableByteSize64 = static_cast<uint64_t>(uniformTableSize) * kShbinUniformInfoBasicSize;
        if (uniformTableByteSize64 > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error(view.Source + ": SHBIN uniform table size overflow");
        }
        view.Require(uniformTableOffset, static_cast<size_t>(uniformTableByteSize64));
        program.Uniforms.reserve(uniformTableSize);
        for (uint32_t i = 0; i < uniformTableSize; ++i) {
            const auto offset = uniformTableOffset + static_cast<size_t>(i) * kShbinUniformInfoBasicSize;
            const auto registers = view.U32(offset + 0x04);
            ShbinUniformInfo uniform;
            uniform.SymbolOffset = view.U32(offset + 0x00);
            uniform.RegisterStart = static_cast<uint16_t>(registers & 0xFFFF);
            uniform.RegisterEnd = static_cast<uint16_t>((registers >> 16) & 0xFFFF);
            uniform.Name = ReadShbinSymbol(view, symbolTableOffset, symbolTableSize, uniform.SymbolOffset);
            program.Uniforms.push_back(std::move(uniform));
        }

        shader.Programs.push_back(std::move(program));
    }

    return shader;
}

ShbinShaderBinary ParseShbinShaderBinaryFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseShbinShaderBinaryBytes(bytes, path.string());
}

CtxbTexture ParseCtxbTextureBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, std::move(source) };
    if (!view.HasMagic(0, "ctxb")) {
        throw std::runtime_error(view.Source + ": expected CTXB magic");
    }
    if (bytes.size() < kCtxbTextureHeaderSize) {
        throw std::runtime_error(view.Source + ": CTXB header is truncated");
    }

    CtxbTexture texture;
    texture.Source = view.Source;
    texture.Name = TextureNameFromSource(view.Source);
    texture.DeclaredFileSize = view.U32(0x04);
    texture.HeaderVersion = view.U32(0x08);
    texture.HeaderWord0C = view.U32(0x0C);
    texture.TexChunkOffset = view.U32(0x10);
    texture.PayloadOffset = view.U32(0x14);
    if (texture.DeclaredFileSize != bytes.size()) {
        throw std::runtime_error(view.Source + ": CTXB declared size does not match file length");
    }
    if (texture.HeaderVersion != 1) {
        throw std::runtime_error(view.Source + ": only OOT3D CTXB version 1 is supported");
    }
    if (texture.HeaderWord0C != 0) {
        throw std::runtime_error(view.Source + ": CTXB header word 0x0C is nonzero");
    }
    if (texture.TexChunkOffset != kCtxbTexChunkOffset || texture.PayloadOffset != kCtxbPayloadOffset) {
        throw std::runtime_error(view.Source + ": CTXB texture chunk offsets do not match OOT3D layout");
    }
    if (!view.HasMagic(texture.TexChunkOffset, "tex ")) {
        throw std::runtime_error(view.Source + ": expected CTXB TEX chunk");
    }

    texture.TexChunkSize = view.U32(0x1C);
    texture.TextureCount = view.U32(0x20);
    texture.PayloadSize = view.U32(0x24);
    texture.Flags = view.U32(0x28);
    texture.Width = view.U16(0x2C);
    texture.Height = view.U16(0x2E);
    texture.TextureFormat = view.U16(0x30);
    texture.DataType = view.U16(0x32);
    texture.SamplerWord = view.U32(0x30);

    if (texture.TexChunkSize != 0x30) {
        throw std::runtime_error(view.Source + ": CTXB TEX chunk size is not 0x30");
    }
    if (texture.TextureCount != 1) {
        throw std::runtime_error(view.Source + ": CTXB texture count is not 1");
    }
    if (texture.PayloadSize != bytes.size() - texture.PayloadOffset) {
        throw std::runtime_error(view.Source + ": CTXB payload size does not match file length");
    }
    if (texture.Width == 0 || texture.Height == 0) {
        throw std::runtime_error(view.Source + ": CTXB dimensions are invalid");
    }

    view.Require(texture.PayloadOffset, texture.PayloadSize);
    texture.Data.assign(view.Bytes.begin() + static_cast<std::ptrdiff_t>(texture.PayloadOffset),
                        view.Bytes.begin() + static_cast<std::ptrdiff_t>(texture.PayloadOffset + texture.PayloadSize));

    auto decodeSource = CmbTextureFromCtxb(texture);
    texture.Rgba8 = DecodeTextureRgba8(decodeSource);
    texture.Rgba8Decoded = texture.Rgba8.size() == static_cast<size_t>(texture.Width) * texture.Height * 4;
    return texture;
}

CtxbTexture ParseCtxbTextureFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseCtxbTextureBytes(bytes, path.string());
}

NativeCtxbDescriptorSlot BuildNativeCtxbDescriptorSlot(const CtxbTexture& texture, uint32_t slotIndex) {
    NativeCtxbDescriptorSlot slot;
    slot.SlotIndex = slotIndex;
    slot.SlotStrideBytes = kNativeCtxbDescriptorSlotStrideBytes;
    slot.SlotRecordBaseOffset = kNativeCtxbDescriptorSlotBaseOffset + slotIndex * kNativeCtxbDescriptorSlotStrideBytes;
    slot.SourceTextureHeaderBaseOffset = kNativeCtxbSourceTextureHeaderBaseOffset;
    slot.SourcePayloadPointerFieldOffset = kNativeCtxbSourcePayloadPointerFieldOffset;
    slot.TextureHeaderParameter = static_cast<int16_t>(texture.Flags & 0xFFFF);
    slot.Width = texture.Width;
    slot.Height = texture.Height;
    slot.TextureFormat = texture.TextureFormat;
    slot.DataType = texture.DataType;
    slot.PackedFormatDataType = texture.SamplerWord;
    slot.PayloadSize = texture.PayloadSize;
    return slot;
}

std::vector<NativeLightSettingsTransitionMode> ParseNativeLightSettingsTransitionTableBytes(
    std::span<const uint8_t> bytes, uint32_t modeCount, uint32_t entryCount, uint32_t modeStrideBytes,
    uint32_t entrySizeBytes) {
    if (entrySizeBytes < 6) {
        throw std::runtime_error("native light-settings transition entry is too small");
    }
    if (modeCount == 0 || entryCount == 0) {
        return {};
    }
    const size_t requiredSize = static_cast<size_t>(modeCount == 0 ? 0 : modeCount - 1) * modeStrideBytes +
                                static_cast<size_t>(entryCount == 0 ? 0 : entryCount - 1) * entrySizeBytes + 6;
    if (requiredSize > bytes.size()) {
        throw std::runtime_error("native light-settings transition table is truncated");
    }

    std::vector<NativeLightSettingsTransitionMode> modes;
    modes.reserve(modeCount);
    for (uint32_t modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
        NativeLightSettingsTransitionMode mode;
        mode.ModeIndex = modeIndex;
        mode.Entries.reserve(entryCount);
        for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
            const size_t offset = static_cast<size_t>(modeIndex) * modeStrideBytes +
                                  static_cast<size_t>(entryIndex) * entrySizeBytes;
            NativeLightSettingsTransitionEntry entry;
            entry.StartAngle =
                static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1] << 8);
            entry.EndAngle =
                static_cast<uint16_t>(bytes[offset + 2]) | static_cast<uint16_t>(bytes[offset + 3] << 8);
            entry.FromLightSettingIndex = bytes[offset + 4];
            entry.ToLightSettingIndex = bytes[offset + 5];
            mode.Entries.push_back(entry);
        }
        modes.push_back(std::move(mode));
    }
    return modes;
}

std::vector<NativeLightSettingsTransitionMode> ParseNativeLightSettingsTransitionTableFromCodeBinBytes(
    std::span<const uint8_t> bytes, uint32_t codeBase, uint32_t tableAddress, uint32_t modeCount,
    uint32_t entryCount, uint32_t modeStrideBytes, uint32_t entrySizeBytes) {
    if (tableAddress < codeBase) {
        throw std::runtime_error("native light-settings transition table address is below code base");
    }
    if (modeCount == 0 || entryCount == 0) {
        return {};
    }
    const size_t tableOffset = static_cast<size_t>(tableAddress - codeBase);
    const size_t tableSize = static_cast<size_t>(modeCount == 0 ? 0 : modeCount - 1) * modeStrideBytes +
                             static_cast<size_t>(entryCount == 0 ? 0 : entryCount - 1) * entrySizeBytes + 6;
    if (tableOffset > bytes.size() || tableSize > bytes.size() - tableOffset) {
        throw std::runtime_error("native light-settings transition table is outside code.bin");
    }
    return ParseNativeLightSettingsTransitionTableBytes(bytes.subspan(tableOffset, tableSize), modeCount, entryCount,
                                                       modeStrideBytes, entrySizeBytes);
}

std::vector<NativeLightSettingsTransitionMode> ParseNativeLightSettingsTransitionTableFromCodeBinFile(
    const std::filesystem::path& path, uint32_t codeBase, uint32_t tableAddress, uint32_t modeCount,
    uint32_t entryCount, uint32_t modeStrideBytes, uint32_t entrySizeBytes) {
    const auto bytes = ReadFileBytes(path);
    return ParseNativeLightSettingsTransitionTableFromCodeBinBytes(bytes, codeBase, tableAddress, modeCount, entryCount,
                                                                  modeStrideBytes, entrySizeBytes);
}

NativeActorProfile ParseNativeActorProfileFromCodeBinBytes(std::span<const uint8_t> bytes, uint16_t actorId) {
    constexpr uint32_t kCodeBase = 0x00100000;
    constexpr uint32_t kOverlayTablePointerLiteralAddress = 0x00373B64;
    constexpr uint32_t kOverlayEntryStrideBytes = 0x20;
    constexpr uint32_t kOverlayEntryProfilePointerOffset = 0x14;
    constexpr uint32_t kProfileSizeBytes = 0x20;

    NativeActorProfile profile;
    profile.ActorId = actorId;
    profile.SourceKind = "oot3d_code_bin_actor_overlay_table_0050cd84_actor_profile";
    const BinaryView view{ bytes, "OOT3D code.bin actor profile" };
    const auto runtimeToOffset = [&](uint32_t address, size_t size) -> size_t {
        if (address < kCodeBase) {
            throw std::runtime_error("native actor profile address is below code base");
        }
        const size_t offset = static_cast<size_t>(address - kCodeBase);
        if (offset > bytes.size() || size > bytes.size() - offset) {
            throw std::runtime_error("native actor profile address is outside code.bin");
        }
        return offset;
    };

    const size_t tableLiteralOffset = runtimeToOffset(kOverlayTablePointerLiteralAddress, sizeof(uint32_t));
    profile.OverlayTableAddress = view.U32(tableLiteralOffset);
    profile.OverlayEntryAddress =
        profile.OverlayTableAddress + static_cast<uint32_t>(actorId) * kOverlayEntryStrideBytes;
    const size_t overlayEntryOffset = runtimeToOffset(profile.OverlayEntryAddress, kOverlayEntryStrideBytes);
    profile.ProfileAddress = view.U32(overlayEntryOffset + kOverlayEntryProfilePointerOffset);
    if (profile.ProfileAddress == 0) {
        return profile;
    }

    const size_t profileOffset = runtimeToOffset(profile.ProfileAddress, kProfileSizeBytes);
    const uint16_t profileActorId = view.U16(profileOffset);
    if (profileActorId != actorId) {
        return profile;
    }
    profile.Category = view.U8(profileOffset + 0x02);
    profile.Flags = view.U32(profileOffset + 0x04);
    profile.ObjectId = view.U16(profileOffset + 0x08);
    profile.InstanceSize = view.U32(profileOffset + 0x0C);
    profile.InitFunctionAddress = view.U32(profileOffset + 0x10);
    profile.DestroyFunctionAddress = view.U32(profileOffset + 0x14);
    profile.UpdateFunctionAddress = view.U32(profileOffset + 0x18);
    profile.DrawFunctionAddress = view.U32(profileOffset + 0x1C);
    profile.Valid = true;
    return profile;
}

NativeActorProfile ParseNativeActorProfileFromCodeBinFile(const std::filesystem::path& path, uint16_t actorId) {
    const auto bytes = ReadFileBytes(path);
    return ParseNativeActorProfileFromCodeBinBytes(bytes, actorId);
}

NativeActorVisualBehavior ParseNativeActorVisualBehaviorFromCodeBinBytes(
    std::span<const uint8_t> bytes, const NativeActorProfile& profile) {
    constexpr uint32_t kCodeBase = 0x00100000;
    constexpr uint16_t kDoughnutObjectId = 0x017A;
    NativeActorVisualBehavior behavior;
    if (!profile.Valid || profile.ObjectId != kDoughnutObjectId) {
        return behavior;
    }

    const BinaryView view{ bytes, "OOT3D code.bin actor visual behavior" };
    const auto runtimeToOffset = [&](uint32_t address, size_t size) -> size_t {
        if (address < kCodeBase) {
            throw std::runtime_error("native actor behavior address is below code base");
        }
        const size_t offset = static_cast<size_t>(address - kCodeBase);
        if (offset > bytes.size() || size > bytes.size() - offset) {
            throw std::runtime_error("native actor behavior address is outside code.bin");
        }
        return offset;
    };
    const auto decodeMovImmediate = [](uint32_t instruction, uint32_t expectedRegister) -> std::optional<uint32_t> {
        if ((instruction & 0x0FE0F000) != (0x03A00000 | (expectedRegister << 12)) ||
            (instruction & 0x00000F00) != 0) {
            return std::nullopt;
        }
        return instruction & 0xFF;
    };

    const size_t initOffset = runtimeToOffset(profile.InitFunctionAddress, 0x1D0);
    const auto fieryIndex = decodeMovImmediate(view.U32(initOffset + 0x1C), 3);
    const auto normalIndex = decodeMovImmediate(view.U32(initOffset + 0x20), 2);
    if (!fieryIndex.has_value() || !normalIndex.has_value()) {
        return behavior;
    }

    const uint32_t scaleLoadInstruction = view.U32(initOffset + 0x5C);
    if ((scaleLoadInstruction & 0x0F3F0F00) != 0x0D1F0A00) {
        return behavior;
    }
    const uint32_t scaleLoadAddress = profile.InitFunctionAddress + 0x5C + 8 +
                                      (scaleLoadInstruction & 0x00800000 ? 1u : static_cast<uint32_t>(-1)) *
                                          ((scaleLoadInstruction & 0xFF) * 4);
    const float defaultScale = view.F32(runtimeToOffset(scaleLoadAddress, sizeof(float)));

    const uint32_t paramScaleTableAddress = view.U32(initOffset + 0x1C8);
    const float paramScaleMultiplier = view.F32(initOffset + 0x1CC);
    const size_t paramScaleTableOffset = runtimeToOffset(paramScaleTableAddress, 5 * sizeof(int16_t));

    const size_t updateOffset = runtimeToOffset(profile.UpdateFunctionAddress, 0x24);
    const uint32_t rotationStepInstruction = view.U32(updateOffset + 0x20);
    if ((rotationStepInstruction & 0x0FFFFF00) != 0x02411000) {
        return behavior;
    }

    behavior.Valid = true;
    behavior.SourceKind =
        "oot3d_code_bin_BgSpot16Doughnut_model_scale_rotation_instruction_decode";
    behavior.FieryCmbTypeLocalIndex = *fieryIndex;
    behavior.NormalCmbTypeLocalIndex = *normalIndex;
    behavior.DefaultScale = defaultScale;
    for (size_t index = 0; index < behavior.ParamScales.size(); ++index) {
        behavior.ParamScales[index] =
            static_cast<float>(view.S16(paramScaleTableOffset + index * sizeof(int16_t))) * paramScaleMultiplier;
    }
    behavior.RotationYStepS16PerTick = -static_cast<int32_t>(rotationStepInstruction & 0xFF);
    return behavior;
}

NativeKankyoRuntimeBridgeContract BuildNativeKankyoRuntimeBridgeContract() {
    NativeKankyoRuntimeBridgeContract contract;
    contract.SourceKind = "oot3d_code_bin_z_kankyo_ctxb_runtime_bridge";
    contract.KankyoObjectInitializerAddress = 0x0044FF38;
    contract.KankyoObjectInitializerEndAddress = 0x00450B5F;
    contract.RuntimeHelpers = {
        {
            0x00340D00,
            0x35C,
            0x28C,
            0x52,
            0x53,
            0x5D,
            0x004970E0,
        },
        {
            0x0034897C,
            0x35C,
            0x1E4,
            0x2E,
            0x2F,
            0x39,
            0x002C4F00,
        },
    };
    contract.RuntimeEffectWrapper.FunctionAddress = 0x0034897C;
    contract.RuntimeEffectWrapper.ManagerArgumentOrdinal = 1;
    contract.RuntimeEffectWrapper.DescriptorObjectArgumentOrdinal = 2;
    contract.RuntimeEffectWrapper.OptionalBackingStorageArgumentOrdinal = 3;
    contract.RuntimeEffectWrapper.BackingCallsiteScanOutputPath =
        "analysis/runtime_effect_wrapper_backings.csv";
    contract.RuntimeEffectWrapper.ContainerStorageSize = 0x35C;
    contract.RuntimeEffectWrapper.InstanceStorageSize = 0x1E4;
    contract.RuntimeEffectWrapper.OptionalBackingStorageSize = 0x234;
    contract.RuntimeEffectWrapper.ContainerClassId = 0x2E;
    contract.RuntimeEffectWrapper.InstanceClassId = 0x2F;
    contract.RuntimeEffectWrapper.OptionalBackingStorageClassId = 0x39;
    contract.RuntimeEffectWrapper.OptionalBackingStorageDefaultInitializerAddress = 0x00347258;
    contract.RuntimeEffectWrapper.ContainerInitializerAddress = 0x002C50D4;
    contract.RuntimeEffectWrapper.InstanceInitializerAddress = 0x002C4F00;
    contract.RuntimeEffectWrapper.ContainerVtableAddress = 0x004EBE00;
    contract.RuntimeEffectWrapper.ContainerSubmitVtableSlotOffset = 0x08;
    contract.RuntimeEffectWrapper.ContainerSubmitVtableEntryAddress = 0x004EBE08;
    contract.RuntimeEffectWrapper.ContainerSubmitFunctionAddress = 0x003FBBA8;
    contract.RuntimeEffectWrapper.ContainerDescriptorPointerOffset = 0x350;
    contract.RuntimeEffectWrapper.ContainerInstancePointerOffset = 0x354;
    contract.RuntimeEffectWrapper.ContainerBackingStoragePointerOffset = 0x358;
    contract.RuntimeEffectWrapper.InstanceBackingStoragePointerOffset = 0x1DC;
    contract.RuntimeEffectWrapper.ManagerWordCopiedToBackingStorageOffset = 0x00;
    contract.RuntimeEffectWrapper.ReturnValueIsInstancePointer = true;
    contract.RuntimeEffectWrapper.ReturnValueIsContainerPointer = false;
    contract.RuntimeEffectWrapper.ReturnValueIsPacketPrepInput = false;
    contract.RuntimeEffectWrapper.ContainerBackingStoragePointerIsPacketPrepInput = true;
    contract.RuntimeEffectWrapper.WritesDrawHandlePacketBuffer = false;
    contract.RuntimeEffectWrapper.NullOptionalBackingAllocatesDefaultStorage = true;
    contract.RuntimeEffectWrapper.ExternalOptionalBackingBypassesDefaultStorageAllocation = true;
    contract.RuntimeEffectWrapper.OptionalBackingArgumentStoredAtContainerBackingStoragePointer = true;
    contract.RuntimeEffectWrapper.DefaultAllocatedBackingStoredAtInstanceBackingStoragePointer = true;
    contract.RuntimeEffectWrapper.WrapperCallsiteScanTotalCount = 72;
    contract.RuntimeEffectWrapper.WrapperCallsiteScanNullBackingCount = 60;
    contract.RuntimeEffectWrapper.WrapperCallsiteScanExternalActorEffectBackingCount = 12;
    contract.RuntimeEffectWrapper.ExternalActorEffectBackingFieldOffset = 0x178;
    contract.RuntimeEffectWrapper.ExternalActorEffectBackingFunctionAddresses = {
        0x0018A8E0, 0x001AEB9C, 0x001D44E0, 0x00213EDC,
        0x0022345C, 0x00231920, 0x0024C3E8, 0x002705B8,
        0x00278D78, 0x002925AC,
    };
    contract.RuntimeEffectWrapper.ExternalActorEffectBackingCallsiteAddresses = {
        0x0018AAA0, 0x001AECD4, 0x001D4628, 0x00214200,
        0x0022394C, 0x00223B48, 0x00231A7C, 0x0024C4B0,
        0x0024C5A8, 0x00270AB0, 0x00278EA0, 0x00292670,
    };
    contract.Bindings = {
        {
            0x44,
            0x44,
            0x1E4,
            0,
            1,
            0,
            1,
            0x1E8,
            0,
            0x00340D00,
            0x004505A8,
            0,
            0,
            0,
            0,
        },
        {
            0x45,
            0x45,
            0x1EC,
            0,
            1,
            0,
            1,
            0x1F0,
            0,
            0x00340D00,
            0x00450738,
            0,
            0,
            0,
            0,
        },
        {
            0x46,
            0x49,
            0x1F4,
            4,
            12,
            0,
            1,
            0x224,
            4,
            0x0034897C,
            0x00450898,
            0x00348BE4,
            0x00450840,
            0,
            0,
        },
        {
            0x4A,
            0x4B,
            0x270,
            0,
            1,
            0,
            2,
            0x274,
            0,
            0x0034897C,
            0x00450AF4,
            0,
            0,
            0x178,
            0x10,
        },
    };
    contract.ThunderUpdate.FunctionAddress = 0x0045FEB0;
    contract.ThunderUpdate.FunctionEndAddress = 0x0046048B;
    contract.ThunderUpdate.SlotCount = 12;
    contract.ThunderUpdate.SelectorModulo = 4;
    contract.ThunderUpdate.SelectorSampleCallsiteAddress = 0x004600B4;
    contract.ThunderUpdate.PayloadObjectOffsets = { 0x254, 0x258, 0x25C, 0x260 };
    contract.ThunderUpdate.InitialPayloadObjectOffset = contract.ThunderUpdate.PayloadObjectOffsets[0];
    contract.ThunderUpdate.DescriptorObjectOffset = 0x1F4;
    contract.ThunderUpdate.DescriptorObjectStrideBytes = 4;
    contract.ThunderUpdate.DescriptorSlot = 0;
    contract.ThunderUpdate.DescriptorRebindCallsiteAddress = 0x00460100;
    contract.ThunderUpdate.RuntimeInstanceOffset = 0x224;
    contract.ThunderUpdate.RuntimeInstanceStrideBytes = 4;
    contract.ThunderUpdate.RuntimeSubmitHelperAddress = 0x00371EAC;
    contract.ThunderUpdate.RuntimeSubmitCallsiteAddress = 0x00460468;
    contract.ThunderUpdate.RuntimeTranslationOffsets = { 0x3C, 0x40, 0x44 };
    contract.ThunderUpdate.RuntimeTransformMatrixOffset = 0x54;
    contract.ThunderUpdate.RuntimeUvTransformOffset = 0x110;
    for (uint32_t slot = 0; slot < contract.ThunderUpdate.SlotCount; ++slot) {
        contract.ThunderUpdate.RuntimeSlots.push_back({
            slot,
            contract.ThunderUpdate.DescriptorObjectOffset + slot * contract.ThunderUpdate.DescriptorObjectStrideBytes,
            contract.ThunderUpdate.RuntimeInstanceOffset + slot * contract.ThunderUpdate.RuntimeInstanceStrideBytes,
            contract.ThunderUpdate.InitialPayloadObjectOffset,
            contract.ThunderUpdate.DescriptorSlot,
        });
    }
    contract.ThunderUpdate.InitBindsInitialPayloadToAllSlots = true;
    contract.ThunderUpdate.UpdateRebindsSelectedPayloadByModulo = true;
    contract.ThunderUpdate.RuntimeSlotsSubmittedByThunderUpdate = true;
    contract.RuntimeBindingSlots = {
        {
            0,
            0,
            contract.Bindings[0].SubresourceIdStart,
            contract.Bindings[0].SubresourceIdEnd,
            contract.Bindings[0].SubresourceIdStart,
            contract.Bindings[0].DescriptorObjectOffset,
            contract.Bindings[0].DescriptorSlot,
            contract.Bindings[0].RuntimeInstanceOffset,
            contract.Bindings[0].RuntimeHelperAddress,
            contract.Bindings[0].RuntimeCallsiteAddress,
            false,
            false,
            true,
            false,
        },
        {
            1,
            0,
            contract.Bindings[1].SubresourceIdStart,
            contract.Bindings[1].SubresourceIdEnd,
            contract.Bindings[1].SubresourceIdStart,
            contract.Bindings[1].DescriptorObjectOffset,
            contract.Bindings[1].DescriptorSlot,
            contract.Bindings[1].RuntimeInstanceOffset,
            contract.Bindings[1].RuntimeHelperAddress,
            contract.Bindings[1].RuntimeCallsiteAddress,
            false,
            false,
            true,
            false,
        },
    };
    for (const auto& slot : contract.ThunderUpdate.RuntimeSlots) {
        contract.RuntimeBindingSlots.push_back({
            2,
            slot.SlotIndex,
            contract.Bindings[2].SubresourceIdStart,
            contract.Bindings[2].SubresourceIdEnd,
            contract.Bindings[2].SubresourceIdStart,
            slot.DescriptorObjectOffset,
            slot.DescriptorSlot,
            slot.RuntimeInstanceOffset,
            contract.Bindings[2].RuntimeHelperAddress,
            contract.Bindings[2].RuntimeCallsiteAddress,
            false,
            true,
            true,
            false,
        });
    }
    for (uint32_t slot = 0; slot < contract.Bindings[3].DescriptorSlotCount; ++slot) {
        contract.RuntimeBindingSlots.push_back({
            3,
            slot,
            contract.Bindings[3].SubresourceIdStart + slot,
            contract.Bindings[3].SubresourceIdStart + slot,
            contract.Bindings[3].SubresourceIdStart + slot,
            contract.Bindings[3].DescriptorObjectOffset,
            slot,
            contract.Bindings[3].RuntimeInstanceOffset,
            contract.Bindings[3].RuntimeHelperAddress,
            contract.Bindings[3].RuntimeCallsiteAddress,
            true,
            false,
            false,
            true,
        });
    }

    contract.DescriptorMaterialization.FunctionAddress = 0x00348BE4;
    contract.DescriptorMaterialization.FunctionEndAddress = 0x00348F0F;
    contract.DescriptorMaterialization.DescriptorRecordCountOffset = 0x0C;
    contract.DescriptorMaterialization.DescriptorFlagsOffset = 0x1C;
    contract.DescriptorMaterialization.DefaultRecordCount = 4;
    contract.DescriptorMaterialization.MaterializedFlagFieldOffset = 0x19C;
    contract.DescriptorMaterialization.BufferSetCountFieldOffset = 0x128;
    contract.DescriptorMaterialization.BufferSetPointerBaseOffset = 0x1A0;
    contract.DescriptorMaterialization.SecondaryBufferSetPointerBaseOffset = 0x1A8;
    contract.DescriptorMaterialization.BasePayloadPointerOffset = 0x00;
    contract.DescriptorMaterialization.Flag0x80PayloadPointerOffset = 0x11C;
    contract.DescriptorMaterialization.Flag0x10PayloadPointerOffset = 0x04;
    contract.DescriptorMaterialization.Flag0x08PayloadPointerOffset = 0x08;
    contract.DescriptorMaterialization.Flag0x40PayloadPointerOffset = 0x118;

    contract.CtxbDescriptorBinding.FunctionAddress = 0x00348A64;
    contract.CtxbDescriptorBinding.FunctionEndAddress = 0x00348B83;
    contract.CtxbDescriptorBinding.DescriptorSlotBaseOffset = 0x13C;
    contract.CtxbDescriptorBinding.DescriptorSlotStrideBytes = 0x30;
    contract.CtxbDescriptorBinding.GeneratedDescriptorWordOffsets = { 0x13C, 0x140, 0x144, 0x148 };
    contract.CtxbDescriptorBinding.SourceTextureHeaderBaseOffset = 0x24;
    contract.CtxbDescriptorBinding.SourceTextureParameterHalfwordOffset = 0x28;
    contract.CtxbDescriptorBinding.SourcePayloadPointerFieldOffset = 0x4C;
    contract.CtxbDescriptorBinding.SourceWidthHalfwordOffset = 0x2C;
    contract.CtxbDescriptorBinding.SourceHeightHalfwordOffset = 0x2E;
    contract.CtxbDescriptorBinding.SourceFormatHalfwordOffset = 0x30;
    contract.CtxbDescriptorBinding.SourceDataTypeHalfwordOffset = 0x32;
    contract.CtxbDescriptorBinding.PayloadPointerDestinationOffset = 0x158;
    contract.CtxbDescriptorBinding.WidthDestinationHalfwordOffset = 0x15C;
    contract.CtxbDescriptorBinding.HeightDestinationHalfwordOffset = 0x15E;
    contract.CtxbDescriptorBinding.SlotOrdinalDestinationOffset = 0x160;
    contract.CtxbDescriptorBinding.PackedFormatDataTypeDestinationOffset = 0x164;
    contract.CtxbDescriptorBinding.FormatDataTypePackHelperAddress = 0x0030807C;
    contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerSelectHelperAddress = 0x0030835C;
    contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerDestinationOffset = 0x10;
    contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerFallbackLiteralAddress = 0x00348B84;
    contract.CtxbDescriptorBinding.WritesDescriptorTextureMetadata = true;
    contract.CtxbDescriptorBinding.WritesPacketPrepSourceSlots = false;
    contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerWritesDrawHandlePacketPrepSource = false;

    contract.DrawCommand.FunctionAddress = 0x003FD1B8;
    contract.DrawCommand.FunctionEndAddress = 0x003FD20B;
    contract.DrawCommand.AllocateCommandHelperAddress = 0x00324154;
    contract.DrawCommand.AllocateCommandHelperEndAddress = 0x0032418F;
    contract.DrawCommand.DrawCommandListOffset = 0x3410;
    contract.DrawCommand.CommandListOwnerFieldOffset = 0x00;
    contract.DrawCommand.CommandListCountFieldOffset = 0x04;
    contract.DrawCommand.CommandRecordBaseOffset = 0x08;
    contract.DrawCommand.CommandRecordStrideBytes = 0x20;
    contract.DrawCommand.CommandListMaxRecordCount = 0x31;
    contract.DrawCommand.CommandListGuardExclusiveCount = 0x32;
    contract.DrawCommand.CommandRecordListOwnerFieldOffset = 0x00;
    contract.DrawCommand.CommandRecordAllocatorArgumentFieldOffset = 0x04;
    contract.DrawCommand.CommandType = 6;
    contract.DrawCommand.CommandTypeFieldOffset = 0x1C;
    contract.DrawCommand.OwnerContextFieldOffset = 0x08;
    contract.DrawCommand.RuntimeBlockFieldOffset = 0x0C;
    contract.DrawCommand.SchedulerContextFieldOffset = 0x10;
    contract.DrawCommand.AllocationFailureReturnsNull = true;

    contract.EffectDrawConsumer.DrawFunctionAddress = 0x003FB5EC;
    contract.EffectDrawConsumer.DrawFunctionEndAddress = 0x003FB99F;
    contract.EffectDrawConsumer.SetupFunctionAddress = 0x003FB9AC;
    contract.EffectDrawConsumer.SetupFunctionEndAddress = 0x003FBBA3;
    contract.EffectDrawConsumer.RenderStateOffset = 0x18;
    contract.EffectDrawConsumer.DescriptorObjectPointerOffset = 0x350;
    contract.EffectDrawConsumer.RuntimeInstancePointerOffset = 0x354;
    contract.EffectDrawConsumer.LightListOffset = 0x50;
    contract.EffectDrawConsumer.LightListCacheValidByteOffset = 0x06;
    contract.EffectDrawConsumer.LightListBuffer0PointerOffset = 0x00;
    contract.EffectDrawConsumer.LightListBuffer1PointerOffset = 0x04;
    contract.EffectDrawConsumer.LightListInitialCapacity = 8;
    contract.EffectDrawConsumer.LightListCapacityOffset = 0x08;
    contract.EffectDrawConsumer.LightListPrimaryCountOffset = 0x0C;
    contract.EffectDrawConsumer.LightListDefaultCountOffset = 0x10;
    contract.EffectDrawConsumer.LightListPrimaryRecordBaseOffset = 0x14;
    contract.EffectDrawConsumer.LightListDefaultRecordBaseOffset = 0xD4;
    contract.EffectDrawConsumer.LightListRecordStrideBytes = 0x10;
    contract.EffectDrawConsumer.LightListDefaultRecordIntensityWord = 0x3F000000;
    contract.EffectDrawConsumer.LightListBuffer0ResolverAddress = 0x00313AD8;
    contract.EffectDrawConsumer.LightListBuffer1ResolverAddress = 0x00313AC8;
    contract.EffectDrawConsumer.LightListActiveIndexOffset = 0x124;
    contract.EffectDrawConsumer.LightListBuffer0TableOffset = 0x1A0;
    contract.EffectDrawConsumer.LightListBuffer1TableOffset = 0x1A8;
    contract.EffectDrawConsumer.LightListAppendDefaultAddress = 0x00313650;
    contract.EffectDrawConsumer.LightListAppendRecordAddress = 0x00313698;
    contract.EffectDrawConsumer.LightListCommandClassifyAddress = 0x003136E4;
    contract.EffectDrawConsumer.LightListFinalizeAddress = 0x00313864;
    contract.EffectDrawConsumer.DescriptorFlagsOffset = 0x1C;
    contract.EffectDrawConsumer.DescriptorLightCountOffset = 0x24;
    contract.EffectDrawConsumer.DescriptorLightRecordBaseOffset = 0x28;
    contract.EffectDrawConsumer.DescriptorLightRecordStrideBytes = 0x28;
    contract.EffectDrawConsumer.DescriptorLightTextureIdOffset = 0x24;
    contract.EffectDrawConsumer.RuntimeMatrixOffset = 0x0C;
    contract.EffectDrawConsumer.RuntimePrimaryColorBaseOffset = 0xF0;
    contract.EffectDrawConsumer.RuntimeSecondaryColorBaseOffset = 0x100;
    contract.EffectDrawConsumer.RuntimeUvTransformBaseOffset = 0x110;
    contract.EffectDrawConsumer.RuntimeUvTransformStrideBytes = 0x30;
    contract.EffectDrawConsumer.RuntimeDrawPayloadOffset = 0x174;
    contract.EffectDrawConsumer.RuntimeFlagsOffset = 0x178;
    contract.EffectDrawConsumer.TextureEnvStageCount = 2;
    contract.EffectDrawConsumer.NativeLightSlotCount = 6;
    contract.EffectDrawConsumer.MatrixUploadMode = 3;
    contract.EffectDrawConsumer.DescriptorFlag0x20ClearDrawMode = 5;
    contract.EffectDrawConsumer.DescriptorFlag0x20SetDrawMode = 4;
    contract.EffectDrawConsumer.PrimitiveDrawPacketFunctionAddress = 0x00313444;
    contract.EffectDrawConsumer.PrimitiveDrawPacketFunctionEndAddress = 0x00313587;
    contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskFunctionAddress = 0x003135AC;
    contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskFunctionEndAddress = 0x003135E3;
    contract.EffectDrawConsumer.PrimitiveDrawModeResolverFunctionAddress = 0x0047FF34;
    contract.EffectDrawConsumer.PrimitiveDrawCommandCommitAddress = 0x003084DC;
    contract.EffectDrawConsumer.PrimitiveDrawPacketWordCount = 0x24;
    contract.EffectDrawConsumer.PrimitiveDrawRenderStateIndexBaseOffset = 0x24;
    contract.EffectDrawConsumer.PrimitiveDrawEffectIndexBaseStackValue = 0;
    contract.EffectDrawConsumer.PrimitiveDrawIndexElementTypeLiteral = 0x1403;
    contract.EffectDrawConsumer.PrimitiveDrawUnsignedShortIndexBaseHighBitMask = 0x80000000;
    contract.EffectDrawConsumer.PrimitiveDrawMode5ResolvedPrimitiveValue = 1;
    contract.EffectDrawConsumer.PrimitiveDrawMode6ResolvedPrimitiveValue = 2;
    contract.EffectDrawConsumer.PrimitiveDrawMode4ResolvedPrimitiveValue = 3;
    contract.EffectDrawConsumer.PrimitiveDrawMode6010ResolvedPrimitiveValue = 3;
    contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskHeaderWord = 0x000F02B0;
    contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskPayloadOrMask = 0x7FFF0000;
    contract.EffectDrawConsumer.PrimitiveDrawPacketLiteralWords = {
        0x00020229, 0x00020253, 0x0002025E, 0x0004025E, 0x000F025F, 0x00010253, 0x000F0227,
        0x00010245, 0x000F022F, 0x000F0231, 0x000F0111, 0x7FFF0000, 0x000C02BA,
    };
    contract.EffectDrawConsumer.PrimitiveDrawCountComesFromRuntimePayload = true;
    contract.EffectDrawConsumer.PrimitiveDrawEffectStackIndexBaseIsZero = true;
    contract.EffectDrawConsumer.PrimitiveDrawPacketBridgeResolved = true;
    contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskPacketResolved = true;

    contract.GeneralLightListEmitter.FunctionAddress = 0x00409194;
    contract.GeneralLightListEmitter.FunctionEndAddress = 0x0040937C;
    contract.GeneralLightListEmitter.InitFunctionAddress = 0x004094F4;
    contract.GeneralLightListEmitter.AppendDefaultAddress = contract.EffectDrawConsumer.LightListAppendDefaultAddress;
    contract.GeneralLightListEmitter.AppendRecordAddress = contract.EffectDrawConsumer.LightListAppendRecordAddress;
    contract.GeneralLightListEmitter.DynamicFloatVectorAppendAddress = 0x0040950C;
    contract.GeneralLightListEmitter.CommandClassifyAddress =
        contract.EffectDrawConsumer.LightListCommandClassifyAddress;
    contract.GeneralLightListEmitter.CommandParamTranslateAddress = 0x004094B4;
    contract.GeneralLightListEmitter.FinalizeAddress = contract.EffectDrawConsumer.LightListFinalizeAddress;
    contract.GeneralLightListEmitter.GenericCommandWriterAddress = 0x00307BD8;
    contract.GeneralLightListEmitter.SlotCount = 8;
    contract.GeneralLightListEmitter.SourceRecordStrideBytes =
        contract.ZsiLightSettingsRecord.NativeRecordSizeBytes == 0 ? 0x1C
                                                                   : contract.ZsiLightSettingsRecord.NativeRecordSizeBytes;
    contract.GeneralLightListEmitter.SourcePointerFieldOffset = 0x24;
    contract.GeneralLightListEmitter.SourceCommandHalfwordOffset = 0x2C;
    contract.GeneralLightListEmitter.DynamicFloatVectorOffset = 0x30;
    contract.GeneralLightListEmitter.DynamicFloatVectorComponentCount = 4;
    contract.GeneralLightListEmitter.DynamicFloatVectorComponentStrideBytes = 4;
    contract.GeneralLightListEmitter.SourceOffsetTableByteOffset = 0x28;
    contract.GeneralLightListEmitter.SourceOffsetTableWordIndexBase = 10;
    contract.GeneralLightListEmitter.EnableMaskHalfwordOffset = 0x08;
    contract.GeneralLightListEmitter.DynamicMaskHalfwordSourceOffset = 0x106;
    contract.GeneralLightListEmitter.SlotClassTableLiteralAddress = 0x00409380;
    contract.GeneralLightListEmitter.SlotClassTableAddress = 0x004E2F24;
    contract.GeneralLightListEmitter.SlotClassValues = { 3, 3, 4, 2, 2, 2, 0, 0 };
    contract.GeneralLightListEmitter.PrimaryUploadRegister = 0x200;
    contract.GeneralLightListEmitter.PrimaryUploadWordCount = 0x27;
    contract.GeneralLightListEmitter.SecondaryUploadRegister = 0x2BB;
    contract.GeneralLightListEmitter.SecondaryUploadWordCount = 2;
    contract.GeneralLightListEmitter.DynamicUploadRegister = 0x232;
    contract.GeneralLightListEmitter.DynamicUploadWordCount = 4;
    contract.GeneralLightListEmitter.UploadSequentialFlag = 1;
    contract.GeneralLightListEmitter.UploadMask = 0xF;
    contract.GeneralLightListEmitter.PrimaryUploadPayloadOffset = 0x194;
    contract.GeneralLightListEmitter.SecondaryUploadPayloadOffset = 0x230;
    contract.GeneralLightListEmitter.DynamicUploadPayloadBaseOffset = 0x238;
    contract.GeneralLightListEmitter.DynamicUploadPayloadStrideBytes =
        contract.EffectDrawConsumer.LightListRecordStrideBytes;
    contract.GeneralLightListEmitter.FinalStateWord0Offset = 0x2F8;
    contract.GeneralLightListEmitter.FinalStateWord1Offset = 0x2FC;
    contract.GeneralLightListEmitter.OutputStateWord0Offset = 0x20;
    contract.GeneralLightListEmitter.OutputStateWord1Offset = 0x24;
    contract.GeneralLightListEmitter.ConsumesZsiLightSettingsRecordStride = true;
    contract.GeneralLightListEmitter.DirectlyWritesRuntimePacketPrepSource = false;

    contract.LightingRegisterEmitter.RenderContextSubmitFunctionAddress = 0x003FBBA8;
    contract.LightingRegisterEmitter.RenderContextSubmitFunctionEndAddress = 0x003FBC6B;
    contract.LightingRegisterEmitter.RenderContextSubmitMinimumDrawPayloadCount = 3;
    contract.LightingRegisterEmitter.RuntimeDrawPayloadCountOffset = contract.EffectDrawConsumer.RuntimeDrawPayloadOffset;
    contract.LightingRegisterEmitter.EffectDrawSetupFunctionAddress = contract.EffectDrawConsumer.SetupFunctionAddress;
    contract.LightingRegisterEmitter.EffectDrawBuildFunctionAddress = contract.EffectDrawConsumer.DrawFunctionAddress;
    contract.LightingRegisterEmitter.GenericDirectWriterAddress = 0x00307C94;
    contract.LightingRegisterEmitter.GenericDirectWriterIndexRegister = 0x02C0;
    contract.LightingRegisterEmitter.GenericDirectWriterDataRegister = 0x02C1;
    contract.LightingRegisterEmitter.GenericDirectWriterCommandMask = 0xF;
    contract.LightingRegisterEmitter.GenericDirectWriterEmitsVshFloatUniform = true;
    contract.LightingRegisterEmitter.GenericSequentialWriterAddress =
        contract.GeneralLightListEmitter.GenericCommandWriterAddress;
    contract.LightingRegisterEmitter.PrimarySecondaryColorEmitterAddress = 0x0031485C;
    contract.LightingRegisterEmitter.PrimarySecondaryColorRegister = 0x008;
    contract.LightingRegisterEmitter.PrimarySecondaryColorWordCount = 2;
    contract.LightingRegisterEmitter.UvTransformEmitterAddress = 0x0031432C;
    contract.LightingRegisterEmitter.UvTransformPrimaryRegister = 0x00A;
    contract.LightingRegisterEmitter.UvTransformSecondaryBaseRegister = 0x00B;
    contract.LightingRegisterEmitter.UvTransformPrimaryWordCount = 4;
    contract.LightingRegisterEmitter.UvTransformSecondaryWordCount = 3;
    contract.LightingRegisterEmitter.UvTransformSlotStrideRegisters = 3;
    contract.LightingRegisterEmitter.RuntimeSubmitFunctionAddress = 0x003F9B5C;
    contract.LightingRegisterEmitter.RuntimeSubmitDescriptorPointerWordIndex = 0;
    contract.LightingRegisterEmitter.RuntimeSubmitTextureObjectPointerWordIndex = 1;
    contract.LightingRegisterEmitter.RuntimeSubmitLightRecordTablePointerWordIndex = 2;
    contract.LightingRegisterEmitter.RuntimeSubmitColor0ByteOffset = 0xA8;
    contract.LightingRegisterEmitter.RuntimeSubmitColor1ByteOffset = 0xA4;
    contract.LightingRegisterEmitter.RuntimeSubmitColorComponentCount = 4;
    contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotCountOffset = 0x120;
    contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotIndexTableOffset = 0x124;
    contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotIndexStrideBytes = 2;
    contract.LightingRegisterEmitter.RuntimeSubmitLightRecordStrideBytes = 0x28;
    contract.LightingRegisterEmitter.RuntimeSubmitLightRecordColorOp0HalfwordOffset = 0x08;
    contract.LightingRegisterEmitter.RuntimeSubmitLightRecordColorOp1HalfwordOffset = 0x0A;
    contract.LightingRegisterEmitter.RuntimeSubmitLightRecordDisabledColorOpValue = 0x8579;
    contract.LightingRegisterEmitter.RuntimeSubmitLightSlotCount = 6;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightFunctionAddress = 0x003FA198;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightCountOffset = 0x08;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightUvSourceCountOffset = 0x0C;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightTextureRefBaseOffset = 0x10;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightSourceRecordBaseOffset = 0x58;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightRecordStrideBytes = 0x18;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputTextureIdHalfwordOffset = 0x450;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputSamplerWordBaseOffset = 0x458;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputModeWordBaseOffset = 0x468;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputWordStrideBytes = 4;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideOwnerOffset = 0x10;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideGateByteOffset = 0x1B5;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideSourcePointerOffset = 0x1A8;
    contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideModeValue = 4;
    contract.LightingRegisterEmitter.RuntimeUvTransformBuildFunctionAddress = 0x003F9F68;
    contract.LightingRegisterEmitter.RuntimeUvTransformSourceBuilderAddress = 0x003143A8;
    contract.LightingRegisterEmitter.RuntimeUvTransformCopyHelperAddress = 0x00372224;
    contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadHelperAddress = 0x00409040;
    contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadHelperAddress =
        contract.LightingRegisterEmitter.UvTransformEmitterAddress;
    contract.LightingRegisterEmitter.RuntimeUvTransformSourceCountOffset = 0x0C;
    contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordBaseOffset = 0x58;
    contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordStrideBytes = 0x18;
    contract.LightingRegisterEmitter.RuntimeUvTransformOutputSlotCount = 3;
    contract.LightingRegisterEmitter.RuntimeUvTransformOutputSlotStrideBytes = 0x30;
    contract.LightingRegisterEmitter.RuntimeUvTransformOutputWordCount = 12;
    contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadRegister =
        contract.LightingRegisterEmitter.UvTransformPrimaryRegister;
    contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadWordCount =
        contract.LightingRegisterEmitter.UvTransformPrimaryWordCount;
    contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadRegisterBase =
        contract.LightingRegisterEmitter.UvTransformSecondaryBaseRegister;
    contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadWordCount =
        contract.LightingRegisterEmitter.UvTransformSecondaryWordCount;
    contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideOwnerOffset = 0x10;
    contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideGateByteOffset = 0x1B5;
    contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideSourcePointerOffset = 0x1A8;
    contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverridePayloadOffset = 0x30;
    contract.LightingRegisterEmitter.RuntimeUvTransformUploadsNativePicaVshUniforms = true;
    contract.LightingRegisterEmitter.RuntimeUvTransformDirectlyWritesPacketPrepSource = false;
    contract.LightingRegisterEmitter.LightSlotEmitterAddress = 0x003146E4;
    contract.LightingRegisterEmitter.LightSlotDefaultEmitterAddress = 0x0031466C;
    contract.LightingRegisterEmitter.LightSlotRegisterBaseTableAddress = 0x004E2EBC;
    contract.LightingRegisterEmitter.LightSlotRegisterBases = { 0x0C0, 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8 };
    contract.LightingRegisterEmitter.LightSlotActiveMainWordCount = 3;
    contract.LightingRegisterEmitter.LightSlotActiveTailRegisterOffset = 4;
    contract.LightingRegisterEmitter.LightSlotActiveTailWordCount = 1;
    contract.LightingRegisterEmitter.LightSlotDefaultPayloadTableAddress = 0x004E2ED4;
    contract.LightingRegisterEmitter.LightSlotDefaultRegisterBaseTableAddress = 0x004E2EE8;
    contract.LightingRegisterEmitter.LightSlotDefaultWordCount = 5;
    contract.LightingRegisterEmitter.LightSlotColorEmitterAddress = 0x0031448C;
    contract.LightingRegisterEmitter.LightSlotColorRegisterTableAddress = 0x004E2F00;
    contract.LightingRegisterEmitter.LightSlotColorRegisters = { 0x0C3, 0x0CB, 0x0D3, 0x0DB, 0x0F3, 0x0FB };
    contract.LightingRegisterEmitter.LightSlotColorFloatToByteScaleWord = 0x437F0000;
    contract.LightingRegisterEmitter.LightSettingsEmitterAddress = 0x00307E34;
    contract.LightingRegisterEmitter.LightSettingsRegisterBaseTableAddress = 0x004E2F18;
    contract.LightingRegisterEmitter.LightSettingsUploadRegisters = { 0x081, 0x091, 0x099 };
    contract.LightingRegisterEmitter.LightSettingsPrimaryHeader = 0x809F0081;
    contract.LightingRegisterEmitter.LightSettingsTerminatorHeader = 0x000F008E;
    contract.LightingRegisterEmitter.ColorOperationEmitterAddress = 0x0031429C;
    contract.LightingRegisterEmitter.ColorOperationFirstHeader = 0x000F0111;
    contract.LightingRegisterEmitter.ColorOperationSecondHeader = 0x000F0110;
    contract.LightingRegisterEmitter.LightingLutInputEmitterAddress = 0x00314538;
    contract.LightingRegisterEmitter.LightingLutInputRegister = 0x059;
    contract.LightingRegisterEmitter.LightingLutInputVshUniformIndex =
        contract.LightingRegisterEmitter.LightingLutInputRegister;
    contract.LightingRegisterEmitter.LightingLutInputWordCount = 1;
    contract.LightingRegisterEmitter.LightingLutConfigEmitterAddress = 0x003142DC;
    contract.LightingRegisterEmitter.LightingLutConfigRegister = 0x05A;
    contract.LightingRegisterEmitter.LightingLutConfigVshUniformIndex =
        contract.LightingRegisterEmitter.LightingLutConfigRegister;
    contract.LightingRegisterEmitter.LightingLutConfigWordCount = 2;
    contract.LightingRegisterEmitter.LightingLutConfigDefaultTableAddress = 0x004EA0B0;
    contract.LightingRegisterEmitter.LightingLutConfigDefaultWord = 0x3F800000;
    contract.LightingRegisterEmitter.LightingEnableEmitterAddress = 0x003142F0;
    contract.LightingRegisterEmitter.LightingEnableRegister = 0x05C;
    contract.LightingRegisterEmitter.LightingEnableVshUniformIndex =
        contract.LightingRegisterEmitter.LightingEnableRegister;
    contract.LightingRegisterEmitter.LightingEnableWordCount = 1;
    contract.LightingRegisterEmitter.FragmentLightingConfigEmitterAddress = 0x00313D6C;
    contract.LightingRegisterEmitter.FragmentLightingConfigRegister = 0x112;
    contract.LightingRegisterEmitter.FragmentLightingConfigHeader = 0x803F0112;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayloadWordCount = 4;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayloadRegisters =
        { 0x112, 0x113, 0x114, 0x115 };
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderSetupCallsiteAddress =
        0x003FB8F4;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupCallsiteAddress =
        0x003FAF30;
    contract.LightingRegisterEmitter.FragmentLightingConfigStateTypeOffset = 0x0C;
    contract.LightingRegisterEmitter.FragmentLightingConfigFlagsOffset = 0x0E;
    contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryEnableOffset = 0x10;
    contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryModeOffset = 0x11;
    contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryEnableOffset = 0x12;
    contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryModeOffset = 0x13;
    contract.LightingRegisterEmitter.FragmentLightingConfigNativeDefaultType = 0x6030;
    contract.LightingRegisterEmitter.FragmentLightingConfigNativeAlternateType = 0x6051;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayload0FallbackMask = 0x0F;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayload1FallbackMask = 0x0F;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayload2EnableBit = 0x02;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayload3EnableBit = 0x02;
    contract.LightingRegisterEmitter.FragmentLightingConfigTypeSetterAddress = 0x00314034;
    contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryStateSetterAddress =
        0x003141CC;
    contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryDisabledStateSetterAddress =
        0x00314098;
    contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryEnabledStateSetterAddress =
        0x00314108;
    contract.LightingRegisterEmitter.FragmentLightingConfigAuxScalarStateSetterAddress =
        0x00314028;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialScalarUploaderAddress =
        0x004090CC;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupAddress = 0x003FAD68;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPreparedStateOffset = 0x24;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPrimarySourceRuntimeLaneOffset =
        0x1C0;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPrimaryDefaultTableAddress =
        0x004E056C;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPrimaryOverrideGateOffset =
        0x0B;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPrimaryCmbBlendGateOffset =
        0x138;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryTypeSelectorCmbOffset =
        0x04;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryTypeDisabledValue =
        0x03;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryTypeRegisterValues =
        { 0x0404, 0x0405, 0x0408 };
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryEnableCmbOffset =
        0x134;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryModeCmbOffset =
        0x135;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryParamCmbOffset =
        0x136;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryOverrideModeOffset =
        0x1BA;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialAuxByteCmbOffset = 0x05;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialAuxHalfwordCmbOffset = 0x06;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateInitializerAddress =
        0x00347258;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateCopyHelperAddress =
        0x00310F7C;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateOverrideGateOffset =
        contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPrimaryOverrideGateOffset;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateOverrideGateDefaultValue =
        0;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateSecondaryModeOffset =
        contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSecondaryOverrideModeOffset;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateSecondaryModeDefaultValue =
        1;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigRuntimeStateOverrideGateCopiedByCopyHelper =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigRuntimeStateSecondaryModeCopiedByCopyHelper =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateDefaultsResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateCopyHelperResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateOverrideWriterAddressCount =
        4;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateOverrideWriterAddresses =
        { 0x001C0310, 0x0028D620, 0x002D5F68, 0x003B4308 };
    contract.LightingRegisterEmitter
        .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddressCount =
        4;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses =
        { 0x00228A34, 0x002D5F68, 0x00377D90, 0x003B4308 };
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateWriterScanResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigRuntimeStateWritersClassifiedAsDrawLocal =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigRuntimeStateActiveProducerResolvedFromWriterScan =
        false;
    contract.LightingRegisterEmitter.FragmentLightingConfigGameplayDrawAddress =
        0x002E25F0;
    contract.LightingRegisterEmitter.FragmentLightingConfigGameplayDrawDispatcherAddress =
        0x00461904;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigGameplayDrawDispatcherCallsiteAddress =
        0x002E2CDC;
    contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntrySubmitAddress =
        0x002D5F68;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntrySubmitCallsiteCount =
        2;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntrySubmitCallsiteAddresses =
        { 0x00461BB4, 0x00461D28 };
    contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryFlagsOffset =
        0x04;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryRenderContextPointerOffset =
        0x178;
    contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryCallbackOffset =
        0x140;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryVisibilityStateOffset =
        0x120;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntrySubmittedByteOffset =
        0x121;
    contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryFadeCounterOffset =
        0x19E;
    contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryFadeLimitOffset =
        0x19F;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryOverrideGateFlagMask =
        0x80000000;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryOverrideGateForceFullFlagMask =
        0x00000020;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntrySubmitRouteResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryOverrideGateRuleResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigDrawEntryRoutePromotesActiveMaterialOverride =
        false;
    contract.LightingRegisterEmitter.FragmentLightingConfigSubmitManagerVtableAddress =
        0x004EBD78;
    contract.LightingRegisterEmitter.FragmentLightingConfigSubmitManagerMaterialConfigSlotOffset =
        0x28;
    contract.LightingRegisterEmitter.FragmentLightingConfigSubmitManagerMaterialConfigAddress =
        0x003FAC2C;
    contract.LightingRegisterEmitter.FragmentLightingConfigSubmitManagerMaterialStateSlotOffset =
        0x44;
    contract.LightingRegisterEmitter.FragmentLightingConfigSubmitManagerMaterialStateAddress =
        contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupAddress;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigSubmitManagerMaterialRouteResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigSubmitManagerMaterialRouteResolvesActiveOverrideGate =
        false;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextSubmitAddress =
        0x003FBBA8;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextPreparedStateOffset =
        0x18;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextAuxFloatOffset = 0x1E0;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextAuxZeroWord = 0;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextNativeType = 0x6030;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateInitializerAddress =
        0x00313CEC;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateBaseResetAddress =
        0x00313D58;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigPreparedStateInitializerNativeTypeLiteralAddress =
        0x00313D54;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateInitializerNativeType =
        0x6030;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateInitializerDefaultFlags =
        0x000F;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigPreparedStateInitializerDefaultPrimaryEnable =
        0;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateInitializerDefaultPrimaryMode =
        0;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigPreparedStateInitializerDefaultSecondaryEnable =
        1;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigPreparedStateInitializerDefaultSecondaryMode =
        1;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateInitializerDefaultAuxByte =
        0;
    contract.LightingRegisterEmitter.FragmentLightingConfigPayloadLogicResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateSettersResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupSourceResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextSourceResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter
        .FragmentLightingConfigPreparedStateInitializerResolvedFromCodebin =
        true;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedFlagsOriginResolved = true;
    contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateOriginResolved =
        false;
    contract.LightingRegisterEmitter.AlphaTestEmitterAddress = 0x00313EBC;
    contract.LightingRegisterEmitter.AlphaTestRegister = 0x104;
    contract.LightingRegisterEmitter.AlphaTestHeader = 0x000F0104;
    contract.LightingRegisterEmitter.EmitsRuntimePicaLightingRegisters = true;
    contract.LightingRegisterEmitter.UsesNativeZsiLightSettingsRecords = true;
    contract.LightingRegisterEmitter.FragopShadowRegisterResolvedInLightingEmitterPath = false;

    contract.PacketPrep.FunctionAddress = 0x003130A4;
    contract.PacketPrep.FunctionEndAddress = 0x00313433;
    contract.PacketPrep.RenderContextConsumerAddress = 0x003FBBA8;
    contract.PacketPrep.RenderContextConsumerCallsiteAddress = 0x003FBBD8;
    contract.PacketPrep.RenderContextPacketBufferOffset = 0x358;
    contract.PacketPrep.RenderContextTransformOffset = 0x18;
    contract.PacketPrep.DrawHandleConsumerAddress = 0x0030F4D0;
    contract.PacketPrep.DrawHandleConsumerCallsiteAddress = 0x0030F524;
    contract.PacketPrep.DrawHandlePacketBufferOffset = 0x10;
    contract.PacketPrep.EnabledUploadModeArgument = 1;
    contract.PacketPrep.EnabledUploadPathBypassesFragopShadowLiveSetter = true;
    contract.PacketPrep.RuntimeSourceVectorCommittedBlockIsDirectInput = false;
    contract.PacketPrep.SlotCount = 3;
    contract.PacketPrep.SlotStrideBytes = 0x60;
    contract.PacketPrep.SourcePayload0Offset = 0x88;
    contract.PacketPrep.SourcePayload1Offset = 0x98;
    contract.PacketPrep.SourceVectorOffsets = { 0xC8, 0xCC, 0xD0 };
    contract.PacketPrep.EnableIntensityOffset = 0xD4;
    contract.PacketPrep.PreparedVectorOffsets = { 0xD8, 0xDC, 0xE0 };
    contract.PacketPrep.PreparedIntensityOffset = 0xE4;
    contract.PacketPrep.EnabledFloatWord = 0x3F800000;
    contract.PacketPrep.DisabledFallbackZWord = 0xBF800000;
    contract.PacketPrep.EnabledUploadHelperAddress = 0x00466EA0;
    contract.PacketPrep.EnabledUploadRegisterBaseTableAddress = 0x004E2EA4;
    contract.PacketPrep.EnabledUploadRegisters = { 0x051, 0x054, 0x057 };
    contract.PacketPrep.EnabledUploadPairedRegisterOffset = 1;
    contract.PacketPrep.EnabledUploadWordCount = 1;
    contract.PacketPrep.VectorUploadHelperAddress = 0x00466F00;
    contract.PacketPrep.VectorUploadRegisterTableAddress = 0x004E2EB0;
    contract.PacketPrep.VectorUploadRegisters = { 0x050, 0x053, 0x056 };
    contract.PacketPrep.VectorUploadWordCount = 1;
    contract.PacketPrep.PrepViewMatrixAccessorAddress = 0x00313644;
    contract.PacketPrep.PrepViewMatrixPointerLiteralAddress = 0x0031364C;
    contract.PacketPrep.PrepViewMatrixRuntimeAddress = 0x005B5018;
    contract.PacketPrep.PrepStaticMatrixAccessorAddress = 0x00466F3C;
    contract.PacketPrep.PrepStaticMatrixPointerLiteralAddress = 0x00466F44;
    contract.PacketPrep.PrepStaticMatrixRuntimeAddress = 0x005B5058;
    contract.PacketPrep.UploadHelpersUseNativePicaRegisterMaps = true;

    contract.RuntimeSourceVector.StaticUpdateAddress = 0x003F95C8;
    contract.RuntimeSourceVector.DynamicSubmitCallbackAddress = 0x003F96BC;
    contract.RuntimeSourceVector.RuntimeFlagsOffset = contract.EffectDrawConsumer.RuntimeFlagsOffset;
    contract.RuntimeSourceVector.DynamicTransformUpdateSkipFlagMask = 0x01;
    contract.RuntimeSourceVector.StaticUpdateSkipFlagMask = 0x02;
    contract.RuntimeSourceVector.CommittedCopySkipFlagMask = 0x08;
    contract.RuntimeSourceVector.DescriptorPointerOffset = 0x04;
    contract.RuntimeSourceVector.DescriptorTypeOffset = 0x18;
    contract.RuntimeSourceVector.DescriptorTypeDirectMatrixValue = 0;
    contract.RuntimeSourceVector.DescriptorTypeDerivedMatrixValue = 1;
    contract.RuntimeSourceVector.BaseVectorXOffset = 0x48;
    contract.RuntimeSourceVector.BaseVectorYOffset = 0x4C;
    contract.RuntimeSourceVector.BaseVectorZOffset = 0x50;
    contract.RuntimeSourceVector.StaticTransformBlockOffset = 0x54;
    contract.RuntimeSourceVector.DynamicTransformBlockOffset = 0x84;
    contract.RuntimeSourceVector.ParentTransformBlockOffset = 0x3C;
    contract.RuntimeSourceVector.WorkingBlockOffset = 0xB4;
    contract.RuntimeSourceVector.WorkingVectorSeedOffsets = { 0xB4, 0xC8, 0xDC };
    contract.RuntimeSourceVector.WorkingPacketSourceVectorOffsets = contract.PacketPrep.SourceVectorOffsets;
    contract.RuntimeSourceVector.WorkingBlockWordCount = 12;
    contract.RuntimeSourceVector.CommittedBlockOffset = contract.EffectDrawConsumer.RuntimeMatrixOffset;
    contract.RuntimeSourceVector.CommittedBlockWordCount = 12;
    contract.RuntimeSourceVector.MatrixComposeHelperAddress = 0x0036C174;
    contract.RuntimeSourceVector.MatrixApplyHelperAddress = 0x0032C78C;
    contract.RuntimeSourceVector.VectorPrepHelperAddress = 0x00372224;
    contract.RuntimeSourceVector.PreparedIntensitySourceOffset = 0x2C;
    contract.RuntimeSourceVector.PreparedIntensityDestinationOffset = contract.PacketPrep.PreparedIntensityOffset;

    contract.RuntimeLightPacketPack.FunctionAddress = 0x003FA5D0;
    contract.RuntimeLightPacketPack.FallbackFunctionAddress = 0x003FA34C;
    contract.RuntimeLightPacketPack.PacketBufferPointerOffset = 0x10;
    contract.RuntimeLightPacketPack.RuntimeDescriptorPointerOffset = 0x00;
    contract.RuntimeLightPacketPack.DescriptorEnabledByteOffset = 0x00;
    contract.RuntimeLightPacketPack.SlotCount = contract.PacketPrep.SlotCount;
    contract.RuntimeLightPacketPack.SourceSlotStrideBytes = contract.PacketPrep.SlotStrideBytes;
    contract.RuntimeLightPacketPack.SourcePreparedVectorBaseOffset = contract.PacketPrep.PreparedVectorOffsets[0];
    contract.RuntimeLightPacketPack.SourcePreparedIntensityOffset = contract.PacketPrep.PreparedIntensityOffset;
    contract.RuntimeLightPacketPack.RequiredPreparedIntensityWord = contract.PacketPrep.EnabledFloatWord;
    contract.RuntimeLightPacketPack.SourceColorPayloadGroupCount = 4;
    contract.RuntimeLightPacketPack.SourceColorPayloadComponentCount = 3;
    contract.RuntimeLightPacketPack.SourceColorPayloadStrideBytes = 0x10;
    contract.RuntimeLightPacketPack.SourceColorPayloadOffsets = { 0x88, 0x98, 0xA8, 0xB8 };
    contract.RuntimeLightPacketPack.DescriptorPrimaryColorScaleOffsets = { 0xA8, 0xA9, 0xAA, 0xAB };
    contract.RuntimeLightPacketPack.DescriptorPayload1ScaleOffsets = { 0xA4, 0xA5, 0xA6 };
    contract.RuntimeLightPacketPack.DescriptorPayload2ScaleOffsets = { 0xAC, 0xAD, 0xAE };
    contract.RuntimeLightPacketPack.DescriptorPayload3ScaleOffsets = { 0xB0, 0xB1, 0xB2 };
    contract.RuntimeLightPacketPack.DescriptorByteToFloatScaleWord = 0x3B808081;
    contract.RuntimeLightPacketPack.ColorFloatToByteScaleWord = 0x437F0000;
    contract.RuntimeLightPacketPack.ColorRoundBiasWord = 0x3F000000;
    contract.RuntimeLightPacketPack.ClampMinWord = 0x00000000;
    contract.RuntimeLightPacketPack.ClampMaxWord = 0x3F800000;
    contract.RuntimeLightPacketPack.RuntimeOutputBaseOffset = 0x10;
    contract.RuntimeLightPacketPack.RuntimeOutputRecordStrideBytes = 0x2C;
    contract.RuntimeLightPacketPack.RuntimeOutputColorByteOffset = 0x08;
    contract.RuntimeLightPacketPack.RuntimeOutputColorByteCount = 12;
    contract.RuntimeLightPacketPack.RuntimeOutputDirectionPackedWord0Offset = 0x14;
    contract.RuntimeLightPacketPack.RuntimeOutputDirectionPackedWord1Offset = 0x18;
    contract.RuntimeLightPacketPack.RuntimeOutputEnabledByteOffset = 0x1C;
    contract.RuntimeLightPacketPack.RuntimeOutputNegatesPreparedVectorBeforePack = true;
    contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordColorByteOffsets = {
        0x04, 0x05, 0x06,
        0x07, 0x08, 0x09,
        0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F,
    };
    contract.RuntimeLightPacketPack.RuntimeOutputSourcePayloadFloatOffsets = {
        0x88, 0x8C, 0x90,
        0x98, 0x9C, 0xA0,
        0xA8, 0xAC, 0xB0,
        0xB8, 0xBC, 0xC0,
    };
    contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordDirectionPackedWordOffsets = { 0x10, 0x14 };
    contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordFlagByteOffset = 0x18;
    contract.RuntimeLightPacketPack.RuntimeOutputRecordLayoutResolved = true;
    contract.RuntimeLightPacketPack.RuntimeOutputFeedsFinalUploadEmitter = true;
    contract.RuntimeLightPacketPack.RuntimeLightEnableFlagsOffset =
        contract.EffectDrawConsumer.RuntimeDrawPayloadOffset;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateByteOffset = 0x1A1;
    contract.RuntimeLightPacketPack.DynamicColorGateContextOffset = 0x0C;
    contract.RuntimeLightPacketPack.DynamicColorGateTableOffset = 0x04;
    contract.RuntimeLightPacketPack.DynamicColorGateStrideBytes = 0x124;
    contract.RuntimeLightPacketPack.DynamicColorOverrideHelperAddress = 0x00333ABC;
    contract.RuntimeLightPacketPack.FinalUploadHelperAddress = 0x004093F8;
    contract.RuntimeLightPacketPack.FallbackFinalUploadHelperAddress = 0x00308498;
    contract.RuntimeLightPacketPack.FinalUploadSlotLoopAddress = 0x0040D15C;
    contract.RuntimeLightPacketPack.FinalUploadSlotPacketEmitterAddress = 0x0040D1A8;
    contract.RuntimeLightPacketPack.FinalUploadSlotLoopCount = 8;
    contract.RuntimeLightPacketPack.FinalUploadSlotEnableByteBaseOffset = 0x164;
    contract.RuntimeLightPacketPack.FinalUploadSourceRecordBaseOffset = 0x04;
    contract.RuntimeLightPacketPack.FinalUploadSourceRecordStrideBytes = 0x2C;
    contract.RuntimeLightPacketPack.FinalUploadPacketWordCount = 14;
    contract.RuntimeLightPacketPack.FinalUploadPacketHeaderWordIndex = 1;
    contract.RuntimeLightPacketPack.FinalUploadPacketPayloadFirstWordIndex = 0;
    contract.RuntimeLightPacketPack.FinalUploadPacketRegisterBase = 0x140;
    contract.RuntimeLightPacketPack.FinalUploadPacketRegisterStride = 0x10;
    contract.RuntimeLightPacketPack.FinalUploadPacketHeaderMask = 0x80BF0000;
    contract.RuntimeLightPacketPack.FinalUploadRecordSlotIndexByteOffset = 0x00;
    contract.RuntimeLightPacketPack.FinalUploadPackedRgbPacketWordIndices = { 0, 2, 3, 4 };
    contract.RuntimeLightPacketPack.FinalUploadPackedRgbByteOffsets = {
        0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F,
        0x04, 0x05, 0x06,
        0x07, 0x08, 0x09,
    };
    contract.RuntimeLightPacketPack.FinalUploadPackedRgbBitShifts = { 20, 10, 0 };
    contract.RuntimeLightPacketPack.FinalUploadCopiedWordSourceOffsets = {
        0x10, 0x14, 0x24, 0x28, 0x1C, 0x20,
    };
    contract.RuntimeLightPacketPack.FinalUploadCopiedWordPacketWordIndices = {
        5, 6, 7, 8, 11, 12,
    };
    contract.RuntimeLightPacketPack.FinalUploadFlagPacketWordIndex = 10;
    contract.RuntimeLightPacketPack.FinalUploadFlagBaseByteOffset = 0x18;
    contract.RuntimeLightPacketPack.FinalUploadFlagBooleanByteOffsets = { 0x01, 0x02, 0x03 };
    contract.RuntimeLightPacketPack.FinalUploadFlagBooleanBitShifts = { 1, 2, 3 };
    contract.RuntimeLightPacketPack.FinalUploadZeroPacketWordIndices = { 9, 13 };
    contract.RuntimeLightPacketPack.FinalUploadRecordPacketEmitterResolved = true;

    contract.PacketDefaultRecord.OwnerFunctionAddress = 0x00471F74;
    contract.PacketDefaultRecord.AssemblerAddress = 0x00472F8C;
    contract.PacketDefaultRecord.AssemblerEndAddress = 0x004730DC;
    contract.PacketDefaultRecord.StaticTemplateAddress = 0x004DBA1C;
    contract.PacketDefaultRecord.StaticTemplateCopySizeBytes = 0x118;
    contract.PacketDefaultRecord.TemplateBlockCopyHelperAddress = 0x00371738;
    contract.PacketDefaultRecord.RootRecordStackOffset = 0x18;
    contract.PacketDefaultRecord.RuntimeCopyHelperAddress = 0x00348B90;
    contract.PacketDefaultRecord.FirstRuntimeCopyCallsiteAddress = 0x00473038;
    contract.PacketDefaultRecord.SecondRuntimeCopyCallsiteAddress = 0x004730BC;
    contract.PacketDefaultRecord.RuntimeCopyDestinationOffset = 0x04;
    contract.PacketDefaultRecord.RuntimeCopySizeBytes = 0x120;
    contract.PacketDefaultRecord.RuntimePostCopyZeroOffset = 0x11C;
    contract.PacketDefaultRecord.RuntimeFlagsWordOffset = 0x20;
    contract.PacketDefaultRecord.RuntimeFlagsOrMask = 0xC0;
    contract.PacketDefaultRecord.RuntimeSelfPointerOffset = 0x00;
    contract.PacketDefaultRecord.RuntimeSelfPointerValueOffset = 0x04;
    contract.PacketDefaultRecord.RuntimeReleasePointerOffset = 0x19C;
    contract.PacketDefaultRecord.RuntimeReleaseHelperAddress = 0x003445D4;
    contract.PacketDefaultRecord.RuntimeBufferSizeBytes = 0x1B8;
    contract.PacketDefaultRecord.FirstAllocatorCallsiteAddress = 0x00473020;
    contract.PacketDefaultRecord.SecondAllocatorCallsiteAddress = 0x004730A8;
    contract.PacketDefaultRecord.FirstRuntimeContextBufferOffset = 0x354;
    contract.PacketDefaultRecord.SecondRuntimeContextBufferOffset = 0x358;
    contract.PacketDefaultRecord.ProviderLookupCallsiteAddress = 0x00471FE4;
    contract.PacketDefaultRecord.ProviderLookupFunctionAddress = 0x002E11D0;
    contract.PacketDefaultRecord.ProviderLookupSlot = 13;
    contract.PacketDefaultRecord.ProviderStackPointerOffset = 0x5D0;
    contract.PacketDefaultRecord.ResourceContextTableOffset = 0x1A4;
    contract.PacketDefaultRecord.ResourceContextEntryCount = 0x33;
    contract.PacketDefaultRecord.ResourceContextEntryZeroPathTableAddress = 0x004DB1B8;
    contract.PacketDefaultRecord.ResourceContextEntryZeroResolvedPathPattern =
        "rom:/menu/<language>/menu_hint_movie_parts00.ctxb";
    contract.PacketDefaultRecord.ProviderSlotIsRoomLightSource = false;
    contract.PacketDefaultRecord.ResourceContextEntryZeroIsRoomLightSource = false;
    contract.PacketDefaultRecord.BindingHelperAddress = 0x00348A64;
    contract.PacketDefaultRecord.FirstBindingCallsiteAddress = 0x00473058;
    contract.PacketDefaultRecord.SecondBindingCallsiteAddress = 0x004730DC;
    contract.PacketDefaultRecord.BindingSlotIndex = 0;
    contract.PacketDefaultRecord.BindingModeWord = 0x2600;
    contract.PacketDefaultRecord.SecondBufferTablePatchAddress = 0x0047305C;
    contract.PacketDefaultRecord.SecondBufferPatchTableStackOffset = 0x160;
    contract.PacketDefaultRecord.SecondBufferPatchEntryCount = 8;
    contract.PacketDefaultRecord.SecondBufferPatchStrideBytes = 0x10;
    contract.PacketDefaultRecord.SecondBufferPatchFloatOffsets = { 0x00, 0x08 };
    contract.PacketDefaultRecord.SecondBufferPatchAddWord = 0x3F800000;
    contract.PacketDefaultRecord.Tables = {
        {
            "vec4_default_table_stack_0x2e0",
            0x004DB7AC,
            0xC0,
            0x2E0,
            0x00,
            12,
            0x10,
        },
        {
            "vec4_default_table_stack_0x160",
            0x004DB96C,
            0x80,
            0x160,
            0x04,
            8,
            0x10,
        },
        {
            "scalar_one_default_table",
            0x004DB86C,
            0x100,
            0x1E0,
            0x08,
            64,
            0x04,
        },
        {
            "u16_index_default_table",
            0x004DB9EC,
            0x28,
            0x130,
            0x10,
            20,
            0x02,
        },
    };

    contract.PacketCopyDataflowAudit.ScanOutputPath =
        "tools/oot3d/decomp_support/analysis/descriptor_packet_copy_candidates.csv";
    contract.PacketCopyDataflowAudit.RuntimeCopyHelperAddress = contract.PacketDefaultRecord.RuntimeCopyHelperAddress;
    contract.PacketCopyDataflowAudit.DescriptorBindingHelperAddress = contract.PacketDefaultRecord.BindingHelperAddress;
    contract.PacketCopyDataflowAudit.TotalCallsiteCount = 135;
    contract.PacketCopyDataflowAudit.RuntimeCopyCallsiteCount = 33;
    contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCount = 102;
    contract.PacketCopyDataflowAudit.DescriptorBindingClassifiedNotPacketValueWriterCount = 102;
    contract.PacketCopyDataflowAudit.DescriptorBindingMentionsRuntimePacketPrepContextCount = 49;
    contract.PacketCopyDataflowAudit.DescriptorBindingMentionsDrawHandlePacketPointerCount = 3;
    contract.PacketCopyDataflowAudit.DescriptorBindingMentionsPacketPrepFunctionCount = 0;
    contract.PacketCopyDataflowAudit.DescriptorBindingMentionsRuntimeLightPacketPackFunctionCount = 0;
    contract.PacketCopyDataflowAudit.DescriptorBindingMode2600MentionCount = 62;
    contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCoverageStatus =
        "descriptor_packet_copy_candidates.csv classifies all 102 0x00348A64 callsites as "
        "ctxb_descriptor_binding_not_packet_value_writer. Forty-nine rows mention ctx+0x358 or "
        "ctx+0x354 in the surrounding default owner, three mention draw_handle+0x10 nearby, and "
        "zero mention 0x003130A4 or 0x003FA5D0. The binding helper is therefore still a CTXB/"
        "descriptor-slot bridge, not the active actor/VS packet value producer.";
    contract.PacketCopyDataflowAudit.DescriptorBindingWritesPacketPrepSourceSlots = false;
    contract.PacketCopyDataflowAudit.DefaultOwnerFunctionAddress = contract.PacketDefaultRecord.OwnerFunctionAddress;
    contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteCount = 28;
    contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteAddresses = {
        0x004722F0, 0x0047238C, 0x004723E4, 0x00472438, 0x00472490, 0x00472578, 0x004725CC,
        0x00472620, 0x00472678, 0x004726AC, 0x004726E0, 0x00472718, 0x00472748, 0x00472778,
        0x004727B8, 0x004727F8, 0x00472848, 0x00472898, 0x00472938, 0x00472ACC, 0x00472BE0,
        0x00472CE8, 0x00472E7C, 0x00472F64, 0x00473038, 0x004730BC, 0x00473158, 0x0047323C,
    };
    contract.PacketCopyDataflowAudit.DefaultPacketBufferCopyCallsiteAddresses = {
        contract.PacketDefaultRecord.FirstRuntimeCopyCallsiteAddress,
        contract.PacketDefaultRecord.SecondRuntimeCopyCallsiteAddress,
    };
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCount = 5;
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateFunctionAddresses = {
        0x002D2754,
        0x0044F7D0,
        0x00477480,
        0x004A0928,
    };
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCallsiteAddresses = {
        0x002D2868,
        0x002D2874,
        0x0044F838,
        0x00477910,
        0x004A09B0,
    };
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateClassifications = {
        "ctxb_dual_record_bounds_and_binding_builder",
        "ctxb_dual_record_bounds_and_binding_builder",
        "global_texture_descriptor_packet_initializer",
        "runtime_variant_descriptor_rebuild_and_binding",
        "double_buffer_descriptor_rebuild_and_binding",
    };
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyExcludedAsDirectLightPacketSourceCount = 5;
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyFeedsSceneZsiLightRecords = false;
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyDirectlyFeedsPacketPrep = false;
    contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyDirectlyFeedsRuntimeLightPacketPack = false;
    contract.PacketCopyDataflowAudit.DrawHandleOrPacketPrepMentionedDescriptorBindingCallsiteAddresses = {
        0x003FDC04,
        0x0041E570,
        0x004A3764,
    };
    contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedFunctionAddresses = {
        0x002F36F4,
    };
    contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedStoreAddresses = {
        0x002F3E68,
        0x002F3E6C,
        0x002F3E70,
        0x002F3E74,
    };
    contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExclusionStatus =
        "0x002F36F4 writes packet-layout-like fields at +0xC8/+0xCC/+0xE0/+0xE4 while constructing "
        "descriptor/default draw-effect records through 0x00348F34, 0x00348A64, and 0x0034897C. "
        "descriptor_packet_copy_candidates.csv classifies callsite 0x002F38D4 as "
        "ctxb_descriptor_binding_not_packet_value_writer, and runtime_effect_wrapper_backings.csv classifies "
        "callsite 0x002F3930 as null_backing_wrapper_allocates_default_autoclass1. It does not prove a writer for "
        "draw_handle+0x10 or ctx+0x358, and it does not call 0x003130A4 or 0x003FA5D0.";
    contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterWritesPacketPrepSource = false;
    contract.PacketCopyDataflowAudit.DecompileScanFoundDirectPacketPrepConsumer = false;
    contract.PacketCopyDataflowAudit.DecompileScanFoundRuntimePacketPackConsumer = false;
    contract.PacketCopyDataflowAudit.ActiveRoomPacketSourceResolved = false;

    contract.ProviderTable.ProviderLookupFunctionAddress = 0x002E11D0;
    contract.ProviderTable.RuntimeProviderTableAddress = 0x0055B490;
    contract.ProviderTable.ProviderTableEntryCount = 16;
    contract.ProviderTable.ProviderTableEntryStrideBytes = 4;
    contract.ProviderTable.ProviderPopulationAddress = 0x004811B4;
    contract.ProviderTable.ProviderPopulationEndAddress = 0x00481318;
    contract.ProviderTable.MenuCtxbProviderSlot = 13;
    contract.ProviderTable.MenuCtxbLanguagePrefixTableAddress = 0x004D3FE8;
    contract.ProviderTable.MenuCtxbSuffixTableAddress = 0x004D4010;
    contract.ProviderTable.MenuCtxbPathFormat = "%s%s";
    contract.ProviderTable.MenuCtxbResolvedPathPattern = "rom:/menu/<language>/menu_cursor00.ctxb";
    contract.ProviderTable.MenuCtxbProviderIsRoomLightSource = false;
    contract.ProviderTable.KankyoCommonOpenHelperAddress = 0x002DE6B8;
    contract.ProviderTable.KankyoCommonPath = "rom:/kankyo/kankyo_common.zar";
    contract.ProviderTable.ZarSetupFunctionAddress = 0x0031B124;
    contract.ProviderTable.ZarHeaderTypeSectionOffset = 0x0C;
    contract.ProviderTable.ZarHeaderMetadataSectionOffset = 0x10;
    contract.ProviderTable.ZarHeaderDataSectionOffset = 0x14;
    contract.ProviderTable.NativeTypeNameTableAddress = 0x0050BBA0;
    contract.ProviderTable.NativeTypeSlotNames = {
        "cmb",
        "csab",
        "anb",
        "ctxb",
        "zsi",
        "cmab",
        "qdb",
        "faceb",
        "tbd",
        "ccb",
        "unkown",
    };
    contract.ProviderTable.ProviderTypeSlotIndexBaseOffset = 0x1C;
    contract.ProviderTable.ProviderTypeSlotIndexStrideBytes = 4;
    contract.ProviderTable.CmbTypeSlot = 0;
    contract.ProviderTable.CtxbTypeSlot = 3;
    contract.ProviderTable.ZsiTypeSlot = 4;
    contract.ProviderTable.TbdTypeSlot = 8;
    contract.ProviderTable.CmbResolverAddress = 0x00358EF8;
    contract.ProviderTable.CtxbResolverAddress = 0x00372C90;
    contract.ProviderTable.ResolverSectionTablePointerOffset = 0x0C;
    contract.ProviderTable.ResolverOffsetTablePointerOffset = 0x14;
    contract.ProviderTable.CmbActiveSectionIndexOffset = 0x1C;
    contract.ProviderTable.CtxbActiveSectionIndexOffset = 0x28;
    contract.ProviderTable.TbdActiveSectionIndexOffset = 0x3C;
    contract.ProviderTable.CmbDecodedCacheOffset = 0x4C;
    contract.ProviderTable.CtxbDecodedCacheOffset = 0x54;
    contract.ProviderTable.TbdDecodedCacheOffset = 0x68;
    contract.ProviderTable.CmbDecodeHelperAddress = 0x00320458;
    contract.ProviderTable.CtxbDecodeHelperAddress = 0x003012B4;
    contract.ProviderTable.TbdResolverAddress = 0x00328DDC;
    contract.ProviderTable.TbdObjectInitializerAddress = 0x004C0F38;
    contract.ProviderTable.TbdObjectFreeHelperAddress = 0x00498CD4;
    contract.ProviderTable.ZarTeardownFunctionAddress = 0x002F70C4;
    contract.ProviderTable.TbdObjectSizeBytes = 8;
    contract.ProviderTable.TbdObjectPayloadPointerOffset = 0x00;
    contract.ProviderTable.TbdObjectRecordPointerTableOffset = 0x04;
    contract.ProviderTable.TbdPayloadRecordCountOffset = 0x0C;
    contract.ProviderTable.TbdPayloadFirstRecordOffset = 0x10;
    contract.ProviderTable.TbdRecordSizeOffset = 0x24;
    contract.ProviderTable.TbdRecordPayloadAccessorAddress = 0x003373B8;
    contract.ProviderTable.TbdRecordPayloadOffset = 0x30;
    contract.ProviderTable.KankyoTbdProviderObjectOffset = 0x268;
    contract.ProviderTable.KankyoLensflareTbdObjectOffset = 0x26C;
    contract.ProviderTable.KankyoStormTbdObjectOffset = 0x27C;
    contract.ProviderTable.PlayLensflareTbdObjectOffset = 0x0ED0;
    contract.ProviderTable.PlayStormTbdObjectOffset = 0x0EE0;
    contract.ProviderTable.LensflareTbdProviderEntryIndex = 0;
    contract.ProviderTable.StormTbdProviderEntryIndex = 1;
    contract.ProviderTable.LensflareTbdConsumerAddress = 0x002D97E4;
    contract.ProviderTable.LensflareTbdDefaultDrawWrapperAddress = 0x0045945C;
    contract.ProviderTable.LensflareTbdConditionalDrawWrapperAddress = 0x0045FE28;
    contract.ProviderTable.LensflareTbdRecordCount = 5;
    contract.ProviderTable.LensflareTbdRecordIndices = { 0, 1, 2, 3, 4 };
    contract.ProviderTable.LensflareTbdRecordNames = {
        "LensScaleMin",
        "LensScaleMax",
        "LensOffset",
        "LensDepth",
        "LensHalationColor",
    };
    contract.ProviderTable.TbdProviderParserLayoutResolved = true;
    contract.ProviderTable.TbdConsumerResolvedBeyondProviderSetup = true;
    contract.ProviderTable.LensflareTbdConsumerResolved = true;
    contract.ProviderTable.LensflareTbdVisibleBackendSubmitResolved = false;
    contract.ProviderTable.KankyoCtxbNativeIdStart = 0x44;
    contract.ProviderTable.KankyoCtxbNativeIdEnd = 0x4B;
    contract.ProviderTable.KankyoCtxbIdsAreDirectLocalArchiveIndices = false;

    contract.LensflareRuntimeList.KankyoListOffset = 0x0EC;
    contract.LensflareRuntimeList.SceneInitAddress = 0x002E47C8;
    contract.LensflareRuntimeList.SceneUpdateAddress = 0x002DE22C;
    contract.LensflareRuntimeList.SceneTeardownAddress = 0x002DECFC;
    contract.LensflareRuntimeList.StateSetterAddress = 0x002D50E8;
    contract.LensflareRuntimeList.DrawAddress = 0x00484F5C;
    contract.LensflareRuntimeList.DrawHelperAddress = 0x002C5314;
    contract.LensflareRuntimeList.PrimaryBuilderAddress = 0x002D5124;
    contract.LensflareRuntimeList.TeardownAddress = 0x0048500C;
    contract.LensflareRuntimeList.ObjectTransformAddress = 0x00371F1C;
    contract.LensflareRuntimeList.SubmitQueueAddress = 0x002C517C;
    contract.LensflareRuntimeList.SubmitRecordWriterAddress = 0x002C1AE8;
    contract.LensflareRuntimeList.BaseRuntimeFactoryAddress = 0x0034897C;
    contract.LensflareRuntimeList.QuadBatchRuntimeFactoryAddress = 0x00340D00;
    contract.LensflareRuntimeList.SharedOwnerConstructorAddress = 0x002C50D4;
    contract.LensflareRuntimeList.BaseRuntimeConstructorAddress = 0x002C4F00;
    contract.LensflareRuntimeList.QuadBatchRuntimeConstructorAddress = 0x004970E0;
    contract.LensflareRuntimeList.BaseRuntimeVtableAddress = 0x004EBD60;
    contract.LensflareRuntimeList.BaseRuntimeDrawMethodAddress = 0x003F96BC;
    contract.LensflareRuntimeList.QuadBatchRuntimeVtableAddress = 0x004EBE9C;
    contract.LensflareRuntimeList.QuadBatchRuntimeDrawMethodAddress = 0x003FC2F8;
    contract.LensflareRuntimeList.QuadBatchRuntimeDestructorAddress = 0x003FCB20;
    contract.LensflareRuntimeList.SharedOwnerVtableAddress = 0x004EBE00;
    contract.LensflareRuntimeList.SharedOwnerDrawMethodAddress = 0x003FBBA8;
    contract.LensflareRuntimeList.SharedOwnerObjectSizeBytes = 0x35C;
    contract.LensflareRuntimeList.BaseRuntimeObjectSizeBytes = 0x1E4;
    contract.LensflareRuntimeList.QuadBatchRuntimeObjectSizeBytes = 0x28C;
    contract.LensflareRuntimeList.QuadBatchPrimaryBufferResolverAddress = 0x00333270;
    contract.LensflareRuntimeList.QuadBatchOptionalBuffer0ResolverAddress = 0x003331EC;
    contract.LensflareRuntimeList.QuadBatchOptionalBuffer1ResolverAddress = 0x00333070;
    contract.LensflareRuntimeList.QuadBatchVertexCountSetterAddress = 0x00333294;
    contract.LensflareRuntimeList.QuadBatchRuntimeElementCountOffset = 0x1FC;
    contract.LensflareRuntimeList.QuadBatchRuntimeElementCapacityOffset = 0x1F8;
    contract.LensflareRuntimeList.QuadBatchRuntimeInputPositionsPointerOffset = 0x1E4;
    contract.LensflareRuntimeList.QuadBatchRuntimeInputMatricesPointerOffset = 0x1E8;
    contract.LensflareRuntimeList.QuadBatchRuntimeInputDepthsPointerOffset = 0x1EC;
    contract.LensflareRuntimeList.QuadBatchRuntimeOptionalMatrixPointerOffset = 0x1F0;
    contract.LensflareRuntimeList.QuadBatchRuntimeOptionalTexcoordPointerOffset = 0x1F4;
    contract.LensflareRuntimeList.QuadBatchDrawHandleVertexCountOffset = 0x174;
    contract.LensflareRuntimeList.QuadBatchDrawHandleCapacityOffset = 0x14;
    contract.LensflareRuntimeList.QuadBatchFallbackQuadVertexCount = 4;
    contract.LensflareRuntimeList.QuadBatchNativeVerticesPerQuad = 6;
    contract.LensflareRuntimeList.ActiveGroupWordOffset = 0x04;
    contract.LensflareRuntimeList.TargetGroupWordOffset = 0x08;
    contract.LensflareRuntimeList.BlendWeightWordOffset = 0x0C;
    contract.LensflareRuntimeList.BlendDefaultLiteralAddress = 0x002D511C;
    contract.LensflareRuntimeList.BlendScaleLiteralAddress = 0x002D5120;
    contract.LensflareRuntimeList.BlendDefault = 1.0f;
    contract.LensflareRuntimeList.BlendScale = 1.0f / 255.0f;
    contract.LensflareRuntimeList.GroupIndexShiftBits = 2;
    contract.LensflareRuntimeList.CmbHandleWordIndexBase = 0x04;
    contract.LensflareRuntimeList.CmbInstanceWordIndexBase = 0x08;
    contract.LensflareRuntimeList.CtxbDescriptorWordIndexBase = 0x0C;
    contract.LensflareRuntimeList.RenderObjectWordIndexBase = 0x24;
    contract.LensflareRuntimeList.PrimaryDrawObjectWordIndexBase = 0x28;
    contract.LensflareRuntimeList.TerminalDrawObjectWordIndexBase = 0x2C;
    contract.LensflareRuntimeList.PrimaryDrawObjectSourceRow = 1;
    contract.LensflareRuntimeList.TerminalDrawObjectSourceRow = 2;
    contract.LensflareRuntimeList.PrimaryElementSubmitIndex = 0x0B;
    contract.LensflareRuntimeList.TerminalElementSubmitIndex = 0x0C;
    contract.LensflareRuntimeList.PrimarySubmitQueueIndexBase = 0x12;
    contract.LensflareRuntimeList.TerminalSubmitQueueIndexBase = 0x14;
    contract.LensflareRuntimeList.SpecialSubmitElementStartIndex = 0x0B;
    contract.LensflareRuntimeList.SpecialSubmitElementEndIndex = 0x0C;
    contract.LensflareRuntimeList.LensflareRuntimeElementCount = 0x0D;
    contract.LensflareRuntimeList.RuntimeObjectFlagOffset = 0x178;
    contract.LensflareRuntimeList.RuntimeObjectVisibleFlagMask = 0x10;
    contract.LensflareRuntimeList.RuntimeObjectPrimaryBuilderFlagMask = 0x80000;
    contract.LensflareRuntimeList.BuilderTableAddress = 0x004D1F24;
    contract.LensflareRuntimeList.CtxbDescriptorTemplateRows = { 0, 2, 3 };
    contract.LensflareRuntimeList.RenderObjectSourceRows = { 0, 1, 2 };
    contract.LensflareRuntimeList.CtxbIndexBaseByRow = { 0, 1, 1 };
    contract.LensflareRuntimeList.InitResolved = true;
    contract.LensflareRuntimeList.DrawSlotsResolved = true;
    contract.LensflareRuntimeList.DrawHelperDispatchResolved = true;
    contract.LensflareRuntimeList.TargetGroupDispatchOnlyWhenDifferent = true;
    contract.LensflareRuntimeList.RuntimeVtableMethodsResolved = true;
    contract.LensflareRuntimeList.RuntimeBackendBufferHelpersResolved = true;
    contract.LensflareRuntimeList.BackendSubmitResolved = false;

    contract.MoonRuntime.SceneInitAddress = 0x002E47C8;
    contract.MoonRuntime.BuilderAddress = 0x002D4F10;
    contract.MoonRuntime.BlueSkyBuilderCallsiteAddress = 0x002E4988;
    contract.MoonRuntime.CtxbBaseTypeLocalIndex = 4;
    contract.MoonRuntime.LayerCount = 3;
    contract.MoonRuntime.GeometryTemplateIndices = { 1, 4, 4 };
    contract.MoonRuntime.GeometryTemplateHalfExtents = { 0.5f, 0.5f, 0.5f };
    contract.MoonRuntime.RuntimeObjectTemplateIndices = { 3, 4, 5 };
    contract.MoonRuntime.MinMagFilter = 0x2601;
    contract.MoonRuntime.WrapModes = { 0x812F, 0x8370, 0x8370 };
    contract.MoonRuntime.InitResolved = true;
    contract.MoonRuntime.TextureInputsResolved = true;
    contract.MoonRuntime.BackendSubmitResolved = false;

    contract.ShadowDepthRegisterState.SourceKind = "oot3d_codebin_pica_shadow_depth_register_state";
    contract.ShadowDepthRegisterState.PicaStateInitializerAddress = 0x003480A8;
    contract.ShadowDepthRegisterState.PicaStateInitializerCallsiteAddress = 0x004489E4;
    contract.ShadowDepthRegisterState.GenericPacketInitializerAddress = 0x00303A94;
    contract.ShadowDepthRegisterState.TextureDescriptorInitializerAddress = 0x00348F34;
    contract.ShadowDepthRegisterState.DescriptorMaterializationAddress =
        contract.DescriptorMaterialization.FunctionAddress;
    contract.ShadowDepthRegisterState.DescriptorBindingAddress =
        contract.CtxbDescriptorBinding.FunctionAddress;
    contract.ShadowDepthRegisterState.DescriptorPacketResetAddress =
        contract.PacketDefaultRecord.RuntimeReleaseHelperAddress;
    contract.ShadowDepthRegisterState.DescriptorPacketObjectSizeBytes = 0x1B8;
    contract.ShadowDepthRegisterState.DescriptorPacketRecordPointerLikeWordOffset = 0x1A8;
    contract.ShadowDepthRegisterState.DescriptorPacketRecordPointerLikeNextWordOffset = 0x1AC;
    contract.ShadowDepthRegisterState.PicaStateWordStrideBytes = 4;
    contract.ShadowDepthRegisterState.PicaRegisterIndexIsStateWordIndex = false;
    contract.ShadowDepthRegisterState.DepthMapScaleRegister = 0x04D;
    contract.ShadowDepthRegisterState.DepthMapOffsetRegister = 0x04E;
    contract.ShadowDepthRegisterState.DepthMapEnableRegister = 0x06D;
    contract.ShadowDepthRegisterState.Texunit0ShadowRegister = 0x08B;
    contract.ShadowDepthRegisterState.FragopShadowRegister = 0x130;
    contract.ShadowDepthRegisterState.DepthMapScaleStateWordIndex = 0x017;
    contract.ShadowDepthRegisterState.DepthMapOffsetStateWordIndex = 0x018;
    contract.ShadowDepthRegisterState.DepthMapEnableStateWordIndex = 0x027;
    contract.ShadowDepthRegisterState.Texunit0ShadowStateWordIndex = 0x02A;
    contract.ShadowDepthRegisterState.FragopShadowStateWordIndex = 0x05B;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterAddress = 0x00307BD8;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterScannedCallsiteCount = 17;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterFragopShadowMatchCount = 0;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableCallsiteCount = 3;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableResolvedCount = 3;
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTablePointerAddresses = {
        0x003146CC, 0x00314858,
    };
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableAddresses = {
        0x004E2EBC, 0x004E2EE8,
    };
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicRegisterBases = {
        0x0C0, 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8,
    };
    contract.ShadowDepthRegisterState.GenericPicaScalarWriterScanSource =
        "FindOot3dPicaRegisterWriters.java plus native data words at 0x003146cc/0x00314858";
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterAddress = 0x00307C94;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterScannedCallsiteCount = 14;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterFragopShadowMatchCount = 0;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterIndexRegister = 0x02C0;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDataRegister = 0x02C1;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterIndexCommandHeader = 0x000F02C0;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDataCommandHeader = 0x000F02C1;
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDynamicIndexTablePointerAddresses = {
        0x00466EFC, 0x00466F38,
    };
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDynamicIndexTableAddresses = {
        0x004E2EA4, 0x004E2EB0,
    };
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDynamicUniformIndices = {
        0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x056, 0x057, 0x058,
    };
    contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterScanSource =
        "FindOot3dPicaRegisterWriters.java plus code.bin literals at 0x00307d84/0x00307d88 and "
        "native data words at 0x00466efc/0x00466f38";
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterCommitAddress = 0x003084DC;
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterScannedCommitRefCount = 21;
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterKnownHeaderStoreCount = 41;
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterShadowDepthHeaderStoreCount = 2;
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterFragopShadowMatchCount = 0;
    contract.ShadowDepthRegisterState.DirectPicaCommandWriterScanSource =
        "FindOot3dDirectPicaCommandWriters.java scanning direct refs to command commit helper 0x003084dc";
    contract.ShadowDepthRegisterState.PicaPacketCopyHelperAddress = 0x00307AF4;
    contract.ShadowDepthRegisterState.PicaPacketCopyScannedCallsiteCount = 3;
    contract.ShadowDepthRegisterState.PicaPacketCopyResolvedStaticPacketCount = 3;
    contract.ShadowDepthRegisterState.PicaPacketCopyFragopShadowMatchCount = 0;
    contract.ShadowDepthRegisterState.PicaPacketCopyStaticPacketAddresses = {
        0x004E2E9C, 0x004E2F44, 0x004E2F54,
    };
    contract.ShadowDepthRegisterState.PicaPacketCopyStaticPacketRegisters = {
        0x08F, 0x0E0, 0x1C6,
    };
    contract.ShadowDepthRegisterState.PicaPacketCopyScanSource =
        "FindOot3dPicaPacketCopies.java plus native data words at 0x004e2e9c/0x004e2f44/0x004e2f54";
    contract.ShadowDepthRegisterState.StaticFragopShadowCommandHeaderLiteralMatchCount = 0;
    contract.ShadowDepthRegisterState.CapturedFragopShadowRegisterWriteCount = 64;
    contract.ShadowDepthRegisterState.CapturedFragopShadowSnapshotRawValue = 0x00003C00;
    contract.ShadowDepthRegisterState.CapturedFragopShadowSnapshotIsPreexistingState = false;
    contract.ShadowDepthRegisterState.PicaStateInitializerHighestVerifiedWordIndex = 0x114;
    contract.ShadowDepthRegisterState.PicaStateInitializerWritesFragopShadow = false;
    contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanRowCount = 56;
    contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanPicaWriterMatchCount = 0;
    contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanSource =
        "FindOot3dOffsetAccesses.java for byte offset 0x4c0, excluding actor/camera/view-projection hits";
    contract.ShadowDepthRegisterState.FragopShadowSourceRequiresFirstWriteTrace = false;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFunctionAddress = 0x00411334;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFunctionEndAddress = 0x00411B13;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitSlotCount = 0x0BD;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitPayloadBaseOffset = 0x100C;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitMaskByteBaseOffset = 0x15F4;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitHeaderTableAddress = 0x005A6BF4;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowHeaderTableSlotIndex = 0x05B;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowPayloadOffset = 0x1178;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowMaskByteOffset = 0x164F;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowCommandHeader = 0x000F0130;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowMask = 0x0F;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowPayloadRawValue = 0x00003C00;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFormula =
        "for slot in [0, 0xbd): if initial_mask[slot] != 0, emit value=shadow_copy[slot], "
        "header=header_table[slot] | (initial_mask[slot]<<16)";
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitTraceSource =
        "Ghidra/code.bin: FUN_00411334 emits the initial/default PICA command list from "
        "shadow_copy[slot] at +0x100c and initial mask bytes at +0x15f4; "
        "slot 0x5b emits GPUREG_FRAGOP_SHADOW value 0x00003c00/header 0x000f0130";
    contract.ShadowDepthRegisterState.FinalRegisterFlushFunctionAddress = 0x0046C204;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFunctionEndAddress = 0x0046FA3F;
    contract.ShadowDepthRegisterState.FinalRegisterFlushDirtyBitBaseOffset = 0x7A8;
    contract.ShadowDepthRegisterState.FinalRegisterFlushStateValueBaseOffset = 0x4B4;
    contract.ShadowDepthRegisterState.FinalRegisterFlushMaskByteBaseOffset = 0x3F6;
    contract.ShadowDepthRegisterState.FinalRegisterFlushShadowCopyBaseOffset = 0x100C;
    contract.ShadowDepthRegisterState.FinalRegisterFlushCommandCursorGlobalAddress = 0x0054CC4C;
    contract.ShadowDepthRegisterState.FinalRegisterFlushCommandEndGlobalAddress = 0x0054CC50;
    contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableAddress = 0x005A6BF4;
    contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableSourcePairsAddress = 0x004DED3C;
    contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableSourcePairStrideBytes = 8;
    contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableStrideBytes = 4;
    contract.ShadowDepthRegisterState.FinalRegisterFlushRegisterListSentinel = 0x0BD;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowRegisterListAddress = 0x0054A58C;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowHeaderTableSlotIndex = 0x05B;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowStateValueOffset = 0x620;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowMaskByteOffset = 0x451;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowShadowCopyOffset = 0x1178;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowDirtyWordOffset = 0x7B0;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowGroupFlagMask = 0x20;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowRegisterDirtyBitMask = 0x08000000;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowCommandHeader = 0x000F0130;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowMask = 0x0F;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowPayloadRawValue = 0x00003C00;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterAddress = 0x002C8434;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterEndAddress = 0x002CCE0F;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterEncodedRegisterFlag = 0x40000;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterSelectorShift = 2;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCase1f = 0x1F;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCase20 = 0x20;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCase1fSavedFloatOffset = 0xDC0;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCase20SavedFloatOffset = 0xDBC;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterStateValueOffset = 0x620;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterMaskByteOffset = 0x451;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterDirtyWordOffset = 0x7B0;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterDirtyBitMask = 0x08000000;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterImmediateShadowCopyOffset = 0x1178;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterVec3WrapperAddress = 0x002D44F4;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterVec4WrapperAddress = 0x002D4524;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterVec3DirectCallsiteAddress = 0x002D4518;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterVec4DirectCallsiteAddress = 0x002D4548;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterWrapperCallerAddresses = {
        0x002CE8CC, 0x002D44DC, 0x00464E00
    };
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterAlternateMaterialStateCallerAddress =
        0x0047D724;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterAlternatePacketPrepCallerAddresses = {
        0x0031335C, 0x00313418
    };
    contract.ShadowDepthRegisterState.FragopShadowMaterialSubmitWrapperAddress = 0x0047D6AC;
    contract.ShadowDepthRegisterState.FragopShadowMaterialSubmitFloatSetterBranchMode = 0;
    contract.ShadowDepthRegisterState.FragopShadowMaterialSubmitVerifiedBypassMode = 1;
    contract.ShadowDepthRegisterState.FragopShadowMaterialSubmitVerifiedCallsiteAddresses = {
        0x004528B0, 0x003FBC10
    };
    contract.ShadowDepthRegisterState.FragopShadowMaterialSubmitVerifiedCallerAddresses = {
        0x0030F4D0, 0x003FBBA8
    };
    contract.ShadowDepthRegisterState
        .FragopShadowMaterialSubmitVerifiedCallsitesBypassLiveSetter = true;
    contract.ShadowDepthRegisterState.FragopShadowScalarMaterialSetterAddress = 0x0048C528;
    contract.ShadowDepthRegisterState.FragopShadowScalarMaterialSetterExcludesPenumbraCases = true;
    contract.ShadowDepthRegisterState.FragopShadowSubmitRouteExclusionSummary =
        "FUN_0047D6AC reaches FUN_002CE8A0/FUN_002D44F4/FUN_002C8434 only when "
        "param_2==0. Verified draw/material submit callers pass param_2==1 "
        "(FUN_0030F4D0 and oot3d_render_context_light_packet_submit at 0x003fbba8; "
        "callsites 0x004528b0/0x003fbc10), so they take the bypass branch that emits through "
        "FUN_0047FE44/FUN_00307BD8 and cannot prove active penumbraScale/penumbraBias "
        "writes. FUN_0048C528 is likewise excluded for the two selector cases because "
        "its switch contains no case 0x1f or case 0x20; "
        "fragop_wrapper_residual_references.csv records 8 total references for the "
        "residual wrapper nodes and no external mode-0 submit caller.";
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperReferenceTargetCount = 6;
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperReferenceTotalCount = 8;
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperRawPointerMatchCount = 0;
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperReferenceSource =
        "Ghidra ExportOot3dReferences.java -> "
        "tools/oot3d/decomp_support/analysis/fragop_wrapper_residual_references.csv; "
        "raw byte scan -> "
        "tools/oot3d/decomp_support/analysis/fragop_wrapper_raw_pointer_scan.csv";
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperReferencesResolved = true;
    contract.ShadowDepthRegisterState.FragopShadowResidualWrapperRawPointersExcluded = true;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCallsiteSummary =
        "direct calls to FUN_002C8434 are the vec3/vec4 wrapper bodies at "
        "0x002d4518/0x002d4548; wrapper callers are 0x002ce8cc, 0x002d44dc, "
        "and 0x00464e00; the render-context and draw-handle packet-prep submit "
        "paths use param_2==1 and therefore upload through 0x00466ea0/0x00466f00 "
        "instead of the alternate wrapper route; verified FUN_0047D6AC material submit "
        "calls also pass param_2==1 and bypass the live float setter branch";
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterFormula =
        "FUN_002C8434 encoded-register path uses selector=(param_1&0x3ffff)>>2; "
        "case 0x1f stores the first shadow-Z source term at +0xdc0 and packs "
        "f16(term_at_0xdbc+term_at_0xdc0) | (f16(-term_at_0xdc0)<<16); "
        "case 0x20 stores the second shadow-Z source term at +0xdbc and repacks "
        "from the saved +0xdc0 term; "
        "both set mask byte +0x451 |= 0x0f, live state +0x620, dirty bit "
        "+0x7b0|=0x08000000, and immediate mirror +0x1178 when immediate mode is active";
    contract.ShadowDepthRegisterState.FragopShadowRecordSetBuilderAddress = 0x002DB998;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetLookupHelperAddress = 0x00307840;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetApplyHelperAddress = 0x00305950;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetAnimationBlockApplyAddress = 0x004C3AE4;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetPrimaryRecordStrideBytes = 0x28;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetSecondaryRecordStrideBytes = 0x2C;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetLookupFamilyStrideBytes = 0x400;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetLookupSecondaryBaseOffset = 0x418;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetSemanticId1f = 0x1F;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetSemanticId20 = 0x20;
    contract.ShadowDepthRegisterState.FragopShadowRecordSetExclusionSummary =
        "FUN_00307840 resolves record-set semantic ids through FUN_00305980 and applies "
        "record animation metadata via FUN_00305950/FUN_004C3AE4; observed semantic ids "
        "0x1f/0x20 in record-set callers are not the encoded-register selector cases "
        "0x1f/0x20 in FUN_002C8434 and do not prove a live GPUREG_FRAGOP_SHADOW value source";
    contract.ShadowDepthRegisterState.FragopShadowRecordSetSemanticIdsExcludedAsLiveStateSource =
        true;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleInitializerAddress = 0x002FF8E0;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleContextOffset = 0xCC;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleSourceTableOffset = 0x2C4;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleProviderEntryCount = 7;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandlePayloadExportFunctionCount = 167;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleTexture2dResourceId = 0x0DE1;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleAltTextureResourceId = 0x8513;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleMaterialResourceBaseId = 0x8517;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleMaterialResourceAcceptedRangeEndId =
        0x851A;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleLutResourceBaseId = 0x6610;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandlePayloadCommonWriterAddress =
        0x002BA45C;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleTextureContainerCreateAddress =
        0x00303960;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleCubeTextureContainerCreateAddress =
        0x003038B0;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandlePayloadRouteSummary =
        "Focused export shadow_fragop_handle_payload_ghidra_export classifies FUN_002FF8E0 "
        "payload builders as native texture/material descriptor routes: FUN_002DE990 and "
        "FUN_002D2BA8 accept 0x0de1, 0x8515/0x8516, and 0x8517..0x851a resource ids, "
        "delegate texture upload/layout conversion to FUN_002BA45C, and mark the active "
        "texture/material slot dirty; FUN_002FB074 binds 2D/cube texture containers and "
        "0x6610-family LUT payloads. This route does not call FUN_002C8434 and does not "
        "write state slot 0x5b/GPUREG_FRAGOP_SHADOW.";
    contract.ShadowDepthRegisterState.FragopShadowNativeHandlePayloadRouteExcludedAsLiveStateSource =
        true;
    contract.ShadowDepthRegisterState.FragopShadowNativeHandleNextProofTarget =
        "Continue from encoded-register handle producers that can reach FUN_002C8434 case "
        "0x1f/0x20; the verified FUN_0047D6AC submit path and FUN_0048C528 scalar route "
        "are excluded as active penumbra value sources, so the next proof target is the "
        "remaining native owner/value-source chain that can call the vec3/vec4 wrapper "
        "with encoded dmp_FragOperation.penumbraScale/penumbraBias handles";
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateCompilerAddress = 0x0041F600;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleFlag = 0x40000;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleTablePointerLiteralAddress =
        0x004205CC;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleRuntimeTableAddress =
        0x005A5E08;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleRecordStrideBytes =
        0x0C;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateDefaultEncodedHandleRecordCount =
        0x129;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleStaticTableAddress =
        0x004DDA9C;
    contract.ShadowDepthRegisterState
        .FragopShadowMaterialStateEncodedHandleStaticTableSentinelAddress = 0x004DED2C;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleStaticRecordStrideBytes =
        0x10;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleStaticRecordCount =
        0x129;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector1fStaticRecordAddress =
        0x004DDC8C;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector1fResourceType = 0x1406;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector1fNamePointerAddress =
        0x004EF255;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector1fName =
        "dmp_FragOperation.penumbraScale";
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector20StaticRecordAddress =
        0x004DDC9C;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector20ResourceType = 0x1406;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector20NamePointerAddress =
        0x004EFC1F;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateSelector20Name =
        "dmp_FragOperation.penumbraBias";
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateCompilerCandidateSummary =
        "FUN_0041F600 constructs encoded material-state handles with "
        "encoded=(index<<2)|(owner_id<<0x13)|0x40000 and adjusts low selector bits for "
        "material codes 0x8b50..0x8b55. FUN_00410C68 populates the runtime table "
        "0x005a5e08 from static code.bin records at 0x004dda9c with 0x10-byte source "
        "records and 0x0c-byte runtime records. Source records 0x1f and 0x20 are "
        "code.bin-backed DMP FragOperation penumbraScale/penumbraBias entries with "
        "resource type 0x1406, so selector coverage is native-code proven; the active "
        "scene value source for the two terms is still unresolved.";
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateEncodedHandleTableReadableFromCodebin =
        false;
    contract.ShadowDepthRegisterState
        .FragopShadowMaterialStateEncodedHandleTableSourceResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState
        .FragopShadowMaterialStateSelectorCoverageResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState.FragopShadowMaterialStateCompilerPromotedAsFragopOwner =
        false;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyTracePayloadWriteCount = 1209;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyTraceHeaderWriteCount = 1209;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseNonDefaultPayloadTraceWriteCount = 0;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseNonDefaultHeaderTraceWriteCount = 0;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyPayloadRawValue = 0x00003C00;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyCommandHeader = 0x000F0130;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyValidatedByTrace = true;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState.FragopShadowLinkHouseDefaultOnlyValidationSource =
        "code.bin default source is FUN_00410C68/FUN_00411334 slot 0x5b; "
        "validation-only trace summary "
        "fragop_shadow_0130_slot4_value_header_summary.csv reports 1209 payload writes "
        "of 0x00003c00 and 1209 headers of 0x000f0130, with zero non-default writes";
    contract.ShadowDepthRegisterState.NngxPicaRegisterStateInitializerAddress = 0x00410C68;
    contract.ShadowDepthRegisterState.NngxPicaRegisterStateDefaultValueBaseOffset = 0x1300;
    contract.ShadowDepthRegisterState.NngxPicaRegisterStateDefaultMaskByteBaseOffset = 0x16B1;
    contract.ShadowDepthRegisterState.FragopShadowDefaultStateValueOffset = 0x146C;
    contract.ShadowDepthRegisterState.FragopShadowDefaultMaskByteOffset = 0x170C;
    contract.ShadowDepthRegisterState.FragopShadowHeaderTableMappingResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState.FragopShadowDefaultStateResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitResolvedFromCodebin = true;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowHeaderTraceWriteCount = 1209;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowPayloadTraceWriteCount = 1209;
    contract.ShadowDepthRegisterState.FinalRegisterFlushTraceMemoryWriteRowCount = 2418;
    contract.ShadowDepthRegisterState.FinalRegisterFlushTraceRegisterWriteCount = 64;
    contract.ShadowDepthRegisterState.FinalRegisterFlushFormula =
        "slot = native_state_slot_for_pica_register; if dirty[slot>>5] & (1<<(slot&31)) and "
        "mask_byte[slot] != 0 and state[slot] != shadow[slot], emit value=state[slot], "
        "header=header_table[slot] | (mask_byte[slot]<<16), update shadow[slot], and clear dirty bit";
    contract.ShadowDepthRegisterState.FinalRegisterFlushTraceSource =
        "Ghidra/code.bin: FUN_00410C68 builds header_table 0x005a6bf4 from source pairs "
        "0x004ded3c and writes default state[0x5b]=0x00003c00/mask[0x5b]=0x0f; "
        "Azahar trace is diagnostic validation only";
    contract.ShadowDepthRegisterState.DepthMapFinalFlushOwnerAddress = 0x003FAD68;
    contract.ShadowDepthRegisterState.DepthMapFinalFlushVtableSlotAddress = 0x004EBDBC;
    contract.ShadowDepthRegisterState.DepthMapFinalFlushEmitterAddress = 0x00313F50;
    contract.ShadowDepthRegisterState.DepthMapFinalFlushCallsiteAddress = 0x003FAEE4;
    contract.ShadowDepthRegisterState.DepthMapScaleOffsetSequentialCommandHeader = 0x801F004D;
    contract.ShadowDepthRegisterState.DepthMapEnableCommandHeader = 0x000F006D;
    contract.ShadowDepthRegisterState.DepthMapScaleOffsetSequentialWordCount = 2;
    contract.ShadowDepthRegisterState.DepthMapEnableWordCount = 1;
    contract.ShadowDepthRegisterState.DepthMapFlushZeroLiteralAddress = 0x0031401C;
    contract.ShadowDepthRegisterState.DepthMapFlushScaleLiteralAddress = 0x003FAF54;
    contract.ShadowDepthRegisterState.DepthMapFlushRecordScaleModeByteOffset = 0x05;
    contract.ShadowDepthRegisterState.DepthMapFlushRecordScaleSourceHalfwordOffset = 0x06;
    contract.ShadowDepthRegisterState.DepthMapFlushContextScaleFactorFloatOffset = 0x14;
    contract.ShadowDepthRegisterState.DepthMapFlushContextScaleModeByteOffset = 0x18;
    contract.ShadowDepthRegisterState.DepthMapFlushContextExponentWordOffset = 0x1C;
    contract.ShadowDepthRegisterState.DepthMapFinalFlushFormula =
        "0x00313F50 emits GPUREG_DEPTHMAP_SCALE/OFFSET with header 0x801F004D "
        "and GPUREG_DEPTHMAP_ENABLE with header 0x000F006D; s0==0 encodes "
        "f32(ctx+0x04)-f32(ctx+0x08), offset from f32(ctx+0x04) or scale-mode "
        "adjusted f32(ctx+0x14), enable=1; s0!=0 encodes -s0, offset=0, enable=0";
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterAddress = 0x00409054;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterOwnerAddress = 0x003FA198;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterCallsiteAddress = 0x003FA284;
    contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableAddress = 0x004EBD78;
    contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableSlotAddress = 0x004EBDA8;
    contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableSlotOffset = 0x30;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawMethodAddress = 0x003F9B5C;
    contract.ShadowDepthRegisterState.Texunit0ShadowMatrixMethodAddress = 0x003F9F68;
    contract.ShadowDepthRegisterState.Texunit0ShadowMatrixUploadAddress = 0x00409040;
    contract.ShadowDepthRegisterState.Texunit0ShadowMatrixRecordOffset = 0x30;
    contract.ShadowDepthRegisterState.Texunit0ShadowMatrixWordCount = 16;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupEmitterAddress = 0x00408F48;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupCallsiteAddress = 0x003FA24C;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupBaseRegister = 0x081;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupWordCount = 10;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupTexParamRegister = 0x08E;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupTexPointerOffset = 0x08;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupSizeHalfword0Offset = 0x0C;
    contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupSizeHalfword1Offset = 0x0E;
    contract.ShadowDepthRegisterState.Texunit0ShadowSpecialTexcoordType = 0x6E01;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConstructorAddress = 0x00347258;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordAllocationSizeBytes = 0x234;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordCloneAddress = 0x00310F7C;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordCloneWrapperAddress = 0x004C346C;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordArrayInitializerAddress = 0x00350820;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordArrayOffset = 0x88;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordStrideBytes = 0x60;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordCount = 3;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialPointerOffset = 0x1A8;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialPointerValue = 0;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialGateByteOffset = 0x1B5;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialGateByteValue = 0;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialTexcoordByteOffset = 0x1B6;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialTexcoordByteValue = 1;
    contract.ShadowDepthRegisterState.Texunit0ShadowGlobalRuntimeRootAddress = 0x005BE5B8;
    contract.ShadowDepthRegisterState.Texunit0ShadowGlobalFactoryManagerSlotOffset = 0x17C;
    contract.ShadowDepthRegisterState.Texunit0ShadowGlobalFactoryManagerSlotAddress = 0x005BE734;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerConstructorAddress = 0x00417C80;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerVtableAddress = 0x004EC060;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerFactorySlotOffset = 0x08;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerFactoryMethodAddress = 0x003FF53C;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerDefaultContextSlotOffset = 0x04;
    contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerOverrideContextSlotOffset = 0x08;
    contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryAddress = 0x0034897C;
    contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryContextField0Offset = 0x1DC;
    contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryContextField1Offset = 0x358;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectSubobjectContextBindAddress = 0x003F9F3C;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectAggregateContextBindAddress = 0x003FEB14;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectSubobjectContextOffset = 0x10;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectAggregateContextOffset = 0x1C;
    contract.ShadowDepthRegisterState.Texunit0ShadowActorSpawnAddress = 0x003738D0;
    contract.ShadowDepthRegisterState.Texunit0ShadowActorContextPointerOffset = 0x178;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawAddress = 0x004BF618;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawRouteFlagOffset = 0x1714;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawRouteFlagMask = 0x04000000;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawCloneCallsiteAddress = 0x004BF984;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawFactoryCallsiteAddress = 0x004BF9A4;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawSubmitCallsiteAddress = 0x004BFC88;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerSourceContextPointerOffset = 0x178;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerShadowResourceCmbOffset = 0x24DC;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerInitAddress = 0x00191844;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerInitCommonAddress = 0x00250768;
    contract.ShadowDepthRegisterState.Texunit0ShadowSkelAnimeInitLinkAddress = 0x003413EC;
    contract.ShadowDepthRegisterState
        .Texunit0ShadowPlayerInitCmbResourceVisibilityLoopCallsiteAddress = 0x00191EB8;
    contract.ShadowDepthRegisterState.Texunit0ShadowResourceVisibilityClearAddress = 0x0036932C;
    contract.ShadowDepthRegisterState.Texunit0ShadowResourceVisibilitySetAddress = 0x0037266C;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceStatePointerOffset = 0x14;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceVisibilityCountOffset = 0x68;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceVisibilityBytesOffset = 0x6C;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerAuxContextPointerOffset = 0x29D4;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawObjectOffset = 0x2918;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCloneDestinationOffset = 0x291C;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordCopyReturnWrapperAddress = 0x003FC0D4;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordDirectCopyCallerCount = 2;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupSetterAddress = 0x0033B504;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelSetterAddress = 0x0032C2C0;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerEquipmentDataAddress = 0x0034913C;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupTableAddress = 0x0053A558;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupEntrySizeBytes = 5;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupEntryCount = 16;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupRenderModeByteOffset = 0;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupModel0ByteOffset = 1;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupGateByteOffset = 2;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupTexcoordByteOffset = 3;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupModel3ByteOffset = 4;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableAddress = 0x0053C698;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableEntryStrideBytes = 4;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableEntryCount = 21;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaHelperAddress = 0x004095E4;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaThresholdLiteralAddress = 0x00409634;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaScaleLiteralAddress = 0x00409638;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaClampLiteralAddress = 0x0040963C;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterWordCount = 1;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterMask = 0x0F;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterSequentialFlag = 0;
    contract.ShadowDepthRegisterState.Texunit0ShadowSpecialRouteGateByteOffset = 0x1B5;
    contract.ShadowDepthRegisterState.Texunit0ShadowSpecialRouteRecordPointerOffset = 0x1A8;
    contract.ShadowDepthRegisterState.Texunit0ShadowBiasSignedDenominatorOffset = 0x0C;
    contract.ShadowDepthRegisterState.Texunit0ShadowExponentNumeratorFloatOffset = 0x2C;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaNearFloatOffset = 0x20;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaFarFloatOffset = 0x24;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaScaleFloatOffset = 0x28;
    contract.ShadowDepthRegisterState.Texunit0ShadowProjectionFlagByteOffset = 0x71;
    contract.ShadowDepthRegisterState.Texunit0ShadowProjectionFlagXorMask = 1;
    contract.ShadowDepthRegisterState.Texunit0ShadowMantissaMask = 0x0FFFFFFE;
    contract.ShadowDepthRegisterState.Texunit0ShadowExponentShift = 24;
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketFormula =
        "GPUREG_TEXUNIT0_SHADOW = (s8(record+0x71) ^ 1) | "
        "(EncodeU24((f32(record+0x20) * f32(record+0x28)) / "
        "(f32(record+0x24) - f32(record+0x20))) & 0x0FFFFFFE) | "
        "(Exponent(f32(record+0x2C) / s16(record+0x0C)) << 24)";
    contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConsumerResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConstructorResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelRoutingResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextAllocationChainResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCmbRouteToSourceContextResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCmbResourceVisibilityRouteResolved = true;
    contract.ShadowDepthRegisterState
        .Texunit0ShadowPlayerCmbResourceVisibilityRouteExcludedAsRecordPointerProducer = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryPropagatesContextOnly = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectContextBindingResolved = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawCloneCopiesExistingRecordPointer = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawCreatesShadowObjectFromPlayerCmb = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextInitChainWritesRecordPointer = false;
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerAuxContextExcludedAsSourceContext = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowDirectGateStoreRouteExhausted = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextRouteSummary =
        "Player_Init loads player+0x24dc from native ZAR_GetCMBByIndex, Player_InitCommon "
        "passes that CMB and player+0x178 into SkelAnime_InitLink, the manager factory "
        "propagates the override AutoClass1 context to draw-object fields, and Player_Draw "
        "later clones the existing source context before creating the Shadow2D draw object "
        "from the same CMB. The clone copies AutoClass1+0x1a8/+0x1b5/+0x1b6/+0x1b7; it "
        "does not create the projection record pointer.";
    contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextNextProofTarget =
        "Find the native CMB/resource/material binding or indirect bulk-copy route that "
        "populates source AutoClass1+0x1a8 before SkelAnime/Player_Draw clone consumers.";
    contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCmbResourceVisibilityRouteSummary =
        "Player_Init iterates the resource count from player+0x24dc CMB and calls "
        "FUN_0036932C at 0x00191eb8 on draw object player+0x27c for every non-selected "
        "resource id. FUN_0036932C writes 0 to (*(draw+0x14)+0x6c)[id] when id is below "
        "(*(draw+0x14)+0x68); FUN_0037266C is the matching writer of 1. This route mutates "
        "draw-object resource visibility bytes, not AutoClass1+0x1a8.";
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixParserAddress = 0x0040F758;
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixParserCallerAddress = 0x0040C9E0;
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixParserCallsiteAddress = 0x0040CAA8;
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixSourceRecordStrideBytes = 0x28;
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixOutputStrideBytes = 0x30;
    contract.ShadowDepthRegisterState.Texunit0ShadowCmbMeshMatrixParserSummary =
        "Focused export shadow_cmb_record_parser_ghidra_export shows FUN_0040F758 is called "
        "from unk_draw_struct_04_constructor at 0x0040caa8 for each CMB mesh transform "
        "record. The caller steps source records by 0x28 and destination matrices by 0x30; "
        "FUN_0040F758 reads rotation floats at +0x10/+0x14/+0x18 and translation floats at "
        "+0x1c/+0x20/+0x24, builds a 3x4 transform matrix, and never writes "
        "AutoClass1+0x1a8 or the Shadow2D projection record fields consumed by FUN_003FA198.";
    contract.ShadowDepthRegisterState
        .Texunit0ShadowCmbMeshMatrixParserExcludedAsRecordPointerProducer = true;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordOwnerResolved = false;
    contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeValuesDecoded = false;
    contract.ShadowDepthRegisterState.ViewProjectionUpdateAddress = 0x00471BA4;
    contract.ShadowDepthRegisterState.AlternateViewProjectionUpdateAddress = 0x00463D18;
    contract.ShadowDepthRegisterState.ViewProjectionMatrixInverseAddress = 0x0034A80C;
    contract.ShadowDepthRegisterState.ViewProjectionDepthLikeSourceWord0 = 0x4D;
    contract.ShadowDepthRegisterState.ViewProjectionDepthLikeSourceWord1 = 0x4E;
    contract.ShadowDepthRegisterState.ViewProjectionDepthLikeDestinationWord0 = 0x65;
    contract.ShadowDepthRegisterState.ViewProjectionDepthLikeDestinationWord1 = 0x66;
    contract.ShadowDepthRegisterState.ViewProjectionCopyExcludedAsShadowRegisterFlush = true;
    contract.ShadowDepthRegisterState.PicaStateInitializerWritesDepthMapOffset = true;
    contract.ShadowDepthRegisterState.PicaStateInitializerWritesDepthMapEnable = true;
    contract.ShadowDepthRegisterState.PicaStateInitializerWritesTexunit0Shadow = true;
    contract.ShadowDepthRegisterState.GenericPacketInitializerZerosDepthMapScale = true;
    contract.ShadowDepthRegisterState.GenericPacketInitializerZerosDepthMapOffset = true;
    contract.ShadowDepthRegisterState.TextureDescriptorInitializerZerosDepthMapScale = true;
    contract.ShadowDepthRegisterState.TextureDescriptorInitializerZerosDepthMapOffset = true;
    contract.ShadowDepthRegisterState.GenericPacketInitializerClearsDescriptorRecordPointerWords = true;
    contract.ShadowDepthRegisterState.TextureDescriptorInitializerClearsDescriptorRecordPointerWords = true;
    contract.ShadowDepthRegisterState.DescriptorPacketResetClearsDescriptorRecordPointerWords = true;
    contract.ShadowDepthRegisterState
        .DescriptorPacketInitializersExcludedAsTexunit0ShadowRecordPointerProducers = true;
    contract.ShadowDepthRegisterState.DescriptorPacketInitializerExclusionSummary =
        "FUN_00303A94, FUN_003432D4, FUN_00348F34, and FUN_003445D4 initialize or reset "
        "0x1b8-byte descriptor/packet records and clear descriptor word indices 0x6a/0x6b "
        "(byte offsets 0x1a8/0x1ac). They are not writes into the 0x234-byte AutoClass1 "
        "render context that carries the Texunit0Shadow special-route pointer.";
    contract.ShadowDepthRegisterState.TextureShadowCompareBiasFormula =
        "GPUREG_TEXUNIT0_SHADOW.bias << 1";
    contract.ShadowDepthRegisterState.FramebufferShadowBiasFormula =
        "GPUREG_FRAGOP_SHADOW constant/linear f16";
    contract.ShadowDepthRegisterState.DepthEncodeFormula =
        "depth = z_over_w * GPUREG_DEPTHMAP_SCALE + GPUREG_DEPTHMAP_OFFSET; "
        "GPUREG_DEPTHMAP_ENABLE selects W/Z buffering";
    contract.ShadowDepthRegisterState.DepthMapScaleOffsetEnableFlushResolved = true;
    contract.ShadowDepthRegisterState.FragopShadowExcludedFromGenericPicaScalarWriter = true;
    contract.ShadowDepthRegisterState.FragopShadowExcludedFromGenericPicaVectorUniformWriter = true;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterResolved = true;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterDirectRoutesResolved = true;
    contract.ShadowDepthRegisterState.FragopShadowVerifiedSubmitPathsBypassLiveStateSetter =
        true;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterActiveCaseSourceResolved = false;
    contract.ShadowDepthRegisterState.FragopShadowLiveStateSetterCallsitesResolved = false;
    contract.ShadowDepthRegisterState.FragopShadowRuntimeNonDefaultValueSourceResolved = false;
    contract.ShadowDepthRegisterState.FragopShadowFinalFlushResolved = true;
    contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitResolved = true;
    contract.ShadowDepthRegisterState.FinalRegisterFlushResolved = true;
    contract.ShadowDepthRegisterState.RuntimeValuesDecoded = false;
    contract.ShadowDepthRegisterState.EmulatorTraceUsedAsRuntimeSource = false;

    contract.MaterialScalarEmit.DispatchAddress = 0x0047D6AC;
    contract.MaterialScalarEmit.DispatchTailBranchAddress = 0x0047D700;
    contract.MaterialScalarEmit.MaterialPacketGateByteOffset = 0x0A;
    contract.MaterialScalarEmit.MaterialPacketVectorBaseOffset = 0x6C;
    contract.MaterialScalarEmit.MaterialPacketVectorComponentCount = 3;
    contract.MaterialScalarEmit.MaterialPacketAuxWordOffset = 0x7C;
    contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset = 0x80;
    contract.MaterialScalarEmit.MaterialPacketFlagWordOffset = 0x84;
    contract.MaterialScalarEmit.MaterialPacketBackendEnabledByteOffset = 0x98;
    contract.MaterialScalarEmit.FallbackPacketEmitAddress = 0x002DD6D0;
    contract.MaterialScalarEmit.BackendPacketBuildAddress = 0x002CE8A0;
    contract.MaterialScalarEmit.BackendEnableRegisterAddress = 0x002CE8E8;
    contract.MaterialScalarEmit.BackendEnableRegisterMask = 0x0B60;
    contract.MaterialScalarEmit.ColorCommandBuildAddress = 0x0047FE44;
    contract.MaterialScalarEmit.ColorScaleWordAddress = 0x0047FECC;
    contract.MaterialScalarEmit.ColorScaleWord = 0x437F0000;
    contract.MaterialScalarEmit.ColorCommandWord0Base = 5;
    contract.MaterialScalarEmit.ColorCommandFlagShift = 16;
    contract.MaterialScalarEmit.ColorCommandWord1 = 0x000500E0;
    contract.MaterialScalarEmit.ColorCommandWord3 = 0x000F00E1;
    contract.MaterialScalarEmit.ScalarEmitWrapperAddress = 0x0047FED8;
    contract.MaterialScalarEmit.GenericScalarWriterAddress = 0x00307BD8;
    contract.MaterialScalarEmit.ScalarRegister0CallAddress = 0x0047FF0C;
    contract.MaterialScalarEmit.ScalarRegister0 = 0x0E6;
    contract.MaterialScalarEmit.ScalarRegister0Count = 1;
    contract.MaterialScalarEmit.ScalarRegister0Mask = 0x0F;
    contract.MaterialScalarEmit.ScalarRegister0SequentialFlag = 0;
    contract.MaterialScalarEmit.ScalarRegister0PayloadWord = 0;
    contract.MaterialScalarEmit.ScalarRegister0PayloadIsLiteralZero = true;
    contract.MaterialScalarEmit.ScalarRegister1CallAddress = 0x0047FF28;
    contract.MaterialScalarEmit.ScalarRegister1 = 0x0E8;
    contract.MaterialScalarEmit.ScalarRegister1Count = 0x80;
    contract.MaterialScalarEmit.ScalarRegister1Mask = 0x0F;
    contract.MaterialScalarEmit.ScalarRegister1SequentialFlag = 0;
    contract.MaterialScalarEmit.ScalarRegister1PayloadPointerOffset =
        contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset;
    contract.MaterialScalarEmit.PayloadSetterCommandDispatcherAddress = 0x0047E7BC;
    contract.MaterialScalarEmit.PayloadSetterCommandOpcode = 0x13;
    contract.MaterialScalarEmit.PayloadSetterCommandTargetPointerOffset = 0x10;
    contract.MaterialScalarEmit.PayloadSetterCommandAuxWordOffset = 0x14;
    contract.MaterialScalarEmit.PayloadSetterCommandPayloadPointerOffset = 0x18;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterAddress = 0x00405084;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterCallerAddress = 0x00403F3C;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterQueueAllocAddress = 0x0030C20C;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterQueueEnqueueAddress = 0x0030C1E8;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterRecordWordCount = 7;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterTargetBaseOffset = 0xF4;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset = 0x18;
    contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset = 0x1C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitAddress = 0x00403CC8;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitCallerAddress = 0x0030D310;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitDriverAddress = 0x0030CBE4;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectCaller0Address = 0x00402570;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectCaller1Address = 0x004025F0;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitDirectObjectCallerAddress = 0x00403A5C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitAuxReadOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitPayloadReadOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeResolverAddress = 0x00488378;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordResolverAddress = 0x003042D4;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeDecodeAddress = 0x0030429C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeSourceArgumentIndex = 2;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeOwnerPayloadOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeOwnerInnerPayloadOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordTableOffset = 0x3C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordCountOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordOffsetTableOffset = 0x08;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeEncodedHighByte = 0x01;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordTypeOffset = 0x0C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordType = 0x2203;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2RecordType = 0x2201;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3RecordType = 0x2202;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ContextListOffset = 0x28;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2ContextListOffset = 0x58;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3ContextListOffset = 0x40;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextListNodeOffset = 0xD4;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextPriorityByteOffset = 0x98;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextPriorityWordOffset = 0x50;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextSubmitArgumentOffset = 0x9C;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ValidatorAddress = 0x0048C0D4;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2ValidatorAddress = 0x0040E198;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3ValidatorAddress = 0x0048BEFC;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2PathAddress = 0x004047D8;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3PathAddress = 0x00403A94;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordPayloadResolverAddress = 0x004958EC;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordPayloadRelativeOffset = 0x10;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockWord0Offset = 0x00;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordsOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordCount = 4;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockWord5Offset = 0x14;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockByte24Offset = 0x18;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockByte25Offset = 0x19;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Word0ResolverAddress = 0x00495864;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceWordsResolverAddress = 0x00495808;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Byte24ResolverAddress = 0x00495884;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Byte25ResolverAddress = 0x004958AC;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableRelativeOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableCountOffset = 0x00;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableFirstEntryOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultReferenceWord = 0xFFFFFFFF;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultByte24 = 0x40;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultByte25 = 0x01;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13Mode = 1;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13ModeCopyCallAddress = 0x0030CF00;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitNonOpcode13Mode2CopyCallAddress = 0x0030D048;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitNonOpcode13Mode3CopyCallAddress = 0x0030D184;
    contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13ModeSubmitCallAddress =
        contract.MaterialScalarEmit.PayloadSetterMaterialSubmitCallerAddress;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextBaseInitAddress = 0x0030B174;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextBaseVtableAddress = 0x004EC6A0;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived0InitAddress = 0x004044F0;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived0VtableAddress = 0x004EC6EC;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived1InitAddress = 0x00404A90;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived1VtableAddress = 0x004EC738;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived2InitAddress = 0x00408248;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived2VtableAddress = 0x004ECA2C;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAuxFieldOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadFieldOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextInitZeroValue = 0;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachCallsiteAddress = 0x0030D4C8;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachAddress = 0x00405414;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListInsertAddress = 0x0030CAB0;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachOptionalListStackOffset = 0x2C;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListCountOffset = 0x00;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListHeadOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListNodeOffset = 0xEC;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachAuxFieldOffset =
        contract.MaterialScalarEmit.PayloadSetterMaterialContextAuxFieldOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachWritesPayloadField = 0;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyAddress = 0x0030B728;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall0Address = 0x0030CF00;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall1Address = 0x0030D048;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall2Address = 0x0030D184;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopySourceArgumentIndex = 3;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyDestinationOffset =
        contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadFieldOffset;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopySourceWordCount = 5;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolverSourceOffset = 0x08;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolverVtableSlotOffset = 0x08;
    contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolvedObjectOffset = 0x28;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBuilderAddress = 0x00402A50;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBuilderCallsiteAddress = 0x00402B70;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceTemplatePointerAddress = 0x00402B94;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceTemplateAddress = 0x004E74EC;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceObjectBaseOffset = 0x58;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourcePayloadObjectOffset = 0x5C;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourcePayloadPointerRelativeOffset = 0x04;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseConstructorAddress = 0x00402C60;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseVtableAddress = 0x004EC46C;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseVtablePointerLiteralAddress = 0x00402CC4;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectConstructorAddress = 0x003FB4D0;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectVtableAddress = 0x004EBDC8;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectVtablePointerLiteralAddress = 0x003FB518;
    contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectLightListAliasOffset =
        contract.EffectDrawConsumer.LightListPrimaryCountOffset + contract.EffectDrawConsumer.LightListOffset;
    contract.MaterialScalarEmit.PayloadSetterDescriptorInitAddress = 0x004644A8;
    contract.MaterialScalarEmit.PayloadSetterDescriptorSourceFileLiteralAddress = 0x00464714;
    contract.MaterialScalarEmit.PayloadSetterDescriptorObjectSizeLiteralAddress = 0x00464524;
    contract.MaterialScalarEmit.PayloadSetterDescriptorObjectSizeBytes = 0xC40;
    contract.MaterialScalarEmit.PayloadSetterDescriptorAuxSourceOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterDescriptorPayloadSourceOffset =
        contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset;
    contract.MaterialScalarEmit.PayloadSetterDescriptorPayloadBaseRelativeOffset = 0x00;
    contract.MaterialScalarEmit.PayloadSetterDescriptorAuxTableRelativeOffset = 0x140;
    contract.MaterialScalarEmit.RuntimePayloadBinderAddress = 0x00368704;
    contract.MaterialScalarEmit.RuntimePayloadBinderVectorSyncAddress = 0x002D4554;
    contract.MaterialScalarEmit.RuntimePayloadBinderPackedTableBuildAddress = 0x004C062C;
    contract.MaterialScalarEmit.RuntimePayloadBinderObjectPointerOffset = 0x00;
    contract.MaterialScalarEmit.RuntimePayloadBinderEnabledByteOffset = 0x44;
    contract.MaterialScalarEmit.RuntimePayloadBinderDirtyByteOffset = 0x45;
    contract.MaterialScalarEmit.RuntimePayloadBinderAuxWordSourceOffset = 0x24;
    contract.MaterialScalarEmit.RuntimePayloadBinderVectorSourceOffset = 0x04;
    contract.MaterialScalarEmit.RuntimePayloadBinderPackedTableSourceOffset = 0x468;
    contract.MaterialScalarEmit.RuntimePayloadBinderPacketAuxWordDestinationOffset =
        contract.MaterialScalarEmit.MaterialPacketAuxWordOffset;
    contract.MaterialScalarEmit.RuntimePayloadBinderPacketPayloadPointerDestinationOffset =
        contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset;
    contract.MaterialScalarEmit.RuntimePayloadBinderPacketVectorDestinationOffset =
        contract.MaterialScalarEmit.MaterialPacketVectorBaseOffset;
    contract.MaterialScalarEmit.RuntimeListBinderAddress = 0x002D960C;
    contract.MaterialScalarEmit.RuntimeListBinderSourcePlayOffset = 0x5FC8;
    contract.MaterialScalarEmit.RuntimeList0PlayOffset = 0x4C30;
    contract.MaterialScalarEmit.RuntimeList1PlayOffset = 0x500C;
    contract.MaterialScalarEmit.RuntimeListCountByteOffset = 0x06;
    contract.MaterialScalarEmit.RuntimeListEntryStrideBytes = 0x3C;
    contract.MaterialScalarEmit.RuntimeListEntryRuntimeObjectPointerOffset = 0x0C;
    contract.MaterialScalarEmit.RuntimeObjectMaterialPacketResolverAddress = 0x003687A8;
    contract.MaterialScalarEmit.RuntimeObjectPacketOwnerPointerOffset = 0x14;
    contract.MaterialScalarEmit.RuntimeObjectPacketOwnerMaterialPacketOffset = 0x10;
    contract.MaterialScalarEmit.RuntimeListBinderAppliesFogSourceToMaterialPackets = true;
    contract.MaterialScalarEmit.PayloadSetterAddress = 0x00487740;
    contract.MaterialScalarEmit.PayloadSetterAuxWordDestinationOffset =
        contract.MaterialScalarEmit.MaterialPacketAuxWordOffset;
    contract.MaterialScalarEmit.PayloadSetterPointerDestinationOffset =
        contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset;
    contract.MaterialScalarEmit.FogPayloadRuntimeUpdateCallerAddress = 0x002E2674;
    contract.MaterialScalarEmit.FogPayloadRuntimeUpdateAddress = 0x00464B2C;
    contract.MaterialScalarEmit.FogPayloadSourceFileLiteralAddress = 0x00464D60;
    contract.MaterialScalarEmit.FogPayloadDefaultSourceAddress = 0x004FA8B8;
    contract.MaterialScalarEmit.FogPayloadObjectSizeLiteralAddress = 0x00464DA4;
    contract.MaterialScalarEmit.FogPayloadObjectSizeBytes = 0x668;
    contract.MaterialScalarEmit.FogPayloadInitAddress = 0x0047FDF8;
    contract.MaterialScalarEmit.FogPayloadBuildAddress = 0x0047FD44;
    contract.MaterialScalarEmit.FogPayloadReleaseAddress = 0x0047FE2C;
    contract.MaterialScalarEmit.FogPayloadSourcePointerOffset = 0x00;
    contract.MaterialScalarEmit.FogPayloadSourceFloat0Offset = 0x68;
    contract.MaterialScalarEmit.FogPayloadSourceFloat1Offset = 0x268;
    contract.MaterialScalarEmit.FogPayloadPackedTableOffset = 0x468;
    contract.MaterialScalarEmit.FogPayloadPackedTableEntryCount = 0x80;
    contract.MaterialScalarEmit.FogPayloadPackedTableWordBytes = 4;

    contract.CmbLutAssetDecode.DecodeAddress = 0x004C382C;
    contract.CmbLutAssetDecode.DecodeCallsiteAddress = 0x00320440;
    contract.CmbLutAssetDecode.DecodeOwnerFunctionAddress = 0x0031FF64;
    contract.CmbLutAssetDecode.SourceLutSectionCountOffset = 0x08;
    contract.CmbLutAssetDecode.SourceLutRecordOffsetTableOffset = 0x10;
    contract.CmbLutAssetDecode.RuntimeObjectSourceSectionPointerOffset = 0x00;
    contract.CmbLutAssetDecode.RuntimeObjectPointerTableOffset = 0x04;
    contract.CmbLutAssetDecode.RuntimeObjectPackedTableBaseOffset = 0x08;
    contract.CmbLutAssetDecode.RuntimeObjectAllocatorContextOffset = 0x0C;
    contract.CmbLutAssetDecode.AllocatorCursorOffset = 0x08;
    contract.CmbLutAssetDecode.PointerTableEntrySizeBytes = 4;
    contract.CmbLutAssetDecode.PackedTableBytesPerLut = 0x800;
    contract.CmbLutAssetDecode.SourceSampleCount = 0x101;
    contract.CmbLutAssetDecode.PackedBaseValueOffset = 0x000;
    contract.CmbLutAssetDecode.PackedDeltaValueOffset = 0x400;
    contract.CmbLutAssetDecode.PackedValueCount = 0x100;
    contract.CmbLutAssetDecode.PackedIterationCount = 0x80;
    contract.CmbLutAssetDecode.PointerTableInitAddress = 0x002DEB7C;
    contract.CmbLutAssetDecode.SourceEvaluatorAddress = 0x003087A4;
    contract.CmbLutAssetDecode.ClampMinimumWordAddress = 0x004C39B4;
    contract.CmbLutAssetDecode.ClampMinimumWord = 0;
    contract.CmbLutAssetDecode.UploadPointerBindAddress = 0x002FB074;
    contract.CmbLutAssetDecode.UploadHelperAddress = 0x004C6964;
    contract.CmbLutAssetDecode.UploadRegisterBase = 0x6614;
    contract.CmbLutAssetDecode.UploadHelperRegisterRangeBase = 0x6610;
    contract.CmbLutAssetDecode.UploadHelperRegisterRangeCount = 0x20;
    contract.CmbLutAssetDecode.UploadCopyWordCount = 0x200;
    contract.CmbLutAssetDecode.UploadCopyByteCount = contract.CmbLutAssetDecode.PackedTableBytesPerLut;
    contract.CmbLutAssetDecode.UploadCopyDestinationOffset = 0x04;
    contract.CmbLutAssetDecode.UploadInvalidationWordOffset = 0x81C;
    contract.CmbLutAssetDecode.UploadInvalidationWordValue = 0xFFFFFFFF;
    contract.CmbLutAssetDecode.UploadDirtyFlagMask = 0x4000;
    contract.CmbLutAssetDecode.UploadHeaderWord0 = 0;
    contract.CmbLutAssetDecode.UploadHeaderWord1Address = 0x004C39B8;
    contract.CmbLutAssetDecode.UploadHeaderWord1 = 0x6605;
    contract.CmbLutAssetDecode.UploadHeaderWord2Address = 0x004C39BC;
    contract.CmbLutAssetDecode.UploadHeaderWord2 = 0x1406;
    contract.CmbLutAssetDecode.FinalShaderSemanticResolved = false;

    contract.DrawHandleSubmit.FunctionAddress = 0x0030F4D0;
    contract.DrawHandleSubmit.PacketPrepAddress = contract.PacketPrep.FunctionAddress;
    contract.DrawHandleSubmit.MatrixBeginAddress = 0x0032471C;
    contract.DrawHandleSubmit.MatrixEndAddress = 0x002F9C74;
    contract.DrawHandleSubmit.PrimitivePacketBuildAddress = 0x0045259C;
    contract.DrawHandleSubmit.MaterialAnimationBuild0Address = 0x00452934;
    contract.DrawHandleSubmit.MaterialAnimationBuild1Address = 0x00452B40;
    contract.DrawHandleSubmit.MaterialAnimationBuild2Address = 0x00452F04;
    contract.DrawHandleSubmit.MaterialStateSetupAddress = contract.MaterialScalarEmit.DispatchAddress;
    contract.DrawHandleSubmit.MaterialStateFallbackAddress = contract.MaterialScalarEmit.FallbackPacketEmitAddress;
    contract.DrawHandleSubmit.PerMeshPacketFlushAddress = 0x00466E2C;
    contract.DrawHandleSubmit.PerMeshCommandWriterInitAddress = 0x002EA028;
    contract.DrawHandleSubmit.PerMeshCommandWriterStateOffset = 0x18;
    contract.DrawHandleSubmit.PerMeshCommandScratchPointerLiteralAddress = 0x0030F6AC;
    contract.DrawHandleSubmit.PerMeshCommandScratchCapacityBytes = 0x4000;
    contract.DrawHandleSubmit.PerMeshCommandPrimitiveEmitterAddress =
        contract.DrawHandleSubmit.PrimitivePacketBuildAddress;
    contract.DrawHandleSubmit.PerMeshCommandUsedSizeAddress = 0x00314870;
    contract.DrawHandleSubmit.PerMeshCommandElementBuildAddress = 0x00454780;
    contract.DrawHandleSubmit.PerMeshCommandListOffset = 0x5C;
    contract.DrawHandleSubmit.PerMeshCommandListElementStrideBytes = 0x18;
    contract.DrawHandleSubmit.PerMeshCommandElementUsedSizeOffset = 0x10;
    contract.DrawHandleSubmit.PerMeshCommandElementAlignedCopyDestinationOffset = 0x08;
    contract.DrawHandleSubmit.PerMeshCommandElementAllocationOverheadBytes = 0x10;
    contract.DrawHandleSubmit.PerMeshCommandElementAllocationAlignmentBytes = 0x10;
    contract.DrawHandleSubmit.PerMeshCommandBuilderDirectlyWritesPacketPrepSource = false;
    contract.DrawHandleSubmit.PerMeshCommandBuilderStatus =
        "0030f4d0 builds per-mesh command bytes through 002ea028/0045259c/00314870 "
        "and copies them with 00454780 to draw_handle+0x5c elements; element+0x10 is "
        "the copied byte count, not the root packet-prep pointer consumed at draw_handle+0x10.";
    contract.DrawHandleSubmit.MaterialTransformCacheAllocAddress = 0x00454780;
    contract.DrawHandleSubmit.MaterialTransformCachePatchAddress = 0x004547DC;
    contract.DrawHandleSubmit.MaterialPacketPointerOffset = 0x10;
    contract.DrawHandleSubmit.MaterialCacheReadyByteOffset = 0x14;
    contract.DrawHandleSubmit.SubmittedPassByteOffset = 0x15;
    contract.DrawHandleSubmit.MaterialTransformArrayOffset = 0x5C;
    contract.DrawHandleSubmit.VisibilityTableOffset = 0x6C;
    contract.DrawHandleSubmit.MaterialTransformPatchSourceOffset = 0x80;
    contract.DrawHandleSubmit.MaterialDrawDispatchAddress = 0x00452854;
    contract.DrawHandleSubmit.MaterialDrawDispatchLoopEndAddress = 0x00452920;
    contract.DrawHandleSubmit.MaterialDrawDispatchResolvedFromCodebin = true;
    contract.DrawHandleSubmit.MaterialDrawDispatchPromotesActiveOverrideGate = false;
    contract.DrawHandleSubmit.CmbMeshStrideBytes = 0x0C;
    contract.DrawHandleSubmit.CmbMeshMaterialLaneByteOffset = 0x02;
    contract.DrawHandleSubmit.CmbMeshVisibilityByteOffset = 0x03;
    contract.DrawHandleSubmit.CmbMeshCountOffset = 0x08;
    contract.DrawHandleSubmit.CmbMeshPassSplitIndexOffset = 0x0C;
    contract.DrawHandleSubmit.MaterialLaneStrideBytes = 0x1CC;
    contract.DrawHandleSubmit.CmbMaterialStateGateByteOffset = 0x02;
    contract.DrawHandleSubmit.MaterialLaneAnimationByteBlock0Offset = 0xA4;
    contract.DrawHandleSubmit.MaterialLaneAnimationByteBlock1Offset = 0xA8;
    contract.DrawHandleSubmit.MaterialLanePopulateCallerAddress = 0x0031FF64;
    contract.DrawHandleSubmit.MaterialLanePopulateAddress = 0x004C34AC;
    contract.DrawHandleSubmit.MaterialLaneResetHelperAddress = 0x004C6264;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPopulateAddress = 0x004C6364;
    contract.DrawHandleSubmit.MaterialLaneBlendSourceTranslateAddress = 0x00307A48;
    contract.DrawHandleSubmit.MaterialLaneBlendOperandTranslateAddress = 0x003079D0;
    contract.DrawHandleSubmit.MaterialLaneBlendEquationTranslateAddress = 0x00307964;
    contract.DrawHandleSubmit.MaterialLaneBlendSourceTablePointerAddress = 0x00307ABC;
    contract.DrawHandleSubmit.MaterialLaneBlendSourceTableAddress = 0x004EA320;
    contract.DrawHandleSubmit.MaterialLaneBlendOperandTablePointerAddress = 0x00307A44;
    contract.DrawHandleSubmit.MaterialLaneBlendOperandTableAddress = 0x004EA340;
    contract.DrawHandleSubmit.MaterialLaneBlendEquationTablePointerAddress = 0x003079CC;
    contract.DrawHandleSubmit.MaterialLaneBlendEquationTableAddress = 0x004EA360;
    contract.DrawHandleSubmit.MaterialLaneBlendFloatScaleWordAddress = 0x004C3664;
    contract.DrawHandleSubmit.MaterialLaneBlendFloatScaleWord = 0x437F0000;
    contract.DrawHandleSubmit.CmbMaterialCountOffset = 0x08;
    contract.DrawHandleSubmit.CmbMaterialTableOffset = 0x0C;
    contract.DrawHandleSubmit.CmbMaterialRecordSizeBytes = kOot3dCmbMaterialSize;
    contract.DrawHandleSubmit.MaterialLaneSourceMaterialPointerOffset = 0x00;
    contract.DrawHandleSubmit.MaterialLaneRuntimeContextPointerOffset = 0x04;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerOffset = 0x08;
    contract.DrawHandleSubmit.MaterialTableMeshResourceHandleTablePointerOffset = 0x08;
    contract.DrawHandleSubmit.MaterialTableLaneBasePointerOffset = 0x0C;
    contract.DrawHandleSubmit.MaterialTableArenaCursorPointerOffset = 0x10;
    contract.DrawHandleSubmit.MaterialLaneMeshResourceHandleTablePointerOffset =
        contract.DrawHandleSubmit.MaterialLaneRuntimeContextPointerOffset;
    contract.DrawHandleSubmit.MaterialLanePostMaterialRecordTablePointerOffset =
        contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerOffset;
    contract.DrawHandleSubmit.MaterialLaneHeaderPointersResolvedFromCodeBin = true;
    contract.DrawHandleSubmit.MaterialLaneHeaderPointersAreDirectCmbMaterialData = false;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTableDerivedFromCmbMaterialRecordTail = true;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerSharedByAllLanes = true;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTableResolvedAsTextureEnvTable = true;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTableBaseFormulaMaterialCountOffset =
        contract.DrawHandleSubmit.CmbMaterialCountOffset;
    contract.DrawHandleSubmit.MaterialLanePostMaterialTableBaseFormulaMaterialRecordSizeBytes =
        contract.DrawHandleSubmit.CmbMaterialRecordSizeBytes;
    contract.DrawHandleSubmit.CmbMaterialTextureEnvRecordSizeBytes = kOot3dCmbMaterialTextureEnvSize;
    contract.DrawHandleSubmit.CmbMaterialTextureEnvStageCountOffset =
        kOot3dCmbMaterialRawTextureStageCountOffset;
    contract.DrawHandleSubmit.CmbMaterialTextureEnvStageIndexOffset =
        kOot3dCmbMaterialRawTextureStageIndexOffset;
    contract.DrawHandleSubmit.MaterialLaneHeaderPointerStatus =
        "0x0031FF64 stores the per-mesh resource-handle table at material-table +0x08, "
        "0x004C34AC stores the material-lane base at material-table +0x0C, and each lane "
        "receives source material +0x00, mesh handle table +0x04, and the shared post-material "
        "CMB TextureEnv table pointer +0x08. 0x004C34AC computes that pointer as "
        "mats+0x0C+material_count*0x15C; CMB material +0x120/+0x124 select 0x28-byte "
        "TextureEnv records from that table.";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockDestinationOffset = 0x0C;
    contract.DrawHandleSubmit.CmbMaterialCopiedBlockSourceOffset = 0xCC;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockSourcePointerOffset = 0x0C;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockMinimumSourceBytes = 0x2C;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0TranslateAddress = 0x004C7CE8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorTranslateAddress = 0x004C7D60;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1TranslateAddress = 0x004C7EB8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2TranslateAddress = 0x004C7DDC;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3TranslateAddress = 0x004C7E18;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4TranslateAddress = 0x004C7F08;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputTranslateAddress = 0x004C7CE8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleTranslateAddress = 0x004C7D60;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitTranslateAddress = 0x004C7EB8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeTranslateAddress = 0x004C7DDC;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigTranslateAddress = 0x004C7E18;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalSourcePointerOffset = 0x00;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalClearedByteOffset = 0x04;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalClearedByteCount = 3;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag0SourceLocalOffset = 0x24;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0SourceLocalOffset = 0x26;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorSourceLocalOffset = 0x28;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1SourceLocalOffset = 0x10;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2SourceLocalOffset = 0x12;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag1SourceLocalOffset = 0x14;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3SourceLocalOffset = 0x18;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4SourceLocalOffset = 0x1C;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag2SourceLocalOffset = 0x1E;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3SourceLocalOffset = 0x1F;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag4SourceLocalOffset = 0x20;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5SourceLocalOffset = 0x23;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputSourceLocalOffset = 0x26;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleSourceLocalOffset = 0x28;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitSourceLocalOffset = 0x10;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeSourceLocalOffset = 0x12;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigSourceLocalOffset = 0x18;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag0ByteOffset = 0x1A5;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0ByteOffset = 0x1A4;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorByteOffset = 0x1A6;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1ByteOffset = 0x198;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2ByteOffset = 0x197;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag1ByteOffset = 0x19D;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3ByteOffset = 0x194;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4ByteOffset = 0x195;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag2ByteOffset = 0x19E;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3ByteOffset = 0x19F;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag4ByteOffset = 0x1A0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5ByteOffset = 0x1A1;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateByteOffset =
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5ByteOffset;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateSourceLocalOffset =
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5SourceLocalOffset;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateSourceMaterialOffset =
        contract.DrawHandleSubmit.CmbMaterialCopiedBlockSourceOffset +
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5SourceLocalOffset;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateConsumerAddress =
        contract.RuntimeLightPacketPack.FunctionAddress;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateFinalUploadAddress =
        contract.RuntimeLightPacketPack.FinalUploadHelperAddress;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateSemanticResolved = true;
    contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateSemantic =
        "gates payload3 material scale multiply before native runtime light packet upload";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputByteOffset = 0x1A4;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleByteOffset = 0x1A6;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitByteOffset = 0x198;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeByteOffset = 0x197;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigByteOffset = 0x194;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputConstantBase = 0x62A0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputConstantCount = 6;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaTextureUnitConstantBase = 0x84C0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaTextureUnitConstantCount = 4;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeConstantBase = 0x62C8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeConstantCount = 3;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigConstantBase = 0x62B0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigLinearCount = 7;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfig7Constant = 0x62B7;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfig7EncodedValue = 8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0TranslateAddress =
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4TranslateAddress;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0SourceLocalOffset =
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4SourceLocalOffset;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset =
        contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4ByteOffset;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0Register = 0x1D0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift = 1;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantBase = 0x62C0;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantCount = 4;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector = {
        1, 0, 0, 0
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0RegisterName =
        "GPUREG_LIGHTING_LUTINPUT_ABS";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0FieldName = "disable_d0";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0SamplerName = "d0";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0PackingRule =
        "selector <= 1 ? 1 - selector : 0";
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketSubmitWrapperAddress =
        0x00308498;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress = 0x0040D040;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketFollowupEmitterAddress =
        0x0040CDD8;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketWordCount = 6;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleansInvertClamp01 = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketHeaders = {
        0x000F01D0, 0x000F01D1, 0x000F01D2
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets = {
        0x195, 0x199, 0x19D, 0x1A1, 0x1A5, 0x1A9, 0x1AD
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts = {
        1, 5, 9, 13, 17, 21, 25
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPrimaryNibbleByteOffsets = {
        0x194, 0x198, 0x19C, 0x1A0, 0x1A4, 0x1A8, 0x1AC
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSecondaryNibbleByteOffsets = {
        0x196, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts = {
        0, 4, 8, 12, 16, 20, 24
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisters = {
        0x1D0, 0x1D1, 0x1D2
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisterNames = {
        "GPUREG_LIGHTING_LUTINPUT_ABS",
        "GPUREG_LIGHTING_LUTINPUT_SELECT",
        "GPUREG_LIGHTING_LUTINPUT_SCALE",
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSamplerOrder = {
        "d0", "d1", "sp", "fr", "rb", "rg", "rr"
    };
    contract.DrawHandleSubmit.MaterialLaneRawFieldConsumerOffsetScanOutputPath =
        "analysis/material_lighting_lane_offset_accesses.csv";
    contract.DrawHandleSubmit.MaterialLaneRawFieldConsumerExportPath =
        "analysis/material_lighting_raw_field_consumer_ghidra_export";
    contract.DrawHandleSubmit.MaterialLaneRawFieldOffsetAccessScanRowCount = 3103;
    contract.DrawHandleSubmit.MaterialLaneRawFieldByteConsumerCandidateCount = 37;
    contract.DrawHandleSubmit.MaterialLaneRawFieldTrueConsumerFunctionAddresses = {
        0x0040D040
    };
    contract.DrawHandleSubmit.MaterialLaneRawFieldProducerFunctionAddresses = {
        0x004C6264, 0x004C6364
    };
    contract.DrawHandleSubmit.MaterialLaneRawFieldFalsePositiveFunctionAddresses = {
        0x001D9004, 0x002A1A18, 0x002D5F68, 0x002D644C, 0x0034E6D0, 0x00368944, 0x00461904, 0x004A31E0
    };
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegistersResolvedFromCodeBin = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigEmitterConsumesLaneByteOffsets = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigOffsetArraysAreLaneRelative = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputPackingResolved = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved = true;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3ConsumedByPicaConfigEmitter = false;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3FinalSemanticResolved = false;
    contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3Status =
        "0x004C6364 copies source material+0xCC+0x1F to lane+0x19F, but 0x0040D040 "
        "does not read lane+0x19F while emitting GPUREG_LIGHTING_LUTINPUT_ABS/SELECT/SCALE; "
        "observed +0x19F readers 0x002D5F68 and 0x00461904 are actor/effect-state false "
        "positives for this material lane. The field remains exposed as raw copied state, but is "
        "not a material-lighting backend blocker until a native material/PICA consumer is proven.";
    contract.DrawHandleSubmit.CmbMaterialBlendModeByteOffset = 0x138;
    contract.DrawHandleSubmit.CmbMaterialBlendModeEnabledValue = 1;
    contract.DrawHandleSubmit.MaterialLaneBlendEnabledByteOffset = 0x1C0;
    contract.DrawHandleSubmit.CmbMaterialBlend0SourceOffset = 0x13C;
    contract.DrawHandleSubmit.CmbMaterialBlend0OperandOffset = 0x13E;
    contract.DrawHandleSubmit.CmbMaterialBlend0EquationOffset = 0x140;
    contract.DrawHandleSubmit.CmbMaterialBlend1SourceOffset = 0x144;
    contract.DrawHandleSubmit.CmbMaterialBlend1OperandOffset = 0x146;
    contract.DrawHandleSubmit.CmbMaterialBlend1EquationOffset = 0x148;
    contract.DrawHandleSubmit.CmbMaterialBlendColorFloatBaseOffset = 0x14C;
    contract.DrawHandleSubmit.CmbMaterialBlendColorFloatCount = 4;
    contract.DrawHandleSubmit.MaterialLaneBlend0SourceByteOffset = 0x1C2;
    contract.DrawHandleSubmit.MaterialLaneBlend0OperandByteOffset = 0x1C3;
    contract.DrawHandleSubmit.MaterialLaneBlend0EquationByteOffset = 0x1C6;
    contract.DrawHandleSubmit.MaterialLaneBlend1SourceByteOffset = 0x1C4;
    contract.DrawHandleSubmit.MaterialLaneBlend1OperandByteOffset = 0x1C5;
    contract.DrawHandleSubmit.MaterialLaneBlend1EquationByteOffset = 0x1C7;
    contract.DrawHandleSubmit.MaterialLaneBlendColorByteBaseOffset = 0x1C8;
    contract.DrawHandleSubmit.MaterialLaneBlendColorByteCount = 4;
    contract.DrawHandleSubmit.VtableLaneSelectSlotOffset = 0x10;
    contract.DrawHandleSubmit.VtableMeshPrepareSlotOffset = 0x08;
    contract.DrawHandleSubmit.VtablePrimitivePacketSlotOffset = 0x0C;
    contract.DrawHandleSubmit.VtableMeshDrawSlotOffset = 0x14;
    contract.DrawHandleSubmit.VtableMaterialDrawSlotOffset = 0x20;
    contract.DrawHandleSubmit.VtableMaterialPreDrawSlotOffset = 0x24;
    contract.DrawHandleSubmit.ExcludedDrawListAllocatorAddress = 0x00313CE0;
    contract.DrawHandleSubmit.ExcludedDrawListBuilderAddress = 0x002FC694;
    contract.DrawHandleSubmit.ExcludedDrawListOwnerCandidateAddress = 0x0044BD54;
    contract.DrawHandleSubmit.ExcludedDrawListCountOffset = 0;
    contract.DrawHandleSubmit.ExcludedDrawListViewportWidthFloatOffset = 0x04;
    contract.DrawHandleSubmit.ExcludedDrawListViewportHeightFloatOffset = 0x08;
    contract.DrawHandleSubmit.ExcludedDrawListBufferOffsets = { 0x0C, 0x10, 0x14, 0x18, 0x1C };
    contract.DrawHandleSubmit.ExcludedDrawListBufferStrideBytes = { 0x30, 0x30, 0x20, 0x40, 0x08 };
    contract.DrawHandleSubmit.ExcludedDrawListStoresCountWhereSubmittedHandleRequiresVtable = true;
    contract.DrawHandleSubmit.ExcludedDrawListDirectlyWritesPacketPrepSource = false;
    contract.DrawHandleSubmit.ExcludedDrawListCandidateIsSubmittedDrawHandle = false;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAddress = 0x004A3658;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorSingletonPointerAddress = 0x004A3788;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorSingletonAddress = 0x0055A1F8;
    contract.DrawHandleSubmit.LazyDescriptorOwnerDescriptorTablePointerAddress = 0x004A378C;
    contract.DrawHandleSubmit.LazyDescriptorOwnerDescriptorTableAddress = 0x004FA640;
    contract.DrawHandleSubmit.LazyDescriptorOwnerSlotTablePointerAddress = 0x004A3798;
    contract.DrawHandleSubmit.LazyDescriptorOwnerSlotTableAddress = 0x004FA77C;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorTagPointerAddress = 0x004A3790;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorTagAddress = 0x004CF4CC;
    contract.DrawHandleSubmit.LazyDescriptorOwnerGenericAllocationTypePointerAddress = 0x004A3794;
    contract.DrawHandleSubmit.LazyDescriptorOwnerGenericAllocationTypeId = 0x0279;
    contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialAllocationTypeId = 0x027C;
    contract.DrawHandleSubmit.LazyDescriptorOwnerAllocationSizeBytes = 0x1B8;
    contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialInitializerAddress = 0x003432D4;
    contract.DrawHandleSubmit.LazyDescriptorOwnerGenericInitializerAddress = 0x00348F34;
    contract.DrawHandleSubmit.LazyDescriptorOwnerMaterializerAddress = 0x00348BE4;
    contract.DrawHandleSubmit.LazyDescriptorOwnerBindingAddress = 0x00348A64;
    contract.DrawHandleSubmit.LazyDescriptorOwnerProviderResolverAddress = 0x0033AAAC;
    contract.DrawHandleSubmit.LazyDescriptorOwnerProviderContextWordOffset = 0x140;
    contract.DrawHandleSubmit.LazyDescriptorOwnerPointerStoreIndexBias = 1;
    contract.DrawHandleSubmit.LazyDescriptorOwnerBindingSlotCount = 2;
    contract.DrawHandleSubmit.LazyDescriptorOwnerBindingRecordStrideBytes = 0x10;
    contract.DrawHandleSubmit.LazyDescriptorOwnerBindingSentinelByteValue = 0xFF;
    contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialDescriptorIds = { 0x0F, 0x2E, 0x2F };
    contract.DrawHandleSubmit.LazyDescriptorOwnerResolved = true;
    contract.DrawHandleSubmit.LazyDescriptorOwnerDirectlyWritesPacketPrepSource = false;
    contract.DrawHandleSubmit.LazyDescriptorOwnerStatus =
        "resolved_descriptor_object_owner_and_ctxb_binding_path_not_packet_value_writer";

    contract.GameplayDrawSequence.FunctionAddress = 0x002E25F0;
    contract.GameplayDrawSequence.TailBranchAddress = 0x002E2E50;
    contract.GameplayDrawSequence.TailBranchTargetAddress = 0x0046FBAC;
    contract.GameplayDrawSequence.Callsites = {
        {
            "environment_vector_prep",
            0x002E2BC4,
            0x004594E0,
            true,
            0x31A6,
            0,
            false,
            0,
        },
        {
            "environment_color_submit",
            0x002E2BCC,
            0x0046423C,
            false,
            0,
            0,
            false,
            0,
        },
        {
            "environment_flash_submit",
            0x002E2BD8,
            0x0045C25C,
            false,
            0,
            0,
            false,
            0,
        },
        {
            "z_kankyo_thunder_runtime_submit_update",
            0x002E2BE4,
            0x0045FEB0,
            false,
            0,
            0,
            true,
            0,
        },
        {
            "z_kankyo_weather_particle_submit_update",
            0x002E2CCC,
            0x00463544,
            false,
            0,
            0,
            false,
            0,
        },
        {
            "z_kankyo_storm_runtime_schedule",
            0x002E2DB0,
            0x004599BC,
            true,
            0x3266,
            0,
            true,
            0,
            true,
            true,
            0x3266,
        },
    };
    contract.RenderRecordScheduler.FunctionAddress = 0x00328350;
    contract.RenderRecordScheduler.FunctionEndAddress = 0x0032839F;
    contract.RenderRecordScheduler.CountBaseOffset = 0x14;
    contract.RenderRecordScheduler.CountStrideBytes = 4;
    contract.RenderRecordScheduler.CategoryRecordStrideBytes = 0x60;
    contract.RenderRecordScheduler.RuntimePointerArrayOffset = 0x214;
    contract.RenderRecordScheduler.AuxValueArrayOffset = 0x4B4;
    contract.RenderRecordScheduler.MaxAcceptedRecordCount = 0x18;
    contract.RenderRecordScheduler.PostInsertCallbackAddress = 0x0030FDA8;
    contract.RenderRecordScheduler.SortFunctionAddress = 0x0030FDA8;
    contract.RenderRecordScheduler.SortFunctionEndAddress = 0x0030FE73;
    contract.RenderRecordScheduler.DrainFunctionAddress = 0x002FEA30;
    contract.RenderRecordScheduler.DrainFunctionEndAddress = 0x002FEABB;
    contract.RenderRecordScheduler.DrawHandleCountBaseOffset = 0x754;
    contract.RenderRecordScheduler.DrawHandlePointerArrayOffset = 0x770;
    contract.RenderRecordScheduler.DrawHandlePointerDrawHandleOffset = 0x14;
    contract.RenderRecordScheduler.DrawHandleSubmitFunctionAddress = 0x0030F4D0;
    contract.RenderRecordScheduler.DrawHandleSubmitPassValues = { 0, 1 };
    contract.RenderRecordScheduler.RuntimeDispatchVtableSlotOffset = 0x0C;
    contract.RenderRecordScheduler.Runtime1E4VtableAddress = 0x004EBD60;
    contract.RenderRecordScheduler.Runtime1E4DispatchVtableEntryAddress = 0x004EBD6C;
    contract.RenderRecordScheduler.Runtime1E4DispatchFunctionAddress = 0x003F9680;
    contract.RenderRecordScheduler.Runtime1E4DispatchFunctionEndAddress = 0x003F96B7;
    contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGatePointerLiteralAddress = 0x003F96B8;
    contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGateAddress = 0x0054C8C8;
    contract.RenderRecordScheduler.Runtime1E4DispatchFlagMasks = { 0x20, 0x40 };
    contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGateValues = { 0, 1 };
    contract.RenderRecordScheduler.RuntimeContainerPointerOffset = 0x08;
    contract.RenderRecordScheduler.RuntimeContainerSubmitVtableAddress =
        contract.RuntimeEffectWrapper.ContainerVtableAddress;
    contract.RenderRecordScheduler.RuntimeContainerSubmitVtableEntryAddress =
        contract.RuntimeEffectWrapper.ContainerSubmitVtableEntryAddress;
    contract.RenderRecordScheduler.RuntimeContainerSubmitVtableSlotOffset =
        contract.RuntimeEffectWrapper.ContainerSubmitVtableSlotOffset;
    contract.RenderRecordScheduler.RuntimeContainerSubmitFunctionAddress =
        contract.RuntimeEffectWrapper.ContainerSubmitFunctionAddress;
    contract.RenderRecordScheduler.RuntimeContainerSubmitEffectDrawFunctionAddress =
        contract.EffectDrawConsumer.DrawFunctionAddress;
    contract.RenderRecordScheduler.DrainWrappers = {
        { 0x004228C4, 0x004228CB, 1, false },
        { 0x004228CC, 0x004228D3, 2, false },
        { 0x004228D4, 0x004228DB, 5, false },
        { 0x004228DC, 0x004228E3, 3, false },
        { 0x004228E4, 0x004228EB, 4, false },
        { 0x0041AFAC, 0x0041B197, 6, true },
    };
    contract.RenderRecordScheduler.WritesRuntimePointerAndAuxValue = true;
    contract.RenderRecordScheduler.ReturnsFalseOnOverflow = true;
    contract.RenderRecordScheduler.SortsAscendingByAuxValue = true;
    contract.RenderRecordScheduler.SortMovesRuntimePointerWithAuxValue = true;
    contract.RenderRecordScheduler.DrainsDrawHandlesBeforeRuntimeVtableDispatch = true;
    contract.RenderRecordScheduler.RuntimeDispatchUsesVtableSlot = true;
    contract.RenderRecordScheduler.Runtime1E4DispatchPassesThroughContainerSubmit = true;
    contract.RenderRecordScheduler.DrainConnectsToEffectDrawConsumer = true;
    contract.RenderRecordScheduler.DrainConnectsToType6DrawCommand = false;
    contract.StormRuntimeSchedule.UpdateFunctionAddress = 0x004599BC;
    contract.StormRuntimeSchedule.UpdateFunctionEndAddress = 0x00459F67;
    contract.StormRuntimeSchedule.GameplayDrawCallsiteAddress = 0x002E2DB0;
    contract.StormRuntimeSchedule.GameplayDrawModeGatePlayOffset = 0x3266;
    contract.StormRuntimeSchedule.GameplayDrawRequiresNonZeroModeGate = true;
    contract.StormRuntimeSchedule.GameplayDrawPassesModeGateAsSecondArgument = true;
    contract.StormRuntimeSchedule.FadeByte0PlayOffset = 0x3267;
    contract.StormRuntimeSchedule.FadeByte1PlayOffset = 0x3268;
    contract.StormRuntimeSchedule.RuntimePointerPlayOffset = 0x3404;
    contract.StormRuntimeSchedule.RuntimePointerKankyoOffset = 0x274;
    contract.StormRuntimeSchedule.PhaseAccumulatorPlayOffset = 0x3408;
    contract.StormRuntimeSchedule.PhaseAccumulatorKankyoOffset = 0x278;
    contract.StormRuntimeSchedule.EnvironmentVectorResolverAddress = 0x002E4660;
    contract.StormRuntimeSchedule.PauseContextGetStateAddress = 0x003695F8;
    contract.StormRuntimeSchedule.SlotParameterWriterAddress = 0x003429C8;
    contract.StormRuntimeSchedule.RuntimeTransformBlockOffsets = { 0x110, 0x140 };
    contract.StormRuntimeSchedule.DescriptorSlotIndices = { 0, 1 };
    contract.StormRuntimeSchedule.SchedulerFunctionAddress = contract.RenderRecordScheduler.FunctionAddress;
    contract.StormRuntimeSchedule.SchedulerBaseLiteralAddress = 0x00459F84;
    contract.StormRuntimeSchedule.SchedulerBaseAddress = 0x005C0BA8;
    contract.StormRuntimeSchedule.SchedulerCategory = 2;
    contract.StormRuntimeSchedule.SchedulerAuxValue = 0;
    contract.StormRuntimeSchedule.UsesRenderRecordSchedulerInsteadOfSubmitWrapper = true;
    contract.StormRuntimeSchedule.ActiveSchedulingResolved = true;

    contract.EnvironmentVector.PrepFunctionAddress = 0x004594E0;
    contract.EnvironmentVector.PrepFunctionEndAddress = 0x004596AF;
    contract.EnvironmentVector.PrepGlobalAngleStateAddress = 0x00587958;
    contract.EnvironmentVector.PrepGlobalAngleHalfwordOffset = 0x0C;
    contract.EnvironmentVector.PrepGlobalEnvironmentStateAddress = 0x00531EB4;
    contract.EnvironmentVector.PrepGlobalVectorScaleFloatOffset = 0x1C;
    contract.EnvironmentVector.PrepOutputVectorXOffset = 0x3194;
    contract.EnvironmentVector.PrepOutputVectorYOffset = 0x3198;
    contract.EnvironmentVector.PrepOutputVectorZOffset = 0x319C;
    contract.EnvironmentVector.PrepVectorSubmitTargetOffset = 0x327C;
    contract.EnvironmentVector.PrepVectorSubmitContextOffset = 0x01B8;
    contract.EnvironmentVector.PrepSmoothingGateAddress = 0x0037571C;
    contract.EnvironmentVector.PrepSmoothingHelperAddress = 0x0036E168;
    contract.EnvironmentVector.PrepSinHelperAddress = 0x002CFCA0;
    contract.EnvironmentVector.PrepCosHelperAddress = 0x00338F60;
    contract.EnvironmentVector.PrepSubmitPositiveVectorAddress = 0x0047D578;
    contract.EnvironmentVector.PrepSubmitNegativeVectorAddress = 0x0047D600;
    contract.EnvironmentVector.ResolverFunctionAddress = 0x002E4660;
    contract.EnvironmentVector.ResolverFunctionEndAddress = 0x002E47BF;
    contract.EnvironmentVector.ResolverSourceObjectPointerOffset = 0x340C;
    contract.EnvironmentVector.ResolverSourceTableHelperAddress = 0x003373B8;
    contract.EnvironmentVector.ResolverSourceTableHelperIndex = 0;
    contract.EnvironmentVector.ResolverSourceRecordCount = 4;
    contract.EnvironmentVector.ResolverSourceRecordStrideBytes = 0x0C;
    contract.EnvironmentVector.ResolverSourceComponentCount = 3;
    contract.EnvironmentVector.ResolverOutputRecordStrideBytes = 0x10;
    contract.EnvironmentVector.ResolverOutputComponentCount = 4;
    contract.EnvironmentVector.ResolverOutputAlphaWord = 0x3F800000;
    contract.EnvironmentVector.ResolverStateByteOffset = 0x31B0;
    contract.EnvironmentVector.ResolverTargetLightSettingOffset = 0x3237;
    contract.EnvironmentVector.ResolverTargetLightSettingInvalidValue = 0xFF;
    contract.EnvironmentVector.ResolverActiveOrTargetRecordIndex = 1;
    contract.EnvironmentVector.ResolverFallbackGlobalStateAddress = 0x00531EB4;
    contract.EnvironmentVector.ResolverFallbackBlendFromIndexOffset = 0x04;
    contract.EnvironmentVector.ResolverFallbackBlendToIndexOffset = 0x05;
    contract.EnvironmentVector.ResolverFallbackBlendWeightFloatOffset = 0x28;
    contract.EnvironmentVector.ResolverColorPackScaleWord = 0x437F0000;
    contract.EnvironmentVector.ResolverPackedColorOutputOffset = 0x5B8;
    contract.EnvironmentVector.ResolverPackedColorModeByteOffset = 0x5BC;
    contract.EnvironmentVector.ResolverPackedColorModeValue = 3;
    contract.EnvironmentVector.ResolverPackedColorCallsiteAddresses = {
        0x002E3320,
        0x002E33A4,
        0x00449524,
        0x00459C1C,
    };

    contract.EnvironmentLightSettingState.SurfaceTypeGetterAddress = 0x002C1E10;
    contract.EnvironmentLightSettingState.SurfaceTypeFieldReaderAddress = 0x00322088;
    contract.EnvironmentLightSettingState.SurfaceTypeLightSettingFieldId = 1;
    contract.EnvironmentLightSettingState.SurfaceTypeRawIndexLeftShift = 21;
    contract.EnvironmentLightSettingState.SurfaceTypeRawIndexRightShift = 27;
    contract.EnvironmentLightSettingState.PlayerFloorLightSettingGetterCallsiteAddress = 0x0032F140;
    contract.EnvironmentLightSettingState.PlayerFloorChangeHelperCallsiteAddress = 0x0032F14C;
    contract.EnvironmentLightSettingState.ChangeHelperAddress = 0x0032B13C;
    contract.EnvironmentLightSettingState.TransitionResetHelperAddress = 0x004B8FC0;
    contract.EnvironmentLightSettingState.TransitionResetCallerAddress = 0x002D0AFC;
    contract.EnvironmentLightSettingState.TransitionModeRequestHelperAddress = 0x0033B880;
    contract.EnvironmentLightSettingState.RequestHelperAddress = 0x00316D74;
    contract.EnvironmentLightSettingState.CameraWaterRequestCallsiteAddress = 0x002D09D8;
    contract.EnvironmentLightSettingState.PlayEnvironmentBaseOffset = 0x3000;
    contract.EnvironmentLightSettingState.EnvironmentStateByteOffset = 0x31B0;
    contract.EnvironmentLightSettingState.TransitionStateByteOffset = 0x3234;
    contract.EnvironmentLightSettingState.CurrentLightSettingOffset = 0x3235;
    contract.EnvironmentLightSettingState.PreviousLightSettingOffset = 0x3236;
    contract.EnvironmentLightSettingState.TargetLightSettingOffset = 0x3237;
    contract.EnvironmentLightSettingState.BlendWeightFloatOffset = 0x3258;
    contract.EnvironmentLightSettingState.NormalizeThreshold = 31;
    contract.EnvironmentLightSettingState.NormalizeFallbackValue = 0;
    contract.EnvironmentLightSettingState.TargetInvalidValue = 0xFF;
    contract.EnvironmentLightSettingState.BlendWeightZeroWord = 0x00000000;
    contract.EnvironmentLightSettingState.BlendWeightOneWord = 0x3F800000;
    contract.EnvironmentLightSettingState.FallbackGlobalStateAddress = 0x00531EB4;
    contract.EnvironmentLightSettingState.FallbackGlobalPreviousByteOffset = 0x01;
    contract.EnvironmentLightSettingState.FallbackMirrorCurrentByteOffset = 0x31B1;
    contract.EnvironmentLightSettingState.FallbackMirrorPreviousByteOffset = 0x31B2;
    contract.EnvironmentLightSettingState.TransitionModeCurrentOffset = 0x31B1;
    contract.EnvironmentLightSettingState.TransitionModeTargetOffset = 0x31B2;
    contract.EnvironmentLightSettingState.TransitionModeBlendActiveOffset = 0x31B3;
    contract.EnvironmentLightSettingState.TransitionModeBlendRemainingHalfwordOffset = 0x31B4;
    contract.EnvironmentLightSettingState.TransitionModeBlendDurationHalfwordOffset = 0x31B6;
    contract.EnvironmentLightSettingState.DecompileConsumerScanCandidateCount = 12;
    contract.EnvironmentLightSettingState.TransitionModeRequestHelperWritesModeState = true;
    contract.EnvironmentLightSettingState.DecompileConsumerScanFoundFinalScenePacketConsumer = false;
    contract.EnvironmentLightSettingState.FinalScenePacketConsumerResolved = false;
    contract.EnvironmentLightSettingState.RequestHelperCallsiteAddresses = {
        0x002D09D8,
        0x003B68AC,
        0x003B6BBC,
        0x003B7078,
        0x003B70E8,
        0x003B7220,
    };
    contract.EnvironmentLightSettingState.StateMachineHelperCandidateAddresses = {
        0x0032B13C,
        0x004B8FC0,
    };
    contract.EnvironmentLightSettingState.ExcludedActorOrCutsceneWriterCandidateAddresses = {
        0x001317DC,
        0x001558A4,
        0x0017DD68,
        0x0018741C,
        0x001A7E18,
        0x001C8DB4,
        0x00242A94,
        0x00260F24,
        0x002C5BA0,
        0x003D374C,
    };

    contract.ZsiLightSettingsRecord.CommandId = 0x0F;
    contract.ZsiLightSettingsRecord.SceneCommandHandlerTableAddress = 0x0053CC84;
    contract.ZsiLightSettingsRecord.SceneCommandHandlerAddress = 0x00379188;
    contract.ZsiLightSettingsRecord.SceneCommandEntrySizeBytes = 8;
    contract.ZsiLightSettingsRecord.SceneCommandCountByteOffset = 1;
    contract.ZsiLightSettingsRecord.SceneCommandSegmentOffsetWordOffset = 4;
    contract.ZsiLightSettingsRecord.PlayStateLightSettingsCountOffset = 0x322C;
    contract.ZsiLightSettingsRecord.PlayStateLightSettingsListPointerOffset = 0x3230;
    contract.ZsiLightSettingsRecord.SceneCommandStoresNativeListPointer = true;
    contract.ZsiLightSettingsRecord.CandidateStartDeltas = {
        0x00,
        0x04,
        0x08,
        0x10,
    };
    contract.ZsiLightSettingsRecord.NativeRecordSizeBytes = 0x1C;
    contract.ZsiLightSettingsRecord.LegacyRecordSizeBytes = 0x16;
    contract.ZsiLightSettingsRecord.NativeRecordLayoutName = "oot3d_pica_light_settings_record_0x1c";
    contract.ZsiLightSettingsRecord.LegacyRecordLayoutName = "legacy_light_settings_record_0x16_raw";
    contract.ZsiLightSettingsRecord.NativeEnvPrefixSizeBytes = 0x0F;
    contract.ZsiLightSettingsRecord.ColorComponentOrder = "bgr_u8";
    contract.ZsiLightSettingsRecord.DirectionComponentEncoding = "signed_vec3_u8";
    contract.ZsiLightSettingsRecord.AmbientColorOffset = 0x00;
    contract.ZsiLightSettingsRecord.Light0DirectionOffset = 0x03;
    contract.ZsiLightSettingsRecord.Light0ColorOffset = 0x06;
    contract.ZsiLightSettingsRecord.Light1DirectionOffset = 0x09;
    contract.ZsiLightSettingsRecord.Light1ColorOffset = 0x0C;
    contract.ZsiLightSettingsRecord.Native3dsTailByteOffset = 0x0F;
    contract.ZsiLightSettingsRecord.FloatParam0Offset = 0x10;
    contract.ZsiLightSettingsRecord.FloatParam1Offset = 0x14;
    contract.ZsiLightSettingsRecord.TailWordOffset = 0x18;
    contract.ZsiLightSettingsRecord.TailWordSizeBytes = 4;
    contract.ZsiLightSettingsRecord.ActorPacketDiffuse0ColorOffset = 0x04;
    contract.ZsiLightSettingsRecord.ActorPacketDiffuse1ColorOffset = 0x0A;
    contract.ZsiLightSettingsRecord.ActorPacketPicaFogColorOffset = 0x0D;
    contract.ZsiLightSettingsRecord.ActorPacketAmbientPreviousTailByte0Offset = 0x1A;
    contract.ZsiLightSettingsRecord.ActorPacketAmbientPreviousTailByte1Offset = 0x1B;
    contract.ZsiLightSettingsRecord.ActorPacketAmbientCurrentByteOffset = 0x00;
    contract.ZsiLightSettingsRecord.ActorPacketRecordIndexDelta = 2;
    contract.ZsiLightSettingsRecord.ActorPacketRecordSelectionSource =
        "native_0x1c_light_settings_record_triplet_selected_environment_record_plus_two_actor_vs_packet_record";
    contract.ZsiLightSettingsRecord.ActorPacketPicaFogColorSource =
        "native_0x1c_actor_vs_material_packet_record_rgb_offset_0x0d_written_to_GPUREG_FOG_COLOR_by_00452894";
    contract.ZsiLightSettingsRecord.RuntimeConsumerAddress = 0x0045DD50;
    contract.ZsiLightSettingsRecord.RuntimeConsumerCallerAddress = 0x002E43CC;
    contract.ZsiLightSettingsRecord.RuntimeStateBasePlayOffset = 0x3190;
    contract.ZsiLightSettingsRecord.RuntimeOutputBasePlayOffset = 0x0A70;
    contract.ZsiLightSettingsRecord.RuntimePauseFlagPlayOffset = 0x318C;
    contract.ZsiLightSettingsRecord.RuntimeInitializedFlagPlayOffset = 0x3234;
    contract.ZsiLightSettingsRecord.RuntimeCurrentIndexPlayOffset = 0x3235;
    contract.ZsiLightSettingsRecord.RuntimePreviousIndexPlayOffset = 0x3236;
    contract.ZsiLightSettingsRecord.RuntimeTargetIndexPlayOffset = 0x3237;
    contract.ZsiLightSettingsRecord.RuntimeBlendWeightPlayOffset = 0x3258;
    contract.ZsiLightSettingsRecord.RuntimeTransitionTableAddress = 0x00531EFC;
    contract.ZsiLightSettingsRecord.RuntimeTransitionTableCodeBase = 0x00100000;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeCount = 5;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeStrideBytes = 0x36;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntryCount = 9;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntrySizeBytes = 6;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntryStartAngleOffset = 0x00;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntryEndAngleOffset = 0x02;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntryFromIndexOffset = 0x04;
    contract.ZsiLightSettingsRecord.RuntimeTransitionEntryToIndexOffset = 0x05;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeStateBasePlayOffset = 0x3190;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeCurrentRelativeOffset = 0x21;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeTargetRelativeOffset = 0x22;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendActiveRelativeOffset = 0x23;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendRemainingHalfwordRelativeOffset = 0x24;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendDurationHalfwordRelativeOffset = 0x26;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeCurrentOffset = 0x31B1;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeTargetOffset = 0x31B2;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendActiveOffset = 0x31B3;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendRemainingHalfwordOffset = 0x31B4;
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendDurationHalfwordOffset = 0x31B6;
    contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleWorkingStateAddress = 0x00587958;
    contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleWorkingHalfwordOffset = 0x0C;
    contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleOutputStateAddress = 0x00588E58;
    contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleOutputHalfwordOffset = 0xA8;
    contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackStateAddress = 0x00531EB4;
    contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackModeOffset = 0x01;
    contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackModeWeightFloatOffset = 0x28;
    contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackFromIndexOffset = 0x04;
    contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackToIndexOffset = 0x05;
    contract.ZsiLightSettingsRecord.RuntimeScalar0Offset = 0x00;
    contract.ZsiLightSettingsRecord.RuntimeScalar1Offset = 0x04;
    contract.ZsiLightSettingsRecord.RuntimePackedHalfwordOffset = 0x08;
    contract.ZsiLightSettingsRecord.RuntimePackedHalfwordMask = 0x03FF;
    contract.ZsiLightSettingsRecord.RuntimeCameraFarOutputOffset = 0x04;
    contract.ZsiLightSettingsRecord.RuntimeFogFarOutputOffset = 0x08;
    contract.ZsiLightSettingsRecord.RuntimeFogNearOutputOffset = 0x0C;
    contract.ZsiLightSettingsRecord.RuntimeCameraFarPlayOffset = 0x0A74;
    contract.ZsiLightSettingsRecord.RuntimeFogFarPlayOffset = 0x0A78;
    contract.ZsiLightSettingsRecord.RuntimeFogNearPlayOffset = 0x0A7C;
    contract.ZsiLightSettingsRecord.RuntimeFogNearAddendStateOffset = 0x7E;
    contract.ZsiLightSettingsRecord.RuntimeFogFarAddendStateOffset = 0x84;
    contract.ZsiLightSettingsRecord.RuntimeProjectionMatrixPlayOffset = 0x01DC;
    contract.ZsiLightSettingsRecord.RuntimeViewInitAddress = 0x002E5A38;
    contract.ZsiLightSettingsRecord.RuntimeViewDefaultNearLiteralAddress = 0x002E5B00;
    contract.ZsiLightSettingsRecord.RuntimeViewDefaultFarLiteralAddress = 0x002E5B04;
    contract.ZsiLightSettingsRecord.RuntimeViewUpdateAddress = 0x002DE690;
    contract.ZsiLightSettingsRecord.RuntimeProjectionBuildAddress = 0x00471BA4;
    contract.ZsiLightSettingsRecord.RuntimeSceneProjectionFarLiteralAddress = 0x00479290;
    contract.ZsiLightSettingsRecord.RuntimeSceneProjectionMatrixOffset = 0x94;
    contract.ZsiLightSettingsRecord.RuntimeTransitionRateShift = 10;
    contract.ZsiLightSettingsRecord.RuntimeRecordStartDelta = 0x10;
    contract.ZsiLightSettingsRecord.RuntimeAmbientColorOffset = 0x0A;
    contract.ZsiLightSettingsRecord.RuntimeLight0DirectionOffset = 0x0D;
    contract.ZsiLightSettingsRecord.RuntimeLight0ColorOffset = 0x10;
    contract.ZsiLightSettingsRecord.RuntimeLight1DirectionOffset = 0x13;
    contract.ZsiLightSettingsRecord.RuntimeLight1ColorOffset = 0x16;
    contract.ZsiLightSettingsRecord.RuntimeFogColorOffset = 0x19;
    contract.ZsiLightSettingsRecord.RuntimePreAddendAmbientColorStateOffset = 0xB2;
    contract.ZsiLightSettingsRecord.RuntimePreAddendLight0ColorStateOffset = 0xB8;
    contract.ZsiLightSettingsRecord.RuntimePreAddendLight1ColorStateOffset = 0xBE;
    contract.ZsiLightSettingsRecord.RuntimePreAddendFogColorStateOffset = 0xC1;
    contract.ZsiLightSettingsRecord.RuntimeColorAddendAmbientStateOffset = 0x6C;
    contract.ZsiLightSettingsRecord.RuntimeColorAddendLightStateOffset = 0x72;
    contract.ZsiLightSettingsRecord.RuntimeColorAddendFogStateOffset = 0x78;
    contract.ZsiLightSettingsRecord.RuntimeFinalAmbientColorOutputOffset = 0x0E;
    contract.ZsiLightSettingsRecord.RuntimeFinalFogColorOutputOffset = 0x12;
    contract.ZsiLightSettingsRecord.RuntimeFinalAmbientColorPlayOffset = 0x0A7E;
    contract.ZsiLightSettingsRecord.RuntimeFinalFogColorPlayOffset = 0x0A82;
    contract.ZsiLightSettingsRecord.RuntimeColorComponentCount = 3;
    contract.ZsiLightSettingsRecord.RuntimeDirectionComponentCount = 3;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadProducerAddress = 0x0045DD50;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadConsumerHandlerAddress = 0x00253A4C;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotCount = 2;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotStrideBytes = 0x18;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotOffsets = { 0x30, 0x48 };
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionOffset = 0x00;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorOffset = 0x03;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSizeBytes = 0x06;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSourceOffsets = { 0xB5, 0xBB };
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSourceOffsets = { 0x33, 0x4B };
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionComponentCount = 3;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorComponentCount = 3;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionScaleDenominator = 127;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionAngleBias = 0x8000;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSinScaleX = -120.0f;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleY = 120.0f;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleZ = 20.0f;
    contract.ZsiLightSettingsRecord.RuntimeColorComponentOrder = "rgb_u8";
    contract.ZsiLightSettingsRecord.RuntimeDirectionComponentEncoding = "signed_vec3_s8";
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSource =
        "0045dd50_runtime_environment_payload_r4_plus_0x30_0x48_consumed_by_00253a4c";
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSource =
        "0045dd50_active_angle_minus_0x8000_sin_cos_scaled_to_r4_plus_0xb5_0xbb";
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSource =
        "0045dd50_actor_vs_record_plus_two_rgb_fields_after_native_color_bias_to_r4_plus_0x33_0x4b";
    contract.ZsiLightSettingsRecord.RuntimeColorAddendSource =
        "0045dd50_clamps_preaddend_rgb_with_s16_playstate_environment_color_addends_"
        "state_plus_0x6c_0x72_0x78";
    contract.ZsiLightSettingsRecord.RuntimeFinalFogColorSource =
        "0045dd50_writes_final_fog_rgb_to_play_plus_0x0a82_from_state_plus_0xc1_"
        "plus_s16_addends_state_plus_0x78_then_Gameplay_Draw_passes_it_to_00464b2c";
    contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleSource =
        "0045dd50_updates_00587958_plus_0x0c_and_mirrors_to_00588e58_plus_0xa8_before_00531efc_lookup";
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeStateSource =
        "0045dd50_reads_mode_state_from_argument_play_plus_0x3190_relative_offsets_0x21_0x22_0x23_0x24_0x26";
    contract.ZsiLightSettingsRecord.RuntimeTransitionBlendFormula =
        "angle_weight = 1.0 - (entry.end_angle - active_angle) / (entry.end_angle - entry.start_angle); "
        "blend record[entry.from_index] to record[entry.to_index] per component";
    contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendFormula =
        "if mode_blend_active then mode_weight = (duration - remaining) / duration and blend current-mode result "
        "toward target-mode result";
    contract.ZsiLightSettingsRecord.RuntimeConsumerResolved = true;
    contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadResolved = true;
    contract.ZsiLightSettingsRecord.RuntimeTransitionTableDecodedFromCodeBin = true;
    contract.ZsiLightSettingsRecord.FinalPacketWriterResolved = false;

    contract.SubmitManager.SubmitWrapperAddress = 0x00371EAC;
    contract.SubmitManager.SubmitCoreAddress = 0x00367788;
    contract.SubmitManager.SubmitRecordWriteAddress = 0x002C1AE8;
    contract.SubmitManager.ManagerInitializerAddress = 0x0041706C;
    contract.SubmitManager.ManagerVtableAddress = 0x004EBD78;
    contract.SubmitManager.ContextSubmitManagerOffset = 0x180;
    contract.SubmitManager.ManagerStorageSizeBytes = 0x2170;
    contract.SubmitManager.InitGuardAddress = 0x003679B4;
    contract.SubmitManager.GlobalInitAddress = 0x0036788C;
    contract.SubmitManager.InitFlagAddress = 0x0055A21C;
    contract.SubmitManager.GlobalContextAddress = 0x005BE5B8;
    contract.SubmitManager.SubmitManagerAddress = 0x005BE738;
    contract.SubmitManager.PrimaryQueueCountStorageOffset = 0x08;
    contract.SubmitManager.PrimaryQueueStorageOffset = 0x2C;
    contract.SubmitManager.SecondaryQueueStorageOffset = 0x102C;
    contract.SubmitManager.AuxiliaryQueueStorageOffset = 0x202C;
    contract.SubmitManager.AuxiliaryQueueCapacity = 0x20;
    contract.SubmitManager.SmallQueueCountOffset = 0x212C;
    contract.SubmitManager.SmallQueueStorageOffset = 0x2130;
    contract.SubmitManager.SmallQueueCapacity = 8;
    contract.SubmitManager.InitialRecordStateValue = 3;
    contract.SubmitManager.ManagerModeFlagOffset = 0x04;
    contract.SubmitManager.SecondaryQueueCountOffset = 0x0C;
    contract.SubmitManager.QueueCapacityOffset = 0x14;
    contract.SubmitManager.PrimaryQueueCountPointerOffset = 0x18;
    contract.SubmitManager.PrimaryQueueBaseOffset = 0x1C;
    contract.SubmitManager.SecondaryQueueBaseOffset = 0x24;
    contract.SubmitManager.SubmitRecordStrideBytes = 8;
    contract.SubmitManager.SubmitRecordRuntimePointerOffset = 0;
    contract.SubmitManager.SubmitRecordValidByteOffset = 4;
    contract.SubmitManager.SubmitRecordValidValue = 1;
    contract.SubmitManager.RuntimeVtableSlotOffset = 0x08;
    contract.SubmitManager.RuntimeSubmitCallbackVtableSlotOffset = contract.SubmitManager.RuntimeVtableSlotOffset;
    contract.SubmitManager.RuntimeRenderDrainVtableSlotOffset =
        contract.RenderRecordScheduler.RuntimeDispatchVtableSlotOffset;
    contract.SubmitManager.CallbackContext0Offset = 0x114;
    contract.SubmitManager.CallbackContext1Offset = 0x174;
    contract.SubmitManager.CallbackContext2Offset = 0x30;
    contract.SubmitManager.Runtime1E4InitializerAddress = 0x002C4F00;
    contract.SubmitManager.Runtime1E4VtableAddress = 0x004EBD60;
    contract.SubmitManager.Runtime1E4SubmitCallbackAddress = 0x003F96BC;
    contract.SubmitManager.Runtime1E4RenderDrainCallbackAddress =
        contract.RenderRecordScheduler.Runtime1E4DispatchFunctionAddress;
    contract.SubmitManager.Runtime28CInitializerAddress = 0x004970E0;
    contract.SubmitManager.Runtime28CVtableAddress = 0x004EBE9C;
    contract.SubmitManager.Runtime28CSubmitCallbackAddress = 0x003FC2F8;
    contract.SubmitManager.RuntimeDrawCountSetterAddress = 0x00333294;
    contract.SubmitManager.RuntimeDrawPayloadOffset = 0x174;
    contract.SubmitManager.RuntimeFlagsOffset = 0x178;
    contract.SubmitManager.FrameDrawPassAddress = 0x00300328;
    contract.SubmitManager.FrameDrawPassManagerOffset = 0x180;
    contract.SubmitManager.PrimarySecondaryPass0Address = 0x004224AC;
    contract.SubmitManager.PrimarySecondaryPass1Address = 0x004224DC;
    contract.SubmitManager.AuxiliaryPass0Address = 0x002FAE00;
    contract.SubmitManager.AuxiliaryPass1Address = 0x002FAD1C;
    contract.SubmitManager.SmallQueueDrainAddress = 0x0042250C;
    contract.SubmitManager.RecordArrayPass0Address = 0x002FAE10;
    contract.SubmitManager.RecordArrayPass1Address = 0x002FAD2C;
    contract.SubmitManager.RecordArrayCombinedPassAddress = 0x003FE374;
    contract.SubmitManager.RecordArrayDepthSortAddress = 0x00422910;
    contract.SubmitManager.AuxiliaryRouteBeginAddress = 0x0032D5DC;
    contract.SubmitManager.AuxiliaryRouteEndAddress = 0x0032D5B8;
    contract.SubmitManager.QueueResetAddress = 0x00417034;
    contract.SubmitManager.AlternateQueueResetAddress = 0x0041ACAC;
    contract.SubmitManager.PrimaryQueuePointerOffset = 0x20;
    contract.SubmitManager.SecondaryQueuePointerOffset = 0x24;
    contract.SubmitManager.AuxiliaryQueueCountOffset = 0x10;
    contract.SubmitManager.AuxiliaryQueuePointerOffset = 0x28;
    contract.SubmitManager.MainQueueCapacity = 0x200;
    contract.SubmitManager.NormalRouteModeValue = 0;
    contract.SubmitManager.AuxiliaryRouteModeValue = 2;
    contract.SubmitManager.FramePrimarySecondaryGateArgumentValue = 0;
    contract.SubmitManager.RecordArrayDefaultModeArgumentValue = 0;
    contract.SubmitManager.RuntimeRecordStateDrawHandleValue = 0;
    contract.SubmitManager.RuntimeRecordStateCallbackValue = 1;
    contract.SubmitManager.RuntimeDrawGateByteOffset = 0xAD;
    contract.SubmitManager.RuntimePrimaryDrawHandleOffset = 0x14;
    contract.SubmitManager.RuntimeSecondaryDrawHandleOffset = 0x18;
    contract.SubmitManager.RuntimeDefaultDrawStateFactoryAddress = 0x002C1AF8;
    contract.SubmitManager.RuntimeDefaultDrawStateBlockSizeBytes = 0x30;
    contract.SubmitManager.RuntimeDefaultDrawStateCopyOffsets = {
        0x54, 0x84, 0x0C, 0x140, 0x110, 0xB4
    };
    contract.SubmitManager.RuntimeDefaultDrawStateCopyCoversSubmitDrawHandleOffsets = true;
    contract.SubmitManager.Runtime1E4InitializerInstallsActiveSubmitDrawHandles = false;
    contract.SubmitManager.RuntimeAnimatedDrawHandleResolverAddress = 0x003687A8;
    contract.SubmitManager.RuntimeAnimatedDrawHandleResolverPrimaryHandleOffset =
        contract.SubmitManager.RuntimePrimaryDrawHandleOffset;
    contract.SubmitManager.RuntimeAnimatedDrawHandleResolverPacketPrepSourceOffset =
        contract.DrawHandleSubmit.MaterialPacketPointerOffset;
    contract.SubmitManager.RuntimeAnimatedDrawHandleResolverReturnsPacketPrepSource = true;
    contract.SubmitManager.RuntimeAnimatedDrawHandleGateByteOffset = 0x1B4;
    contract.SubmitManager.RuntimeState1ResolverAddress = 0x002EA854;
    contract.SubmitManager.RuntimeState1GateWordOffset = 0x170;
    contract.SubmitManager.RuntimeState1VtableSlotOffset = 0x0C;
    contract.SubmitManager.RuntimeState1FlagsMask = 0x80;
    contract.SubmitManager.DrawHandleSubmitAddress = contract.DrawHandleSubmit.FunctionAddress;
    contract.SubmitManager.DrawHandlePassByteOffset = contract.DrawHandleSubmit.SubmittedPassByteOffset;
    contract.SubmitManager.DrawHandleMaterialPacketOffset = contract.DrawHandleSubmit.MaterialPacketPointerOffset;
    contract.SubmitManager.DrawHandleSubmittedPass0Value = 0;
    contract.SubmitManager.DrawHandleSubmittedPass1Value = 1;
    contract.SubmitManager.DrawHandlePacketPrepAddress = contract.DrawHandleSubmit.PacketPrepAddress;
    contract.SubmitManager.DirectSubmitCallerCount = 57;
    contract.SubmitManager.SubmitRecordWriteStoresRuntimePointerAndValidByteOnly = true;
    contract.SubmitManager.SubmitCoreInvokesRuntimeVtableSlot = true;
    contract.SubmitManager.SubmitCoreUsesSubmitCallbackVtableSlot = true;
    contract.SubmitManager.SubmitCoreUsesRenderDrainVtableSlot = false;
    contract.SubmitManager.SubmitCorePassesNativeCallbackContexts = true;
    contract.SubmitManager.SubmitCoreDirectlyWritesPacketPrepSource = false;
    contract.SubmitManager.SubmitCoreDirectlyWritesDrawHandlePacketPrepSource = false;
    contract.SubmitManager.SubmitManagerConnectsToEffectDrawConsumer = false;
    contract.SubmitManager.SubmitManagerConnectsToType6DrawCommand = false;
    contract.SubmitManager.PacketPrepBackingWriterResolved = false;
    contract.SubmitManager.PacketPrepBackingWriterStatus =
        "00367788/00371EAC enqueue runtime records and dispatch vtable+0x08; 002C1AE8 writes only runtime pointer "
        "and valid byte. The proven effect route uses the distinct render-record drain slot vtable+0x0C for 1E4 "
        "instances, not the submit-manager update slot. 002C4F00 initializes runtime default draw-state blocks but "
        "does not install active submit draw handles; 002FC694/00313CE0 are draw-list allocation helpers, not submitted "
        "handle packet writers. Continue through actor/CMB draw owners for the packet backing contents consumed by "
        "003130A4.";
    contract.SubmitManager.WeatherParticleSubmitGameplayDrawCallsiteAddress = 0x002E2CCC;
    contract.SubmitManager.WeatherParticleSubmitFunctionAddress = 0x00463544;
    contract.SubmitManager.WeatherParticleSubmitFunctionEndAddress = 0x00463CE7;
    contract.SubmitManager.WeatherParticleSubmitPlayGateByteOffset = 0x326F;
    contract.SubmitManager.WeatherParticleSubmitPlayGateRequiresNonZero = true;
    contract.SubmitManager.WeatherParticleSubmitGateAlsoParticleLoopCount = true;
    contract.SubmitManager.WeatherParticleKankyoObjectBasePlayOffset = 0x3190;
    contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes = {
        {
            "rain",
            0x00463A68,
            0x3378,
            0x1E8,
        },
        {
            "ripple",
            0x00463CD8,
            0x3380,
            0x1F0,
        },
    };
    contract.SubmitManager.VerifiedKankyoSubmitCallsiteAddresses = {
        contract.ThunderUpdate.RuntimeSubmitCallsiteAddress,
    };
    contract.SubmitManager.VerifiedKankyoSubmittedRuntimeOffset = contract.ThunderUpdate.RuntimeInstanceOffset;
    contract.SubmitManager.VerifiedKankyoSubmittedRuntimeStrideBytes = contract.ThunderUpdate.RuntimeInstanceStrideBytes;
    contract.SubmitManager.VerifiedKankyoSubmittedRuntimeSlotCount = contract.ThunderUpdate.SlotCount;
    contract.SubmitManager.UnresolvedKankyoRuntimeSubmitOffsets = {};
    contract.SubmitManager.NonThunderKankyoRuntimeSubmitsResolved = true;
    contract.SubmitManager.VtableSlots = {
        {
            "record_array_pass_0",
            0x08,
            0x002FAE10,
        },
        {
            "record_array_pass_1",
            0x0C,
            0x002FAD2C,
        },
        {
            "record_array_combined_pass",
            0x10,
            0x003FE374,
        },
        {
            "record_array_depth_sort",
            0x14,
            0x00422910,
        },
        {
            "manager_cleanup_release",
            0x24,
            0x003FB2A8,
        },
        {
            "material_draw_state_setup",
            0x28,
            0x003FAC2C,
        },
        {
            "descriptor_light_list_build",
            0x30,
            0x003F9B5C,
        },
        {
            "runtime_three_slot_light_packet_pack",
            0x34,
            0x003FA5D0,
        },
        {
            "runtime_three_slot_light_packet_fallback_pack",
            0x38,
            0x003FA34C,
        },
        {
            "render_state_setup",
            0x44,
            0x003FAD68,
        },
    };

    contract.GenericCommandWriterOwnership.ScalarWriterAddress =
        contract.GeneralLightListEmitter.GenericCommandWriterAddress;
    contract.GenericCommandWriterOwnership.VectorUniformWriterAddress =
        contract.LightingRegisterEmitter.GenericDirectWriterAddress;
    contract.GenericCommandWriterOwnership.ScalarWriterIsPacketFormatHelper = true;
    contract.GenericCommandWriterOwnership.VectorUniformWriterIsPacketFormatHelper = true;
    contract.GenericCommandWriterOwnership.SemanticsAttachedToCallers = true;
    contract.GenericCommandWriterOwnership.OwnershipRule =
        "0x00307BD8 and 0x00307C94 emit packet/VSH-uniform command formats only; register semantics "
        "are owned by decoded caller contracts.";
    contract.GenericCommandWriterOwnership.Callers = {
        {
            0x0031317C,
            contract.PacketPrep.FunctionAddress,
            "NativeKankyoPacketPrepContract",
            "runtime_vs_light_packet_slot_upload",
            { 0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x056, 0x057, 0x058 },
            true,
            false,
            true,
            true,
            "enabled branch in 0x003130A4 uses 0x00466EA0/0x00466F00 with code.bin tables "
            "0x004E2EA4/0x004E2EB0",
        },
        {
            0x00452894,
            contract.DrawHandleSubmit.MaterialDrawDispatchAddress,
            "NativeKankyoDrawHandleSubmitContract",
            "cmb_mesh_material_lane_selects_material_state_setup",
            {},
            true,
            false,
            true,
            true,
            "0x00452894 loads the selected material lane source pointer and gates 0x0047D6AC at "
            "0x004528B0 with param2=1",
        },
        {
            contract.MaterialScalarEmit.DispatchAddress,
            contract.MaterialScalarEmit.DispatchAddress,
            "NativePicaMaterialScalarEmitContract",
            "material_scalar_payload_registers_0x0e6_0x0e8",
            { contract.MaterialScalarEmit.ScalarRegister0, contract.MaterialScalarEmit.ScalarRegister1 },
            true,
            true,
            true,
            true,
            "0x0047FF0C/0x0047FF28 call 0x00307BD8 for material payload registers 0x0E6 and 0x0E8",
        },
        {
            0x00438550,
            0x00438550,
            "NativeFramebufferFlushRegisterEmit",
            "framebuffer_flush_register_defaults",
            { 0x111, 0x110, 0x010 },
            true,
            true,
            true,
            true,
            "0x00438550 reads literal table 0x004E2E90 and emits registers 0x111/0x110/0x010",
        },
    };

    contract.MaterialDrawState.FunctionAddress = 0x003FAC2C;
    contract.MaterialDrawState.SubmitManagerVtableSlotOffset = 0x28;
    contract.MaterialDrawState.DrawRecordIndexPointerSlotOffset = 0x00;
    contract.MaterialDrawState.DrawRecordTablePointerSlotOffset = 0x04;
    contract.MaterialDrawState.DrawRecordTableBasePointerOffset = 0x00;
    contract.MaterialDrawState.DrawRecordOffsetTablePointerOffset = 0x04;
    contract.MaterialDrawState.DrawRecordOffsetTableEntryStrideBytes = 2;
    contract.MaterialDrawState.DrawRecordOffsetTableEntriesAreU16 = true;
    contract.MaterialDrawState.RuntimeMaterialTablePointerSlotOffset = 0x08;
    contract.MaterialDrawState.RuntimeMaterialTableLaneBasePointerOffset = 0x0C;
    contract.MaterialDrawState.RuntimeMaterialLaneIndexByteOffset = 0x02;
    contract.MaterialDrawState.RenderStateBitmaskOffset = 0x478;
    contract.MaterialDrawState.RenderStateBitmaskBaseValue = 0x02;
    contract.MaterialDrawState.SourceMaterialByte0Offset = 0x00;
    contract.MaterialDrawState.SourceMaterialByte0Bit = 0x400;
    contract.MaterialDrawState.SourceMaterialByte1Offset = 0x01;
    contract.MaterialDrawState.SourceMaterialByte1Bit = 0x200;
    contract.MaterialDrawState.DrawRecordFlagHalfwordOffset = 0x0A;
    contract.MaterialDrawState.DrawRecordFlagSourceBits = { 0x04, 0x08, 0x10, 0x20 };
    contract.MaterialDrawState.DrawRecordFlagDestinationBits = { 0x20, 0x40, 0x80, 0x100 };
    contract.MaterialDrawState.SourceColorVectorWordCount = 6;
    contract.MaterialDrawState.SourceColorVectorWordOffsets = { 0x28, 0x44, 0x60, 0x7C, 0x98, 0xB4 };
    contract.MaterialDrawState.ColorVectorUploadHelperAddress = 0x003142DC;
    contract.MaterialDrawState.ColorVectorVshUniformIndex = 0x5A;
    contract.MaterialDrawState.ColorVectorVshUniformWordCount = 2;
    contract.MaterialDrawState.LightingEnableUploadHelperAddress = 0x003142F0;
    contract.MaterialDrawState.LightingEnableVshUniformIndex = 0x5C;
    contract.MaterialDrawState.LightingEnableVshUniformWordCount = 1;
    contract.MaterialDrawState.SignedHalfwordFloatSeedHelperAddress = 0x00314308;
    contract.MaterialDrawState.SignedHalfwordSourceDrawRecordOffset = 0x104;
    contract.MaterialDrawState.SignedHalfwordDestinationRenderStateOffset = 0x58;
    contract.MaterialDrawState.FinalPicaRegisterPackingResolved = false;

    contract.Runtime28CParticleBatch.CallbackAddress = contract.SubmitManager.Runtime28CSubmitCallbackAddress;
    contract.Runtime28CParticleBatch.CallbackEndAddress = 0x003FCB1F;
    contract.Runtime28CParticleBatch.RuntimeFlagsOffset = contract.SubmitManager.RuntimeFlagsOffset;
    contract.Runtime28CParticleBatch.RuntimeDescriptorPointerOffset = contract.RuntimeSourceVector.DescriptorPointerOffset;
    contract.Runtime28CParticleBatch.RuntimeTransformBlockOffset = contract.RuntimeSourceVector.DynamicTransformBlockOffset;
    contract.Runtime28CParticleBatch.RuntimeAverageDepthOffset = contract.PacketPrep.PreparedIntensityOffset;
    contract.Runtime28CParticleBatch.RuntimePositionArrayPointerOffset = 0x1E4;
    contract.Runtime28CParticleBatch.RuntimeMatrixArrayPointerOffset = 0x1E8;
    contract.Runtime28CParticleBatch.RuntimeColorArrayPointerOffset = 0x1F0;
    contract.Runtime28CParticleBatch.RuntimeTexcoordArrayPointerOffset = 0x1F4;
    contract.Runtime28CParticleBatch.RuntimeBatchCountOffset = 0x1FC;
    contract.Runtime28CParticleBatch.RuntimeBatchCapacityOffset = 0x1F8;
    contract.Runtime28CParticleBatch.RuntimeLocalVectorArrayPointerOffset = 0x1EC;
    contract.Runtime28CParticleBatch.RuntimeAveragePositionBaseOffset = 0x280;
    contract.Runtime28CParticleBatch.RuntimeAveragePositionComponentCount = 3;
    contract.Runtime28CParticleBatch.DescriptorFlagsOffset = contract.DescriptorMaterialization.DescriptorFlagsOffset;
    contract.Runtime28CParticleBatch.DescriptorTypeOffset = contract.RuntimeSourceVector.DescriptorTypeOffset;
    contract.Runtime28CParticleBatch.DescriptorTypeDirectMatrixValue =
        contract.RuntimeSourceVector.DescriptorTypeDirectMatrixValue;
    contract.Runtime28CParticleBatch.DescriptorTypeBillboardMatrixValue =
        contract.RuntimeSourceVector.DescriptorTypeDerivedMatrixValue;
    contract.Runtime28CParticleBatch.MatrixArrayRecordStrideBytes = 0x30;
    contract.Runtime28CParticleBatch.PositionRecordStrideBytes = 0x0C;
    contract.Runtime28CParticleBatch.LocalVectorRecordStrideBytes = 0x0C;
    contract.Runtime28CParticleBatch.ColorRecordStrideBytes = 0x10;
    contract.Runtime28CParticleBatch.TexcoordRecordStrideBytes = 0x08;
    contract.Runtime28CParticleBatch.QuadVertexCount = 4;
    contract.Runtime28CParticleBatch.DrawCountPerVisibleBatch = 6;
    contract.Runtime28CParticleBatch.DrawCountSetterAddress = contract.SubmitManager.RuntimeDrawCountSetterAddress;
    contract.Runtime28CParticleBatch.OutputPositionBufferResolverAddress = 0x00333270;
    contract.Runtime28CParticleBatch.OutputColorBufferResolverAddress = 0x003331EC;
    contract.Runtime28CParticleBatch.OutputTexcoordBufferResolverAddress = 0x00333070;
    contract.Runtime28CParticleBatch.OptionalSecondaryPositionBufferResolverAddress = 0x00408C80;
    contract.Runtime28CParticleBatch.SkipSecondaryPositionRuntimeFlagMask = 0x00020000;
    contract.Runtime28CParticleBatch.SkipColorRuntimeFlagMask = 0x00040000;
    contract.Runtime28CParticleBatch.SkipTexcoordRuntimeFlagMask = 0x00080000;
    contract.Runtime28CParticleBatch.DescriptorSkipSecondaryPositionFlagMask = 0x80;
    contract.Runtime28CParticleBatch.ClearsBatchCountAfterEmit = true;
    contract.Runtime28CParticleBatch.ResolvesNonThunderSubmitCallsite = false;
    contract.Runtime28CParticleBatch.EnqueueWriterResolved = true;
    contract.Runtime28CParticleBatch.ObjectTransformReplicatesColorForAllQuadCorners = true;
    return contract;
}

std::vector<ZsiEmbeddedCmb> ParseZsiEmbeddedCmbsBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, source };
    if (!view.HasMagic(0, std::string_view("ZSI\x01", 4))) {
        throw std::runtime_error(source + ": expected ZSI magic");
    }

    std::vector<ZsiEmbeddedCmb> cmbs;
    for (size_t offset = 0; offset + 4 <= bytes.size(); ++offset) {
        if (!LooksLikeCmbAt(bytes, offset)) {
            continue;
        }
        const auto size = view.U32(offset + 4);
        auto model = ParseCmbModelBytes(bytes.subspan(offset, size),
                                        source + "!cmb[" + std::to_string(cmbs.size()) + "]@" +
                                            std::to_string(offset));
        cmbs.push_back({ static_cast<uint32_t>(cmbs.size()), static_cast<uint32_t>(offset), size, std::move(model) });
    }
    return cmbs;
}

std::vector<ZsiEmbeddedCmb> ParseZsiEmbeddedCmbsFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseZsiEmbeddedCmbsBytes(bytes, path.string());
}

CsabMetadata ParseCsabMetadataBytes(std::span<const uint8_t> bytes) {
    BinaryView view{ bytes, "<csab>" };
    if (!view.HasMagic(0, "csab")) {
        throw std::runtime_error("expected CSAB magic");
    }
    CsabMetadata metadata;
    metadata.DeclaredSize = view.U32(0x04);
    metadata.Version = view.U32(0x08);
    metadata.FrameCount = view.U32(kCsabFrameCountOffset);
    metadata.AnimatedBoneCount = view.U32(kCsabAnimatedBoneCountOffset);
    metadata.SkeletonBoneCount = view.U32(kCsabSkeletonBoneCountOffset);

    const auto boneTableEnd = kCsabBoneTableOffset + metadata.SkeletonBoneCount * 2;
    const auto nodeOffsetTableStart = Align4(boneTableEnd);
    const auto nodeOffsetTableEnd = nodeOffsetTableStart + metadata.AnimatedBoneCount * 4;
    view.Require(kCsabBoneTableOffset, metadata.SkeletonBoneCount * 2);
    view.Require(nodeOffsetTableStart, metadata.AnimatedBoneCount * 4);

    metadata.BoneToNodeIndices.reserve(metadata.SkeletonBoneCount);
    for (uint32_t index = 0; index < metadata.SkeletonBoneCount; ++index) {
        metadata.BoneToNodeIndices.push_back(view.U16(kCsabBoneTableOffset + index * 2));
    }
    metadata.NodeOffsets.reserve(metadata.AnimatedBoneCount);
    for (uint32_t index = 0; index < metadata.AnimatedBoneCount; ++index) {
        metadata.NodeOffsets.push_back(view.U32(nodeOffsetTableStart + index * 4));
    }
    view.Require(nodeOffsetTableEnd, 0);
    return metadata;
}

CsabMetadata ParseCsabMetadataFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseCsabMetadataBytes(bytes);
}

std::vector<Matrix4f> BuildCmbSkeletonWorldTransforms(const CmbSkeleton& skeleton) {
    return SkeletonWorldTransforms(skeleton);
}

bool RecomposeCsabPoseWorldTransforms(const CmbSkeleton& skeleton, CsabPose& pose) {
    if (pose.LocalTransforms.size() != skeleton.Bones.size()) {
        pose.Valid = false;
        pose.WorldTransforms.clear();
        return false;
    }
    pose.WorldTransforms = ComposeWorldTransforms(skeleton, pose.LocalTransforms);
    pose.Valid = pose.Valid && pose.WorldTransforms.size() == skeleton.Bones.size();
    return pose.Valid;
}

CsabPose SampleCsabPoseFrameBytes(std::span<const uint8_t> bytes, const CmbModel& targetModel,
                                  const CsabMetadata& metadata, float frame) {
    return SampleCsabPoseFrameBytes(bytes, targetModel, metadata, frame, {});
}

CsabPose SampleCsabPoseFrameBytes(std::span<const uint8_t> bytes, const CmbModel& targetModel,
                                  const CsabMetadata& metadata, float frame,
                                  const CsabPoseSamplingPolicy& policy) {
    BinaryView view{ bytes, "<csab>" };
    if (metadata.SkeletonBoneCount != targetModel.Skeleton.Bones.size()) {
        throw std::runtime_error("CSAB skeleton bone count does not match target CMB");
    }
    auto frameChannels = CsabChannelValuesByNode(view, metadata, frame);
    std::vector<Matrix4f> localTransforms;
    localTransforms.reserve(targetModel.Skeleton.Bones.size());
    uint32_t animatedBoneTransforms = 0;
    for (size_t boneIndex = 0; boneIndex < targetModel.Skeleton.Bones.size(); ++boneIndex) {
        const auto nodeIndex = metadata.BoneToNodeIndices[boneIndex];
        std::map<uint16_t, float> values;
        if (nodeIndex != 0xFFFF) {
            ++animatedBoneTransforms;
            if (auto it = frameChannels.ValuesByNode.find(nodeIndex); it != frameChannels.ValuesByNode.end()) {
                values = it->second;
            }
        }
        const uint8_t channelMask = static_cast<int32_t>(boneIndex) == policy.SpecialBone
                                        ? policy.SpecialBoneChannelMask
                                        : policy.DefaultChannelMask;
        const auto poseBone = PoseBoneFromValues(targetModel.Skeleton.Bones[boneIndex], values, channelMask);
        localTransforms.push_back(BoneLocalTransform(poseBone));
    }

    CsabPose pose;
    pose.Frame = static_cast<uint32_t>(std::max(0.0f, frame));
    pose.LocalTransforms = localTransforms;
    pose.WorldTransforms = ComposeWorldTransforms(targetModel.Skeleton, localTransforms);
    pose.SampledChannelValueCount = frameChannels.SampledChannelValueCount;
    pose.FiniteChannelValueCount = frameChannels.FiniteChannelValueCount;
    pose.NonF32ChannelBlockCount = frameChannels.NonF32ChannelBlockCount;
    pose.AnimatedBoneTransformCount = animatedBoneTransforms;
    pose.Valid = pose.WorldTransforms.size() == targetModel.Skeleton.Bones.size() &&
                 pose.SampledChannelValueCount == pose.FiniteChannelValueCount;
    return pose;
}

CsabPose SampleCsabPoseFrameBytes(std::span<const uint8_t> bytes, const CmbModel& targetModel, float frame) {
    return SampleCsabPoseFrameBytes(bytes, targetModel, ParseCsabMetadataBytes(bytes), frame);
}

CsabPose SampleCsabPoseFrameFile(const std::filesystem::path& path, const CmbModel& targetModel, float frame) {
    auto bytes = ReadFileBytes(path);
    return SampleCsabPoseFrameBytes(bytes, targetModel, frame);
}

CsabPose MorphCsabPoseNative(const CmbSkeleton& skeleton, const CsabPose& currentPose,
                             const CsabPose& morphPose, float morphWeight) {
    CsabPose out = currentPose;
    const size_t boneCount = skeleton.Bones.size();
    if (currentPose.LocalTransforms.size() != boneCount || morphPose.LocalTransforms.size() != boneCount) {
        out.Valid = false;
        out.LocalTransforms.clear();
        out.WorldTransforms.clear();
        return out;
    }
    if (morphWeight <= 0.0f) {
        return out;
    }

    out.LocalTransforms.clear();
    out.LocalTransforms.reserve(boneCount);
    if (morphWeight >= 1.0f) {
        out.LocalTransforms = morphPose.LocalTransforms;
    } else {
        for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            out.LocalTransforms.push_back(MorphSkelAnimeLocalTransform(
                currentPose.LocalTransforms[boneIndex], morphPose.LocalTransforms[boneIndex], morphWeight));
        }
    }
    out.WorldTransforms = ComposeWorldTransforms(skeleton, out.LocalTransforms);
    out.Valid = currentPose.Valid && morphPose.Valid && out.WorldTransforms.size() == boneCount;
    return out;
}

ZarArchive ParseZarArchiveBytes(std::span<const uint8_t> bytes, std::string source) {
    BinaryView view{ bytes, source };
    if (!view.HasMagic(0, std::string_view("ZAR\x01", 4))) {
        throw std::runtime_error(source + ": expected ZAR magic");
    }
    const auto archiveSize = view.U32(0x04);
    const auto typeCount = view.U16(0x08);
    const auto fileCount = view.U16(0x0A);
    const auto typeSection = view.U32(0x0C);
    const auto metaSection = view.U32(0x10);
    const auto dataSection = view.U32(0x14);
    if (archiveSize > bytes.size()) {
        throw std::runtime_error(source + ": ZAR declared size exceeds file length");
    }

    std::vector<std::string> names(fileCount);
    std::vector<uint32_t> sizes(fileCount);
    for (uint16_t index = 0; index < fileCount; ++index) {
        const auto entry = metaSection + index * 8;
        sizes[index] = view.U32(entry);
        names[index] = ReadCString(view, view.U32(entry + 4));
    }

    std::vector<std::string> typeForFile(fileCount, "unknown");
    std::vector<uint32_t> typeLocalIndexForFile(fileCount, 0xFFFFFFFF);
    for (uint16_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
        const auto entry = typeSection + typeIndex * 0x10;
        const auto typedCount = view.U32(entry);
        const auto listOffset = view.U32(entry + 4);
        const auto typeName = ReadCString(view, view.U32(entry + 8));
        for (uint32_t typedFileIndex = 0; typedFileIndex < typedCount; ++typedFileIndex) {
            const auto fileIndex = view.U32(listOffset + typedFileIndex * 4);
            if (fileIndex >= fileCount) {
                throw std::runtime_error(source + ": ZAR type references invalid file index");
            }
            typeForFile[fileIndex] = typeName;
            typeLocalIndexForFile[fileIndex] = typedFileIndex;
        }
    }

    ZarArchive archive;
    archive.Source = source;
    archive.Files.reserve(fileCount);
    for (uint16_t index = 0; index < fileCount; ++index) {
        const auto dataOffset = view.U32(dataSection + index * 4);
        view.Require(dataOffset, sizes[index]);
        archive.Files.push_back(
            { index, names[index], typeForFile[index], typeLocalIndexForFile[index], dataOffset, sizes[index] });
    }
    return archive;
}

ZarArchive ParseZarArchiveFile(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    return ParseZarArchiveBytes(bytes, path.string());
}

std::vector<ZarFileEntry> ResolveZarTypeLocalEntries(const ZarArchive& archive, std::string_view typeName,
                                                     std::span<const uint32_t> typeLocalIndices) {
    if (typeName.empty()) {
        throw std::runtime_error(archive.Source + ": ZAR type-local lookup requires a type name");
    }

    std::vector<ZarFileEntry> resolved;
    resolved.reserve(typeLocalIndices.size());
    for (const auto typeLocalIndex : typeLocalIndices) {
        const ZarFileEntry* match = nullptr;
        for (const auto& file : archive.Files) {
            if (file.TypeName != typeName || file.TypeLocalIndex != typeLocalIndex) {
                continue;
            }
            if (match != nullptr) {
                throw std::runtime_error(archive.Source + ": duplicate ZAR " + std::string(typeName) +
                                         " type-local entry " + std::to_string(typeLocalIndex));
            }
            match = &file;
        }
        if (match == nullptr) {
            throw std::runtime_error(archive.Source + ": missing ZAR " + std::string(typeName) +
                                     " type-local entry " + std::to_string(typeLocalIndex));
        }
        resolved.push_back(*match);
    }
    return resolved;
}

NativeObjectDescriptorCmbSelectionContract BuildNativeGanon2DescriptorCmbSelectionContract(
    const ZarArchive& archive) {
    NativeObjectDescriptorCmbSelectionContract contract;
    contract.SourceKind = "oot3d_codebin_object_descriptor_cmb_selection";
    contract.SelectorFunctionAddress = 0x0036A924;
    contract.ObjectGetIndexFunctionAddress = 0x00363C10;
    contract.CmbResolverAddress = 0x00358EF8;
    contract.SourceId = 0x0153;
    contract.SourceSymbol = "OBJECT_GANON2";
    contract.ArchivePath = "rom:/actor/zelda_ganon2.zar";
    contract.DescriptorIds = { 5, 6, 7 };
    contract.ResourceContextObjectTableBaseOffset = 0x3A58;
    contract.ResourceContextEntryStrideBytes = 0x80;
    contract.ResourceContextAvailabilityPointerOffset = 0x3A64;
    contract.ResourceContextPayloadOffset = 0x3A6C;
    contract.DescriptorStoreOffset = 0x350;
    contract.DescriptorIdsAreCmbTypeLocalIndices = true;
    contract.DescriptorIdsAreGlobalFileIndices = false;
    contract.DescriptorIdsAreZsiIndices = false;
    contract.ActiveDemoBinding = false;

    const auto resolved =
        ResolveZarTypeLocalEntries(archive, "cmb",
                                   std::span<const uint32_t>(contract.DescriptorIds.data(),
                                                             contract.DescriptorIds.size()));
    for (size_t index = 0; index < resolved.size(); ++index) {
        contract.ResolvedCmbs.push_back({ contract.DescriptorIds[index], resolved[index] });
    }
    return contract;
}

std::vector<uint8_t> ExtractZarFileBytes(const std::filesystem::path& archivePath, std::string_view fileName) {
    auto bytes = ReadFileBytes(archivePath);
    auto archive = ParseZarArchiveBytes(bytes, archivePath.string());
    const auto normalizedTarget = NormalizeArchiveName(fileName);
    for (const auto& file : archive.Files) {
        if (NormalizeArchiveName(file.Name) == normalizedTarget) {
            return std::vector<uint8_t>(bytes.begin() + file.Offset, bytes.begin() + file.Offset + file.Size);
        }
    }
    throw std::runtime_error(archivePath.string() + ": ZAR file entry not found: " + std::string(fileName));
}

} // namespace ThreeDsRecomp::Oot3d
