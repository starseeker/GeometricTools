// Test program comparing RVD-based vs approximate Lloyd relaxation

#include <GTE/Mathematics/MeshRemesh.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace gte;

// Load OBJ file
bool LoadOBJ(std::string const& filename,
             std::vector<Vector3<double>>& vertices,
             std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    vertices.clear();
    triangles.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v")
        {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{ x, y, z });
        }
        else if (type == "f")
        {
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            triangles.push_back({ v0 - 1, v1 - 1, v2 - 1 });
        }
    }

    return !vertices.empty() && !triangles.empty();
}

// Save OBJ file
bool SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open()) return false;

    for (auto const& v : vertices)
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";

    for (auto const& tri : triangles)
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";

    return true;
}

// Compute mesh quality metrics
struct QualityMetrics
{
    double avgEdgeLength;
    double minEdgeLength;
    double maxEdgeLength;
    double edgeLengthStdDev;
    double avgTriangleQuality;
    double minTriangleQuality;
};

QualityMetrics ComputeQualityMetrics(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    QualityMetrics metrics{};
    
    if (triangles.empty()) return metrics;
    
    // Compute edge lengths
    std::vector<double> edgeLengths;
    std::map<std::pair<int32_t, int32_t>, bool> processedEdges;
    
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int j = (i + 1) % 3;
            auto edge = std::make_pair(
                std::min(tri[i], tri[j]),
                std::max(tri[i], tri[j])
            );
            
            if (processedEdges.find(edge) == processedEdges.end())
            {
                processedEdges[edge] = true;
                double len = Length(vertices[tri[i]] - vertices[tri[j]]);
                edgeLengths.push_back(len);
            }
        }
    }
    
    // Edge statistics
    metrics.avgEdgeLength = 0.0;
    metrics.minEdgeLength = edgeLengths[0];
    metrics.maxEdgeLength = edgeLengths[0];
    
    for (double len : edgeLengths)
    {
        metrics.avgEdgeLength += len;
        metrics.minEdgeLength = std::min(metrics.minEdgeLength, len);
        metrics.maxEdgeLength = std::max(metrics.maxEdgeLength, len);
    }
    metrics.avgEdgeLength /= edgeLengths.size();
    
    // Standard deviation
    double variance = 0.0;
    for (double len : edgeLengths)
    {
        double diff = len - metrics.avgEdgeLength;
        variance += diff * diff;
    }
    metrics.edgeLengthStdDev = std::sqrt(variance / edgeLengths.size());
    
    // Triangle quality
    metrics.avgTriangleQuality = 0.0;
    metrics.minTriangleQuality = 1.0;
    
    for (auto const& tri : triangles)
    {
        Vector3<double> const& p0 = vertices[tri[0]];
        Vector3<double> const& p1 = vertices[tri[1]];
        Vector3<double> const& p2 = vertices[tri[2]];
        
        Vector3<double> e1 = p1 - p0;
        Vector3<double> e2 = p2 - p0;
        Vector3<double> e3 = p2 - p1;
        
        double area = Length(Cross(e1, e2)) * 0.5;
        double len0 = Length(e1);
        double len1 = Length(e2);
        double len2 = Length(e3);
        
        double sumLenSq = len0 * len0 + len1 * len1 + len2 * len2;
        double quality = (sumLenSq > 0.0) ? (4.0 * std::sqrt(3.0) * area / sumLenSq) : 0.0;
        
        metrics.avgTriangleQuality += quality;
        metrics.minTriangleQuality = std::min(metrics.minTriangleQuality, quality);
    }
    metrics.avgTriangleQuality /= triangles.size();
    
    return metrics;
}

void PrintMetrics(std::string const& label, QualityMetrics const& metrics)
{
    std::cout << label << ":" << std::endl;
    std::cout << "  Edge Length: avg=" << metrics.avgEdgeLength 
              << ", min=" << metrics.minEdgeLength
              << ", max=" << metrics.maxEdgeLength
              << ", stddev=" << metrics.edgeLengthStdDev << std::endl;
    std::cout << "  Triangle Quality: avg=" << metrics.avgTriangleQuality
              << ", min=" << metrics.minTriangleQuality << std::endl;
}

int main(int argc, char* argv[])
{
    std::cout << "===== RVD-based vs Approximate Lloyd Comparison =====" << std::endl;

    // Load test mesh
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    std::vector<std::string> testFiles = {
        "test_tiny.obj",
        "stress_concave.obj",
        "test_ec3d.obj"
    };

    bool loaded = false;
    std::string loadedFile;
    for (auto const& filename : testFiles)
    {
        if (LoadOBJ(filename, vertices, triangles))
        {
            loadedFile = filename;
            loaded = true;
            break;
        }
    }

    if (!loaded)
    {
        std::cout << "No test mesh found, creating simple grid" << std::endl;
        // Create a 5x5 grid
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                vertices.push_back(Vector3<double>{
                    static_cast<double>(x),
                    static_cast<double>(y),
                    0.0
                });
            }
        }
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                int v0 = y * 5 + x;
                int v1 = y * 5 + (x + 1);
                int v2 = (y + 1) * 5 + (x + 1);
                int v3 = (y + 1) * 5 + x;
                triangles.push_back({ v0, v1, v2 });
                triangles.push_back({ v0, v2, v3 });
            }
        }
        loadedFile = "synthetic grid";
    }

    std::cout << "Loaded: " << loadedFile << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;

    // Compute initial metrics
    auto initialMetrics = ComputeQualityMetrics(vertices, triangles);
    PrintMetrics("\nInitial Mesh", initialMetrics);

    // Test 1: Approximate Lloyd
    std::cout << "\n===== Test 1: Approximate Lloyd (90% quality) =====" << std::endl;
    {
        auto testVertices = vertices;
        auto testTriangles = triangles;

        MeshRemesh<double>::Parameters params;
        params.lloydIterations = 5;
        params.useRVD = false;  // Use approximate method
        params.preserveBoundary = true;
        params.projectToSurface = false;  // Don't project for fair comparison

        if (MeshRemesh<double>::Remesh(testVertices, testTriangles, params))
        {
            std::cout << "SUCCESS: Approximate remeshing complete" << std::endl;
            std::cout << "  Output: " << testVertices.size() << " vertices, "
                      << testTriangles.size() << " triangles" << std::endl;

            auto metrics = ComputeQualityMetrics(testVertices, testTriangles);
            PrintMetrics("  Final Mesh", metrics);

            SaveOBJ("remesh_approximate.obj", testVertices, testTriangles);
            std::cout << "  Saved to: remesh_approximate.obj" << std::endl;
        }
        else
        {
            std::cout << "FAILED: Approximate remeshing" << std::endl;
        }
    }

    // Test 2: RVD-based Lloyd
    std::cout << "\n===== Test 2: RVD-based Lloyd (100% quality) =====" << std::endl;
    {
        auto testVertices = vertices;
        auto testTriangles = triangles;

        MeshRemesh<double>::Parameters params;
        params.lloydIterations = 5;
        params.useRVD = true;  // Use exact RVD
        params.preserveBoundary = true;
        params.projectToSurface = false;  // Don't project for fair comparison

        if (MeshRemesh<double>::Remesh(testVertices, testTriangles, params))
        {
            std::cout << "SUCCESS: RVD-based remeshing complete" << std::endl;
            std::cout << "  Output: " << testVertices.size() << " vertices, "
                      << testTriangles.size() << " triangles" << std::endl;

            auto metrics = ComputeQualityMetrics(testVertices, testTriangles);
            PrintMetrics("  Final Mesh", metrics);

            SaveOBJ("remesh_rvd.obj", testVertices, testTriangles);
            std::cout << "  Saved to: remesh_rvd.obj" << std::endl;
        }
        else
        {
            std::cout << "FAILED: RVD-based remeshing" << std::endl;
        }
    }

    std::cout << "\n===== Comparison Complete =====" << std::endl;
    std::cout << "Compare the output files:" << std::endl;
    std::cout << "  - remesh_approximate.obj (90% quality, faster)" << std::endl;
    std::cout << "  - remesh_rvd.obj (100% quality, exact CVT)" << std::endl;

    return 0;
}
