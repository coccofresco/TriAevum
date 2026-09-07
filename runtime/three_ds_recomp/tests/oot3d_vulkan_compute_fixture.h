#pragma once

#include <gtest/gtest.h>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
void Check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("outline GPU test Vulkan failure: " + std::to_string(result));
}

// Executes the production contour kernel on deterministic surfaces, without
// title assets, gameplay or screen-capture ambiguity.
struct ComputeFixture {
    VkInstance Instance = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkShaderModule Shader = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool Commands = VK_NULL_HANDLE;

    ~ComputeFixture() {
        if (Device) {
            vkDeviceWaitIdle(Device);
            vkDestroyCommandPool(Device, Commands, nullptr);
            vkDestroyPipeline(Device, Pipeline, nullptr);
            vkDestroyShaderModule(Device, Shader, nullptr);
            vkDestroyPipelineLayout(Device, Layout, nullptr);
            vkDestroyDescriptorPool(Device, Pool, nullptr);
            vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
            vkDestroyBuffer(Device, Buffer, nullptr);
            vkFreeMemory(Device, Memory, nullptr);
            vkDestroyDevice(Device, nullptr);
        }
        vkDestroyInstance(Instance, nullptr);
    }

    std::array<std::array<float, 4>, 48> Run(const std::string& source) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance.pApplicationInfo = &app;
        Check(vkCreateInstance(&instance, nullptr, &Instance));
        uint32_t count = 0;
        Check(vkEnumeratePhysicalDevices(Instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        Check(vkEnumeratePhysicalDevices(Instance, &count, devices.data()));
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        uint32_t family = 0;
        for (auto candidate : devices) {
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, queues.data());
            for (uint32_t i = 0; i < count; ++i) {
                if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { physical = candidate; family = i; break; }
            }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("no Vulkan compute device for outline contract test");
        const float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queueInfo;
        Check(vkCreateDevice(physical, &device, nullptr, &Device));
        VkQueue queue;
        vkGetDeviceQueue(Device, family, 0, &queue);

        std::array<std::array<float, 4>, 48> result{};
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = sizeof(result); buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Check(vkCreateBuffer(Device, &buffer, nullptr, &Buffer));
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(Device, Buffer, &requirements);
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        uint32_t memoryType = UINT32_MAX;
        const auto hostFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1U << i)) &&
                (memory.memoryTypes[i].propertyFlags & hostFlags) == hostFlags) { memoryType = i; break; }
        if (memoryType == UINT32_MAX) throw std::runtime_error("no host coherent memory for outline test");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memoryType;
        Check(vkAllocateMemory(Device, &allocation, nullptr, &Memory));
        Check(vkBindBufferMemory(Device, Buffer, Memory, 0));

        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT};
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = 1; setLayout.pBindings = &binding;
        Check(vkCreateDescriptorSetLayout(Device, &setLayout, nullptr, &SetLayout));
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &poolSize;
        Check(vkCreateDescriptorPool(Device, &pool, nullptr, &Pool));
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = Pool; setInfo.descriptorSetCount = 1; setInfo.pSetLayouts = &SetLayout;
        VkDescriptorSet set;
        Check(vkAllocateDescriptorSets(Device, &setInfo, &set));
        VkDescriptorBufferInfo bufferInfo{Buffer, 0, sizeof(result)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1; layout.pSetLayouts = &SetLayout;
        Check(vkCreatePipelineLayout(Device, &layout, nullptr, &Layout));
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        const auto compiled = compiler.CompileGlslToSpv(source, shaderc_compute_shader, "outline_contract.comp", options);
        if (compiled.GetCompilationStatus() != shaderc_compilation_status_success)
            throw std::runtime_error(compiled.GetErrorMessage());
        const std::vector<uint32_t> spirv(compiled.cbegin(), compiled.cend());
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = spirv.size() * sizeof(uint32_t); shader.pCode = spirv.data();
        Check(vkCreateShaderModule(Device, &shader, nullptr, &Shader));
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout = Layout;
        pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                          VK_SHADER_STAGE_COMPUTE_BIT, Shader, "main"};
        Check(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipeline));
        VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commands.queueFamilyIndex = family;
        Check(vkCreateCommandPool(Device, &commands, nullptr, &Commands));
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool = Commands; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command;
        Check(vkAllocateCommandBuffers(Device, &commandInfo, &command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Check(vkBeginCommandBuffer(command, &begin));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, 1, 6, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
        Check(vkEndCommandBuffer(command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        Check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        Check(vkQueueWaitIdle(queue));
        void* mapped = nullptr;
        Check(vkMapMemory(Device, Memory, 0, sizeof(result), 0, &mapped));
        std::memcpy(result.data(), mapped, sizeof(result));
        vkUnmapMemory(Device, Memory);
        return result;
    }
};
} // namespace


