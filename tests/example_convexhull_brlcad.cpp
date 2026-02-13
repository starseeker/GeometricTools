// Example: Using ConvexHull3 for BRL-CAD point cloud reconstruction
// For CONVEX shapes - guaranteed closed, watertight mesh

#include <GTE/Mathematics/ConvexHull3.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <Mathematics/Matrix3x3.h>
#include <Mathematics/PolyhedralMassProperties.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

bool LoadPointCloud(std::string const& filename,
    std::vector<gte::Vector3<double>>& points,
    std::vector<gte::Vector3<double>>& normals)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    points.clear();
    normals.clear();
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        gte::Vector3<double> p, n;
        if (iss >> p[0] >> p[1] >> p[2] >> n[0] >> n[1] >> n[2])
        {
            points.push_back(p);
            normals.push_back(n);
        }
    }
    return !points.empty();
}

bool SaveOBJ(std::string const& filename,
    std::vector<gte::Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (auto const& f : triangles)
    {
        file << "f " << (f[0] + 1) << " " << (f[1] + 1) << " " << (f[2] + 1) << "\n";
    }
    return true;
}

double ComputeVolume(
    std::vector<gte::Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    if (triangles.empty()) return 0.0;
    
    std::vector<int32_t> indices;
    for (auto const& tri : triangles)
    {
        indices.push_back(tri[0]);
        indices.push_back(tri[1]);
        indices.push_back(tri[2]);
    }
    
    double mass;
    gte::Vector3<double> center;
    gte::Matrix3x3<double> inertia;
    
    gte::ComputeMassProperties(vertices.data(),
        static_cast<int32_t>(triangles.size()),
        indices.data(), true, mass, center, inertia);
    
    return std::abs(mass);
}

int main(int argc, char* argv[])
{
    std::string inputFile = "r768.xyz";
    size_t numPoints = 1000;
    if (argc >= 2) numPoints = std::atoi(argv[1]);

    std::cout << "=== ConvexHull3 for BRL-CAD ===" << std::endl;
    std::cout << "Guaranteed closed, watertight mesh" << std::endl;
    std::cout << "Using " << numPoints << " points from " << inputFile << std::endl << std::endl;

    // Load point cloud
    std::vector<gte::Vector3<double>> points, normals;
    if (!LoadPointCloud(inputFile, points, normals))
    {
        std::cerr << "Failed to load " << inputFile << std::endl;
        return 1;
    }
    
    if (numPoints < points.size())
    {
        points.resize(numPoints);
    }
    
    std::cout << "Loaded " << points.size() << " points" << std::endl;

    // Compute convex hull
    std::cout << "\n=== Computing Convex Hull ===" << std::endl;
    
    gte::ConvexHull3<double> convexHull;
    size_t numThreads = 0;  // Single-threaded (use >0 for parallel)
    convexHull(points.size(), points.data(), numThreads);
    
    size_t dimension = convexHull.GetDimension();
    std::cout << "Dimension: " << dimension << std::endl;
    
    if (dimension != 3)
    {
        std::cerr << "Error: Point cloud dimension is " << dimension << " (expected 3)" << std::endl;
        std::cerr << "Point cloud may be degenerate or too small" << std::endl;
        return 1;
    }
    
    // Extract triangles
    auto const& hullIndices = convexHull.GetHull();
    std::cout << "Hull indices: " << hullIndices.size() << " (3 per triangle)" << std::endl;
    std::cout << "Triangles: " << hullIndices.size() / 3 << std::endl;
    
    // Convert to triangle array
    std::vector<std::array<int32_t, 3>> triangles;
    triangles.reserve(hullIndices.size() / 3);
    
    for (size_t i = 0; i < hullIndices.size(); i += 3)
    {
        std::array<int32_t, 3> tri;
        tri[0] = static_cast<int32_t>(hullIndices[i]);
        tri[1] = static_cast<int32_t>(hullIndices[i+1]);
        tri[2] = static_cast<int32_t>(hullIndices[i+2]);
        triangles.push_back(tri);
    }
    
    // Validate mesh
    std::cout << "\n=== Validation ===" << std::endl;
    auto validation = gte::MeshValidation<double>::Validate(points, triangles);
    
    std::cout << "Manifold: " << (validation.isManifold ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << "Closed: " << (validation.isClosed ? "✅ Yes" : "❌ No") << std::endl;
    std::cout << "Boundary edges: " << validation.boundaryEdges << std::endl;
    std::cout << "Non-manifold edges: " << validation.nonManifoldEdges << std::endl;
    
    // Compute volume
    std::cout << "\n=== Properties ===" << std::endl;
    double volume = ComputeVolume(points, triangles);
    std::cout << "Volume: " << volume << " cubic units" << std::endl;
    
    // Save mesh
    std::string outputFile = "r768_convexhull_example.obj";
    if (SaveOBJ(outputFile, points, triangles))
    {
        std::cout << "\nSaved to: " << outputFile << std::endl;
    }
    
    // Summary
    std::cout << "\n=== SUMMARY ===" << std::endl;
    if (validation.isManifold && validation.isClosed)
    {
        std::cout << "✅ SUCCESS: Mesh is manifold and closed" << std::endl;
        std::cout << "✅ Suitable for BRL-CAD" << std::endl;
        std::cout << "✅ Volume calculation is valid: " << volume << std::endl;
    }
    else
    {
        std::cout << "⚠️  Warning: Mesh has issues (should not happen with ConvexHull3)" << std::endl;
    }
    
    std::cout << "\n=== USAGE NOTES ===" << std::endl;
    std::cout << "ConvexHull3 is ideal when:" << std::endl;
    std::cout << "  • Original shape is convex or nearly convex" << std::endl;
    std::cout << "  • You need guaranteed closed, watertight mesh" << std::endl;
    std::cout << "  • Fast computation is important" << std::endl;
    std::cout << "  • Volume accuracy is critical" << std::endl;
    std::cout << "\nFor concave shapes, consider:" << std::endl;
    std::cout << "  • Poisson Surface Reconstruction" << std::endl;
    std::cout << "  • Ball-Pivoting Algorithm" << std::endl;
    std::cout << "  • Alpha Shapes" << std::endl;

    return 0;
}
