// Test Phase 4 integration: CVTN with anisotropic remeshing
#include <GTE/Mathematics/MeshRemeshFull.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>

using namespace gte;

// Helper to create a simple test mesh (tetrahedron)
void CreateTetrahedronMesh(
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    vertices.clear();
    triangles.clear();
    
    // Tetrahedron vertices
    vertices.push_back(Vector3<double>{0.0, 0.0, 0.0});
    vertices.push_back(Vector3<double>{1.0, 0.0, 0.0});
    vertices.push_back(Vector3<double>{0.5, std::sqrt(3.0)/2.0, 0.0});
    vertices.push_back(Vector3<double>{0.5, std::sqrt(3.0)/6.0, std::sqrt(6.0)/3.0});
    
    // Tetrahedron faces
    triangles.push_back({0, 2, 1});
    triangles.push_back({0, 1, 3});
    triangles.push_back({1, 2, 3});
    triangles.push_back({2, 0, 3});
}

// Helper to create a cube mesh
void CreateCubeMesh(
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    vertices.clear();
    triangles.clear();
    
    // Cube vertices
    vertices.push_back(Vector3<double>{0.0, 0.0, 0.0});
    vertices.push_back(Vector3<double>{1.0, 0.0, 0.0});
    vertices.push_back(Vector3<double>{1.0, 1.0, 0.0});
    vertices.push_back(Vector3<double>{0.0, 1.0, 0.0});
    vertices.push_back(Vector3<double>{0.0, 0.0, 1.0});
    vertices.push_back(Vector3<double>{1.0, 0.0, 1.0});
    vertices.push_back(Vector3<double>{1.0, 1.0, 1.0});
    vertices.push_back(Vector3<double>{0.0, 1.0, 1.0});
    
    // Cube faces (12 triangles)
    triangles.push_back({0, 1, 2});
    triangles.push_back({0, 2, 3});
    triangles.push_back({4, 6, 5});
    triangles.push_back({4, 7, 6});
    triangles.push_back({0, 5, 1});
    triangles.push_back({0, 4, 5});
    triangles.push_back({1, 6, 2});
    triangles.push_back({1, 5, 6});
    triangles.push_back({2, 7, 3});
    triangles.push_back({2, 6, 7});
    triangles.push_back({3, 4, 0});
    triangles.push_back({3, 7, 4});
}

// Test 1: Isotropic remeshing with CVTN<3>
bool Test1_IsotropicCVTN()
{
    std::cout << "\n=== Test 1: Isotropic Remeshing with CVTN<3> ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateTetrahedronMesh(vertices, triangles);
    
    std::cout << "Initial mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    
    // Configure parameters for isotropic CVT with new CVTN
    MeshRemeshFull<double>::Parameters params;
    params.targetVertexCount = 6;  // Target 6 vertices
    params.lloydIterations = 5;
    params.useRVD = true;
    params.useCVTN = true;  // Use new CVTN<3> infrastructure
    params.useAnisotropic = false;  // Isotropic mode
    params.preserveBoundary = false;
    params.projectToSurface = true;
    
    // Run remeshing
    bool success = MeshRemeshFull<double>::Remesh(vertices, triangles, params);
    
    if (!success)
    {
        std::cout << "❌ Remeshing failed!" << std::endl;
        return false;
    }
    
    std::cout << "Final mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    std::cout << "✓ Isotropic CVTN remeshing completed" << std::endl;
    
    return true;
}

// Test 2: Anisotropic remeshing with CVTN<6>
bool Test2_AnisotropicCVTN()
{
    std::cout << "\n=== Test 2: Anisotropic Remeshing with CVTN<6> ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateTetrahedronMesh(vertices, triangles);
    
    std::cout << "Initial mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    
    // Configure parameters for anisotropic CVT
    MeshRemeshFull<double>::Parameters params;
    params.targetVertexCount = 6;
    params.lloydIterations = 5;
    params.useRVD = true;
    params.useCVTN = true;
    params.useAnisotropic = true;  // Enable anisotropic mode (6D)
    params.anisotropyScale = 0.04;  // Typical anisotropy scale
    params.curvatureAdaptive = false;  // Simple uniform anisotropy
    params.preserveBoundary = false;
    params.projectToSurface = true;
    
    // Run remeshing
    bool success = MeshRemeshFull<double>::Remesh(vertices, triangles, params);
    
    if (!success)
    {
        std::cout << "❌ Anisotropic remeshing failed!" << std::endl;
        return false;
    }
    
    std::cout << "Final mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    std::cout << "✓ Anisotropic CVTN remeshing completed" << std::endl;
    
    return true;
}

// Test 3: Curvature-adaptive anisotropic remeshing
bool Test3_CurvatureAdaptive()
{
    std::cout << "\n=== Test 3: Curvature-Adaptive Anisotropic Remeshing ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateCubeMesh(vertices, triangles);
    
    std::cout << "Initial mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    
    // Configure parameters for curvature-adaptive anisotropic CVT
    MeshRemeshFull<double>::Parameters params;
    params.targetVertexCount = 12;
    params.lloydIterations = 5;
    params.useRVD = true;
    params.useCVTN = true;
    params.useAnisotropic = true;
    params.anisotropyScale = 0.04;
    params.curvatureAdaptive = true;  // Use curvature-adaptive scaling
    params.preserveBoundary = false;
    params.projectToSurface = true;
    
    // Run remeshing
    bool success = MeshRemeshFull<double>::Remesh(vertices, triangles, params);
    
    if (!success)
    {
        std::cout << "❌ Curvature-adaptive remeshing failed!" << std::endl;
        return false;
    }
    
    std::cout << "Final mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    std::cout << "✓ Curvature-adaptive anisotropic remeshing completed" << std::endl;
    
    return true;
}

// Test 4: Backward compatibility with old RVD
bool Test4_BackwardCompatibility()
{
    std::cout << "\n=== Test 4: Backward Compatibility (Old RVD) ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateTetrahedronMesh(vertices, triangles);
    
    std::cout << "Initial mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    
    // Configure parameters to use old RVD (not CVTN)
    MeshRemeshFull<double>::Parameters params;
    params.targetVertexCount = 6;
    params.lloydIterations = 3;
    params.useRVD = true;
    params.useCVTN = false;  // Use old RVD infrastructure
    params.useAnisotropic = false;
    params.preserveBoundary = false;
    params.projectToSurface = true;
    
    // Run remeshing
    bool success = MeshRemeshFull<double>::Remesh(vertices, triangles, params);
    
    if (!success)
    {
        std::cout << "❌ Old RVD remeshing failed!" << std::endl;
        return false;
    }
    
    std::cout << "Final mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    std::cout << "✓ Old RVD still works (backward compatible)" << std::endl;
    
    return true;
}

// Test 5: CVTN with simpler remeshing (no aggressive edge operations)
bool Test5_CVTNSimpleRemesh()
{
    std::cout << "\n=== Test 5: CVTN with Simple Remeshing ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateCubeMesh(vertices, triangles);
    
    std::cout << "Initial mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    
    // Configure parameters for CVTN without aggressive edge operations
    MeshRemeshFull<double>::Parameters params;
    params.targetVertexCount = 10;  // Use vertex count instead of edge length
    params.lloydIterations = 3;     // CVTN Lloyd iterations
    params.useRVD = true;
    params.useCVTN = true;
    params.useAnisotropic = false;
    params.preserveBoundary = false;
    params.projectToSurface = true;
    
    // Run remeshing
    bool success = MeshRemeshFull<double>::Remesh(vertices, triangles, params);
    
    if (!success)
    {
        std::cout << "❌ CVTN remeshing failed!" << std::endl;
        return false;
    }
    
    std::cout << "Final mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;
    std::cout << "✓ CVTN simple remeshing works" << std::endl;
    
    return true;
}

int main()
{
    std::cout << "====================================" << std::endl;
    std::cout << "Phase 4 Integration Tests" << std::endl;
    std::cout << "Testing CVTN integration with MeshRemeshFull" << std::endl;
    std::cout << "====================================" << std::endl;
    
    int passed = 0;
    int total = 5;
    
    if (Test1_IsotropicCVTN()) ++passed;
    if (Test2_AnisotropicCVTN()) ++passed;
    if (Test3_CurvatureAdaptive()) ++passed;
    if (Test4_BackwardCompatibility()) ++passed;
    if (Test5_CVTNSimpleRemesh()) ++passed;
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "Results: " << passed << "/" << total << " tests passed" << std::endl;
    
    if (passed == total)
    {
        std::cout << "✓ All Phase 4 integration tests passed!" << std::endl;
        std::cout << "====================================" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "✗ Some tests failed" << std::endl;
        std::cout << "====================================" << std::endl;
        return 1;
    }
}
