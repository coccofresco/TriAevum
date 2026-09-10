#include "fast/renderer/pipeline_preparation_job.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "headless_device.h"
#include "fast/oot3d/pica_pipeline_preparation.h"
#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace Fast;
using Json = nlohmann::json;

using TriAevum::Tools::HeadlessDevice;
}

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        std::vector<std::filesystem::path> manifests;
        bool validation = false;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--validation") { validation = true; continue; }
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
        if (manifests.empty()) throw std::runtime_error("required: --manifest");
        for (const auto* key : {"--pack", "--cache-dir", "--report"})
            if (!args.contains(key) || args.at(key).empty()) throw std::runtime_error(std::string("required: ") + key);
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
        if (!pack.Load(args.at("--pack"), &error))
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
        execution.MaxDrawsPerFrame = 1;
        if (!bridge.Initialize(device, execution)) throw std::runtime_error(bridge.UnavailableReason());
        std::vector<uint8_t> cache;
        const bool loaded = Renderer3ds::LoadPipelineCacheData(cachePath, identity, cache);
        if (!bridge.InitializePipelineCache(cache)) throw std::runtime_error("NRI device pipeline cache unavailable");
        Renderer::PipelinePreparationJob job(entries.size(), [&](size_t index, std::string& why) {
            const auto item = Fast::Oot3d::ResolvePicaPipelinePreparationItem(entries[index], pack, depthFormat);
            if (!bridge.PreparePipeline(item.Descriptor)) { why = "NRI rejected pipeline " + std::to_string(index); return false; }
            if (device.ValidationErrors()) { why = "NRI/Vulkan validation reported errors"; return false; }
            return true;
        });
        const auto cancelled = [&] { return args.contains("--cancel-file") && std::filesystem::exists(args.at("--cancel-file")); };
        while (!job.Progress().Finished()) {
            const auto& p = job.Step(8, cancelled);
            std::cout << Json{{"event", "pipeline_progress"}, {"total", p.Total}, {"attempted", p.Attempted},
                {"prepared", p.Prepared}, {"failed", p.Failed}, {"error", p.LastError}}.dump() << std::endl;
        }
        cache = bridge.GetPipelineCacheData();
        if (!Renderer3ds::StorePipelineCacheData(cachePath, identity, cache, &error)) throw std::runtime_error(error);
        const auto& p = job.Progress();
        const auto statistics = bridge.PipelineStatistics();
        Json report{{"format", "triaevum_device_pipeline_preparation_v1"},
            {"device_pipeline_prewarm", p.Complete() ? "complete" : p.Cancelled ? "cancelled" : "partial"},
            {"total", p.Total}, {"prepared", p.Prepared}, {"failed", p.Failed}, {"error", p.LastError},
            {"manifest_count", manifests.size()}, {"input_recipes", observations},
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
        return p.Complete() ? 0 : p.Cancelled ? 4 : 3;
    } catch (const std::exception& exception) {
        std::cerr << "NRI pipeline preparation: " << exception.what() << '\n';
        return 1;
    }
}
