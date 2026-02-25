// Performance benchmark for Co3Ne mesh reconstruction and stitching
//
// Tests various input sizes to determine scaling characteristics

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <random>

using namespace gte;
using namespace std::chrono;

// Load XYZ file
bool LoadXYZ(std::string const& filename, 
             std::vector<Vector3<double>>& points)
{
    std::ifstream in(filename);
    if (!in.is_open())
    {
        std::cerr << "Failed to open " << filename << "\n";
        return false;
    }
    
    points.clear();
    double x, y, z;
    
    // Try to read with normals first
    double nx, ny, nz;
    std::string line;
    if (std::getline(in, line))
    {
        std::istringstream iss(line);
        if (iss >> x >> y >> z >> nx >> ny >> nz)
        {
            // Has normals, read remaining with normals
            points.push_back({x, y, z});
            while (in >> x >> y >> z >> nx >> ny >> nz)
            {
                points.push_back({x, y, z});
            }
        }
        else
        {
            // No normals, just coordinates
            std::istringstream iss2(line);
            if (iss2 >> x >> y >> z)
            {
                points.push_back({x, y, z});
                while (in >> x >> y >> z)
                {
                    points.push_back({x, y, z});
                }
            }
        }
    }
    
    in.close();
    return !points.empty();
}

// Benchmark a specific point cloud size
struct BenchmarkResult
{
    size_t numPoints;
    size_t numVertices;
    size_t numTriangles;
    size_t numComponents;
    size_t boundaryEdges;
    double reconstructionTime;
    double stitchingTime;
    double totalTime;
    bool success;
    bool isManifold;
};

BenchmarkResult RunBenchmark(std::vector<Vector3<double>> const& points, bool verbose = false)
{
    BenchmarkResult result;
    result.numPoints = points.size();
    result.success = false;
    result.isManifold = false;
    
    if (verbose)
    {
        std::cout << "\n=== Benchmarking " << points.size() << " points ===\n";
    }
    
    // Phase 1: Co3Ne Reconstruction
    auto startReconstruct = high_resolution_clock::now();
    
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool reconstructSuccess = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    auto endReconstruct = high_resolution_clock::now();
    result.reconstructionTime = duration<double>(endReconstruct - startReconstruct).count();
    
    if (!reconstructSuccess || triangles.empty())
    {
        if (verbose)
        {
            std::cout << "Reconstruction FAILED\n";
        }
        return result;
    }
    
    result.numVertices = vertices.size();
    result.numTriangles = triangles.size();
    
    if (verbose)
    {
        std::cout << "Reconstruction: " << result.reconstructionTime << "s\n";
        std::cout << "  Output: " << vertices.size() << " vertices, " 
                  << triangles.size() << " triangles\n";
    }
    
    // Phase 2: Manifold Stitching
    auto startStitch = high_resolution_clock::now();
    
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = false;
    stitchParams.enableIterativeBridging = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.enableBallPivotHoleFiller = false;
    stitchParams.initialBridgeThreshold = 2.0;
    stitchParams.maxBridgeThreshold = 10.0;
    stitchParams.maxIterations = 10;
    
    result.isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    auto endStitch = high_resolution_clock::now();
    result.stitchingTime = duration<double>(endStitch - startStitch).count();
    result.totalTime = result.reconstructionTime + result.stitchingTime;
    
    // Get final statistics
    bool isClosedManifold = false;
    bool hasNonManifoldEdges = false;
    size_t boundaryEdgeCount = 0;
    size_t componentCount = 0;
    
    // We need to access the private method, so we'll estimate from the result
    result.numComponents = result.isManifold ? 1 : 0;  // Simplified
    result.boundaryEdges = 0;  // Would need access to ValidateManifoldDetailed
    result.success = true;
    
    if (verbose)
    {
        std::cout << "Stitching: " << result.stitchingTime << "s\n";
        std::cout << "  Final: " << triangles.size() << " triangles\n";
        std::cout << "  Manifold: " << (result.isManifold ? "YES" : "NO") << "\n";
        std::cout << "Total time: " << result.totalTime << "s\n";
    }
    
    return result;
}

int main(int argc, char* argv[])
{
    std::cout << "=== Co3Ne Performance Benchmark ===\n\n";
    
    // Load full dataset
    std::vector<Vector3<double>> fullPoints;
    if (!LoadXYZ("r768.xyz", fullPoints))
    {
        std::cerr << "Failed to load r768.xyz\n";
        return 1;
    }
    
    std::cout << "Loaded " << fullPoints.size() << " points from r768.xyz\n\n";

    // Build a shuffled index array so every subset is a spatially distributed
    // random sample rather than a head-N geographic cluster.  A fixed seed
    // makes results reproducible across runs.
    std::vector<size_t> shuffledIdx(fullPoints.size());
    std::iota(shuffledIdx.begin(), shuffledIdx.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(shuffledIdx.begin(), shuffledIdx.end(), rng);

    // Test sizes: 100, 500, 1k, 2k, 5k, 10k, 20k, 40k (and full if small enough)
    std::vector<size_t> testSizes = {100, 500, 1000, 2000, 5000, 10000};

    if (fullPoints.size() <= 50000)
    {
        testSizes.push_back(fullPoints.size());
    }
    else
    {
        testSizes.push_back(20000);
        testSizes.push_back(40000);
        if (fullPoints.size() > 40000)
        {
            testSizes.push_back(fullPoints.size());
        }
    }

    std::vector<BenchmarkResult> results;

    std::cout << "Running benchmarks...\n";
    std::cout << std::string(80, '=') << "\n";

    for (size_t testSize : testSizes)
    {
        if (testSize > fullPoints.size())
        {
            continue;
        }

        // Build a spatially distributed random subset (not a geographic head slice)
        std::vector<Vector3<double>> testPoints(testSize);
        for (size_t i = 0; i < testSize; ++i)
        {
            testPoints[i] = fullPoints[shuffledIdx[i]];
        }
        
        // Run benchmark
        auto result = RunBenchmark(testPoints, true);
        results.push_back(result);
        
        // Early stop if times are getting too long
        if (result.totalTime > 60.0)
        {
            std::cout << "\nStopping: runtime exceeded 60 seconds\n";
            break;
        }
    }
    
    // Print summary table
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "\n=== Performance Summary ===\n\n";
    std::cout << std::setw(8) << "Points" << " | "
              << std::setw(8) << "Vertices" << " | "
              << std::setw(8) << "Triangles" << " | "
              << std::setw(10) << "Reconstr." << " | "
              << std::setw(10) << "Stitching" << " | "
              << std::setw(10) << "Total" << " | "
              << "Status\n";
    std::cout << std::string(80, '-') << "\n";
    
    for (auto const& r : results)
    {
        if (!r.success) continue;
        
        std::cout << std::setw(8) << r.numPoints << " | "
                  << std::setw(8) << r.numVertices << " | "
                  << std::setw(8) << r.numTriangles << " | "
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.reconstructionTime << "s | "
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.stitchingTime << "s | "
                  << std::setw(9) << std::fixed << std::setprecision(2) << r.totalTime << "s | "
                  << (r.isManifold ? "✓" : "✗") << "\n";
    }
    
    // Analyze scaling
    std::cout << "\n=== Scaling Analysis ===\n\n";
    
    if (results.size() >= 2)
    {
        // Calculate approximate complexity
        for (size_t i = 1; i < results.size(); ++i)
        {
            auto const& prev = results[i-1];
            auto const& curr = results[i];
            
            if (!prev.success || !curr.success) continue;
            
            double sizeRatio = static_cast<double>(curr.numPoints) / prev.numPoints;
            double timeRatio = curr.totalTime / prev.totalTime;
            
            // Estimate complexity: if time ratio = size ratio^k, then k = log(timeRatio)/log(sizeRatio)
            double k = std::log(timeRatio) / std::log(sizeRatio);
            
            std::cout << "From " << prev.numPoints << " to " << curr.numPoints << " points:\n";
            std::cout << "  Size ratio: " << std::fixed << std::setprecision(2) << sizeRatio << "x\n";
            std::cout << "  Time ratio: " << std::fixed << std::setprecision(2) << timeRatio << "x\n";
            std::cout << "  Estimated complexity: O(n^" << std::fixed << std::setprecision(2) << k << ")\n\n";
        }
    }
    
    // Extrapolate to larger sizes
    if (results.size() >= 2 && results.back().success)
    {
        std::cout << "=== Extrapolation ===\n\n";
        
        auto const& last = results.back();
        double avgK = 0;
        int count = 0;
        
        for (size_t i = 1; i < results.size(); ++i)
        {
            auto const& prev = results[i-1];
            auto const& curr = results[i];
            if (!prev.success || !curr.success) continue;
            
            double sizeRatio = static_cast<double>(curr.numPoints) / prev.numPoints;
            double timeRatio = curr.totalTime / prev.totalTime;
            avgK += std::log(timeRatio) / std::log(sizeRatio);
            count++;
        }
        
        if (count > 0)
        {
            avgK /= count;
            
            std::cout << "Based on average complexity O(n^" << std::fixed << std::setprecision(2) << avgK << "):\n\n";
            
            for (size_t targetSize : {50000, 100000, 200000})
            {
                if (targetSize <= last.numPoints) continue;
                
                double sizeRatio = static_cast<double>(targetSize) / last.numPoints;
                double expectedTime = last.totalTime * std::pow(sizeRatio, avgK);
                
                std::cout << "  " << std::setw(7) << targetSize << " points: ~"
                          << std::fixed << std::setprecision(1) << expectedTime << "s";
                
                if (expectedTime < 60)
                {
                    std::cout << " (< 1 min) ✓\n";
                }
                else if (expectedTime < 300)
                {
                    std::cout << " (< 5 min) ~\n";
                }
                else
                {
                    std::cout << " (> 5 min) ✗\n";
                }
            }
        }
    }
    
    std::cout << "\n=== Conclusion ===\n\n";
    
    if (!results.empty() && results.back().success)
    {
        auto const& best = results.back();
        std::cout << "Successfully processed up to " << best.numPoints << " points\n";
        std::cout << "in " << std::fixed << std::setprecision(1) << best.totalTime << " seconds.\n\n";
        
        if (best.totalTime < 10.0)
        {
            std::cout << "Performance: EXCELLENT - Can handle much larger inputs\n";
        }
        else if (best.totalTime < 60.0)
        {
            std::cout << "Performance: GOOD - Practical for real-world use\n";
        }
        else
        {
            std::cout << "Performance: ACCEPTABLE - May need optimization for larger inputs\n";
        }
    }
    
    return 0;
}
