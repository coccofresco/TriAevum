#include "fast/renderer/pipeline_preparation_job.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_startup_programs.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/oot3d/pica_uber_fragment_binaries.h"
#include "headless_device.h"
#include "fast/oot3d/pica_pipeline_preparation.h"
// Translated title vertex family (title code, like the runtime executable's).
#include "oot3d_native_vertex_binaries.h"
#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace {
using namespace Fast;
using Json = nlohmann::json;

using TriAevum::Tools::HeadlessDevice;

// The renderer's startup program parts, created by worker threads: about one
// per physical core, since driver compilation oversubscribed across logical
// cores makes each part slower for no added throughput.
struct StartupPreparation {
    std::vector<Renderer3ds::PicaStartupProgramPart> Parts;
    Renderer3ds::PicaStartupProgramDevice Device;
    unsigned Threads = 0;
    std::atomic<size_t> Next{0};
    std::atomic<size_t> Attempted{0};
    std::atomic<size_t> Prepared{0};
    std::atomic<size_t> Failed{0};
    std::atomic<unsigned> Running{0};
    std::atomic<bool> Cancelled{false};
    std::mutex ErrorMutex;
    std::string LastError;
    double Seconds = 0.0;
};
}

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        std::vector<std::filesystem::path> manifests;
        bool validation = false;
        bool startupPrograms = false;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--validation") { validation = true; continue; }
            if (key == "--startup-programs") { startupPrograms = true; continue; }
            if (key == "--manifest") {
                if (++i >= argc || manifests.size() == 64) throw std::runtime_error("expected 1 to 64 manifests");
                manifests.emplace_back(argv[i]);
                continue;
            }
            if (key != "--manifest" && key != "--pack" && key != "--cache-dir" &&
                key != "--report" && key != "--cancel-file" && key != "--adapter")
                throw std::runtime_error("unknown pipeline preparation option: " + key);
            if (++i >= argc || !args.emplace(key, argv[i]).second) throw std::runtime_error("missing or duplicate option");
        }
        if (manifests.empty() && !startupPrograms) throw std::runtime_error("required: --manifest or --startup-programs");
        for (const auto* key : {"--cache-dir", "--report"})
            if (!args.contains(key) || args.at(key).empty()) throw std::runtime_error(std::string("required: ") + key);
        if (!manifests.empty() && (!args.contains("--pack") || args.at("--pack").empty()))
            throw std::runtime_error("required: --pack");
        uint32_t adapter = 0;
        if (args.contains("--adapter")) {
            const auto& value = args.at("--adapter");
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), adapter);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                throw std::runtime_error("adapter must be an unsigned integer");
        }
        const auto cachePath = std::filesystem::weakly_canonical(
            std::filesystem::path(args.at("--cache-dir")) / Renderer3ds::kNriPipelineCacheFilename);
        const auto reportPath = std::filesystem::weakly_canonical(args.at("--report"));
        for (const auto* key : {"--manifest", "--pack", "--cancel-file"}) {
            if (!args.contains(key)) continue;
            const auto input = std::filesystem::weakly_canonical(args.at(key));
            if (input == cachePath || input == reportPath)
                throw std::runtime_error("pipeline preparation output overlaps an input");
        }
        if (cachePath == reportPath) throw std::runtime_error("preparation outputs overlap");
        for (const auto& path : manifests) {
            const auto input = std::filesystem::weakly_canonical(path);
            if (input == cachePath || input == reportPath)
                throw std::runtime_error("pipeline preparation output overlaps a manifest");
        }
        const auto begin = std::chrono::steady_clock::now();
        Fast::Oot3d::PicaAotShaderPack pack;
        std::string error;
        if (!manifests.empty() && !pack.Load(args.at("--pack"), &error))
            throw std::runtime_error(error);
        std::map<uint64_t, Fast::Oot3d::PicaGraphicsPipelineManifestEntry> unique;
        size_t observations = 0;
        for (const auto& path : manifests) {
            Fast::Oot3d::PicaGraphicsPipelineManifest manifest;
            if (!manifest.Load(path, &error)) throw std::runtime_error(error);
            if (manifest.DescriptorSchemaVersion() != pack.DescriptorSchemaVersion())
                throw std::runtime_error("pipeline manifest and shader pack schema differ");
            observations += manifest.Entries().size();
            for (const auto& entry : manifest.Entries()) {
                auto [position, inserted] = unique.emplace(entry.StructuralId(), entry);
                if (!inserted && !position->second.StructurallyEquivalent(entry))
                    throw std::runtime_error("pipeline manifest structural collision");
                if (unique.size() > 65536) throw std::runtime_error("pipeline union exceeds recipe budget");
            }
        }
        std::vector<Fast::Oot3d::PicaGraphicsPipelineManifestEntry> entries;
        for (auto& [id, entry] : unique) entries.push_back(std::move(entry));
        HeadlessDevice device;
        if (!device.Initialize(adapter, validation))
            throw std::runtime_error("headless NRI Vulkan device initialization failed");
        const auto physical = reinterpret_cast<VkPhysicalDevice>(device.Wrapper.GetPhysicalDeviceVK(*device.Device()));
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        const auto identity = Renderer3ds::MakePipelineCacheHeader(properties);
        const auto depthFormat = Fast::Oot3d::FindPicaDepthFormat(physical);
        Renderer3ds::NriPicaPipelineBridge bridge;
        Renderer3ds::NriPicaExecutionConfig execution;
        execution.OwnedDraws = false;
        execution.FrameSlotCount = 1;
        execution.DescriptorSetsPerPool = 1;
        execution.PipelineLibrariesAvailable = device.PipelineLibraries;
        if (!bridge.Initialize(device, execution)) throw std::runtime_error(bridge.UnavailableReason());
        std::vector<uint8_t> cache;
        const bool loaded = Renderer3ds::LoadPipelineCacheData(cachePath, identity, cache,
                                                               Renderer3ds::kMaximumNriPipelineCacheBytes);
        if (!bridge.InitializePipelineCache(cache)) throw std::runtime_error("NRI device pipeline cache unavailable");
        const auto cancelled = [&] { return args.contains("--cancel-file") && std::filesystem::exists(args.at("--cancel-file")); };

        // The parts the renderer creates before its first frame: the same
        // derivation, at its single-sample startup state. Without library
        // support the renderer prepares none either.
        StartupPreparation startup;
        startup.Device = {depthFormat, VK_SAMPLE_COUNT_1_BIT, device.GeometryShaders};
        if (startupPrograms && device.PipelineLibraries)
            startup.Parts = Renderer3ds::PicaStartupProgramParts(
                Oot3dNativeGame::TitleVertexArtifacts(), Fast::Oot3d::UberFragmentArtifacts(),
                Renderer3ds::NativeFragmentArtifacts(), startup.Device);
        const size_t total = startup.Parts.size() + entries.size();
        const auto progress = [&](size_t attempted, size_t prepared, size_t failed, const std::string& last) {
            std::cout << Json{{"event", "pipeline_progress"}, {"total", total}, {"attempted", attempted},
                {"prepared", prepared}, {"failed", failed}, {"error", last}}.dump() << std::endl;
        };
        if (!startup.Parts.empty()) {
            const auto startupBegin = std::chrono::steady_clock::now();
            startup.Threads = std::clamp(std::thread::hardware_concurrency() / 2U, 1U, 8U);
            startup.Threads = std::min<unsigned>(startup.Threads, static_cast<unsigned>(startup.Parts.size()));
            std::vector<std::thread> workers;
            startup.Running = startup.Threads;
            for (unsigned thread = 0; thread < startup.Threads; ++thread) {
                workers.emplace_back([&] {
                    for (size_t index; !startup.Cancelled && (index = startup.Next++) < startup.Parts.size();) {
                        bool prepared = false;
                        std::string why;
                        try {
                            prepared = Renderer3ds::PreparePicaStartupProgramPart(bridge, startup.Parts[index], startup.Device);
                            if (!prepared) why = "NRI rejected startup program part " + std::to_string(index);
                        } catch (const std::exception& exception) { why = exception.what(); }
                        if (prepared && device.ValidationErrors()) {
                            prepared = false;
                            why = "NRI/Vulkan validation reported errors";
                        }
                        if (prepared) ++startup.Prepared;
                        else {
                            ++startup.Failed;
                            std::lock_guard lock(startup.ErrorMutex);
                            startup.LastError = why;
                        }
                        ++startup.Attempted;
                    }
                    --startup.Running;
                });
            }
            size_t reported = 0;
            while (startup.Running != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!startup.Cancelled && cancelled()) startup.Cancelled = true;
                const size_t attempted = startup.Attempted;
                if (attempted == reported) continue;
                reported = attempted;
                std::lock_guard lock(startup.ErrorMutex);
                progress(attempted, startup.Prepared, startup.Failed, startup.LastError);
            }
            for (auto& worker : workers) worker.join();
            startup.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startupBegin).count();
        }

        std::optional<Renderer::PipelinePreparationJob> job;
        if (!entries.empty() && !startup.Cancelled) {
            job.emplace(entries.size(), [&](size_t index, std::string& why) {
                const auto item = Fast::Oot3d::ResolvePicaPipelinePreparationItem(entries[index], pack, depthFormat);
                if (!bridge.PreparePipeline(item.Descriptor)) { why = "NRI rejected pipeline " + std::to_string(index); return false; }
                if (device.ValidationErrors()) { why = "NRI/Vulkan validation reported errors"; return false; }
                return true;
            });
            while (!job->Progress().Finished()) {
                const auto& p = job->Step(8, cancelled);
                progress(startup.Attempted + p.Attempted, startup.Prepared + p.Prepared, startup.Failed + p.Failed,
                         p.LastError.empty() ? startup.LastError : p.LastError);
            }
        }
        cache = bridge.GetPipelineCacheData();
        if (!Renderer3ds::StorePipelineCacheData(cachePath, identity, cache, &error,
                                                 Renderer3ds::kMaximumNriPipelineCacheBytes))
            throw std::runtime_error(error);
        const bool wasCancelled = startup.Cancelled || (job && job->Progress().Cancelled);
        const size_t attempted = startup.Attempted + (job ? job->Progress().Attempted : 0U);
        const size_t prepared = startup.Prepared + (job ? job->Progress().Prepared : 0U);
        const size_t failed = startup.Failed + (job ? job->Progress().Failed : 0U);
        const std::string lastError = job && !job->Progress().LastError.empty() ? job->Progress().LastError : startup.LastError;
        const bool complete = !wasCancelled && total != 0 && attempted == total && failed == 0;
        const auto statistics = bridge.PipelineStatistics();
        Json report{{"format", "triaevum_device_pipeline_preparation_v1"},
            {"device_pipeline_prewarm", complete ? "complete" : wasCancelled ? "cancelled" : "partial"},
            {"total", total}, {"prepared", prepared}, {"failed", failed}, {"error", lastError},
            {"manifest_count", manifests.size()}, {"input_recipes", observations}, {"recipes", entries.size()},
            {"startup_programs_requested", startupPrograms}, {"pipeline_libraries", device.PipelineLibraries},
            {"startup_parts", startup.Parts.size()}, {"startup_prepared", startup.Prepared.load()},
            {"startup_failed", startup.Failed.load()}, {"startup_threads", startup.Threads},
            {"startup_seconds", startup.Seconds},
            {"validation_requested", validation}, {"validation_errors", device.ValidationErrors()},
            {"creation_nanoseconds", statistics.CreationNanoseconds},
            {"initial_cache_bytes", statistics.InitialCacheBytes},
            {"creation_attempts", statistics.CreationAttempts}, {"created", statistics.Created},
            {"cache_input_loaded", loaded}, {"cache_bytes", cache.size()}, {"cache_path", cachePath.string()},
            {"gpu", properties.deviceName}, {"vendor_id", identity.VendorId}, {"device_id", identity.DeviceId},
            {"driver_version", identity.DriverVersion}, {"pipeline_cache_uuid", identity.Uuid},
            {"renderer_abi", identity.RendererAbi}, {"depth_format", static_cast<uint32_t>(depthFormat)},
            {"game_booted", false}, {"game_coverage_proven", false},
            {"seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()}};
        if (!reportPath.parent_path().empty()) std::filesystem::create_directories(reportPath.parent_path());
        std::ofstream out(reportPath, std::ios::binary | std::ios::trunc);
        out << report.dump(2) << '\n'; out.close();
        if (!out) throw std::runtime_error("cannot write preparation report");
        std::cout << report.dump() << std::endl;
        return complete ? 0 : wasCancelled ? 4 : 3;
    } catch (const std::exception& exception) {
        std::cerr << "NRI pipeline preparation: " << exception.what() << '\n';
        return 1;
    }
}
