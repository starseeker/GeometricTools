// Test program for Co3Ne with RVD smoothing

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <cmath>
#include <random>

using namespace gte;

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

// Compute mesh quality
struct QualityMetrics
{
    double avgEdgeLength;
    double edgeLengthStdDev;
    double avgTriangleQuality;
    double minTriangleQuality;
    int numTriangles;
};

QualityMetrics ComputeQuality(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    QualityMetrics metrics{};
    metrics.numTriangles = static_cast<int>(triangles.size());

    if (triangles.empty()) return metrics;

    // Compute edge lengths
    std::vector<double> edgeLengths;
    std::map<std::pair<int32_t, int32_t>, bool> processedEdges;

    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int j = (i + 1) % 3;
            auto edge = std::make_pair(std::min(tri[i], tri[j]), std::max(tri[i], tri[j]));

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
    for (double len : edgeLengths)
    {
        metrics.avgEdgeLength += len;
    }
    metrics.avgEdgeLength /= edgeLengths.size();

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
    std::cout << "  Triangles: " << metrics.numTriangles << std::endl;
    std::cout << "  Edge Length: avg=" << metrics.avgEdgeLength
              << ", stddev=" << metrics.edgeLengthStdDev << std::endl;
    std::cout << "  Triangle Quality: avg=" << metrics.avgTriangleQuality
              << ", min=" << metrics.minTriangleQuality << std::endl;
}

// Generate point cloud from sphere
std::vector<Vector3<double>> GenerateSpherePointCloud(int numPoints, double radius)
{
    std::vector<Vector3<double>> points;
    points.reserve(numPoints);

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (int i = 0; i < numPoints; ++i)
    {
        // Fibonacci sphere sampling for uniform distribution
        double phi = std::acos(1.0 - 2.0 * (i + 0.5) / numPoints);
        double theta = 3.14159265358979323846 * (1.0 + std::sqrt(5.0)) * i;

        double x = radius * std::sin(phi) * std::cos(theta);
        double y = radius * std::sin(phi) * std::sin(theta);
        double z = radius * std::cos(phi);

        // Add small noise
        x += (dist(rng) - 0.5) * radius * 0.01;
        y += (dist(rng) - 0.5) * radius * 0.01;
        z += (dist(rng) - 0.5) * radius * 0.01;

        points.push_back(Vector3<double>{ x, y, z });
    }

    return points;
}

int main(int argc, char* argv[])
{
    std::cout << "===== Co3Ne with RVD Smoothing Test =====" << std::endl;

    // Generate sphere point cloud
    int numPoints = 200;
    double radius = 1.0;
    std::cout << "\nGenerating sphere point cloud..." << std::endl;
    std::cout << "  Points: " << numPoints << std::endl;
    std::cout << "  Radius: " << radius << std::endl;

    auto points = GenerateSpherePointCloud(numPoints, radius);
    std::cout << "  Generated " << points.size() << " points" << std::endl;

    // Test 1: Without RVD smoothing
    std::cout << "\n===== Test 1: Co3Ne WITHOUT RVD Smoothing =====" << std::endl;
    {
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;

        Co3Ne<double>::Parameters params;
        params.kNeighbors = 20;
        params.orientNormals = true;
        params.smoothWithRVD = false;  // Disable RVD smoothing

        if (Co3Ne<double>::Reconstruct(points, vertices, triangles, params))
        {
            std::cout << "SUCCESS: Reconstruction complete" << std::endl;
            std::cout << "  Output: " << vertices.size() << " vertices, "
                      << triangles.size() << " triangles" << std::endl;

            auto metrics = ComputeQuality(vertices, triangles);
            PrintMetrics("  Quality", metrics);

            SaveOBJ("co3ne_no_rvd.obj", vertices, triangles);
            std::cout << "  Saved to: co3ne_no_rvd.obj" << std::endl;
        }
        else
        {
            std::cout << "FAILED: Reconstruction failed" << std::endl;
        }
    }

    // Test 2: With RVD smoothing
    std::cout << "\n===== Test 2: Co3Ne WITH RVD Smoothing =====" << std::endl;
    {
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;

        Co3Ne<double>::Parameters params;
        params.kNeighbors = 20;
        params.orientNormals = true;
        params.smoothWithRVD = true;       // Enable RVD smoothing
        params.rvdSmoothIterations = 3;    // 3 iterations

        if (Co3Ne<double>::Reconstruct(points, vertices, triangles, params))
        {
            std::cout << "SUCCESS: Reconstruction complete" << std::endl;
            std::cout << "  Output: " << vertices.size() << " vertices, "
                      << triangles.size() << " triangles" << std::endl;

            auto metrics = ComputeQuality(vertices, triangles);
            PrintMetrics("  Quality", metrics);

            SaveOBJ("co3ne_with_rvd.obj", vertices, triangles);
            std::cout << "  Saved to: co3ne_with_rvd.obj" << std::endl;
        }
        else
        {
            std::cout << "FAILED: Reconstruction failed" << std::endl;
        }
    }

    std::cout << "\n===== Test Complete =====" << std::endl;
    std::cout << "Compare output files:" << std::endl;
    std::cout << "  - co3ne_no_rvd.obj (without RVD smoothing)" << std::endl;
    std::cout << "  - co3ne_with_rvd.obj (with RVD smoothing - better quality)" << std::endl;

    return 0;
}
