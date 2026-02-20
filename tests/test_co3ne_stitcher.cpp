// Test suite for Co3NeManifoldStitcher
//
// Tests the patch stitching functionality for Co3Ne output

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace gte;

// Helper to load XYZ point cloud
std::vector<Vector3<double>> LoadXYZ(std::string const& filename)
{
    std::vector<Vector3<double>> points;
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << "\n";
        return points;
    }
    
    double x, y, z;
    while (file >> x >> y >> z)
    {
        points.push_back({x, y, z});
    }
    
    return points;
}

// Helper to save mesh as OBJ
void SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open output file: " << filename << "\n";
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
    
    std::cout << "Saved mesh to " << filename << "\n";
}

// Test basic functionality
void TestBasicStitching()
{
    std::cout << "\n=== Test: Basic Stitching ===\n";
    
    // Create a simple point cloud (cube vertices)
    std::vector<Vector3<double>> points = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
    };
    
    // Run Co3Ne reconstruction
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 5;
    co3neParams.relaxedManifoldExtraction = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping stitcher test (no triangles generated)\n";
        return;
    }
    
    // Apply stitching
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "Stitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Final mesh: " << triangles.size() << " triangles\n";
}

// Test with r768.xyz data
void TestRealDataStitching(size_t maxPoints)
{
    std::cout << "\n=== Test: Real Data Stitching (r768.xyz) ===\n";
    
    // Load point cloud
    std::vector<Vector3<double>> points = LoadXYZ("r768.xyz");
    
    if (points.empty())
    {
        std::cout << "Failed to load r768.xyz - skipping test\n";
        return;
    }
    
    // Limit points if requested
    if (maxPoints > 0 && points.size() > maxPoints)
    {
        points.resize(maxPoints);
    }
    
    std::cout << "Loaded " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction with relaxed mode
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.relaxedManifoldExtraction = true;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::cout << "Running Co3Ne reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles from " 
              << vertices.size() << " vertices\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping stitcher test (no triangles generated)\n";
        return;
    }
    
    // Save pre-stitching mesh
    SaveOBJ("co3ne_before_stitch.obj", vertices, triangles);
    
    // Apply stitching
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.maxHoleEdges = 50;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.holeFillingMethod = MeshHoleFilling<double>::TriangulationMethod::CDT;
    
    std::cout << "\nApplying manifold stitching...\n";
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "\nStitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Final mesh: " << triangles.size() << " triangles, " 
              << vertices.size() << " vertices\n";
    
    // Save post-stitching mesh
    SaveOBJ("co3ne_after_stitch.obj", vertices, triangles);
}

// Test non-manifold edge removal
void TestNonManifoldRemoval()
{
    std::cout << "\n=== Test: Non-Manifold Edge Removal ===\n";
    
    // Create a mesh with a known non-manifold edge
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0}, {1, 0, 0}, {0.5, 1, 0},  // Triangle 1
        {0, 0, 0}, {1, 0, 0}, {0.5, -1, 0}, // Triangle 2
        {0, 0, 0}, {1, 0, 0}, {0.5, 0, 1}   // Triangle 3 (creates non-manifold edge 0-1)
    };
    
    std::vector<std::array<int32_t, 3>> triangles = {
        {0, 1, 2},
        {0, 1, 3},
        {0, 1, 4}
    };
    
    std::cout << "Input: 3 triangles sharing edge 0-1 (non-manifold)\n";
    
    // Apply stitching with non-manifold removal
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.enableHoleFilling = false;
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "Result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Remaining triangles: " << triangles.size() << "\n";
    
    if (triangles.size() == 2)
    {
        std::cout << "✓ Correctly removed 1 triangle to fix non-manifold edge\n";
    }
    else
    {
        std::cout << "✗ Expected 2 triangles, got " << triangles.size() << "\n";
    }
}

// Test ball pivot welding
void TestBallPivotWelding()
{
    std::cout << "\n=== Test: Ball Pivot Welding ===\n";
    
    // Load point cloud
    std::vector<Vector3<double>> points = LoadXYZ("r768.xyz");
    
    if (points.empty())
    {
        std::cout << "Failed to load r768.xyz - skipping test\n";
        return;
    }
    
    // Use 1000 points for testing
    if (points.size() > 1000)
    {
        points.resize(1000);
    }
    
    std::cout << "Loaded " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction with relaxed mode
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.relaxedManifoldExtraction = true;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::cout << "Running Co3Ne reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping Ball Pivot test (no triangles generated)\n";
        return;
    }
    
    // Save pre-welding mesh
    SaveOBJ("co3ne_before_welding.obj", vertices, triangles);
    
    // Apply stitching with Ball Pivot enabled
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    
    std::cout << "\nApplying stitching with Ball Pivot welding...\n";
    size_t trianglesBefore = triangles.size();
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    size_t trianglesAfter = triangles.size();
    
    std::cout << "\nStitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Triangle count: " << trianglesBefore << " -> " << trianglesAfter 
              << " (added " << (trianglesAfter - trianglesBefore) << ")\n";
    
    // Save post-welding mesh
    SaveOBJ("co3ne_after_welding.obj", vertices, triangles);
    
    if (trianglesAfter > trianglesBefore)
    {
        std::cout << "✓ Ball Pivot successfully added welding triangles\n";
    }
    else
    {
        std::cout << "ℹ Ball Pivot did not add triangles (patches may already be well-connected)\n";
    }
}

int main(int argc, char* argv[])
{
    std::cout << "Co3Ne Manifold Stitcher Test Suite\n";
    std::cout << "===================================\n";
    
    // Run tests
    TestBasicStitching();
    TestNonManifoldRemoval();
    TestBallPivotWelding();  // New ball pivot welding test
    
    // Test with real data if available
    if (argc > 1)
    {
        size_t maxPoints = std::atoi(argv[1]);
        TestRealDataStitching(maxPoints);
    }
    else
    {
        TestRealDataStitching(1000);  // Default: 1000 points
    }
    
    std::cout << "\n=== All Tests Complete ===\n";
    
    return 0;
}
