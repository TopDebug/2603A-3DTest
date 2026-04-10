#include "Application.h"
#include "Test.h"

#include <QVulkanInstance>
#include <QWindow>

#include <cfloat>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool pathLooksLikeObj(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".obj";
}
} // namespace

void Application::attachAndInit(QWindow* w, QVulkanInstance* inst) {
    attachQtWindow(w, inst);
}

void Application::shutdownBeforeQtTeardown() {
    shutdownWindowSystem();
}

Application::ViewState Application::viewStateSnapshot() const {
    ViewState s;
    s.coordinate = _coordinate;
    s.rotation = getRotation();
    s.ortho = _ortho;
    s.viewport = getViewportSize();
    return s;
}

void Application::applyViewState(const ViewState& s) {
    _coordinate = s.coordinate;
    setRotation(s.rotation);
    _ortho = s.ortho;
    setViewportSize(s.viewport);
    markOverlayDirty();
}

void Application::openObjFile(const std::string& path) {
    if (!pathLooksLikeObj(path)) {
        std::cout << "Open: not an OBJ file: " << path << '\n';
        return;
    }
    _showCube = false;
    showFlattenedMesh(path);
}

void Application::showCubeMesh() {
    _showCube = true;
    setApplicationVertices(buildCubeVertices());
    rebuildVertexBuffer();
}

void Application::setViewRotation(float degreesX, float degreesY) {
    setRotation(glm::vec2(degreesX, degreesY));
}

void Application::runLscmAndShowMesh() {
    lscm();
    showFlattenedMesh("models/qian_NL.obj");
}

bool Application::captureDepthBufferFloat01(std::vector<float>& outDepth, uint32_t& outW, uint32_t& outH) {
    drawFrame();
    drawFrame();
    return readDepthBufferFloat01(outDepth, outW, outH);
}

std::vector<Vertex> Application::buildCubeVertices() const {
    if (!_showCube) {
        return {};
    }

    std::vector<Vertex> v;
    glm::vec3 topColor = glm::vec3(0.0f, 0.0f, 0.5f);
    glm::vec3 bottomColor = glm::vec3(0.0f, 0.0f, 0.4f);
    glm::vec3 frontColor = glm::vec3(0.0f, 0.5f, 0.0f);
    glm::vec3 backColor = glm::vec3(0.0f, 0.4f, 0.0f);
    glm::vec3 leftColor = glm::vec3(0.4f, 0.0f, 0.0f);
    glm::vec3 rightColor = glm::vec3(0.5f, 0.0f, 0.0f);

    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        v.push_back(Vertex{ a, n, color });
        v.push_back(Vertex{ b, n, color });
        v.push_back(Vertex{ c, n, color });
        v.push_back(Vertex{ c, n, color });
        v.push_back(Vertex{ d, n, color });
        v.push_back(Vertex{ a, n, color });
    };

    glm::vec3 p000 = { -1.0f, -1.0f, -1.0f };
    glm::vec3 p001 = { -1.0f, -1.0f, 1.0f };
    glm::vec3 p010 = { -1.0f, 1.0f, -1.0f };
    glm::vec3 p011 = { -1.0f, 1.0f, 1.0f };
    glm::vec3 p100 = { 1.0f, -1.0f, -1.0f };
    glm::vec3 p101 = { 1.0f, -1.0f, 1.0f };
    glm::vec3 p110 = { 1.0f, 1.0f, -1.0f };
    glm::vec3 p111 = { 1.0f, 1.0f, 1.0f };

    quad(p000, p001, p101, p100, bottomColor);
    quad(p010, p110, p111, p011, topColor);
    quad(p011, p111, p101, p001, frontColor);
    quad(p010, p000, p100, p110, backColor);
    quad(p010, p011, p001, p000, leftColor);
    quad(p110, p100, p101, p111, rightColor);

    return v;
}

std::vector<Vertex> Application::loadObjTriangles(const std::string& objPath) const {
    struct P3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    const std::filesystem::path inputPath(objPath);
    const std::filesystem::path fullPath = std::filesystem::absolute(inputPath);
    std::cout << "loadObjTriangles() file: " << fullPath.string() << '\n';

    std::ifstream in(fullPath);
    if (!in.is_open()) {
        std::cout << "Failed to open mesh: " << fullPath.string() << '\n';
        return {};
    }

    std::vector<P3> positions;
    std::vector<Vertex> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            P3 p;
            iss >> c >> p.x >> p.y >> p.z;
            positions.push_back(p);
            continue;
        }
        if (line.rfind("f ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            iss >> c;
            std::vector<int> ids;
            std::string token;
            while (iss >> token) {
                const size_t slash = token.find('/');
                const std::string vStr = (slash == std::string::npos) ? token : token.substr(0, slash);
                if (vStr.empty()) {
                    continue;
                }
                int idx = std::stoi(vStr);
                if (idx > 0) {
                    ids.push_back(idx - 1);
                }
                else if (idx < 0) {
                    ids.push_back(static_cast<int>(positions.size()) + idx);
                }
            }

            if (ids.size() < 3) {
                continue;
            }

            const glm::vec3 n(0.0f, 0.0f, 1.0f);
            const glm::vec3 color(0.2f, 0.8f, 1.0f);
            for (size_t i = 1; i + 1 < ids.size(); ++i) {
                const int i0 = ids[0];
                const int i1 = ids[i];
                const int i2 = ids[i + 1];
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    i0 >= static_cast<int>(positions.size()) ||
                    i1 >= static_cast<int>(positions.size()) ||
                    i2 >= static_cast<int>(positions.size())) {
                    continue;
                }
                out.push_back(Vertex{ glm::vec3(positions[i0].x, positions[i0].y, positions[i0].z), n, color });
                out.push_back(Vertex{ glm::vec3(positions[i1].x, positions[i1].y, positions[i1].z), n, color });
                out.push_back(Vertex{ glm::vec3(positions[i2].x, positions[i2].y, positions[i2].z), n, color });
            }
        }
    }
    return out;
}

void Application::showFlattenedMesh(const std::string& objPath) {
    std::vector<Vertex> mesh = loadObjTriangles(objPath);
    if (mesh.empty()) {
        std::cout << "No triangles loaded from " << objPath << '\n';
        return;
    }

    glm::vec3 minP(FLT_MAX, FLT_MAX, FLT_MAX);
    glm::vec3 maxP(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const Vertex& v : mesh) {
        minP = glm::min(minP, v.pos);
        maxP = glm::max(maxP, v.pos);
    }
    const glm::vec3 center = (minP + maxP) * 0.5f;

    for (auto& v : mesh) {
        v.pos -= center;
    }

    setApplicationVertices(mesh);
    rebuildVertexBuffer();
    std::cout << "Rendered flattened mesh: " << objPath << " (" << mesh.size() / 3 << " triangles)\n";
}
