#include "oot3d_native_pica_semantic_trace.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "oot3d_native_pica_semantic_trace_tests: " << message
                  << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    const auto path = std::filesystem::temp_directory_path() /
                      "oot3d_native_pica_semantic_trace_test.jsonl";
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    {
        Oot3dPicaSemanticTraceWriter writer(path);
        writer.BeginSession("nri", "native30_interpolated", "topscreen");

        Oot3dPicaMemoryFillSubmission fill;
        fill.CompletionId = 2U;
        fill.StartPhysicalAddress = 0x18000000U;
        fill.EndPhysicalAddress = 0x18001000U;
        writer.RecordMemoryFill(3U, 2U, fill);

        Oot3dPicaDrawSubmission draw;
        draw.Id = 4U;
        draw.Packet.VertexShader.ProgramWordCount = 1U;
        draw.Packet.VertexShader.Program[0] = 0x12345678U;
        draw.Packet.VertexShader.SwizzleWordCount = 1U;
        draw.Packet.VertexShader.Swizzles[0] = 0x87654321U;
        draw.Packet.Registers[0x08FU] = 1U;
        draw.Packet.Registers[0x1C4U] = 0xFF7FFFFFU;
        draw.Packet.Registers[0x149U] = 1U;
        draw.State.VertexInput.VertexCount = 3U;
        draw.State.Framebuffer.Width = 240U;
        draw.State.Framebuffer.Height = 400U;
        Oot3dPicaVulkanDrawPlan plan;
        plan.SubmissionId = draw.Id;
        plan.State = draw.State;
        plan.VertexCount = 3U;
        plan.VertexShader.StateKey = 5U;
        plan.FragmentShader.StateKey = 6U;
        plan.VertexShader.Source = "#version 450\nvoid main(){}";
        plan.FragmentShader.Source = "#version 450\nvoid main(){}";
        auto temporalProgram = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        temporalProgram->Hooks.SchemaVersion = Oot3d::Renderer::kPicaShaderHookSchemaVersion;
        temporalProgram->Hooks.SourceSize = plan.VertexShader.Source.size();
        temporalProgram->Hooks.Offsets.fill(0U);
        temporalProgram->Hooks.Semantics = Oot3d::Renderer::PicaVertexShaderSemantic::TransformProgram |
                                           Oot3d::Renderer::PicaVertexShaderSemantic::SkeletonProgram;
        temporalProgram->Hooks.Transform = {
            Oot3d::Renderer::PicaVertexTransformOperation::ModelViewProjection3x4,
            0U, 4U, 4U, 3U, 20U, 3U, 0U, 1U,
        };
        temporalProgram->Hooks.Skeleton = {
            Oot3d::Renderer::PicaVertexSkeletonOperation::MatrixPalette3x4,
            2U, 3U, 20U, 3U, 6U, 7U, 4U,
        };
        plan.VertexShader.TemporalProgram = temporalProgram;
        plan.VertexShader.Uniforms.BooleanMask = (1U << 2U) | (1U << 3U);
        writer.RecordDraw(3U, 2U, draw, plan, false, false, false);

        Oot3dPicaDisplayTransferSubmission transfer;
        transfer.CompletionId = 7U;
        transfer.Transfer.InputAddress = 0x18000000U;
        transfer.Transfer.OutputAddress = 0x1F000000U;
        writer.RecordDisplayTransfer(3U, 2U, transfer, true, false);
        writer.RecordPresentationSelection(3U, 2U, false,
                                           transfer.Transfer.OutputAddress,
                                           std::nullopt, &transfer);
        writer.RecordFrameBoundary(3U, 2U, true);
        writer.Finish();
    }

    std::ifstream stream(path);
    Require(static_cast<bool>(stream), "trace output was not created");
    std::vector<nlohmann::json> events;
    for (std::string line; std::getline(stream, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    Require(events.size() == 9U, "trace event count is wrong");
    Require(events.front().at("event") == "session_begin" &&
                events.back().at("event") == "session_end",
            "trace session boundaries are missing");
    for (size_t index = 0; index < events.size(); ++index) {
        Require(events[index].at("format").get<std::string>() ==
                    kOot3dPicaSemanticTraceFormat,
                "trace format tag is missing");
        Require(events[index].at("event_id").get<uint64_t>() == index + 1U,
                "trace event IDs are not monotonic");
    }
    const auto drawEvent = std::find_if(
        events.begin(), events.end(), [](const auto& event) {
            return event.at("event") == "draw";
        });
    Require(drawEvent != events.end() && drawEvent->contains("identity") &&
                drawEvent->at("identity").contains("pipeline") &&
                drawEvent->contains("fragment_features") &&
                drawEvent->at("fragment_features")
                    .at("fully_supported")
                    .get<bool>() &&
                drawEvent->at("fragment_features")
                        .at("fragment_lighting_layout")
                        .at("valid")
                        .get<bool>() &&
                drawEvent->at("fragment_features")
                        .at("fragment_lighting_layout")
                        .at("active_light_count") == 1U &&
                drawEvent->at("fragment_features")
                        .at("fragment_lighting_layout")
                        .at("lights")[0]
                        .at("directional")
                        .get<bool>(),
            "draw canonical identity is missing");
    const auto sourceCount = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return event.at("event") == "shader_source";
        });
    Require(sourceCount == 2U,
            "deduplicated shader-source dictionary is missing");
    const auto vertexSource = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.at("event") == "shader_source" && event.at("stage") == "vertex";
    });
    Require(vertexSource != events.end() && vertexSource->at("main_offset") == 0U &&
                vertexSource->at("program_words") == nlohmann::json::array({ 0x12345678U }) &&
                vertexSource->at("swizzle_words") == nlohmann::json::array({ 0x87654321U }) &&
                vertexSource->at("vertex_program_layout").at("skeleton").at("available").get<bool>() &&
                !vertexSource->at("vertex_program_layout").at("skeleton").contains("active"),
            "vertex bytecode dictionary is missing");
    Require(drawEvent->at("vertex_program_state").at("skeleton").at("active").get<bool>() &&
                drawEvent->at("vertex_program_state").at("skeleton").at("multiple_influences").get<bool>() &&
                drawEvent->at("vertex_program_state").at("skeleton").at("influence_count") == 4U,
            "draw-local vertex program state is missing");
    stream.close();
    std::filesystem::remove(path, removeError);
    std::cout << "oot3d_native_pica_semantic_trace_tests: ok\n";
    return 0;
}
