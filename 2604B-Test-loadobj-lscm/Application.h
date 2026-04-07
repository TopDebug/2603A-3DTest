
#pragma once

#include "Window.h"
#include <string>

class Application : public Window {
public:
    void run();

private:
    void renderMenu();
    void renderLeftViewPanel();
    std::vector<Vertex> buildCubeVertices() const;
    std::vector<Vertex> loadObjTriangles(const std::string& path) const;
    void showFlattenedMesh(const std::string& objPath);
    bool _showCube = false;
};