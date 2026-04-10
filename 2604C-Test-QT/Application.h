#pragma once

#include "Window.h"
#include <cstdint>
#include <string>
#include <vector>

class QWindow;
class QVulkanInstance;

class Application : public Window {
public:
    struct ViewState {
        glm::vec3 coordinate{};
        glm::vec2 rotation{};
        glm::vec4 ortho{};
        glm::ivec2 viewport{};
    };

    void attachAndInit(QWindow* w, QVulkanInstance* inst);

    void shutdownBeforeQtTeardown();

    ViewState viewStateSnapshot() const;
    void applyViewState(const ViewState& s);

    std::string cursorReadoutString() { return getCursorReadoutText(); }

    void openObjFile(const std::string& path);
    void showCubeMesh();
    void setViewRotation(float degreesX, float degreesY);
    void runLscmAndShowMesh();

    bool captureDepthBufferFloat01(std::vector<float>& outDepth, uint32_t& outW, uint32_t& outH);

private:
    std::vector<Vertex> buildCubeVertices() const;
    std::vector<Vertex> loadObjTriangles(const std::string& path) const;
    void showFlattenedMesh(const std::string& objPath);
    bool _showCube = false;
};
