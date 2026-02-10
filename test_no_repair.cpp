#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <GTE/Mathematics/MeshRepair.h>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace gte;

bool LoadOBJ(std::string const& filename,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix == "v")
        {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{x, y, z});
        }
        else if (prefix == "f")
        {
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            triangles.push_back({v0-1, v1-1, v2-1});
        }
    }
    return true;
}

int main()
{
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    LoadOBJ("test_tiny.obj", vertices, triangles);
    
    std::cout << "Input: " << vertices.size() << "v, " << triangles.size() << "t\n";
    auto v1 = MeshValidation<double>::Validate(vertices, triangles, false);
    std::cout << "  Manifold: " << v1.isManifold << ", nonManifold edges: " << v1.nonManifoldEdges << "\n\n";
    
    // Test 1: Repair FIRST
    std::cout << "Test 1: Repair then fill holes\n";
    auto vert1 = vertices;
    auto tri1 = triangles;
    
    MeshRepair<double>::Parameters repairParams;
    MeshRepair<double>::Repair(vert1, tri1, repairParams);
    std::cout << "  After repair: " << vert1.size() << "v, " << tri1.size() << "t\n";
    auto v2 = MeshValidation<double>::Validate(vert1, tri1, false);
    std::cout << "    Manifold: " << v2.isManifold << ", nonManifold: " << v2.nonManifoldEdges << "\n";
    
    MeshHoleFilling<double>::Parameters fillParams;
    fillParams.repair = false;  // Don't repair after
    fillParams.validateOutput = false;  // Don't validate yet
    MeshHoleFilling<double>::FillHoles(vert1, tri1, fillParams);
    
    std::cout << "  After fill (no repair): " << vert1.size() << "v, " << tri1.size() << "t\n";
    auto v3 = MeshValidation<double>::Validate(vert1, tri1, false);
    std::cout << "    Manifold: " << v3.isManifold << ", nonManifold: " << v3.nonManifoldEdges << "\n";
    
    // Now repair
    MeshRepair<double>::Repair(vert1, tri1, repairParams);
    std::cout << "  After post-repair: " << vert1.size() << "v, " << tri1.size() << "t\n";
    auto v4 = MeshValidation<double>::Validate(vert1, tri1, false);
    std::cout << "    Manifold: " << v4.isManifold << ", nonManifold: " << v4.nonManifoldEdges << "\n";
    
    return 0;
}
