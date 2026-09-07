#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_process_image.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_shader_gen.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include "oot3d_a32_generated.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#ifdef OOT3D_NATIVE_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

namespace {

struct Arguments {
    std::filesystem::path Manifest;
    std::filesystem::path CodeBin;
    std::filesystem::path Output;
    std::filesystem::path SaveDataDirectory;
    uint32_t VblankCount = 2;
};

void PrintUsage() {
    std::cerr << "usage: oot3d_native_a32_boot_probe --manifest <process.json> "
                 "[--code-bin <code.bin>] [--output <report.json>] "
                 "[--save-data <directory>] [--vblanks <count>]\n";
}

std::optional<Arguments> ParseArguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (index + 1 >= argc) {
            return std::nullopt;
        }
        const std::filesystem::path value = argv[++index];
        if (option == "--manifest") {
            result.Manifest = value;
        } else if (option == "--code-bin") {
            result.CodeBin = value;
        } else if (option == "--output") {
            result.Output = value;
        } else if (option == "--save-data") {
            result.SaveDataDirectory = value;
        } else if (option == "--vblanks") {
            const auto count = std::stoul(value.string());
            if (count > 600U) {
                return std::nullopt;
            }
            result.VblankCount = static_cast<uint32_t>(count);
        } else {
            return std::nullopt;
        }
    }
    return result.Manifest.empty() ? std::nullopt
                                   : std::optional<Arguments>(result);
}

const char* RunKindName(Oot3dNativeGame::NativeA32ProcessRunKind kind) {
    switch (kind) {
    case Oot3dNativeGame::NativeA32ProcessRunKind::Yielded:
        return "yielded";
    case Oot3dNativeGame::NativeA32ProcessRunKind::Waiting:
        return "waiting";
    case Oot3dNativeGame::NativeA32ProcessRunKind::Terminated:
        return "terminated";
    case Oot3dNativeGame::NativeA32ProcessRunKind::Faulted:
        return "faulted";
    }
    return "unknown";
}

const char* ThreadStatusName(Oot3dNativeGame::NativeA32ThreadStatus status) {
    switch (status) {
    case Oot3dNativeGame::NativeA32ThreadStatus::Empty:
        return "empty";
    case Oot3dNativeGame::NativeA32ThreadStatus::Ready:
        return "ready";
    case Oot3dNativeGame::NativeA32ThreadStatus::Running:
        return "running";
    case Oot3dNativeGame::NativeA32ThreadStatus::Waiting:
        return "waiting";
    case Oot3dNativeGame::NativeA32ThreadStatus::Terminated:
        return "terminated";
    case Oot3dNativeGame::NativeA32ThreadStatus::Faulted:
        return "faulted";
    }
    return "unknown";
}

Oot3dNativeGame::NativeA32ProcessRunResult RunUntilGuestWait(
    Oot3dNativeGame::NativeA32Process& process) {
    auto result = process.Run();
    while (result.Kind ==
           Oot3dNativeGame::NativeA32ProcessRunKind::Yielded) {
        result = process.Run();
    }
    return result;
}

void AdvanceGuestClockTo(
    Oot3dNativeGame::NativeA32Process& process,
    Oot3dNativeGame::NativeA32CtrHostServices& hostServices,
    Oot3dNativeGame::NativeA32ProcessRunResult& result,
    uint64_t deadline) {
    if (hostServices.SystemTicks() > deadline) {
        throw std::runtime_error(
            "native CTR clock advanced beyond the probe deadline");
    }
    while (result.Kind !=
           Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
        const auto wakeTick = hostServices.NextSleepWakeTick();
        if (!wakeTick.has_value() || *wakeTick > deadline) {
            break;
        }
        hostServices.AdvanceSystemTicks(*wakeTick - hostServices.SystemTicks());
        result = RunUntilGuestWait(process);
        if (result.Kind ==
            Oot3dNativeGame::NativeA32ProcessRunKind::Faulted) {
            return;
        }
    }
    if (hostServices.SystemTicks() < deadline) {
        hostServices.AdvanceSystemTicks(deadline - hostServices.SystemTicks());
    }
}

void WriteReport(const nlohmann::json& report,
                 const std::filesystem::path& output) {
    if (!output.empty()) {
        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path());
        }
        std::ofstream stream(output);
        if (!stream) {
            throw std::runtime_error("cannot open boot probe output");
        }
        stream << report.dump(2) << '\n';
    }
    std::cout << report.dump(2) << '\n';
}

bool CompileVertexShader(const std::string& source, std::string& error) {
#ifdef OOT3D_NATIVE_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        source, shaderc_vertex_shader, "oot3d_native_pica.vert", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        error = result.GetErrorMessage();
        return false;
    }
    return true;
#else
    error = "shaderc is unavailable in this build";
    return false;
#endif
}

bool CompileFragmentShader(const std::string& source, std::string& error) {
#ifdef OOT3D_NATIVE_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        source, shaderc_fragment_shader, "oot3d_native_pica.frag", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        error = result.GetErrorMessage();
        return false;
    }
    return true;
#else
    error = "shaderc is unavailable in this build";
    return false;
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = ParseArguments(argc, argv);
        if (!arguments.has_value()) {
            PrintUsage();
            return 64;
        }

        std::string error;
        const auto manifest =
            Oot3dNativeGame::LoadNativeA32ProcessImageManifest(
                arguments->Manifest, &error);
        if (!manifest.has_value()) {
            throw std::runtime_error(error);
        }

        Oot3dNativeGame::Oot3dNativePicaFrontend picaFrontend;
        Oot3dNativeGame::NativeA32CtrHostConfig hostConfig{
            manifest->ResourceLimitValues,
            manifest->ResourceCurrentValues,
            manifest->LinearHeapBaseAddress,
            manifest->LinearHeapSize,
            manifest->HeapBaseAddress,
            manifest->HeapSize,
            manifest->RomFsImagePath,
            manifest->RomFsImageOffset,
            manifest->RomFsImageSize,
            &picaFrontend,
        };
        hostConfig.SaveDataDirectory =
            arguments->SaveDataDirectory.empty()
                ? arguments->Output.parent_path() /
                      "oot3d_native_savedata"
                : arguments->SaveDataDirectory;
        // The probe serializes every SVC boundary, unlike the product runtime
        // which only needs bounded diagnostic summaries.
        hostConfig.RetainFullSvcHistory = true;
        Oot3dNativeGame::NativeA32CtrHostServices hostServices(
            std::move(hostConfig));
        Oot3dNativeGame::NativeA32Process process(
            oot3d::recomp::GetA32GeneratedRegistry(), hostServices);
        if (!Oot3dNativeGame::MountNativeA32ProcessImage(
                process, *manifest, arguments->CodeBin, &error)) {
            throw std::runtime_error(error);
        }
        std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion>
            picaMemoryRegions{{0x20000000U,
                               manifest->LinearHeapBaseAddress,
                               manifest->LinearHeapSize}};
        for (const auto& region : manifest->SystemRegions) {
            if (region.Name == "ctr_vram") {
                picaMemoryRegions.push_back(
                    {0x18000000U, region.Address, region.MappedSize});
                break;
            }
        }
        Oot3dNativeGame::Oot3dNativePicaSubmissionQueue submissionQueue(
            Oot3dNativeGame::Oot3dPicaPhysicalMemoryView(
                process.Memory(), std::move(picaMemoryRegions)));
        picaFrontend.SetPacketSink(&submissionQueue);

        auto result = RunUntilGuestWait(process);
        uint32_t vblanksDelivered = 0;
        uint64_t refreshTickRemainder = 0;
        uint64_t nextVblankTick = 0;
        constexpr uint64_t kCtrArm11TicksPerSecond = 268111856ULL;
        constexpr uint64_t kDisplayRefreshRate = 60ULL;
        while (vblanksDelivered < arguments->VblankCount &&
               result.Kind ==
                   Oot3dNativeGame::NativeA32ProcessRunKind::Waiting) {
            refreshTickRemainder += kCtrArm11TicksPerSecond;
            nextVblankTick += refreshTickRemainder / kDisplayRefreshRate;
            refreshTickRemainder %= kDisplayRefreshRate;
            AdvanceGuestClockTo(process, hostServices, result,
                                nextVblankTick);
            if (result.Kind ==
                Oot3dNativeGame::NativeA32ProcessRunKind::Faulted) {
                break;
            }
            if (!hostServices.SignalVBlank(process.Memory())) {
                break;
            }
            ++vblanksDelivered;
            result = RunUntilGuestWait(process);
        }
        const auto& state = process.PrimaryThreadState();
        nlohmann::json registers = nlohmann::json::array();
        for (uint32_t value : state.r) {
            registers.push_back(value);
        }
        nlohmann::json threads = nlohmann::json::array();
        nlohmann::json ipcCommandBuffers = nlohmann::json::array();
        nlohmann::json ipcStaticBuffers = nlohmann::json::array();
        std::vector<uint32_t> memoryCandidates;
        for (uint32_t threadId = 0; threadId < process.ThreadCount();
             ++threadId) {
            const auto* threadState = process.ThreadState(threadId);
            if (threadState == nullptr) {
                continue;
            }
            nlohmann::json threadRegisters = nlohmann::json::array();
            for (uint32_t value : threadState->r) {
                threadRegisters.push_back(value);
                memoryCandidates.push_back(value);
            }
            nlohmann::json stackWords = nlohmann::json::array();
            const uint32_t stackAddress = threadState->r[13];
            for (uint32_t offset = 0; offset < 0x80U; offset += 4U) {
                uint32_t value = 0;
                if (!process.Memory().Read32(stackAddress + offset, &value)) {
                    break;
                }
                stackWords.push_back(value);
                memoryCandidates.push_back(value);
            }
            threads.push_back({
                {"id", threadId},
                {"status", ThreadStatusName(process.ThreadStatus(threadId))},
                {"priority", process.ThreadPriority(threadId).value_or(0)},
                {"registers", std::move(threadRegisters)},
                {"cpsr", threadState->cpsr},
                {"fpscr", threadState->fpscr},
                {"thread_pointer", threadState->thread_pointer},
                {"stack_address", stackAddress},
                {"stack_words", std::move(stackWords)},
            });
            const uint32_t commandBufferAddress =
                threadState->thread_pointer + 0x80U;
            nlohmann::json commandWords = nlohmann::json::array();
            for (uint32_t offset = 0; offset < 0x100U; offset += 4U) {
                uint32_t value = 0;
                if (!process.Memory().Read32(commandBufferAddress + offset,
                                             &value)) {
                    break;
                }
                commandWords.push_back(value);
            }
            ipcCommandBuffers.push_back({
                {"thread_id", threadId},
                {"address", commandBufferAddress},
                {"words", std::move(commandWords)},
            });
            const uint32_t staticBufferAddress =
                threadState->thread_pointer + 0x180U;
            nlohmann::json staticBufferWords = nlohmann::json::array();
            for (uint32_t offset = 0; offset < 0x80U; offset += 4U) {
                uint32_t value = 0;
                if (!process.Memory().Read32(staticBufferAddress + offset,
                                             &value)) {
                    break;
                }
                staticBufferWords.push_back(value);
            }
            ipcStaticBuffers.push_back({
                {"thread_id", threadId},
                {"address", staticBufferAddress},
                {"words", std::move(staticBufferWords)},
            });
        }
        nlohmann::json memorySnapshots = nlohmann::json::array();
        std::set<uint32_t> capturedAddresses;
        for (size_t index = 0;
             index < memoryCandidates.size() && memorySnapshots.size() < 512U;
             ++index) {
            const uint32_t address = memoryCandidates[index] & ~3U;
            if (!capturedAddresses.insert(address).second ||
                !process.Memory().IsMapped(address, 0x100U)) {
                continue;
            }
            nlohmann::json words = nlohmann::json::array();
            for (uint32_t offset = 0; offset < 0x100U; offset += 4U) {
                uint32_t value = 0;
                if (!process.Memory().Read32(address + offset, &value)) {
                    break;
                }
                words.push_back(value);
                memoryCandidates.push_back(value);
            }
            memorySnapshots.push_back({
                {"address", address},
                {"words", std::move(words)},
            });
        }
        nlohmann::json svcEvents = nlohmann::json::array();
        for (const auto& event : hostServices.SvcEvents()) {
            svcEvents.push_back({
                {"immediate", event.Immediate},
                {"pc", event.Pc},
                {"thread_id", event.ThreadId},
                {"handled", event.Handled},
                {"name", event.Name},
                {"detail", event.Detail},
            });
        }
        const auto lastSvc = hostServices.SvcEvents().empty()
                                 ? Oot3dNativeGame::NativeA32CtrSvcEvent{}
                                 : hostServices.SvcEvents().back();
        const std::string boundaryKind =
            hostServices.FallbackReason() !=
                    oot3d::recomp::a32::FallbackReason::None
                ? "fallback"
                : (!hostServices.SvcEvents().empty() ? "svc" : "dispatch");
        nlohmann::json gspCommands = nlohmann::json::array();
        for (const auto& command : picaFrontend.PendingGspCommands()) {
            gspCommands.push_back({
                {"control", command.Control},
                {"id", command.Control & 0xFFU},
                {"parameters", command.Parameters},
            });
        }
        nlohmann::json hardwareWrites = nlohmann::json::array();
        for (const auto& write : picaFrontend.PendingWrites()) {
            hardwareWrites.push_back({
                {"offset", write.Offset},
                {"physical_address", write.PhysicalAddress},
                {"value", write.Value},
            });
        }
        nlohmann::json drawPackets = nlohmann::json::array();
        for (const auto& draw : picaFrontend.PendingDrawPackets()) {
            nlohmann::json registers = nlohmann::json::array();
            for (size_t registerId = 0;
                 registerId < draw.Registers.size(); ++registerId) {
                if (draw.Registers[registerId] != 0U) {
                    registers.push_back(
                        {registerId, draw.Registers[registerId]});
                }
            }
            drawPackets.push_back({
                {"command_list_address", draw.CommandListAddress},
                {"command_list_offset_words", draw.CommandListOffsetWords},
                {"indexed", draw.Indexed},
                {"nonzero_registers", std::move(registers)},
            });
        }
        nlohmann::json drawSubmissions = nlohmann::json::array();
        std::unordered_map<uint64_t, std::pair<bool, std::string>>
            compiledVertexShaders;
        std::unordered_map<uint64_t, std::pair<bool, std::string>>
            compiledFragmentShaders;
        for (const auto& submission : submissionQueue.PendingDraws()) {
            nlohmann::json resources = nlohmann::json::array();
            for (const auto& resource : submission.Resources) {
                resources.push_back({
                    {"kind", static_cast<uint32_t>(resource.Kind)},
                    {"slot", resource.Slot},
                    {"physical_address", resource.PhysicalAddress},
                    {"first_element", resource.FirstElement},
                    {"byte_count", resource.ResolvedBytes().size()},
                });
            }
            nlohmann::json defaultAttributes = nlohmann::json::array();
            for (size_t attribute = 0;
                 attribute < submission.State.VertexInput.AttributeCount;
                 ++attribute) {
                if (!submission.State.VertexInput.Attributes[attribute].Default) {
                    continue;
                }
                defaultAttributes.push_back({
                    {"attribute", attribute},
                    {"input_register",
                     submission.State.ShaderInterface
                         .InputRegisterByAttribute[attribute]},
                    {"value", submission.Packet.DefaultAttributes[attribute]},
                });
            }
            Oot3dNativeGame::Oot3dPicaGeneratedVertexShader vertexShader;
            std::string shaderError;
            const bool vertexShaderGenerated =
                Oot3dNativeGame::GenerateOot3dPicaVertexShader(
                    submission.Packet, submission.State, vertexShader,
                    &shaderError);
            bool vertexShaderCacheHit = false;
            bool vertexShaderCompiled = false;
            std::string shaderCompileError;
            if (vertexShaderGenerated) {
                const auto cached =
                    compiledVertexShaders.find(vertexShader.StateKey);
                if (cached != compiledVertexShaders.end()) {
                    vertexShaderCacheHit = true;
                    vertexShaderCompiled = cached->second.first;
                    shaderCompileError = cached->second.second;
                } else {
                    vertexShaderCompiled = CompileVertexShader(
                        vertexShader.Source, shaderCompileError);
                    compiledVertexShaders.emplace(
                        vertexShader.StateKey,
                        std::pair{vertexShaderCompiled, shaderCompileError});
                }
            }
            Oot3dNativeGame::Oot3dPicaGeneratedFragmentShader fragmentShader;
            std::string fragmentShaderError;
            const bool fragmentShaderGenerated =
                Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                    submission.Packet, submission.State, fragmentShader,
                    &fragmentShaderError);
            bool fragmentShaderCacheHit = false;
            bool fragmentShaderCompiled = false;
            std::string fragmentShaderCompileError;
            if (fragmentShaderGenerated) {
                const auto cached =
                    compiledFragmentShaders.find(fragmentShader.StateKey);
                if (cached != compiledFragmentShaders.end()) {
                    fragmentShaderCacheHit = true;
                    fragmentShaderCompiled = cached->second.first;
                    fragmentShaderCompileError = cached->second.second;
                } else {
                    fragmentShaderCompiled = CompileFragmentShader(
                        fragmentShader.Source, fragmentShaderCompileError);
                    compiledFragmentShaders.emplace(
                        fragmentShader.StateKey,
                        std::pair{fragmentShaderCompiled,
                                  fragmentShaderCompileError});
                }
            }
            Oot3dNativeGame::Oot3dPicaVulkanDrawPlan vulkanPlan;
            std::string vulkanPlanError;
            const bool vulkanPlanBuilt =
                Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                    submission, vulkanPlan, &vulkanPlanError);
            drawSubmissions.push_back({
                {"id", submission.Id},
                {"minimum_vertex_index", submission.MinimumVertexIndex},
                {"maximum_vertex_index", submission.MaximumVertexIndex},
                {"vertex_count", submission.State.VertexInput.VertexCount},
                {"indexed", submission.State.VertexInput.Indexed},
                {"vertex_program_words",
                 submission.Packet.VertexShader.ProgramWordCount},
                {"vertex_swizzle_words",
                 submission.Packet.VertexShader.SwizzleWordCount},
                {"vertex_shader_generated", vertexShaderGenerated},
                {"vertex_shader_error", shaderError},
                {"vertex_shader_state_key", vertexShader.StateKey},
                {"vertex_shader_source_bytes", vertexShader.Source.size()},
                {"vertex_shader_compiled", vertexShaderCompiled},
                {"vertex_shader_cache_hit", vertexShaderCacheHit},
                {"vertex_shader_compile_error", shaderCompileError},
                {"fragment_shader_generated", fragmentShaderGenerated},
                {"fragment_shader_error", fragmentShaderError},
                {"fragment_shader_state_key", fragmentShader.StateKey},
                {"fragment_shader_source_bytes", fragmentShader.Source.size()},
                {"fragment_shader_compiled", fragmentShaderCompiled},
                {"fragment_shader_cache_hit", fragmentShaderCacheHit},
                {"fragment_shader_compile_error", fragmentShaderCompileError},
                {"vulkan_plan_built", vulkanPlanBuilt},
                {"vulkan_plan_error", vulkanPlanError},
                {"vulkan_vertex_bindings", vulkanPlan.VertexBindings.size()},
                {"vulkan_vertex_attributes",
                 vulkanPlan.VertexAttributes.size()},
                {"vulkan_index_bytes", vulkanPlan.ResolvedIndexBytes().size()},
                {"vulkan_base_vertex", vulkanPlan.BaseVertex},
                {"vulkan_texture_bindings", vulkanPlan.Textures.size()},
                {"pipeline_state",
                 {{"topology",
                   static_cast<uint32_t>(submission.State.Topology)},
                  {"cull_mode",
                   static_cast<uint32_t>(submission.State.CullMode)},
                  {"viewport",
                   {{"half_width", submission.State.Viewport.HalfWidth},
                    {"half_height", submission.State.Viewport.HalfHeight},
                    {"corner_x", submission.State.Viewport.CornerX},
                    {"corner_y", submission.State.Viewport.CornerY},
                    {"depth_range", submission.State.Viewport.DepthRange},
                    {"near_plane", submission.State.Viewport.NearPlane},
                    {"z_buffering", submission.State.Viewport.ZBuffering}}},
                  {"scissor",
                   {{"mode",
                     static_cast<uint32_t>(submission.State.Scissor.Mode)},
                    {"x1", submission.State.Scissor.X1},
                    {"y1", submission.State.Scissor.Y1},
                    {"x2", submission.State.Scissor.X2},
                    {"y2", submission.State.Scissor.Y2}}},
                  {"color_write_mask",
                   submission.State.OutputMerger.ColorWriteMask},
                  {"fragment_operation_mode",
                   submission.State.OutputMerger.FragmentOperationMode},
                  {"logic_operation",
                   static_cast<uint32_t>(
                       submission.State.OutputMerger.LogicOperation)},
                  {"blend_enabled",
                   submission.State.OutputMerger.Blend.Enabled},
                  {"depth_test_enabled",
                   submission.State.OutputMerger.Depth.TestEnabled},
                  {"depth_write_enabled",
                   submission.State.OutputMerger.Depth.WriteEnabled},
                  {"depth_compare",
                   static_cast<uint32_t>(
                       submission.State.OutputMerger.Depth.Compare)},
                  {"stencil_enabled",
                   submission.State.OutputMerger.Stencil.Enabled}}},
                {"default_attributes", std::move(defaultAttributes)},
                {"resources", std::move(resources)},
            });
        }
        const nlohmann::json report = {
            {"format", "oot3d_native_a32_boot_probe_v1"},
            {"process",
             {{"name", manifest->ProcessName},
              {"entrypoint", manifest->EntryAddress},
              {"code_bin_sha256", manifest->CodeBinSha256},
              {"mapped_regions", process.Memory().RegionCount()}}},
            {"pica_frontend",
             {{"hardware_register_writes",
              picaFrontend.HardwareWriteCount()},
              {"hardware_register_write_records", std::move(hardwareWrites)},
              {"gsp_commands", std::move(gspCommands)},
              {"pica_register_writes",
               picaFrontend.PendingRegisterWrites().size()},
              {"draw_packets", picaFrontend.PendingDrawPackets().size()},
              {"draw_packet_records", std::move(drawPackets)},
              {"draw_submissions", std::move(drawSubmissions)}}},
            {"run",
             {{"kind", RunKindName(result.Kind)},
              {"error", result.Error},
              {"exit_kind", static_cast<uint32_t>(result.Exit.kind)},
              {"exit_pc", result.Exit.pc},
              {"exit_detail", result.Exit.detail},
              {"host_transitions", result.HostTransitions},
              {"vblanks_requested", arguments->VblankCount},
              {"vblanks_delivered", vblanksDelivered},
              {"system_ticks", hostServices.SystemTicks()}}},
            {"boundary",
             {{"kind", boundaryKind},
              {"pc", boundaryKind == "fallback" ? hostServices.FallbackPc()
                                                  : lastSvc.Pc},
              {"svc_immediate", lastSvc.Immediate},
              {"svc_name", lastSvc.Name},
              {"svc_handled", lastSvc.Handled},
              {"fallback_reason",
               static_cast<uint32_t>(hostServices.FallbackReason())},
              {"raw_operation", hostServices.FallbackRawOperation()}}},
            {"svc_events", svcEvents},
            {"threads", std::move(threads)},
            {"ipc_command_buffers", std::move(ipcCommandBuffers)},
            {"ipc_static_buffers", std::move(ipcStaticBuffers)},
            {"memory_snapshots", std::move(memorySnapshots)},
            {"state",
             {{"registers", registers},
              {"cpsr", state.cpsr},
              {"fpscr", state.fpscr},
              {"thread_pointer", state.thread_pointer}}},
        };
        WriteReport(report, arguments->Output);
        return result.Kind ==
                       Oot3dNativeGame::NativeA32ProcessRunKind::Waiting &&
                   boundaryKind == "svc"
                   ? 0
                   : 2;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_a32_boot_probe: " << exception.what()
                  << '\n';
        return 1;
    }
}
