
#define VK_USE_PLATFORM_WIN32_KHR   // remove if not on Windows
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//#include <shaderc/shaderc.h>
//#include <shaderc/shaderc.hpp>

#include <Eigen/Geometry>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>

static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
static const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription d{};
        d.binding = 0;
        d.stride = sizeof(Vertex);
        d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return d;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> a{};
        a[0].binding = 0; a[0].location = 0;
        a[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[0].offset = offsetof(Vertex, pos);

        a[1].binding = 0; a[1].location = 1;
        a[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[1].offset = offsetof(Vertex, normal);

        a[2].binding = 0; a[2].location = 2;
        a[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[2].offset = offsetof(Vertex, color);
        return a;
    }
};

static std::vector<Vertex> makeCubeVertices()
{
    std::vector<Vertex> v;
    // colors
    // define face colors (top, bottom, front, back, left, right)
    glm::vec3 topColor = glm::vec3(0.0f, 0.0f, 0.5f);  // vivid yellow
    glm::vec3 bottomColor = glm::vec3(0.0f, 0.0f, 0.4f);  // bright white
    glm::vec3 frontColor = glm::vec3(0.0f, 0.5f, 0.0f);  // electric blue
    glm::vec3 backColor = glm::vec3(0.0f, 0.4f, 0.0f); // vivid green
    glm::vec3 leftColor = glm::vec3(0.4f, 0.0f, 0.0f); // bright orange
    glm::vec3 rightColor = glm::vec3(0.5f, 0.0f, 0.0f);// vivid red

    // Helper lambda to add two triangles (quad) with color and compute face normal
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color) {
        // compute normal using first triangle (a,b,c)
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        // Triangle 1: a, b, c
        v.push_back(Vertex{ a, n, color });
        v.push_back(Vertex{ b, n, color });
        v.push_back(Vertex{ c, n, color });
        // Triangle 2: c, d, a
        v.push_back(Vertex{ c, n, color });
        v.push_back(Vertex{ d, n, color });
        v.push_back(Vertex{ a, n, color });
    };

    // corners
    glm::vec3 p000 = { -1.0f, -1.0f, -1.0f };
    glm::vec3 p001 = { -1.0f, -1.0f,  1.0f };
    glm::vec3 p010 = { -1.0f,  1.0f, -1.0f };
    glm::vec3 p011 = { -1.0f,  1.0f,  1.0f };
    glm::vec3 p100 = {  1.0f, -1.0f, -1.0f };
    glm::vec3 p101 = {  1.0f, -1.0f,  1.0f };
    glm::vec3 p110 = {  1.0f,  1.0f, -1.0f };
    glm::vec3 p111 = {  1.0f,  1.0f,  1.0f };

    // Top face (y=1)
    quad(p010, p110, p111, p011, topColor);

    // Bottom face (y=-1)
    quad(p000, p001, p101, p100, bottomColor);

    // Front face (z=1)
    quad(p011, p111, p101, p001, frontColor);

    // Back face (z=-1) - disabled, use Vulkan clear color for background
    quad(p010, p000, p100, p110, backColor);

    // Left face (x=-1)
    quad(p010, p011, p001, p000, leftColor);

    // Right face (x=1)
    quad(p110, p100, p101, p111, rightColor);

    return v;
}

// NOTE: makeCubeVertices declared after Vertex struct (uses Vertex type)
static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

// ────────────────────────────────────────────────────────────
//  Shader helpers  (readTextFile / compileGLSL / createShaderModule)
// ────────────────────────────────────────────────────────────
/*
static std::string readTextFile(const std::string& filename)
{
    namespace fs = std::filesystem;
    fs::path inputPath(filename);
    fs::path resolvedPath;

    const fs::path cwd = fs::current_path();
    const std::array<fs::path, 5> candidates = {
        cwd / inputPath,
        cwd.parent_path() / inputPath,
        cwd.parent_path().parent_path() / inputPath,
        cwd.parent_path().parent_path().parent_path() / inputPath,
        cwd.parent_path().parent_path().parent_path().parent_path() / inputPath
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) { resolvedPath = candidate; break; }
    }
    if (resolvedPath.empty())
        throw std::runtime_error("Failed to find file: " + filename);

    std::ifstream file(resolvedPath);
    if (!file.is_open())
        throw std::runtime_error("Failed to open file: " + filename);

    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
}

static std::vector<uint32_t> compileGLSL(
    const std::string& source,
    shaderc_shader_kind kind,
    const std::string& name)
{
    shaderc::Compiler       compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
        shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    auto result = compiler.CompileGlslToSpv(source, kind, name.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error("Shader compile error (" + name + "):\n" +
            result.GetErrorMessage());

    return { result.cbegin(), result.cend() };
}

static VkShaderModule createShaderModule(VkDevice device,
    const std::vector<uint32_t>& spirv)
{
    VkShaderModuleCreateInfo info{};9
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Failed to create VkShaderModule");
    return module;
}
*/

std::vector<char> readSPVFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    // 注意：Vulkan 要求 uint32_t* 对齐，用 reinterpret_cast
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

// ────────────────────────────────────────────────────────────
//  Data structures
// ────────────────────────────────────────────────────────────

// X = Red (1,0,0)  |  Y = Blue (0,0,1)  |  Z = Green (0,1,0)
// Each axis: shaft + two arrow-head lines
// For axis lines we still use Vertex type; normals set to zero
static const std::vector<Vertex> AXIS_VERTICES = {
    // ── X  (Red) ───────────────────────────────────────────
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {1.f, 0.f, 0.f}},
    {{ 1.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {1.f, 0.f, 0.f}},

    // ── Y  (Blue) ──────────────────────────────────────────
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 0.f, 1.f}},
    {{ 0.00f,  1.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 0.f, 1.f}},

    // ── Z  (Green) ─────────────────────────────────────────
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 1.f, 0.f}},
    {{ 0.00f,  0.00f,  1.00f}, {0.f,0.f,0.f}, {0.f, 1.f, 0.f}},
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

// ────────────────────────────────────────────────────────────
//  Debug messenger
// ────────────────────────────────────────────────────────────
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    std::cerr << "[VK] " << pData->pMessage << '\n';
    return VK_FALSE;
}

// ────────────────────────────────────────────────────────────
//  Application  (all member variables start with  _ )
// ────────────────────────────────────────────────────────────
class CoordApp {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    // window
    GLFWwindow* _window = nullptr;

    glm::vec3 _coordinate = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec2 _rotation = glm::vec2(45.0f, -45.0f);
    glm::vec4 _ortho = glm::vec4(-5.0f, 5.0f, -5.0f, 5.0f);
	glm::vec2 _viewport = glm::vec2(1024, 768);

    //Eigen::AlignedBox3f orthoBox(Eigen::Vector3f(-5.0f, -4.0f, 0.1f), Eigen::Vector3f(5.0f, 4.0f, 1000));

    // core
    VkInstance               _instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             _surface = VK_NULL_HANDLE;
    VkPhysicalDevice         _physicalDevice = VK_NULL_HANDLE;
    VkDevice                 _device = VK_NULL_HANDLE;
    VkQueue                  _graphicsQueue = VK_NULL_HANDLE;
    VkQueue                  _presentQueue = VK_NULL_HANDLE;

    // swap chain
    VkSwapchainKHR             _swapChain = VK_NULL_HANDLE;
    std::vector<VkImage>       _swapChainImages;
    VkFormat                   _swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                 _swapChainExtent{};
    std::vector<VkImageView>   _swapChainImageViews;
    std::vector<VkFramebuffer> _swapChainFramebuffers;

    // depth
    VkImage        _depthImage = VK_NULL_HANDLE;
    VkDeviceMemory _depthImageMemory = VK_NULL_HANDLE;
    VkImageView    _depthImageView = VK_NULL_HANDLE;

    // pipeline
    VkRenderPass          _renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            _trianglePipeline = VK_NULL_HANDLE;
    VkPipeline            _linePipeline = VK_NULL_HANDLE;

    // buffers
    VkBuffer       _vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _vertexBufferMemory = VK_NULL_HANDLE;
    uint32_t       _axisVertexCount = 0;
    uint32_t       _cubeVertexCount = 0;

    std::vector<VkBuffer>       _uniformBuffers;
    std::vector<VkDeviceMemory> _uniformBuffersMemory;
    std::vector<void*>          _uniformBuffersMapped;

    // descriptors
    VkDescriptorPool             _descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets;

    // commands
    VkCommandPool                _commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> _commandBuffers;

    // sync
    std::vector<VkSemaphore> _imageAvailableSemaphores;
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    std::vector<VkFence>     _inFlightFences;
    // state
    uint32_t _currentFrame = 0;
    bool     _framebufferResized = false;
    // camera / input state
    double _lastX = 0.0, _lastY = 0.0;
    bool   _leftMousePressed = false;
    float  _camYaw = 0.0f;   // degrees
    float  _camPitch = 0.0f; // degrees
    float  _camDistance = 10.0f;
    glm::vec3 _camTarget = glm::vec3(0.0f);
    // track previous modifier state to avoid large jumps when modifier is first pressed
    bool _shiftPressedPrev = false;
    bool _ctrlPressedPrev = false;
    bool _altPressedPrev = false;
    bool _winPressedPrev = false;

    // ── Window ──────────────────────────────────────────────
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        _window = glfwCreateWindow(_viewport[0], _viewport[1], "VULKAN", nullptr, nullptr);
        glfwSetWindowUserPointer(_window, this);
        glfwSetFramebufferSizeCallback(_window, onFramebufferResize);
        glfwSetCursorPosCallback(_window, onCursorPos);
        glfwSetMouseButtonCallback(_window, onMouseButton);
        glfwSetScrollCallback(_window, onScroll);
    }

    static void onFramebufferResize(GLFWwindow* w, int, int) {
        reinterpret_cast<CoordApp*>(glfwGetWindowUserPointer(w))->_framebufferResized = true;
    }

    // Static GLFW callbacks that forward to instance methods
    static void onCursorPos(GLFWwindow* w, double x, double y) {
        auto app = reinterpret_cast<CoordApp*>(glfwGetWindowUserPointer(w));
        if (app) app->cursorPosCallbackImpl(x, y);
    }

    static void onMouseButton(GLFWwindow* w, int button, int action, int mods) {
        auto app = reinterpret_cast<CoordApp*>(glfwGetWindowUserPointer(w));
        if (app) app->mouseButtonCallbackImpl(button, action, mods);
    }

    static void onScroll(GLFWwindow* w, double xoffset, double yoffset) {
        auto app = reinterpret_cast<CoordApp*>(glfwGetWindowUserPointer(w));
        if (app) app->scrollCallbackImpl(xoffset, yoffset);
    }

    glm::mat4 buildViewMatrix() const {
        glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
        glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));
        glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);
        return glm::lookAt(_coordinate - viewZ * 100.0f, _coordinate + viewZ * 100.0f, viewY);
    }

    glm::mat4 buildProjMatrix() const {
        glm::mat4 proj = glm::ortho(_ortho[0], _ortho[1], _ortho[2], _ortho[3], 0.1f, 200.0f);
        proj[1][1] *= -1.0f; // Keep same Y-flip used by rendering
        return proj;
    }

    bool hasStencilComponent(VkFormat format) const {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    bool readDepthAtFramebufferPixel(uint32_t px, uint32_t py, float& outDepth01) {
        if (px >= _swapChainExtent.width || py >= _swapChainExtent.height) {
            return false;
        }

        // Keep this simple/safe for picking: block until current frame work is finished.
        vkDeviceWaitIdle(_device);

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(
            8, // enough for D32/D24 packed depth copies
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = _commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(_device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Temporarily switch depth image into transfer-src so we can copy one pixel.
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = _depthImage;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(findDepthFormat())) {
            toTransfer.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        toTransfer.subresourceRange.baseMipLevel = 0;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseArrayLayer = 0;
        toTransfer.subresourceRange.layerCount = 1;
        toTransfer.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toTransfer);

        // Copy exactly one depth pixel under the cursor.
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { static_cast<int32_t>(px), static_cast<int32_t>(py), 0 };
        region.imageExtent = { 1, 1, 1 };

        vkCmdCopyImageToBuffer(
            cmd,
            _depthImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer,
            1,
            &region);

        // Restore the depth attachment layout for normal rendering.
        VkImageMemoryBarrier backToDepth{};
        backToDepth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        backToDepth.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backToDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        backToDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        backToDepth.image = _depthImage;
        backToDepth.subresourceRange = toTransfer.subresourceRange;
        backToDepth.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        backToDepth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &backToDepth);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicsQueue);

        void* mapped = nullptr;
        vkMapMemory(_device, stagingMemory, 0, 8, 0, &mapped);
        const uint32_t packed = *reinterpret_cast<const uint32_t*>(mapped);
        vkUnmapMemory(_device, stagingMemory);

        vkFreeCommandBuffers(_device, _commandPool, 1, &cmd);
        vkDestroyBuffer(_device, stagingBuffer, nullptr);
        vkFreeMemory(_device, stagingMemory, nullptr);

        // Decode copied depth based on the runtime-selected depth format.
        const VkFormat depthFmt = findDepthFormat();
        if (depthFmt == VK_FORMAT_D32_SFLOAT || depthFmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            outDepth01 = *reinterpret_cast<const float*>(&packed);
        } else if (depthFmt == VK_FORMAT_D24_UNORM_S8_UINT) {
            outDepth01 = static_cast<float>(packed & 0x00FFFFFFu) / 16777215.0f;
        } else {
            return false;
        }

        outDepth01 = std::clamp(outDepth01, 0.0f, 1.0f);
        // If depth is still clear value, no entity was drawn at this pixel.
        if (outDepth01 >= 0.999999f) {
            return false;
        }
        return true;
    }

    bool readDepthAtCursor(double mouseX, double mouseY, float& outDepth01) {
        int windowWidth = 0, windowHeight = 0;
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(_window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(_window, &framebufferWidth, &framebufferHeight);
        if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
            return false;
        }

        // GLFW cursor is window-space; convert to framebuffer pixels (HiDPI-safe).
        const double framebufferX = mouseX * static_cast<double>(framebufferWidth) / static_cast<double>(windowWidth);
        const double framebufferY = mouseY * static_cast<double>(framebufferHeight) / static_cast<double>(windowHeight);

        const uint32_t pixelX = static_cast<uint32_t>(std::clamp<int>(
            static_cast<int>(framebufferX), 0, framebufferWidth - 1));
        const uint32_t pixelY = static_cast<uint32_t>(std::clamp<int>(
            static_cast<int>(framebufferY), 0, framebufferHeight - 1));

        return readDepthAtFramebufferPixel(pixelX, pixelY, outDepth01);
    }

    glm::vec3 screenToWorldByDepth(double mouseX, double mouseY, float depth01) const {
        int windowWidth = 0, windowHeight = 0;
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(_window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(_window, &framebufferWidth, &framebufferHeight);
        if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
            return glm::vec3(0.0f);
        }

        // Cursor position is in window coordinates; convert to framebuffer pixels.
        const double framebufferX = mouseX * static_cast<double>(framebufferWidth) / static_cast<double>(windowWidth);
        const double framebufferY = mouseY * static_cast<double>(framebufferHeight) / static_cast<double>(windowHeight);

        const float ndcX = static_cast<float>((2.0 * framebufferX) / static_cast<double>(framebufferWidth) - 1.0);
        // Vulkan viewport coordinates have top-left origin with positive viewport height.
        const float ndcY = static_cast<float>((2.0 * framebufferY) / static_cast<double>(framebufferHeight) - 1.0);
        const float ndcZ = std::clamp(depth01, 0.0f, 1.0f); // Vulkan NDC z range is [0, 1]

        // Unproject from NDC back to world using inverse view-projection.
        const glm::mat4 invVP = glm::inverse(buildProjMatrix() * buildViewMatrix());
        const glm::vec4 worldH = invVP * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
        if (worldH.w == 0.0f) {
            return glm::vec3(0.0f);
        }
        return glm::vec3(worldH) / worldH.w;
    }

    // Returns (screenX, screenY, depth01) for a world-space point.
    glm::vec3 worldToScreen(const glm::vec3& world) const {
        int windowWidth = 0, windowHeight = 0;
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(_window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(_window, &framebufferWidth, &framebufferHeight);
        if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
            return glm::vec3(0.0f);
        }

        // Project world -> clip -> NDC -> window-space cursor coordinates.
        const glm::vec4 clip = buildProjMatrix() * buildViewMatrix() * glm::vec4(world, 1.0f);
        if (clip.w == 0.0f) {
            return glm::vec3(0.0f);
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const double framebufferX = (static_cast<double>(ndc.x) + 1.0) * 0.5 * static_cast<double>(framebufferWidth);
        const double framebufferY = (static_cast<double>(ndc.y) + 1.0) * 0.5 * static_cast<double>(framebufferHeight);

        const float screenX = static_cast<float>(framebufferX * static_cast<double>(windowWidth) / static_cast<double>(framebufferWidth));
        const float screenY = static_cast<float>(framebufferY * static_cast<double>(windowHeight) / static_cast<double>(framebufferHeight));
        const float depth01 = std::clamp(ndc.z, 0.0f, 1.0f);

        return glm::vec3(screenX, screenY, depth01);
    }

    // Instance handlers for input
    void cursorPosCallbackImpl(double xpos, double ypos) {
        bool shiftHeld = (glfwGetKey(_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        bool ctrlHeld = (glfwGetKey(_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
        bool altHeld = (glfwGetKey(_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

        // If neither left mouse nor modifier keys are active, just update last pos and return
        if (!_leftMousePressed && !shiftHeld && !ctrlHeld && !altHeld) {
            _lastX = xpos;
            _lastY = ypos;
            return;
        }

        // If a modifier was just pressed, initialize last positions to avoid a large jump
        if (shiftHeld && !_shiftPressedPrev && !_leftMousePressed) {
            _lastX = xpos; _lastY = ypos;
        }
        if (ctrlHeld && !_ctrlPressedPrev && !_leftMousePressed) {
            _lastX = xpos; _lastY = ypos;
        }
        if (altHeld && !_altPressedPrev && !_leftMousePressed) {
            _lastX = xpos; _lastY = ypos;
        }

        double deltaX = xpos - _lastX;
        double deltaY = ypos - _lastY;

        // Controls:
        // - Shift: pan view
        // - Ctrl: orbit (change rotation angles)
        // - Alt: move only the coordinate axis origin in camera X/Y directions
        if (shiftHeld) { // Pan (translate target) when shift is held
            float transX = deltaX * (_ortho[1] - _ortho[0]) / _viewport[0];
            float transY = -deltaY * (_ortho[3] - _ortho[2]) / _viewport[1];

            _ortho[0] -= transX;
            _ortho[1] -= transX;
            _ortho[2] += transY;
            _ortho[3] += transY;
        }   
        else if (ctrlHeld) { // Rotate when ctrl is held          
            float rotateY = deltaX * 0.8f;
            float rotateX = deltaY * 0.8f;

            _rotation += glm::vec2(rotateX, rotateY);
        }
        else if (altHeld) { // Alt + mouse move: translate coordinate (axes) position
            // camera x, y
            float transX = deltaX * (_ortho[1] - _ortho[0]) / _viewport[0];
            float transY = -deltaY * (_ortho[3] - _ortho[2]) / _viewport[1];

            glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
            glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));

            // camera x, y world vector
            glm::vec3 viewX = quatY * quatX * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);

            _coordinate += transX * viewX;
            _coordinate += transY * viewY;

            _ortho[0] -= transX;
            _ortho[1] -= transX;
            _ortho[2] += transY;
            _ortho[3] += transY;
        }

        _lastX = xpos;
        _lastY = ypos;

        // store previous modifier state for next callback to avoid jumps
        _shiftPressedPrev = shiftHeld;
        _ctrlPressedPrev = ctrlHeld;
        _altPressedPrev = altHeld;
    }

    void mouseButtonCallbackImpl(int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                _leftMousePressed = true;
                double x, y; glfwGetCursorPos(_window, &x, &y);
                _lastX = x; _lastY = y;

                // --- Double-click detection ---
                double timeNow = glfwGetTime();
                static double timeOld = timeNow;

                if (((timeNow - timeOld) < 0.4) && ((mods & GLFW_MOD_ALT) || (mods & GLFW_MOD_CONTROL))) { // Alt/Ctrl + double-click
                    float depth01 = 1.0f;
                    if (readDepthAtCursor(x, y, depth01)) {
                        // Pick 3D position from current cursor pixel depth.
                        const glm::vec3 pickedCoordinate = screenToWorldByDepth(x, y, depth01);
                        std::cout << std::fixed << std::setprecision(4)
                                  << "Alt+DoubleClick x=" << x
                                  << " y=" << y
                                  << " zDepth=" << depth01
                                  << " -> coordinateNew=(" << pickedCoordinate.x << ", " << pickedCoordinate.y << ", " << pickedCoordinate.z << ")\n";


                        glm::vec3 coordinateScreen = worldToScreen(_coordinate);

                        double deltaX = x - coordinateScreen[0];
                        double deltaY = y - coordinateScreen[1];

                        float transX = deltaX * (_ortho[1] - _ortho[0]) / _viewport[0];
                        float transY = -deltaY * (_ortho[3] - _ortho[2]) / _viewport[1];

                        glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
                        glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));

                        // camera x, y world vector
                        glm::vec3 viewX = quatY * quatX * glm::vec3(1.0f, 0.0f, 0.0f);
                        glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
                        glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);

                        // Keep the picked coordinate visually under the cursor by compensating ortho window.
                        _ortho[0] -= transX;
                        _ortho[1] -= transX;
                        _ortho[2] += transY;
                        _ortho[3] += transY;

                        _coordinate = pickedCoordinate;
                    } else {
                        std::cout << "Alt+DoubleClick depth read failed at x=" << x << " y=" << y << "\n";
                    }
                    timeOld = 0.0;
                }
                else {
                    timeOld = timeNow;
                }
            }
            else if (action == GLFW_RELEASE) {
                _leftMousePressed = false;
            }
        }
    }

    void scrollCallbackImpl(double /*xoffset*/, double yoffset) {
        // If Alt is held, adjust Z component of transform (move coordinate in Z)
        bool altHeld = (glfwGetKey(_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        if (altHeld) {
            float transZ = yoffset * (_ortho[3] - _ortho[2]) / _viewport[1] * 20;

            glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
            glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));

            glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, 1.0f);

            _coordinate += transZ * viewZ;
            return;
        }

        float scale = 1.0f - static_cast<float>(yoffset) * 0.1f;
        _ortho *= scale; // _coorinate(0, 0)
    }

    // ── Vulkan init sequence ─────────────────────────────────
    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createDepthResources();
        createFramebuffers();
        createVertexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    // ── Instance ─────────────────────────────────────────────
    void createInstance() {
        if (ENABLE_VALIDATION && !checkValidationLayerSupport())
            throw std::runtime_error("Validation layers not available.");

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "CoordAxis";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        auto extensions = getRequiredExtensions();

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &appInfo;
        ci.enabledExtensionCount = (uint32_t)extensions.size();
        ci.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        if (ENABLE_VALIDATION) {
            ci.enabledLayerCount = (uint32_t)VALIDATION_LAYERS.size();
            ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
            fillDebugMessengerCI(dbgCI);
            ci.pNext = &dbgCI;
        }

        if (vkCreateInstance(&ci, nullptr, &_instance) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed.");
    }

    bool checkValidationLayerSupport() {
        uint32_t count;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        for (const char* name : VALIDATION_LAYERS) {
            bool found = false;
            for (auto& p : available)
                if (strcmp(name, p.layerName) == 0) { found = true; break; }
            if (!found) return false;
        }
        return true;
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t cnt = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&cnt);
        std::vector<const char*> exts(glfwExts, glfwExts + cnt);
        if (ENABLE_VALIDATION) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return exts;
    }

    // ── Debug messenger ──────────────────────────────────────
    void fillDebugMessengerCI(VkDebugUtilsMessengerCreateInfoEXT& ci) {
        ci = {};
        ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = debugCallback;
    }

    void setupDebugMessenger() {
        if (!ENABLE_VALIDATION) return;
        VkDebugUtilsMessengerCreateInfoEXT ci{};
        fillDebugMessengerCI(ci);
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
        if (!fn || fn(_instance, &ci, nullptr, &_debugMessenger) != VK_SUCCESS)
            throw std::runtime_error("Failed to setup debug messenger.");
    }

    // ── Surface ──────────────────────────────────────────────
    void createSurface() {
        if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create window surface.");
    }

    // ── Physical device ──────────────────────────────────────
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(_instance, &count, nullptr);
        if (!count) throw std::runtime_error("No Vulkan-capable GPU found.");
        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(_instance, &count, devs.data());
        for (auto& d : devs)
            if (isDeviceSuitable(d)) { _physicalDevice = d; break; }
        if (_physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable GPU found.");
    }

    bool isDeviceSuitable(VkPhysicalDevice dev) {
        auto idx = findQueueFamilies(dev);
        bool extOk = checkDeviceExtensionSupport(dev);
        bool swapOk = false;
        if (extOk) {
            auto sc = querySwapChainSupport(dev);
            swapOk = !sc.formats.empty() && !sc.presentModes.empty();
        }
        return idx.isComplete() && extOk && swapOk;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice dev) {
        uint32_t cnt;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, nullptr);
        std::vector<VkExtensionProperties> available(cnt);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, available.data());
        std::set<std::string> required(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
        for (auto& e : available) required.erase(e.extensionName);
        return required.empty();
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) {
        QueueFamilyIndices idx;
        uint32_t cnt;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &cnt, nullptr);
        std::vector<VkQueueFamilyProperties> fams(cnt);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &cnt, fams.data());
        for (uint32_t i = 0; i < cnt; i++) {
            if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                idx.graphicsFamily = i;
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, _surface, &present);
            if (present) idx.presentFamily = i;
            if (idx.isComplete()) break;
        }
        return idx;
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice dev) {
        SwapChainSupportDetails d;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, _surface, &d.capabilities);
        uint32_t n;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, _surface, &n, nullptr);
        d.formats.resize(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, _surface, &n, d.formats.data());
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, _surface, &n, nullptr);
        d.presentModes.resize(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, _surface, &n, d.presentModes.data());
        return d;
    }

    // ── Logical device ───────────────────────────────────────
    void createLogicalDevice() {
        auto idx = findQueueFamilies(_physicalDevice);
        std::set<uint32_t> uniqueFams = {
            idx.graphicsFamily.value(), idx.presentFamily.value()
        };

        float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCIs;
        for (uint32_t fam : uniqueFams) {
            std::set<uint32_t> uniqueFams = {
                idx.graphicsFamily.value(), idx.presentFamily.value()
            };
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = fam;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queueCIs.push_back(qi);
        }
        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = (uint32_t)queueCIs.size();
        ci.pQueueCreateInfos = queueCIs.data();
        ci.pEnabledFeatures = &features;
        ci.enabledExtensionCount = (uint32_t)DEVICE_EXTENSIONS.size();
        ci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
        if (ENABLE_VALIDATION) {
            ci.enabledLayerCount = (uint32_t)VALIDATION_LAYERS.size();
            ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }
        if (vkCreateDevice(_physicalDevice, &ci, nullptr, &_device) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed.");

        vkGetDeviceQueue(_device, idx.graphicsFamily.value(), 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, idx.presentFamily.value(), 0, &_presentQueue);
    }

    // ── Swap chain ───────────────────────────────────────────
    VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& fmts)
    {
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
        return fmts[0];
    }

    VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes)
    {
        for (auto& m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return caps.currentExtent;
        int w, h;
        glfwGetFramebufferSize(_window, &w, &h);
        return {
            std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width),
            std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height)
        };
    }

    void createSwapChain() {
        auto sc = querySwapChainSupport(_physicalDevice);
        auto fmt = chooseSurfaceFormat(sc.formats);
        auto pm = choosePresentMode(sc.presentModes);
        auto ext = chooseSwapExtent(sc.capabilities);

        uint32_t imgCount = sc.capabilities.minImageCount + 1;
        if (sc.capabilities.maxImageCount > 0)
            imgCount = std::min(imgCount, sc.capabilities.maxImageCount);

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = _surface;
        ci.minImageCount = imgCount;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto idx = findQueueFamilies(_physicalDevice);
        uint32_t queueFamilyIndices[] = {
            idx.graphicsFamily.value(), idx.presentFamily.value()
        };
        if (idx.graphicsFamily != idx.presentFamily) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform = sc.capabilities.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = pm;
        ci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(_device, &ci, nullptr, &_swapChain) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed.");

        vkGetSwapchainImagesKHR(_device, _swapChain, &imgCount, nullptr);
        _swapChainImages.resize(imgCount);
        vkGetSwapchainImagesKHR(_device, _swapChain, &imgCount, _swapChainImages.data());

        _swapChainImageFormat = fmt.format;
        _swapChainExtent = ext;
    }

    // ── Image views ──────────────────────────────────────────
    VkImageView createImageView(VkImage image, VkFormat format,
        VkImageAspectFlags aspectFlags)
    {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = format;
        ci.subresourceRange.aspectMask = aspectFlags;
        ci.subresourceRange.baseMipLevel = 0;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount = 1;
        VkImageView view;
        if (vkCreateImageView(_device, &ci, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView failed.");
        return view;
    }

    void createImageViews() {
        _swapChainImageViews.resize(_swapChainImages.size());
        for (size_t i = 0; i < _swapChainImages.size(); i++)
            _swapChainImageViews[i] = createImageView(
                _swapChainImages[i], _swapChainImageFormat,
                VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // ── Render pass  (color + depth) ─────────────────────────
    void createRenderPass() {
        // color attachment
        VkAttachmentDescription colorAtt{};
        colorAtt.format = _swapChainImageFormat;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // depth attachment
        VkAttachmentDescription depthAtt{};
        depthAtt.format = findDepthFormat();
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = { colorAtt, depthAtt };
        VkRenderPassCreateInfo rpCI{};
        rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCI.attachmentCount = (uint32_t)attachments.size();
        rpCI.pAttachments = attachments.data();
        rpCI.subpassCount = 1;
        rpCI.pSubpasses = &subpass;
        rpCI.dependencyCount = 1;
        rpCI.pDependencies = &dep;

        if (vkCreateRenderPass(_device, &rpCI, nullptr, &_renderPass) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed.");
    }

    // ── Descriptor set layout ────────────────────────────────
    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(_device, &ci, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout failed.");
    }

    // ── Graphics pipeline  (uses .vert / .frag via shaderc) ──
    void createGraphicsPipeline() {
        // ── Compile shaders at runtime ──────────────────────
        //auto vertCode = compileGLSL(readTextFile("shader.vert"), shaderc_glsl_vertex_shader, "shader.vert");
        //auto fragCode = compileGLSL(readTextFile("shader.frag"), shaderc_glsl_fragment_shader, "shader.frag");

        auto vertCode = readSPVFile("shader.vert.spv");
        auto fragCode = readSPVFile("shader.frag.spv");

        VkShaderModule vertModule = createShaderModule(_device, vertCode);
        VkShaderModule fragModule = createShaderModule(_device, fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

        // ── Vertex input ────────────────────────────────────
        auto bindDesc = Vertex::getBindingDescription();
        auto attrDesc = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputCI{};
        vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputCI.vertexBindingDescriptionCount = 1;
        vertexInputCI.pVertexBindingDescriptions = &bindDesc;
        vertexInputCI.vertexAttributeDescriptionCount = (uint32_t)attrDesc.size();
        vertexInputCI.pVertexAttributeDescriptions = attrDesc.data();

        // ── Input assembly  — LINE_LIST for axes ────────────
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
        inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        // ── Viewport / scissor (dynamic) ────────────────────
        VkPipelineViewportStateCreateInfo viewportCI{};
        viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportCI.viewportCount = 1;
        viewportCI.scissorCount = 1;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicCI{};
        dynamicCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicCI.dynamicStateCount = (uint32_t)dynamicStates.size();
        dynamicCI.pDynamicStates = dynamicStates.data();

        // ── Rasteriser ──────────────────────────────────────
        VkPipelineRasterizationStateCreateInfo rasterCI{};
        rasterCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterCI.lineWidth = 1.0f;          // thick axes
        rasterCI.cullMode = VK_CULL_MODE_NONE;
        rasterCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // ── Multisampling ───────────────────────────────────
        VkPipelineMultisampleStateCreateInfo msCI{};
        msCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // ── Depth / stencil  (depth test ON) ────────────────
        VkPipelineDepthStencilStateCreateInfo depthCI{};
        depthCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthCI.depthTestEnable = VK_TRUE;
        depthCI.depthWriteEnable = VK_TRUE;
        depthCI.depthCompareOp = VK_COMPARE_OP_LESS;
        depthCI.minDepthBounds = 0.0f;
        depthCI.maxDepthBounds = 1.0f;

        // ── Color blend ─────────────────────────────────────
        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blendCI{};
        blendCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blendCI.attachmentCount = 1;
        blendCI.pAttachments = &blendAtt;

        // ── Pipeline layout ──────────────────────────────────
        VkPipelineLayoutCreateInfo layoutCI{};
        layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCI.setLayoutCount = 1;
        layoutCI.pSetLayouts = &_descriptorSetLayout;

        if (vkCreatePipelineLayout(_device, &layoutCI, nullptr, &_pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout failed.");

        // Create triangle pipeline (for cube)
        VkGraphicsPipelineCreateInfo triCI{};
        triCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        triCI.stageCount = 2;
        triCI.pStages = stages;
        triCI.pVertexInputState = &vertexInputCI;
        // triangles
        inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        triCI.pInputAssemblyState = &inputAssemblyCI;
        triCI.pViewportState = &viewportCI;
        triCI.pRasterizationState = &rasterCI;
        triCI.pMultisampleState = &msCI;
        triCI.pDepthStencilState = &depthCI;
        triCI.pColorBlendState = &blendCI;
        triCI.pDynamicState = &dynamicCI;
        triCI.layout = _pipelineLayout;
        triCI.renderPass = _renderPass;
        triCI.subpass = 0;

        if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
            &triCI, nullptr,
            &_trianglePipeline) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (triangle) failed.");

        // Create line pipeline (for axes) - set input assembly back to lines
        inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        VkPipelineRasterizationStateCreateInfo lineRaster = rasterCI;
        lineRaster.lineWidth = 2.0f;

        VkGraphicsPipelineCreateInfo lineCI = triCI;
        lineCI.pInputAssemblyState = &inputAssemblyCI;
        lineCI.pRasterizationState = &lineRaster;
        // Enable depth test for axis lines so they can be occluded by the cube
        VkPipelineDepthStencilStateCreateInfo lineDepthCI = depthCI;
        lineDepthCI.depthTestEnable = VK_TRUE;
        lineDepthCI.depthWriteEnable = VK_TRUE;
        lineDepthCI.depthCompareOp = VK_COMPARE_OP_LESS;
        lineCI.pDepthStencilState = &lineDepthCI;

        if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
            &lineCI, nullptr,
            &_linePipeline) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (line) failed.");

        vkDestroyShaderModule(_device, vertModule, nullptr);
        vkDestroyShaderModule(_device, fragModule, nullptr);
    }

    // ── Command pool ─────────────────────────────────────────
    void createCommandPool() {
        auto idx = findQueueFamilies(_physicalDevice);
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = idx.graphicsFamily.value();
        if (vkCreateCommandPool(_device, &ci, nullptr, &_commandPool) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed.");
    }

    // ── Depth resources ──────────────────────────────────────
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features)
    {
        for (VkFormat fmt : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(_physicalDevice, fmt, &props);
            if (tiling == VK_IMAGE_TILING_LINEAR &&
                (props.linearTilingFeatures & features) == features) return fmt;
            if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                (props.optimalTilingFeatures & features) == features) return fmt;
        }
        throw std::runtime_error("Failed to find supported format.");
    }

    VkFormat findDepthFormat() {
        return findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT,
             VK_FORMAT_D32_SFLOAT_S8_UINT,
             VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("Failed to find suitable memory type.");
    }

    void createImage(uint32_t w, uint32_t h, VkFormat fmt,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkImage& image, VkDeviceMemory& memory)
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = { w, h, 1 };
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = fmt;
        ci.tiling = tiling;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = usage;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(_device, &ci, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage failed.");

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(_device, image, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);

        if (vkAllocateMemory(_device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (image) failed.");

        vkBindImageMemory(_device, image, memory, 0);
    }

    void createDepthResources() {
        VkFormat depthFormat = findDepthFormat();
        createImage(
            _swapChainExtent.width, _swapChainExtent.height,
            depthFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _depthImage, _depthImageMemory);
        _depthImageView = createImageView(
            _depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    // ── Framebuffers ─────────────────────────────────────────
    void createFramebuffers() {
        _swapChainFramebuffers.resize(_swapChainImageViews.size());
        for (size_t i = 0; i < _swapChainImageViews.size(); i++) {
            std::array<VkImageView, 2> attachments = {
                _swapChainImageViews[i], _depthImageView
            };
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = _renderPass;
            ci.attachmentCount = (uint32_t)attachments.size();
            ci.pAttachments = attachments.data();
            ci.width = _swapChainExtent.width;
            ci.height = _swapChainExtent.height;
            ci.layers = 1;
            if (vkCreateFramebuffer(_device, &ci, nullptr,
                &_swapChainFramebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed.");
        }
    }

    // ── Buffer helpers ───────────────────────────────────────
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer& buf, VkDeviceMemory& mem)
    {
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(_device, &ci, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("vkCreateBuffer failed.");

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(_device, buf, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);
        if (vkAllocateMemory(_device, &allocInfo, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory failed.");
        vkBindBufferMemory(_device, buf, mem, 0);
    }

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = _commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(_device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{ 0, 0, size };
        vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicsQueue);
        vkFreeCommandBuffers(_device, _commandPool, 1, &cmd);
    }

    // ── Vertex buffer ────────────────────────────────────────
    void createVertexBuffer() {
        auto cubeVerts = makeCubeVertices();

        _axisVertexCount = (uint32_t)AXIS_VERTICES.size();
        _cubeVertexCount = (uint32_t)cubeVerts.size();

        VkDeviceSize size = sizeof(Vertex) * (_axisVertexCount + _cubeVertexCount);

        VkBuffer       stagingBuf;
        VkDeviceMemory stagingMem;
        createBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuf, stagingMem);

        void* data;
        vkMapMemory(_device, stagingMem, 0, size, 0, &data);
        // copy axis then cube
        memcpy(data, AXIS_VERTICES.data(), sizeof(Vertex) * _axisVertexCount);
        memcpy((char*)data + sizeof(Vertex) * _axisVertexCount,
            cubeVerts.data(), sizeof(Vertex) * _cubeVertexCount);
        vkUnmapMemory(_device, stagingMem);

        createBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _vertexBuffer, _vertexBufferMemory);

        copyBuffer(stagingBuf, _vertexBuffer, size);
        vkDestroyBuffer(_device, stagingBuf, nullptr);
        vkFreeMemory(_device, stagingMem, nullptr);
    }

    // ── Uniform buffers ──────────────────────────────────────
    void createUniformBuffers() {
        VkDeviceSize bufSize = sizeof(UniformBufferObject);
        // We need two UBOs per frame: one for cube (triangles) and one for axes (lines)
        size_t totalUBOs = MAX_FRAMES_IN_FLIGHT * 2;
        _uniformBuffers.resize(totalUBOs);
        _uniformBuffersMemory.resize(totalUBOs);
        _uniformBuffersMapped.resize(totalUBOs);

        for (size_t i = 0; i < totalUBOs; i++) {
            createBuffer(bufSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                _uniformBuffers[i], _uniformBuffersMemory[i]);
            vkMapMemory(_device, _uniformBuffersMemory[i], 0, bufSize, 0,
                &_uniformBuffersMapped[i]);
        }
    }

    // ── Descriptor pool & sets ───────────────────────────────
    void createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // two UBOs per frame (cube + axes)
        poolSize.descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &poolSize;
        ci.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;

        if (vkCreateDescriptorPool(_device, &ci, nullptr, &_descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool failed.");
    }

    void createDescriptorSets() {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = _descriptorPool;
        // allocate descriptor sets: two per frame (cube + axes)
        ai.descriptorSetCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;
        std::vector<VkDescriptorSetLayout> setLayouts(ai.descriptorSetCount, _descriptorSetLayout);
        ai.pSetLayouts = setLayouts.data();

        _descriptorSets.resize(ai.descriptorSetCount);
        if (vkAllocateDescriptorSets(_device, &ai, _descriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateDescriptorSets failed.");

        // Update descriptor sets: for frame i, index 2*i => cube UBO, index 2*i+1 => axes UBO
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorBufferInfo bufCube{};
            bufCube.buffer = _uniformBuffers[2*i];
            bufCube.offset = 0;
            bufCube.range  = sizeof(UniformBufferObject);

            VkWriteDescriptorSet writeCube{};
            writeCube.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeCube.dstSet = _descriptorSets[2*i];
            writeCube.dstBinding = 0;
            writeCube.descriptorCount = 1;
            writeCube.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeCube.pBufferInfo = &bufCube;

            VkDescriptorBufferInfo bufAxes{};
            bufAxes.buffer = _uniformBuffers[2*i + 1];
            bufAxes.offset = 0;
            bufAxes.range  = sizeof(UniformBufferObject);

            VkWriteDescriptorSet writeAxes{};
            writeAxes.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeAxes.dstSet = _descriptorSets[2*i + 1];
            writeAxes.dstBinding = 0;
            writeAxes.descriptorCount = 1;
            writeAxes.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeAxes.pBufferInfo = &bufAxes;

            VkWriteDescriptorSet writes[2] = { writeCube, writeAxes };
            vkUpdateDescriptorSets(_device, 2, writes, 0, nullptr);
        }
    }

    // ── Command buffers ──────────────────────────────────────
    void createCommandBuffers() {
        _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = _commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)_commandBuffers.size();
        if (vkAllocateCommandBuffers(_device, &ai, _commandBuffers.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed.");
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed.");

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { 0.05f, 0.05f, 0.05f, 1.0f }; // dark bg
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBI{};
        rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBI.renderPass = _renderPass;
        rpBI.framebuffer = _swapChainFramebuffers[imageIndex];
        rpBI.renderArea.offset = { 0, 0 };
        rpBI.renderArea.extent = _swapChainExtent;
        rpBI.clearValueCount = (uint32_t)clearValues.size();
        rpBI.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.f;
        viewport.y = 0.f;
        viewport.width = (float)_swapChainExtent.width;
        viewport.height = (float)_swapChainExtent.height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{ {0, 0}, _swapChainExtent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkBuffer     vbs[] = { _vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
        // Bind descriptor set
        // Bind descriptor set for cube (each frame has two sets: cube then axes)
        uint32_t cubeDescIndex = _currentFrame * 2;
        uint32_t axesDescIndex = _currentFrame * 2 + 1;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            _pipelineLayout, 0, 1,
            &_descriptorSets[cubeDescIndex], 0, nullptr);

        // Draw cube triangles first (so depth test on)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
        vkCmdDraw(cmd, _cubeVertexCount, 1, _axisVertexCount, 0);

        // Then draw axes as lines on top using line pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _linePipeline);
        // bind axes descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            _pipelineLayout, 0, 1,
            &_descriptorSets[axesDescIndex], 0, nullptr);
        vkCmdDraw(cmd, _axisVertexCount, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed.");
    }

    // ── Sync objects ─────────────────────────────────────────
    void createSyncObjects() {
        _imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        _renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semCI{};
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceCI{};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(_device, &semCI, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(_device, &semCI, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(_device, &fenceCI, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create sync objects.");
        }
    }

    // ── Update UBO ───────────────────────────────────────────
    void updateUniformBuffer(uint32_t currentImage) {
        UniformBufferObject ubo{};

        ubo.model = glm::mat4(1.0f);

        glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
        glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));

        glm::vec3 viewX = quatY * quatX * glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);

        ubo.view = glm::lookAt(_coordinate - viewZ * 100.0f, _coordinate + viewZ * 100.0f, viewY);

        ubo.proj = glm::ortho(_ortho[0], _ortho[1], _ortho[2], _ortho[3], 0.1f, 200.0f);

        ubo.proj[1][1] *= -1; // flip Y for Vulkan NDC

        // write cube UBO into slot 2*currentImage
        void* mappedCube = _uniformBuffersMapped[2 * currentImage];
        memcpy(mappedCube, &ubo, sizeof(ubo));


        UniformBufferObject axesUbo = ubo;
        axesUbo.model = glm::mat4(1.0f);
        axesUbo.model = glm::translate(axesUbo.model, _coordinate);

        float scale = (_ortho[3] - _ortho[2]) * 0.4f;
        

        axesUbo.model = glm::scale(axesUbo.model, glm::vec3(scale, scale, scale));

        void* mappedAxes = _uniformBuffersMapped[2 * currentImage + 1];
        memcpy(mappedAxes, &axesUbo, sizeof(axesUbo));


 
    }

    // ── Swap-chain recreation ────────────────────────────────
    void cleanupSwapChain() {
        vkDestroyImageView(_device, _depthImageView, nullptr);
        vkDestroyImage(_device, _depthImage, nullptr);
        vkFreeMemory(_device, _depthImageMemory, nullptr);
        for (auto fb : _swapChainFramebuffers)
            vkDestroyFramebuffer(_device, fb, nullptr);
        for (auto iv : _swapChainImageViews)
            vkDestroyImageView(_device, iv, nullptr);
        vkDestroySwapchainKHR(_device, _swapChain, nullptr);
    }

    void recreateSwapChain() {
        int w = 0, h = 0;
        glfwGetFramebufferSize(_window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(_window, &w, &h);
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(_device);
        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        createDepthResources();
        createFramebuffers();
    }

    // ── Draw frame ───────────────────────────────────────────
    void drawFrame() {
        vkWaitForFences(_device, 1, &_inFlightFences[_currentFrame],
            VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(
            _device, _swapChain, UINT64_MAX,
            _imageAvailableSemaphores[_currentFrame],
            VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return; }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed.");

        vkResetFences(_device, 1, &_inFlightFences[_currentFrame]);
        vkResetCommandBuffer(_commandBuffers[_currentFrame], 0);
        updateUniformBuffer(_currentFrame);
        recordCommandBuffer(_commandBuffers[_currentFrame], imageIndex);

        VkSemaphore waitSems[] = { _imageAvailableSemaphores[_currentFrame] };
        VkSemaphore signalSems[] = { _renderFinishedSemaphores[_currentFrame] };
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSems;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_commandBuffers[_currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSems;

        if (vkQueueSubmit(_graphicsQueue, 1, &submitInfo,
            _inFlightFences[_currentFrame]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed.");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSems;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &_swapChain;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(_presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR || _framebufferResized) {
            _framebufferResized = false;
            recreateSwapChain();
        }
        else if (result != VK_SUCCESS) {
            throw std::runtime_error("vkQueuePresentKHR failed.");
        }

        _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // ── Main loop ────────────────────────────────────────────
    void mainLoop() {
        while (!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(_device);
    }

    // ── Full cleanup ─────────────────────────────────────────
    void cleanup() {
        cleanupSwapChain();

        // destroy all uniform buffers (we created 2 per frame)
        for (size_t i = 0; i < _uniformBuffers.size(); ++i) {
            vkDestroyBuffer(_device, _uniformBuffers[i], nullptr);
            vkFreeMemory(_device, _uniformBuffersMemory[i], nullptr);
        }
        vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        vkDestroyBuffer(_device, _vertexBuffer, nullptr);
        vkFreeMemory(_device, _vertexBufferMemory, nullptr);
        vkDestroyPipeline(_device, _trianglePipeline, nullptr);
        vkDestroyPipeline(_device, _linePipeline, nullptr);
        vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
        vkDestroyRenderPass(_device, _renderPass, nullptr);
        vkDestroyCommandPool(_device, _commandPool, nullptr);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(_device, _imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(_device, _renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(_device, _inFlightFences[i], nullptr);
        }
        vkDestroyDevice(_device, nullptr);

        if (ENABLE_VALIDATION) {
            auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
            if (fn) fn(_instance, _debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyInstance(_instance, nullptr);
        glfwDestroyWindow(_window);
        glfwTerminate();
    }
};

int main() {
    try {
        CoordApp app;
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
