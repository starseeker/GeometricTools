// Integration test for BallPivotMeshHoleFiller with Co3NeManifoldStitcher
// Tests the complete workflow with Co3Ne reconstruction

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cmath>
#include <random>

using namespace gte;

// Helper function to save mesh as OBJ
void SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream out(filename);
    if (!out.is_open())
    {
        std::cerr << "Failed to open " << filename << " for writing\n";
        return;
    }
    
    for (auto const& v : vertices)
    {
        out << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    for (auto const& t : triangles)
    {
        out << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    }
    
    out.close();
    std::cout << "Saved " << filename << "\n";
}

// Create a simple point cloud (sphere with gaps)
std::vector<Vector3<double>> CreateSpherePointCloud(int numPoints)
{
    std::vector<Vector3<double>> points;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    
    for (int i = 0; i < numPoints; ++i)
    {
        // Generate points on a unit sphere using spherical coordinates
        double theta = 2.0 * 3.14159265358979323846 * dist(rng);
        double phi = std::acos(2.0 * dist(rng) - 1.0);
        
        double x = std::sin(phi) * std::cos(theta);
        double y = std::sin(phi) * std::sin(theta);
        double z = std::cos(phi);
        
        points.push_back({x, y, z});
    }
    
    return points;
}

int main()
{
    std::cout << "=== BallPivotMeshHoleFiller Integration Test ===\n\n";
    
    // Create a sphere point cloud
    std::cout << "Creating sphere point cloud...\n";
    int numPoints = 200;  // Small for quick test
    auto points = CreateSpherePointCloud(numPoints);
    std::cout << "  Generated " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction
    std::cout << "\nRunning Co3Ne reconstruction...\n";
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.relaxedManifoldExtraction = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    if (!success)
    {
        std::cerr << "Co3Ne reconstruction failed!\n";
        return 1;
    }
    
    std::cout << "  Result: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles\n";
    
    // Save Co3Ne output
    SaveOBJ("test_co3ne_output.obj", vertices, triangles);
    
    // Test 1: Stitching WITHOUT ball pivot hole filler
    std::cout << "\n=== Test 1: Traditional Stitching (without ball pivot hole filler) ===\n";
    {
        auto testVertices = vertices;
        auto testTriangles = triangles;
        
        Co3NeManifoldStitcher<double>::Parameters stitchParams;
        stitchParams.verbose = true;
        stitchParams.enableHoleFilling = true;
        stitchParams.enableBallPivotHoleFiller = false;  // Disable new hole filler
        
        Co3NeManifoldStitcher<double>::StitchPatches(testVertices, testTriangles, stitchParams);
        
        std::cout << "  Output: " << testVertices.size() << " vertices, " 
                  << testTriangles.size() << " triangles\n";
        
        SaveOBJ("test_stitched_traditional.obj", testVertices, testTriangles);
    }
    
    // Test 2: Stitching WITH ball pivot hole filler
    std::cout << "\n=== Test 2: Stitching with Ball Pivot Hole Filler ===\n";
    {
        auto testVertices = vertices;
        auto testTriangles = triangles;
        
        Co3NeManifoldStitcher<double>::Parameters stitchParams;
        stitchParams.verbose = true;
        stitchParams.enableHoleFilling = true;
        stitchParams.enableBallPivotHoleFiller = true;  // Enable new hole filler
        stitchParams.edgeMetric = BallPivotMeshHoleFiller<double>::EdgeMetric::Average;
        stitchParams.radiusScales = {1.0, 1.5, 2.0, 3.0, 5.0};
        stitchParams.maxHoleFillerIterations = 5;
        stitchParams.removeEdgeTrianglesOnFailure = true;
        
        Co3NeManifoldStitcher<double>::StitchPatches(testVertices, testTriangles, stitchParams);
        
        std::cout << "  Output: " << testVertices.size() << " vertices, " 
                  << testTriangles.size() << " triangles\n";
        
        SaveOBJ("test_stitched_with_ball_pivot.obj", testVertices, testTriangles);
    }
    
    // Test 3: Direct hole filler on existing mesh
    std::cout << "\n=== Test 3: Direct Ball Pivot Hole Filler Test ===\n";
    {
        auto testVertices = vertices;
        auto testTriangles = triangles;
        
        BallPivotMeshHoleFiller<double>::Parameters holeParams;
        holeParams.verbose = true;
        holeParams.edgeMetric = BallPivotMeshHoleFiller<double>::EdgeMetric::Average;
        holeParams.maxIterations = 5;
        
        size_t trianglesBefore = testTriangles.size();
        bool holeFilled = BallPivotMeshHoleFiller<double>::FillAllHoles(
            testVertices, testTriangles, holeParams);
        size_t trianglesAfter = testTriangles.size();
        
        std::cout << "  Result: " << (holeFilled ? "SUCCESS" : "NO CHANGES") << "\n";
        std::cout << "  Triangles added: " << (trianglesAfter - trianglesBefore) << "\n";
        std::cout << "  Output: " << testVertices.size() << " vertices, " 
                  << testTriangles.size() << " triangles\n";
        
        SaveOBJ("test_direct_hole_filler.obj", testVertices, testTriangles);
    }
    
    std::cout << "\n=== Integration Test Complete ===\n";
    std::cout << "Generated OBJ files:\n";
    std::cout << "  - test_co3ne_output.obj (Co3Ne reconstruction)\n";
    std::cout << "  - test_stitched_traditional.obj (traditional stitching)\n";
    std::cout << "  - test_stitched_with_ball_pivot.obj (with ball pivot hole filler)\n";
    std::cout << "  - test_direct_hole_filler.obj (direct hole filler)\n";
    
    return 0;
}
