#pragma once
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include <vector>
#include <iostream>
#include <stdexcept>
inline void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline void Check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result));
}
struct Gpu {
    VkInstance Instance{};
    VkPhysicalDevice Physical{};
    VkDevice Device{};
    VkQueue Queue{};
    uint32_t Family{};
    VkPhysicalDeviceMemoryProperties Memory{};
    struct Buffer { VkBuffer Handle{}; VkDeviceMemory Memory{}; void* Mapped{}; };
    Gpu() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
        Check(vkCreateInstance(&info, nullptr, &Instance));
        uint32_t count = 0; Check(vkEnumeratePhysicalDevices(Instance, &count, nullptr));
        Require(count > 0, "no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(Instance, &count, devices.data()));
        Physical = devices[0];
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(Physical, &properties);
        std::cout << "GPU: " << properties.deviceName << '\n';
        vkGetPhysicalDeviceMemoryProperties(Physical, &Memory);
        vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, queues.data());
        while (Family < count && !(queues[Family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++Family;
        Require(Family < count, "no graphics queue");
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex = Family; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q;
        Check(vkCreateDevice(Physical, &d, nullptr, &Device)); vkGetDeviceQueue(Device, Family, 0, &Queue);
    }
    uint32_t Type(uint32_t mask, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < Memory.memoryTypeCount; ++i)
            if ((mask & (1U << i)) && (Memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("required memory type unavailable");
    }
    Buffer MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        Buffer b;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = size; info.usage = usage;
        Check(vkCreateBuffer(Device, &info, nullptr, &b.Handle));
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(Device, b.Handle, &requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(Device, &alloc, nullptr, &b.Memory)); Check(vkBindBufferMemory(Device, b.Handle, b.Memory, 0));
        Check(vkMapMemory(Device, b.Memory, 0, size, 0, &b.Mapped)); return b;
    }
    void Free(Buffer b) { vkUnmapMemory(Device, b.Memory); vkDestroyBuffer(Device, b.Handle, nullptr); vkFreeMemory(Device, b.Memory, nullptr); }
    VkShaderModule Shader(const std::string& source, shaderc_shader_kind kind) {
        shaderc::Compiler compiler; shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
        auto spirv = compiler.CompileGlslToSpv(source, kind, "tev_test", options);
        Require(spirv.GetCompilationStatus() == shaderc_compilation_status_success, spirv.GetErrorMessage().c_str());
        std::vector<uint32_t> words(spirv.cbegin(), spirv.cend());
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = words.size() * 4; info.pCode = words.data();
        VkShaderModule result{}; Check(vkCreateShaderModule(Device, &info, nullptr, &result)); return result;
    }
    ~Gpu() { if (Device) vkDestroyDevice(Device, nullptr); if (Instance) vkDestroyInstance(Instance, nullptr); }
};
