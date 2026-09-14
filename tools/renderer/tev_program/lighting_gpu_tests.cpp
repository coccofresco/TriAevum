#include "vulkan_test_device.h"
#include "oot3d_native_pica_fragment_lighting_gen.h"
#include "fast/renderer3ds/pica_lighting_program.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>

using namespace Oot3dNativeGame;
using Fast::Renderer3ds::PicaLightingProgram;
struct alignas(16) Uniforms {
    Oot3dPicaFragmentLightingUniformState Values;
    PicaLightingProgram Program;
};
struct Result { std::array<float,4> Primary, Secondary; };
static_assert(sizeof(Uniforms)==1168);

int main() {
    try {
        constexpr unsigned count=64;
        std::array<Uniforms,count> inputs{};
        std::ostringstream functions, dispatch;
        std::string common, dynamicBody;
        auto packet=std::make_unique<Oot3dPicaDrawPacket>();
        constexpr unsigned environments[]{0,1,2,3,4,5,6,8};
        for(unsigned n=0;n<count;++n) {
            packet->Registers[0x08F]=1;
            packet->Registers[0x1C2]=n%8;
            packet->Registers[0x1C3]=(n&1) | (environments[n/8]<<4) | ((n%4)<<2) |
                ((n&1)<<27) | ((n%8)<<16) | ((n&1)<<19) | ((n%3)<<28) | ((n&1)<<30);
            packet->Registers[0x1C4]=(n&1 ? 0xFF00U : 0x000000FFU) | ((n%3)<<16);
            packet->Registers[0x1D9]=n&1 ? 0x76543210 : 0x01234567;
            packet->Registers[0x1D0]=n&1 ? 0x02222222 : 0;
            packet->Registers[0x1D1]=(n%6)*0x01111111;
            packet->Registers[0x1D2]=(n%4)*0x01111111;
            for(unsigned i=0;i<8;++i) packet->Registers[0x149+i*16]=(n+i)&15;
            auto state=Fast::Oot3d::DecodePicaFragmentLighting(packet->Registers);
            auto& u=inputs[n];
            u.Program=Fast::Renderer3ds::BuildPicaLightingProgram(state);
            u.Values.GlobalAmbient={0.03F,0.04F,0.02F,0};
            for(unsigned i=0;i<8;++i) {
                u.Values.Position[i]={0.2F+float(i)*0.1F,-0.3F,0.5F,0};
                u.Values.SpotDirection[i]={0.1F,0.4F,-0.8F,0};
                u.Values.Attenuation[i]={0.1F,0.2F,0,0};
                u.Values.Diffuse[i]={0.07F,0.04F,0.09F,0};
                u.Values.Ambient[i]={0.01F,0.02F,0.01F,0};
                u.Values.Specular0[i]={0.03F,0.06F,0.02F,0};
                u.Values.Specular1[i]={0.04F,0.01F,0.03F,0};
            }
            std::string decl, body, error;
            Require(GenerateOot3dPicaFragmentLightingSource(*packet,"vec4(0.4)","vec4(0.3,0.6,0.9,0.7)",
                decl,body,&error,Oot3dPicaShaderBuildPurpose::OfflineSource,false),error.c_str());
            functions << "Result canonical_" << n << "() {\n" <<
                "vec3 pica_view=vec3(0.2,-0.1,0.8); vec4 pica_normquat=vec4(0.1,0.2,0.3,0.9);\n"
                "vec4 primary_fragment_color,secondary_fragment_color;\n" << body <<
                "return Result(primary_fragment_color,secondary_fragment_color);}\n";
            dispatch << "case " << n << "u: expected=canonical_" << n << "(); break;\n";
            std::string currentBody;
            Require(GenerateOot3dPicaFragmentLightingSource(*packet,"vec4(0.4)","vec4(0.3,0.6,0.9,0.7)",
                common,currentBody,&error,Oot3dPicaShaderBuildPurpose::OfflineSource,true),error.c_str());
            if(n==0) dynamicBody=currentBody;
            Require(currentBody==dynamicBody,"surface/light program specializes its source");
        }
        std::string shader="#version 450\n"+std::string(Fast::Renderer3ds::PicaLightingProgramDeclaration)+R"glsl(
struct LightingUniforms {
 vec4 lighting_specular0[8]; vec4 lighting_specular1[8];
 vec4 lighting_diffuse[8]; vec4 lighting_ambient[8];
 vec4 lighting_position[8]; vec4 lighting_spot_direction[8];
 vec4 lighting_attenuation[8]; vec4 lighting_global_ambient;
 PicaLightingProgram lighting_program;
};
struct Result {vec4 primary; vec4 secondary;};
layout(set=0,binding=0,std430) readonly buffer Input {LightingUniforms values[];};
layout(set=0,binding=1,std430) writeonly buffer Output {Result results[];};
#define fragment_uniforms values[gl_GlobalInvocationID.x]
)glsl"+common+functions.str()+"Result dynamic_lighting() {\n"
            "vec3 pica_view=vec3(0.2,-0.1,0.8); vec4 pica_normquat=vec4(0.1,0.2,0.3,0.9);\n"
            "vec4 primary_fragment_color,secondary_fragment_color;\n"+dynamicBody+
            "return Result(primary_fragment_color,secondary_fragment_color);}\n"
            "layout(local_size_x=1) in; void main(){uint i=gl_GlobalInvocationID.x; Result expected; switch(i){\n"+
            dispatch.str()+"} results[i*2u]=expected; results[i*2u+1u]=dynamic_lighting();}\n";
        Gpu gpu;
        auto device=gpu.Device;
        auto input=gpu.MakeBuffer(sizeof(inputs),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto output=gpu.MakeBuffer(count*2*sizeof(Result),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(input.Mapped,inputs.data(),sizeof(inputs));
        std::array<uint32_t,256*24> lut{};
        for(unsigned t=0;t<24;++t) for(unsigned i=0;i<256;++i)
            lut[t*256+i]=((i*11+t*97)%4096) | (((i+t)%127)<<12) | ((i&1)<<23);
        auto upload=gpu.MakeBuffer(sizeof(lut),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(upload.Mapped,lut.data(),sizeof(lut));
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType=VK_IMAGE_TYPE_2D; imageInfo.format=VK_FORMAT_R32_UINT;
        imageInfo.extent={256,24,1}; imageInfo.mipLevels=1; imageInfo.arrayLayers=1;
        imageInfo.samples=VK_SAMPLE_COUNT_1_BIT; imageInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImage image{}; Check(vkCreateImage(device,&imageInfo,nullptr,&image));
        VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device,image,&mr);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize=mr.size;
        ma.memoryTypeIndex=gpu.Type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory memory{}; Check(vkAllocateMemory(device,&ma,nullptr,&memory)); Check(vkBindImageMemory(device,image,memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=image;
        vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_R32_UINT; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkImageView view{}; Check(vkCreateImageView(device,&vi,nullptr,&view));
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        VkSampler sampler{}; Check(vkCreateSampler(device,&si,nullptr,&sampler));
        std::array<VkDescriptorSetLayoutBinding,3> bindings{{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {13,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=3;li.pBindings=bindings.data();
        VkDescriptorSetLayout layout{};Check(vkCreateDescriptorSetLayout(device,&li,nullptr,&layout));
        std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=2;pi.pPoolSizes=sizes.data();
        VkDescriptorPool pool{};Check(vkCreateDescriptorPool(device,&pi,nullptr,&pool));
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;
        VkDescriptorSet set{};Check(vkAllocateDescriptorSets(device,&ai,&set));
        std::array<VkDescriptorBufferInfo,2> buffers{{{input.Handle,0,VK_WHOLE_SIZE},{output.Handle,0,VK_WHOLE_SIZE}}};
        VkDescriptorImageInfo imageDescriptor{sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet,3> writes{};
        for(unsigned i=0;i<3;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;
            writes[i].dstBinding=bindings[i].binding;writes[i].descriptorType=bindings[i].descriptorType;writes[i].descriptorCount=1;
            if(i<2)writes[i].pBufferInfo=&buffers[i];else writes[i].pImageInfo=&imageDescriptor;}
        vkUpdateDescriptorSets(device,3,writes.data(),0,nullptr);
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=1;pli.pSetLayouts=&layout;
        VkPipelineLayout pipelineLayout{};Check(vkCreatePipelineLayout(device,&pli,nullptr,&pipelineLayout));
        auto module=gpu.Shader(shader,shaderc_compute_shader);
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.layout=pipelineLayout;
        ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
        VkPipeline pipeline{};Check(vkCreateComputePipelines(device,{},1,&ci,nullptr,&pipeline));
        VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=gpu.Family;
        VkCommandPool commands{};Check(vkCreateCommandPool(device,&cpi,nullptr,&commands));
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=commands;cai.commandBufferCount=1;
        VkCommandBuffer cmd{};Check(vkAllocateCommandBuffers(device,&cai,&cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
        VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.image=image;ib.subresourceRange=vi.subresourceRange;
        ib.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={256,24,1};
        vkCmdCopyBufferToImage(cmd,upload.Handle,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        ib.oldLayout=ib.newLayout;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ib);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);
        vkCmdDispatch(cmd,count,1,1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        Check(vkEndCommandBuffer(cmd));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};Check(vkCreateFence(device,&fci,nullptr,&fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        Check(vkQueueSubmit(gpu.Queue,1,&submit,fence));Check(vkWaitForFences(device,1,&fence,VK_TRUE,30000000000ULL));
        const auto* results=static_cast<const Result*>(output.Mapped);
        unsigned failures=0;float maximum=0;
        for(unsigned i=0;i<count;++i)for(unsigned component=0;component<8;++component){
            const auto& a=component<4?results[2*i].Primary:results[2*i].Secondary;
            const auto& b=component<4?results[2*i+1].Primary:results[2*i+1].Secondary;
            float difference=std::abs(a[component%4]-b[component%4]);maximum=std::max(maximum,difference);
            if(!std::isfinite(a[component%4])||!std::isfinite(b[component%4])||difference>0.00001F){
                if(failures<8)std::cerr<<"case="<<i<<" component="<<component<<" canonical="<<a[component%4]<<" parametric="<<b[component%4]<<'\n';
                ++failures;}}
        vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,commands,nullptr);
        vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);
        vkDestroyPipelineLayout(device,pipelineLayout,nullptr);vkDestroyDescriptorPool(device,pool,nullptr);vkDestroyDescriptorSetLayout(device,layout,nullptr);
        vkDestroySampler(device,sampler,nullptr);vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,memory,nullptr);
        gpu.Free(input);gpu.Free(output);gpu.Free(upload);
        std::cout<<count<<" lighting configurations, "<<count*8<<" channels, mismatches="<<failures<<", max_error="<<maximum<<'\n';
        Require(failures==0,"parametric lighting differs from canonical GPU equations");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
