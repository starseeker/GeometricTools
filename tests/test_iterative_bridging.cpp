// Test iterative component bridging on r768_1000.xyz

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>

using namespace gte;

// Load XYZ file
bool LoadXYZ(std::string const& filename, 
             std::vector<Vector3<double>>& points,
             std::vector<Vector3<double>>& normals)
{
    std::ifstream in(filename);
    if (!in.is_open())
    {
        std::cerr << "Failed to open " << filename << "\n";
        return false;
    }
    
    points.clear();
    normals.clear();
    
    double x, y, z, nx, ny, nz;
    while (in >> x >> y >> z >> nx >> ny >> nz)
    {
        points.push_back({x, y, z});
        normals.push_back({nx, ny, nz});
    }
    
    in.close();
    return !points.empty();
}

// Save mesh as OBJ
void SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream out(filename);
    if (!out.is_open()) return;
    
    for (auto const& v : vertices)
        out << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    
    for (auto const& t : triangles)
        out << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    
    out.close();
    std::cout << "Saved " << filename << "\n";
}

int main(int argc, char* argv[])
{
    std::string dataFile = "r768_1000.xyz";
    if (argc > 1) dataFile = argv[1];
    
    std::cout << "=== Iterative Component Bridging Test ===\n\n";
    std::cout << "Loading " << dataFile << "...\n";
    
    std::vector<Vector3<double>> points, normals;
    if (!LoadXYZ(dataFile, points, normals))
    {
        std::cerr << "Failed to load " << dataFile << "\n";
        return 1;
    }
    
    std::cout << "Loaded " << points.size() << " points\n\n";
    
    // Run Co3Ne
    std::cout << "Running Co3Ne reconstruction...\n";
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.relaxedManifoldExtraction = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    if (!Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams))
    {
        std::cerr << "Co3Ne failed\n";
        return 1;
    }
    
    std::cout << "Co3Ne output: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles\n";
    
    SaveOBJ("output_co3ne.obj", vertices, triangles);
    
    // Apply iterative component bridging
    std::cout << "\n";
    BallPivotMeshHoleFiller<double>::Parameters params;
    params.verbose = true;
    params.maxIterations = 20;  // Allow more iterations
    params.allowNonManifoldEdges = false;
    
    BallPivotMeshHoleFiller<double>::FillAllHolesWithComponentBridging(
        vertices, triangles, params);
    
    SaveOBJ("output_bridged.obj", vertices, triangles);
    
    return 0;
}
