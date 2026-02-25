// Test program for GTE-style mesh repair functionality
// This program loads an OBJ file and applies mesh repair operations

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshPreprocessing.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <cmath>

using namespace gte;

// Simple OBJ file loader
bool LoadOBJ(
    std::string const& filename,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            // Vertex
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{x, y, z});
        }
        else if (prefix == "f")
        {
            // Face - handle OBJ 1-based indexing
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            
            // Convert from 1-based to 0-based indexing
            triangles.push_back({v0 - 1, v1 - 1, v2 - 1});
        }
    }

    file.close();
    return true;
}

// Simple OBJ file writer
bool SaveOBJ(
    std::string const& filename,
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not create file " << filename << std::endl;
        return false;
    }

    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }

    // Write faces (convert to 1-based indexing)
    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }

    file.close();
    return true;
}

// Compute bounding box diagonal
double ComputeBBoxDiagonal(std::vector<Vector3<double>> const& vertices)
{
    if (vertices.empty())
    {
        return 0.0;
    }

    Vector3<double> min = vertices[0];
    Vector3<double> max = vertices[0];

    for (auto const& v : vertices)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (v[i] < min[i]) min[i] = v[i];
            if (v[i] > max[i]) max[i] = v[i];
        }
    }

    return Length(max - min);
}

// Compute total mesh area
double ComputeTotalArea(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    double totalArea = 0.0;

    for (auto const& tri : triangles)
    {
        Vector3<double> const& p0 = vertices[tri[0]];
        Vector3<double> const& p1 = vertices[tri[1]];
        Vector3<double> const& p2 = vertices[tri[2]];

        Vector3<double> edge1 = p1 - p0;
        Vector3<double> edge2 = p2 - p0;
        Vector3<double> normal = Cross(edge1, edge2);

        totalArea += Length(normal) * 0.5;
    }

    return totalArea;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input.obj> [output.obj] [ec|cdt|ec3d]" << std::endl;
        std::cerr << "  ec   - Use Ear Clipping triangulation (2D projection, default)" << std::endl;
        std::cerr << "  cdt  - Use Constrained Delaunay Triangulation (2D projection)" << std::endl;
        std::cerr << "  ec3d - Use 3D Ear Clipping (no projection, handles non-planar holes)" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = (argc >= 3) ? argv[2] : "repaired.obj";
    
    // Parse triangulation method
    gte::MeshHoleFilling<double>::TriangulationMethod method = 
        gte::MeshHoleFilling<double>::TriangulationMethod::EarClipping;
    std::string methodName = "Ear Clipping (2D)";
    
    if (argc >= 4)
    {
        std::string methodArg = argv[3];
        if (methodArg == "cdt")
        {
            method = gte::MeshHoleFilling<double>::TriangulationMethod::CDT;
            methodName = "Constrained Delaunay Triangulation (2D)";
        }
        else if (methodArg == "ec3d")
        {
            method = gte::MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
            methodName = "Ear Clipping (3D - no projection)";
        }
    }

    std::cout << "=== GTE Mesh Repair Test ===" << std::endl;
    std::cout << "Input: " << inputFile << std::endl;
    std::cout << "Output: " << outputFile << std::endl;
    std::cout << "Triangulation: " << methodName << std::endl;
    std::cout << std::endl;

    // Load mesh
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    std::cout << "Loading mesh..." << std::endl;
    if (!LoadOBJ(inputFile, vertices, triangles))
    {
        return 1;
    }

    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;

    // Compute initial statistics
    double bboxDiag = ComputeBBoxDiagonal(vertices);
    double initialArea = ComputeTotalArea(vertices, triangles);

    std::cout << "  Bounding box diagonal: " << bboxDiag << std::endl;
    std::cout << "  Total area: " << initialArea << std::endl;
    std::cout << std::endl;

    // Step 1: Mesh Repair
    std::cout << "Step 1: Mesh Repair" << std::endl;
    
    MeshRepair<double>::Parameters repairParams;
    repairParams.epsilon = 1e-6 * (0.01 * bboxDiag);  // Match Geogram's epsilon calculation
    repairParams.mode = MeshRepair<double>::RepairMode::DEFAULT;

    size_t verticesBefore = vertices.size();
    size_t trianglesBefore = triangles.size();

    MeshRepair<double>::Repair(vertices, triangles, repairParams);

    std::cout << "  Vertices: " << verticesBefore << " -> " << vertices.size() 
              << " (" << (verticesBefore - vertices.size()) << " removed)" << std::endl;
    std::cout << "  Triangles: " << trianglesBefore << " -> " << triangles.size()
              << " (" << (trianglesBefore - triangles.size()) << " removed)" << std::endl;
    std::cout << std::endl;

    // Step 2: Fill Holes
    std::cout << "Step 2: Fill Holes (" << methodName << ")" << std::endl;

    MeshHoleFilling<double>::Parameters fillParams;
    fillParams.maxArea = 1e30;  // Fill all holes (match Geogram default)
    fillParams.maxEdges = std::numeric_limits<size_t>::max();
    fillParams.repair = true;
    fillParams.method = method;  // Use selected triangulation method

    trianglesBefore = triangles.size();

    MeshHoleFilling<double>::FillHoles(vertices, triangles, fillParams);

    std::cout << "  Triangles added: " << (triangles.size() - trianglesBefore) << std::endl;
    std::cout << std::endl;

    // Step 3: Remove small components
    std::cout << "Step 3: Remove Small Components" << std::endl;

    double newArea = ComputeTotalArea(vertices, triangles);
    double minCompArea = 0.03 * newArea;  // Match Geogram's heuristic

    trianglesBefore = triangles.size();

    MeshPreprocessing<double>::RemoveSmallComponents(vertices, triangles, minCompArea, 0);

    std::cout << "  Triangles removed: " << (trianglesBefore - triangles.size()) << std::endl;
    std::cout << std::endl;

    // Final statistics
    double finalArea = ComputeTotalArea(vertices, triangles);
    double finalBBoxDiag = ComputeBBoxDiagonal(vertices);

    std::cout << "=== Final Results ===" << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;
    std::cout << "  Total area: " << finalArea << " (change: " 
              << ((finalArea - initialArea) / initialArea * 100.0) << "%)" << std::endl;
    std::cout << "  BBox diagonal: " << finalBBoxDiag << " (change: "
              << ((finalBBoxDiag - bboxDiag) / bboxDiag * 100.0) << "%)" << std::endl;
    std::cout << std::endl;

    // Save repaired mesh
    std::cout << "Saving repaired mesh to " << outputFile << "..." << std::endl;
    if (!SaveOBJ(outputFile, vertices, triangles))
    {
        return 1;
    }

    std::cout << "Done!" << std::endl;

    return 0;
}
