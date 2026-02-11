// Detailed debugging for 50-point sphere reconstruction failure
// This test investigates why Test 6 fails

#include <GTE/Mathematics/Co3NeFullEnhanced.h>
#include <GTE/Mathematics/Co3NeFull.h>
#include <iostream>
#include <iomanip>

using namespace gte;

std::vector<Vector3<double>> CreateSpherePoints(size_t numPoints)
{
    std::vector<Vector3<double>> points;
    points.reserve(numPoints);
    
    // Fibonacci sphere
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    const double ga = 2.0 * 3.141592653589793 / phi;
    
    for (size_t i = 0; i < numPoints; ++i)
    {
        double lat = std::asin(-1.0 + 2.0 * i / (numPoints - 1.0));
        double lon = ga * i;
        
        double x = std::cos(lat) * std::cos(lon);
        double y = std::cos(lat) * std::sin(lon);
        double z = std::sin(lat);
        
        points.push_back({x, y, z});
    }
    
    return points;
}

int main()
{
    std::cout << "=== Debugging 50-Point Sphere Reconstruction ===\n\n";
    
    auto points = CreateSpherePoints(50);
    std::cout << "Created " << points.size() << " points\n";
    
    // Test with different approaches
    std::cout << "\n--- Test 1: Simplified Co3Ne ---\n";
    {
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeFull<double>::Parameters params;
        params.kNeighbors = 8;
        params.searchRadius = 0.0;  // Auto
        
        bool success = Co3NeFull<double>::Reconstruct(points, vertices, triangles, params);
        std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
        std::cout << "Triangles: " << triangles.size() << "\n";
    }
    
    std::cout << "\n--- Test 2: Enhanced with default params ---\n";
    {
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeFullEnhanced<double>::EnhancedParameters params;
        params.useEnhancedManifold = true;
        
        std::cout << "Parameters:\n";
        std::cout << "  kNeighbors: " << params.kNeighbors << "\n";
        std::cout << "  searchRadius: " << params.searchRadius << "\n";
        std::cout << "  maxNormalAngle: " << params.maxNormalAngle << "\n";
        std::cout << "  strictMode: " << (params.strictMode ? "YES" : "NO") << "\n";
        
        bool success = Co3NeFullEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
        std::cout << "Vertices: " << vertices.size() << "\n";
        std::cout << "Triangles: " << triangles.size() << "\n";
    }
    
    std::cout << "\n--- Test 3: Enhanced with relaxed params ---\n";
    {
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeFullEnhanced<double>::EnhancedParameters params;
        params.useEnhancedManifold = true;
        params.kNeighbors = 12;  // More neighbors
        params.searchRadius = 0.5;  // Larger search radius
        params.maxNormalAngle = 120.0;  // More relaxed angle
        params.strictMode = false;
        
        std::cout << "Parameters:\n";
        std::cout << "  kNeighbors: " << params.kNeighbors << "\n";
        std::cout << "  searchRadius: " << params.searchRadius << "\n";
        std::cout << "  maxNormalAngle: " << params.maxNormalAngle << "\n";
        std::cout << "  strictMode: " << (params.strictMode ? "YES" : "NO") << "\n";
        
        bool success = Co3NeFullEnhanced<double>::Reconstruct(points, vertices, triangles, params);
        std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
        std::cout << "Vertices: " << vertices.size() << "\n";
        std::cout << "Triangles: " << triangles.size() << "\n";
    }
    
    std::cout << "\n--- Test 4: Different point counts ---\n";
    for (size_t n : {20, 30, 40, 50, 60, 75, 100})
    {
        auto pts = CreateSpherePoints(n);
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        Co3NeFullEnhanced<double>::EnhancedParameters params;
        params.useEnhancedManifold = true;
        
        bool success = Co3NeFullEnhanced<double>::Reconstruct(pts, vertices, triangles, params);
        std::cout << "Points: " << std::setw(3) << n 
                  << "  Success: " << (success ? "YES" : "NO ")
                  << "  Triangles: " << std::setw(4) << triangles.size() << "\n";
    }
    
    return 0;
}
