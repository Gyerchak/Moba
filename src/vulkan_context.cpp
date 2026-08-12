#include "vulkan_context.hpp"

#include "shader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
VkInstance g_instance = VK_NULL_HANDLE;  // not used; kept for clarity
}

static constexpr uint32_t kMaxInstances = 4096;

namespace {
// Locate a SPIR-V file relative to several plausible working directories.
std::vector<char> loadSpv(const std::string& name) {
    const std::vector<std::string> candidates = {
        "shaders/" + name,
        "../shaders/" + name,
        "Moba/shaders/" + name,
        "../Moba/shaders/" + name,
    };
    std::string lastError = "shaders/" + name;
    for (const auto& p : candidates) {
        try {
            return readFile(p);
        } catch (const std::exception&) {
            lastError = p;
        }
    }
    throw std::runtime_error("failed to open shader file: " + lastError);
}
}  // namespace

VulkanContext::VulkanContext(GLFWwindow* window) {
    createInstance();
    createSurface(window);

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance_, &gpuCount, nullptr);
    if (gpuCount == 0) throw std::runtime_error("no Vulkan GPU found");
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance_, &gpuCount, gpus.data());
    physicalDevice_ = gpus[0];
    pickPhysicalDevice();

    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createCommandPool();
    createUniformBuffers();
    createPipelines();
    createFramebuffers();
    createUnitResources();
    createSyncObjects();
}

VulkanContext::~VulkanContext() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        cleanup();
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanContext::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Moba";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "MobaEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);

    std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = 0;

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance");
    }
}

void VulkanContext::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueCount, families.data());

    bool found = false;
    for (uint32_t i = 0; i < queueCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &present);
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            graphicsFamily_ = i;
            presentFamily_ = i;
            found = true;
            break;
        }
    }
    if (!found) {
        // Separate present family fallback
        for (uint32_t i = 0; i < queueCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsFamily_ = i;
            VkBool32 p = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &p);
            if (p) presentFamily_ = i;
        }
    }
    if (graphicsFamily_ == UINT32_MAX || presentFamily_ == UINT32_MAX) {
        throw std::runtime_error("no suitable queue family");
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &surfaceCapabilities_);
}

void VulkanContext::createLogicalDevice() {
    std::set<uint32_t> unique = {graphicsFamily_, presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float priority = 1.0f;
    for (uint32_t f : unique) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = f;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    di.pQueueCreateInfos = queueInfos.data();
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = devExts;
    di.pEnabledFeatures = &features;

    if (vkCreateDevice(physicalDevice_, &di, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

void VulkanContext::createSwapchain() {
    VkSurfaceFormatKHR surfaceFormat{};
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    surfaceFormat = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = f;
            break;
        }
    }
    swapchainFormat_ = surfaceFormat.format;

    if (surfaceCapabilities_.currentExtent.width != UINT32_MAX) {
        extent_ = surfaceCapabilities_.currentExtent;
    } else {
        extent_.width = std::max(surfaceCapabilities_.minImageExtent.width,
                                 std::min(1280u, surfaceCapabilities_.maxImageExtent.width));
        extent_.height = std::max(surfaceCapabilities_.minImageExtent.height,
                                  std::min(720u, surfaceCapabilities_.maxImageExtent.height));
    }

    uint32_t imageCount = surfaceCapabilities_.minImageCount + 1;
    if (surfaceCapabilities_.maxImageCount > 0 && imageCount > surfaceCapabilities_.maxImageCount) {
        imageCount = surfaceCapabilities_.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = imageCount;
    sci.imageFormat = swapchainFormat_;
    sci.imageColorSpace = surfaceFormat.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform = surfaceCapabilities_.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;

    if (graphicsFamily_ != presentFamily_) {
        uint32_t q[2] = {graphicsFamily_, presentFamily_};
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = q;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());
}

void VulkanContext::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = swapchainImages_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = swapchainFormat_;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &ci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view");
        }
    }
}

void VulkanContext::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;

    if (vkCreateRenderPass(device_, &rp, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass");
    }
}

void VulkanContext::createFramebuffers() {
    swapchainFramebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &swapchainImageViews_[i];
        fi.width = extent_.width;
        fi.height = extent_.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(device_, &fi, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = graphicsFamily_;
    if (vkCreateCommandPool(device_, &ci, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &ai, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer");
    }
}

static VkDeviceMemory allocateMemory(VkDevice device, VkPhysicalDevice phys,
                                     VkBuffer buffer, VkMemoryPropertyFlags props,
                                     uint32_t& memTypeIndex) {
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            memTypeIndex = i;
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = i;
            VkDeviceMemory mem;
            if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate buffer memory");
            }
            return mem;
        }
    }
    throw std::runtime_error("no suitable memory type");
}

void VulkanContext::createUniformBuffers() {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sizeof(float) * 16;
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &mvpBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mvp buffer");
    }
    uint32_t memIdx = 0;
    mvpMemory_ = allocateMemory(device_, physicalDevice_, mvpBuffer_,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                memIdx);
    vkBindBufferMemory(device_, mvpBuffer_, mvpMemory_, 0);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &poolSize;
    pi.maxSets = 1;
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(device_, &ai, &mvpDescriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = mvpBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(float) * 16;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = mvpDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void VulkanContext::createPipelines() {
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descriptorSetLayout_;
    if (vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    auto vert = loadSpv("instance.vert.spv");
    auto frag = loadSpv("instance.frag.spv");

    VkShaderModuleCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vci.codeSize = vert.size();
    vci.pCode = reinterpret_cast<const uint32_t*>(vert.data());
    VkShaderModule vertModule;
    if (vkCreateShaderModule(device_, &vci, nullptr, &vertModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vert shader module");
    }
    vci.codeSize = frag.size();
    vci.pCode = reinterpret_cast<const uint32_t*>(frag.data());
    VkShaderModule fragModule;
    if (vkCreateShaderModule(device_, &vci, nullptr, &fragModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create frag shader module");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription perVertex{};
    perVertex.binding = 0;
    perVertex.stride = sizeof(float) * 2;
    perVertex.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttr{};
    vertexAttr.location = 0;
    vertexAttr.binding = 0;
    vertexAttr.format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttr.offset = 0;

    VkVertexInputBindingDescription perInstance{};
    perInstance.binding = 1;
    perInstance.stride = sizeof(float) * 7;  // vec2 pos + vec4 color + float size
    perInstance.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription instanceAttr[3]{};
    instanceAttr[0].location = 1;
    instanceAttr[0].binding = 1;
    instanceAttr[0].format = VK_FORMAT_R32G32_SFLOAT;
    instanceAttr[0].offset = 0;
    instanceAttr[1].location = 2;
    instanceAttr[1].binding = 1;
    instanceAttr[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    instanceAttr[1].offset = sizeof(float) * 2;
    instanceAttr[2].location = 3;
    instanceAttr[2].binding = 1;
    instanceAttr[2].format = VK_FORMAT_R32_SFLOAT;
    instanceAttr[2].offset = sizeof(float) * 6;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = &perVertex;
    // note: vertexBindingDescriptions must point to an array
    VkVertexInputBindingDescription vertBindings[2] = {perVertex, perInstance};
    VkVertexInputAttributeDescription attrs[4] = {vertexAttr, instanceAttr[0], instanceAttr[1], instanceAttr[2]};
    vi.pVertexBindingDescriptions = vertBindings;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent_;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.pViewports = &viewport;
    vs.scissorCount = 1;
    vs.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vs;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.layout = pipelineLayout_;
    pi.renderPass = renderPass_;
    pi.subpass = 0;
    pi.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
}

void VulkanContext::createUnitResources() {
    // One unit quad centered at origin: [-0.5, 0.5]^2 (XZ plane)
    const float vertices[4][2] = {
        {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sizeof(vertices);
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &vertexBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer");
    }
    uint32_t memIdx = 0;
    vertexMemory_ = allocateMemory(device_, physicalDevice_, vertexBuffer_,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   memIdx);
    vkBindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0);
    void* data = nullptr;
    vkMapMemory(device_, vertexMemory_, 0, sizeof(vertices), 0, &data);
    std::memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device_, vertexMemory_);

    bi.size = sizeof(indices);
    bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bi, nullptr, &indexBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer");
    }
    indexMemory_ = allocateMemory(device_, physicalDevice_, indexBuffer_,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  memIdx);
    vkBindBufferMemory(device_, indexBuffer_, indexMemory_, 0);
    vkMapMemory(device_, indexMemory_, 0, sizeof(indices), 0, &data);
    std::memcpy(data, indices, sizeof(indices));
    vkUnmapMemory(device_, indexMemory_);

    instanceCapacity_ = kMaxInstances * sizeof(float) * 7;
    bi.size = instanceCapacity_;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bi, nullptr, &instanceBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance buffer");
    }
    instanceMemory_ = allocateMemory(device_, physicalDevice_, instanceBuffer_,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     memIdx);
    vkBindBufferMemory(device_, instanceBuffer_, instanceMemory_, 0);
}

void VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device_, &si, nullptr, &imageAvailable_) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &si, nullptr, &renderFinished_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphores");
    }
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_, &fi, nullptr, &inFlight_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fence");
    }
}

void VulkanContext::recordCommandBuffer(uint32_t imageIndex) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkClearValue clear{};
    clear.color = {{0.05f, 0.07f, 0.09f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_;
    rp.framebuffer = swapchainFramebuffers_[imageIndex];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkBeginCommandBuffer(commandBuffer_, &bi);
    vkCmdBeginRenderPass(commandBuffer_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &mvpDescriptorSet_, 0, nullptr);
    VkBuffer vb[] = {vertexBuffer_, instanceBuffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer_, 0, 2, vb, offsets);
    vkCmdBindIndexBuffer(commandBuffer_, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer_, 6, instanceCount_, 0, 0, 0);
    vkCmdEndRenderPass(commandBuffer_);
    vkEndCommandBuffer(commandBuffer_);
}

void VulkanContext::draw(const float viewProj[16],
                         const float* positions, const float* colors,
                         const float* sizes,
                         uint32_t instanceCount) {
    if (instanceCount == 0 || instanceCount > kMaxInstances) instanceCount = 0;

    vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlight_);

    uint32_t imageIndex = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                       imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
        return;
    }
    if (r != VK_SUCCESS) return;
    instanceCount_ = instanceCount;

    // Upload MVP
    void* data = nullptr;
    vkMapMemory(device_, mvpMemory_, 0, sizeof(float) * 16, 0, &data);
    std::memcpy(data, viewProj, sizeof(float) * 16);
    vkUnmapMemory(device_, mvpMemory_);

    // Upload instances
    if (instanceCount > 0) {
        size_t bytes = static_cast<size_t>(instanceCount) * sizeof(float) * 7;
        vkMapMemory(device_, instanceMemory_, 0, bytes, 0, &data);
        for (uint32_t i = 0; i < instanceCount; ++i) {
            float* dst = static_cast<float*>(data) + i * 7;
            dst[0] = positions[i * 2];
            dst[1] = positions[i * 2 + 1];
            dst[2] = colors[i * 4];
            dst[3] = colors[i * 4 + 1];
            dst[4] = colors[i * 4 + 2];
            dst[5] = colors[i * 4 + 3];
            dst[6] = sizes[i];
        }
        vkUnmapMemory(device_, instanceMemory_);
    }

    vkResetCommandBuffer(commandBuffer_, 0);
    recordCommandBuffer(imageIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailable_;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &commandBuffer_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_;
    if (vkQueueSubmit(graphicsQueue_, 1, &si, inFlight_) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit queue");
    }

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex;
    VkResult pr = vkQueuePresentKHR(presentQueue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
}

void VulkanContext::recreateSwapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(glfwGetCurrentContext(), &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device_);

    for (auto f : swapchainFramebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    swapchainFramebuffers_.clear();
    for (auto iv : swapchainImageViews_) vkDestroyImageView(device_, iv, nullptr);
    swapchainImageViews_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    createSwapchain();
    createImageViews();
    createFramebuffers();
}

void VulkanContext::cleanupSwapchain() {}

void VulkanContext::cleanup() {
    vkDestroyFence(device_, inFlight_, nullptr);
    vkDestroySemaphore(device_, imageAvailable_, nullptr);
    vkDestroySemaphore(device_, renderFinished_, nullptr);
    vkFreeMemory(device_, instanceMemory_, nullptr);
    vkDestroyBuffer(device_, instanceBuffer_, nullptr);
    vkFreeMemory(device_, indexMemory_, nullptr);
    vkDestroyBuffer(device_, indexBuffer_, nullptr);
    vkFreeMemory(device_, vertexMemory_, nullptr);
    vkDestroyBuffer(device_, vertexBuffer_, nullptr);
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    vkFreeMemory(device_, mvpMemory_, nullptr);
    vkDestroyBuffer(device_, mvpBuffer_, nullptr);
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    for (auto f : swapchainFramebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    for (auto iv : swapchainImageViews_) vkDestroyImageView(device_, iv, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
}
