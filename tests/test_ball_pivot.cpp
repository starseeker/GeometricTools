// Test program for BallPivotReconstruction

#include <GTE/Mathematics/BallPivotReconstruction.h>
#include <iostream>
#include <fstream>
#include <cmath>

using namespace gte;

// Generate a simple sphere point cloud
void GenerateSphere(
    std::vector<Vector3<double>>& points,
    std::vector<Vector3<double>>& normals,
    int numPoints,
    double radius)
{
    points.clear();
    normals.clear();
    
    // Generate points on sphere using golden spiral
    double phi = (1.0 + std::sqrt(5.0)) / 2.0;  // Golden ratio
    
    for (int i = 0; i < numPoints; ++i)
    {
        double y = 1.0 - (i / double(numPoints - 1)) * 2.0;  // y from 1 to -1
        double radiusAtY = std::sqrt(1.0 - y * y);
        
        double theta = 2.0 * 3.14159265358979323846 * i / phi;
        
        double x = std::cos(theta) * radiusAtY;
        double z = std::sin(theta) * radiusAtY;
        
        Vector3<double> point{x * radius, y * radius, z * radius};
        Vector3<double> normal{x, y, z};  // Unit outward normal
        
        points.push_back(point);
        normals.push_back(normal);
    }
}

// Save mesh to OBJ file
void SaveOBJ(
    std::string const& filename,
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file)
    {
        std::cerr << "Failed to open " << filename << "\n";
        return;
    }
    
    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    // Write faces (OBJ uses 1-based indices)
    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }
    
    file.close();
    std::cout << "Saved mesh to " << filename << "\n";
}

int main(int argc, char* argv[])
{
    std::cout << "=== Ball Pivot Reconstruction Test ===\n\n";
    
    // Test 1: Simple tetrahedron
    std::cout << "Test 1: Tetrahedron (4 points)\n";
    {
        std::vector<Vector3<double>> points = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {0.5, 0.866, 0.0},
            {0.5, 0.433, 0.816}
        };
        
        std::vector<Vector3<double>> normals = {
            {-0.577, -0.577, -0.577},
            {0.816, -0.408, -0.408},
            {0.0, 0.816, -0.408},
            {0.0, 0.0, 1.0}
        };
        
        // Normalize normals
        for (auto& n : normals)
        {
            double len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
            if (len > 0.0)
            {
                n[0] /= len;
                n[1] /= len;
                n[2] /= len;
            }
        }
        
        BallPivotReconstruction<double>::Parameters params;
        params.verbose = true;
        params.radii = {0.5, 1.0};  // Multiple radii
        
        std::vector<Vector3<double>> outVertices;
        std::vector<std::array<int32_t, 3>> outTriangles;
        
        bool success = BallPivotReconstruction<double>::Reconstruct(
            points, normals, outVertices, outTriangles, params);
        
        std::cout << "  Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
        std::cout << "  Triangles: " << outTriangles.size() << "\n\n";
        
        if (success && !outTriangles.empty())
        {
            SaveOBJ("tetrahedron_output.obj", outVertices, outTriangles);
        }
    }
    
    // Test 2: Sphere point cloud
    std::cout << "Test 2: Sphere (100 points)\n";
    {
        std::vector<Vector3<double>> points;
        std::vector<Vector3<double>> normals;
        
        GenerateSphere(points, normals, 100, 1.0);
        
        BallPivotReconstruction<double>::Parameters params;
        params.verbose = true;
        // Auto radii based on point cloud
        
        std::vector<Vector3<double>> outVertices;
        std::vector<std::array<int32_t, 3>> outTriangles;
        
        bool success = BallPivotReconstruction<double>::Reconstruct(
            points, normals, outVertices, outTriangles, params);
        
        std::cout << "  Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
        std::cout << "  Triangles: " << outTriangles.size() << "\n\n";
        
        if (success && !outTriangles.empty())
        {
            SaveOBJ("sphere_output.obj", outVertices, outTriangles);
        }
    }
    
    // Test 3: Larger sphere
    std::cout << "Test 3: Sphere (500 points)\n";
    {
        std::vector<Vector3<double>> points;
        std::vector<Vector3<double>> normals;
        
        GenerateSphere(points, normals, 500, 1.0);
        
        BallPivotReconstruction<double>::Parameters params;
        params.verbose = false;  // Less verbose for larger test
        
        std::vector<Vector3<double>> outVertices;
        std::vector<std::array<int32_t, 3>> outTriangles;
        
        bool success = BallPivotReconstruction<double>::Reconstruct(
            points, normals, outVertices, outTriangles, params);
        
        std::cout << "  Result: " << (success ? "SUCCESS" : "FAILED") << "\n";
        std::cout << "  Triangles: " << outTriangles.size() << "\n\n";
        
        if (success && !outTriangles.empty())
        {
            SaveOBJ("sphere500_output.obj", outVertices, outTriangles);
        }
    }
    
    std::cout << "=== Tests Complete ===\n";
    
    return 0;
}
