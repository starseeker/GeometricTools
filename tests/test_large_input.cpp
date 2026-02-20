// Test topology-aware bridging performance on larger input

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

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

int main()
{
    std::cout << "=== Large Input Performance Test ===\n\n";
    
    // Test with r768_5k.xyz (5000 points)
    std::vector<Vector3<double>> points;
    if (!LoadXYZ("r768_5k.xyz", points))
    {
        std::cerr << "Failed to load r768_5k.xyz\n";
        return 1;
    }
    
    std::cout << "Loaded " << points.size() << " points from r768_5k.xyz\n\n";
    
    // Run Co3Ne reconstruction
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Running Co3Ne reconstruction...\n";
    
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.relaxedManifoldExtraction = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    auto co3neEnd = std::chrono::high_resolution_clock::now();
    auto co3neDuration = std::chrono::duration_cast<std::chrono::milliseconds>(co3neEnd - start);
    
    if (!success || triangles.empty())
    {
        std::cerr << "Co3Ne reconstruction failed\n";
        return 1;
    }
    
    std::cout << "Co3Ne generated " << triangles.size() << " triangles in " 
              << co3neDuration.count() << " ms\n\n";
    
    // Run stitching with optimized topology-aware bridging
    std::cout << "=== Running Optimized Topology-Aware Stitching ===\n";
    
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.enableIterativeBridging = true;
    stitchParams.maxIterations = 15;  // More iterations for larger input
    stitchParams.initialBridgeThreshold = 2.0;
    stitchParams.maxBridgeThreshold = 15.0;  // Higher max threshold
    stitchParams.targetSingleComponent = true;
    stitchParams.verbose = true;
    
    auto stitchStart = std::chrono::high_resolution_clock::now();
    
    Co3NeManifoldStitcher<double>::StitchPatches(vertices, triangles, stitchParams);
    
    auto stitchEnd = std::chrono::high_resolution_clock::now();
    auto stitchDuration = std::chrono::duration_cast<std::chrono::milliseconds>(stitchEnd - stitchStart);
    
    std::cout << "\n=== Performance Summary ===\n";
    std::cout << "Co3Ne time: " << co3neDuration.count() << " ms\n";
    std::cout << "Stitching time: " << stitchDuration.count() << " ms\n";
    std::cout << "Total time: " << (co3neDuration + stitchDuration).count() << " ms\n";
    
    return 0;
}
