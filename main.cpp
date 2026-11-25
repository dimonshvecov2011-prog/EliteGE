#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {
constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION = false;
#else
constexpr bool ENABLE_VALIDATION = true;
#endif
const std::vector<const char*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> DEVICE_EXTENSIONS = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};
}

class VulkanApp {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;
    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT debugMessenger;
    vk::SurfaceKHR surface{};
    vk::PhysicalDevice physicalDevice{};
    vk::UniqueDevice device;
    vk::Queue graphicsQueue{}, presentQueue{};
    uint32_t graphicsFamily = 0, presentFamily = 0;
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> swapchainImages;
    vk::Format swapchainFormat{};
    vk::Extent2D swapchainExtent{};
    std::vector<vk::UniqueImageView> imageViews;
    vk::UniqueRenderPass renderPass;
    std::vector<vk::UniqueFramebuffer> framebuffers;
    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
    std::array<vk::UniqueSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailable;
    std::array<vk::UniqueSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinished;
    std::array<vk::UniqueFence, MAX_FRAMES_IN_FLIGHT> fences;
    size_t currentFrame = 0;
    bool framebufferResized = false;

    void initWindow() {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "GameVault Engine MVP", nullptr, nullptr);
        if (!window) throw std::runtime_error("GLFW window creation failed");
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
            static_cast<VulkanApp*>(glfwGetWindowUserPointer(w))->framebufferResized = true;
        });
    }

    void initVulkan() {
        createInstance();
        createDebugMessenger();
        if (glfwCreateWindowSurface(*instance, window, nullptr,
                                    reinterpret_cast<VkSurfaceKHR*>(&surface)) != VK_SUCCESS)
            throw std::runtime_error("Window surface creation failed");
        pickPhysicalDevice();
        createDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        createCommandPool();
        allocateCommandBuffers();
        createSyncObjects();
    }

    static bool hasValidationLayers() {
        const auto available = vk::enumerateInstanceLayerProperties();
        return std::all_of(VALIDATION_LAYERS.begin(), VALIDATION_LAYERS.end(),
            [&](const char* wanted) {
                return std::any_of(available.begin(), available.end(),
                    [&](const auto& layer) { return std::strcmp(layer.layerName, wanted) == 0; });
            });
    }

    void createInstance() {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
        if (ENABLE_VALIDATION && !hasValidationLayers())
            throw std::runtime_error("VK_LAYER_KHRONOS_validation is not installed");
        uint32_t count = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&count);
        if (!glfwExtensions) throw std::runtime_error("GLFW Vulkan extensions unavailable");
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + count);
        if (ENABLE_VALIDATION) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        vk::ApplicationInfo app("GameVault Engine MVP", 1, "EliteGE", 1, VK_API_VERSION_1_0);
        vk::InstanceCreateInfo info({}, &app,
            ENABLE_VALIDATION ? static_cast<uint32_t>(VALIDATION_LAYERS.size()) : 0,
            ENABLE_VALIDATION ? VALIDATION_LAYERS.data() : nullptr,
            static_cast<uint32_t>(extensions.size()), extensions.data());
        instance = vk::createInstanceUnique(info);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    }

    void createDebugMessenger() {
        if (!ENABLE_VALIDATION) return;
        vk::DebugUtilsMessengerCreateInfoEXT info({},
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance, debugCallback);
        debugMessenger = instance->createDebugUtilsMessengerEXTUnique(info);
    }

    QueueFamilies findQueueFamilies(vk::PhysicalDevice gpu) const {
        QueueFamilies result;
        const auto properties = gpu.getQueueFamilyProperties();
        for (uint32_t i = 0; i < properties.size(); ++i) {
            if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics) result.graphics = i;
            if (gpu.getSurfaceSupportKHR(i, surface)) result.present = i;
            if (result.complete()) break;
        }
        return result;
    }

    bool supportsDevice(vk::PhysicalDevice gpu) const {
        const auto extensions = gpu.enumerateDeviceExtensionProperties();
        return std::all_of(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end(),
            [&](const char* wanted) {
                return std::any_of(extensions.begin(), extensions.end(),
                    [&](const auto& ext) { return std::strcmp(ext.extensionName, wanted) == 0; });
            });
    }

    void pickPhysicalDevice() {
        for (const auto& gpu : instance->enumeratePhysicalDevices()) {
            if (supportsDevice(gpu) && findQueueFamilies(gpu).complete()) {
                physicalDevice = gpu;
                return;
            }
        }
        throw std::runtime_error("No suitable Vulkan GPU found");
    }

    void createDevice() {
        const QueueFamilies families = findQueueFamilies(physicalDevice);
        graphicsFamily = *families.graphics;
        presentFamily = *families.present;
        const std::set<uint32_t> uniqueFamilies = {graphicsFamily, presentFamily};
        const float priority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queues;
        for (uint32_t family : uniqueFamilies)
            queues.emplace_back(vk::DeviceQueueCreateFlags{}, family, 1, &priority);
        vk::PhysicalDeviceFeatures features{};
        vk::DeviceCreateInfo info({}, queues, {}, DEVICE_EXTENSIONS, &features);
        device = physicalDevice.createDeviceUnique(info);
        graphicsQueue = device->getQueue(graphicsFamily, 0);
        presentQueue = device->getQueue(presentFamily, 0);
    }

    vk::SurfaceFormatKHR chooseFormat(const std::vector<vk::SurfaceFormatKHR>& formats) const {
        for (const auto& format : formats)
            if (format.format == vk::Format::eB8G8R8A8Srgb &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return format;
        return formats.front();
    }

    vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& modes) const {
        for (auto mode : modes) if (mode == vk::PresentModeKHR::eMailbox) return mode;
        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& caps) const {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        vk::Extent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

    void createSwapchain() {
        const auto caps = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        const auto format = chooseFormat(physicalDevice.getSurfaceFormatsKHR(surface));
        const auto mode = choosePresentMode(physicalDevice.getSurfacePresentModesKHR(surface));
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
        swapchainExtent = chooseExtent(caps);
        vk::SwapchainCreateInfoKHR info({}, surface, imageCount, format.format, format.colorSpace,
            swapchainExtent, 1, vk::ImageUsageFlagBits::eColorAttachment);
        const uint32_t families[] = {graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily)
            info.setImageSharingMode(vk::SharingMode::eConcurrent).setQueueFamilyIndices(families);
        else info.setImageSharingMode(vk::SharingMode::eExclusive);
        info.setPreTransform(caps.currentTransform).setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(mode).setClipped(VK_TRUE);
        swapchain = device->createSwapchainKHRUnique(info);
        swapchainImages = device->getSwapchainImagesKHR(*swapchain);
        swapchainFormat = format.format;
    }

    void createImageViews() {
        for (auto image : swapchainImages) {
            vk::ImageViewCreateInfo info({}, image, vk::ImageViewType::e2D, swapchainFormat, {},
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            imageViews.push_back(device->createImageViewUnique(info));
        }
    }

    void createRenderPass() {
        vk::AttachmentDescription attachment({}, swapchainFormat, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
        vk::AttachmentReference reference(0, vk::ImageLayout::eColorAttachmentOptimal);
        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, nullptr, 1, &reference);
        renderPass = device->createRenderPassUnique({{}, 1, &attachment, 1, &subpass});
    }

    void createFramebuffers() {
        for (const auto& view : imageViews) {
            vk::ImageView handle = view.get();
            vk::FramebufferCreateInfo info({}, *renderPass, 1, &handle,
                swapchainExtent.width, swapchainExtent.height, 1);
            framebuffers.push_back(device->createFramebufferUnique(info));
        }
    }

    void createCommandPool() {
        commandPool = device->createCommandPoolUnique({vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily});
    }

    void allocateCommandBuffers() {
        commandBuffers = device->allocateCommandBuffersUnique({*commandPool, vk::CommandBufferLevel::ePrimary,
                                                                static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)});
    }

    void createSyncObjects() {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            imageAvailable[i] = device->createSemaphoreUnique({});
            renderFinished[i] = device->createSemaphoreUnique({});
            fences[i] = device->createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void recreateSwapchain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while ((width == 0 || height == 0) && !glfwWindowShouldClose(window)) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }
        if (glfwWindowShouldClose(window)) return;
        device->waitIdle();
        framebuffers.clear(); imageViews.clear(); swapchain.reset();
        createSwapchain(); createImageViews(); createFramebuffers();
    }

    void drawFrame() {
        vk::Fence currentFence = fences[currentFrame].get();
        vk::Result fenceResult = device->waitForFences(1, &currentFence, VK_TRUE, UINT64_MAX);
        (void)fenceResult;
        uint32_t imageIndex = 0;
        vk::Result result;
        try {
            auto acquired = device->acquireNextImageKHR(*swapchain, UINT64_MAX,
                *imageAvailable[currentFrame], {});
            result = acquired.result; imageIndex = acquired.value;
        } catch (const vk::OutOfDateKHRError&) { recreateSwapchain(); return; }
        if (result == vk::Result::eSuboptimalKHR || framebufferResized) {
            framebufferResized = false; recreateSwapchain(); return;
        }
        vk::Result resetResult = device->resetFences(1, &currentFence);
        (void)resetResult;
        auto& command = commandBuffers[currentFrame];
        command->reset();
        vk::Result commandBeginResult = command->begin({});
        (void)commandBeginResult;
        glm::vec4 color(0.1f, 0.1f, 0.2f, 1.0f);
        vk::ClearValue clear(std::array<float, 4>{color.r, color.g, color.b, color.a});
        vk::RenderPassBeginInfo pass(*renderPass, *framebuffers[imageIndex], {{0, 0}, swapchainExtent}, 1, &clear);
        command->beginRenderPass(pass, vk::SubpassContents::eInline); command->endRenderPass(); command->end();
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit(1, &imageAvailable[currentFrame].get(), &waitStage, 1,
                              &command.get(), 1, &renderFinished[currentFrame].get());
        graphicsQueue.submit(submit, *fences[currentFrame]);
        vk::PresentInfoKHR present(1, &renderFinished[currentFrame].get(), 1, &swapchain.get(), &imageIndex);
        try { result = presentQueue.presentKHR(present); }
        catch (const vk::OutOfDateKHRError&) { result = vk::Result::eErrorOutOfDateKHR; }
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
            framebufferResized = false; recreateSwapchain();
        }
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
            drawFrame();
        }
    }

    void cleanup() {
        if (device) device->waitIdle();
        for (auto& semaphore : imageAvailable) semaphore.reset();
        for (auto& semaphore : renderFinished) semaphore.reset();
        for (auto& fence : fences) fence.reset();
        commandBuffers.clear(); commandPool.reset(); framebuffers.clear(); renderPass.reset();
        imageViews.clear(); swapchain.reset(); device.reset();
        if (instance && surface) instance->destroySurfaceKHR(surface);
        debugMessenger.reset(); instance.reset();
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main() {
    try { VulkanApp{}.run(); }
    catch (const std::exception& error) {
        std::cerr << "Critical Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
