
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
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <set>
#include <stdexcept>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

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

static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

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

// 
//  Debug messenger
// 
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    std::cerr << "[VK] " << pData->pMessage << '\n';
    return VK_FALSE;
}

// 
//  Application  (all member variables start with  _ )
// 
class Window {
public:
    ~Window() = default;

protected:
    enum class MeshDisplayMode {
        Line,
        Face,
        LineAndFace
    };

    void initializeWindowSystem();
    void shutdownWindowSystem();
    bool shouldClose() const;
    void pollWindowEvents();
    void beginImGuiFrame();
    void drawFrame();
    void rebuildVertexBuffer();
    void setApplicationVertices(const std::vector<Vertex>& vertices);
    void setApplicationModelTransform(const glm::vec3& translation, const glm::vec3& scale);
    glm::ivec2 getViewportSize() const;
    void setViewportSize(const glm::ivec2& size);
    glm::vec2 getRotation() const;
    void setRotation(const glm::vec2& rotationDeg);
    std::string getCursorReadoutText();
    void enforceOrthoAspectFromWindow();

    void cleanup();

    // Editable view parameters for subclasses (Application).
    glm::vec3 _coordinate = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec2 _rotation = glm::vec2(0.0f, 0.0f);
    glm::vec4 _ortho = glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
    glm::ivec2 _viewport = glm::ivec2(1200, 800);
    MeshDisplayMode _meshDisplayMode = MeshDisplayMode::LineAndFace;

private:
    // window
    GLFWwindow* _window = nullptr;
    
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
    VkPipeline            _rulerPipeline = VK_NULL_HANDLE;

    // buffers
    VkBuffer       _vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _vertexBufferMemory = VK_NULL_HANDLE;
    uint32_t       _axisVertexCount = 0;
    uint32_t       _rulerVertexCount = 0;
    uint32_t       _applicationTriangleVertexCount = 0;
    uint32_t       _applicationLineVertexCount = 0;
    std::vector<Vertex> _applicationVertices;
    glm::vec3 _applicationModelTranslation = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 _applicationModelScale = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec4 _cachedOrthoForOverlay = glm::vec4(0.0f);
    bool _hasCachedOrthoForOverlay = false;
    uint32_t _cachedOverlayExtentWidth = 0;
    uint32_t _cachedOverlayExtentHeight = 0;
    int _cachedMouseOverlayX = -1;
    int _cachedMouseOverlayY = -1;
    bool _overlayDirty = true;

    std::vector<VkBuffer>       _uniformBuffers;
    std::vector<VkDeviceMemory> _uniformBuffersMemory;
    std::vector<void*>          _uniformBuffersMapped;

    // descriptors
    VkDescriptorPool             _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool             _imguiDescriptorPool = VK_NULL_HANDLE;
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
    // Track previous modifier state to avoid first-drag jumps.
    bool _shiftPressedPrev = false;
    bool _ctrlPressedPrev = false;
    bool _altPressedPrev = false;
    glm::vec3 _lastSavedCoordinate = _coordinate;
    glm::vec2 _lastSavedRotation = _rotation;
    glm::vec4 _lastSavedOrtho = _ortho;
    glm::ivec2 _lastSavedViewport = _viewport;
    bool _hasSavedViewState = false;

    //  Window 
    void initWindow();
    static void onFramebufferResize(GLFWwindow* w, int, int);
    static void onCursorPos(GLFWwindow* w, double x, double y);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onScroll(GLFWwindow* w, double xoffset, double yoffset);
    glm::mat4 buildViewMatrix() const;
    glm::mat4 buildProjMatrix() const;

    bool hasStencilComponent(VkFormat format) const;

    bool readDepthAtFramebufferPixel(uint32_t px, uint32_t py, float& outDepth01);

    bool readDepthAtCursor(double mouseX, double mouseY, float& outDepth01);

    glm::vec3 screenToWorldByDepth(double mouseX, double mouseY, float depth01) const;

    // Returns (screenX, screenY, depth01) for a world-space point.
    glm::vec3 worldToScreen(const glm::vec3& world) const;

    // Instance handlers for input
    void cursorPosCallbackImpl(double xpos, double ypos);

    void mouseButtonCallbackImpl(int button, int action, int mods);

    void scrollCallbackImpl(double /*xoffset*/, double yoffset);

    // Persist view parameters in config.xml.
    bool loadViewConfig();
    bool saveViewConfig() const;
    void saveViewConfigIfChanged();

    //  Vulkan init sequence 
    void initVulkan();

    void initImGui();

    //  Instance 
    void createInstance();

    bool checkValidationLayerSupport();

    std::vector<const char*> getRequiredExtensions();

    //  Debug messenger 
    void fillDebugMessengerCI(VkDebugUtilsMessengerCreateInfoEXT& ci);

    void setupDebugMessenger();

    //  Surface 
    void createSurface();

    //  Physical device 
    void pickPhysicalDevice();

    bool isDeviceSuitable(VkPhysicalDevice dev);

    bool checkDeviceExtensionSupport(VkPhysicalDevice dev);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev);

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice dev);

    //  Logical device 
    void createLogicalDevice();

    //  Swap chain 
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& fmts);

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps);

    void createSwapChain();

    //  Image views 
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    void createImageViews();

    //  Render pass  (color + depth) 
    void createRenderPass();

    //  Descriptor set layout 
    void createDescriptorSetLayout();

    //  Graphics pipeline  (uses .vert / .frag via shaderc) 
    void createGraphicsPipeline();
    void createRulerPipeline();

    //  Command pool 
    void createCommandPool();

    //  Depth resources 
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    VkFormat findDepthFormat();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);

    void createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props, VkImage& image, VkDeviceMemory& memory);

    void createDepthResources();

    //  Framebuffers 
    void createFramebuffers();

    //  Buffer helpers 
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    //  Vertex buffer 
    void createVertexBuffer();

    //  Uniform buffers 
    void createUniformBuffers();

    //  Descriptor pool & sets 
    void createDescriptorPool();

    void createDescriptorSets();

    //  Command buffers 
    void createCommandBuffers();

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    //  Sync objects 
    void createSyncObjects();

    //  Update UBO 
    void updateUniformBuffer(uint32_t currentImage);

    //  Swap-chain recreation 
    void cleanupSwapChain();

    void recreateSwapChain();

    //  Main loop 
    void mainLoop();

};
