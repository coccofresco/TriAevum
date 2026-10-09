#include "fast/renderer/pipeline_preparation_job.h"
#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"
#include "fast/renderer3ds/pica_vulkan_device_profile.h"
#include "fast/renderer3ds/pica_startup_programs.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"
#include "fast/oot3d/pica_pipeline_preparation.h"
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
        // Geometry shaders back the opt-in PBR seam primitive IDs.
        Check(enabled.independentBlend && enabled.geometryShader && !enabled.shaderInt16);
        VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        supported12.timelineSemaphore = VK_TRUE;
        supported12.descriptorIndexing = VK_TRUE;
        supported12.pNext = &supported;
        const auto enabled12 = Renderer3ds::PicaVulkan12Features(supported12);
        Check(enabled12.timelineSemaphore && !enabled12.descriptorIndexing && !enabled12.shaderFloat16 && !enabled12.pNext);
        VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT libraries{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicState{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        VkPhysicalDeviceFeatures core{};
        libraries.graphicsPipelineLibrary = dynamicState.extendedDynamicState = core.fragmentStoresAndAtomics = VK_TRUE;
        Check(Renderer3ds::PicaPipelineLibrariesSupported(true, libraries, dynamicState, core));
        Check(!Renderer3ds::PicaPipelineLibrariesSupported(false, libraries, dynamicState, core));
        core.fragmentStoresAndAtomics = VK_FALSE;
        Check(!Renderer3ds::PicaPipelineLibrariesSupported(true, libraries, dynamicState, core));
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
        {
            // The NRI cache outgrows the plain limit: refused there (the last
            // valid file stays), stored and loaded within the NRI limit.
            const std::vector<uint8_t> large(Renderer3ds::kMaximumPipelineCacheBytes + 1U, 7U);
            Check(Renderer3ds::StorePipelineCacheData(path, identity, bytes));
            Check(!Renderer3ds::StorePipelineCacheData(path, identity, large));
            Check(Renderer3ds::LoadPipelineCacheData(path, identity, read) && read == bytes);
            Check(Renderer3ds::StorePipelineCacheData(path, identity, large, nullptr,
                                                      Renderer3ds::kMaximumNriPipelineCacheBytes));
            Check(!Renderer3ds::LoadPipelineCacheData(path, identity, read));
            Check(Renderer3ds::LoadPipelineCacheData(path, identity, read,
                                                     Renderer3ds::kMaximumNriPipelineCacheBytes) &&
                  read == large);
        }
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
        // The geometry guide's outline channels (R/A); G/B carry the face normal.
        Check(state.Colors[6].colorWriteMask == (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT));
        state = Fast::Oot3d::BuildPicaNriPipelineState(draw, outputs, {Renderer3ds::kAllPicaAuxiliaryOutputs},
                                                VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT, false, true);
        Check(!state.DepthWrite && !state.DepthTest && state.Colors[0].colorWriteMask == 0);
        Check(state.Colors[3].colorWriteMask == VK_COLOR_COMPONENT_A_BIT);
        // Exercise the same manifest-to-NRI lowering used by Forge, not only
        // the live raster-state factory. Synthetic modules never reach a GPU.
        Fast::Oot3d::PicaGraphicsPipelineManifestEntry recipe;
        recipe.DescriptorSchemaVersion = 3;
        recipe.VertexShaderKey = 1; recipe.FragmentShaderKey = 2;
        recipe.VertexSource = {1, 2, 8}; recipe.FragmentSource = {3, 4, 8};
        recipe.NriFragmentSource = {5, 6, 8}; recipe.NriFragmentAvailable = true;
        recipe.VertexBindings = {{0, 16, false}};
        recipe.VertexAttributes = {{0, 0, ::Oot3d::Renderer::PicaVertexFormat::Float, 4, 0}};
        recipe.Domain = Fast::Oot3d::PicaGraphicsPipelineDomain::Instrumented;
        recipe.RequestedFeatures = recipe.AppliedFeatures = Fast::Oot3d::PicaShaderInstrumentationFeature::NormalGuide;
        recipe.AttachmentRequirementsKey = static_cast<uint8_t>(Renderer3ds::kAllPicaAuxiliaryOutputs);
        recipe.ShaderOutputs.SceneDomainTransparentDepthOverlay = true;
        recipe.DepthTestEnabled = recipe.DepthWriteEnabled = true;
        recipe.ColorWriteMask = 15;
        const std::vector<uint32_t> spirv{0x07230203, 0x00010000, 0, 1, 0};
        const std::array binaries{
            Fast::Oot3d::PicaAotShaderBinary{Fast::Oot3d::PicaAotShaderStage::Vertex, recipe.VertexSource, spirv},
            Fast::Oot3d::PicaAotShaderBinary{Fast::Oot3d::PicaAotShaderStage::NriFragment, recipe.NriFragmentSource, spirv}};
        path += ".o3ps";
        std::string error;
        Check(Fast::Oot3d::WritePicaAotShaderPack(path, 3, binaries, &error));
        Fast::Oot3d::PicaAotShaderPack pack;
        Check(pack.Load(path, &error));
        auto prepared = Fast::Oot3d::ResolvePicaPipelinePreparationItem(recipe, pack, VK_FORMAT_D32_SFLOAT);
        Check(prepared.Descriptor.DepthTest && prepared.Descriptor.DepthWrite &&
              prepared.Descriptor.Colors[0].colorWriteMask == 15);
        recipe.OutlineOcclusionOnly = true;
        prepared = Fast::Oot3d::ResolvePicaPipelinePreparationItem(recipe, pack, VK_FORMAT_D32_SFLOAT);
        Check(!prepared.Descriptor.DepthTest && !prepared.Descriptor.DepthWrite &&
              !prepared.Descriptor.StencilTest && prepared.Descriptor.Colors[0].colorWriteMask == 0 &&
              prepared.Descriptor.Colors[3].colorWriteMask == VK_COLOR_COMPONENT_A_BIT);
        std::filesystem::remove(path);
        draw.FragmentOperationMode = 3;
        state = Fast::Oot3d::BuildPicaNriPipelineState(draw, {}, {}, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT);
        Check(state.Colors[0].colorWriteMask == 0 && !state.LogicOpEnabled);

        // Startup program parts: one derivation for the renderer and Forge.
        const std::vector<uint32_t> plain{0x07230203, 0x00010000, 0, 1, 0};
        const std::vector<uint32_t> geometry{0x07230203, 0x00010000, 0, 1, 0, (2U << 16U) | 17U, 2U};
        const std::array vertices{Renderer3ds::PicaVertexArtifact{{}, plain},
                                  Renderer3ds::PicaVertexArtifact{{}, geometry}};
        const std::array superset{Renderer3ds::PicaFragmentArtifact{{}, true, geometry},
                                  Renderer3ds::PicaFragmentArtifact{{}, false, plain}};
        const std::array canonical{Renderer3ds::PicaFragmentArtifact{{}, true, plain}};
        Renderer3ds::PicaStartupProgramDevice startupDevice{VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, true};
        auto parts = Renderer3ds::PicaStartupProgramParts(vertices, superset, canonical, startupDevice);
        // Other vertex programs with the canonical base, then the superset
        // (separate samplers only), then the canonical bases.
        Check(parts.size() == 3 && parts[0].Vertex.data() == geometry.data() && parts[0].Fragment.data() == plain.data());
        Check(parts[1].Vertex.data() == plain.data() && parts[1].Fragment.data() == geometry.data());
        Check(parts[2].Fragment.data() == plain.data() && !parts[2].AlphaToCoverage);
        startupDevice.GeometryShaders = false;
        Check(Renderer3ds::PicaStartupProgramParts(vertices, superset, canonical, startupDevice).size() == 2);
        startupDevice.Samples = VK_SAMPLE_COUNT_4_BIT;
        parts = Renderer3ds::PicaStartupProgramParts(vertices, superset, canonical, startupDevice);
        Check(parts.size() == 3 && !parts[1].AlphaToCoverage && parts[2].AlphaToCoverage);
        bool threw = false;
        try { Renderer3ds::PicaStartupProgramParts({}, superset, canonical, startupDevice); }
        catch (const std::runtime_error&) { threw = true; }
        Check(threw);

        // Every item is a valid link: a fragment program reading the temporal
        // inputs (7, 8) takes the vertex variant that writes them, and the
        // vertex parts take a base with the layout's separate samplers.
        const auto module = [](uint32_t storageClass, std::initializer_list<uint32_t> locations) {
            std::vector<uint32_t> words{0x07230203, 0x00010000, 0, 100, 0};
            uint32_t id = 10;
            for (const uint32_t location : locations) words.insert(words.end(), {(4U << 16U) | 71U, id++, 30U, location});
            id = 10;
            for (size_t index = 0; index < locations.size(); ++index)
                words.insert(words.end(), {(4U << 16U) | 59U, 1U, id++, storageClass});
            return words;
        };
        const auto vertexPlain = module(3, {0, 1, 2, 3, 4, 5, 6});
        const auto vertexTemporal = module(3, {0, 1, 2, 3, 4, 5, 6, 7, 8});
        const auto fragmentTemporal = module(1, {0, 1, 2, 7, 8});
        const auto fragmentSurface = module(1, {0, 9, 10});
        const auto fragmentBase = module(1, {0, 1, 2, 3, 4});
        Check(Renderer3ds::SpirvInterfaceLocations(vertexTemporal, 3) == 0x1ffU &&
              Renderer3ds::SpirvInterfaceLocations(vertexTemporal, 1) == 0U);
        Check(!Renderer3ds::PicaStageInterfaceLinks(vertexPlain, fragmentTemporal) &&
              Renderer3ds::PicaStageInterfaceLinks(vertexTemporal, fragmentTemporal));
        const std::array family{Renderer3ds::PicaVertexArtifact{{}, vertexPlain},
                                Renderer3ds::PicaVertexArtifact{{}, vertexTemporal}};
        const std::array effects{Renderer3ds::PicaFragmentArtifact{{}, true, fragmentTemporal},
                                 Renderer3ds::PicaFragmentArtifact{{}, true, fragmentSurface}};
        const std::array bases{Renderer3ds::PicaFragmentArtifact{{}, false, fragmentBase},
                               Renderer3ds::PicaFragmentArtifact{{}, true, fragmentBase}};
        const Renderer3ds::PicaStartupProgramDevice linkDevice{VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, true};
        const auto links = Renderer3ds::PicaStartupProgramParts(family, effects, bases, linkDevice);
        // The temporal vertex variant with the separate base, the temporal
        // effect with it (no variant writes 9 and 10: no item for the surface
        // effect), then the separate base with the plain vertex program.
        Check(links.size() == 3);
        Check(links[0].Vertex.data() == vertexTemporal.data() && links[0].Fragment.data() == bases[1].Spirv.data());
        Check(links[1].Vertex.data() == vertexTemporal.data() && links[1].Fragment.data() == fragmentTemporal.data());
        Check(links[2].Vertex.data() == vertexPlain.data() && links[2].Fragment.data() == bases[1].Spirv.data());
        for (const auto& part : links) Check(Renderer3ds::PicaStageInterfaceLinks(part.Vertex, part.Fragment));
        struct RecordingBridge {
            Renderer3ds::NriPicaGraphicsPipelineDesc Desc;
            Renderer3ds::NriPicaPipelineFactory Factory = Renderer3ds::NriPicaPipelineFactory::Default;
            bool PreparePipeline(const Renderer3ds::NriPicaGraphicsPipelineDesc& desc,
                                 Renderer3ds::NriPicaPipelineFactory factory) {
                Desc = desc; Factory = factory; return true;
            }
        } recording;
        Check(Renderer3ds::PreparePicaStartupProgramPart(recording, parts[2], startupDevice));
        Check(recording.Factory == Renderer3ds::NriPicaPipelineFactory::Libraries &&
              recording.Desc.Samples == VK_SAMPLE_COUNT_4_BIT && recording.Desc.AlphaToCoverage &&
              recording.Desc.DepthStencilFormat == VK_FORMAT_D32_SFLOAT &&
              recording.Desc.ColorAttachmentCount == 1 && recording.Desc.VertexAttributes.size() == 16);
        std::cout << "pipeline preparation: batching, cancellation, failure, cache, raster and startup program contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
