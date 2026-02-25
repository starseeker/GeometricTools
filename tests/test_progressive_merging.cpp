// Test progressive component merging
//
// Creates two separate mesh components and verifies that they get merged
// based on proximity using the new distance-aware progressive merging

#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>

using namespace gte;

int main()
{
    std::cout << "=== Progressive Component Merging Test ===\n\n";
    
    // Create two separate cube-like components
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    // Component 1: Small cube at origin (vertices 0-7)
    vertices.push_back({0, 0, 0});
    vertices.push_back({1, 0, 0});
    vertices.push_back({1, 1, 0});
    vertices.push_back({0, 1, 0});
    vertices.push_back({0, 0, 1});
    vertices.push_back({1, 0, 1});
    vertices.push_back({1, 1, 1});
    vertices.push_back({0, 1, 1});
    
    // Component 1 triangles (bottom face + partial sides = open mesh)
    triangles.push_back({0, 1, 2});  // Bottom
    triangles.push_back({0, 2, 3});
    triangles.push_back({0, 4, 5});  // Front
    triangles.push_back({0, 5, 1});
    triangles.push_back({1, 5, 6});  // Right
    triangles.push_back({1, 6, 2});
    
    // Component 2: Small cube near the first one (vertices 8-15)
    // Position it close to the first cube (gap of about 0.5)
    vertices.push_back({1.5, 0, 0});   // 8
    vertices.push_back({2.5, 0, 0});   // 9
    vertices.push_back({2.5, 1, 0});   // 10
    vertices.push_back({1.5, 1, 0});   // 11
    vertices.push_back({1.5, 0, 1});   // 12
    vertices.push_back({2.5, 0, 1});   // 13
    vertices.push_back({2.5, 1, 1});   // 14
    vertices.push_back({1.5, 1, 1});   // 15
    
    // Component 2 triangles (bottom face + partial sides = open mesh)
    triangles.push_back({8, 9, 10});   // Bottom
    triangles.push_back({8, 10, 11});
    triangles.push_back({8, 12, 13});  // Front
    triangles.push_back({8, 13, 9});
    triangles.push_back({9, 13, 14});  // Right
    triangles.push_back({9, 14, 10});
    
    std::cout << "Created mesh with " << vertices.size() << " vertices and " 
              << triangles.size() << " triangles\n";
    std::cout << "This mesh has 2 separate components\n\n";
    
    std::cout << "Initial state: " << triangles.size() << " triangles\n\n";
    
    // Apply progressive component merging via StitchPatches
    std::cout << "Applying progressive component merging...\n\n";
    
    Co3NeManifoldStitcher<double>::Parameters params;
    params.verbose = true;
    params.enableIterativeBridging = true;
    params.enableHoleFilling = false;  // Disable to test bridging only
    params.enableBallPivotHoleFiller = false;
    params.initialBridgeThreshold = 1.0;  // Start with reasonable threshold
    params.maxBridgeThreshold = 5.0;
    params.maxIterations = 10;
    
    // Call StitchPatches which will call TopologyAwareComponentBridging
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(vertices, triangles, params);
    
    std::cout << "\n=== Final State ===\n";
    std::cout << "  Triangles: " << triangles.size() << "\n";
    std::cout << "  Manifold: " << (isManifold ? "YES" : "NO") << "\n\n";
    
    // The output from StitchPatches will show component count
    // Check if we got more triangles (bridges were created)
    if (triangles.size() > 12)
    {
        std::cout << "✓ SUCCESS: Bridges were created (triangles increased from 12 to " 
                  << triangles.size() << ")\n";
        return 0;
    }
    else
    {
        std::cout << "✗ FAILED: No bridges created (still " << triangles.size() << " triangles)\n";
        return 1;
    }
}
