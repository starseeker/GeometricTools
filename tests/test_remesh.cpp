// Test program for GTE MeshRemesh functionality
// Demonstrates mesh remeshing using GTE style headers

#include <GTE/Mathematics/MeshRemesh.h>
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Simple OBJ file loader
bool LoadOBJ(
    std::string const& filename,
    std::vector<gte::Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }

    vertices.clear();
    triangles.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v")
        {
            gte::Vector3<double> v;
            iss >> v[0] >> v[1] >> v[2];
            vertices.push_back(v);
        }
        else if (type == "f")
        {
            std::array<int32_t, 3> face;
            for (int i = 0; i < 3; ++i)
            {
                std::string vertex;
                iss >> vertex;
                
                // Handle face formats: v, v/vt, v/vt/vn, v//vn
                size_t slash = vertex.find('/');
                if (slash != std::string::npos)
                {
                    vertex = vertex.substr(0, slash);
                }
                
                face[i] = std::stoi(vertex) - 1; // OBJ indices are 1-based
            }
            triangles.push_back(face);
        }
    }

    return !vertices.empty() && !triangles.empty();
}

// Simple OBJ file saver
bool SaveOBJ(
    std::string const& filename,
    std::vector<gte::Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not write to file " << filename << std::endl;
        return false;
    }

    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }

    // Write faces
    for (auto const& f : triangles)
    {
        file << "f " << (f[0] + 1) << " " << (f[1] + 1) << " " << (f[2] + 1) << "\n";
    }

    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <input.obj> <output.obj> [target_vertices]" << std::endl;
        std::cout << "  target_vertices: Optional target number of vertices (default: same as input)" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    size_t targetVertices = 0;

    if (argc >= 4)
    {
        targetVertices = std::stoull(argv[3]);
    }

    std::cout << "=== GTE Mesh Remesh Test ===" << std::endl;

    // Load mesh
    std::cout << "Loading mesh from " << inputFile << "..." << std::endl;
    std::vector<gte::Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    if (!LoadOBJ(inputFile, vertices, triangles))
    {
        std::cerr << "Failed to load mesh!" << std::endl;
        return 1;
    }

    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;

    // Initial validation
    auto initialValidation = gte::MeshValidation<double>::Validate(vertices, triangles);
    std::cout << "  Initial manifold: " << (initialValidation.isManifold ? "Yes" : "No") << std::endl;
    std::cout << "  Initial closed: " << (initialValidation.isClosed ? "Yes" : "No") << std::endl;

    // Pre-repair if not manifold
    if (!initialValidation.isManifold)
    {
        std::cout << "\nStep 1: Mesh Repair (input is non-manifold)" << std::endl;
        
        gte::MeshRepair<double>::Parameters repairParams;
        repairParams.epsilon = 1e-6;
        repairParams.mode = gte::MeshRepair<double>::RepairMode::DEFAULT;
        
        size_t vBefore = vertices.size();
        size_t tBefore = triangles.size();
        
        gte::MeshRepair<double>::Repair(vertices, triangles, repairParams);
        
        std::cout << "  Vertices: " << vBefore << " -> " << vertices.size();
        std::cout << " (" << (vBefore - vertices.size()) << " removed)" << std::endl;
        std::cout << "  Triangles: " << tBefore << " -> " << triangles.size();
        std::cout << " (" << (tBefore - triangles.size()) << " removed)" << std::endl;
    }

    // Remeshing
    std::cout << "\nStep 2: Remeshing" << std::endl;

    gte::MeshRemesh<double>::Parameters remeshParams;
    remeshParams.lloydIterations = 10;
    remeshParams.splitIterations = 5;
    remeshParams.collapseIterations = 5;
    remeshParams.smoothIterations = 5;
    remeshParams.useRVD = true;  // Use exact RVD for Lloyd
    remeshParams.projectToSurface = true;
    remeshParams.preserveBoundary = true;

    std::cout << "  Method: Isotropic CVT" << std::endl;
    std::cout << "  Lloyd iterations: " << remeshParams.lloydIterations << std::endl;
    std::cout << "  Smoothing iterations: " << remeshParams.smoothIterations << std::endl;

    size_t vBefore = vertices.size();
    size_t tBefore = triangles.size();
    
    // Keep original mesh for projection
    auto originalVertices = vertices;
    auto originalTriangles = triangles;

    gte::MeshRemesh<double>::Remesh(vertices, triangles, originalVertices, originalTriangles, remeshParams);

    std::cout << "  Vertices: " << vBefore << " -> " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << tBefore << " -> " << triangles.size() << std::endl;

    // Final validation
    auto finalValidation = gte::MeshValidation<double>::Validate(vertices, triangles);
    std::cout << "\n=== Final Results ===" << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;
    std::cout << "  Manifold: " << (finalValidation.isManifold ? "Yes" : "No") << std::endl;
    std::cout << "  Closed: " << (finalValidation.isClosed ? "Yes" : "No") << std::endl;
    
    if (!finalValidation.isManifold)
    {
        std::cout << "  Non-manifold edges: " << finalValidation.nonManifoldEdges << std::endl;
    }
    if (!finalValidation.isClosed)
    {
        std::cout << "  Boundary edges: " << finalValidation.boundaryEdges << std::endl;
    }

    // Save result
    std::cout << "\nSaving to " << outputFile << "..." << std::endl;
    if (!SaveOBJ(outputFile, vertices, triangles))
    {
        std::cerr << "Failed to save mesh!" << std::endl;
        return 1;
    }

    std::cout << "Done!" << std::endl;
    return 0;
}
