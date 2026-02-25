// Comprehensive Manifold Closure Analysis Tool
// Analyzes current state of manifold closure approach and identifies all structural issues

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace gte;

// Topology analysis structure
struct TopologyAnalysis
{
    int32_t numVertices = 0;
    int32_t numTriangles = 0;
    int32_t numBoundaryEdges = 0;
    int32_t numNonManifoldEdges = 0;
    int32_t numManifoldEdges = 0;
    int32_t numComponents = 0;
    int32_t largestComponentSize = 0;
    int32_t numClosedComponents = 0;
    int32_t numOpenComponents = 0;
    
    std::map<int32_t, int32_t> edgeTriangleCount;  // edge -> num triangles
    std::vector<std::pair<int32_t, int32_t>> nonManifoldEdges;
    std::vector<std::pair<int32_t, int32_t>> boundaryEdges;
    std::vector<int32_t> componentSizes;
    std::vector<int32_t> componentBoundaryEdges;
};

// Analyze mesh topology in detail
TopologyAnalysis AnalyzeTopology(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles,
    bool verbose = false)
{
    TopologyAnalysis analysis;
    analysis.numVertices = static_cast<int32_t>(vertices.size());
    analysis.numTriangles = static_cast<int32_t>(triangles.size());
    
    // Build edge to triangle map
    std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> edgeToTriangles;
    
    for (size_t i = 0; i < triangles.size(); ++i)
    {
        auto const& tri = triangles[i];
        for (int j = 0; j < 3; ++j)
        {
            int32_t v0 = tri[j];
            int32_t v1 = tri[(j + 1) % 3];
            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
            edgeToTriangles[edge].push_back(static_cast<int32_t>(i));
        }
    }
    
    // Classify edges
    for (auto const& et : edgeToTriangles)
    {
        int32_t count = static_cast<int32_t>(et.second.size());
        analysis.edgeTriangleCount[count]++;
        
        if (count == 1)
        {
            analysis.numBoundaryEdges++;
            analysis.boundaryEdges.push_back(et.first);
        }
        else if (count == 2)
        {
            analysis.numManifoldEdges++;
        }
        else  // count >= 3
        {
            analysis.numNonManifoldEdges++;
            analysis.nonManifoldEdges.push_back(et.first);
        }
    }
    
    // Component analysis via DFS
    std::vector<bool> visited(triangles.size(), false);
    std::map<std::pair<int32_t, int32_t>, std::set<int32_t>> edgeToTris;
    
    for (size_t i = 0; i < triangles.size(); ++i)
    {
        auto const& tri = triangles[i];
        for (int j = 0; j < 3; ++j)
        {
            int32_t v0 = tri[j];
            int32_t v1 = tri[(j + 1) % 3];
            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
            edgeToTris[edge].insert(static_cast<int32_t>(i));
        }
    }
    
    for (size_t seedTri = 0; seedTri < triangles.size(); ++seedTri)
    {
        if (visited[seedTri]) continue;
        
        // BFS to find connected component
        std::vector<int32_t> component;
        std::vector<int32_t> queue;
        queue.push_back(static_cast<int32_t>(seedTri));
        visited[seedTri] = true;
        
        while (!queue.empty())
        {
            int32_t current = queue.back();
            queue.pop_back();
            component.push_back(current);
            
            // Find neighbors through edges
            auto const& tri = triangles[current];
            for (int j = 0; j < 3; ++j)
            {
                int32_t v0 = tri[j];
                int32_t v1 = tri[(j + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                
                for (int32_t neighborTri : edgeToTris[edge])
                {
                    if (!visited[neighborTri])
                    {
                        visited[neighborTri] = true;
                        queue.push_back(neighborTri);
                    }
                }
            }
        }
        
        analysis.componentSizes.push_back(static_cast<int32_t>(component.size()));
        
        // Check if component is closed (no boundary edges)
        std::set<std::pair<int32_t, int32_t>> componentEdges;
        int32_t componentBoundary = 0;
        
        for (int32_t triIdx : component)
        {
            auto const& tri = triangles[triIdx];
            for (int j = 0; j < 3; ++j)
            {
                int32_t v0 = tri[j];
                int32_t v1 = tri[(j + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                if (edgeToTriangles[edge].size() == 1)
                {
                    componentBoundary++;
                }
            }
        }
        
        analysis.componentBoundaryEdges.push_back(componentBoundary);
        
        if (componentBoundary == 0)
        {
            analysis.numClosedComponents++;
        }
        else
        {
            analysis.numOpenComponents++;
        }
    }
    
    analysis.numComponents = static_cast<int32_t>(analysis.componentSizes.size());
    if (!analysis.componentSizes.empty())
    {
        analysis.largestComponentSize = *std::max_element(
            analysis.componentSizes.begin(), analysis.componentSizes.end());
    }
    
    if (verbose)
    {
        std::cout << "\n=== TOPOLOGY ANALYSIS ===" << std::endl;
        std::cout << "Vertices: " << analysis.numVertices << std::endl;
        std::cout << "Triangles: " << analysis.numTriangles << std::endl;
        std::cout << "\nEDGE CLASSIFICATION:" << std::endl;
        std::cout << "  Boundary edges (1 tri): " << analysis.numBoundaryEdges << std::endl;
        std::cout << "  Manifold edges (2 tris): " << analysis.numManifoldEdges << std::endl;
        std::cout << "  Non-manifold edges (3+ tris): " << analysis.numNonManifoldEdges << std::endl;
        
        if (!analysis.edgeTriangleCount.empty())
        {
            std::cout << "\nEDGE STATISTICS:" << std::endl;
            for (auto const& etc : analysis.edgeTriangleCount)
            {
                std::cout << "  " << etc.second << " edges with " << etc.first << " triangle(s)" << std::endl;
            }
        }
        
        std::cout << "\nCOMPONENT ANALYSIS:" << std::endl;
        std::cout << "  Total components: " << analysis.numComponents << std::endl;
        std::cout << "  Closed components: " << analysis.numClosedComponents << std::endl;
        std::cout << "  Open components: " << analysis.numOpenComponents << std::endl;
        std::cout << "  Largest component: " << analysis.largestComponentSize << " triangles" << std::endl;
        
        std::cout << "\nCOMPONENT SIZES:" << std::endl;
        for (size_t i = 0; i < analysis.componentSizes.size(); ++i)
        {
            std::cout << "  Component " << i << ": " << analysis.componentSizes[i] 
                     << " triangles, " << analysis.componentBoundaryEdges[i] 
                     << " boundary edges";
            if (analysis.componentBoundaryEdges[i] == 0)
            {
                std::cout << " (CLOSED)";
            }
            std::cout << std::endl;
        }
        
        std::cout << "\nMANIFOLD STATUS:" << std::endl;
        if (analysis.numBoundaryEdges == 0 && analysis.numNonManifoldEdges == 0 && analysis.numComponents == 1)
        {
            std::cout << "  ✓ CLOSED MANIFOLD!" << std::endl;
        }
        else
        {
            std::cout << "  ✗ NOT A CLOSED MANIFOLD" << std::endl;
            if (analysis.numBoundaryEdges > 0)
            {
                std::cout << "    - Has " << analysis.numBoundaryEdges << " boundary edges (should be 0)" << std::endl;
            }
            if (analysis.numNonManifoldEdges > 0)
            {
                std::cout << "    - Has " << analysis.numNonManifoldEdges << " non-manifold edges (should be 0)" << std::endl;
            }
            if (analysis.numComponents > 1)
            {
                std::cout << "    - Has " << analysis.numComponents << " components (should be 1)" << std::endl;
            }
        }
    }
    
    return analysis;
}

// Write OBJ file
void WriteOBJ(std::string const& filename,
              std::vector<Vector3<double>> const& vertices,
              std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream ofs(filename);
    if (!ofs)
    {
        std::cerr << "Failed to open " << filename << std::endl;
        return;
    }
    
    for (auto const& v : vertices)
    {
        ofs << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    for (auto const& t : triangles)
    {
        ofs << "f " << (t[0] + 1) << " " << (t[1] + 1) << " " << (t[2] + 1) << "\n";
    }
    
    std::cout << "Wrote " << filename << std::endl;
}

int main(int argc, char* argv[])
{
    std::string inputFile = "tests/data/r768_1000.xyz";
    if (argc > 1)
    {
        inputFile = argv[1];
    }
    
    std::cout << "==================================================================" << std::endl;
    std::cout << "   COMPREHENSIVE MANIFOLD CLOSURE ANALYSIS" << std::endl;
    std::cout << "==================================================================" << std::endl;
    std::cout << "\nInput: " << inputFile << std::endl;
    
    // Load input data
    std::ifstream ifs(inputFile);
    if (!ifs)
    {
        std::cerr << "Failed to open " << inputFile << std::endl;
        return 1;
    }
    
    std::vector<Vector3<double>> points;
    std::vector<Vector3<double>> normals;
    
    double x, y, z, nx, ny, nz;
    while (ifs >> x >> y >> z >> nx >> ny >> nz)
    {
        points.push_back({x, y, z});
        normals.push_back({nx, ny, nz});
    }
    
    std::cout << "Loaded " << points.size() << " points with normals" << std::endl;
    
    // ==================================================================
    // STAGE 1: Co3Ne Reconstruction
    // ==================================================================
    std::cout << "\n=================================================================="<< std::endl;
    std::cout << "STAGE 1: Co3Ne Reconstruction (Baseline)" << std::endl;
    std::cout << "==================================================================" << std::endl;
    
    Co3Ne<double> co3ne;
    Co3Ne<double>::Parameters co3neParams;
    // Use default parameters
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool co3neSuccess = co3ne.Reconstruct(points, vertices, triangles, co3neParams);
    
    if (!co3neSuccess || triangles.empty())
    {
        std::cerr << "Co3Ne reconstruction failed" << std::endl;
        return 1;
    }
    
    std::cout << "\nCo3Ne Output:" << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    std::cout << "  Triangles: " << triangles.size() << std::endl;
    
    auto co3neAnalysis = AnalyzeTopology(vertices, triangles, true);
    WriteOBJ("test_output_co3ne.obj", vertices, triangles);
    
    // ==================================================================
    // STAGE 2: Ball Pivot Hole Filling
    // ==================================================================
    std::cout << "\n==================================================================" << std::endl;
    std::cout << "STAGE 2: Ball Pivot Hole Filling" << std::endl;
    std::cout << "==================================================================" << std::endl;
    
    BallPivotMeshHoleFiller<double>::Parameters fillParams;
    fillParams.verbose = true;
    fillParams.allowNonManifoldEdges = false;  // Conservative
    fillParams.rejectSmallComponents = true;
    fillParams.smallComponentThreshold = 20;
    fillParams.radiusScales = {1.5, 2.0, 3.0, 5.0, 7.5, 10.0, 15.0, 20.0};
    
    auto verticesCopy = vertices;
    auto trianglesCopy = triangles;
    
    bool fillSuccess = BallPivotMeshHoleFiller<double>::FillAllHolesWithComponentBridging(
        verticesCopy, trianglesCopy, fillParams);
    
    std::cout << "\nBall Pivot Output:" << std::endl;
    std::cout << "  Success: " << (fillSuccess ? "YES" : "NO") << std::endl;
    std::cout << "  Vertices: " << verticesCopy.size() << std::endl;
    std::cout << "  Triangles: " << trianglesCopy.size() 
             << " (added " << (trianglesCopy.size() - triangles.size()) << ")" << std::endl;
    
    auto fillAnalysis = AnalyzeTopology(verticesCopy, trianglesCopy, true);
    WriteOBJ("test_output_filled.obj", verticesCopy, trianglesCopy);
    
    // ==================================================================
    // STAGE 3: Comparison and Problem Identification
    // ==================================================================
    std::cout << "\n==================================================================" << std::endl;
    std::cout << "STAGE 3: What's Working and What's Not" << std::endl;
    std::cout << "==================================================================" << std::endl;
    
    std::cout << "\n✓ WHAT'S WORKING:" << std::endl;
    
    if (fillAnalysis.numBoundaryEdges < co3neAnalysis.numBoundaryEdges)
    {
        double reduction = 100.0 * (co3neAnalysis.numBoundaryEdges - fillAnalysis.numBoundaryEdges) / 
                          co3neAnalysis.numBoundaryEdges;
        std::cout << "  ✓ Boundary edge reduction: " << reduction << "% "
                 << "(" << co3neAnalysis.numBoundaryEdges << " → " << fillAnalysis.numBoundaryEdges << ")" << std::endl;
    }
    
    if (fillAnalysis.numComponents < co3neAnalysis.numComponents)
    {
        std::cout << "  ✓ Component bridging working: " << co3neAnalysis.numComponents 
                 << " → " << fillAnalysis.numComponents << " components" << std::endl;
    }
    
    if (fillAnalysis.numNonManifoldEdges <= co3neAnalysis.numNonManifoldEdges)
    {
        std::cout << "  ✓ Non-manifold edges maintained/reduced: " 
                 << co3neAnalysis.numNonManifoldEdges << " → " << fillAnalysis.numNonManifoldEdges << std::endl;
    }
    
    std::cout << "\n✗ REMAINING PROBLEMS:" << std::endl;
    
    if (fillAnalysis.numBoundaryEdges > 0)
    {
        std::cout << "  ✗ PROBLEM 1: " << fillAnalysis.numBoundaryEdges 
                 << " boundary edges remain (need 0 for closed manifold)" << std::endl;
        std::cout << "    - These are holes that couldn't be filled" << std::endl;
        std::cout << "    - Likely due to validation preventing overlapping triangles" << std::endl;
        std::cout << "    - OR complex geometry that ear clipping can't handle" << std::endl;
    }
    
    if (fillAnalysis.numNonManifoldEdges > 0)
    {
        std::cout << "  ✗ PROBLEM 2: " << fillAnalysis.numNonManifoldEdges 
                 << " non-manifold edges (3+ triangles sharing edge)" << std::endl;
        std::cout << "    - These create ambiguous inside/outside definition" << std::endl;
        std::cout << "    - Need local remeshing to fix" << std::endl;
    }
    
    if (fillAnalysis.numComponents > 1)
    {
        std::cout << "  ✗ PROBLEM 3: " << fillAnalysis.numComponents 
                 << " disconnected components (need 1)" << std::endl;
        std::cout << "    - Component gaps too large to bridge" << std::endl;
        std::cout << "    - OR closed components that can't/shouldn't be connected" << std::endl;
        
        for (size_t i = 0; i < fillAnalysis.componentSizes.size(); ++i)
        {
            if (fillAnalysis.componentBoundaryEdges[i] == 0)
            {
                std::cout << "    - Component " << i << " is CLOSED (" 
                         << fillAnalysis.componentSizes[i] << " triangles)" << std::endl;
            }
        }
    }
    
    // ==================================================================
    // STAGE 4: Recommendations
    // ==================================================================
    std::cout << "\n==================================================================" << std::endl;
    std::cout << "STAGE 4: Recommended Fixes" << std::endl;
    std::cout << "==================================================================" << std::endl;
    
    std::cout << "\nFor BOUNDARY EDGES:" << std::endl;
    std::cout << "  1. Try relaxed validation (allowNonManifoldEdges=true)" << std::endl;
    std::cout << "  2. Implement post-processing to fix created non-manifold edges" << std::endl;
    std::cout << "  3. OR: Pre-process to remove conflicting triangles before filling" << std::endl;
    
    if (fillAnalysis.numNonManifoldEdges > 0)
    {
        std::cout << "\nFor NON-MANIFOLD EDGES:" << std::endl;
        std::cout << "  1. Identify non-manifold edges and surrounding triangles" << std::endl;
        std::cout << "  2. Remove triangles creating non-manifold configuration" << std::endl;
        std::cout << "  3. Retriangulate the region locally" << std::endl;
        std::cout << "  4. Validate result is manifold" << std::endl;
    }
    
    if (fillAnalysis.numComponents > 1)
    {
        std::cout << "\nFor MULTIPLE COMPONENTS:" << std::endl;
        if (fillAnalysis.numClosedComponents > 0)
        {
            std::cout << "  1. Remove small closed components (already implemented)" << std::endl;
            std::cout << "  2. For large closed components, verify they should be separate" << std::endl;
        }
        std::cout << "  3. Increase gap threshold scales (try up to 50x-100x)" << std::endl;
        std::cout << "  4. Multiple bridging iterations until single component" << std::endl;
    }
    
    // ==================================================================
    // FINAL SUMMARY
    // ==================================================================
    std::cout << "\n==================================================================" << std::endl;
    std::cout << "FINAL SUMMARY" << std::endl;
    std::cout << "==================================================================" << std::endl;
    
    bool isManifold = (fillAnalysis.numBoundaryEdges == 0 && 
                       fillAnalysis.numNonManifoldEdges == 0 && 
                       fillAnalysis.numComponents == 1);
    
    if (isManifold)
    {
        std::cout << "\n✓✓✓ SUCCESS: Achieved closed manifold! ✓✓✓" << std::endl;
    }
    else
    {
        std::cout << "\n✗✗✗ NOT YET MANIFOLD - Issues remain ✗✗✗" << std::endl;
        std::cout << "\nRemaining work:" << std::endl;
        if (fillAnalysis.numBoundaryEdges > 0)
        {
            std::cout << "  - Fix " << fillAnalysis.numBoundaryEdges << " boundary edges" << std::endl;
        }
        if (fillAnalysis.numNonManifoldEdges > 0)
        {
            std::cout << "  - Fix " << fillAnalysis.numNonManifoldEdges << " non-manifold edges" << std::endl;
        }
        if (fillAnalysis.numComponents > 1)
        {
            std::cout << "  - Merge " << fillAnalysis.numComponents << " components into 1" << std::endl;
        }
    }
    
    std::cout << "\n==================================================================" << std::endl;
    
    return 0;
}
