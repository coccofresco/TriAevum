#include "fast/renderer/pipeline_preparation_job.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/oot3d/pica_pipeline_preparation.h"
#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <charconv>
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace Fast;
using Json = nlohmann::json;

class HeadlessDevice final : public Renderer3ds::NriPicaInterop {
  public:
    ~HeadlessDevice() override { if (mDevice) nri::nriDestroyDevice(mDevice); }
    bool Initialize(uint32_t index, bool validation) {
        uint32_t count = 0;
        if (nri::nriEnumerateAdapters(nullptr, count) != nri::Result::SUCCESS || !count || count > 64 || index >= count)
            return false;
        std::vector<nri::AdapterDesc> adapters(count);
        if (nri::nriEnumerateAdapters(adapters.data(), count) != nri::Result::SUCCESS || index >= count) return false;
        nri::DeviceCreationDesc create{};
        create.graphicsAPI = nri::GraphicsAPI::VK;
        create.adapterDesc = &adapters[index];
        create.disableVKRayTracing = true;
        create.enableNRIValidation = validation;
        create.enableGraphicsAPIValidation = validation;
        create.callbackInterface.MessageCallback = Message;
        create.callbackInterface.userArg = this;
        if (nri::nriCreateDevice(create, mDevice) != nri::Result::SUCCESS) return false;
        return nri::nriGetInterface(*mDevice, NRI_INTERFACE(nri::CoreInterface), &mCore) == nri::Result::SUCCESS &&
            nri::nriGetInterface(*mDevice, NRI_INTERFACE(nri::WrapperVKInterface), &Wrapper) == nri::Result::SUCCESS;
    }
    bool Available() const override { return mDevice != nullptr; }
    const std::string& UnavailableReason() const override { return Reason; }
    nri::Device* Device() override { return mDevice; }
    nri::CoreInterface* Core() override { return &mCore; }
    bool WrapBuffer(VkBuffer, uint64_t, uint8_t*) override { return false; }
    bool WrapTexture(VkImage, VkFormat, VkImageType, VkImageUsageFlags, uint32_t, uint32_t, uint32_t) override { return false; }
    nri::CommandBuffer* CommandBuffer(uint32_t) override { return nullptr; }
    nri::Descriptor* TextureView(VkImage, bool) override { return nullptr; }
    nri::Buffer* Buffer(VkBuffer) override { return nullptr; }
    nri::Pipeline* WrapGraphicsPipeline(VkPipeline) override { return nullptr; }
    void DestroyPipelineWrapper(nri::Pipeline*) override {}
    bool CmdSetPipeline(uint32_t, nri::Pipeline*) override { return false; }
    uint32_t ValidationErrors() const { return mValidationErrors.load(); }
    nri::WrapperVKInterface Wrapper{};
  private:
    static void NRI_CALL Message(nri::Message type, const char*, uint32_t, const char* text, void* context) {
        if (type == nri::Message::ERROR) ++static_cast<HeadlessDevice*>(context)->mValidationErrors;
        std::cerr << "NRI: " << text << '\n';
    }
    std::atomic<uint32_t> mValidationErrors{0};
    nri::Device* mDevice = nullptr;
    nri::CoreInterface mCore{};
    std::string Reason = "headless NRI device unavailable";
};
}

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        bool validation = false;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--validation") { validation = true; continue; }
            if (key != "--manifest" && key != "--pack" && key != "--cache-dir" &&
                key != "--report" && key != "--cancel-file" && key != "--adapter")
                throw std::runtime_error("unknown pipeline preparation option: " + key);
            if (++i >= argc || !args.emplace(key, argv[i]).second) throw std::runtime_error("missing or duplicate option");
        }
        for (const auto* key : {"--manifest", "--pack", "--cache-dir", "--report"})
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
        const auto begin = std::chrono::steady_clock::now();
        Fast::Oot3d::PicaGraphicsPipelineManifest manifest;
        Fast::Oot3d::PicaAotShaderPack pack;
        std::string error;
        if (!manifest.Load(args.at("--manifest"), &error) || !pack.Load(args.at("--pack"), &error))
            throw std::runtime_error(error);
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
        Renderer::PipelinePreparationJob job(manifest.Entries().size(), [&](size_t index, std::string& why) {
            const auto item = Fast::Oot3d::ResolvePicaPipelinePreparationItem(manifest.Entries()[index], pack, depthFormat);
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
        Json report{{"format", "triaevum_device_pipeline_preparation_v1"},
            {"device_pipeline_prewarm", p.Complete() ? "complete" : p.Cancelled ? "cancelled" : "partial"},
            {"total", p.Total}, {"prepared", p.Prepared}, {"failed", p.Failed}, {"error", p.LastError},
            {"validation_requested", validation}, {"validation_errors", device.ValidationErrors()},
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
