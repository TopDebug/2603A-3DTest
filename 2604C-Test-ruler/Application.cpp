#include "Application.h"
#include "Test.h"
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cctype>
#include <string>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#endif

#ifndef _WIN32
#include <cstdio>
#endif

namespace {
bool openObjFileDialog(std::string& outPath) {
#ifdef _WIN32
    char fileBuffer[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = fileBuffer;
        return true;
    }
    return false;
#else
    const char* commands[] = {
        "zenity --file-selection 2>/dev/null",
        "kdialog --getopenfilename 2>/dev/null"
    };
    char buffer[2048] = {};
    for (const char* cmd : commands) {
        FILE* pipe = popen(cmd, "r");
        if (!pipe) {
            continue;
        }
        std::string path;
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            path = buffer;
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
                path.pop_back();
            }
        }
        const int rc = pclose(pipe);
        if (rc == 0 && !path.empty()) {
            outPath = path;
            return true;
        }
    }
    return false;
#endif
}

bool pathLooksLikeObj(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".obj";
}
} // namespace

void Application::run() {
    initializeWindowSystem();
    while (!shouldClose()) {
        pollWindowEvents();
        beginImGuiFrame();
        renderMenu();
        renderLeftViewPanel();
        ImGui::Render();
        drawFrame();
    }
    shutdownWindowSystem();
}

void Application::renderMenu() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleVar(2);
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open")) {
            std::string selectedPath;
            if (openObjFileDialog(selectedPath)) {
                if (!pathLooksLikeObj(selectedPath)) {
                    std::cout << "Open: not an OBJ file: " << selectedPath << '\n';
                }
                else {
                    _showCube = false;
                    showFlattenedMesh(selectedPath);
                }
            }
        }
        if (ImGui::MenuItem("Cube")) {
            _showCube = true;
            //setApplicationModelTransform(glm::vec3(0.0f), glm::vec3(1.0f));
            setApplicationVertices(buildCubeVertices());
            rebuildVertexBuffer();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Front View")) {
            setRotation(glm::vec2(0.0f, 0.0f));
        }
        if (ImGui::MenuItem("Right View")) {
            setRotation(glm::vec2(0.0f, -90.0f));
        }
        if (ImGui::MenuItem("Top View")) {
            setRotation(glm::vec2(90.0f, 0.0f));
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Test")) {
        if (ImGui::MenuItem("lscm")) {
            lscm();
            showFlattenedMesh("models/qian_NL.obj");
        }
        ImGui::EndMenu();
    }

    // Single right-side cursor/depth readout text block.
    const std::string menuReadout = getCursorReadoutText();
    const float coordBlockWidth = ImGui::CalcTextSize(menuReadout.c_str()).x + 10.0f;
    const float rightPadding = 10.0f;
    const float targetX = ImGui::GetWindowWidth() - coordBlockWidth - rightPadding;
    if (targetX > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(targetX);
    }
    ImGui::TextUnformatted(menuReadout.c_str());

    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar(2);
}

void Application::renderLeftViewPanel() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" ")) {
        ImGui::End();
        return;
    }

    const ImVec2 currSize = ImGui::GetWindowSize();
    if (_leftPanelSizeInitialized) {
        if (std::fabs(currSize.x - _lastLeftPanelSize.x) > 0.5f ||
            std::fabs(currSize.y - _lastLeftPanelSize.y) > 0.5f) {
            _leftPanelResizePending = true;
        }
    }
    _lastLeftPanelSize = glm::vec2(currSize.x, currSize.y);
    _leftPanelSizeInitialized = true;
    if (_leftPanelResizePending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        std::cout << "Panel pos=(" << pos.x << ", " << pos.y << ")"
                  << " size=(" << size.x << ", " << size.y << ")\n";
        _leftPanelResizePending = false;
    }

    float coordinateEdit[3] = { _coordinate.x, _coordinate.y, _coordinate.z };
    const glm::vec2 rotSnap = getRotation();
    float rotationEdit[2] = { rotSnap.x, rotSnap.y };
    float orthoEdit[4] = { _ortho.x, _ortho.y, _ortho.z, _ortho.w };
    float viewportEdit[2] = { _viewport.x, _viewport.y};

    ImGui::Columns(2, "view_params_columns", false);
    ImGui::SetColumnWidth(0, 115.0f);

    auto rowFloat = [&](const char* label, const char* id, float& value, const char* format) -> bool {
        ImGui::TextUnformatted(label);
        ImGui::NextColumn();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat(id, &value, 0.0f, 0.0f, format);
        const bool committed = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::NextColumn();
        return committed;
    };

    bool changed = false;
    changed |= rowFloat("coordinateX", "##coordinateX", coordinateEdit[0], "%0.1f");
    changed |= rowFloat("coordinateY", "##coordinateY", coordinateEdit[1], "%0.1f");
    changed |= rowFloat("coordinateZ", "##coordinateZ", coordinateEdit[2], "%0.1f");
    changed |= rowFloat("roateX", "##roateX", rotationEdit[0], "%0.1f");
    changed |= rowFloat("rotateY", "##rotateY", rotationEdit[1], "%0.1f");
    changed |= rowFloat("orthoT", "##orthoT", orthoEdit[3], "%0.1f");
    changed |= rowFloat("orthoB", "##orthoB", orthoEdit[2], "%0.1f");
    changed |= rowFloat("orthoR", "##orthoR", orthoEdit[1], "%0.1f");
    changed |= rowFloat("orthoL", "##orthoL", orthoEdit[0], "%0.1f");
    changed |= rowFloat("viewportW", "##viewportW", viewportEdit[0], "%0.0f");
    changed |= rowFloat("viewportH", "##viewportH", viewportEdit[1], "%0.0f");
    
    ImGui::Columns(1);

    if (changed) {
        _coordinate = glm::vec3(coordinateEdit[0], coordinateEdit[1], coordinateEdit[2]);
        setRotation(glm::vec2(rotationEdit[0], rotationEdit[1]));
        _ortho = glm::vec4(orthoEdit[0], orthoEdit[1], orthoEdit[2], orthoEdit[3]);
        setViewportSize(glm::ivec2(viewportEdit[0], viewportEdit[1]));
    }

    ImGui::End();
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

    quad(p010, p110, p111, p011, topColor);
    quad(p000, p001, p101, p100, bottomColor);
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

//    _coordinate = center;
    setApplicationVertices(mesh);
    rebuildVertexBuffer();
    std::cout << "Rendered flattened mesh: " << objPath << " (" << mesh.size() / 3 << " triangles)\n";
}
