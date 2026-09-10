#include "fast/renderer/pipeline_preparation_job.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_vulkan_device_profile.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <source_location>

namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("pipeline preparation contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace Fast;
    try {
        Check(Renderer3ds::PicaVulkanApplicationInfo().apiVersion == VK_API_VERSION_1_2);
        VkPhysicalDeviceFeatures supported{};
        supported.independentBlend = VK_TRUE;
        supported.geometryShader = VK_TRUE;
        const auto enabled = Renderer3ds::PicaVulkanCoreFeatures(supported);
        Check(enabled.independentBlend && !enabled.geometryShader && !enabled.shaderInt16);
        VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        supported12.timelineSemaphore = VK_TRUE;
        supported12.descriptorIndexing = VK_TRUE;
        supported12.pNext = &supported;
        const auto enabled12 = Renderer3ds::PicaVulkan12Features(supported12);
        Check(enabled12.timelineSemaphore && !enabled12.descriptorIndexing && !enabled12.shaderFloat16 && !enabled12.pNext);
        size_t calls = 0;
        Renderer::PipelinePreparationJob job(5, [&](size_t index, std::string&) { Check(index == calls++); return true; });
        Check(job.Step(2).Prepared == 2);
        Check(!job.Progress().Finished());
        Check(job.Step(2).Prepared == 4);
        Check(job.Step(2).Complete());
        Check(job.Step(3).Prepared == 5 && calls == 5);
        Renderer::PipelinePreparationJob cancelled(3, [](size_t, std::string&) { return true; });
        Check(cancelled.Step(1, [] { return true; }).Cancelled);
        Check(cancelled.Progress().Prepared == 0 && !cancelled.Progress().Complete());
        Renderer::PipelinePreparationJob failure(2, [](size_t i, std::string&) {
            if (i) throw std::runtime_error("driver failure");
            return true;
        });
        Check(failure.Step(8).Failed == 1 && !failure.Progress().Complete());
        Check(failure.Progress().LastError == "driver failure");

        auto path = std::filesystem::temp_directory_path() /
            ("triaevum-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Renderer3ds::VulkanPipelineCacheHeader identity;
        identity.VendorId = 1; identity.DeviceId = 2; identity.DriverVersion = 3; identity.Uuid[3] = 4;
        const std::vector<uint8_t> bytes{0, 1, 2, 3, 9};
        std::vector<uint8_t> read;
        Check(Renderer3ds::StorePipelineCacheData(path, identity, bytes));
        Check(Renderer3ds::LoadPipelineCacheData(path, identity, read) && read == bytes);
        Check(Renderer3ds::StorePipelineCacheData(path, identity, bytes));
        for (int field = 0; field < 5; ++field) {
            auto changed = identity;
            if (field == 0) ++changed.RendererAbi;
            if (field == 1) ++changed.VendorId;
            if (field == 2) ++changed.DeviceId;
            if (field == 3) ++changed.DriverVersion;
            if (field == 4) ++changed.Uuid[0];
            Check(!Renderer3ds::LoadPipelineCacheData(path, changed, read) && read.empty());
        }
        Check(!Renderer3ds::StorePipelineCacheData(path, identity, {}));
        Check(Renderer3ds::LoadPipelineCacheData(path, identity, read) && read == bytes);
        {
            std::fstream corrupt(path, std::ios::in | std::ios::out | std::ios::binary);
            corrupt.seekp(-1, std::ios::end); corrupt.put('x');
        }
        Check(!Renderer3ds::LoadPipelineCacheData(path, identity, read) && read.empty());
        Check(Renderer3ds::StorePipelineCacheData(path, identity, bytes));
        std::filesystem::resize_file(path, 12);
        Check(!Renderer3ds::LoadPipelineCacheData(path, identity, read));
        std::filesystem::remove(path);

        Renderer3ds::PicaDrawView draw;
        draw.ColorWriteMask = 15;
        draw.DepthWriteEnabled = true;
        draw.DepthCompare = Renderer3ds::PicaCompareFunction::Less;
        auto state = Fast::Oot3d::BuildPicaNriPipelineState(draw, {}, {}, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT);
        Check(state.DepthTest && state.DepthWrite && state.DepthCompare == VK_COMPARE_OP_ALWAYS);
        Check(state.Colors[0].colorWriteMask == 15 && state.ColorAttachmentCount == 1);
        draw.DepthTestEnabled = true;
        draw.CullMode = Renderer3ds::NativeCullMode::KeepCounterClockwise;
        draw.FramebufferFlipped = true;
        Fast::Oot3d::PicaFragmentInstrumentationOutputLayout outputs;
        outputs.WritesOutlineGeometryGuide = true;
        state = Fast::Oot3d::BuildPicaNriPipelineState(draw, outputs, {Renderer3ds::kAllPicaAuxiliaryOutputs},
                                                VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT);
        Check(state.DepthCompare == VK_COMPARE_OP_LESS && state.CullMode == VK_CULL_MODE_FRONT_BIT);
        Check(state.FrontFace == VK_FRONT_FACE_CLOCKWISE && state.ColorAttachmentCount == 7);
        Check(state.Colors[6].colorWriteMask == 15);
        state = Fast::Oot3d::BuildPicaNriPipelineState(draw, outputs, {Renderer3ds::kAllPicaAuxiliaryOutputs},
                                                VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT, false, true);
        Check(!state.DepthWrite && !state.DepthTest && state.Colors[0].colorWriteMask == 0);
        Check(state.Colors[3].colorWriteMask == VK_COLOR_COMPONENT_A_BIT);
        draw.FragmentOperationMode = 3;
        state = Fast::Oot3d::BuildPicaNriPipelineState(draw, {}, {}, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT);
        Check(state.Colors[0].colorWriteMask == 0 && !state.LogicOpEnabled);
        std::cout << "pipeline preparation: batching, cancellation, failure, cache and raster contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
