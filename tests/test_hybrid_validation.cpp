// Comprehensive validation test for hybrid reconstruction
// Tests if methods produce different and manifold outputs

#include <GTE/Mathematics/HybridReconstruction.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <algorithm>

using namespace gte;

// Load XYZ file
bool LoadXYZ(std::string const& filename, std::vector<Vector3<double>>& points)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open: " << filename << std::endl;
        return false;
    }
    
    points.clear();
    double x, y, z;
    while (file >> x >> y >> z)
    {
        points.push_back({x, y, z});
    }
    
    std::cout << "Loaded " << points.size() << " points from " << filename << std::endl;
    return !points.empty();
}

// Save mesh to OBJ file
bool SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to create: " << filename << std::endl;
        return false;
    }
    
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    for (auto const& t : triangles)
    {
        file << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    }
    
    return true;
}

// Validate manifold properties
struct ManifoldStats
{
    int numTriangles;
    int numVertices;
    int numBoundaryEdges;
    int numNonManifoldEdges;
    int numConnectedComponents;
    bool isManifold;
    bool isClosed;
    
    void Print(std::string const& name) const
    {
        std::cout << "\n=== " << name << " ===" << std::endl;
        std::cout << "  Vertices: " << numVertices << std::endl;
        std::cout << "  Triangles: " << numTriangles << std::endl;
        std::cout << "  Boundary edges: " << numBoundaryEdges << std::endl;
        std::cout << "  Non-manifold edges: " << numNonManifoldEdges << std::endl;
        std::cout << "  Connected components: " << numConnectedComponents << std::endl;
        std::cout << "  Manifold: " << (isManifold ? "YES" : "NO") << std::endl;
        std::cout << "  Closed: " << (isClosed ? "YES" : "NO") << std::endl;
    }
};

ManifoldStats ValidateMesh(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    ManifoldStats stats;
    stats.numVertices = static_cast<int>(vertices.size());
    stats.numTriangles = static_cast<int>(triangles.size());
    
    // Count edge occurrences
    std::map<std::pair<int32_t, int32_t>, int> edgeCount;
    
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int32_t v0 = tri[i];
            int32_t v1 = tri[(i + 1) % 3];
            if (v0 > v1) std::swap(v0, v1);
            edgeCount[{v0, v1}]++;
        }
    }
    
    // Classify edges
    stats.numBoundaryEdges = 0;
    stats.numNonManifoldEdges = 0;
    
    for (auto const& [edge, count] : edgeCount)
    {
        if (count == 1)
        {
            stats.numBoundaryEdges++;
        }
        else if (count > 2)
        {
            stats.numNonManifoldEdges++;
        }
    }
    
    stats.isManifold = (stats.numNonManifoldEdges == 0);
    stats.isClosed = (stats.numBoundaryEdges == 0);
    
    // Count connected components via flood fill
    std::set<int32_t> unvisited;
    for (auto const& tri : triangles)
    {
        unvisited.insert(tri[0]);
        unvisited.insert(tri[1]);
        unvisited.insert(tri[2]);
    }
    
    // Build adjacency
    std::map<int32_t, std::set<int32_t>> adjacency;
    for (auto const& tri : triangles)
    {
        adjacency[tri[0]].insert(tri[1]);
        adjacency[tri[0]].insert(tri[2]);
        adjacency[tri[1]].insert(tri[0]);
        adjacency[tri[1]].insert(tri[2]);
        adjacency[tri[2]].insert(tri[0]);
        adjacency[tri[2]].insert(tri[1]);
    }
    
    stats.numConnectedComponents = 0;
    
    while (!unvisited.empty())
    {
        // Start new component
        stats.numConnectedComponents++;
        
        std::vector<int32_t> queue;
        int32_t start = *unvisited.begin();
        queue.push_back(start);
        unvisited.erase(start);
        
        while (!queue.empty())
        {
            int32_t current = queue.back();
            queue.pop_back();
            
            for (int32_t neighbor : adjacency[current])
            {
                if (unvisited.count(neighbor))
                {
                    queue.push_back(neighbor);
                    unvisited.erase(neighbor);
                }
            }
        }
    }
    
    return stats;
}

int main(int argc, char* argv[])
{
    std::string inputFile = "r768_1000.xyz";
    if (argc > 1)
    {
        inputFile = argv[1];
    }
    
    std::vector<Vector3<double>> points;
    if (!LoadXYZ(inputFile, points))
    {
        return 1;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "HYBRID RECONSTRUCTION VALIDATION TEST" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Test all 4 strategies
    std::vector<std::string> strategyNames = {
        "PoissonOnly",
        "Co3NeOnly",
        "QualityBased",
        "Co3NeWithPoissonGaps"
    };
    
    std::vector<HybridReconstruction<double>::Parameters::MergeStrategy> strategies = {
        HybridReconstruction<double>::Parameters::MergeStrategy::PoissonOnly,
        HybridReconstruction<double>::Parameters::MergeStrategy::Co3NeOnly,
        HybridReconstruction<double>::Parameters::MergeStrategy::QualityBased,
        HybridReconstruction<double>::Parameters::MergeStrategy::Co3NeWithPoissonGaps
    };
    
    std::vector<ManifoldStats> results;
    
    for (size_t i = 0; i < strategies.size(); ++i)
    {
        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "Testing: " << strategyNames[i] << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        
        HybridReconstruction<double>::Parameters params;
        params.mergeStrategy = strategies[i];
        params.poissonParams.depth = 7;  // Faster for testing
        params.poissonParams.verbose = false;
        params.verbose = true;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        bool success = HybridReconstruction<double>::Reconstruct(points, vertices, triangles, params);
        
        if (success)
        {
            std::string outputFile = "validation_" + strategyNames[i] + ".obj";
            SaveOBJ(outputFile, vertices, triangles);
            
            ManifoldStats stats = ValidateMesh(vertices, triangles);
            stats.Print(strategyNames[i]);
            results.push_back(stats);
        }
        else
        {
            std::cerr << "FAILED: " << strategyNames[i] << std::endl;
            ManifoldStats emptyStats = {0, 0, 0, 0, 0, false, false};
            results.push_back(emptyStats);
        }
    }
    
    // Compare results
    std::cout << "\n========================================" << std::endl;
    std::cout << "COMPARISON SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\nStrategy              | Vertices | Triangles | Components | Manifold | Closed" << std::endl;
    std::cout << "----------------------|----------|-----------|------------|----------|--------" << std::endl;
    
    for (size_t i = 0; i < strategyNames.size(); ++i)
    {
        auto const& stats = results[i];
        printf("%-20s  | %8d | %9d | %10d | %8s | %6s\n",
               strategyNames[i].c_str(),
               stats.numVertices,
               stats.numTriangles,
               stats.numConnectedComponents,
               stats.isManifold ? "YES" : "NO",
               stats.isClosed ? "YES" : "NO");
    }
    
    // Check if outputs are different
    std::cout << "\n========================================" << std::endl;
    std::cout << "DIFFERENCES CHECK" << std::endl;
    std::cout << "========================================" << std::endl;
    
    bool allDifferent = true;
    for (size_t i = 0; i < results.size(); ++i)
    {
        for (size_t j = i + 1; j < results.size(); ++j)
        {
            bool same = (results[i].numTriangles == results[j].numTriangles) &&
                       (results[i].numVertices == results[j].numVertices);
            
            if (same)
            {
                std::cout << "WARNING: " << strategyNames[i] << " and " << strategyNames[j] 
                         << " produced identical outputs!" << std::endl;
                allDifferent = false;
            }
        }
    }
    
    if (allDifferent)
    {
        std::cout << "✓ All strategies produced different outputs" << std::endl;
    }
    
    // Check specific requirements
    std::cout << "\n========================================" << std::endl;
    std::cout << "REQUIREMENTS CHECK" << std::endl;
    std::cout << "========================================" << std::endl;
    
    bool req1 = results[0].numConnectedComponents == 1;  // PoissonOnly should be 1 component
    bool req2 = results[1].numConnectedComponents > 1;   // Co3NeOnly should be multiple
    bool req3 = results[1].isManifold;                   // Co3Ne should be manifold
    bool req4 = results[3].numConnectedComponents <= results[1].numConnectedComponents; // Gaps should reduce components
    
    std::cout << (req1 ? "✓" : "✗") << " PoissonOnly produces single component: " 
              << results[0].numConnectedComponents << std::endl;
    std::cout << (req2 ? "✓" : "✗") << " Co3NeOnly produces multiple components: " 
              << results[1].numConnectedComponents << std::endl;
    std::cout << (req3 ? "✓" : "✗") << " Co3Ne maintains manifold property" << std::endl;
    std::cout << (req4 ? "✓" : "✗") << " Co3NeWithPoissonGaps reduces components from " 
              << results[1].numConnectedComponents << " to " 
              << results[3].numConnectedComponents << std::endl;
    
    bool allReqsMet = req1 && req2 && req3 && req4;
    
    std::cout << "\n========================================" << std::endl;
    if (allReqsMet && allDifferent)
    {
        std::cout << "✓ ALL TESTS PASSED!" << std::endl;
    }
    else
    {
        std::cout << "✗ SOME TESTS FAILED - Implementation needs work" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    return allReqsMet && allDifferent ? 0 : 1;
}
