
#pragma once

#include "Window.h"

class Application : public Window {
public:
    void run();

private:
    void renderMenu();
    void renderLeftViewPanel();
    std::vector<Vertex> buildCubeVertices() const;
    bool _showCube = false;
};