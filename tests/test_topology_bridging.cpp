// Test topology-aware component bridging

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace gte;

// Load XYZ point cloud
bool LoadXYZ(std::string const& filename, std::vector<Vector3<double>>& points)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open " << filename << "\n";
        return false;
    }
    
    points.clear();
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        double x, y, z;
        if (iss >> x >> y >> z)
        {
            points.push_back(Vector3<double>{x, y, z});
        }
    }
    
    return !points.empty();
}

// Save mesh as OBJ
void SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to create " << filename << "\n";
        return;
    }
    
    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    // Write faces (OBJ uses 1-based indexing)
    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }
    
    std::cout << "Saved " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles to " << filename << "\n";
}

int main()
{
    std::cout << "=== Topology-Aware Component Bridging Test ===\n\n";
    
    // Test with r768_1000.xyz (smaller dataset)
    std::vector<Vector3<double>> points;
    if (!LoadXYZ("r768_1000.xyz", points))
    {
        std::cerr << "Failed to load r768_1000.xyz\n";
        return 1;
    }
    
    std::cout << "Loaded " << points.size() << " points from r768_1000.xyz\n\n";
    
    // Run Co3Ne reconstruction
    std::cout << "Running Co3Ne reconstruction...\n";
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.relaxedManifoldExtraction = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    if (!success || triangles.empty())
    {
        std::cerr << "Co3Ne reconstruction failed\n";
        return 1;
    }
    
    std::cout << "Co3Ne generated " << triangles.size() << " triangles\n\n";
    
    // Save before stitching
    SaveOBJ("topology_before.obj", vertices, triangles);
    
    // Run stitching with topology-aware bridging
    std::cout << "=== Running Topology-Aware Stitching ===\n";
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.verbose = true;
    
    Co3NeManifoldStitcher<double>::StitchPatches(vertices, triangles, stitchParams);
    
    // Save after stitching
    SaveOBJ("topology_after.obj", vertices, triangles);
    
    std::cout << "\n=== Test Complete ===\n";
    std::cout << "Generated files:\n";
    std::cout << "  topology_before.obj - Co3Ne output before stitching\n";
    std::cout << "  topology_after.obj - After topology-aware bridging\n";
    
    return 0;
}
