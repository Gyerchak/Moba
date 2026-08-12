#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

#include <vector>
#include <cstdint>
#include <string>

// Minimal Vulkan abstraction for rendering instanced colored quads on a
// top-down plane. Handles the full pipeline and per-frame drawing.
class VulkanContext {
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Draw instances. World space is XZ (y=0). viewProj is a column-major mat4.
    // positions: pairs of (x, z); colors: RGBA per instance; sizes: per instance.
    void draw(const float viewProj[16],
              const float* positions,
              const float* colors,
              const float* sizes,
              uint32_t instanceCount);

    void recreateSwapchain();

private:
    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createUniformBuffers();
    void createPipelines();
    void createSyncObjects();
    void createUnitResources();
    void createFramebufferResources();
    void cleanupSwapchain();
    void cleanup();
    void recordCommandBuffer(uint32_t imageIndex);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = UINT32_MAX;
    uint32_t presentFamily_ = UINT32_MAX;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer mvpBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory mvpMemory_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet mvpDescriptorSet_ = VK_NULL_HANDLE;

    // Unit quad vertex + index buffers
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;

    // Dynamic instance buffer (positions + colors)
    VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory_ = VK_NULL_HANDLE;
    VkDeviceSize instanceCapacity_ = 0;
    uint32_t instanceCount_ = 0;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;

    VkSurfaceCapabilitiesKHR surfaceCapabilities_{};
};
