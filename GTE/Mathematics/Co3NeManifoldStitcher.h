// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Co3Ne Manifold Patch Stitcher
//
// This class provides functionality to stitch together Co3Ne's manifold patches
// into a fully manifold mesh using:
// 1. Hole filling for gaps between patches with identifiable boundary loops
// 2. Ball pivoting for welding floating patches together
// 3. UV parameterization + 2D remeshing for complex patch merging
//
// The goal is to take Co3Ne's output (which may contain multiple disconnected
// manifold patches or patches with non-manifold edges) and produce a single,
// fully manifold mesh.

#ifndef GTE_MATHEMATICS_CO3NE_MANIFOLD_STITCHER_H
#define GTE_MATHEMATICS_CO3NE_MANIFOLD_STITCHER_H

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/ETManifoldMesh.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <GTE/Mathematics/BallPivotReconstruction.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <memory>

namespace gte
{
    template <typename Real>
    class Co3NeManifoldStitcher
    {
    public:
        // Represents a connected patch of triangles
        struct Patch
        {
            std::vector<int32_t> triangleIndices;  // Indices into the full triangle array
            std::set<int32_t> vertexIndices;       // Unique vertices in this patch
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;  // Boundary edges
            bool isManifold;                        // Whether patch is locally manifold
            bool isClosed;                          // Whether patch has no boundary edges
            Real area;                              // Total patch area
            Vector3<Real> centroid;                 // Patch centroid
            
            Patch() 
                : isManifold(false)
                , isClosed(false)
                , area(static_cast<Real>(0)) 
                , centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)}
            {}
        };
        
        // Patch pair with gap analysis
        struct PatchPair
        {
            size_t patch1Index;
            size_t patch2Index;
            Real minDistance;               // Minimum distance between patches
            Real maxDistance;               // Maximum distance between patches
            size_t closeBoundaryVertices;   // Number of boundary vertices within threshold
            
            PatchPair()
                : patch1Index(0)
                , patch2Index(0)
                , minDistance(std::numeric_limits<Real>::max())
                , maxDistance(static_cast<Real>(0))
                , closeBoundaryVertices(0)
            {}
        };
        
        // Edge classification
        enum class EdgeType
        {
            Interior,      // Edge shared by exactly 2 triangles (manifold)
            Boundary,      // Edge with exactly 1 triangle (manifold boundary)
            NonManifold    // Edge shared by 3+ triangles (non-manifold)
        };
        
        // Parameters for stitching operations
        struct Parameters
        {
            // Hole filling parameters
            bool enableHoleFilling;
            Real maxHoleArea;
            size_t maxHoleEdges;
            typename MeshHoleFilling<Real>::TriangulationMethod holeFillingMethod;
            
            // Ball pivot parameters
            bool enableBallPivot;
            std::vector<Real> ballRadii;  // Multiple radii to try
            size_t minNeighborsForPivot;
            
            // UV parameterization parameters
            bool enableUVMerging;
            Real uvMergingThreshold;
            
            // General parameters
            bool removeNonManifoldEdges;  // Remove triangles causing non-manifold edges
            bool verbose;
            
            Parameters()
                : enableHoleFilling(true)
                , maxHoleArea(static_cast<Real>(0))  // No limit
                , maxHoleEdges(100)
                , holeFillingMethod(MeshHoleFilling<Real>::TriangulationMethod::CDT)
                , enableBallPivot(false)  // Disabled by default (not yet implemented)
                , minNeighborsForPivot(10)
                , enableUVMerging(false)  // Disabled by default (not yet implemented)
                , uvMergingThreshold(static_cast<Real>(0.1))
                , removeNonManifoldEdges(true)
                , verbose(false)
            {
                // Default ball radii - multiple scales for adaptive stitching
                ballRadii = { 
                    static_cast<Real>(0.01), 
                    static_cast<Real>(0.05), 
                    static_cast<Real>(0.1) 
                };
            }
        };
        
        // Main stitching function
        static bool StitchPatches(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters())
        {
            if (triangles.empty())
            {
                return true;  // Nothing to stitch
            }
            
            // Step 1: Analyze mesh topology
            std::map<std::pair<int32_t, int32_t>, EdgeType> edgeTypes;
            std::vector<std::pair<int32_t, int32_t>> nonManifoldEdges;
            AnalyzeEdgeTopology(triangles, edgeTypes, nonManifoldEdges);
            
            if (params.verbose)
            {
                size_t boundaryEdges = 0;
                size_t interiorEdges = 0;
                for (auto const& et : edgeTypes)
                {
                    if (et.second == EdgeType::Boundary) ++boundaryEdges;
                    if (et.second == EdgeType::Interior) ++interiorEdges;
                }
                
                std::cout << "Topology Analysis:\n";
                std::cout << "  Boundary edges: " << boundaryEdges << "\n";
                std::cout << "  Interior edges: " << interiorEdges << "\n";
                std::cout << "  Non-manifold edges: " << nonManifoldEdges.size() << "\n";
            }
            
            // Step 2: If enabled, remove non-manifold edges first
            if (params.removeNonManifoldEdges && !nonManifoldEdges.empty())
            {
                size_t removed = RemoveNonManifoldEdges(triangles, nonManifoldEdges);
                
                if (params.verbose)
                {
                    std::cout << "Removed " << removed << " triangles to fix non-manifold edges\n";
                }
                
                // Re-analyze after removal
                edgeTypes.clear();
                nonManifoldEdges.clear();
                AnalyzeEdgeTopology(triangles, edgeTypes, nonManifoldEdges);
            }
            
            // Step 3: Identify connected patches
            std::vector<Patch> patches;
            IdentifyPatches(vertices, triangles, edgeTypes, patches);
            
            if (params.verbose)
            {
                std::cout << "Found " << patches.size() << " connected patches\n";
                size_t closedPatches = 0;
                for (size_t i = 0; i < patches.size(); ++i)
                {
                    std::cout << "  Patch " << i << ": " 
                              << patches[i].triangleIndices.size() << " triangles, "
                              << patches[i].boundaryEdges.size() << " boundary edges, "
                              << (patches[i].isManifold ? "manifold" : "non-manifold")
                              << (patches[i].isClosed ? ", closed" : ", open") << "\n";
                    
                    if (patches[i].isClosed)
                    {
                        closedPatches++;
                    }
                }
                std::cout << "  " << closedPatches << " closed patches (no boundaries)\n";
                std::cout << "  " << (patches.size() - closedPatches) << " open patches (have boundaries)\n";
            }
            
            // Step 4a: Analyze patch pairs to identify stitching opportunities
            std::vector<PatchPair> patchPairs;
            if (patches.size() > 1)
            {
                // Estimate a reasonable max distance based on mesh scale
                Real avgEdgeLength = EstimateAverageEdgeLength(vertices, triangles);
                Real maxPairDistance = avgEdgeLength * static_cast<Real>(3);  // 3x average edge
                
                AnalyzePatchPairs(vertices, patches, patchPairs, maxPairDistance);
                
                if (params.verbose && !patchPairs.empty())
                {
                    std::cout << "\nFound " << patchPairs.size() << " patch pairs for potential stitching\n";
                    size_t toShow = std::min(static_cast<size_t>(5), patchPairs.size());
                    for (size_t i = 0; i < toShow; ++i)
                    {
                        auto const& pair = patchPairs[i];
                        std::cout << "  Pair " << i << ": patches " << pair.patch1Index 
                                  << " and " << pair.patch2Index
                                  << ", min dist = " << pair.minDistance
                                  << ", close vertices = " << pair.closeBoundaryVertices << "\n";
                    }
                    if (patchPairs.size() > toShow)
                    {
                        std::cout << "  ... and " << (patchPairs.size() - toShow) << " more pairs\n";
                    }
                }
            }
            
            // Step 4: Fill holes using existing hole filling (with validation)
            if (params.enableHoleFilling)
            {
                typename MeshHoleFilling<Real>::Parameters holeParams;
                holeParams.maxArea = params.maxHoleArea;
                holeParams.maxEdges = params.maxHoleEdges;
                holeParams.method = params.holeFillingMethod;
                holeParams.repair = true;
                
                size_t trianglesBefore = triangles.size();
                MeshHoleFilling<Real>::FillHoles(vertices, triangles, holeParams);
                size_t trianglesAfter = triangles.size();
                
                // Validate no non-manifold edges were created
                std::map<std::pair<int32_t, int32_t>, EdgeType> checkEdgeTypes;
                std::vector<std::pair<int32_t, int32_t>> checkNonManifold;
                AnalyzeEdgeTopology(triangles, checkEdgeTypes, checkNonManifold);
                
                if (!checkNonManifold.empty())
                {
                    // Hole filling created non-manifold edges - revert
                    if (params.verbose)
                    {
                        std::cout << "  Warning: Hole filling created " << checkNonManifold.size() 
                                  << " non-manifold edges, reverting\n";
                    }
                    triangles.resize(trianglesBefore);
                }
                else if (params.verbose && trianglesAfter > trianglesBefore)
                {
                    std::cout << "Hole filling added " << (trianglesAfter - trianglesBefore) 
                              << " triangles\n";
                }
            }
            
            // Step 5: Ball pivot stitching (if enabled) - SMART mode
            if (params.enableBallPivot && patches.size() > 1)
            {
                size_t trianglesBefore = triangles.size();
                
                // Process patch pairs for welding - with validation after each weld
                BallPivotWeldPatchesSmart(vertices, triangles, patches, patchPairs, params);
                
                size_t trianglesAfter = triangles.size();
                
                if (params.verbose && trianglesAfter > trianglesBefore)
                {
                    std::cout << "Ball pivot welding added " << (trianglesAfter - trianglesBefore) 
                              << " triangles\n";
                }
            }
            
            // Step 6: Final hole filling (conservative, not aggressive)
            if (params.enableHoleFilling)
            {
                // Fill remaining holes conservatively
                typename MeshHoleFilling<Real>::Parameters holeParams;
                holeParams.maxArea = params.maxHoleArea;
                holeParams.maxEdges = params.maxHoleEdges;
                holeParams.method = params.holeFillingMethod;
                holeParams.repair = true;
                
                size_t trianglesBefore = triangles.size();
                MeshHoleFilling<Real>::FillHoles(vertices, triangles, holeParams);
                size_t trianglesAfter = triangles.size();
                
                // Validate no non-manifold edges were created
                std::map<std::pair<int32_t, int32_t>, EdgeType> checkEdgeTypes;
                std::vector<std::pair<int32_t, int32_t>> checkNonManifold;
                AnalyzeEdgeTopology(triangles, checkEdgeTypes, checkNonManifold);
                
                if (!checkNonManifold.empty())
                {
                    // Hole filling created non-manifold edges - revert
                    if (params.verbose)
                    {
                        std::cout << "  Warning: Final hole filling created non-manifold edges, reverting\n";
                    }
                    triangles.resize(trianglesBefore);
                }
                else if (params.verbose && trianglesAfter > trianglesBefore)
                {
                    std::cout << "Final hole filling added " 
                              << (trianglesAfter - trianglesBefore) << " triangles\n";
                }
            }
            
            // Step 7: UV parameterization merging (if enabled)
            if (params.enableUVMerging)
            {
                // TODO: Implement UV-based patch merging
                if (params.verbose)
                {
                    std::cout << "UV-based patch merging not yet implemented\n";
                }
            }
            
            // Step 8: Final validation with detailed reporting
            bool isClosedManifold = false;
            bool hasNonManifoldEdges = false;
            size_t boundaryEdgeCount = 0;
            size_t componentCount = 0;
            
            ValidateManifoldDetailed(triangles, isClosedManifold, hasNonManifoldEdges, 
                                    boundaryEdgeCount, componentCount);
            
            if (params.verbose)
            {
                std::cout << "\n=== Final Mesh Status ===\n";
                std::cout << "  Triangles: " << triangles.size() << "\n";
                std::cout << "  Boundary edges: " << boundaryEdgeCount << "\n";
                std::cout << "  Connected components: " << componentCount << "\n";
                std::cout << "  Non-manifold edges: " << (hasNonManifoldEdges ? "YES" : "NO") << "\n";
                std::cout << "  Closed manifold: " << (isClosedManifold ? "YES" : "NO") << "\n";
            }
            
            return isClosedManifold;
        }
        
    private:
        // Analyze edge topology and classify edges
        static void AnalyzeEdgeTopology(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::map<std::pair<int32_t, int32_t>, EdgeType>& edgeTypes,
            std::vector<std::pair<int32_t, int32_t>>& nonManifoldEdges)
        {
            // Count triangle occurrences for each edge
            std::map<std::pair<int32_t, int32_t>, size_t> edgeCounts;
            
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    
                    // Use canonical edge ordering (smaller vertex first)
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    edgeCounts[edge]++;
                }
            }
            
            // Classify edges
            for (auto const& ec : edgeCounts)
            {
                if (ec.second == 1)
                {
                    edgeTypes[ec.first] = EdgeType::Boundary;
                }
                else if (ec.second == 2)
                {
                    edgeTypes[ec.first] = EdgeType::Interior;
                }
                else  // >= 3
                {
                    edgeTypes[ec.first] = EdgeType::NonManifold;
                    nonManifoldEdges.push_back(ec.first);
                }
            }
        }
        
        // Remove triangles that cause non-manifold edges
        static size_t RemoveNonManifoldEdges(
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<std::pair<int32_t, int32_t>> const& nonManifoldEdges)
        {
            if (nonManifoldEdges.empty())
            {
                return 0;
            }
            
            // Build set of non-manifold edges for fast lookup
            std::set<std::pair<int32_t, int32_t>> nmEdgeSet(
                nonManifoldEdges.begin(), nonManifoldEdges.end());
            
            // For each non-manifold edge, find all triangles containing it
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> edgeToTriangles;
            
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    
                    if (nmEdgeSet.find(edge) != nmEdgeSet.end())
                    {
                        edgeToTriangles[edge].push_back(i);
                    }
                }
            }
            
            // For each non-manifold edge, keep first 2 triangles, mark rest for removal
            std::set<size_t> trianglesToRemove;
            
            for (auto const& et : edgeToTriangles)
            {
                auto const& triIndices = et.second;
                if (triIndices.size() > 2)
                {
                    // Keep first 2, remove the rest
                    for (size_t i = 2; i < triIndices.size(); ++i)
                    {
                        trianglesToRemove.insert(triIndices[i]);
                    }
                }
            }
            
            // Remove marked triangles (in reverse order to maintain indices)
            std::vector<size_t> sortedToRemove(trianglesToRemove.begin(), trianglesToRemove.end());
            std::sort(sortedToRemove.rbegin(), sortedToRemove.rend());
            
            for (auto idx : sortedToRemove)
            {
                triangles.erase(triangles.begin() + idx);
            }
            
            return sortedToRemove.size();
        }
        
        // Identify connected patches in the mesh
        static void IdentifyPatches(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::map<std::pair<int32_t, int32_t>, EdgeType> const& edgeTypes,
            std::vector<Patch>& patches)
        {
            if (triangles.empty())
            {
                return;
            }
            
            // Build triangle adjacency graph
            std::map<size_t, std::set<size_t>> triangleAdjacency;
            BuildTriangleAdjacency(triangles, triangleAdjacency);
            
            // Flood-fill to identify connected components
            std::vector<bool> visited(triangles.size(), false);
            
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                if (visited[i])
                {
                    continue;
                }
                
                // Start a new patch
                Patch patch;
                std::vector<size_t> stack = { i };
                
                while (!stack.empty())
                {
                    size_t current = stack.back();
                    stack.pop_back();
                    
                    if (visited[current])
                    {
                        continue;
                    }
                    
                    visited[current] = true;
                    patch.triangleIndices.push_back(static_cast<int32_t>(current));
                    
                    // Add vertices
                    auto const& tri = triangles[current];
                    for (int j = 0; j < 3; ++j)
                    {
                        patch.vertexIndices.insert(tri[j]);
                    }
                    
                    // Add neighbors to stack
                    auto it = triangleAdjacency.find(current);
                    if (it != triangleAdjacency.end())
                    {
                        for (auto neighbor : it->second)
                        {
                            if (!visited[neighbor])
                            {
                                stack.push_back(neighbor);
                            }
                        }
                    }
                }
                
                // Compute patch properties
                ComputePatchProperties(vertices, triangles, edgeTypes, patch);
                patches.push_back(patch);
            }
        }
        
        // Build triangle adjacency graph (triangles sharing edges)
        static void BuildTriangleAdjacency(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::map<size_t, std::set<size_t>>& adjacency)
        {
            // Map edges to triangles
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> edgeToTriangles;
            
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    edgeToTriangles[edge].push_back(i);
                }
            }
            
            // Build adjacency from shared edges
            for (auto const& et : edgeToTriangles)
            {
                auto const& tris = et.second;
                for (size_t i = 0; i < tris.size(); ++i)
                {
                    for (size_t j = i + 1; j < tris.size(); ++j)
                    {
                        adjacency[tris[i]].insert(tris[j]);
                        adjacency[tris[j]].insert(tris[i]);
                    }
                }
            }
        }
        
        // Compute patch properties (boundary, area, manifold status)
        static void ComputePatchProperties(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::map<std::pair<int32_t, int32_t>, EdgeType> const& edgeTypes,
            Patch& patch)
        {
            // Compute area and centroid
            patch.area = static_cast<Real>(0);
            Vector3<Real> weightedCentroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
            
            for (auto triIdx : patch.triangleIndices)
            {
                auto const& tri = triangles[triIdx];
                Vector3<Real> v0 = vertices[tri[0]];
                Vector3<Real> v1 = vertices[tri[1]];
                Vector3<Real> v2 = vertices[tri[2]];
                
                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> cross = Cross(edge1, edge2);
                Real triArea = Length(cross) * static_cast<Real>(0.5);
                patch.area += triArea;
                
                // Triangle centroid
                Vector3<Real> triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);
                weightedCentroid += triCentroid * triArea;
            }
            
            if (patch.area > static_cast<Real>(0))
            {
                patch.centroid = weightedCentroid / patch.area;
            }
            
            // Find boundary edges and check manifold status
            std::map<std::pair<int32_t, int32_t>, size_t> patchEdgeCounts;
            
            for (auto triIdx : patch.triangleIndices)
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
            
            // Classify edges within this patch
            patch.isManifold = true;
            patch.isClosed = true;
            
            for (auto const& ec : patchEdgeCounts)
            {
                if (ec.second == 1)
                {
                    patch.boundaryEdges.push_back(ec.first);
                    patch.isClosed = false;
                }
                else if (ec.second > 2)
                {
                    patch.isManifold = false;
                }
            }
        }
        
        // Analyze distances between patch pairs
        static void AnalyzePatchPairs(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<Patch> const& patches,
            std::vector<PatchPair>& patchPairs,
            Real maxDistance = static_cast<Real>(0))
        {
            // Find pairs of patches that are close enough to potentially stitch
            for (size_t i = 0; i < patches.size(); ++i)
            {
                for (size_t j = i + 1; j < patches.size(); ++j)
                {
                    PatchPair pair;
                    pair.patch1Index = i;
                    pair.patch2Index = j;
                    
                    // Quick distance check using centroids
                    Vector3<Real> centroidDiff = patches[i].centroid - patches[j].centroid;
                    Real centroidDist = Length(centroidDiff);
                    
                    // If centroids are too far apart, skip detailed analysis
                    if (maxDistance > static_cast<Real>(0) && 
                        centroidDist > maxDistance * static_cast<Real>(5))
                    {
                        continue;
                    }
                    
                    // Analyze boundary vertex distances
                    std::set<int32_t> boundary1, boundary2;
                    for (auto const& edge : patches[i].boundaryEdges)
                    {
                        boundary1.insert(edge.first);
                        boundary1.insert(edge.second);
                    }
                    for (auto const& edge : patches[j].boundaryEdges)
                    {
                        boundary2.insert(edge.first);
                        boundary2.insert(edge.second);
                    }
                    
                    // Find min/max distances between boundary vertices
                    pair.minDistance = std::numeric_limits<Real>::max();
                    pair.maxDistance = static_cast<Real>(0);
                    pair.closeBoundaryVertices = 0;
                    
                    for (auto v1 : boundary1)
                    {
                        for (auto v2 : boundary2)
                        {
                            Vector3<Real> diff = vertices[v1] - vertices[v2];
                            Real dist = Length(diff);
                            
                            pair.minDistance = std::min(pair.minDistance, dist);
                            pair.maxDistance = std::max(pair.maxDistance, dist);
                            
                            if (maxDistance > static_cast<Real>(0) && dist < maxDistance)
                            {
                                pair.closeBoundaryVertices++;
                            }
                        }
                    }
                    
                    // Only add pairs that are close enough
                    if (maxDistance <= static_cast<Real>(0) || 
                        pair.minDistance < maxDistance)
                    {
                        patchPairs.push_back(pair);
                    }
                }
            }
            
            // Sort by minimum distance (closest pairs first)
            std::sort(patchPairs.begin(), patchPairs.end(),
                [](PatchPair const& a, PatchPair const& b) {
                    return a.minDistance < b.minDistance;
                });
        }
        
        // Ball pivot welding of patches
        static void BallPivotWeldPatches(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<Patch> const& patches,
            std::vector<PatchPair> const& patchPairs,
            Parameters const& params)
        {
            if (patchPairs.empty() || patches.empty())
            {
                return;
            }
            
            // Process patch pairs in order of proximity
            // AGGRESSIVE MODE: Process ALL viable pairs (no limit)
            size_t pairsToProcess = patchPairs.size();  // Process all pairs!
            
            size_t successfulWelds = 0;
            for (size_t pairIdx = 0; pairIdx < pairsToProcess; ++pairIdx)
            {
                auto const& pair = patchPairs[pairIdx];
                
                if (params.verbose)
                {
                    std::cout << "  Attempting to weld patches " << pair.patch1Index 
                              << " and " << pair.patch2Index 
                              << " (min dist = " << pair.minDistance << ")\n";
                }
                
                // Step 1: Identify outer boundary triangles in both patches
                std::set<size_t> outerTriangles1, outerTriangles2;
                IdentifyOuterBoundaryTriangles(vertices, triangles, 
                    patches[pair.patch1Index], outerTriangles1);
                IdentifyOuterBoundaryTriangles(vertices, triangles, 
                    patches[pair.patch2Index], outerTriangles2);
                
                if (params.verbose && (!outerTriangles1.empty() || !outerTriangles2.empty()))
                {
                    std::cout << "    Found " << outerTriangles1.size() << " outer triangles in patch " 
                              << pair.patch1Index << ", " 
                              << outerTriangles2.size() << " in patch " << pair.patch2Index << "\n";
                }
                
                // Step 2: Remove outer triangles to create clean boundaries
                std::set<size_t> allOuterTriangles = outerTriangles1;
                allOuterTriangles.insert(outerTriangles2.begin(), outerTriangles2.end());
                
                size_t removedCount = 0;
                if (!allOuterTriangles.empty())
                {
                    removedCount = RemoveTrianglesByIndex(triangles, allOuterTriangles);
                    
                    if (params.verbose && removedCount > 0)
                    {
                        std::cout << "    Removed " << removedCount << " outer triangles\n";
                    }
                }
                
                // Step 3: Re-identify patches after triangle removal to get updated boundaries
                // We need fresh boundary information after removing outer triangles
                std::map<std::pair<int32_t, int32_t>, EdgeType> updatedEdgeTypes;
                std::vector<std::pair<int32_t, int32_t>> dummy;
                AnalyzeEdgeTopology(triangles, updatedEdgeTypes, dummy);
                
                std::vector<Patch> updatedPatches;
                IdentifyPatches(vertices, triangles, updatedEdgeTypes, updatedPatches);
                
                // Find which updated patches correspond to our original pair
                // This is a bit tricky since patches may have merged or split
                // For now, we'll extract all boundary points from the current mesh state
                std::set<int32_t> allBoundaryVerts;
                for (auto const& et : updatedEdgeTypes)
                {
                    if (et.second == EdgeType::Boundary)
                    {
                        allBoundaryVerts.insert(et.first.first);
                        allBoundaryVerts.insert(et.first.second);
                    }
                }
                
                // Extract boundary points and normals
                std::vector<Vector3<Real>> boundaryPoints;
                std::vector<Vector3<Real>> boundaryNormals;
                ExtractBoundaryPointsFromVertices(vertices, triangles, 
                    allBoundaryVerts, boundaryPoints, boundaryNormals);
                
                if (boundaryPoints.empty())
                {
                    if (params.verbose)
                    {
                        std::cout << "    No boundary points found for welding\n";
                    }
                    continue;
                }
                
                // Step 4: Compute appropriate ball radius based on gap size
                Real ballRadius = EstimateBallRadiusForGap(boundaryPoints, pair.minDistance);
                
                if (params.verbose)
                {
                    std::cout << "    Using ball radius: " << ballRadius 
                              << " for " << boundaryPoints.size() << " boundary points\n";
                }
                
                // Step 5: Run Ball Pivot on boundary region
                typename BallPivotReconstruction<Real>::Parameters bpParams;
                bpParams.radii = {ballRadius};
                bpParams.verbose = false;  // Keep it quiet to avoid clutter
                
                std::vector<Vector3<Real>> weldVertices;
                std::vector<std::array<int32_t, 3>> weldTriangles;
                
                bool success = BallPivotReconstruction<Real>::Reconstruct(
                    boundaryPoints, boundaryNormals, weldVertices, weldTriangles, bpParams);
                
                if (!success || weldTriangles.empty())
                {
                    if (params.verbose)
                    {
                        std::cout << "    Ball pivot failed to generate welding triangles\n";
                    }
                    continue;
                }
                
                // Step 6: Merge welding triangles into main mesh
                size_t addedTriangles = MergeWeldingTriangles(
                    vertices, triangles, boundaryPoints, weldTriangles);
                
                if (addedTriangles > 0)
                {
                    successfulWelds++;
                }
                
                if (params.verbose && addedTriangles > 0)
                {
                    std::cout << "    Added " << addedTriangles << " welding triangles\n";
                }
            }
            
            if (params.verbose && successfulWelds > 0)
            {
                std::cout << "  Successfully welded " << successfulWelds << " patch pairs\n";
            }
        }
        
        // Smart Ball Pivot welding that validates after each weld
        static void BallPivotWeldPatchesSmart(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<Patch> const& patches,
            std::vector<PatchPair> const& patchPairs,
            Parameters const& params)
        {
            if (patchPairs.empty() || patches.empty())
            {
                return;
            }
            
            size_t successfulWelds = 0;
            size_t skippedWelds = 0;
            
            // Process patch pairs, validating after each weld
            for (size_t pairIdx = 0; pairIdx < patchPairs.size(); ++pairIdx)
            {
                auto const& pair = patchPairs[pairIdx];
                
                // Save state before welding
                size_t trianglesBefore = triangles.size();
                std::vector<std::array<int32_t, 3>> trianglesBackup = triangles;
                
                if (params.verbose && (pairIdx < 10 || successfulWelds < 5))
                {
                    std::cout << "  Attempting to weld patches " << pair.patch1Index 
                              << " and " << pair.patch2Index 
                              << " (min dist = " << pair.minDistance << ")\n";
                }
                
                // Try welding using boundary points from current mesh state
                std::set<int32_t> allBoundaryVerts;
                std::map<std::pair<int32_t, int32_t>, EdgeType> updatedEdgeTypes;
                std::vector<std::pair<int32_t, int32_t>> dummy;
                AnalyzeEdgeTopology(triangles, updatedEdgeTypes, dummy);
                
                for (auto const& et : updatedEdgeTypes)
                {
                    if (et.second == EdgeType::Boundary)
                    {
                        allBoundaryVerts.insert(et.first.first);
                        allBoundaryVerts.insert(et.first.second);
                    }
                }
                
                if (allBoundaryVerts.size() < 3)
                {
                    skippedWelds++;
                    continue;  // Not enough boundary points
                }
                
                // Extract boundary points and normals
                std::vector<Vector3<Real>> boundaryPoints;
                std::vector<Vector3<Real>> boundaryNormals;
                ExtractBoundaryPointsFromVertices(vertices, triangles, 
                    allBoundaryVerts, boundaryPoints, boundaryNormals);
                
                if (boundaryPoints.size() < 4)
                {
                    skippedWelds++;
                    continue;  // Not enough points for Ball Pivot
                }
                
                // Estimate ball radius
                Real ballRadius = EstimateBallRadiusForGap(boundaryPoints, pair.minDistance);
                
                // Run Ball Pivot on boundary region
                typename BallPivotReconstruction<Real>::Parameters bpParams;
                bpParams.verbose = false;
                bpParams.radii = {ballRadius};
                
                std::vector<Vector3<Real>> weldPoints;
                std::vector<std::array<int32_t, 3>> weldTriangles;
                
                bool bpSuccess = BallPivotReconstruction<Real>::Reconstruct(
                    boundaryPoints, boundaryNormals, weldPoints, weldTriangles, bpParams);
                
                if (!bpSuccess || weldTriangles.empty())
                {
                    skippedWelds++;
                    continue;
                }
                
                // Merge welding triangles
                MergeWeldingTriangles(vertices, triangles, boundaryPoints, weldTriangles);
                
                // Validate: check for non-manifold edges
                std::map<std::pair<int32_t, int32_t>, EdgeType> checkEdgeTypes;
                std::vector<std::pair<int32_t, int32_t>> checkNonManifold;
                AnalyzeEdgeTopology(triangles, checkEdgeTypes, checkNonManifold);
                
                if (!checkNonManifold.empty())
                {
                    // Welding created non-manifold edges - revert
                    triangles = trianglesBackup;
                    skippedWelds++;
                    
                    if (params.verbose && (pairIdx < 10 || successfulWelds < 5))
                    {
                        std::cout << "    Weld created non-manifold edges, reverted\n";
                    }
                }
                else
                {
                    // Success!
                    successfulWelds++;
                    
                    if (params.verbose && (pairIdx < 10 || successfulWelds < 5))
                    {
                        std::cout << "    Added " << (triangles.size() - trianglesBefore) 
                                  << " welding triangles\n";
                    }
                    
                    // Stop after reasonable number of successful welds to avoid conflicts
                    if (successfulWelds >= 50)
                    {
                        if (params.verbose)
                        {
                            std::cout << "  Reached 50 successful welds, stopping to avoid conflicts\n";
                        }
                        break;
                    }
                }
            }
            
            if (params.verbose)
            {
                std::cout << "  Smart welding: " << successfulWelds << " successful, " 
                          << skippedWelds << " skipped/reverted\n";
            }
        }
        
        // Identify outer boundary triangles in a patch
        // These are triangles on the boundary that face away from the patch interior
        static void IdentifyOuterBoundaryTriangles(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            Patch const& patch,
            std::set<size_t>& outerTriangles)
        {
            outerTriangles.clear();
            
            if (patch.isClosed)
            {
                return;  // No boundary triangles in closed patches
            }
            
            // Build set of boundary edges for fast lookup
            std::set<std::pair<int32_t, int32_t>> boundaryEdgeSet;
            for (auto const& edge : patch.boundaryEdges)
            {
                boundaryEdgeSet.insert(std::make_pair(
                    std::min(edge.first, edge.second),
                    std::max(edge.first, edge.second)));
            }
            
            // For each triangle in patch, check if it has boundary edges
            for (auto triIdx : patch.triangleIndices)
            {
                auto const& tri = triangles[triIdx];
                
                // Count how many boundary edges this triangle has
                int boundaryEdgeCount = 0;
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    
                    if (boundaryEdgeSet.find(edge) != boundaryEdgeSet.end())
                    {
                        boundaryEdgeCount++;
                    }
                }
                
                // Triangles with 2+ boundary edges are considered "outer"
                // These are on the fringe and can be safely removed
                if (boundaryEdgeCount >= 2)
                {
                    outerTriangles.insert(triIdx);
                }
            }
        }
        
        // Remove triangles by their indices
        static size_t RemoveTrianglesByIndex(
            std::vector<std::array<int32_t, 3>>& triangles,
            std::set<size_t> const& indicesToRemove)
        {
            if (indicesToRemove.empty())
            {
                return 0;
            }
            
            // Sort indices in reverse order for safe removal
            std::vector<size_t> sortedIndices(indicesToRemove.begin(), indicesToRemove.end());
            std::sort(sortedIndices.rbegin(), sortedIndices.rend());
            
            for (auto idx : sortedIndices)
            {
                if (idx < triangles.size())
                {
                    triangles.erase(triangles.begin() + idx);
                }
            }
            
            return sortedIndices.size();
        }
        
        // Extract boundary points given a set of vertex indices
        static void ExtractBoundaryPointsFromVertices(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::set<int32_t> const& boundaryVertices,
            std::vector<Vector3<Real>>& boundaryPoints,
            std::vector<Vector3<Real>>& boundaryNormals)
        {
            boundaryPoints.clear();
            boundaryNormals.clear();
            
            if (boundaryVertices.empty())
            {
                return;
            }
            
            boundaryPoints.reserve(boundaryVertices.size());
            boundaryNormals.reserve(boundaryVertices.size());
            
            // For each boundary vertex, get position and estimate normal
            for (auto vIdx : boundaryVertices)
            {
                boundaryPoints.push_back(vertices[vIdx]);
                
                // Estimate normal by averaging adjacent triangle normals
                Vector3<Real> avgNormal = Vector3<Real>::Zero();
                int normalCount = 0;
                
                // Find all triangles using this vertex
                for (auto const& tri : triangles)
                {
                    if (tri[0] == vIdx || tri[1] == vIdx || tri[2] == vIdx)
                    {
                        Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
                        Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
                        Vector3<Real> n = Cross(e1, e2);
                        Real len = Length(n);
                        if (len > static_cast<Real>(0))
                        {
                            avgNormal += n / len;
                            normalCount++;
                        }
                    }
                }
                
                if (normalCount > 0)
                {
                    avgNormal /= static_cast<Real>(normalCount);
                    Real avgLen = Length(avgNormal);
                    if (avgLen > static_cast<Real>(0))
                    {
                        avgNormal /= avgLen;
                    }
                }
                else
                {
                    // Default normal if no triangles found
                    avgNormal = Vector3<Real>::Unit(2);  // Z-axis
                }
                
                boundaryNormals.push_back(avgNormal);
            }
        }
        
        // Extract boundary points from patch pair for welding
        static void ExtractPatchBoundaryPoints(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            Patch const& patch1,
            Patch const& patch2,
            std::vector<Vector3<Real>>& boundaryPoints,
            std::vector<Vector3<Real>>& boundaryNormals)
        {
            boundaryPoints.clear();
            boundaryNormals.clear();
            
            // Collect vertices from boundary edges of both patches
            std::set<int32_t> boundaryVertexSet;
            
            // Add from patch 1
            for (auto const& edge : patch1.boundaryEdges)
            {
                boundaryVertexSet.insert(edge.first);
                boundaryVertexSet.insert(edge.second);
            }
            
            // Add from patch 2
            for (auto const& edge : patch2.boundaryEdges)
            {
                boundaryVertexSet.insert(edge.first);
                boundaryVertexSet.insert(edge.second);
            }
            
            // Convert to vectors
            std::vector<int32_t> boundaryVertices(boundaryVertexSet.begin(), boundaryVertexSet.end());
            
            boundaryPoints.reserve(boundaryVertices.size());
            boundaryNormals.reserve(boundaryVertices.size());
            
            // For each boundary vertex, get position and estimate normal
            for (auto vIdx : boundaryVertices)
            {
                boundaryPoints.push_back(vertices[vIdx]);
                
                // Estimate normal by averaging adjacent triangle normals
                Vector3<Real> avgNormal = Vector3<Real>::Zero();
                int normalCount = 0;
                
                // Check both patches for triangles using this vertex
                for (auto triIdx : patch1.triangleIndices)
                {
                    auto const& tri = triangles[triIdx];
                    if (tri[0] == vIdx || tri[1] == vIdx || tri[2] == vIdx)
                    {
                        Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
                        Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
                        Vector3<Real> n = Cross(e1, e2);
                        Real len = Length(n);
                        if (len > static_cast<Real>(0))
                        {
                            avgNormal += n / len;
                            normalCount++;
                        }
                    }
                }
                
                for (auto triIdx : patch2.triangleIndices)
                {
                    auto const& tri = triangles[triIdx];
                    if (tri[0] == vIdx || tri[1] == vIdx || tri[2] == vIdx)
                    {
                        Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
                        Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
                        Vector3<Real> n = Cross(e1, e2);
                        Real len = Length(n);
                        if (len > static_cast<Real>(0))
                        {
                            avgNormal += n / len;
                            normalCount++;
                        }
                    }
                }
                
                if (normalCount > 0)
                {
                    avgNormal /= static_cast<Real>(normalCount);
                    Real avgLen = Length(avgNormal);
                    if (avgLen > static_cast<Real>(0))
                    {
                        avgNormal /= avgLen;
                    }
                }
                else
                {
                    // Default normal if no triangles found
                    avgNormal = Vector3<Real>::Unit(2);  // Z-axis
                }
                
                boundaryNormals.push_back(avgNormal);
            }
        }
        
        // Estimate appropriate ball radius for a gap
        static Real EstimateBallRadiusForGap(
            std::vector<Vector3<Real>> const& boundaryPoints,
            Real minDistance)
        {
            if (boundaryPoints.size() < 2)
            {
                // Use minimum distance or a small default
                return std::max(minDistance * static_cast<Real>(1.5), static_cast<Real>(0.1));
            }
            
            // Compute average nearest neighbor distance in boundary points
            Real avgSpacing = static_cast<Real>(0);
            size_t count = 0;
            
            // Sample uniformly across all boundary points
            for (size_t i = 0; i < boundaryPoints.size(); ++i)
            {
                Real minDist = std::numeric_limits<Real>::max();
                
                // Find nearest neighbor
                for (size_t j = 0; j < boundaryPoints.size(); ++j)
                {
                    if (i == j) continue;
                    
                    Real dist = Length(boundaryPoints[j] - boundaryPoints[i]);
                    if (dist > static_cast<Real>(0) && dist < minDist)
                    {
                        minDist = dist;
                    }
                }
                
                if (minDist < std::numeric_limits<Real>::max())
                {
                    avgSpacing += minDist;
                    count++;
                }
            }
            
            if (count > 0)
            {
                avgSpacing /= static_cast<Real>(count);
            }
            else
            {
                // Fallback: use minDistance if available
                if (minDistance > static_cast<Real>(0))
                {
                    avgSpacing = minDistance * static_cast<Real>(2);
                }
                else
                {
                    avgSpacing = static_cast<Real>(1);
                }
            }
            
            // Use 1.5x the average spacing as the ball radius
            // This is large enough to bridge gaps but not so large it creates bad triangles
            Real radius = avgSpacing * static_cast<Real>(1.5);
            
            return radius;
        }
        
        // Merge welding triangles into the main mesh
        static size_t MergeWeldingTriangles(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<Vector3<Real>> const& weldPoints,
            std::vector<std::array<int32_t, 3>> const& weldTriangles)
        {
            if (weldTriangles.empty())
            {
                return 0;
            }
            
            // Build a map from weld point coordinates to existing vertex indices
            // Use a simple tolerance-based matching
            std::map<int32_t, int32_t> weldToMainIndex;  // weldIndex -> mainIndex
            Real tolerance = static_cast<Real>(1e-6);
            
            for (size_t wIdx = 0; wIdx < weldPoints.size(); ++wIdx)
            {
                bool found = false;
                
                // Try to find matching vertex in main mesh
                for (size_t vIdx = 0; vIdx < vertices.size(); ++vIdx)
                {
                    Real dist = Length(vertices[vIdx] - weldPoints[wIdx]);
                    if (dist < tolerance)
                    {
                        weldToMainIndex[static_cast<int32_t>(wIdx)] = static_cast<int32_t>(vIdx);
                        found = true;
                        break;
                    }
                }
                
                // If not found, add as new vertex
                if (!found)
                {
                    weldToMainIndex[static_cast<int32_t>(wIdx)] = static_cast<int32_t>(vertices.size());
                    vertices.push_back(weldPoints[wIdx]);
                }
            }
            
            // Add welding triangles with remapped indices
            size_t addedCount = 0;
            for (auto const& tri : weldTriangles)
            {
                std::array<int32_t, 3> newTri;
                newTri[0] = weldToMainIndex[tri[0]];
                newTri[1] = weldToMainIndex[tri[1]];
                newTri[2] = weldToMainIndex[tri[2]];
                
                triangles.push_back(newTri);
                addedCount++;
            }
            
            return addedCount;
        }
        
        // Validate that mesh is manifold (simple check)
        static bool ValidateManifold(std::vector<std::array<int32_t, 3>> const& triangles)
        {
            std::map<std::pair<int32_t, int32_t>, size_t> edgeCounts;
            
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
            
            for (auto const& ec : edgeCounts)
            {
                if (ec.second > 2)
                {
                    return false;  // Non-manifold edge found
                }
            }
            
            return true;
        }
        
        // Detailed manifold validation
        static void ValidateManifoldDetailed(
            std::vector<std::array<int32_t, 3>> const& triangles,
            bool& isClosedManifold,
            bool& hasNonManifoldEdges,
            size_t& boundaryEdgeCount,
            size_t& componentCount)
        {
            isClosedManifold = false;
            hasNonManifoldEdges = false;
            boundaryEdgeCount = 0;
            componentCount = 0;
            
            if (triangles.empty())
            {
                return;
            }
            
            // Count edge occurrences
            std::map<std::pair<int32_t, int32_t>, size_t> edgeCounts;
            
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
            
            // Analyze edges
            for (auto const& ec : edgeCounts)
            {
                if (ec.second == 1)
                {
                    boundaryEdgeCount++;
                }
                else if (ec.second > 2)
                {
                    hasNonManifoldEdges = true;
                }
            }
            
            // Count connected components using BFS
            std::set<size_t> visited;
            componentCount = 0;
            
            for (size_t startIdx = 0; startIdx < triangles.size(); ++startIdx)
            {
                if (visited.count(startIdx) > 0)
                {
                    continue;
                }
                
                // Start new component
                componentCount++;
                std::vector<size_t> queue;
                queue.push_back(startIdx);
                visited.insert(startIdx);
                
                // BFS to find all connected triangles
                while (!queue.empty())
                {
                    size_t currentIdx = queue.back();
                    queue.pop_back();
                    
                    auto const& currentTri = triangles[currentIdx];
                    
                    // Find triangles sharing edges with current
                    for (size_t otherIdx = 0; otherIdx < triangles.size(); ++otherIdx)
                    {
                        if (visited.count(otherIdx) > 0)
                        {
                            continue;
                        }
                        
                        auto const& otherTri = triangles[otherIdx];
                        
                        // Check if they share an edge
                        bool sharesEdge = false;
                        for (int i = 0; i < 3 && !sharesEdge; ++i)
                        {
                            auto e1 = std::make_pair(
                                std::min(currentTri[i], currentTri[(i + 1) % 3]),
                                std::max(currentTri[i], currentTri[(i + 1) % 3]));
                            
                            for (int j = 0; j < 3 && !sharesEdge; ++j)
                            {
                                auto e2 = std::make_pair(
                                    std::min(otherTri[j], otherTri[(j + 1) % 3]),
                                    std::max(otherTri[j], otherTri[(j + 1) % 3]));
                                
                                if (e1 == e2)
                                {
                                    sharesEdge = true;
                                }
                            }
                        }
                        
                        if (sharesEdge)
                        {
                            queue.push_back(otherIdx);
                            visited.insert(otherIdx);
                        }
                    }
                }
            }
            
            // A closed manifold has no boundary edges, no non-manifold edges, and is connected
            isClosedManifold = (boundaryEdgeCount == 0) && (!hasNonManifoldEdges) && (componentCount == 1);
        }
        
        // Estimate average edge length in the mesh
        static Real EstimateAverageEdgeLength(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            if (triangles.empty())
            {
                return static_cast<Real>(1);
            }
            
            Real totalLength = static_cast<Real>(0);
            size_t edgeCount = 0;
            
            // Sample edges from triangles (just use first few to be efficient)
            size_t samplesToUse = std::min(static_cast<size_t>(100), triangles.size());
            
            for (size_t i = 0; i < samplesToUse; ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    Vector3<Real> v0 = vertices[tri[j]];
                    Vector3<Real> v1 = vertices[tri[(j + 1) % 3]];
                    totalLength += Length(v1 - v0);
                    edgeCount++;
                }
            }
            
            return edgeCount > 0 ? totalLength / static_cast<Real>(edgeCount) : static_cast<Real>(1);
        }
    };
}

#endif // GTE_MATHEMATICS_CO3NE_MANIFOLD_STITCHER_H
