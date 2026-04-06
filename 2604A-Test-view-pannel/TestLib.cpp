
#include "Test.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>


void lscm_LIB() {
    const std::string inputPath = "qian.OBJ";
    const std::string outputPath = "qian_NL.obj";
    std::cout << "lscm_LIB() start\n";
    std::cout << "input data folder: " << inputPath << '\n';
    std::cout << "output: " << outputPath << '\n';
    const bool ok = lscm(inputPath, outputPath);
    std::cout << (ok ? "lscm_LIB() done\n" : "lscm_LIB() failed\n");
}
