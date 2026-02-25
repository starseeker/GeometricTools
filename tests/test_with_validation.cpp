// Test with detailed validation reporting
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshValidation.h>
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

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input.obj> <method>\n";
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string methodArg = argv[2];
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    if (!LoadOBJ(inputFile, vertices, triangles))
    {
        std::cerr << "Failed to load " << inputFile << "\n";
        return 1;
    }
    
    std::cout << "Input mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles\n";
    
    // Validate input
    auto inputValidation = MeshValidation<double>::Validate(vertices, triangles, false);
    std::cout << "Input: isManifold=" << inputValidation.isManifold 
              << ", boundaryEdges=" << inputValidation.boundaryEdges << "\n";
    
    // Set up hole filling
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = true;  // Allow fallback
    params.validateOutput = true;
    params.requireManifold = true;
    params.requireNoSelfIntersections = false;  // Too expensive for now
    
    if (methodArg == "ec")
        params.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping;
    else if (methodArg == "cdt")
        params.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    else
        params.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    
    size_t trianglesBefore = triangles.size();
    
    std::cout << "\nTrying method: " << methodArg << "\n";
    MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
    
    std::cout << "Triangles added: " << (triangles.size() - trianglesBefore) << "\n";
    
    // Validate output
    auto outputValidation = MeshValidation<double>::Validate(vertices, triangles, false);
    std::cout << "\nOutput validation:\n";
    std::cout << "  isManifold: " << outputValidation.isManifold << "\n";
    std::cout << "  isClosed: " << outputValidation.isClosed << "\n";
    std::cout << "  boundaryEdges: " << outputValidation.boundaryEdges << "\n";
    std::cout << "  nonManifoldEdges: " << outputValidation.nonManifoldEdges << "\n";
    
    if (!outputValidation.isManifold)
    {
        std::cout << "  ERROR: Output is NOT manifold!\n";
        return 1;
    }
    
    std::cout << "  SUCCESS: Output is manifold!\n";
    return 0;
}
