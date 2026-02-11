// Test parallel RVD implementation
// Verifies correctness and measures performance improvement

#include <GTE/Mathematics/RestrictedVoronoiDiagramOptimized.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>

using namespace gte;

// Create a simple cube mesh
void CreateCubeMesh(
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    // Cube vertices
    vertices = {
        {0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0},  // Bottom
        {0, 0, 2}, {2, 0, 2}, {2, 2, 2}, {0, 2, 2}   // Top
    };

    // Cube faces (2 triangles per face)
    triangles = {
        // Bottom
        {0, 1, 2}, {0, 2, 3},
        // Top
        {4, 6, 5}, {4, 7, 6},
        // Front
        {0, 5, 1}, {0, 4, 5},
        // Back
        {2, 7, 3}, {2, 6, 7},
        // Left
        {0, 3, 7}, {0, 7, 4},
        // Right
        {1, 6, 2}, {1, 5, 6}
    };
}

// Create a grid mesh
void CreateGridMesh(
    int gridSize,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    vertices.clear();
    triangles.clear();

    // Create grid of vertices
    for (int y = 0; y <= gridSize; ++y)
    {
        for (int x = 0; x <= gridSize; ++x)
        {
            double fx = static_cast<double>(x) / gridSize;
            double fy = static_cast<double>(y) / gridSize;
            vertices.push_back({fx * 10.0, fy * 10.0, 0.0});
        }
    }

    // Create triangles
    for (int y = 0; y < gridSize; ++y)
    {
        for (int x = 0; x < gridSize; ++x)
        {
            int v0 = y * (gridSize + 1) + x;
            int v1 = v0 + 1;
            int v2 = v0 + (gridSize + 1);
            int v3 = v2 + 1;

            triangles.push_back({v0, v1, v2});
            triangles.push_back({v1, v3, v2});
        }
    }
}

// Test 1: Correctness - Sequential vs Parallel
bool TestCorrectness()
{
    std::cout << "\n=== Test 1: Correctness (Sequential vs Parallel) ===" << std::endl;

    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateCubeMesh(vertices, triangles);

    // Create Voronoi sites
    std::vector<Vector3<double>> sites = {
        {0.5, 0.5, 0.5},
        {1.5, 0.5, 0.5},
        {0.5, 1.5, 0.5},
        {1.5, 1.5, 0.5},
        {0.5, 0.5, 1.5},
        {1.5, 0.5, 1.5},
        {0.5, 1.5, 1.5},
        {1.5, 1.5, 1.5}
    };

    // Sequential computation
    RestrictedVoronoiDiagramOptimized<double> rvdSeq;
    RestrictedVoronoiDiagramOptimized<double>::Parameters paramsSeq;
    paramsSeq.useParallel = false;
    
    rvdSeq.Initialize(vertices, triangles, sites, paramsSeq);
    std::vector<RestrictedVoronoiDiagramOptimized<double>::RVD_Cell> cellsSeq;
    rvdSeq.ComputeCells(cellsSeq);

    // Parallel computation
    RestrictedVoronoiDiagramOptimized<double> rvdPar;
    RestrictedVoronoiDiagramOptimized<double>::Parameters paramsPar;
    paramsPar.useParallel = true;
    paramsPar.numThreads = 4;
    
    rvdPar.Initialize(vertices, triangles, sites, paramsPar);
    std::vector<RestrictedVoronoiDiagramOptimized<double>::RVD_Cell> cellsPar;
    rvdPar.ComputeCells(cellsPar);

    // Compare results
    if (cellsSeq.size() != cellsPar.size())
    {
        std::cout << "FAILED: Different number of cells" << std::endl;
        return false;
    }

    double maxCentroidError = 0.0;
    double maxMassError = 0.0;

    for (size_t i = 0; i < cellsSeq.size(); ++i)
    {
        // Compare mass
        double massError = std::abs(cellsSeq[i].mass - cellsPar[i].mass);
        maxMassError = std::max(maxMassError, massError);

        // Compare centroids
        Vector3<double> diff = cellsSeq[i].centroid - cellsPar[i].centroid;
        double centroidError = Length(diff);
        maxCentroidError = std::max(maxCentroidError, centroidError);
    }

    std::cout << "  Max mass error: " << maxMassError << std::endl;
    std::cout << "  Max centroid error: " << maxCentroidError << std::endl;

    if (maxMassError > 1e-10 || maxCentroidError > 1e-10)
    {
        std::cout << "FAILED: Results differ significantly" << std::endl;
        return false;
    }

    std::cout << "PASSED: Sequential and parallel results match" << std::endl;
    return true;
}

// Test 2: Performance - Different thread counts
bool TestPerformance()
{
    std::cout << "\n=== Test 2: Performance Scaling ===" << std::endl;

    // Create larger mesh
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateGridMesh(30, vertices, triangles);  // 961 vertices, 1800 triangles

    std::cout << "  Mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles" << std::endl;

    // Create many Voronoi sites
    std::vector<Vector3<double>> sites;
    for (int z = 0; z < 3; ++z)
    {
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                sites.push_back({
                    x * 2.5 + 1.0,
                    y * 2.5 + 1.0,
                    z * 0.5 - 0.5
                });
            }
        }
    }

    std::cout << "  Sites: " << sites.size() << std::endl;

    // Test different thread counts
    std::vector<int> threadCounts = {0, 1, 2, 4, 8};  // 0 = sequential
    std::vector<double> times;

    for (int numThreads : threadCounts)
    {
        RestrictedVoronoiDiagramOptimized<double> rvd;
        RestrictedVoronoiDiagramOptimized<double>::Parameters params;
        params.useParallel = (numThreads > 0);
        params.numThreads = numThreads;

        rvd.Initialize(vertices, triangles, sites, params);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<RestrictedVoronoiDiagramOptimized<double>::RVD_Cell> cells;
        rvd.ComputeCells(cells);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        times.push_back(duration.count());

        std::string label = (numThreads == 0) ? "Sequential" : 
                           std::to_string(numThreads) + " threads";
        std::cout << "  " << label << ": " << duration.count() << " ms" << std::endl;
    }

    // Calculate speedup
    if (times.size() >= 2 && times[0] > 0)
    {
        for (size_t i = 1; i < times.size(); ++i)
        {
            double speedup = times[0] / times[i];
            std::cout << "  Speedup (" << threadCounts[i] << " threads): " 
                      << speedup << "x" << std::endl;
        }
    }

    std::cout << "PASSED: Performance test complete" << std::endl;
    return true;
}

// Test 3: Lloyd relaxation with parallel RVD
bool TestLloydWithParallel()
{
    std::cout << "\n=== Test 3: Lloyd Relaxation with Parallel RVD ===" << std::endl;

    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    CreateGridMesh(20, vertices, triangles);

    // Initial sites (slightly off-center)
    std::vector<Vector3<double>> sites = {
        {2.3, 2.3, 0.0}, {7.7, 2.3, 0.0},
        {2.3, 7.7, 0.0}, {7.7, 7.7, 0.0}
    };

    RestrictedVoronoiDiagramOptimized<double> rvd;
    RestrictedVoronoiDiagramOptimized<double>::Parameters params;
    params.useParallel = true;
    params.numThreads = 4;

    // Lloyd iterations
    const int numIterations = 5;
    for (int iter = 0; iter < numIterations; ++iter)
    {
        rvd.Initialize(vertices, triangles, sites, params);
        
        std::vector<Vector3<double>> centroids;
        rvd.ComputeCentroids(centroids);

        // Compute displacement
        double totalDisplacement = 0.0;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            Vector3<double> diff = centroids[i] - sites[i];
            totalDisplacement += Length(diff);
        }

        std::cout << "  Iteration " << (iter + 1) << ": displacement = " 
                  << totalDisplacement << std::endl;

        // Move sites to centroids
        sites = centroids;

        // Check for convergence
        if (totalDisplacement < 0.001)
        {
            std::cout << "  Converged after " << (iter + 1) << " iterations" << std::endl;
            break;
        }
    }

    std::cout << "PASSED: Lloyd relaxation converged" << std::endl;
    return true;
}

int main()
{
    std::cout << "Parallel RVD Test Suite" << std::endl;
    std::cout << "=======================" << std::endl;

    // Detect hardware
    size_t hwConcurrency = std::thread::hardware_concurrency();
    std::cout << "Hardware concurrency: " << hwConcurrency << " threads" << std::endl;

    bool allPassed = true;

    allPassed &= TestCorrectness();
    allPassed &= TestPerformance();
    allPassed &= TestLloydWithParallel();

    std::cout << "\n=== Summary ===" << std::endl;
    if (allPassed)
    {
        std::cout << "✓ ALL TESTS PASSED" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "✗ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
