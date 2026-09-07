#include "renderer_3ds_pica_capture_conformance.h"

#include <gtest/gtest.h>

#include <sstream>

namespace Fast::Renderer3ds::Diagnostics {
namespace {

nlohmann::json RegisterGroup(uint32_t first, uint32_t last) {
    return { { "first", first }, { "last", last }, { "values", std::vector<uint32_t>(last - first + 1U, 0U) } };
}

void SetRegister(nlohmann::json& snapshot, std::string_view group, uint32_t index, uint32_t value) {
    const uint32_t first = snapshot.at(group).at("first").get<uint32_t>();
    snapshot.at(group).at("values").at(index - first) = value;
}

nlohmann::json RepresentativeDraw() {
    nlohmann::json snapshot = {
        { "rasterizer", RegisterGroup(0x040U, 0x06FU) },  { "texturing", RegisterGroup(0x080U, 0x0FDU) },
        { "framebuffer", RegisterGroup(0x100U, 0x130U) }, { "lighting", RegisterGroup(0x140U, 0x1D9U) },
        { "pipeline", RegisterGroup(0x200U, 0x25FU) },
    };
    SetRegister(snapshot, "rasterizer", 0x040U, 2U);
    SetRegister(snapshot, "texturing", 0x08FU, 1U);
    SetRegister(snapshot, "texturing", 0x0E0U, 5U);
    SetRegister(snapshot, "framebuffer", 0x100U, 1U << 8U);
    SetRegister(snapshot, "framebuffer", 0x101U, 0x10000000U);
    SetRegister(snapshot, "framebuffer", 0x104U, 1U);
    SetRegister(snapshot, "framebuffer", 0x107U, 0x00001F01U);
    SetRegister(snapshot, "lighting", 0x1C2U, 2U);
    for (const uint32_t base : { 0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U }) {
        SetRegister(snapshot, "texturing", base, 0x00F00E03U);
        SetRegister(snapshot, "texturing", base + 2U, 0x00010001U);
    }
    return {
        { "event", "draw_begin" },
        { "mode", "arrays" },
        { "is_indexed", true },
        { "triangle_topology", 3U },
        { "use_gs", true },
        { "textures",
          { { { "index", 0U },
              { "enabled", true },
              { "format", 13U },
              { "type", 0U },
              { "width", 256U },
              { "height", 64U } },
            { { "index", 1U }, { "enabled", false } },
            { { "index", 2U }, { "enabled", false } } } },
        { "shader_identity",
          { { "format", "pica.capture.identity.v1" },
            { "vertex", { { "program_hash", "vp" }, { "swizzle_hash", "vs" }, { "entry_point", 0U } } },
            { "geometry",
              { { "enabled", true }, { "program_hash", "gp" }, { "swizzle_hash", "gs" }, { "entry_point", 1U } } },
            { "fragment_config_hash", "fp" } } },
        { "register_snapshot", std::move(snapshot) },
    };
}

std::string EncodeCapture(const nlohmann::json& draw) {
    std::ostringstream stream;
    for (const auto& event :
         { nlohmann::json{ { "event", "capture_begin" } }, nlohmann::json{ { "event", "command_list_begin" } },
           nlohmann::json{ { "event", "register_write" }, { "id", 0x2CBU } },
           nlohmann::json{ { "event", "register_write" }, { "id", 0x2CCU } }, draw,
           nlohmann::json{ { "event", "draw_end" } }, nlohmann::json{ { "event", "frame_boundary" } },
           nlohmann::json{ { "event", "capture_end" } } }) {
        stream << event.dump() << '\n';
    }
    return stream.str();
}

TEST(Renderer3dsPicaCaptureConformance, AcceptsRepresentativeTitleNeutralSurface) {
    std::istringstream input(EncodeCapture(RepresentativeDraw()));
    PicaCaptureConformanceReport report;
    std::string error;
    ASSERT_TRUE(AnalyzePicaCaptureStream(input, report, &error)) << error;
    ASSERT_TRUE(report.Conformant()) << report.ToJson("synthetic").dump(2);
    EXPECT_EQ(report.DecodedDrawCount, 1U);
    EXPECT_EQ(report.ContractRepresentableDrawCount, 1U);
    EXPECT_EQ(report.GeometryShaderTopologyDrawCount, 1U);
    EXPECT_EQ(report.ExplicitGeometryShaderDrawCount, 1U);
    EXPECT_EQ(report.FragmentLightingDrawCount, 1U);
    EXPECT_EQ(report.FogDrawCount, 1U);
    EXPECT_EQ(report.CompressedTextureCount, 1U);
    EXPECT_EQ(report.VertexProgramUploadCount, 1U);
    EXPECT_EQ(report.VertexProgramUploadWordCount, 1U);
    EXPECT_EQ(report.TevStateIdentities.size(), 1U);
    const auto json = report.ToJson("synthetic");
    EXPECT_FALSE(json.at("privacy").at("contains_addresses").get<bool>());
    EXPECT_EQ(json.dump().find("program_hash"), std::string::npos);
}

TEST(Renderer3dsPicaCaptureConformance, RejectsUnknownNativeStateAndBrokenOrdering) {
    auto draw = RepresentativeDraw();
    draw["triangle_topology"] = 8U;
    draw["textures"][0]["format"] = 31U;
    std::ostringstream stream;
    stream << draw.dump() << '\n';
    stream << nlohmann::json{ { "event", "draw_end" } }.dump() << '\n';
    std::istringstream input(stream.str());
    PicaCaptureConformanceReport report;
    ASSERT_TRUE(AnalyzePicaCaptureStream(input, report));
    EXPECT_FALSE(report.Conformant());
    EXPECT_EQ(report.IssueCounts.at("draw_outside_command_list"), 1U);
    EXPECT_EQ(report.IssueCounts.at("unknown_topology"), 1U);
    EXPECT_EQ(report.IssueCounts.at("unknown_texture_format"), 1U);
}

} // namespace
} // namespace Fast::Renderer3ds::Diagnostics
