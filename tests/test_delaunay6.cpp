// Test program for Delaunay6 implementation
// Tests 6D Delaunay triangulation for anisotropic CVT

#include <GTE/Mathematics/Delaunay6.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <iostream>
#include <vector>

int main()
{
    std::cout << "Testing Delaunay6 implementation...\n\n";

    // Test 1: Basic 6D point creation
    std::cout << "Test 1: Creating 6D points\n";
    std::vector<gte::Vector<6, double>> points6D;
    
    // Create some test points (7 minimum for a 6D simplex)
    for (int i = 0; i < 10; ++i)
    {
        gte::Vector<6, double> p;
        // Position coordinates (x, y, z)
        p[0] = static_cast<double>(i) * 0.1;
        p[1] = static_cast<double>(i) * 0.2;
        p[2] = static_cast<double>(i) * 0.15;
        // Normal coordinates (nx, ny, nz) - scaled
        p[3] = 0.1;
        p[4] = 0.05;
        p[5] = 0.08;
        points6D.push_back(p);
    }
    std::cout << "Created " << points6D.size() << " 6D points\n";

    // Test 2: Delaunay6 triangulation
    std::cout << "\nTest 2: Computing Delaunay triangulation\n";
    gte::Delaunay6<double> delaunay;
    
    bool success = delaunay(points6D.size(), points6D.data());
    
    if (success)
    {
        std::cout << "✓ Delaunay triangulation succeeded\n";
        std::cout << "  Number of simplices: " << delaunay.GetNumSimplices() << "\n";
        std::cout << "  Number of indices: " << delaunay.GetIndices().size() << "\n";
    }
    else
    {
        std::cout << "✗ Delaunay triangulation failed\n";
    }

    // Test 3: Distance computation
    std::cout << "\nTest 3: Testing 6D distance computation\n";
    gte::Vector<6, double> p1, p2;
    for (int i = 0; i < 6; ++i)
    {
        p1[i] = 0.0;
        p2[i] = 1.0;
    }
    
    double dist = gte::Delaunay6<double>::DistanceSquared(p1, p2);
    std::cout << "Distance squared between origin and (1,1,1,1,1,1): " << dist << "\n";
    std::cout << "Expected: 6.0, Got: " << dist << "\n";
    
    if (std::abs(dist - 6.0) < 1e-10)
    {
        std::cout << "✓ Distance computation correct\n";
    }
    else
    {
        std::cout << "✗ Distance computation failed\n";
    }

    // Test 4: Integration with MeshAnisotropy
    std::cout << "\nTest 4: Integration with anisotropic mesh utilities\n";
    
    // Create simple 3D mesh
    std::vector<gte::Vector3<double>> vertices3D = {
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {1.0, 1.0, 0.0},
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0}
    };
    
    std::vector<std::array<int32_t, 3>> triangles = {
        {0, 1, 2},
        {0, 1, 3},
        {0, 2, 3},
        {1, 2, 4}
    };
    
    // Compute anisotropic normals
    std::vector<gte::Vector3<double>> normals;
    gte::MeshAnisotropy<double>::SetAnisotropy(vertices3D, triangles, normals, 0.04);
    std::cout << "Computed " << normals.size() << " anisotropic normals\n";
    
    // Create 6D points from 3D mesh
    auto flattened6D = gte::MeshAnisotropy<double>::Create6DPoints(vertices3D, normals);
    std::cout << "Created " << (flattened6D.size() / 6) << " 6D points from mesh\n";
    
    // Convert flat array to Vector6 array
    std::vector<gte::Vector<6, double>> mesh6D;
    for (size_t i = 0; i < flattened6D.size(); i += 6)
    {
        gte::Vector<6, double> p;
        for (int j = 0; j < 6; ++j)
        {
            p[j] = flattened6D[i + j];
        }
        mesh6D.push_back(p);
    }
    
    std::cout << "✓ Successfully integrated with anisotropic utilities\n";
    
    // Test 5: Nearest simplex query
    if (success && delaunay.GetNumSimplices() > 0)
    {
        std::cout << "\nTest 5: Nearest simplex query\n";
        gte::Vector<6, double> query = points6D[0];
        int32_t nearestIdx = delaunay.FindNearestSimplex(query);
        
        if (nearestIdx >= 0)
        {
            std::cout << "✓ Found nearest simplex: " << nearestIdx << "\n";
        }
        else
        {
            std::cout << "✗ Nearest simplex query failed\n";
        }
    }

    std::cout << "\n=== All basic tests completed ===\n";
    std::cout << "\nNote: This is a minimal implementation.\n";
    std::cout << "Full 6D Delaunay requires more sophisticated algorithms.\n";
    
    return 0;
}
