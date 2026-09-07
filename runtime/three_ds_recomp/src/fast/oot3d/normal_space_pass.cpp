#include "fast/oot3d/normal_space_pass.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <shaderc/shaderc.hpp>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
struct NormalSpacePass::Impl {
    NriInteropContext* Interop = nullptr;
    NriOwnedTexture2D Output{};
    ResourceStateTracker States;
    uint32_t Width = 0, Height = 0;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    nri::DescriptorSet* Set = nullptr;
#endif
};

NormalSpacePass::NormalSpacePass() : mImpl(std::make_unique<Impl>()) {}
NormalSpacePass::~NormalSpacePass() {
    Invalidate();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core && mImpl->Pipeline) mImpl->Core->DestroyPipeline(mImpl->Pipeline);
    if (mImpl->Core && mImpl->Layout) mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
#endif
}

void NormalSpacePass::Initialize(NriInteropContext& interop) {
#ifndef ENABLE_OOT3D_NRI
    throw std::runtime_error("normal-space conversion requires NRI");
#else
    mImpl->Interop = &interop;
    mImpl->Core = NriInteropAccess::Core(interop);
    auto* device = NriInteropAccess::Device(interop);
    if (!device || !mImpl->Core) throw std::runtime_error("normal-space NRI device unavailable");
    const std::array<nri::DescriptorRangeDesc, 2> ranges{{
        {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
        {1, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER}}};
    nri::DescriptorSetDesc set{};
    set.ranges = ranges.data(); set.rangeNum = 2;
    nri::RootConstantDesc root{};
    root.size = sizeof(float) * 16; root.shaderStages = nri::StageBits::COMPUTE_SHADER;
    nri::PipelineLayoutDesc layout{};
    layout.rootConstants = &root; layout.rootConstantNum = 1;
    layout.descriptorSets = &set; layout.descriptorSetNum = 1;
    layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
    layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
    if (mImpl->Core->CreatePipelineLayout(*device, layout, mImpl->Layout) != nri::Result::SUCCESS)
        throw std::runtime_error("normal-space layout creation failed");
    const char* source = R"glsl(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D input_normal;
layout(set=0,binding=1,rgba16f) uniform writeonly image2D output_normal;
layout(push_constant) uniform Transform { mat4 normal_to_output; } pc;
void main() {
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(p,imageSize(output_normal)))) return;
    vec4 guide=texelFetch(input_normal,p,0);
    vec3 n=guide.xyz*2.0-1.0;
    n=normalize(mat3(pc.normal_to_output)*n);
    imageStore(output_normal,p,vec4(n*0.5+0.5,guide.a));
}
)glsl";
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto compiled = compiler.CompileGlslToSpv(source, shaderc_compute_shader, "normal_space.comp", options);
    if (compiled.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(compiled.GetErrorMessage());
    const std::vector<uint32_t> code(compiled.cbegin(), compiled.cend());
    nri::ComputePipelineDesc pipeline{};
    pipeline.pipelineLayout = mImpl->Layout;
    pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
    pipeline.shader.bytecode = code.data(); pipeline.shader.size = code.size() * sizeof(uint32_t);
    if (mImpl->Core->CreateComputePipeline(*device, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
        throw std::runtime_error("normal-space pipeline creation failed");
#endif
}

void NormalSpacePass::Configure(uint32_t width, uint32_t height, VkImage input, VkFormat format) {
    Invalidate();
#ifdef ENABLE_OOT3D_NRI
    if (!mImpl->Pipeline || !width || !height || !input)
        throw std::runtime_error("normal-space input is unavailable");
    if (!mImpl->Interop->WrapTexture(input, format, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT, width, height) ||
        !mImpl->Interop->CreateOwnedTexture2D(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, mImpl->Output))
        throw std::runtime_error("normal-space image allocation failed");
    auto* src = NriInteropAccess::TextureView(*mImpl->Interop, input, false);
    auto* dst = NriInteropAccess::TextureView(*mImpl->Interop, mImpl->Output.Image, true);
    if (!src || !dst) throw std::runtime_error("normal-space descriptors unavailable");
    nri::DescriptorPoolDesc pool{};
    pool.descriptorSetMaxNum = 1; pool.textureMaxNum = 1; pool.storageTextureMaxNum = 1;
    if (mImpl->Core->CreateDescriptorPool(*NriInteropAccess::Device(*mImpl->Interop), pool, mImpl->Pool) != nri::Result::SUCCESS ||
        mImpl->Core->AllocateDescriptorSets(*mImpl->Pool, *mImpl->Layout, 0, &mImpl->Set, 1, 0) != nri::Result::SUCCESS)
        throw std::runtime_error("normal-space descriptor allocation failed");
    const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
        {mImpl->Set, 0, 0, &src, 1}, {mImpl->Set, 1, 0, &dst, 1}}};
    mImpl->Core->UpdateDescriptorRanges(updates.data(), 2);
    mImpl->Width = width; mImpl->Height = height;
#endif
}

void NormalSpacePass::Execute(uint32_t frameSlot, const std::array<float, 16>& transform,
                              EffectPassBarrierExecution& barriers) {
#ifdef ENABLE_OOT3D_NRI
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (!command || !mImpl->Set) throw std::runtime_error("normal-space dispatch not configured");
    const auto transition = [&](ResourceAccess access) {
        const auto barrier = mImpl->States.PlanTransition(reinterpret_cast<uintptr_t>(mImpl->Output.Image), {access, 0});
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, mImpl->Output.Image, barrier))
            throw std::runtime_error("normal-space resource transition failed");
        mImpl->States.Commit(barrier);
        barriers.RecordPrivateTransition(barrier);
    };
    transition(ResourceAccess::ComputeWrite);
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    mImpl->Core->CmdSetDescriptorSet(*command, {0, mImpl->Set});
    mImpl->Core->CmdSetRootConstants(*command, {0, transform.data(), sizeof(transform)});
    mImpl->Core->CmdDispatch(*command, {(mImpl->Width + 7) / 8, (mImpl->Height + 7) / 8, 1});
    transition(ResourceAccess::ShaderRead);
#endif
}

void NormalSpacePass::Invalidate() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core && mImpl->Pool) mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr; mImpl->Set = nullptr;
#endif
    if (mImpl->Interop && mImpl->Output.Image) mImpl->Interop->DestroyOwnedTexture(mImpl->Output.Image);
    mImpl->Output = {}; mImpl->Width = mImpl->Height = 0;
    mImpl->States.Clear();
}
VkImage NormalSpacePass::OutputImage() const { return mImpl->Output.Image; }
} // namespace Fast::Oot3d
#endif
