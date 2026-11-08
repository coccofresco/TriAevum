#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_startup_programs.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/extension/pica_uber_fragment_binaries.h"
#include "fast/extension/pica_builtin_fragment_families.h"
#include "headless_device.h"
#include "fast/extension/pica_pipeline_preparation.h"
#include "fast/renderer/preparation_host.h"
#include "fast/renderer/title_program_pack.h"
#include <memory>
#include "fast/renderer3ds/pipeline_cache_flusher.h"
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
#include <cstdlib>
#include <string_view>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {
using namespace Fast;
using Json = nlohmann::json;

using TriAevum::Tools::HeadlessDevice;

// Independent pipeline creations on worker threads. The driver's compilation
// scales sub-linearly (the 1872 cold pipelines of the reference PC took 204 s
// on 6 threads and 175 s on 12; sequential recipes about 9 minutes) while each
// worker holds a compiler working set and some drivers (AMD proprietary)
// compile on a pool of their own, so the worker count follows
// Renderer::PreparationWorkerCount, every worker runs below normal priority,
// and a worker retires when host memory runs short (never the last one).
// Items the driver refused are retried once, sequentially, after the parallel
// pass: a refusal under memory pressure (VK_ERROR_OUT_OF_HOST_MEMORY, #78/#84)
// is not a verdict on the pipeline. The bridge and the device's pipeline
// cache accept concurrent creation.
struct ParallelPreparation {
    unsigned Threads = 0;
    std::atomic<size_t> Next{0};
    std::atomic<size_t> Attempted{0};
    std::atomic<size_t> Prepared{0};
    std::atomic<size_t> Failed{0};
    std::atomic<unsigned> Running{0};
    std::atomic<unsigned> RetiredForMemory{0};
    std::atomic<bool> Cancelled{false};
    std::mutex ErrorMutex;
    std::string LastError;
    std::vector<size_t> FailedIndices;
    size_t Retried = 0;
    size_t Recovered = 0;
    double Seconds = 0.0;

    template <class Prepare>
    bool Attempt(size_t index, Prepare& prepare) {
        bool prepared = false;
        std::string why;
        try {
            prepared = prepare(index, why);
        } catch (const std::exception& exception) { why = exception.what(); }
        if (prepared) ++Prepared;
        else {
            ++Failed;
            std::lock_guard lock(ErrorMutex);
            LastError = why;
            FailedIndices.push_back(index);
        }
        ++Attempted;
        return prepared;
    }

    // prepare(index, why) -> prepared; progress(attempted, prepared, failed, last error).
    template <class Prepare, class Cancel, class Progress>
    void Run(size_t count, unsigned workers, Prepare prepare, Cancel cancelled, Progress progress) {
        if (count == 0) return;
        const auto begin = std::chrono::steady_clock::now();
        Threads = std::min<unsigned>(std::max(1U, workers),
                                     static_cast<unsigned>(std::min<size_t>(count, Renderer::kPreparationWorkerLimit)));
        std::vector<std::thread> threads;
        Running = Threads;
        for (unsigned thread = 0; thread < Threads; ++thread) {
            threads.emplace_back([&] {
                Renderer::LowerPreparationThreadPriority();
                while (!Cancelled) {
                    // Decided before taking an item: the compiler working set
                    // of one more concurrent creation may not fit this machine.
                    if (Renderer::ShouldRetirePreparationWorker(Renderer::QueryHostMemory(), Running.load())) {
                        unsigned running = Running.load();
                        if (running > 1U && Running.compare_exchange_strong(running, running - 1U)) {
                            ++RetiredForMemory;
                            return;
                        }
                        continue;
                    }
                    const size_t index = Next++;
                    if (index >= count) break;
                    Attempt(index, prepare);
                }
                --Running;
            });
        }
        size_t reported = 0;
        while (Running != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!Cancelled && cancelled()) Cancelled = true;
            const size_t attempted = Attempted;
            if (attempted == reported) continue;
            reported = attempted;
            std::lock_guard lock(ErrorMutex);
            progress(attempted, Prepared.load(), Failed.load(), LastError);
        }
        for (auto& worker : threads) worker.join();
        std::vector<size_t> failed;
        {
            std::lock_guard lock(ErrorMutex);
            failed.swap(FailedIndices);
        }
        for (const size_t index : failed) {
            if (Cancelled || cancelled()) { Cancelled = true; break; }
            ++Retried;
            --Failed;
            if (Attempt(index, prepare)) ++Recovered;
            --Attempted;
            std::lock_guard lock(ErrorMutex);
            progress(Attempted.load(), Prepared.load(), Failed.load(), LastError);
        }
        Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    }
};
}

int TriAevumPipelinePrepareMain(int argc, char** argv) {
    try {
        // The desktop keeps its cores: this job competes with nothing interactive.
        const bool priorityLowered = Fast::Renderer::LowerPreparationProcessPriority();
        const auto hostMemoryAtStart = Fast::Renderer::QueryHostMemory();
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
                key != "--report" && key != "--cancel-file" && key != "--adapter" && key != "--title-program-pack")
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
        for (const auto* key : {"--manifest", "--pack", "--cancel-file", "--title-program-pack"}) {
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
        Fast::Extension::PicaAotShaderPack pack;
        std::string error;
        // The runtime reads binaries on demand; installation checks them all.
        if (!manifests.empty() && (!pack.Load(args.at("--pack"), &error) || !pack.Verify(&error)))
            throw std::runtime_error(error);
        // Own and validate the optional family before touching the GPU. Like
        // runtime startup, another title prepares only its own vertex programs.
        std::unique_ptr<Fast::Renderer::TitleProgramPack> titlePrograms;
        std::vector<Renderer3ds::PicaVertexArtifact> titleVertices;
        if (args.contains("--title-program-pack")) {
            titlePrograms = std::make_unique<Fast::Renderer::TitleProgramPack>(args.at("--title-program-pack"));
            titleVertices = Renderer3ds::PicaVertexArtifactsFromPack(titlePrograms->Entries());
            if (startupPrograms && titleVertices.empty()) throw std::runtime_error("title program pack has no vertex family");
        }
        std::map<uint64_t, Fast::Extension::PicaGraphicsPipelineManifestEntry> unique;
        size_t observations = 0;
        for (const auto& path : manifests) {
            Fast::Extension::PicaGraphicsPipelineManifest manifest;
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
        std::vector<Fast::Extension::PicaGraphicsPipelineManifestEntry> entries;
        for (auto& [id, entry] : unique) entries.push_back(std::move(entry));
        HeadlessDevice device;
        if (!device.Initialize(adapter, validation))
            throw std::runtime_error("headless NRI Vulkan device initialization failed");
        const auto physical = reinterpret_cast<VkPhysicalDevice>(device.Wrapper.GetPhysicalDeviceVK(*device.Device()));
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        const auto identity = Renderer3ds::MakePipelineCacheHeader(properties);
        const auto depthFormat = Fast::Extension::FindPicaDepthFormat(physical);
        Renderer3ds::NriPicaPipelineBridge bridge;
        Renderer3ds::NriPicaExecutionConfig execution;
        execution.OwnedDraws = false;
        execution.FrameSlotCount = 1;
        execution.DescriptorSetsPerPool = 1;
        execution.PipelineLibrariesAvailable = device.PipelineLibraries;
        execution.PipelineLibrariesEnabled = device.AllPipelinesUseLibraries;
        if (!bridge.Initialize(device, execution)) throw std::runtime_error(bridge.UnavailableReason());
        std::vector<uint8_t> cache;
        const bool loaded = Renderer3ds::LoadPipelineCacheData(cachePath, identity, cache,
                                                               Renderer3ds::kMaximumNriPipelineCacheBytes);
        if (!bridge.InitializePipelineCache(cache)) throw std::runtime_error("NRI device pipeline cache unavailable");
        const unsigned workers = Fast::Renderer::PreparationWorkerCount(
            {std::thread::hardware_concurrency(), device.DriverId == VK_DRIVER_ID_AMD_PROPRIETARY,
             std::getenv("TRIAEVUM_PIPELINE_PREPARE_THREADS")});
        // Work survives a kill or an exhausted time budget: the cache is flushed
        // while creation continues, then stored once more at the end.
        Renderer3ds::PipelineCacheFlusher flusher;
        flusher.Start(cachePath, identity, Renderer3ds::kMaximumNriPipelineCacheBytes,
                      [&] { return bridge.GetPipelineCacheData(); });
        const auto cancelled = [&] { return args.contains("--cancel-file") && std::filesystem::exists(args.at("--cancel-file")); };

        // The parts the renderer creates before its first frame: the same
        // derivation, at its single-sample startup state. Without library
        // support the renderer prepares none either.
        std::vector<Renderer3ds::PicaStartupProgramPart> parts;
        const Renderer3ds::PicaStartupProgramDevice startupDevice{depthFormat, VK_SAMPLE_COUNT_1_BIT,
                                                                  device.GeometryShaders};
        if (startupPrograms && device.PipelineLibraries)
            parts = Renderer3ds::PicaStartupProgramParts(
                titlePrograms ? std::span<const Renderer3ds::PicaVertexArtifact>(titleVertices)
                              : Oot3dNativeGame::TitleVertexArtifacts(),
                titlePrograms ? Fast::Extension::UberFragmentArtifacts()
                              : Fast::Extension::StartupUberFragmentArtifacts(false, false),
                Renderer3ds::NativeFragmentArtifacts(), startupDevice);
        const size_t total = parts.size() + entries.size();
        const auto progress = [&](size_t attempted, size_t prepared, size_t failed, const std::string& last) {
            flusher.Notice(prepared);
            std::cout << Json{{"event", "pipeline_progress"}, {"total", total}, {"attempted", attempted},
                {"prepared", prepared}, {"failed", failed}, {"error", last}}.dump() << std::endl;
        };
        const auto validated = [&](bool prepared, std::string& why) {
            if (prepared && device.ValidationErrors()) {
                why = "NRI/Vulkan validation reported errors";
                return false;
            }
            return prepared;
        };
        ParallelPreparation startup, recipes;
        startup.Run(parts.size(), workers, [&](size_t index, std::string& why) {
            const bool prepared = Renderer3ds::PreparePicaStartupProgramPart(bridge, parts[index], startupDevice);
            if (!prepared) why = "NRI rejected startup program part " + std::to_string(index);
            return validated(prepared, why);
        }, cancelled, progress);
        if (!startup.Cancelled)
            recipes.Run(entries.size(), workers, [&](size_t index, std::string& why) {
                const auto item = Fast::Extension::ResolvePicaPipelinePreparationItem(entries[index], pack, depthFormat,
                                                                                &Fast::Extension::FindPicaBuiltInFragmentSpirv);
                const bool prepared = bridge.PreparePipeline(item.Descriptor);
                if (!prepared) why = "NRI rejected pipeline " + std::to_string(index);
                return validated(prepared, why);
            }, cancelled, [&](size_t attempted, size_t prepared, size_t failed, const std::string& last) {
                progress(startup.Attempted + attempted, startup.Prepared + prepared, startup.Failed + failed,
                         last.empty() ? startup.LastError : last);
            });
        flusher.Stop();
        const auto flushes = flusher.Stats();
        cache = bridge.GetPipelineCacheData();
        if (!Renderer3ds::StorePipelineCacheData(cachePath, identity, cache, &error,
                                                 Renderer3ds::kMaximumNriPipelineCacheBytes))
            throw std::runtime_error(error);
        const bool wasCancelled = startup.Cancelled || recipes.Cancelled;
        const size_t attempted = startup.Attempted + recipes.Attempted;
        const size_t prepared = startup.Prepared + recipes.Prepared;
        const size_t failed = startup.Failed + recipes.Failed;
        const std::string lastError = !recipes.LastError.empty() ? recipes.LastError : startup.LastError;
        const bool complete = !wasCancelled && total != 0 && attempted == total && failed == 0;
        const auto statistics = bridge.PipelineStatistics();
        Json report{{"format", "triaevum_device_pipeline_preparation_v1"},
            {"device_pipeline_prewarm", complete ? "complete" : wasCancelled ? "cancelled" : "partial"},
            {"total", total}, {"prepared", prepared}, {"failed", failed}, {"error", lastError},
            {"manifest_count", manifests.size()}, {"input_recipes", observations}, {"recipes", entries.size()},
            {"startup_programs_requested", startupPrograms}, {"pipeline_libraries", device.PipelineLibraries},
            {"startup_parts", parts.size()}, {"startup_prepared", startup.Prepared.load()},
            {"startup_failed", startup.Failed.load()}, {"startup_threads", startup.Threads},
            {"startup_seconds", startup.Seconds},
            {"recipe_threads", recipes.Threads}, {"recipe_seconds", recipes.Seconds},
            {"worker_policy_threads", workers}, {"process_priority_lowered", priorityLowered},
            {"workers_retired_for_memory", startup.RetiredForMemory.load() + recipes.RetiredForMemory.load()},
            {"retried", startup.Retried + recipes.Retried}, {"recovered", startup.Recovered + recipes.Recovered},
            {"host_memory_known", hostMemoryAtStart.Known},
            {"host_memory_total_bytes", hostMemoryAtStart.TotalBytes},
            {"host_memory_available_bytes", hostMemoryAtStart.AvailableBytes},
            {"peak_working_set_bytes", Fast::Renderer::PeakWorkingSetBytes()},
            {"cache_flushes", flushes.Flushes}, {"cache_flushed_bytes", flushes.FlushedBytes},
            {"driver_id", static_cast<uint32_t>(device.DriverId)},
            {"validation_requested", validation}, {"validation_errors", device.ValidationErrors()},
            {"creation_nanoseconds", statistics.CreationNanoseconds},
            {"initial_cache_bytes", statistics.InitialCacheBytes},
            {"creation_attempts", statistics.CreationAttempts}, {"created", statistics.Created},
            {"creation_failures", statistics.CreationFailures}, {"library_fallbacks", statistics.LibraryFallbacks},
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

// The Android runtime library links this helper and calls it in process
// (triaevum_forge_tool.cpp) on the game's own renderer build.
#ifndef TRIAEVUM_TOOL_LIBRARY
int main(int argc, char** argv) {
    return TriAevumPipelinePrepareMain(argc, argv);
}
#endif
