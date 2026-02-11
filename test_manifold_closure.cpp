// Test automatic manifold closure
#include <GTE/Mathematics/Co3NeFullEnhanced.h>
#include <iostream>
#include <cmath>
#include <set>
#include <map>

using namespace gte;

// Create sphere points using Fibonacci sphere
template <typename Real>
std::vector<Vector3<Real>> CreateSpherePoints(size_t n, Real radius = static_cast<Real>(1))
{
    std::vector<Vector3<Real>> points;
    points.reserve(n);
    
    Real goldenRatio = static_cast<Real>(1.618033988749895);
    Real angleIncrement = static_cast<Real>(2) * static_cast<Real>(3.14159265358979323846) * goldenRatio;
    
    for (size_t i = 0; i < n; ++i)
    {
        Real t = static_cast<Real>(i) / static_cast<Real>(n);
        Real inclination = std::acos(static_cast<Real>(1) - static_cast<Real>(2) * t);
        Real azimuth = angleIncrement * static_cast<Real>(i);
        
        Real x = std::sin(inclination) * std::cos(azimuth);
        Real y = std::sin(inclination) * std::sin(azimuth);
        Real z = std::cos(inclination);
        
        points.push_back(Vector3<Real>{x * radius, y * radius, z * radius});
    }
    
    return points;
}

// Count boundary edges
template <typename Real>
std::pair<int, int> CountBoundaryEdges(std::vector<std::array<int32_t, 3>> const& triangles)
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
    
    int boundaryEdges = 0;
    int interiorEdges = 0;
    
    for (auto const& [edge, count] : edgeCount)
    {
        if (count == 1) boundaryEdges++;
        else if (count == 2) interiorEdges++;
    }
    
    return {boundaryEdges, interiorEdges};
}

int main()
{
    std::cout << "=== Manifold Closure Test ===\n\n";
    
    // Test with 50-point sphere
    auto points = CreateSpherePoints<double>(50, 1.0);
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    // Test 1: Without automatic closure (current behavior)
    std::cout << "Test 1: Standard reconstruction (no automatic closure)\n";
    {
        Co3NeFullEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = false;  // Disable automatic closure
        
        if (Co3NeFullEnhanced<double>::Reconstruct(points, vertices, triangles, params))
        {
            auto [boundary, interior] = CountBoundaryEdges(triangles);
            std::cout << "  Triangles: " << triangles.size() << "\n";
            std::cout << "  Boundary edges: " << boundary << "\n";
            std::cout << "  Interior edges: " << interior << "\n";
            std::cout << "  Status: " << (boundary == 0 ? "CLOSED" : "OPEN") << "\n";
        }
        else
        {
            std::cout << "  Reconstruction failed\n";
        }
    }
    
    std::cout << "\nTest 2: With automatic closure (new feature)\n";
    {
        Co3NeFullEnhanced<double>::EnhancedParameters params;
        params.guaranteeManifold = true;  // Enable automatic closure
        params.maxRefinementIterations = 5;
        
        vertices.clear();
        triangles.clear();
        
        if (Co3NeFullEnhanced<double>::Reconstruct(points, vertices, triangles, params))
        {
            auto [boundary, interior] = CountBoundaryEdges(triangles);
            std::cout << "  Triangles: " << triangles.size() << "\n";
            std::cout << "  Boundary edges: " << boundary << "\n";
            std::cout << "  Interior edges: " << interior << "\n";
            std::cout << "  Status: " << (boundary == 0 ? "CLOSED ✓" : "OPEN") << "\n";
            
            // Calculate Euler characteristic
            std::set<int32_t> usedVertices;
            for (auto const& tri : triangles)
            {
                usedVertices.insert(tri[0]);
                usedVertices.insert(tri[1]);
                usedVertices.insert(tri[2]);
            }
            int V = usedVertices.size();
            int E = interior + boundary;
            int F = triangles.size();
            int euler = V - E + F;
            std::cout << "  Euler characteristic: " << euler << " (should be 2 for closed sphere)\n";
        }
        else
        {
            std::cout << "  Reconstruction failed\n";
        }
    }
    
    return 0;
}
