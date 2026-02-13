// Test hybrid Co3Ne + Poisson reconstruction
#include <GTE/Mathematics/HybridReconstruction.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cstdint>

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
    
    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    // Write faces (OBJ indices start at 1)
    for (auto const& t : triangles)
    {
        file << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    }
    
    std::cout << "Saved mesh to " << filename << ": " 
              << vertices.size() << " vertices, "
              << triangles.size() << " triangles" << std::endl;
    
    return true;
}

int main(int argc, char* argv[])
{
    std::string inputFile = "r768_1000.xyz";
    if (argc > 1)
    {
        inputFile = argv[1];
    }
    
    // Load input points
    std::vector<Vector3<double>> points;
    if (!LoadXYZ(inputFile, points))
    {
        return 1;
    }
    
    // Test 1: Poisson only (guaranteed single component)
    {
        std::cout << "\n=== Test 1: Poisson Only ===" << std::endl;
        
        HybridReconstruction<double>::Parameters params;
        params.mergeStrategy = HybridReconstruction<double>::Parameters::MergeStrategy::PoissonOnly;
        params.poissonParams.depth = 8;
        params.poissonParams.verbose = true;
        params.verbose = true;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        if (HybridReconstruction<double>::Reconstruct(points, vertices, triangles, params))
        {
            SaveOBJ("hybrid_poisson_only.obj", vertices, triangles);
        }
        else
        {
            std::cerr << "Poisson reconstruction failed" << std::endl;
        }
    }
    
    // Test 2: Co3Ne only (detailed patches)
    {
        std::cout << "\n=== Test 2: Co3Ne Only ===" << std::endl;
        
        HybridReconstruction<double>::Parameters params;
        params.mergeStrategy = HybridReconstruction<double>::Parameters::MergeStrategy::Co3NeOnly;
        params.co3neParams.relaxedManifoldExtraction = false;
        params.verbose = true;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        if (HybridReconstruction<double>::Reconstruct(points, vertices, triangles, params))
        {
            SaveOBJ("hybrid_co3ne_only.obj", vertices, triangles);
        }
        else
        {
            std::cerr << "Co3Ne reconstruction failed" << std::endl;
        }
    }
    
    // Test 3: Quality-based selection
    {
        std::cout << "\n=== Test 3: Quality-Based Selection ===" << std::endl;
        
        HybridReconstruction<double>::Parameters params;
        params.mergeStrategy = HybridReconstruction<double>::Parameters::MergeStrategy::QualityBased;
        params.poissonParams.depth = 8;
        params.co3neParams.relaxedManifoldExtraction = false;
        params.verbose = true;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        if (HybridReconstruction<double>::Reconstruct(points, vertices, triangles, params))
        {
            SaveOBJ("hybrid_quality_based.obj", vertices, triangles);
        }
        else
        {
            std::cerr << "Quality-based reconstruction failed" << std::endl;
        }
    }
    
    // Test 4: Co3Ne with Poisson gaps
    {
        std::cout << "\n=== Test 4: Co3Ne with Poisson Gap Filling ===" << std::endl;
        
        HybridReconstruction<double>::Parameters params;
        params.mergeStrategy = HybridReconstruction<double>::Parameters::MergeStrategy::Co3NeWithPoissonGaps;
        params.poissonParams.depth = 8;
        params.co3neParams.relaxedManifoldExtraction = false;
        params.verbose = true;
        
        std::vector<Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        if (HybridReconstruction<double>::Reconstruct(points, vertices, triangles, params))
        {
            SaveOBJ("hybrid_co3ne_poisson_gaps.obj", vertices, triangles);
        }
        else
        {
            std::cerr << "Co3Ne with Poisson gaps reconstruction failed" << std::endl;
        }
    }
    
    std::cout << "\n=== Hybrid Reconstruction Tests Complete ===" << std::endl;
    std::cout << "\nCompare outputs:" << std::endl;
    std::cout << "  1. hybrid_poisson_only.obj        - Single component, smooth" << std::endl;
    std::cout << "  2. hybrid_co3ne_only.obj          - Multiple components, detailed" << std::endl;
    std::cout << "  3. hybrid_quality_based.obj       - Smart quality selection" << std::endl;
    std::cout << "  4. hybrid_co3ne_poisson_gaps.obj  - Co3Ne + Poisson gap filling" << std::endl;
    
    return 0;
}
