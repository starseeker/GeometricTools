// Test program for BallPivotMeshHoleFiller
// Validates hole detection and filling with ball pivoting

#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cmath>

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
    
    // Write vertices
    for (auto const& v : vertices)
    {
        out << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    // Write faces (OBJ uses 1-based indexing)
    for (auto const& t : triangles)
    {
        out << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    }
    
    out.close();
    std::cout << "Saved " << filename << " with " << vertices.size() 
              << " vertices and " << triangles.size() << " triangles\n";
}

// Test 1: Simple cube with one face missing (creates a square hole)
bool TestCubeWithHole()
{
    std::cout << "\n=== Test 1: Cube with Missing Face ===\n";
    
    // Create cube vertices
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},  // Bottom face
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}   // Top face
    };
    
    // Create 5 faces of cube (missing top face to create a hole)
    std::vector<std::array<int32_t, 3>> triangles = {
        // Bottom face
        {0, 1, 2}, {0, 2, 3},
        // Front face
        {0, 1, 5}, {0, 5, 4},
        // Right face
        {1, 2, 6}, {1, 6, 5},
        // Back face
        {2, 3, 7}, {2, 7, 6},
        // Left face
        {3, 0, 4}, {3, 4, 7}
    };
    
    std::cout << "Input: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
    
    // Detect holes
    auto holes = BallPivotMeshHoleFiller<double>::DetectBoundaryLoops(vertices, triangles);
    std::cout << "Detected " << holes.size() << " hole(s)\n";
    
    if (holes.empty())
    {
        std::cout << "FAILED: Expected 1 hole but found none\n";
        return false;
    }
    
    for (size_t i = 0; i < holes.size(); ++i)
    {
        std::cout << "  Hole " << (i + 1) << ": " << holes[i].vertexIndices.size() << " vertices\n";
        std::cout << "    Average edge length: " << holes[i].avgEdgeLength << "\n";
    }
    
    // Fill holes
    BallPivotMeshHoleFiller<double>::Parameters params;
    params.verbose = true;
    params.edgeMetric = BallPivotMeshHoleFiller<double>::EdgeMetric::Average;
    
    size_t trianglesBefore = triangles.size();
    bool success = BallPivotMeshHoleFiller<double>::FillAllHoles(vertices, triangles, params);
    size_t trianglesAfter = triangles.size();
    
    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Triangles added: " << (trianglesAfter - trianglesBefore) << "\n";
    
    // Check if hole is filled
    auto remainingHoles = BallPivotMeshHoleFiller<double>::DetectBoundaryLoops(vertices, triangles);
    std::cout << "Remaining holes: " << remainingHoles.size() << "\n";
    
    // Save result
    SaveOBJ("test_cube_filled.obj", vertices, triangles);
    
    return remainingHoles.empty();
}

// Test 2: Tetrahedron with one face missing
bool TestTetrahedronWithHole()
{
    std::cout << "\n=== Test 2: Tetrahedron with Missing Face ===\n";
    
    // Create tetrahedron vertices
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0},
        {1, 0, 0},
        {0.5, std::sqrt(3.0) / 2.0, 0},
        {0.5, std::sqrt(3.0) / 6.0, std::sqrt(2.0 / 3.0)}
    };
    
    // Create 3 faces (missing bottom face)
    std::vector<std::array<int32_t, 3>> triangles = {
        {0, 1, 3},  // Front face
        {1, 2, 3},  // Right face
        {2, 0, 3}   // Left face
    };
    
    std::cout << "Input: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
    
    // Fill holes
    BallPivotMeshHoleFiller<double>::Parameters params;
    params.verbose = true;
    
    size_t trianglesBefore = triangles.size();
    bool success = BallPivotMeshHoleFiller<double>::FillAllHoles(vertices, triangles, params);
    size_t trianglesAfter = triangles.size();
    
    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Triangles added: " << (trianglesAfter - trianglesBefore) << "\n";
    
    // Check if hole is filled
    auto remainingHoles = BallPivotMeshHoleFiller<double>::DetectBoundaryLoops(vertices, triangles);
    std::cout << "Remaining holes: " << remainingHoles.size() << "\n";
    
    // Save result
    SaveOBJ("test_tetrahedron_filled.obj", vertices, triangles);
    
    return remainingHoles.empty();
}

// Test 3: Adaptive radius calculation
bool TestAdaptiveRadius()
{
    std::cout << "\n=== Test 3: Adaptive Radius Calculation ===\n";
    
    // Create a simple mesh
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}
    };
    
    std::vector<std::array<int32_t, 3>> triangles = {
        {0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}
    };
    
    // Calculate adaptive radius for vertex 0
    double avgRadius = BallPivotMeshHoleFiller<double>::CalculateAdaptiveRadius(
        0, vertices, triangles, BallPivotMeshHoleFiller<double>::EdgeMetric::Average);
    
    double minRadius = BallPivotMeshHoleFiller<double>::CalculateAdaptiveRadius(
        0, vertices, triangles, BallPivotMeshHoleFiller<double>::EdgeMetric::Minimum);
    
    double maxRadius = BallPivotMeshHoleFiller<double>::CalculateAdaptiveRadius(
        0, vertices, triangles, BallPivotMeshHoleFiller<double>::EdgeMetric::Maximum);
    
    std::cout << "Vertex 0 adaptive radii:\n";
    std::cout << "  Average: " << avgRadius << "\n";
    std::cout << "  Minimum: " << minRadius << "\n";
    std::cout << "  Maximum: " << maxRadius << "\n";
    
    // Verify reasonable values
    bool success = (avgRadius > 0) && (minRadius > 0) && (maxRadius > 0) &&
                   (minRadius <= avgRadius) && (avgRadius <= maxRadius);
    
    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    
    return success;
}

// Test 4: Multiple holes
bool TestMultipleHoles()
{
    std::cout << "\n=== Test 4: Mesh with Multiple Holes ===\n";
    
    // Create a mesh with two separate holes
    std::vector<Vector3<double>> vertices = {
        // First hole boundary (square)
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        // Second hole boundary (triangle at different location)
        {2, 0, 0}, {3, 0, 0}, {2.5, 1, 0}
    };
    
    // Create partial mesh with two holes
    std::vector<std::array<int32_t, 3>> triangles = {
        // Triangles around first hole (but not filling it)
        {0, 1, 4}, {4, 3, 0},
        // Triangles around second hole (but not filling it)
        {1, 2, 5}, {5, 4, 1}
    };
    
    std::cout << "Input: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
    
    // Detect holes
    auto holes = BallPivotMeshHoleFiller<double>::DetectBoundaryLoops(vertices, triangles);
    std::cout << "Detected " << holes.size() << " hole(s)\n";
    
    // Fill holes
    BallPivotMeshHoleFiller<double>::Parameters params;
    params.verbose = true;
    params.maxIterations = 5;
    
    size_t trianglesBefore = triangles.size();
    bool success = BallPivotMeshHoleFiller<double>::FillAllHoles(vertices, triangles, params);
    size_t trianglesAfter = triangles.size();
    
    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Triangles added: " << (trianglesAfter - trianglesBefore) << "\n";
    
    // Save result
    SaveOBJ("test_multiple_holes_filled.obj", vertices, triangles);
    
    return success;
}

int main()
{
    std::cout << "=== BallPivotMeshHoleFiller Test Suite ===\n";
    
    int passed = 0;
    int total = 0;
    
    // Run tests
    ++total;
    if (TestCubeWithHole()) ++passed;
    
    ++total;
    if (TestTetrahedronWithHole()) ++passed;
    
    ++total;
    if (TestAdaptiveRadius()) ++passed;
    
    ++total;
    if (TestMultipleHoles()) ++passed;
    
    // Summary
    std::cout << "\n=== Test Summary ===\n";
    std::cout << "Passed: " << passed << " / " << total << "\n";
    
    if (passed == total)
    {
        std::cout << "All tests PASSED!\n";
        return 0;
    }
    else
    {
        std::cout << "Some tests FAILED.\n";
        return 1;
    }
}
