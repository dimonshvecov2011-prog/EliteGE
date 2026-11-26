#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE namespace {
    constexpr uint32_t WIDTH = 1280, HEIGHT = 720;
    constexpr size_t FRAMES = 2;
#ifdef NDEBUG
    constexpr bool VALIDATION = false;
#else
    constexpr bool VALIDATION = true;
#endif
    const std::vector<const char*> LAYERS = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> EXTENSIONS = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            std::cerr << "[Vulkan] " << data->pMessage << '\n';
        return VK_FALSE;
    }
    struct Families {
        std::optional<uint32_t> graphics, present;
        bool complete() const {
            return graphics.has_value() && present.has_value();
        }
    };
    struct Vertex {
        float position[2];
        float color[3];
        float uv[2];
    };
    struct Transform2D {
        glm::vec2 position{0.0f};
        glm::vec2 scale{1.0f};
        float rotation = 0.0f;
        int layer = 0;
    };
    struct SpriteObject {
        uint32_t id = 0;
        Transform2D transform;
    };
    const std::array<Vertex, 6> QUAD = {
        Vertex{{-0.55f, -0.55f}, {1, 1, 1}, {0, 1}}, Vertex{{0.55f, -0.55f}, {1, 1, 1}, {1, 1}},
        Vertex{{0.55f, 0.55f}, {1, 1, 1}, {1, 0}},   Vertex{{-0.55f, -0.55f}, {1, 1, 1}, {0, 1}},
        Vertex{{0.55f, 0.55f}, {1, 1, 1}, {1, 0}},   Vertex{{-0.55f, 0.55f}, {1, 1, 1}, {0, 0}}};
    class Camera2D {
       public:
        glm::vec2 position{0.0f};
        float zoom = 1.0f;
        float viewHeight = 2.0f;
        glm::mat4 projection(uint32_t width, uint32_t height) const {
            const float aspect = height == 0 ? 1.0f : static_cast<float>(width) / height;
            const float halfHeight = viewHeight / (2.0f * zoom);
            const float halfWidth = halfHeight * aspect;
            glm::mat4 result = glm::orthoRH_ZO(-halfWidth + position.x, halfWidth + position.x,
                                               -halfHeight + position.y, halfHeight + position.y, -1.0f, 1.0f);
            // Vulkan NDC Y points down; keep world +Y pointing upward.
            result[1][1] *= -1.0f;
            return result;
        }
    };
}
class VulkanApp {
   public:
    ~VulkanApp() {
        cleanup();
    }
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
    }

   private:
    GLFWwindow* window = nullptr;
    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT debugMessenger;
    vk::SurfaceKHR surface{};
    vk::PhysicalDevice physicalDevice{};
    vk::UniqueDevice device;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vk::Queue graphicsQueue{}, presentQueue{};
    uint32_t graphicsFamily = 0, presentFamily = 0;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapchainImages;
    vk::Format swapchainFormat{};
    vk::Extent2D swapchainExtent{};
    std::vector<vk::UniqueImageView> imageViews;
    vk::Format depthFormat{};
    vk::UniqueImage depthImage;
    vk::UniqueDeviceMemory depthMemory;
    vk::UniqueImageView depthView;
    vk::UniqueRenderPass renderPass;
    std::vector<vk::UniqueFramebuffer> framebuffers;
    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
    vk::UniqueBuffer vertexBuffer;
    vk::UniqueDeviceMemory vertexMemory;
    vk::UniqueBuffer stagingBuffer;
    vk::UniqueDeviceMemory stagingMemory;
    vk::UniqueImage textureImage;
    vk::UniqueDeviceMemory textureMemory;
    vk::UniqueImageView textureView;
    vk::UniqueSampler textureSampler;
    std::array<VkBuffer, FRAMES> uniformBuffers{};
    std::array<VmaAllocation, FRAMES> uniformAllocations{};
    vk::UniqueDescriptorSetLayout descriptorLayout;
    vk::UniqueDescriptorPool descriptorPool;
    std::array<vk::DescriptorSet, FRAMES> descriptorSets{};
    vk::UniqueShaderModule vertexShader, fragmentShader;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;
    std::array<vk::UniqueSemaphore, FRAMES> imageAvailable;
    std::vector<vk::UniqueSemaphore> renderFinished;
    std::array<vk::UniqueFence, FRAMES> fences;
    Camera2D camera;
    std::vector<SpriteObject> objects;
    size_t currentFrame = 0;
    bool resized = false;
    bool validationEnabled = false;
    void initWindow() {
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "EliteGE Engine", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("GLFW window creation failed");
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
            static_cast<VulkanApp*>(glfwGetWindowUserPointer(w))->resized = true;
        });
    }
    void initVulkan() {
        createInstance();
        createDebugMessenger();
        if (glfwCreateWindowSurface(*instance, window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&surface)) !=
            VK_SUCCESS)
            throw std::runtime_error("Surface creation failed");
        pickPhysicalDevice();
        createDevice();
        createAllocator();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createCommandPool();
        createTextureResources();
        createCameraResources();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        allocateCommandBuffers();
        createVertexBuffer();
        createSceneObjects();
        createSyncObjects();
    }
    static bool hasLayers() {
        auto available = vk::enumerateInstanceLayerProperties();
        return std::all_of(LAYERS.begin(), LAYERS.end(), [&](const char* wanted) {
            return std::any_of(available.begin(), available.end(),
                               [&](const auto& layer) { return std::strcmp(layer.layerName, wanted) == 0; });
        });
    }
    void createInstance() {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
        validationEnabled = VALIDATION && hasLayers();
        if (VALIDATION && !validationEnabled)
            std::cerr << "Validation layer not found, continuing without it\n";
        uint32_t count = 0;
        auto required = glfwGetRequiredInstanceExtensions(&count);
        if (!required)
            throw std::runtime_error("GLFW Vulkan extensions unavailable");
        std::vector<const char*> extensions(required, required + count);
        if (validationEnabled)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        vk::ApplicationInfo app("EliteGE Engine", 1, "EliteGE", 1, VK_API_VERSION_1_0);
        vk::InstanceCreateInfo info({}, &app, validationEnabled ? uint32_t(LAYERS.size()) : 0,
                                    validationEnabled ? LAYERS.data() : nullptr, uint32_t(extensions.size()),
                                    extensions.data());
        instance = vk::createInstanceUnique(info);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    }
    void createDebugMessenger() {
        if (!validationEnabled)
            return;
        vk::DebugUtilsMessengerCreateInfoEXT info{};
        info.messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        info.pfnUserCallback = reinterpret_cast<decltype(info.pfnUserCallback)>(debugCallback);
        debugMessenger = instance->createDebugUtilsMessengerEXTUnique(info);
    }
    Families findFamilies(vk::PhysicalDevice gpu) const {
        Families families;
        auto properties = gpu.getQueueFamilyProperties();
        for (uint32_t i = 0; i < properties.size(); ++i) {
            if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
                families.graphics = i;
            if (gpu.getSurfaceSupportKHR(i, surface))
                families.present = i;
            if (families.complete())
                break;
        }
        return families;
    }
    bool supports(vk::PhysicalDevice gpu) const {
        auto available = gpu.enumerateDeviceExtensionProperties();
        return std::all_of(EXTENSIONS.begin(), EXTENSIONS.end(), [&](const char* wanted) {
            return std::any_of(available.begin(), available.end(), [&](const auto& extension) {
                return std::strcmp(extension.extensionName, wanted) == 0;
            });
        });
    }
    static int deviceTypePriority(vk::PhysicalDeviceType type) {
        switch (type) {
            case vk::PhysicalDeviceType::eDiscreteGpu:
                return 4;
            case vk::PhysicalDeviceType::eIntegratedGpu:
                return 3;
            case vk::PhysicalDeviceType::eVirtualGpu:
                return 2;
            case vk::PhysicalDeviceType::eCpu:
                return 1;
            default:
                return 0;
        }
    }
    static vk::DeviceSize deviceLocalMemory(vk::PhysicalDevice gpu) {
        vk::DeviceSize total = 0;
        auto memory = gpu.getMemoryProperties();
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if (memory.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
                total += memory.memoryHeaps[i].size;
        }
        return total;
    }
    void pickPhysicalDevice() {
        int bestTypePriority = -1;
        vk::DeviceSize bestLocalMemory = 0;
        bool found = false;
        for (auto gpu : instance->enumeratePhysicalDevices()) {
            if (supports(gpu) && findFamilies(gpu).complete()) {
                auto properties = gpu.getProperties();
                int typePriority = deviceTypePriority(properties.deviceType);
                vk::DeviceSize localMemory = deviceLocalMemory(gpu);
                if (!found || typePriority > bestTypePriority ||
                    (typePriority == bestTypePriority && localMemory > bestLocalMemory)) {
                    physicalDevice = gpu;
                    bestTypePriority = typePriority;
                    bestLocalMemory = localMemory;
                    found = true;
                }
            }
        }
        if (!found)
            throw std::runtime_error("No suitable Vulkan GPU found");
        auto selected = physicalDevice.getProperties();
        std::cerr << "Selected GPU: " << selected.deviceName << '\n';
    }
    void createDevice() {
        auto families = findFamilies(physicalDevice);
        graphicsFamily = *families.graphics;
        presentFamily = *families.present;
        std::set<uint32_t> unique = {graphicsFamily, presentFamily};
        float priority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queues;
        for (auto family : unique) queues.emplace_back(vk::DeviceQueueCreateFlags{}, family, 1, &priority);
        vk::PhysicalDeviceFeatures features{};
        device = physicalDevice.createDeviceUnique({{}, queues, {}, EXTENSIONS, &features});
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
        graphicsQueue = device->getQueue(graphicsFamily, 0);
        presentQueue = device->getQueue(presentFamily, 0);
    }
    void createAllocator() {
        VmaAllocatorCreateInfo info{};
        info.physicalDevice = static_cast<VkPhysicalDevice>(physicalDevice);
        info.device = static_cast<VkDevice>(*device);
        info.instance = static_cast<VkInstance>(*instance);
        info.vulkanApiVersion = VK_API_VERSION_1_0;
        if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS)
            throw std::runtime_error("VMA allocator creation failed");
    }
    vk::SurfaceFormatKHR chooseFormat(const std::vector<vk::SurfaceFormatKHR>& formats) const {
        for (auto format : formats)
            if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                return format;
        return formats.front();
    }
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        return {std::clamp(uint32_t(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(uint32_t(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
    }
    void createSwapchain() {
        auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        auto format = chooseFormat(physicalDevice.getSurfaceFormatsKHR(surface));
        uint32_t count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount && count > capabilities.maxImageCount)
            count = capabilities.maxImageCount;
        swapchainExtent = chooseExtent(capabilities);
        vk::SwapchainCreateInfoKHR info({}, surface, count, format.format, format.colorSpace, swapchainExtent, 1,
                                        vk::ImageUsageFlagBits::eColorAttachment);
        uint32_t families[] = {graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily)
            info.setImageSharingMode(vk::SharingMode::eConcurrent).setQueueFamilyIndices(families);
        info.setPreTransform(capabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(vk::PresentModeKHR::eFifo)
            .setClipped(true);
        swapchain = device->createSwapchainKHRUnique(info);
        swapchainImages = device->getSwapchainImagesKHR(*swapchain);
        swapchainFormat = format.format;
    }
    void createImageViews() {
        for (auto image : swapchainImages)
            imageViews.push_back(device->createImageViewUnique({{},
                                                                image,
                                                                vk::ImageViewType::e2D,
                                                                swapchainFormat,
                                                                {},
                                                                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }
    void createDepthResources() {
        depthFormat = vk::Format::eD32Sfloat;
        vk::ImageCreateInfo info({}, vk::ImageType::e2D, depthFormat,
                                 {swapchainExtent.width, swapchainExtent.height, 1}, 1, 1, vk::SampleCountFlagBits::e1,
                                 vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment);
        depthImage = device->createImageUnique(info);
        auto requirements = device->getImageMemoryRequirements(*depthImage);
        depthMemory = device->allocateMemoryUnique(
            {requirements.size, memoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
        device->bindImageMemory(*depthImage, *depthMemory, 0);
        depthView = device->createImageViewUnique(
            {{}, *depthImage, vk::ImageViewType::e2D, depthFormat, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}});
    }
    void createRenderPass() {
        vk::AttachmentDescription color({}, swapchainFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear,
                                        vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
                                        vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::ePresentSrcKHR);
        vk::AttachmentDescription depth({}, depthFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear,
                                        vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare,
                                        vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::AttachmentDescription attachments[] = {color, depth};
        vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, nullptr, 1, &colorRef, nullptr,
                                       &depthRef);
        renderPass = device->createRenderPassUnique({{}, 2, attachments, 1, &subpass});
    }
    std::vector<uint32_t> readShader(const char* name) {
        std::ifstream file(std::string(ELITEGE_SHADER_DIR) + "/" + name, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error(std::string("Shader missing: ") + name);
        auto size = file.tellg();
        std::vector<uint32_t> code(size / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), size);
        return code;
    }
    vk::UniqueShaderModule shader(const std::vector<uint32_t>& code) {
        return device->createShaderModuleUnique({{}, code});
    }
    void createTextureResources() {
        int width = 0, height = 0, channels = 0;
        std::string path = std::string(ELITEGE_ASSET_DIR) + "/sprite.png";
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!pixels)
            throw std::runtime_error("Could not load texture: " + path);
        vk::DeviceSize bytes = vk::DeviceSize(width) * height * 4;
        stagingBuffer =
            device->createBufferUnique({{}, bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive});
        auto requirements = device->getBufferMemoryRequirements(*stagingBuffer);
        stagingMemory = device->allocateMemoryUnique(
            {requirements.size,
             memoryType(requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)});
        device->bindBufferMemory(*stagingBuffer, *stagingMemory, 0);
        void* mapped = device->mapMemory(*stagingMemory, 0, bytes);
        std::memcpy(mapped, pixels, bytes);
        device->unmapMemory(*stagingMemory);
        stbi_image_free(pixels);
        textureImage =
            device->createImageUnique({{},
                                       vk::ImageType::e2D,
                                       vk::Format::eR8G8B8A8Srgb,
                                       {uint32_t(width), uint32_t(height), 1},
                                       1,
                                       1,
                                       vk::SampleCountFlagBits::e1,
                                       vk::ImageTiling::eOptimal,
                                       vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled});
        auto imageRequirements = device->getImageMemoryRequirements(*textureImage);
        textureMemory = device->allocateMemoryUnique(
            {imageRequirements.size,
             memoryType(imageRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
        device->bindImageMemory(*textureImage, *textureMemory, 0);
        auto commands = device->allocateCommandBuffersUnique({*commandPool, vk::CommandBufferLevel::ePrimary, 1});
        auto& cmd = commands[0];
        vk::CommandBufferBeginInfo uploadBegin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        (void)cmd->begin(uploadBegin);
        vk::ImageMemoryBarrier toTransfer({}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined,
                                          vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED,
                                          VK_QUEUE_FAMILY_IGNORED, *textureImage,
                                          {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr,
                             nullptr, toTransfer);
        vk::BufferImageCopy copy(0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0},
                                 {uint32_t(width), uint32_t(height), 1});
        cmd->copyBufferToImage(*stagingBuffer, *textureImage, vk::ImageLayout::eTransferDstOptimal, copy);
        vk::ImageMemoryBarrier toShader(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                                        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *textureImage,
                                        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
                             nullptr, nullptr, toShader);
        cmd->end();
        vk::CommandBuffer uploadCommand = cmd.get();
        vk::SubmitInfo submit({}, nullptr, nullptr, 1, &uploadCommand, {}, nullptr);
        graphicsQueue.submit(submit, {});
        graphicsQueue.waitIdle();
        textureView = device->createImageViewUnique({{},
                                                     *textureImage,
                                                     vk::ImageViewType::e2D,
                                                     vk::Format::eR8G8B8A8Srgb,
                                                     {},
                                                     {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        textureSampler = device->createSamplerUnique({{},
                                                      vk::Filter::eNearest,
                                                      vk::Filter::eNearest,
                                                      vk::SamplerMipmapMode::eNearest,
                                                      vk::SamplerAddressMode::eRepeat,
                                                      vk::SamplerAddressMode::eRepeat,
                                                      vk::SamplerAddressMode::eRepeat,
                                                      0,
                                                      false,
                                                      1,
                                                      false,
                                                      vk::CompareOp::eAlways,
                                                      0,
                                                      0,
                                                      vk::BorderColor::eIntOpaqueBlack,
                                                      false});
    }
    void updateCamera() {
        glm::mat4 projection = camera.projection(swapchainExtent.width, swapchainExtent.height);
        for (size_t i = 0; i < FRAMES; ++i) {
            void* mapped = nullptr;
            if (vmaMapMemory(allocator, uniformAllocations[i], &mapped) != VK_SUCCESS)
                throw std::runtime_error("Could not map camera uniform buffer");
            std::memcpy(mapped, &projection, sizeof(projection));
            vmaUnmapMemory(allocator, uniformAllocations[i]);
            vmaFlushAllocation(allocator, uniformAllocations[i], 0, sizeof(projection));
        }
    }
    void createCameraResources() {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(glm::mat4);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        for (size_t i = 0; i < FRAMES; ++i) {
            if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &uniformBuffers[i], &uniformAllocations[i],
                                nullptr) != VK_SUCCESS)
                throw std::runtime_error("Could not create camera uniform buffer");
        }
        vk::DescriptorSetLayoutBinding bindings[] = {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1,
                                           vk::ShaderStageFlagBits::eFragment)};
        descriptorLayout = device->createDescriptorSetLayoutUnique({{}, 2, bindings});
        vk::DescriptorPoolSize sizes[] = {{vk::DescriptorType::eUniformBuffer, uint32_t(FRAMES)},
                                          {vk::DescriptorType::eCombinedImageSampler, uint32_t(FRAMES)}};
        descriptorPool = device->createDescriptorPoolUnique({{}, uint32_t(FRAMES), 2, sizes});
        vk::DescriptorSetLayout layout = descriptorLayout.get();
        std::array<vk::DescriptorSetLayout, FRAMES> layouts = {layout, layout};
        auto sets = device->allocateDescriptorSets({*descriptorPool, layouts});
        for (size_t i = 0; i < FRAMES; ++i) {
            descriptorSets[i] = sets[i];
            vk::DescriptorBufferInfo uniformInfo(vk::Buffer(uniformBuffers[i]), 0, sizeof(glm::mat4));
            vk::DescriptorImageInfo imageInfo(*textureSampler, *textureView, vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet writes[] = {
                {descriptorSets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformInfo},
                {descriptorSets[i], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo}};
            device->updateDescriptorSets(2, writes, 0, nullptr);
        }
        updateCamera();
    }
    void createGraphicsPipeline() {
        auto vertexCode = readShader("quad.vert.spv"), fragmentCode = readShader("quad.frag.spv");
        vertexShader = shader(vertexCode);
        fragmentShader = shader(fragmentCode);
        vk::PipelineShaderStageCreateInfo stages[2] = {
            {{}, vk::ShaderStageFlagBits::eVertex, *vertexShader, "main"},
            {{}, vk::ShaderStageFlagBits::eFragment, *fragmentShader, "main"}};
        vk::VertexInputBindingDescription binding(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
        vk::VertexInputAttributeDescription attributes[] = {
            {0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, position)},
            {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)},
            {2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)}};
        vk::PipelineVertexInputStateCreateInfo input({}, 1, &binding, 3, attributes);
        vk::PipelineInputAssemblyStateCreateInfo assembly({}, vk::PrimitiveTopology::eTriangleList, false);
        vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);
        vk::PipelineRasterizationStateCreateInfo raster({}, false, false, vk::PolygonMode::eFill,
                                                        vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise, false,
                                                        0, 0, 0, 1);
        vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
        vk::PipelineColorBlendAttachmentState blend(
            true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
            vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::ColorComponentFlags(0xF));
        vk::PipelineColorBlendStateCreateInfo color({}, false, vk::LogicOp::eCopy, 1, &blend);
        vk::PipelineDepthStencilStateCreateInfo depth{};
        depth.setDepthTestEnable(false).setDepthWriteEnable(false);
        vk::DescriptorSetLayout layout = descriptorLayout.get();
        vk::PushConstantRange objectRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4));
        pipelineLayout = device->createPipelineLayoutUnique({{}, 1, &layout, 1, &objectRange});
        vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamic({}, 2, dynamicStates);
        vk::GraphicsPipelineCreateInfo info({}, 2, stages, &input, &assembly, nullptr, &viewportState, &raster,
                                            &multisample, &depth, &color, &dynamic, *pipelineLayout, *renderPass);
        pipeline = device->createGraphicsPipelineUnique({}, info).value;
    }
    void createFramebuffers() {
        for (auto& view : imageViews) {
            vk::ImageView attachments[] = {view.get(), depthView.get()};
            framebuffers.push_back(device->createFramebufferUnique(
                {{}, *renderPass, 2, attachments, swapchainExtent.width, swapchainExtent.height, 1}));
        }
    }
    void createCommandPool() {
        commandPool =
            device->createCommandPoolUnique({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily});
    }
    void allocateCommandBuffers() {
        commandBuffers =
            device->allocateCommandBuffersUnique({*commandPool, vk::CommandBufferLevel::ePrimary, uint32_t(FRAMES)});
    }
    uint32_t memoryType(uint32_t bits, vk::MemoryPropertyFlags wanted) {
        auto memory = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        throw std::runtime_error("No suitable memory type");
    }
    void createVertexBuffer() {
        vertexBuffer = device->createBufferUnique(
            {{}, sizeof(QUAD), vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive});
        auto requirements = device->getBufferMemoryRequirements(*vertexBuffer);
        vertexMemory = device->allocateMemoryUnique(
            {requirements.size,
             memoryType(requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)});
        device->bindBufferMemory(*vertexBuffer, *vertexMemory, 0);
        void* mapped = device->mapMemory(*vertexMemory, 0, sizeof(QUAD));
        std::memcpy(mapped, QUAD.data(), sizeof(QUAD));
        device->unmapMemory(*vertexMemory);
    }
    static SpriteObject makeSprite(uint32_t id, glm::vec2 position, glm::vec2 scale, float rotation, int layer) {
        return SpriteObject{id, Transform2D{position, scale, rotation, layer}};
    }
    void createSceneObjects() {
        objects = {makeSprite(0, {-0.18f, 0.18f}, {0.55f, 0.55f}, 0.0f, 0),
                   makeSprite(1, {0.0f, 0.0f}, {0.55f, 0.55f}, 0.0f, 0),
                   makeSprite(2, {0.18f, -0.18f}, {0.55f, 0.55f}, 0.0f, 0)};
    }
    void sortObjects() {
        std::stable_sort(objects.begin(), objects.end(), [](const SpriteObject& lhs, const SpriteObject& rhs) {
            if (lhs.transform.layer != rhs.transform.layer)
                return lhs.transform.layer < rhs.transform.layer;
            if (lhs.transform.position.y != rhs.transform.position.y)
                return lhs.transform.position.y > rhs.transform.position.y;
            return lhs.id < rhs.id;
        });
    }
    void updateScene() {
        sortObjects();
    }
    static glm::mat4 modelMatrix(const Transform2D& transform) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(transform.position, 0.0f));
        model = glm::rotate(model, transform.rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(transform.scale, 1.0f));
        return model;
    }
    void createSyncObjects() {
        for (size_t i = 0; i < FRAMES; ++i) {
            imageAvailable[i] = device->createSemaphoreUnique({});
            fences[i] = device->createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
        renderFinished.clear();
        for (size_t i = 0; i < swapchainImages.size(); ++i) renderFinished.push_back(device->createSemaphoreUnique({}));
    }
    void recreateSwapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while ((width == 0 || height == 0) && !glfwWindowShouldClose(window)) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }
        if (glfwWindowShouldClose(window))
            return;
        device->waitIdle();
        framebuffers.clear();
        imageViews.clear();
        depthView.reset();
        depthImage.reset();
        depthMemory.reset();
        renderFinished.clear();
        swapchain.reset();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createFramebuffers();
        for (size_t i = 0; i < swapchainImages.size(); ++i) renderFinished.push_back(device->createSemaphoreUnique({}));
        updateCamera();
        resized = false;
    }
    void drawFrame() {
        vk::Fence fence = fences[currentFrame].get();
        (void)device->waitForFences(1, &fence, VK_TRUE, UINT64_MAX);
        uint32_t imageIndex = 0;
        vk::Result result;
        try {
            auto acquired = device->acquireNextImageKHR(*swapchain, UINT64_MAX, *imageAvailable[currentFrame], {});
            result = acquired.result;
            imageIndex = acquired.value;
        } catch (const vk::OutOfDateKHRError&) {
            recreateSwapchain();
            return;
        }
        const bool recreateAfterFrame = result == vk::Result::eSuboptimalKHR || resized;
        (void)device->resetFences(1, &fence);
        auto& command = commandBuffers[currentFrame];
        command->reset();
        vk::CommandBufferBeginInfo beginInfo{};
        (void)command->begin(beginInfo);
        std::array<vk::ClearValue, 2> clears = {vk::ClearValue(std::array<float, 4>{0.02f, 0.04f, 0.14f, 1.0f}),
                                                vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))};
        vk::RenderPassBeginInfo pass(*renderPass, *framebuffers[imageIndex], {{0, 0}, swapchainExtent},
                                     uint32_t(clears.size()), clears.data());
        command->beginRenderPass(pass, vk::SubpassContents::eInline);
        command->setViewport(0, vk::Viewport(0, 0, float(swapchainExtent.width), float(swapchainExtent.height), 0, 1));
        command->setScissor(0, vk::Rect2D({0, 0}, swapchainExtent));
        command->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
        command->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, 1,
                                    &descriptorSets[currentFrame], 0, nullptr);
        vk::DeviceSize offset = 0;
        vk::Buffer buffer = vertexBuffer.get();
        command->bindVertexBuffers(0, 1, &buffer, &offset);
        for (const auto& object : objects) {
            glm::mat4 model = modelMatrix(object.transform);
            command->pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(model), &model);
            command->draw(uint32_t(QUAD.size()), 1, 0, 0);
        }
        command->endRenderPass();
        command->end();
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::Semaphore available = imageAvailable[currentFrame].get(), finished = renderFinished[imageIndex].get();
        vk::CommandBuffer commandHandle = command.get();
        vk::SwapchainKHR chain = swapchain.get();
        vk::SubmitInfo submit(1, &available, &waitStage, 1, &commandHandle, 1, &finished);
        graphicsQueue.submit(submit, *fences[currentFrame]);
        vk::PresentInfoKHR present(1, &finished, 1, &chain, &imageIndex);
        try {
            result = presentQueue.presentKHR(present);
        } catch (const vk::OutOfDateKHRError&) {
            result = vk::Result::eErrorOutOfDateKHR;
        }
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || recreateAfterFrame)
            recreateSwapchain();
        currentFrame = (currentFrame + 1) % FRAMES;
    }
    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            updateScene();
            drawFrame();
        }
    }
    void cleanup() {
        if (device)
            device->waitIdle();
        for (auto& semaphore : imageAvailable) semaphore.reset();
        renderFinished.clear();
        for (auto& fence : fences) fence.reset();
        commandBuffers.clear();
        commandPool.reset();
        pipeline.reset();
        pipelineLayout.reset();
        depthView.reset();
        depthImage.reset();
        depthMemory.reset();
        vertexShader.reset();
        fragmentShader.reset();
        vertexBuffer.reset();
        vertexMemory.reset();
        descriptorPool.reset();
        descriptorLayout.reset();
        if (allocator != VK_NULL_HANDLE) {
            for (size_t i = 0; i < FRAMES; ++i) {
                if (uniformBuffers[i] != VK_NULL_HANDLE)
                    vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
                uniformBuffers[i] = VK_NULL_HANDLE;
                uniformAllocations[i] = VK_NULL_HANDLE;
            }
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        } else {
            uniformBuffers.fill(VK_NULL_HANDLE);
            uniformAllocations.fill(VK_NULL_HANDLE);
        }
        textureSampler.reset();
        textureView.reset();
        textureImage.reset();
        textureMemory.reset();
        stagingBuffer.reset();
        stagingMemory.reset();
        framebuffers.clear();
        renderPass.reset();
        imageViews.clear();
        swapchain.reset();
        device.reset();
        if (instance && surface)
            instance->destroySurfaceKHR(surface);
        debugMessenger.reset();
        instance.reset();
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
    }
};
int main() {
    try {
        VulkanApp{}.run();
    } catch (const std::exception& error) {
        std::cerr << "Critical Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
