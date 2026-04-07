
#include "Test.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Face {
        std::vector<int> v;
        std::vector<int> vt;
        std::vector<int> vn;
    };

    struct EdgeConstraint {
        int a = 0; // 0-based vertex index
        int b = 0; // 0-based vertex index
        double targetLength = 0.0;
    };

    static int resolveObjIndex(int idx, int count) {
        if (idx > 0) {
            return idx - 1; // OBJ positive index is 1-based
        }
        if (idx < 0) {
            return count + idx; // OBJ negative index is relative to end
        }
        return -1;
    }

    static double distance3D(const Vec3& p0, const Vec3& p1) {
        const double dx = static_cast<double>(p0.x) - static_cast<double>(p1.x);
        const double dy = static_cast<double>(p0.y) - static_cast<double>(p1.y);
        const double dz = static_cast<double>(p0.z) - static_cast<double>(p1.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static void parseFaceToken(const std::string& token, int& v, int& vt, int& vn) {
        v = vt = vn = 0;
        const size_t p1 = token.find('/');
        if (p1 == std::string::npos) {
            v = std::stoi(token);
            return;
        }
        const size_t p2 = token.find('/', p1 + 1);
        v = std::stoi(token.substr(0, p1));
        if (p2 == std::string::npos) {
            const std::string t = token.substr(p1 + 1);
            if (!t.empty()) {
                vt = std::stoi(t);
            }
            return;
        }
        const std::string t = token.substr(p1 + 1, p2 - p1 - 1);
        const std::string n = token.substr(p2 + 1);
        if (!t.empty()) {
            vt = std::stoi(t);
        }
        if (!n.empty()) {
            vn = std::stoi(n);
        }
    }
} // namespace

bool lscm(const std::string& inputObjPath, const std::string& outputObjPath) {
    std::ifstream in(inputObjPath);
    if (!in.is_open()) {
        std::cout << "LSCM: failed to open input " << inputObjPath << '\n';
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<std::string> otherLines;
    std::vector<Face> faces;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            Vec3 p;
            iss >> c >> p.x >> p.y >> p.z;
            positions.push_back(p);
            continue;
        }
        if (line.rfind("f ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            iss >> c;
            Face face;
            std::string token;
            while (iss >> token) {
                int v = 0, vt = 0, vn = 0;
                parseFaceToken(token, v, vt, vn);
                face.v.push_back(v);
                face.vt.push_back(vt);
                face.vn.push_back(vn);
            }
            if (!face.v.empty()) {
                faces.push_back(std::move(face));
            }
            continue;
        }
        otherLines.push_back(line);
    }

    if (positions.empty()) {
        std::cout << "LSCM: no vertices found in " << inputObjPath << '\n';
        return false;
    }

    // Build unique edge constraints from mesh topology to preserve triangle shape.
    std::vector<EdgeConstraint> constraints;
    constraints.reserve(faces.size() * 3);
    std::unordered_map<std::uint64_t, size_t> edgeMap;
    edgeMap.reserve(faces.size() * 3);
    const int vertexCount = static_cast<int>(positions.size());
    auto addConstraint = [&](int ia, int ib) {
        if (ia < 0 || ib < 0 || ia >= vertexCount || ib >= vertexCount || ia == ib) {
            return;
        }
        int a = ia;
        int b = ib;
        if (a > b) {
            std::swap(a, b);
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
            static_cast<std::uint32_t>(b);
        if (edgeMap.find(key) != edgeMap.end()) {
            return;
        }
        EdgeConstraint c;
        c.a = a;
        c.b = b;
        c.targetLength = distance3D(positions[a], positions[b]);
        edgeMap.emplace(key, constraints.size());
        constraints.push_back(c);
    };

    for (const Face& f : faces) {
        if (f.v.size() < 3) {
            continue;
        }
        std::vector<int> ids;
        ids.reserve(f.v.size());
        for (int objIdx : f.v) {
            ids.push_back(resolveObjIndex(objIdx, vertexCount));
        }
        // Polygon boundary edges.
        for (size_t i = 0; i < ids.size(); ++i) {
            addConstraint(ids[i], ids[(i + 1) % ids.size()]);
        }
        // Triangle fan diagonals to better preserve non-triangle polygons.
        for (size_t i = 1; i + 1 < ids.size(); ++i) {
            addConstraint(ids[0], ids[i + 1]);
        }
    }
    if (constraints.empty()) {
        std::cout << "LSCM: no valid edges found in " << inputObjPath << '\n';
        return false;
    }

    // Initial UV from best planar projection (drop smallest bbox axis).
    float minX = std::numeric_limits<float>::max(), minY = minX, minZ = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxY = maxX, maxZ = maxX;
    for (const Vec3& p : positions) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    const float rx = maxX - minX;
    const float ry = maxY - minY;
    const float rz = maxZ - minZ;

    std::vector<double> u(vertexCount, 0.0);
    std::vector<double> v(vertexCount, 0.0);
    for (int i = 0; i < vertexCount; ++i) {
        if (rz <= rx && rz <= ry) { // project XY
            u[i] = static_cast<double>(positions[i].x);
            v[i] = static_cast<double>(positions[i].y);
        }
        else if (ry <= rx && ry <= rz) { // project XZ
            u[i] = static_cast<double>(positions[i].x);
            v[i] = static_cast<double>(positions[i].z);
        }
        else { // project YZ
            u[i] = static_cast<double>(positions[i].y);
            v[i] = static_cast<double>(positions[i].z);
        }
    }

    auto normalizeUV = [&]() {
        double minU = std::numeric_limits<double>::max();
        double minV = std::numeric_limits<double>::max();
        double maxU = std::numeric_limits<double>::lowest();
        double maxV = std::numeric_limits<double>::lowest();
        for (int i = 0; i < vertexCount; ++i) {
            minU = std::min(minU, u[i]); maxU = std::max(maxU, u[i]);
            minV = std::min(minV, v[i]); maxV = std::max(maxV, v[i]);
        }
        const double du = (maxU - minU > 1e-12) ? (maxU - minU) : 1.0;
        const double dv = (maxV - minV > 1e-12) ? (maxV - minV) : 1.0;
        for (int i = 0; i < vertexCount; ++i) {
            u[i] = (u[i] - minU) / du;
            v[i] = (v[i] - minV) / dv;
        }
    };
    normalizeUV();

    // Scale target lengths to current UV scale.
    double avg3D = 0.0;
    double avg2D = 0.0;
    for (const EdgeConstraint& c : constraints) {
        const double du = u[c.a] - u[c.b];
        const double dv = v[c.a] - v[c.b];
        avg3D += c.targetLength;
        avg2D += std::sqrt(du * du + dv * dv);
    }
    avg3D /= static_cast<double>(constraints.size());
    avg2D /= static_cast<double>(constraints.size());
    const double lengthScale = (avg3D > 1e-12) ? (avg2D / avg3D) : 1.0;
    for (EdgeConstraint& c : constraints) {
        c.targetLength *= lengthScale;
    }

    // Fix two anchor vertices to remove translation/rotation/scale ambiguity.
    int anchor0 = 0;
    int anchor1 = 0;
    for (int i = 1; i < vertexCount; ++i) {
        if (u[i] < u[anchor0]) {
            anchor0 = i;
        }
        if (u[i] > u[anchor1]) {
            anchor1 = i;
        }
    }
    if (anchor0 == anchor1 && vertexCount > 1) {
        anchor1 = 1;
    }
    const double anchor0u = u[anchor0];
    const double anchor0v = v[anchor0];
    const double anchor1u = u[anchor1];
    const double anchor1v = v[anchor1];

    // Edge-length preserving optimization (pure C++ gradient descent).
    std::vector<double> gradU(vertexCount, 0.0);
    std::vector<double> gradV(vertexCount, 0.0);
    double step = 0.08;
    for (int iter = 0; iter < 400; ++iter) {
        std::fill(gradU.begin(), gradU.end(), 0.0);
        std::fill(gradV.begin(), gradV.end(), 0.0);

        for (const EdgeConstraint& c : constraints) {
            const double du = u[c.a] - u[c.b];
            const double dv = v[c.a] - v[c.b];
            const double d = std::sqrt(du * du + dv * dv) + 1e-12;
            const double diff = d - c.targetLength;
            const double weight = 1.0 / (c.targetLength + 1e-6);
            const double g = 2.0 * weight * diff / d;
            const double gx = g * du;
            const double gy = g * dv;
            gradU[c.a] += gx; gradV[c.a] += gy;
            gradU[c.b] -= gx; gradV[c.b] -= gy;
        }

        for (int i = 0; i < vertexCount; ++i) {
            if (i == anchor0 || i == anchor1) {
                continue;
            }
            u[i] -= step * gradU[i];
            v[i] -= step * gradV[i];
        }

        // Keep anchors fixed.
        u[anchor0] = anchor0u; v[anchor0] = anchor0v;
        u[anchor1] = anchor1u; v[anchor1] = anchor1v;

        // Dampen step over time.
        step *= 0.995;
    }
    normalizeUV();

    std::ofstream out(outputObjPath, std::ios::trunc);
    if (!out.is_open()) {
        std::cout << "LSCM: failed to open output " << outputObjPath << '\n';
        return false;
    }

    out << "# Generated by pure C++ LSCM fallback\n";
    for (const std::string& l : otherLines) {
        if (l.rfind("vt ", 0) == 0 || l.rfind("vn ", 0) == 0) {
            continue;
        }
        out << l << '\n';
    }
    // Write flattened geometry on XY plane (z = 0).
    for (int i = 0; i < vertexCount; ++i) {
        out << "v " << static_cast<float>(u[i]) << ' ' << static_cast<float>(v[i]) << " 0.0\n";
    }
    for (int i = 0; i < vertexCount; ++i) {
        out << "vt " << static_cast<float>(u[i]) << ' ' << static_cast<float>(v[i]) << '\n';
    }
    for (const Face& f : faces) {
        out << "f";
        for (size_t i = 0; i < f.v.size(); ++i) {
            const int resolved = resolveObjIndex(f.v[i], vertexCount);
            if (resolved < 0) {
                continue;
            }
            const int vi = resolved + 1;
            out << ' ' << vi << '/' << vi;
        }
        out << '\n';
    }
    return true;
}

void lscm_CPU() {
    const std::string inputPath = "qian.OBJ";
    const std::string outputPath = "qian_NL.obj";
    std::cout << "lscm_CPU() start\n";
    std::cout << "input data folder: " << inputPath << '\n';
    std::cout << "output: " << outputPath << '\n';
    const bool ok = lscm(inputPath, outputPath);
    std::cout << (ok ? "lscm_CPU() done\n" : "lscm_CPU() failed\n");
}