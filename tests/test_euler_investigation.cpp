#include <GTE/Mathematics/Co3NeEnhanced.h>
#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <cmath>
#include <algorithm>

using namespace gte;

// Create Fibonacci sphere
std::vector<Vector3<double>> CreateSpherePoints(size_t n)
{
    std::vector<Vector3<double>> points;
    points.reserve(n);
    
    double goldenRatio = (1.0 + std::sqrt(5.0)) / 2.0;
    double angleIncrement = 2.0 * 3.14159265358979323846 * goldenRatio;
    
    for (size_t i = 0; i < n; ++i)
    {
        double t = static_cast<double>(i) / static_cast<double>(n);
        double inclination = std::acos(1.0 - 2.0 * t);
        double azimuth = angleIncrement * static_cast<double>(i);
        
        double x = std::sin(inclination) * std::cos(azimuth);
        double y = std::sin(inclination) * std::sin(azimuth);
        double z = std::cos(inclination);
        
        points.push_back({x, y, z});
    }
    
    return points;
}

int main()
{
    std::cout << "=== Euler Characteristic Investigation ===\n\n";
    
    auto points = CreateSpherePoints(50);
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    Co3NeEnhanced<double>::EnhancedParameters params;
    params.useEnhancedManifold = true;
    
    bool success = Co3NeEnhanced<double>::Reconstruct(
        points, vertices, triangles, params);
    
    if (!success || triangles.empty())
    {
        std::cout << "Reconstruction failed\n";
        return 1;
    }
    
    std::cout << "Reconstruction successful\n";
    std::cout << "Input points: " << points.size() << "\n";
    std::cout << "Output vertices: " << vertices.size() << "\n";
    std::cout << "Output triangles: " << triangles.size() << "\n\n";
    
    // Find actual vertices used in triangulation
    std::set<int32_t> usedVertices;
    for (auto const& tri : triangles)
    {
        usedVertices.insert(tri[0]);
        usedVertices.insert(tri[1]);
        usedVertices.insert(tri[2]);
    }
    
    std::cout << "Vertices actually used in mesh: " << usedVertices.size() << "\n";
    std::cout << "Unused vertices: " << (vertices.size() - usedVertices.size()) << "\n\n";
    
    // Count edges and classify them
    std::map<std::pair<int32_t, int32_t>, int> edgeCount;
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int v1 = tri[i];
            int v2 = tri[(i + 1) % 3];
            auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
            edgeCount[edge]++;
        }
    }
    
    int boundaryEdges = 0;
    int interiorEdges = 0;
    int nonManifoldEdges = 0;
    
    for (auto const& [edge, count] : edgeCount)
    {
        if (count == 1) boundaryEdges++;
        else if (count == 2) interiorEdges++;
        else nonManifoldEdges++;
    }
    
    std::cout << "Edge Analysis:\n";
    std::cout << "  Total edges: " << edgeCount.size() << "\n";
    std::cout << "  Boundary edges (1 triangle): " << boundaryEdges << "\n";
    std::cout << "  Interior edges (2 triangles): " << interiorEdges << "\n";
    std::cout << "  Non-manifold edges (>2 triangles): " << nonManifoldEdges << "\n\n";
    
    // Calculate Euler characteristic with ACTUAL vertices
    int V_actual = static_cast<int>(usedVertices.size());
    int E = static_cast<int>(edgeCount.size());
    int F = static_cast<int>(triangles.size());
    int euler_actual = V_actual - E + F;
    
    std::cout << "Euler Characteristic (WRONG - using all input vertices):\n";
    std::cout << "  V=" << vertices.size() << " E=" << E << " F=" << F << "\n";
    std::cout << "  V-E+F = " << (vertices.size() - E + F) << "\n\n";
    
    std::cout << "Euler Characteristic (CORRECT - using actual vertices in mesh):\n";
    std::cout << "  V=" << V_actual << " E=" << E << " F=" << F << "\n";
    std::cout << "  V-E+F = " << euler_actual << "\n";
    std::cout << "  Expected for closed sphere: 2\n";
    std::cout << "  Difference: " << (euler_actual - 2) << "\n\n";
    
    // Analyze why it's not closed
    if (boundaryEdges > 0)
    {
        std::cout << "Analysis: Mesh is NOT closed (has " << boundaryEdges << " boundary edges)\n";
        std::cout << "  This means there are holes in the reconstruction\n";
        std::cout << "  For a closed sphere, all edges should have exactly 2 triangles\n\n";
    }
    else if (euler_actual == 2)
    {
        std::cout << "Analysis: Mesh appears to be a closed sphere! ✓\n\n";
    }
    else
    {
        std::cout << "Analysis: Mesh topology is unusual\n";
        std::cout << "  Euler characteristic suggests genus = " << (1 - euler_actual/2) << "\n\n";
    }
    
    // Sample some boundary edges
    if (boundaryEdges > 0)
    {
        std::cout << "Sample boundary edges (first 10):\n";
        int count = 0;
        for (auto const& [edge, edgeCount] : edgeCount)
        {
            if (edgeCount == 1)
            {
                std::cout << "  Edge (" << edge.first << ", " << edge.second << ")\n";
                if (++count >= 10) break;
            }
        }
        std::cout << "\n";
    }
    
    return 0;
}
