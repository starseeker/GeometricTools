// Test for MeshBoolean operations
// Verifies compilation and basic functionality

#include <Mathematics/MeshBoolean.h>
#include <Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>

using namespace gte;

int main()
{
    // Create two simple cube meshes for testing
    std::vector<Vector3<double>> verticesA = {
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0},  // bottom
        {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0}   // top
    };

    std::vector<std::array<int32_t, 3>> trianglesA = {
        {0, 1, 2}, {0, 2, 3},  // bottom
        {4, 6, 5}, {4, 7, 6},  // top
        {0, 4, 5}, {0, 5, 1},  // front
        {2, 6, 7}, {2, 7, 3},  // back
        {0, 3, 7}, {0, 7, 4},  // left
        {1, 5, 6}, {1, 6, 2}   // right
    };

    // Create second cube offset by 0.5 in X direction
    std::vector<Vector3<double>> verticesB = {
        {0.5, 0.0, 0.0}, {1.5, 0.0, 0.0}, {1.5, 1.0, 0.0}, {0.5, 1.0, 0.0},
        {0.5, 0.0, 1.0}, {1.5, 0.0, 1.0}, {1.5, 1.0, 1.0}, {0.5, 1.0, 1.0}
    };

    std::vector<std::array<int32_t, 3>> trianglesB = trianglesA;  // Same topology

    // Test Boolean Union
    std::cout << "Testing Boolean Union..." << std::endl;
    auto unionResult = MeshBoolean<double>::Execute(
        verticesA, trianglesA,
        verticesB, trianglesB,
        MeshBoolean<double>::Operation::Union);

    if (unionResult.success)
    {
        std::cout << "Union succeeded: " << unionResult.vertices.size() 
                  << " vertices, " << unionResult.triangles.size() 
                  << " triangles" << std::endl;
    }
    else
    {
        std::cout << "Union failed: " << unionResult.errorMessage << std::endl;
    }

    // Test Boolean Intersection
    std::cout << "\nTesting Boolean Intersection..." << std::endl;
    auto intersectionResult = MeshBoolean<double>::Execute(
        verticesA, trianglesA,
        verticesB, trianglesB,
        MeshBoolean<double>::Operation::Intersection);

    if (intersectionResult.success)
    {
        std::cout << "Intersection succeeded: " << intersectionResult.vertices.size() 
                  << " vertices, " << intersectionResult.triangles.size() 
                  << " triangles" << std::endl;
    }
    else
    {
        std::cout << "Intersection failed: " << intersectionResult.errorMessage << std::endl;
    }

    // Test Boolean Difference
    std::cout << "\nTesting Boolean Difference..." << std::endl;
    auto differenceResult = MeshBoolean<double>::Execute(
        verticesA, trianglesA,
        verticesB, trianglesB,
        MeshBoolean<double>::Operation::Difference);

    if (differenceResult.success)
    {
        std::cout << "Difference succeeded: " << differenceResult.vertices.size() 
                  << " vertices, " << differenceResult.triangles.size() 
                  << " triangles" << std::endl;
    }
    else
    {
        std::cout << "Difference failed: " << differenceResult.errorMessage << std::endl;
    }

    std::cout << "\nAll tests completed!" << std::endl;
    return 0;
}
