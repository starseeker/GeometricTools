// Diagnostic test for Ball Pivot welding failures
//
// This test investigates why certain patch pairs fail to weld

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/BallPivotReconstruction.h>
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace gte;

// Helper to load XYZ point cloud
std::vector<Vector3<double>> LoadXYZ(std::string const& filename)
{
    std::vector<Vector3<double>> points;
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << "\n";
        return points;
    }
    
    double x, y, z;
    while (file >> x >> y >> z)
    {
        points.push_back({x, y, z});
    }
    
    return points;
}

// Analyze a specific patch
void AnalyzePatch(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles,
    std::vector<int32_t> const& triangleIndices,
    std::vector<std::pair<int32_t, int32_t>> const& boundaryEdges,
    int patchIndex)
{
    std::cout << "\n--- Patch " << patchIndex << " Analysis ---\n";
    std::cout << "  Triangles: " << triangleIndices.size() << "\n";
    std::cout << "  Boundary edges: " << boundaryEdges.size() << "\n";
    
    // Collect unique vertices
    std::set<int32_t> uniqueVerts;
    for (auto triIdx : triangleIndices)
    {
        auto const& tri = triangles[triIdx];
        uniqueVerts.insert(tri[0]);
        uniqueVerts.insert(tri[1]);
        uniqueVerts.insert(tri[2]);
    }
    std::cout << "  Unique vertices: " << uniqueVerts.size() << "\n";
    
    // Collect boundary vertices
    std::set<int32_t> boundaryVerts;
    for (auto const& edge : boundaryEdges)
    {
        boundaryVerts.insert(edge.first);
        boundaryVerts.insert(edge.second);
    }
    std::cout << "  Boundary vertices: " << boundaryVerts.size() << "\n";
    
    // Compute bounding box
    if (!uniqueVerts.empty())
    {
        Vector3<double> minPt = vertices[*uniqueVerts.begin()];
        Vector3<double> maxPt = minPt;
        
        for (auto vIdx : uniqueVerts)
        {
            auto const& v = vertices[vIdx];
            for (int i = 0; i < 3; ++i)
            {
                minPt[i] = std::min(minPt[i], v[i]);
                maxPt[i] = std::max(maxPt[i], v[i]);
            }
        }
        
        Vector3<double> size = maxPt - minPt;
        std::cout << "  Bounding box size: (" 
                  << size[0] << ", " << size[1] << ", " << size[2] << ")\n";
        std::cout << "  Diagonal: " << Length(size) << "\n";
    }
    
    // Compute average edge length
    if (!triangleIndices.empty())
    {
        double totalLen = 0;
        int edgeCount = 0;
        
        for (auto triIdx : triangleIndices)
        {
            auto const& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                Vector3<double> v0 = vertices[tri[i]];
                Vector3<double> v1 = vertices[tri[(i+1)%3]];
                totalLen += Length(v1 - v0);
                edgeCount++;
            }
        }
        
        if (edgeCount > 0)
        {
            std::cout << "  Average edge length: " << (totalLen / edgeCount) << "\n";
        }
    }
}

// Test diagnostic welding
void TestWeldingDiagnosis()
{
    std::cout << "\n=== Ball Pivot Welding Diagnostic Test ===\n";
    
    // Load point cloud
    std::vector<Vector3<double>> points = LoadXYZ("r768.xyz");
    
    if (points.empty())
    {
        std::cout << "Failed to load r768.xyz\n";
        return;
    }
    
    // Use 1000 points for consistency with previous tests
    if (points.size() > 1000)
    {
        points.resize(1000);
    }
    
    std::cout << "Loaded " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.relaxedManifoldExtraction = true;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::cout << "Running Co3Ne reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    if (!success || triangles.empty())
    {
        std::cout << "Co3Ne failed\n";
        return;
    }
    
    std::cout << "Co3Ne: " << triangles.size() << " triangles, " 
              << vertices.size() << " vertices\n";
    
    // Analyze topology and identify patches
    std::map<std::pair<int32_t, int32_t>, int> edgeCounts;
    
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int32_t v0 = tri[i];
            int32_t v1 = tri[(i + 1) % 3];
            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
            edgeCounts[edge]++;
        }
    }
    
    // Remove non-manifold edges
    std::set<size_t> toRemove;
    for (auto const& ec : edgeCounts)
    {
        if (ec.second > 2)
        {
            // Find and mark triangles for removal
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    if (edge == ec.first)
                    {
                        toRemove.insert(i);
                        break;
                    }
                }
            }
        }
    }
    
    // Remove marked triangles
    std::vector<size_t> sorted(toRemove.begin(), toRemove.end());
    std::sort(sorted.rbegin(), sorted.rend());
    for (auto idx : sorted)
    {
        if (idx < triangles.size())
        {
            triangles.erase(triangles.begin() + idx);
        }
    }
    
    // Rebuild edge counts
    edgeCounts.clear();
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int32_t v0 = tri[i];
            int32_t v1 = tri[(i + 1) % 3];
            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
            edgeCounts[edge]++;
        }
    }
    
    // Identify patches using BFS
    std::vector<bool> visited(triangles.size(), false);
    std::vector<std::vector<int32_t>> patches;
    
    for (size_t startIdx = 0; startIdx < triangles.size(); ++startIdx)
    {
        if (visited[startIdx]) continue;
        
        std::vector<int32_t> patch;
        std::vector<size_t> queue;
        queue.push_back(startIdx);
        visited[startIdx] = true;
        
        while (!queue.empty())
        {
            size_t triIdx = queue.back();
            queue.pop_back();
            patch.push_back(static_cast<int32_t>(triIdx));
            
            auto const& tri = triangles[triIdx];
            
            // Find adjacent triangles
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                
                if (edgeCounts[edge] == 2)  // Interior edge
                {
                    // Find the other triangle
                    for (size_t j = 0; j < triangles.size(); ++j)
                    {
                        if (visited[j] || j == triIdx) continue;
                        
                        auto const& tri2 = triangles[j];
                        for (int k = 0; k < 3; ++k)
                        {
                            int32_t u0 = tri2[k];
                            int32_t u1 = tri2[(k + 1) % 3];
                            auto edge2 = std::make_pair(std::min(u0, u1), std::max(u0, u1));
                            
                            if (edge == edge2)
                            {
                                visited[j] = true;
                                queue.push_back(j);
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        patches.push_back(patch);
    }
    
    std::cout << "\nFound " << patches.size() << " patches\n";
    
    // Analyze each patch and find boundary edges
    std::vector<std::vector<std::pair<int32_t, int32_t>>> patchBoundaries(patches.size());
    
    for (size_t pIdx = 0; pIdx < patches.size(); ++pIdx)
    {
        auto const& patch = patches[pIdx];
        
        // Find boundary edges
        std::map<std::pair<int32_t, int32_t>, int> patchEdgeCounts;
        
        for (auto triIdx : patch)
        {
            auto const& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                patchEdgeCounts[edge]++;
            }
        }
        
        for (auto const& ec : patchEdgeCounts)
        {
            if (ec.second == 1)
            {
                patchBoundaries[pIdx].push_back(ec.first);
            }
        }
        
        AnalyzePatch(vertices, triangles, patch, patchBoundaries[pIdx], static_cast<int>(pIdx));
    }
    
    // Now analyze the specific failing pairs
    std::cout << "\n=== Analyzing Known Failing Pairs ===\n";
    
    std::vector<std::pair<int, int>> failingPairs = {{2, 33}, {9, 10}, {20, 30}};
    
    for (auto const& pairIndices : failingPairs)
    {
        if (pairIndices.first >= static_cast<int>(patches.size()) || 
            pairIndices.second >= static_cast<int>(patches.size()))
        {
            std::cout << "\nPair (" << pairIndices.first << ", " << pairIndices.second 
                      << ") - OUT OF RANGE\n";
            continue;
        }
        
        std::cout << "\n=== Pair (" << pairIndices.first << ", " << pairIndices.second << ") ===\n";
        
        // Extract boundary points
        std::set<int32_t> boundaryVerts1, boundaryVerts2;
        
        for (auto const& edge : patchBoundaries[pairIndices.first])
        {
            boundaryVerts1.insert(edge.first);
            boundaryVerts1.insert(edge.second);
        }
        
        for (auto const& edge : patchBoundaries[pairIndices.second])
        {
            boundaryVerts2.insert(edge.first);
            boundaryVerts2.insert(edge.second);
        }
        
        std::cout << "Patch " << pairIndices.first << " boundary vertices: " 
                  << boundaryVerts1.size() << "\n";
        std::cout << "Patch " << pairIndices.second << " boundary vertices: " 
                  << boundaryVerts2.size() << "\n";
        
        // Find shared vertices
        std::set<int32_t> shared;
        for (auto v : boundaryVerts1)
        {
            if (boundaryVerts2.find(v) != boundaryVerts2.end())
            {
                shared.insert(v);
            }
        }
        
        std::cout << "Shared boundary vertices: " << shared.size() << "\n";
        
        // Compute min distance between boundary vertices
        double minDist = std::numeric_limits<double>::max();
        for (auto v1 : boundaryVerts1)
        {
            for (auto v2 : boundaryVerts2)
            {
                if (v1 == v2) continue;
                double dist = Length(vertices[v2] - vertices[v1]);
                minDist = std::min(minDist, dist);
            }
        }
        
        std::cout << "Min distance between boundaries: " << minDist << "\n";
        
        // Compute average spacing within each boundary
        auto computeAvgSpacing = [&](std::set<int32_t> const& verts) -> double {
            if (verts.size() < 2) return 0.0;
            
            std::vector<int32_t> vertList(verts.begin(), verts.end());
            double totalDist = 0;
            int count = 0;
            
            for (size_t i = 0; i < vertList.size(); ++i)
            {
                double minLocalDist = std::numeric_limits<double>::max();
                for (size_t j = 0; j < vertList.size(); ++j)
                {
                    if (i == j) continue;
                    double dist = Length(vertices[vertList[j]] - vertices[vertList[i]]);
                    if (dist > 0) minLocalDist = std::min(minLocalDist, dist);
                }
                if (minLocalDist < std::numeric_limits<double>::max())
                {
                    totalDist += minLocalDist;
                    count++;
                }
            }
            
            return count > 0 ? totalDist / count : 0.0;
        };
        
        double avgSpacing1 = computeAvgSpacing(boundaryVerts1);
        double avgSpacing2 = computeAvgSpacing(boundaryVerts2);
        
        std::cout << "Patch " << pairIndices.first << " avg boundary spacing: " 
                  << avgSpacing1 << "\n";
        std::cout << "Patch " << pairIndices.second << " avg boundary spacing: " 
                  << avgSpacing2 << "\n";
        
        // Combined boundary points
        std::set<int32_t> combinedBoundary = boundaryVerts1;
        combinedBoundary.insert(boundaryVerts2.begin(), boundaryVerts2.end());
        
        double combinedSpacing = computeAvgSpacing(combinedBoundary);
        std::cout << "Combined boundary avg spacing: " << combinedSpacing << "\n";
        
        // Test Ball Pivot directly on this pair
        std::vector<Vector3<double>> bpPoints;
        std::vector<Vector3<double>> bpNormals;
        
        for (auto v : combinedBoundary)
        {
            bpPoints.push_back(vertices[v]);
            
            // Estimate normal (simple approach - just use Z-up for now)
            bpNormals.push_back(Vector3<double>::Unit(2));
        }
        
        std::cout << "Testing Ball Pivot with " << bpPoints.size() << " points\n";
        
        // Try multiple radii
        std::vector<double> testRadii = {
            combinedSpacing * 0.5,
            combinedSpacing * 1.0,
            combinedSpacing * 1.5,
            combinedSpacing * 2.0,
            combinedSpacing * 3.0
        };
        
        for (double radius : testRadii)
        {
            BallPivotReconstruction<double>::Parameters bpParams;
            bpParams.radii = {radius};
            bpParams.verbose = false;
            
            std::vector<Vector3<double>> outVerts;
            std::vector<std::array<int32_t, 3>> outTris;
            
            bool bpSuccess = BallPivotReconstruction<double>::Reconstruct(
                bpPoints, bpNormals, outVerts, outTris, bpParams);
            
            std::cout << "  Radius " << radius << ": " 
                      << (bpSuccess ? "SUCCESS" : "FAILED")
                      << " - " << outTris.size() << " triangles\n";
        }
    }
}

int main()
{
    std::cout << "Ball Pivot Welding Failure Diagnosis\n";
    std::cout << "====================================\n";
    
    TestWeldingDiagnosis();
    
    std::cout << "\n=== Diagnostic Complete ===\n";
    
    return 0;
}
