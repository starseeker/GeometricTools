// Benchmark different triangulation methods
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <iostream>
#include <chrono>
#include <vector>

using namespace gte;

int main() {
    // Create a simple test hole (planar hexagon)
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0},
        {1, 0, 0},
        {1.5, 0.866, 0},
        {1, 1.732, 0},
        {0, 1.732, 0},
        {-0.5, 0.866, 0}
    };
    
    MeshHoleFilling<double>::HoleBoundary hole;
    hole.vertices = {0, 1, 2, 3, 4, 5};
    
    std::cout << "=== Triangulation Method Comparison ===" << std::endl;
    std::cout << "Test: Planar hexagon (6 vertices)" << std::endl;
    std::cout << std::endl;
    
    // Test each method
    const char* methods[] = {"EC (2D)", "CDT (2D)", "EC3D (3D)"};
    auto methodEnums = {
        MeshHoleFilling<double>::TriangulationMethod::EarClipping,
        MeshHoleFilling<double>::TriangulationMethod::CDT,
        MeshHoleFilling<double>::TriangulationMethod::EarClipping3D
    };
    
    int methodIdx = 0;
    for (auto method : methodEnums) {
        std::vector<std::array<int32_t, 3>> triangles;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        bool success;
        if (method == MeshHoleFilling<double>::TriangulationMethod::EarClipping3D) {
            success = MeshHoleFilling<double>::TriangulateHole3D(vertices, hole, triangles);
        } else {
            success = MeshHoleFilling<double>::TriangulateHole(vertices, hole, triangles, method);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << methods[methodIdx] << ":" << std::endl;
        std::cout << "  Success: " << (success ? "Yes" : "No") << std::endl;
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Time: " << duration.count() << " μs" << std::endl;
        std::cout << std::endl;
        
        methodIdx++;
    }
    
    return 0;
}
