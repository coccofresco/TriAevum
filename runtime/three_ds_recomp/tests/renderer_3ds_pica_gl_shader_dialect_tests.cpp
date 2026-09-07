#include "fast/renderer3ds/pica_gl_shader_dialect.h"

#include <gtest/gtest.h>

#include <string>

namespace Fast::Renderer3ds {
namespace {

TEST(PicaGlShaderDialect, RewritesCanonicalDescriptorAndDepthConventions) {
    const std::string canonical =
        "#version 450\n"
        "layout(location=0) in vec4 pica_input;\n"
        "layout(set=0,binding=0,std140) uniform PicaVertexUniforms { "
        "vec4 f[96]; } uniforms;\n"
        "void main() {\n"
        "vec4 pica_position = pica_input;\n"
        "gl_Position = vec4(pica_position.x, pica_position.y, "
        "-pica_position.z, pica_position.w);\n"
        "}\n";
    std::string translated;
    std::string error;

    ASSERT_TRUE(TranslatePicaShaderToOpenGl43(
        canonical, translated, &error)) << error;
    EXPECT_TRUE(translated.starts_with("#version 430\n"));
    EXPECT_EQ(translated.find("set="), std::string::npos);
    EXPECT_NE(translated.find("layout(binding=0, std140)"),
              std::string::npos);
    EXPECT_NE(translated.find(
                  "-2.0 * pica_position.z - pica_position.w"),
              std::string::npos);
}

TEST(PicaGlShaderDialect, AcceptsCombinedSamplersAndSpacedSetZero) {
    const std::string canonical =
        "#version 450\n"
        "layout(set = 0, binding = 2) uniform sampler2D pica_texture1;\n"
        "layout(location = 0) out vec4 pica_color;\n"
        "void main() { pica_color = texture(pica_texture1, vec2(0)); }\n";
    std::string translated;
    std::string error;

    ASSERT_TRUE(TranslatePicaShaderToOpenGl43(
        canonical, translated, &error)) << error;
    EXPECT_NE(translated.find("layout(binding = 2) uniform sampler2D"),
              std::string::npos);
}

TEST(PicaGlShaderDialect, RejectsNonzeroSetsAndVulkanOnlyResources) {
    std::string translated;
    std::string error;
    EXPECT_FALSE(TranslatePicaShaderToOpenGl43(
        "#version 450\nlayout(set=1,binding=0) uniform sampler2D t;\n",
        translated, &error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(TranslatePicaShaderToOpenGl43(
        "#version 450\nlayout(push_constant) uniform State { int x; } s;\n",
        translated, &error));
    EXPECT_FALSE(TranslatePicaShaderToOpenGl43(
        "#version 450\nlayout(set=0,binding=0) uniform texture2D t;\n",
        translated, &error));
}

} // namespace
} // namespace Fast::Renderer3ds
