
#include "Window.h"

// 
//  Shader helpers  (readTextFile / compileGLSL / createShaderModule)
// 
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

static std::vector<char> readSPVFile(const std::string& filename) {
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

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    // ????Vulkan ??? uint32_t* ? reinterpret_cast
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

// 
//  Data structures
// 

// X = Red (1,0,0)  |  Y = Blue (0,0,1)  |  Z = Green (0,1,0)
// Each axis: shaft + two arrow-head lines
// For axis lines we still use Vertex type; normals set to zero
static const std::vector<Vertex> AXIS_VERTICES = {
    //  X  (Red) 
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {1.f, 0.f, 0.f}},
    {{ 1.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {1.f, 0.f, 0.f}},

    //  Y  (Blue) 
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 0.f, 1.f}},
    {{ 0.00f,  1.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 0.f, 1.f}},

    //  Z  (Green) 
    {{ 0.00f,  0.00f,  0.00f}, {0.f,0.f,0.f}, {0.f, 1.f, 0.f}},
    {{ 0.00f,  0.00f,  1.00f}, {0.f,0.f,0.f}, {0.f, 1.f, 0.f}},
};

void Window::initializeWindowSystem() {
    loadViewConfig();
    initWindow();
    initVulkan();
}

void Window::shutdownWindowSystem() {
     cleanup();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(_window);
}

void Window::pollWindowEvents() {
    glfwPollEvents();
}

void Window::beginImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

bool Window::loadViewConfig() {
    const std::filesystem::path configPath = "config.xml";
    std::ifstream input(configPath);
    if (!input.is_open()) {
        return false;
    }

    const std::string xml((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());

    auto readTagFloat = [&](const char* tag, float& outValue) -> bool {
        const std::string openTag = std::string("<") + tag + ">";
        const std::string closeTag = std::string("</") + tag + ">";
        const std::size_t start = xml.find(openTag);
        if (start == std::string::npos) {
            return false;
        }
        const std::size_t valueBegin = start + openTag.size();
        const std::size_t end = xml.find(closeTag, valueBegin);
        if (end == std::string::npos) {
            return false;
        }
        try {
            outValue = std::stof(xml.substr(valueBegin, end - valueBegin));
            return true;
        }
        catch (...) {
            return false;
        }
    };

    float coordinateX = _coordinate.x;
    float coordinateY = _coordinate.y;
    float coordinateZ = _coordinate.z;
    float rotationX = _rotation.x;
    float rotationY = _rotation.y;
    float orthoL = _ortho.x;
    float orthoR = _ortho.y;
    float orthoB = _ortho.z;
    float orthoT = _ortho.w;

    const bool ok =
        readTagFloat("coordinateX", coordinateX) &&
        readTagFloat("coordinateY", coordinateY) &&
        readTagFloat("coordinateZ", coordinateZ) &&
        readTagFloat("rotationX", rotationX) &&
        readTagFloat("rotationY", rotationY) &&
        readTagFloat("orthoL", orthoL) &&
        readTagFloat("orthoR", orthoR) &&
        readTagFloat("orthoB", orthoB) &&
        readTagFloat("orthoT", orthoT);

    if (!ok) {
        return false;
    }

    _coordinate = glm::vec3(coordinateX, coordinateY, coordinateZ);
    _rotation = glm::vec2(rotationX, rotationY);
    _ortho = glm::vec4(orthoL, orthoR, orthoB, orthoT);

    _lastSavedCoordinate = _coordinate;
    _lastSavedRotation = _rotation;
    _lastSavedOrtho = _ortho;
    _hasSavedViewState = true;
    return true;
}

bool Window::saveViewConfig() const {
    const std::filesystem::path configPath = "config.xml";
    std::ofstream output(configPath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    output << "<Config>\n";
    output << "    <View>\n";
    output << std::setprecision(9);
    output << "        <coordinateX>" << _coordinate.x << "</coordinateX>\n";
    output << "        <coordinateY>" << _coordinate.y << "</coordinateY>\n";
    output << "        <coordinateZ>" << _coordinate.z << "</coordinateZ>\n";
    output << "        <rotationX>" << _rotation.x << "</rotationX>\n";
    output << "        <rotationY>" << _rotation.y << "</rotationY>\n";
    output << "        <orthoL>" << _ortho.x << "</orthoL>\n";
    output << "        <orthoR>" << _ortho.y << "</orthoR>\n";
    output << "        <orthoB>" << _ortho.z << "</orthoB>\n";
    output << "        <orthoT>" << _ortho.w << "</orthoT>\n";
    output << "    </View>\n";
    output << "</Config>\n";

    return output.good();
}

void Window::saveViewConfigIfChanged() {
    const bool changed =
        !_hasSavedViewState ||
        _coordinate != _lastSavedCoordinate ||
        _rotation != _lastSavedRotation ||
        _ortho != _lastSavedOrtho;

    if (!changed) {
        return;
    }

    if (saveViewConfig()) {
        _lastSavedCoordinate = _coordinate;
        _lastSavedRotation = _rotation;
        _lastSavedOrtho = _ortho;
        _hasSavedViewState = true;
    }
}

void Window::cleanup() {
    cleanupSwapChain();

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

void Window::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(_viewport[0], _viewport[1], "MINGJIE2026@GMAIL.COM", nullptr, nullptr);
    glfwSetWindowUserPointer(_window, this);
    glfwSetFramebufferSizeCallback(_window, onFramebufferResize);
    glfwSetCursorPosCallback(_window, onCursorPos);
    glfwSetMouseButtonCallback(_window, onMouseButton);
    glfwSetScrollCallback(_window, onScroll);
}

void Window::onFramebufferResize(GLFWwindow* w, int, int) {
    reinterpret_cast<Window*>(glfwGetWindowUserPointer(w))->_framebufferResized = true;
}

void Window::onCursorPos(GLFWwindow* w, double x, double y) {
    auto app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(w));
    if (app) app->cursorPosCallbackImpl(x, y);
}

void Window::onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    auto app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(w));
    if (app) app->mouseButtonCallbackImpl(button, action, mods);
}

void Window::onScroll(GLFWwindow* w, double xoffset, double yoffset) {
    auto app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(w));
    if (app) app->scrollCallbackImpl(xoffset, yoffset);
}

glm::mat4 Window::buildViewMatrix() const {
    glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
    glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));
    glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::lookAt(_coordinate - viewZ * 100.0f, _coordinate + viewZ * 100.0f, viewY);
}

glm::mat4 Window::buildProjMatrix() const {
    glm::mat4 proj = glm::ortho(_ortho[0], _ortho[1], _ortho[2], _ortho[3], 0.1f, 200.0f);
    proj[1][1] *= -1.0f;
    return proj;
}


bool Window::hasStencilComponent(VkFormat format) const {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool Window::readDepthAtFramebufferPixel(uint32_t px, uint32_t py, float& outDepth01) {
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
    }
    else if (depthFmt == VK_FORMAT_D24_UNORM_S8_UINT) {
        outDepth01 = static_cast<float>(packed & 0x00FFFFFFu) / 16777215.0f;
    }
    else {
        return false;
    }

    outDepth01 = std::clamp(outDepth01, 0.0f, 1.0f);
    // If depth is still clear value, no entity was drawn at this pixel.
    if (outDepth01 >= 0.999999f) {
        return false;
    }
    return true;
}

bool Window::readDepthAtCursor(double mouseX, double mouseY, float& outDepth01) {
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

glm::vec3 Window::screenToWorldByDepth(double mouseX, double mouseY, float depth01) const {
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

glm::vec3 Window::worldToScreen(const glm::vec3& world) const {
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

void Window::cursorPosCallbackImpl(double xpos, double ypos) {
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

void Window::mouseButtonCallbackImpl(int button, int action, int mods) {
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

                    // Keep the picked coordinate visually under the cursor by compensating ortho window.
                    _ortho[0] -= transX;
                    _ortho[1] -= transX;
                    _ortho[2] += transY;
                    _ortho[3] += transY;

                    _coordinate = pickedCoordinate;
                }
                else {
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

void Window::scrollCallbackImpl(double /*xoffset*/, double yoffset) {
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

void Window::initVulkan() {
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
    initImGui();
}

void Window::initImGui() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool.");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.0f;
    ImFontConfig fontCfg{};
    fontCfg.RasterizerMultiply = 1.35f; // Thicken glyphs to make text appear bolder.
    const std::array<std::filesystem::path, 4> boldFontCandidates = {
        std::filesystem::path("C:/Windows/Fonts/arialbd.ttf"),
        std::filesystem::path("C:/Windows/Fonts/segoeuib.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
        std::filesystem::path("/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf")
    };
    bool loadedBoldFont = false;
    for (const auto& fontPath : boldFontCandidates) {
        if (std::filesystem::exists(fontPath)) {
            if (ImFont* bold = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f, &fontCfg)) {
                io.FontDefault = bold;
                loadedBoldFont = true;
                break;
            }
        }
    }
    if (!loadedBoldFont) {
        io.FontDefault = io.Fonts->AddFontDefault(&fontCfg);
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(_window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
    initInfo.Instance = _instance;
    initInfo.PhysicalDevice = _physicalDevice;
    initInfo.Device = _device;
    initInfo.QueueFamily = findQueueFamilies(_physicalDevice).graphicsFamily.value();
    initInfo.Queue = _graphicsQueue;
    initInfo.DescriptorPool = _imguiDescriptorPool;
    initInfo.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    initInfo.ImageCount = static_cast<uint32_t>(_swapChainImages.size());
    initInfo.PipelineInfoMain.RenderPass = _renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed.");
    }
}

void Window::createInstance() {
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

bool Window::checkValidationLayerSupport() {
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

std::vector<const char*> Window::getRequiredExtensions() {
    uint32_t cnt = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&cnt);
    std::vector<const char*> exts(glfwExts, glfwExts + cnt);
    if (ENABLE_VALIDATION) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return exts;
}

void Window::fillDebugMessengerCI(VkDebugUtilsMessengerCreateInfoEXT& ci) {
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

void Window::setupDebugMessenger() {
    if (!ENABLE_VALIDATION) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    fillDebugMessengerCI(ci);
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
    if (!fn || fn(_instance, &ci, nullptr, &_debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("Failed to setup debug messenger.");
}

void Window::createSurface() {
    if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface.");
}

void Window::pickPhysicalDevice() {
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

bool Window::isDeviceSuitable(VkPhysicalDevice dev) {
    auto idx = findQueueFamilies(dev);
    bool extOk = checkDeviceExtensionSupport(dev);
    bool swapOk = false;
    if (extOk) {
        auto sc = querySwapChainSupport(dev);
        swapOk = !sc.formats.empty() && !sc.presentModes.empty();
    }
    return idx.isComplete() && extOk && swapOk;
}

bool Window::checkDeviceExtensionSupport(VkPhysicalDevice dev) {
    uint32_t cnt;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, nullptr);
    std::vector<VkExtensionProperties> available(cnt);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &cnt, available.data());
    std::set<std::string> required(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
    for (auto& e : available) required.erase(e.extensionName);
    return required.empty();
}

QueueFamilyIndices Window::findQueueFamilies(VkPhysicalDevice dev) {
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

SwapChainSupportDetails Window::querySwapChainSupport(VkPhysicalDevice dev) {
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

void Window::createLogicalDevice() {
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

VkExtent2D Window::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;
    int w, h;
    glfwGetFramebufferSize(_window, &w, &h);
    return {
        std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

VkSurfaceFormatKHR Window::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& fmts) {
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return fmts[0];
}

VkPresentModeKHR Window::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto& m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

void Window::createSwapChain() {
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

void Window::createImageViews() {
    _swapChainImageViews.resize(_swapChainImages.size());
    for (size_t i = 0; i < _swapChainImages.size(); i++)
        _swapChainImageViews[i] = createImageView(
            _swapChainImages[i], _swapChainImageFormat,
            VK_IMAGE_ASPECT_COLOR_BIT);
}

VkImageView Window::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
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
    if (vkCreateImageView(_device, &ci, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView failed.");
    }
    return view;
}

void Window::createRenderPass() {
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

void Window::createDescriptorSetLayout() {
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

void Window::createGraphicsPipeline() {
    //  Compile shaders at runtime 
    //auto vertCode = compileGLSL(readTextFile("shader.vert"), shaderc_glsl_vertex_shader, "shader.vert");
    //auto fragCode = compileGLSL(readTextFile("shader.frag"), shaderc_glsl_fragment_shader, "shader.frag");

    auto vertCode = readSPVFile("shaders/shader.vert.spv");
    auto fragCode = readSPVFile("shaders/shader.frag.spv");

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

    //  Vertex input 
    auto bindDesc = Vertex::getBindingDescription();
    auto attrDesc = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputCI{};
    vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputCI.vertexBindingDescriptionCount = 1;
    vertexInputCI.pVertexBindingDescriptions = &bindDesc;
    vertexInputCI.vertexAttributeDescriptionCount = (uint32_t)attrDesc.size();
    vertexInputCI.pVertexAttributeDescriptions = attrDesc.data();

    //  Input assembly   LINE_LIST for axes 
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
    inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    //  Viewport / scissor (dynamic) 
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

    //  Rasteriser 
    VkPipelineRasterizationStateCreateInfo rasterCI{};
    rasterCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterCI.lineWidth = 1.0f;          // thick axes
    rasterCI.cullMode = VK_CULL_MODE_NONE;
    rasterCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    //  Multisampling 
    VkPipelineMultisampleStateCreateInfo msCI{};
    msCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    //  Depth / stencil  (depth test ON) 
    VkPipelineDepthStencilStateCreateInfo depthCI{};
    depthCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthCI.depthTestEnable = VK_TRUE;
    depthCI.depthWriteEnable = VK_TRUE;
    depthCI.depthCompareOp = VK_COMPARE_OP_LESS;
    depthCI.minDepthBounds = 0.0f;
    depthCI.maxDepthBounds = 1.0f;

    //  Color blend 
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendCI{};
    blendCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendCI.attachmentCount = 1;
    blendCI.pAttachments = &blendAtt;

    //  Pipeline layout 
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
    // Draw lines as an overlay on triangle surfaces.
    VkPipelineDepthStencilStateCreateInfo lineDepthCI = depthCI;
    lineDepthCI.depthTestEnable = VK_TRUE;
    lineDepthCI.depthWriteEnable = VK_FALSE;
    lineDepthCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    lineCI.pDepthStencilState = &lineDepthCI;

    if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1,
        &lineCI, nullptr,
        &_linePipeline) != VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines (line) failed.");

    vkDestroyShaderModule(_device, vertModule, nullptr);
    vkDestroyShaderModule(_device, fragModule, nullptr);
}

void Window::createCommandPool() {
    auto idx = findQueueFamilies(_physicalDevice);
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = idx.graphicsFamily.value();
    if (vkCreateCommandPool(_device, &ci, nullptr, &_commandPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateCommandPool failed.");
}

VkFormat Window::findDepthFormat() {
    return findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat Window::findSupportedFormat(const std::vector<VkFormat>& candidates,
VkImageTiling tiling,
VkFormatFeatureFlags features) {
    for (VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, fmt, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return fmt;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return fmt;
        }
    }
    throw std::runtime_error("Failed to find supported format.");
}

uint32_t Window::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("Failed to find suitable memory type.");
}

void Window::createImage(uint32_t w, uint32_t h, VkFormat fmt,
VkImageTiling tiling, VkImageUsageFlags usage,
VkMemoryPropertyFlags props,
VkImage& image, VkDeviceMemory& memory) {
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

    if (vkCreateImage(_device, &ci, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage failed.");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(_device, image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (image) failed.");
    }

    vkBindImageMemory(_device, image, memory, 0);
}

void Window::createDepthResources() {
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

void Window::createFramebuffers() {
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

void Window::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
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

void Window::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
VkMemoryPropertyFlags props,
VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_device, &ci, nullptr, &buf) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer failed.");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(_device, buf, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);
    if (vkAllocateMemory(_device, &allocInfo, nullptr, &mem) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory failed.");
    }
    vkBindBufferMemory(_device, buf, mem, 0);
}

void Window::createVertexBuffer() {
    _axisVertexCount = (uint32_t)AXIS_VERTICES.size();
    _applicationVertexCount = static_cast<uint32_t>(_applicationVertices.size());
    _applicationLineVertexCount = static_cast<uint32_t>(_applicationLineVertices.size());
    const size_t totalVertexCount =
        static_cast<size_t>(_axisVertexCount) +
        static_cast<size_t>(_applicationVertexCount) +
        static_cast<size_t>(_applicationLineVertexCount);
    VkDeviceSize size = sizeof(Vertex) * totalVertexCount;

    VkBuffer       stagingBuf;
    VkDeviceMemory stagingMem;
    createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuf, stagingMem);

    void* data;
    vkMapMemory(_device, stagingMem, 0, size, 0, &data);
    memcpy(data, AXIS_VERTICES.data(), sizeof(Vertex) * _axisVertexCount);
    if (_applicationVertexCount > 0) {
        memcpy(static_cast<char*>(data) + sizeof(Vertex) * _axisVertexCount,
            _applicationVertices.data(),
            sizeof(Vertex) * _applicationVertexCount);
    }
    if (_applicationLineVertexCount > 0) {
        memcpy(static_cast<char*>(data) + sizeof(Vertex) * (_axisVertexCount + _applicationVertexCount),
            _applicationLineVertices.data(),
            sizeof(Vertex) * _applicationLineVertexCount);
    }
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

void Window::createUniformBuffers() {
    VkDeviceSize bufSize = sizeof(UniformBufferObject);
    // One UBO for the scene transform and one UBO for the axis transform.
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

void Window::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // Two uniform descriptors are used per frame.
    poolSize.descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &poolSize;
    ci.maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;

    if (vkCreateDescriptorPool(_device, &ci, nullptr, &_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorPool failed.");
}

void Window::createDescriptorSets() {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = _descriptorPool;
    // Allocate two descriptor sets per frame.
    ai.descriptorSetCount = (uint32_t)MAX_FRAMES_IN_FLIGHT * 2;
    std::vector<VkDescriptorSetLayout> setLayouts(ai.descriptorSetCount, _descriptorSetLayout);
    ai.pSetLayouts = setLayouts.data();

    _descriptorSets.resize(ai.descriptorSetCount);
    if (vkAllocateDescriptorSets(_device, &ai, _descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateDescriptorSets failed.");

    // Update descriptor sets for both per-frame UBOs.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufCube{};
        bufCube.buffer = _uniformBuffers[2 * i];
        bufCube.offset = 0;
        bufCube.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet writeCube{};
        writeCube.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeCube.dstSet = _descriptorSets[2 * i];
        writeCube.dstBinding = 0;
        writeCube.descriptorCount = 1;
        writeCube.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeCube.pBufferInfo = &bufCube;

        VkDescriptorBufferInfo bufAxes{};
        bufAxes.buffer = _uniformBuffers[2 * i + 1];
        bufAxes.offset = 0;
        bufAxes.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet writeAxes{};
        writeAxes.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeAxes.dstSet = _descriptorSets[2 * i + 1];
        writeAxes.dstBinding = 0;
        writeAxes.descriptorCount = 1;
        writeAxes.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeAxes.pBufferInfo = &bufAxes;

        VkWriteDescriptorSet writes[2] = { writeCube, writeAxes };
        vkUpdateDescriptorSets(_device, 2, writes, 0, nullptr);
    }
}

void Window::createCommandBuffers() {
    _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = _commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)_commandBuffers.size();
    if (vkAllocateCommandBuffers(_device, &ai, _commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateCommandBuffers failed.");
}

void Window::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
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
    // Bind the scene descriptor set.
    uint32_t cubeDescIndex = _currentFrame * 2;
    uint32_t axesDescIndex = _currentFrame * 2 + 1;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _pipelineLayout, 0, 1,
        &_descriptorSets[cubeDescIndex], 0, nullptr);

    if (_applicationVertexCount > 0) {
        const bool drawLine = (_meshDisplayMode == MeshDisplayMode::Line ||
                               _meshDisplayMode == MeshDisplayMode::LineAndFace);
        const bool drawFace = (_meshDisplayMode == MeshDisplayMode::Face ||
                               _meshDisplayMode == MeshDisplayMode::LineAndFace);

        if (drawFace) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
            vkCmdDraw(cmd, _applicationVertexCount, 1, _axisVertexCount, 0);
        }
        if (drawLine && _applicationLineVertexCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _linePipeline);
            vkCmdDraw(cmd, _applicationLineVertexCount, 1, _axisVertexCount + _applicationVertexCount, 0);
        }
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _linePipeline);
    // Bind axis descriptor set.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        _pipelineLayout, 0, 1,
        &_descriptorSets[axesDescIndex], 0, nullptr);
    vkCmdDraw(cmd, _axisVertexCount, 1, 0, 0);

    // Submit ImGui draw lists to the same active render pass.
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("vkEndCommandBuffer failed.");
}

void Window::createSyncObjects() {
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

void Window::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};

    ubo.model = glm::mat4(1.0f);

    glm::quat quatX = glm::angleAxis(glm::radians(-_rotation[0]), glm::vec3(1, 0, 0));
    glm::quat quatY = glm::angleAxis(glm::radians(-_rotation[1]), glm::vec3(0, 1, 0));

    glm::vec3 viewY = quatY * quatX * glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 viewZ = quatY * quatX * glm::vec3(0.0f, 0.0f, -1.0f);

    ubo.view = glm::lookAt(_coordinate - viewZ * 100.0f, _coordinate + viewZ * 100.0f, viewY);

    // Keep ortho aspect locked to app framebuffer aspect:
    // (orthoT - orthoB) / (orthoR - orthoL) == screenHeight / screenWidth
    const float orthoWidth = _ortho[1] - _ortho[0];
    if ((orthoWidth > 1e-6f || orthoWidth < -1e-6f) && _swapChainExtent.width > 0 && _swapChainExtent.height > 0) {
        const float targetAspect = static_cast<float>(_swapChainExtent.height) /
                                   static_cast<float>(_swapChainExtent.width);
        const float centerY = 0.5f * (_ortho[2] + _ortho[3]);
        const float orthoHeight = orthoWidth * targetAspect;
        _ortho[2] = centerY - 0.5f * orthoHeight;
        _ortho[3] = centerY + 0.5f * orthoHeight;
    }

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

void Window::cleanupSwapChain() {
    vkDestroyImageView(_device, _depthImageView, nullptr);
    vkDestroyImage(_device, _depthImage, nullptr);
    vkFreeMemory(_device, _depthImageMemory, nullptr);
    for (auto fb : _swapChainFramebuffers)
        vkDestroyFramebuffer(_device, fb, nullptr);
    for (auto iv : _swapChainImageViews)
        vkDestroyImageView(_device, iv, nullptr);
    vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}

void Window::recreateSwapChain() {
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

void Window::drawFrame() {
    saveViewConfigIfChanged();

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

void Window::setApplicationVertices(const std::vector<Vertex>& vertices) {
    _applicationVertices = vertices;
    _applicationLineVertices.clear();
    _applicationLineVertices.reserve((vertices.size() / 3) * 6);

    auto addEdge = [&](const Vertex& v0, const Vertex& v1) {
        _applicationLineVertices.push_back(v0);
        _applicationLineVertices.push_back(v1);
    };

    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        // Each 3 vertices are one triangle: (v0, v1, v2).
        // Build the 3 triangle edges for LINE_LIST rendering.
        const Vertex& v0 = vertices[i];
        const Vertex& v1 = vertices[i + 1];
        const Vertex& v2 = vertices[i + 2];
        addEdge(v0, v1);
        addEdge(v1, v2);
        addEdge(v2, v0);
    }
}

void Window::mainLoop() {
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();
        drawFrame();
    }
    vkDeviceWaitIdle(_device);
}

void Window::rebuildVertexBuffer() {
    vkDeviceWaitIdle(_device);
    if (_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(_device, _vertexBuffer, nullptr);
        _vertexBuffer = VK_NULL_HANDLE;
    }
    if (_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(_device, _vertexBufferMemory, nullptr);
        _vertexBufferMemory = VK_NULL_HANDLE;
    }
    createVertexBuffer();
}
