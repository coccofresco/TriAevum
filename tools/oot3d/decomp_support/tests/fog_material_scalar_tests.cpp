#include <gtest/gtest.h>

#include <array>
#include <cstring>

extern "C" {
#include "oot3d/fog_material_scalar.h"
}

TEST(Oot3dFogMaterialScalar, SaturatesNativePicaCurveOutsideFogRange) {
    constexpr float zNear = 7.0f;
    constexpr float zFar = 32000.0f;
    const float glA = (zFar + zNear) / (zNear - zFar);
    const float glB = (2.0f * zFar * zNear) / (zNear - zFar);
    std::array<float, OOT3D_FOG_VIEW_WORD_COUNT> projection = {
        1.90295839f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.17159724f, 0.0f, 0.0f,
        0.0f, 0.0f, glA, glB,
        0.0f, 0.0f, -1.0f, 0.0f,
    };
    for (size_t column = 0; column < 4; ++column) {
        projection[2 * 4 + column] =
            -0.5f * (projection[2 * 4 + column] + projection[3 * 4 + column]);
    }

    Oot3dFogMaterialRuntimeLayout runtime{};
    Oot3dFogMaterialTemplateDecoded fog = gOot3dFogMaterialTemplateDecoded_004FA8B8;
    uint32_t generation = 0;
    fog.nearZ = 67;
    fog.farZ = 41381;
    fog.rebuildDisabled = 0;
    fog.curveMode = 0;
    Oot3d_FogRuntimeInitFromTemplate(&runtime, OOT3D_FOG_MATERIAL_TEMPLATE_ADDR);
    Oot3d_FogCopyViewTermsToRuntime(&runtime, projection.data());

    ASSERT_TRUE(Oot3d_FogRuntimeApplyTemplateAndBuildLut(
        &runtime, &fog, &generation, nullptr, nullptr));
    EXPECT_EQ(runtime.packedPicaFogLut[0], 0x00FFE000u);
    EXPECT_EQ(runtime.packedPicaFogLut[113], 0x00FFE000u);
    EXPECT_EQ(runtime.packedPicaFogLut[114], 0x00FFFFFFu);
    EXPECT_EQ(runtime.packedPicaFogLut[127], 0x00FB19FAu);
}
