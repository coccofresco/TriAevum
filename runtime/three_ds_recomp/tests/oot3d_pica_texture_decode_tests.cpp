#include "fast/oot3d/pica_texture_decode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

struct TextureCase {
    uint8_t Format;
    uint32_t EncodedSize;
    std::vector<uint8_t> FirstBytes;
    std::array<uint8_t, 4> Expected;
};

class Oot3dPicaTextureDecodeTest
    : public testing::TestWithParam<TextureCase> {};

TEST_P(Oot3dPicaTextureDecodeTest, DecodesNativeFirstTexel) {
    const auto& test = GetParam();
    std::vector<uint8_t> encoded(test.EncodedSize);
    std::copy(test.FirstBytes.begin(), test.FirstBytes.end(),
              encoded.begin());
    std::vector<uint8_t> rgba8;
    std::string error;

    ASSERT_TRUE(Fast::Oot3d::DecodePicaTextureRgba8(
        test.Format, 1U, 1U, encoded, rgba8, &error))
        << error;
    ASSERT_EQ(rgba8.size(), 4U);
    EXPECT_EQ(
        (std::array<uint8_t, 4>{
            rgba8[0], rgba8[1], rgba8[2], rgba8[3]}),
        test.Expected);
}

INSTANTIATE_TEST_SUITE_P(
    NativeFormats, Oot3dPicaTextureDecodeTest,
    testing::Values(
        TextureCase{0U, 256U, {40U, 30U, 20U, 10U},
                    {10U, 20U, 30U, 40U}},
        TextureCase{1U, 192U, {30U, 20U, 10U},
                    {10U, 20U, 30U, 255U}},
        TextureCase{2U, 128U, {0x01U, 0xF8U},
                    {255U, 0U, 0U, 255U}},
        TextureCase{3U, 128U, {0x00U, 0xF8U},
                    {255U, 0U, 0U, 255U}},
        TextureCase{4U, 128U, {0x0FU, 0xF0U},
                    {255U, 0U, 0U, 255U}},
        TextureCase{5U, 128U, {64U, 96U},
                    {96U, 96U, 96U, 64U}},
        TextureCase{6U, 128U, {77U, 33U},
                    {33U, 77U, 0U, 255U}},
        TextureCase{7U, 64U, {55U}, {55U, 55U, 55U, 255U}},
        TextureCase{8U, 64U, {64U}, {0U, 0U, 0U, 64U}},
        TextureCase{9U, 64U, {0xA5U}, {170U, 170U, 170U, 85U}},
        TextureCase{10U, 32U, {0x0AU},
                    {170U, 170U, 170U, 255U}},
        TextureCase{11U, 32U, {0x0AU}, {0U, 0U, 0U, 170U}},
        TextureCase{12U, 32U, {}, {2U, 2U, 2U, 255U}},
        TextureCase{13U, 64U, {}, {2U, 2U, 2U, 0U}}));

TEST(Oot3dPicaTextureDecode, RejectsTruncatedAndUnknownFormats) {
    std::vector<uint8_t> rgba8;
    std::string error;
    EXPECT_FALSE(Fast::Oot3d::DecodePicaTextureRgba8(
        0U, 1U, 1U, {}, rgba8, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(Fast::Oot3d::DecodePicaTextureRgba8(
        14U, 1U, 1U, {}, rgba8, &error));
    EXPECT_FALSE(error.empty());
}

} // namespace
