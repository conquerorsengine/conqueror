#pragma once

#ifdef CQ_VULKAN_ENABLED

// Vulkan yükleyicisi çalışma anında açılır (volk benzeri): yürütülebilir dosyalar vulkan-1.dll / libvulkan'a
// bağlanmaz. Vulkan sürücüsü olmayan sistemde program yine açılır ve OpenGL/D3D11'e düşer.
// Bu başlık <vulkan/vulkan.h>'yi VK_NO_PROTOTYPES ile içerir; motorda Vulkan'ı hep buradan içerin.

#ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <string>

#define CQ_VK_GLOBAL_FUNCTIONS(X) \
    X(vkCreateInstance) \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties) \
    X(vkEnumerateInstanceVersion)

#define CQ_VK_INSTANCE_FUNCTIONS(X) \
    X(vkCreateDebugUtilsMessengerEXT) \
    X(vkCreateDevice) \
    X(vkDestroyDebugUtilsMessengerEXT) \
    X(vkDestroyInstance) \
    X(vkDestroySurfaceKHR) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetDeviceProcAddr) \
    X(vkGetPhysicalDeviceFeatures) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)

#define CQ_VK_DEVICE_FUNCTIONS(X) \
    X(vkAcquireNextImageKHR) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginRenderingKHR) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBlitImage) \
    X(vkCmdClearAttachments) \
    X(vkCmdClearColorImage) \
    X(vkCmdClearDepthStencilImage) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdEndRenderingKHR) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdResolveImage) \
    X(vkCmdSetCullModeEXT) \
    X(vkCmdSetDepthBias) \
    X(vkCmdSetDepthBiasEnableEXT) \
    X(vkCmdSetDepthCompareOpEXT) \
    X(vkCmdSetDepthTestEnableEXT) \
    X(vkCmdSetDepthWriteEnableEXT) \
    X(vkCmdSetFrontFaceEXT) \
    X(vkCmdSetLineWidth) \
    X(vkCmdSetScissor) \
    X(vkCmdSetStencilCompareMask) \
    X(vkCmdSetStencilOpEXT) \
    X(vkCmdSetStencilReference) \
    X(vkCmdSetStencilTestEnableEXT) \
    X(vkCmdSetStencilWriteMask) \
    X(vkCmdSetViewport) \
    X(vkCmdWriteTimestamp) \
    X(vkCmdBeginQuery) \
    X(vkCmdEndQuery) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateFence) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineCache) \
    X(vkCreatePipelineLayout) \
    X(vkCreateQueryPool) \
    X(vkCreateSampler) \
    X(vkCreateSemaphore) \
    X(vkCreateShaderModule) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyDevice) \
    X(vkDestroyFence) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineCache) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyQueryPool) \
    X(vkDestroySampler) \
    X(vkDestroySemaphore) \
    X(vkDestroyShaderModule) \
    X(vkDestroySwapchainKHR) \
    X(vkDeviceWaitIdle) \
    X(vkEndCommandBuffer) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceQueue) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetQueryPoolResults) \
    X(vkGetSwapchainImagesKHR) \
    X(vkMapMemory) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkResetCommandBuffer) \
    X(vkResetDescriptorPool) \
    X(vkResetFences) \
    X(vkResetQueryPoolEXT) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)

// Göstericiler "cq_" önekli: Vulkan API adlarıyla aynı adlı sembol tanımlanırsa ELF sembol araya girmesi
// (interposition) yükleyicinin kendi iç çağrılarını bu değişkenlere bağlar ve çöker. Motor kodu makrolarla
// aynı adları kullanmaya devam eder.
#define CQ_VK_DECLARE(name) extern PFN_##name cq_##name;
extern PFN_vkGetInstanceProcAddr cq_vkGetInstanceProcAddr;
CQ_VK_GLOBAL_FUNCTIONS(CQ_VK_DECLARE)
CQ_VK_INSTANCE_FUNCTIONS(CQ_VK_DECLARE)
CQ_VK_DEVICE_FUNCTIONS(CQ_VK_DECLARE)
#undef CQ_VK_DECLARE

#define vkGetInstanceProcAddr cq_vkGetInstanceProcAddr
#define vkCreateInstance cq_vkCreateInstance
#define vkEnumerateInstanceExtensionProperties cq_vkEnumerateInstanceExtensionProperties
#define vkEnumerateInstanceLayerProperties cq_vkEnumerateInstanceLayerProperties
#define vkEnumerateInstanceVersion cq_vkEnumerateInstanceVersion
#define vkCreateDebugUtilsMessengerEXT cq_vkCreateDebugUtilsMessengerEXT
#define vkCreateDevice cq_vkCreateDevice
#define vkDestroyDebugUtilsMessengerEXT cq_vkDestroyDebugUtilsMessengerEXT
#define vkDestroyInstance cq_vkDestroyInstance
#define vkDestroySurfaceKHR cq_vkDestroySurfaceKHR
#define vkEnumerateDeviceExtensionProperties cq_vkEnumerateDeviceExtensionProperties
#define vkEnumeratePhysicalDevices cq_vkEnumeratePhysicalDevices
#define vkGetDeviceProcAddr cq_vkGetDeviceProcAddr
#define vkGetPhysicalDeviceFeatures cq_vkGetPhysicalDeviceFeatures
#define vkGetPhysicalDeviceFormatProperties cq_vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceMemoryProperties cq_vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceProperties cq_vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties cq_vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR cq_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR cq_vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR cq_vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPhysicalDeviceSurfaceSupportKHR cq_vkGetPhysicalDeviceSurfaceSupportKHR
#define vkAcquireNextImageKHR cq_vkAcquireNextImageKHR
#define vkAllocateCommandBuffers cq_vkAllocateCommandBuffers
#define vkAllocateDescriptorSets cq_vkAllocateDescriptorSets
#define vkAllocateMemory cq_vkAllocateMemory
#define vkBeginCommandBuffer cq_vkBeginCommandBuffer
#define vkBindBufferMemory cq_vkBindBufferMemory
#define vkBindImageMemory cq_vkBindImageMemory
#define vkCmdBeginRenderingKHR cq_vkCmdBeginRenderingKHR
#define vkCmdBindDescriptorSets cq_vkCmdBindDescriptorSets
#define vkCmdBindIndexBuffer cq_vkCmdBindIndexBuffer
#define vkCmdBindPipeline cq_vkCmdBindPipeline
#define vkCmdBindVertexBuffers cq_vkCmdBindVertexBuffers
#define vkCmdBlitImage cq_vkCmdBlitImage
#define vkCmdClearAttachments cq_vkCmdClearAttachments
#define vkCmdClearColorImage cq_vkCmdClearColorImage
#define vkCmdClearDepthStencilImage cq_vkCmdClearDepthStencilImage
#define vkCmdCopyBufferToImage cq_vkCmdCopyBufferToImage
#define vkCmdCopyImage cq_vkCmdCopyImage
#define vkCmdCopyImageToBuffer cq_vkCmdCopyImageToBuffer
#define vkCmdDraw cq_vkCmdDraw
#define vkCmdDrawIndexed cq_vkCmdDrawIndexed
#define vkCmdEndRenderingKHR cq_vkCmdEndRenderingKHR
#define vkCmdPipelineBarrier cq_vkCmdPipelineBarrier
#define vkCmdResolveImage cq_vkCmdResolveImage
#define vkCmdSetCullModeEXT cq_vkCmdSetCullModeEXT
#define vkCmdSetDepthBias cq_vkCmdSetDepthBias
#define vkCmdSetDepthBiasEnableEXT cq_vkCmdSetDepthBiasEnableEXT
#define vkCmdSetDepthCompareOpEXT cq_vkCmdSetDepthCompareOpEXT
#define vkCmdSetDepthTestEnableEXT cq_vkCmdSetDepthTestEnableEXT
#define vkCmdSetDepthWriteEnableEXT cq_vkCmdSetDepthWriteEnableEXT
#define vkCmdSetFrontFaceEXT cq_vkCmdSetFrontFaceEXT
#define vkCmdSetLineWidth cq_vkCmdSetLineWidth
#define vkCmdSetScissor cq_vkCmdSetScissor
#define vkCmdSetStencilCompareMask cq_vkCmdSetStencilCompareMask
#define vkCmdSetStencilOpEXT cq_vkCmdSetStencilOpEXT
#define vkCmdSetStencilReference cq_vkCmdSetStencilReference
#define vkCmdSetStencilTestEnableEXT cq_vkCmdSetStencilTestEnableEXT
#define vkCmdSetStencilWriteMask cq_vkCmdSetStencilWriteMask
#define vkCmdSetViewport cq_vkCmdSetViewport
#define vkCmdWriteTimestamp cq_vkCmdWriteTimestamp
#define vkCmdBeginQuery cq_vkCmdBeginQuery
#define vkCmdEndQuery cq_vkCmdEndQuery
#define vkCreateBuffer cq_vkCreateBuffer
#define vkCreateCommandPool cq_vkCreateCommandPool
#define vkCreateDescriptorPool cq_vkCreateDescriptorPool
#define vkCreateDescriptorSetLayout cq_vkCreateDescriptorSetLayout
#define vkCreateFence cq_vkCreateFence
#define vkCreateGraphicsPipelines cq_vkCreateGraphicsPipelines
#define vkCreateImage cq_vkCreateImage
#define vkCreateImageView cq_vkCreateImageView
#define vkCreatePipelineCache cq_vkCreatePipelineCache
#define vkCreatePipelineLayout cq_vkCreatePipelineLayout
#define vkCreateQueryPool cq_vkCreateQueryPool
#define vkCreateSampler cq_vkCreateSampler
#define vkCreateSemaphore cq_vkCreateSemaphore
#define vkCreateShaderModule cq_vkCreateShaderModule
#define vkCreateSwapchainKHR cq_vkCreateSwapchainKHR
#define vkDestroyBuffer cq_vkDestroyBuffer
#define vkDestroyCommandPool cq_vkDestroyCommandPool
#define vkDestroyDescriptorPool cq_vkDestroyDescriptorPool
#define vkDestroyDescriptorSetLayout cq_vkDestroyDescriptorSetLayout
#define vkDestroyDevice cq_vkDestroyDevice
#define vkDestroyFence cq_vkDestroyFence
#define vkDestroyImage cq_vkDestroyImage
#define vkDestroyImageView cq_vkDestroyImageView
#define vkDestroyPipeline cq_vkDestroyPipeline
#define vkDestroyPipelineCache cq_vkDestroyPipelineCache
#define vkDestroyPipelineLayout cq_vkDestroyPipelineLayout
#define vkDestroyQueryPool cq_vkDestroyQueryPool
#define vkDestroySampler cq_vkDestroySampler
#define vkDestroySemaphore cq_vkDestroySemaphore
#define vkDestroyShaderModule cq_vkDestroyShaderModule
#define vkDestroySwapchainKHR cq_vkDestroySwapchainKHR
#define vkDeviceWaitIdle cq_vkDeviceWaitIdle
#define vkEndCommandBuffer cq_vkEndCommandBuffer
#define vkFreeMemory cq_vkFreeMemory
#define vkGetBufferMemoryRequirements cq_vkGetBufferMemoryRequirements
#define vkGetDeviceQueue cq_vkGetDeviceQueue
#define vkGetImageMemoryRequirements cq_vkGetImageMemoryRequirements
#define vkGetQueryPoolResults cq_vkGetQueryPoolResults
#define vkGetSwapchainImagesKHR cq_vkGetSwapchainImagesKHR
#define vkMapMemory cq_vkMapMemory
#define vkQueuePresentKHR cq_vkQueuePresentKHR
#define vkQueueSubmit cq_vkQueueSubmit
#define vkResetCommandBuffer cq_vkResetCommandBuffer
#define vkResetDescriptorPool cq_vkResetDescriptorPool
#define vkResetFences cq_vkResetFences
#define vkResetQueryPoolEXT cq_vkResetQueryPoolEXT
#define vkUpdateDescriptorSets cq_vkUpdateDescriptorSets
#define vkWaitForFences cq_vkWaitForFences

namespace Conqueror::VK
{
    // Yükleyici kitaplığını aç ve küresel işlevleri çöz (false: Vulkan çalışma zamanı yok)
    bool OpenRuntime(std::string& error);
    void LoadInstanceFunctions(VkInstance instance);
    void LoadDeviceFunctions(VkDevice device);   // sürücüye doğrudan göstericiler (yükleyici sıçraması yok)
    void CloseRuntime();
}

#endif
