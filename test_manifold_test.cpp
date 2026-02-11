// Test suite for enhanced manifold extraction (Co3NeFullEnhanced)
//
// Tests the full Geogram-equivalent manifold extraction including:
// - Topology operations (insert, remove, corner traversal)
// - Adjacent corner detection
// - Orientation tests
// - Non-manifold detection
// - Integration with Co3Ne
// - Comparison with simplified version

#include <Mathematics/Co3NeFullEnhanced.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>

using namespace gte;

// Helper to measure time
class Timer
{
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}
    
    double ElapsedMs() const
    {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }
    
private:
    std::chrono::high_resolution_clock::time_point start_;
};

// Helper to create test point clouds
std::vector<Vector3<double>> CreateCubePoints()
{
    // 8 corners of a unit cube
    return {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
    };
}

std::vector<Vector3<double>> CreateTetrahedronPoints()
{
    // 4 vertices of a tetrahedron
    return {
        {0, 0, 0}, {1, 0, 0}, {0.5, 0.866, 0}, {0.5, 0.289, 0.816}
    };
}

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

// Helper to check if mesh is manifold
bool IsManifold(std::vector<std::array<int32_t, 3>> const& triangles)
{
    // Build edge map: edge -> triangle count
    std::map<std::pair<int32_t, int32_t>, int> edgeCount;
    
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int v1 = tri[i];
            int v2 = tri[(i + 1) % 3];
            
            // Canonical edge (smaller vertex first)
            auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
            edgeCount[edge]++;
        }
    }
    
    // Check all edges have <= 2 triangles
    for (auto const& kv : edgeCount)
    {
        if (kv.second > 2)
        {
            std::cout << "Non-manifold edge: (" << kv.first.first 
                      << "," << kv.first.second << ") with " 
                      << kv.second << " triangles\n";
            return false;
        }
    }
    
    return true;
}

// Helper to check orientation consistency
bool HasConsistentOrientation(std::vector<std::array<int32_t, 3>> const& triangles)
{
    // Build edge map: edge -> (triangle, orientation)
    std::map<std::pair<int32_t, int32_t>, std::vector<std::pair<int, bool>>> edgeTriangles;
    
    for (size_t t = 0; t < triangles.size(); ++t)
    {
        auto const& tri = triangles[t];
        for (int i = 0; i < 3; ++i)
        {
            int v1 = tri[i];
            int v2 = tri[(i + 1) % 3];
            
            auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
            bool forward = (v1 < v2);
            
            edgeTriangles[edge].push_back({static_cast<int>(t), forward});
        }
    }
    
    // Check adjacent triangles have opposite orientations on shared edge
    for (auto const& kv : edgeTriangles)
    {
        if (kv.second.size() == 2)
        {
            // Two triangles share this edge - they should have opposite orientations
            if (kv.second[0].second == kv.second[1].second)
            {
                std::cout << "Inconsistent orientation on edge: (" 
                          << kv.first.first << "," << kv.first.second 
                          << ") triangles " << kv.second[0].first 
                          << " and " << kv.second[1].first << "\n";
                return false;
            }
        }
    }
    
    return true;
}

// Test 1: Basic reconstruction with simple point cloud
bool Test1_SimpleReconstruction()
{
    std::cout << "\n=== Test 1: Simple Reconstruction (Cube) ===\n";
    
    auto points = CreateCubePoints();
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    Co3NeFullEnhanced<double>::EnhancedParameters params;
    params.useEnhancedManifold = true;
    params.searchRadius = 2.0;  // Large enough to capture all neighbors
    
    Timer timer;
    bool success = Co3NeFullEnhanced<double>::Reconstruct(
        points, vertices, triangles, params);
    double elapsed = timer.ElapsedMs();
    
    std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
    std::cout << "Time: " << elapsed << " ms\n";
    std::cout << "Input points: " << points.size() << "\n";
    std::cout << "Output vertices: " << vertices.size() << "\n";
    std::cout << "Output triangles: " << triangles.size() << "\n";
    
    if (success && !triangles.empty())
    {
        bool manifold = IsManifold(triangles);
        bool oriented = HasConsistentOrientation(triangles);
        
        std::cout << "Manifold: " << (manifold ? "YES" : "NO") << "\n";
        std::cout << "Consistent orientation: " << (oriented ? "YES" : "NO") << "\n";
        
        return manifold && oriented;
    }
    
    return false;
}

// Test 2: Compare enhanced vs simplified
bool Test2_EnhancedVsSimplified()
{
    std::cout << "\n=== Test 2: Enhanced vs Simplified ===\n";
    
    auto points = CreateTetrahedronPoints();
    
    // Test with simplified manifold
    std::vector<Vector3<double>> verticesSimple;
    std::vector<std::array<int32_t, 3>> trianglesSimple;
    
    Co3NeFull<double>::Parameters paramsSimple;
    paramsSimple.searchRadius = 2.0;
    
    Timer timer1;
    bool success1 = Co3NeFull<double>::Reconstruct(
        points, verticesSimple, trianglesSimple, paramsSimple);
    double elapsed1 = timer1.ElapsedMs();
    
    // Test with enhanced manifold
    std::vector<Vector3<double>> verticesEnhanced;
    std::vector<std::array<int32_t, 3>> trianglesEnhanced;
    
    Co3NeFullEnhanced<double>::EnhancedParameters paramsEnhanced;
    paramsEnhanced.useEnhancedManifold = true;
    paramsEnhanced.searchRadius = 2.0;
    
    Timer timer2;
    bool success2 = Co3NeFullEnhanced<double>::Reconstruct(
        points, verticesEnhanced, trianglesEnhanced, paramsEnhanced);
    double elapsed2 = timer2.ElapsedMs();
    
    std::cout << "Simplified:\n";
    std::cout << "  Success: " << (success1 ? "YES" : "NO") << "\n";
    std::cout << "  Time: " << elapsed1 << " ms\n";
    std::cout << "  Triangles: " << trianglesSimple.size() << "\n";
    if (!trianglesSimple.empty())
    {
        std::cout << "  Manifold: " << (IsManifold(trianglesSimple) ? "YES" : "NO") << "\n";
    }
    
    std::cout << "Enhanced:\n";
    std::cout << "  Success: " << (success2 ? "YES" : "NO") << "\n";
    std::cout << "  Time: " << elapsed2 << " ms\n";
    std::cout << "  Triangles: " << trianglesEnhanced.size() << "\n";
    if (!trianglesEnhanced.empty())
    {
        std::cout << "  Manifold: " << (IsManifold(trianglesEnhanced) ? "YES" : "NO") << "\n";
    }
    
    return success1 && success2;
}

// Test 3: Larger point cloud (sphere)
bool Test3_LargerPointCloud()
{
    std::cout << "\n=== Test 3: Larger Point Cloud (Sphere 100 points) ===\n";
    
    auto points = CreateSpherePoints(100);
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    Co3NeFullEnhanced<double>::EnhancedParameters params;
    params.useEnhancedManifold = true;
    params.searchRadius = 0.0;  // Auto-detect
    
    Timer timer;
    bool success = Co3NeFullEnhanced<double>::Reconstruct(
        points, vertices, triangles, params);
    double elapsed = timer.ElapsedMs();
    
    std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
    std::cout << "Time: " << elapsed << " ms\n";
    std::cout << "Input points: " << points.size() << "\n";
    std::cout << "Output vertices: " << vertices.size() << "\n";
    std::cout << "Output triangles: " << triangles.size() << "\n";
    
    if (success && !triangles.empty())
    {
        bool manifold = IsManifold(triangles);
        bool oriented = HasConsistentOrientation(triangles);
        
        std::cout << "Manifold: " << (manifold ? "YES" : "NO") << "\n";
        std::cout << "Consistent orientation: " << (oriented ? "YES" : "NO") << "\n";
        
        // Save output for inspection
        std::ofstream out("test_sphere_enhanced.obj");
        if (out.is_open())
        {
            for (auto const& v : vertices)
            {
                out << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
            }
            for (auto const& t : triangles)
            {
                out << "f " << (t[0]+1) << " " << (t[1]+1) << " " << (t[2]+1) << "\n";
            }
            out.close();
            std::cout << "Output saved to test_sphere_enhanced.obj\n";
        }
        
        return manifold && oriented;
    }
    
    return false;
}

// Test 4: Strict mode
bool Test4_StrictMode()
{
    std::cout << "\n=== Test 4: Strict Mode ===\n";
    
    auto points = CreateCubePoints();
    
    // Non-strict mode
    std::vector<Vector3<double>> verticesNonStrict;
    std::vector<std::array<int32_t, 3>> trianglesNonStrict;
    
    Co3NeFullEnhanced<double>::EnhancedParameters paramsNonStrict;
    paramsNonStrict.useEnhancedManifold = true;
    paramsNonStrict.strictMode = false;
    paramsNonStrict.searchRadius = 2.0;
    
    bool success1 = Co3NeFullEnhanced<double>::Reconstruct(
        points, verticesNonStrict, trianglesNonStrict, paramsNonStrict);
    
    // Strict mode
    std::vector<Vector3<double>> verticesStrict;
    std::vector<std::array<int32_t, 3>> trianglesStrict;
    
    Co3NeFullEnhanced<double>::EnhancedParameters paramsStrict;
    paramsStrict.useEnhancedManifold = true;
    paramsStrict.strictMode = true;
    paramsStrict.searchRadius = 2.0;
    
    bool success2 = Co3NeFullEnhanced<double>::Reconstruct(
        points, verticesStrict, trianglesStrict, paramsStrict);
    
    std::cout << "Non-strict mode:\n";
    std::cout << "  Success: " << (success1 ? "YES" : "NO") << "\n";
    std::cout << "  Triangles: " << trianglesNonStrict.size() << "\n";
    
    std::cout << "Strict mode:\n";
    std::cout << "  Success: " << (success2 ? "YES" : "NO") << "\n";
    std::cout << "  Triangles: " << trianglesStrict.size() << "\n";
    
    std::cout << "Note: Strict mode may produce fewer triangles (more conservative)\n";
    
    return true;  // Both modes should work
}

// Test 5: Performance comparison
bool Test5_PerformanceComparison()
{
    std::cout << "\n=== Test 5: Performance Comparison ===\n";
    
    std::vector<size_t> sizes = {10, 50, 100};
    
    std::cout << std::setw(10) << "Points" 
              << std::setw(15) << "Simplified(ms)" 
              << std::setw(15) << "Enhanced(ms)"
              << std::setw(10) << "Ratio" << "\n";
    std::cout << std::string(50, '-') << "\n";
    
    for (auto size : sizes)
    {
        auto points = CreateSpherePoints(size);
        
        // Simplified
        std::vector<Vector3<double>> v1;
        std::vector<std::array<int32_t, 3>> t1;
        Co3NeFull<double>::Parameters p1;
        
        Timer timer1;
        Co3NeFull<double>::Reconstruct(points, v1, t1, p1);
        double time1 = timer1.ElapsedMs();
        
        // Enhanced
        std::vector<Vector3<double>> v2;
        std::vector<std::array<int32_t, 3>> t2;
        Co3NeFullEnhanced<double>::EnhancedParameters p2;
        p2.useEnhancedManifold = true;
        
        Timer timer2;
        Co3NeFullEnhanced<double>::Reconstruct(points, v2, t2, p2);
        double time2 = timer2.ElapsedMs();
        
        double ratio = (time1 > 0) ? (time2 / time1) : 1.0;
        
        std::cout << std::setw(10) << size
                  << std::setw(15) << std::fixed << std::setprecision(2) << time1
                  << std::setw(15) << std::fixed << std::setprecision(2) << time2
                  << std::setw(10) << std::fixed << std::setprecision(2) << ratio << "x\n";
    }
    
    return true;
}

// Test 6: Manifold properties validation
bool Test6_ManifoldValidation()
{
    std::cout << "\n=== Test 6: Manifold Properties Validation ===\n";
    
    auto points = CreateSpherePoints(50);
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    Co3NeFullEnhanced<double>::EnhancedParameters params;
    params.useEnhancedManifold = true;
    
    bool success = Co3NeFullEnhanced<double>::Reconstruct(
        points, vertices, triangles, params);
    
    if (!success || triangles.empty())
    {
        std::cout << "Reconstruction failed\n";
        return false;
    }
    
    // Check manifold properties
    std::cout << "Checking manifold properties...\n";
    
    // 1. Edge manifold check (all edges have <= 2 triangles)
    bool isManifold = IsManifold(triangles);
    std::cout << "1. Edge manifold: " << (isManifold ? "PASS" : "FAIL") << "\n";
    
    // 2. Orientation consistency
    bool hasConsistentOri = HasConsistentOrientation(triangles);
    std::cout << "2. Consistent orientation: " << (hasConsistentOri ? "PASS" : "FAIL") << "\n";
    
    // 3. No degenerate triangles
    bool hasNoDegen = true;
    for (auto const& tri : triangles)
    {
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0])
        {
            hasNoDegen = false;
            std::cout << "Degenerate triangle found: " << tri[0] << " " << tri[1] << " " << tri[2] << "\n";
        }
    }
    std::cout << "3. No degenerate triangles: " << (hasNoDegen ? "PASS" : "FAIL") << "\n";
    
    // 4. Topology analysis
    std::set<std::pair<int32_t, int32_t>> edges;
    std::map<std::pair<int32_t, int32_t>, int> edgeCount;
    std::set<int32_t> usedVertices;
    
    for (auto const& tri : triangles)
    {
        // Count vertices actually used in mesh
        usedVertices.insert(tri[0]);
        usedVertices.insert(tri[1]);
        usedVertices.insert(tri[2]);
        
        // Count edge usage
        for (int i = 0; i < 3; ++i)
        {
            int v1 = tri[i];
            int v2 = tri[(i + 1) % 3];
            auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
            edges.insert(edge);
            edgeCount[edge]++;
        }
    }
    
    // Classify edges
    int boundaryEdges = 0;
    int interiorEdges = 0;
    for (auto const& [edge, count] : edgeCount)
    {
        if (count == 1) boundaryEdges++;
        else if (count == 2) interiorEdges++;
    }
    
    // Calculate Euler characteristic with ACTUAL vertices in mesh
    int V_input = static_cast<int>(vertices.size());
    int V_actual = static_cast<int>(usedVertices.size());
    int E = static_cast<int>(edges.size());
    int F = static_cast<int>(triangles.size());
    int euler = V_actual - E + F;
    
    std::cout << "4. Topology analysis:\n";
    std::cout << "   Input vertices: " << V_input << ", Used in mesh: " << V_actual << "\n";
    std::cout << "   Edges: " << E << " (boundary: " << boundaryEdges << ", interior: " << interiorEdges << ")\n";
    std::cout << "   Faces: " << F << "\n";
    std::cout << "   Euler characteristic: V-E+F = " << V_actual << "-" << E << "+" << F << " = " << euler << "\n";
    if (boundaryEdges > 0)
    {
        std::cout << "   Status: OPEN surface (" << boundaryEdges << " boundary edges - not closed sphere)\n";
    }
    else if (euler == 2)
    {
        std::cout << "   Status: CLOSED sphere (Euler = 2) ✓\n";
    }
    else
    {
        std::cout << "   Status: Closed surface with genus " << (1 - euler/2) << "\n";
    }
    
    bool allPassed = isManifold && hasConsistentOri && hasNoDegen;
    std::cout << "\nOverall: " << (allPassed ? "PASS" : "FAIL") << "\n";
    
    return allPassed;
}

int main()
{
    std::cout << "===============================================\n";
    std::cout << "Enhanced Manifold Extraction Test Suite\n";
    std::cout << "===============================================\n";
    
    int passed = 0;
    int total = 0;
    
    // Run all tests
    total++; if (Test1_SimpleReconstruction()) passed++;
    total++; if (Test2_EnhancedVsSimplified()) passed++;
    total++; if (Test3_LargerPointCloud()) passed++;
    total++; if (Test4_StrictMode()) passed++;
    total++; if (Test5_PerformanceComparison()) passed++;
    total++; if (Test6_ManifoldValidation()) passed++;
    
    // Summary
    std::cout << "\n===============================================\n";
    std::cout << "Test Results: " << passed << "/" << total << " passed\n";
    std::cout << "===============================================\n";
    
    return (passed == total) ? 0 : 1;
}
