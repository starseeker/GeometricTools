// Test to diagnose why we can't reach 100% closure

#include <GTE/Mathematics/Co3NeEnhanced.h>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>

using namespace gte;

// Create sphere point cloud using Fibonacci sphere
std::vector<Vector3<double>> CreateSpherePoints(size_t n)
{
    std::vector<Vector3<double>> points;
    points.reserve(n);
    
    double phi = (1.0 + std::sqrt(5.0)) / 2.0;  // Golden ratio
    
    for (size_t i = 0; i < n; ++i)
    {
        double y = 1.0 - (i / static_cast<double>(n - 1)) * 2.0;
        double radius = std::sqrt(1.0 - y * y);
        double theta = 2.0 * 3.14159265358979323846 * i / phi;
        
        double x = std::cos(theta) * radius;
        double z = std::sin(theta) * radius;
        
        points.push_back({x, y, z});
    }
    
    return points;
}

// Count boundary edges
int CountBoundaryEdges(std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::map<std::pair<int32_t, int32_t>, int> edgeCount;
    
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int32_t v0 = tri[i];
            int32_t v1 = tri[(i + 1) % 3];
            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
            edgeCount[edge]++;
        }
    }
    
    int boundaryCount = 0;
    for (auto const& [edge, count] : edgeCount)
    {
        if (count == 1)
            boundaryCount++;
    }
    
    return boundaryCount;
}

int main()
{
    std::cout << "=== Manifold Closure Diagnostic Test ===" << std::endl << std::endl;
    
    // Create test point cloud
    size_t numPoints = 50;
    auto points = CreateSpherePoints(numPoints);
    
    std::cout << "Testing different parameter combinations:" << std::endl << std::endl;
    
    // Test 1: Current defaults
    {
        std::cout << "Test 1: Default parameters (5 iter, 1.5 mult, 0.1 quality)" << std::endl;
        
        Co3NeEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;
        params.maxRefinementIterations = 5;
        params.refinementRadiusMultiplier = 1.5;
        params.minTriangleQuality = 0.1;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        
        int boundaryEdges = CountBoundaryEdges(triangles);
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Boundary edges: " << boundaryEdges << std::endl;
        std::cout << std::endl;
    }
    
    // Test 2: More iterations
    {
        std::cout << "Test 2: More iterations (20 iter)" << std::endl;
        
        Co3NeEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;
        params.maxRefinementIterations = 20;
        params.refinementRadiusMultiplier = 1.5;
        params.minTriangleQuality = 0.1;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        
        int boundaryEdges = CountBoundaryEdges(triangles);
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Boundary edges: " << boundaryEdges << std::endl;
        std::cout << std::endl;
    }
    
    // Test 3: Lower quality threshold
    {
        std::cout << "Test 3: Lower quality threshold (0.01)" << std::endl;
        
        Co3NeEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;
        params.maxRefinementIterations = 5;
        params.refinementRadiusMultiplier = 1.5;
        params.minTriangleQuality = 0.01;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        
        int boundaryEdges = CountBoundaryEdges(triangles);
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Boundary edges: " << boundaryEdges << std::endl;
        std::cout << std::endl;
    }
    
    // Test 4: Larger radius multiplier
    {
        std::cout << "Test 4: Larger radius multiplier (2.5)" << std::endl;
        
        Co3NeEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;
        params.maxRefinementIterations = 5;
        params.refinementRadiusMultiplier = 2.5;
        params.minTriangleQuality = 0.1;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        
        int boundaryEdges = CountBoundaryEdges(triangles);
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Boundary edges: " << boundaryEdges << std::endl;
        std::cout << std::endl;
    }
    
    // Test 5: Combination - more iterations + lower quality
    {
        std::cout << "Test 5: Combined (20 iter, 2.0 mult, 0.01 quality)" << std::endl;
        
        Co3NeEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;
        params.maxRefinementIterations = 20;
        params.refinementRadiusMultiplier = 2.0;
        params.minTriangleQuality = 0.01;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        
        int boundaryEdges = CountBoundaryEdges(triangles);
        std::cout << "  Triangles: " << triangles.size() << std::endl;
        std::cout << "  Boundary edges: " << boundaryEdges << std::endl;
        std::cout << std::endl;
    }
    
    return 0;
}
