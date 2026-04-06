
#include "Application.h"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#endif

#ifndef _WIN32
#include <cstdio>
#endif

namespace {
bool openSystemFileDialog(std::string& outPath) {
#ifdef _WIN32
    char fileBuffer[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "All Files\0*.*\0";
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
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open")) {
            std::string selectedPath;
            if (openSystemFileDialog(selectedPath)) {
                std::cout << "Opened file: " << selectedPath << '\n';
            }
        }
        if (ImGui::MenuItem("Cube")) {
            _showCube = true;
            setApplicationVertices(buildCubeVertices());
            rebuildVertexBuffer();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Application::renderLeftViewPanel() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" ")) {
        ImGui::End();
        return;
    }

    float coordinateEdit[3] = { _coordinate.x, _coordinate.y, _coordinate.z };
    float rotationEdit[2] = { _rotation.x, _rotation.y };
    float orthoEdit[4] = { _ortho.x, _ortho.y, _ortho.z, _ortho.w };

    ImGui::Columns(2, "view_params_columns", false);
    ImGui::SetColumnWidth(0, 115.0f);

    auto rowFloat = [&](const char* label, const char* id, float& value) -> bool {
        ImGui::TextUnformatted(label);
        ImGui::NextColumn();
        ImGui::SetNextItemWidth(120.0f);
        const bool changed = ImGui::InputFloat(id, &value);
        ImGui::NextColumn();
        return changed;
    };

    bool changed = false;
    changed |= rowFloat("coordinateX", "##coordinateX", coordinateEdit[0]);
    changed |= rowFloat("coordinateY", "##coordinateY", coordinateEdit[1]);
    changed |= rowFloat("coordinateZ", "##coordinateZ", coordinateEdit[2]);
    changed |= rowFloat("roateX", "##roateX", rotationEdit[0]);
    changed |= rowFloat("rotateY", "##rotateY", rotationEdit[1]);
    changed |= rowFloat("orthoL", "##orthoL", orthoEdit[0]);
    changed |= rowFloat("orthoR", "##orthoR", orthoEdit[1]);
    changed |= rowFloat("orthoB", "##orthoB", orthoEdit[2]);
    changed |= rowFloat("orthoT", "##orthoT", orthoEdit[3]);

    ImGui::Columns(1);

    if (changed) {
        _coordinate = glm::vec3(coordinateEdit[0], coordinateEdit[1], coordinateEdit[2]);
        _rotation = glm::vec2(rotationEdit[0], rotationEdit[1]);
        _ortho = glm::vec4(orthoEdit[0], orthoEdit[1], orthoEdit[2], orthoEdit[3]);
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
