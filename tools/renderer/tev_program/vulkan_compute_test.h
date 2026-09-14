#pragma once
#include "vulkan_test_device.h"
#include <array>
#include <cstring>
#include <span>

// Small synchronous two-buffer harness, shared by equation differentials.
inline void RunComputeTest(const std::string& source, std::span<const std::byte> inputBytes,
                           std::span<std::byte> outputBytes, uint32_t count) {
    Gpu gpu;
    auto d=gpu.Device;
    auto input=gpu.MakeBuffer(inputBytes.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto output=gpu.MakeBuffer(outputBytes.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::memcpy(input.Mapped,inputBytes.data(),inputBytes.size());
    std::array<VkDescriptorSetLayoutBinding,2> bindings{{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount=2;li.pBindings=bindings.data();
    VkDescriptorSetLayout layout{};Check(vkCreateDescriptorSetLayout(d,&li,nullptr,&layout));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&ps;
    VkDescriptorPool pool{};Check(vkCreateDescriptorPool(d,&pi,nullptr,&pool));
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;
    VkDescriptorSet set{};Check(vkAllocateDescriptorSets(d,&ai,&set));
    std::array<VkDescriptorBufferInfo,2> buffers{{{input.Handle,0,VK_WHOLE_SIZE},{output.Handle,0,VK_WHOLE_SIZE}}};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(unsigned i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;
        writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
    vkUpdateDescriptorSets(d,2,writes.data(),0,nullptr);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;
    VkPipelineLayout pipelineLayout{};Check(vkCreatePipelineLayout(d,&pl,nullptr,&pipelineLayout));
    auto shader=gpu.Shader(source,shaderc_compute_shader);
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};ci.layout=pipelineLayout;
    VkPipeline pipeline{};Check(vkCreateComputePipelines(d,{},1,&ci,nullptr,&pipeline));
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=gpu.Family;
    VkCommandPool commands{};Check(vkCreateCommandPool(d,&cp,nullptr,&commands));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commands;ca.commandBufferCount=1;
    VkCommandBuffer cmd{};Check(vkAllocateCommandBuffers(d,&ca,&cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);
    vkCmdDispatch(cmd,count,1,1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    Check(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};Check(vkCreateFence(d,&fc,nullptr,&fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    Check(vkQueueSubmit(gpu.Queue,1,&submit,fence));Check(vkWaitForFences(d,1,&fence,VK_TRUE,30000000000ULL));
    std::memcpy(outputBytes.data(),output.Mapped,outputBytes.size());
    vkDestroyFence(d,fence,nullptr);vkDestroyCommandPool(d,commands,nullptr);
    vkDestroyPipeline(d,pipeline,nullptr);vkDestroyShaderModule(d,shader,nullptr);vkDestroyPipelineLayout(d,pipelineLayout,nullptr);
    vkDestroyDescriptorPool(d,pool,nullptr);vkDestroyDescriptorSetLayout(d,layout,nullptr);gpu.Free(input);gpu.Free(output);
}
