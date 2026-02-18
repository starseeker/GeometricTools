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
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
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
            
            // Ball pivot mesh hole filler parameters
            bool enableBallPivotHoleFiller;  // Use new adaptive hole filler
            typename BallPivotMeshHoleFiller<Real>::EdgeMetric edgeMetric;
            std::vector<Real> radiusScales;  // Scaling factors for adaptive radius
            int32_t maxHoleFillerIterations;
            bool removeEdgeTrianglesOnFailure;
            
            // UV parameterization parameters
            bool enableUVMerging;
            Real uvMergingThreshold;
            
            // Topology bridging parameters
            bool enableIterativeBridging;  // Multi-pass bridging with hole filling
            size_t maxIterations;          // Max number of bridging iterations
            Real initialBridgeThreshold;   // Initial distance threshold multiplier
            Real maxBridgeThreshold;       // Maximum distance threshold multiplier
            bool targetSingleComponent;    // Stop when single component achieved
            
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
                , enableBallPivotHoleFiller(true)  // Enabled by default (new implementation)
                , edgeMetric(BallPivotMeshHoleFiller<Real>::EdgeMetric::Average)
                , radiusScales({static_cast<Real>(1.0), static_cast<Real>(1.5), 
                               static_cast<Real>(2.0), static_cast<Real>(3.0), 
                               static_cast<Real>(5.0)})
                , maxHoleFillerIterations(10)
                , removeEdgeTrianglesOnFailure(true)
                , enableUVMerging(false)  // Disabled by default (not yet implemented)
                , uvMergingThreshold(static_cast<Real>(0.1))
                , enableIterativeBridging(true)  // Enabled by default
                , maxIterations(10)
                , initialBridgeThreshold(static_cast<Real>(2.0))
                , maxBridgeThreshold(static_cast<Real>(10.0))
                , targetSingleComponent(true)
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
            
            // Step 6: Ball Pivot Mesh Hole Filler (adaptive, iterative)
            if (params.enableBallPivotHoleFiller)
            {
                if (params.verbose)
                {
                    std::cout << "[Stitcher] Step 6: Ball Pivot Mesh Hole Filler (adaptive)\n";
                }
                
                // Configure hole filler parameters
                typename BallPivotMeshHoleFiller<Real>::Parameters holeFillerParams;
                holeFillerParams.edgeMetric = params.edgeMetric;
                holeFillerParams.radiusScales = params.radiusScales;
                holeFillerParams.maxIterations = params.maxHoleFillerIterations;
                holeFillerParams.removeEdgeTrianglesOnFailure = params.removeEdgeTrianglesOnFailure;
                holeFillerParams.verbose = params.verbose;
                
                size_t trianglesBefore = triangles.size();
                bool holeFilled = BallPivotMeshHoleFiller<Real>::FillAllHoles(
                    vertices, triangles, holeFillerParams);
                size_t trianglesAfter = triangles.size();
                
                if (params.verbose)
                {
                    std::cout << "  Ball pivot hole filler: " 
                              << (holeFilled ? "SUCCESS" : "NO CHANGES") 
                              << ", added " << (trianglesAfter - trianglesBefore) 
                              << " triangles\n";
                }
            }
            
            // Step 7: Final hole filling (conservative, not aggressive)
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
            
            // Step 7: Topology-Aware Component Bridging (alternative to Ball Pivot)
            if (!params.enableBallPivot)  // Use when Ball Pivot is disabled
            {
                TopologyAwareComponentBridging(vertices, triangles, params);
            }
            
            // Step 8: UV parameterization merging (if enabled)
            if (params.enableUVMerging)
            {
                // TODO: Implement UV-based patch merging
                if (params.verbose)
                {
                    std::cout << "UV-based patch merging not yet implemented\n";
                }
            }
            
            // Step 9: Final validation with detailed reporting
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
        
        // ======================================================================
        // TOPOLOGY-AWARE COMPONENT BRIDGING
        // ======================================================================
        
        // Extract boundary loops from mesh
        // A boundary loop is a closed sequence of boundary edges
        struct BoundaryLoop
        {
            std::vector<int32_t> vertices;  // Ordered vertices forming the loop
            Vector3<Real> centroid;
            Real perimeter;
            
            BoundaryLoop() : perimeter(static_cast<Real>(0)) {}
        };
        
        static void ExtractBoundaryLoops(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<BoundaryLoop>& loops)
        {
            loops.clear();
            
            // Find all boundary edges
            std::map<std::pair<int32_t, int32_t>, EdgeType> edgeTypes;
            std::vector<std::pair<int32_t, int32_t>> dummy;
            AnalyzeEdgeTopology(triangles, edgeTypes, dummy);
            
            // Build adjacency for boundary vertices
            std::map<int32_t, std::vector<int32_t>> boundaryAdj;
            for (auto const& et : edgeTypes)
            {
                if (et.second == EdgeType::Boundary)
                {
                    int32_t v0 = et.first.first;
                    int32_t v1 = et.first.second;
                    boundaryAdj[v0].push_back(v1);
                    boundaryAdj[v1].push_back(v0);
                }
            }
            
            // Extract loops using traversal
            std::set<int32_t> visited;
            
            for (auto const& adj : boundaryAdj)
            {
                int32_t startVertex = adj.first;
                if (visited.count(startVertex)) continue;
                
                // Start a new loop
                BoundaryLoop loop;
                loop.vertices.push_back(startVertex);
                visited.insert(startVertex);
                
                int32_t current = startVertex;
                int32_t previous = -1;
                
                // Follow the boundary
                while (true)
                {
                    // Find next unvisited neighbor
                    int32_t next = -1;
                    for (int32_t neighbor : boundaryAdj[current])
                    {
                        if (neighbor != previous)
                        {
                            next = neighbor;
                            break;
                        }
                    }
                    
                    if (next == -1 || next == startVertex)
                    {
                        break;  // Loop complete or dead end
                    }
                    
                    if (visited.count(next))
                    {
                        break;  // Already visited
                    }
                    
                    loop.vertices.push_back(next);
                    visited.insert(next);
                    previous = current;
                    current = next;
                }
                
                // Compute loop properties
                if (loop.vertices.size() >= 3)
                {
                    loop.centroid = Vector3<Real>::Zero();
                    for (int32_t v : loop.vertices)
                    {
                        loop.centroid += vertices[v];
                    }
                    loop.centroid /= static_cast<Real>(loop.vertices.size());
                    
                    loop.perimeter = static_cast<Real>(0);
                    for (size_t i = 0; i < loop.vertices.size(); ++i)
                    {
                        size_t j = (i + 1) % loop.vertices.size();
                        loop.perimeter += Length(vertices[loop.vertices[j]] - vertices[loop.vertices[i]]);
                    }
                    
                    loops.push_back(loop);
                }
            }
        }
        
        // Bridge two boundary edges by creating two triangles
        static bool BridgeBoundaryEdges(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::pair<int32_t, int32_t> const& edge1,
            std::pair<int32_t, int32_t> const& edge2,
            bool validate = true)
        {
            // Create two triangles to bridge edge1 and edge2
            // edge1: (a, b), edge2: (c, d)
            // Create triangles: (a, b, c) and (b, d, c)
            
            int32_t a = edge1.first;
            int32_t b = edge1.second;
            int32_t c = edge2.first;
            int32_t d = edge2.second;
            
            // Check edge orientations and distances
            Real dist_ac = Length(vertices[c] - vertices[a]);
            Real dist_bd = Length(vertices[d] - vertices[b]);
            Real dist_ad = Length(vertices[d] - vertices[a]);
            Real dist_bc = Length(vertices[c] - vertices[b]);
            
            // Choose better pairing
            std::array<int32_t, 3> tri1, tri2;
            if (dist_ac + dist_bd < dist_ad + dist_bc)
            {
                // Bridge a-c and b-d
                tri1 = {a, b, c};
                tri2 = {b, d, c};
            }
            else
            {
                // Bridge a-d and b-c
                tri1 = {a, b, d};
                tri2 = {a, d, c};
            }
            
            // Save current state
            size_t oldSize = triangles.size();
            
            // Add triangles
            triangles.push_back(tri1);
            triangles.push_back(tri2);
            
            // Validate if requested
            if (validate)
            {
                if (!ValidateManifold(triangles))
                {
                    // Revert
                    triangles.resize(oldSize);
                    return false;
                }
            }
            
            return true;
        }
        
        // Cap a boundary loop with fan triangulation from centroid
        static bool CapBoundaryLoop(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& loop,
            bool validate = true)
        {
            if (loop.vertices.size() < 3)
            {
                return false;
            }
            
            // Save current state
            size_t oldTriCount = triangles.size();
            size_t oldVertCount = vertices.size();
            
            // Add centroid as new vertex
            int32_t centroidIdx = static_cast<int32_t>(vertices.size());
            vertices.push_back(loop.centroid);
            
            // Create fan triangulation
            for (size_t i = 0; i < loop.vertices.size(); ++i)
            {
                size_t j = (i + 1) % loop.vertices.size();
                
                std::array<int32_t, 3> tri = {
                    loop.vertices[i],
                    loop.vertices[j],
                    centroidIdx
                };
                
                triangles.push_back(tri);
            }
            
            // Validate if requested
            if (validate)
            {
                if (!ValidateManifold(triangles))
                {
                    // Revert
                    triangles.resize(oldTriCount);
                    vertices.resize(oldVertCount);
                    return false;
                }
            }
            
            return true;
        }
        
        // ======================================================================
        // COMPONENT-AWARE PROGRESSIVE MERGING
        // ======================================================================
        
        // Structure to represent a connected component
        struct MeshComponent
        {
            std::vector<size_t> triangleIndices;  // Indices of triangles in this component
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;  // Boundary edges
            std::set<int32_t> vertices;  // All vertices in component
            Vector3<Real> centroid;  // Component centroid
            Real boundingRadius;  // Bounding sphere radius
            
            MeshComponent() 
                : centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)}
                , boundingRadius(static_cast<Real>(0))
            {}
            
            size_t Size() const { return triangleIndices.size(); }
        };
        
        // Extract connected components from mesh
        static void ExtractComponents(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<MeshComponent>& components)
        {
            components.clear();
            
            if (triangles.empty())
            {
                return;
            }
            
            // Build edge-to-triangles adjacency
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> edgeToTriangles;
            for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
            {
                auto const& tri = triangles[triIdx];
                for (int i = 0; i < 3; ++i)
                {
                    auto edge = std::make_pair(
                        std::min(tri[i], tri[(i + 1) % 3]),
                        std::max(tri[i], tri[(i + 1) % 3]));
                    edgeToTriangles[edge].push_back(triIdx);
                }
            }
            
            // BFS to find connected components
            std::set<size_t> visited;
            
            for (size_t startIdx = 0; startIdx < triangles.size(); ++startIdx)
            {
                if (visited.count(startIdx) > 0)
                {
                    continue;
                }
                
                // Start new component
                MeshComponent component;
                std::vector<size_t> queue;
                queue.push_back(startIdx);
                visited.insert(startIdx);
                component.triangleIndices.push_back(startIdx);
                
                // Add vertices from first triangle
                for (int i = 0; i < 3; ++i)
                {
                    component.vertices.insert(triangles[startIdx][i]);
                }
                
                // BFS to find all connected triangles
                size_t queuePos = 0;
                while (queuePos < queue.size())
                {
                    size_t currentIdx = queue[queuePos++];
                    auto const& currentTri = triangles[currentIdx];
                    
                    // Check all edges of current triangle
                    for (int i = 0; i < 3; ++i)
                    {
                        auto edge = std::make_pair(
                            std::min(currentTri[i], currentTri[(i + 1) % 3]),
                            std::max(currentTri[i], currentTri[(i + 1) % 3]));
                        
                        auto it = edgeToTriangles.find(edge);
                        if (it != edgeToTriangles.end())
                        {
                            for (size_t neighborIdx : it->second)
                            {
                                if (visited.count(neighborIdx) == 0)
                                {
                                    visited.insert(neighborIdx);
                                    queue.push_back(neighborIdx);
                                    component.triangleIndices.push_back(neighborIdx);
                                    
                                    // Add vertices from this triangle
                                    for (int j = 0; j < 3; ++j)
                                    {
                                        component.vertices.insert(triangles[neighborIdx][j]);
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Extract boundary edges for this component
                std::map<std::pair<int32_t, int32_t>, size_t> edgeCounts;
                for (size_t triIdx : component.triangleIndices)
                {
                    auto const& tri = triangles[triIdx];
                    for (int i = 0; i < 3; ++i)
                    {
                        auto edge = std::make_pair(
                            std::min(tri[i], tri[(i + 1) % 3]),
                            std::max(tri[i], tri[(i + 1) % 3]));
                        edgeCounts[edge]++;
                    }
                }
                
                for (auto const& ec : edgeCounts)
                {
                    if (ec.second == 1)  // Boundary edge
                    {
                        component.boundaryEdges.push_back(ec.first);
                    }
                }
                
                // Compute centroid and bounding radius
                component.centroid = Vector3<Real>{
                    static_cast<Real>(0),
                    static_cast<Real>(0),
                    static_cast<Real>(0)
                };
                
                for (int32_t vIdx : component.vertices)
                {
                    component.centroid += vertices[vIdx];
                }
                
                if (!component.vertices.empty())
                {
                    component.centroid /= static_cast<Real>(component.vertices.size());
                }
                
                // Compute bounding radius
                component.boundingRadius = static_cast<Real>(0);
                for (int32_t vIdx : component.vertices)
                {
                    Real dist = Length(vertices[vIdx] - component.centroid);
                    component.boundingRadius = std::max(component.boundingRadius, dist);
                }
                
                components.push_back(component);
            }
        }
        
        // Compute minimum distance between two components' boundary edges
        static Real ComputeComponentDistance(
            std::vector<Vector3<Real>> const& vertices,
            MeshComponent const& comp1,
            MeshComponent const& comp2,
            std::pair<int32_t, int32_t>& closestEdge1,
            std::pair<int32_t, int32_t>& closestEdge2)
        {
            Real minDistance = std::numeric_limits<Real>::max();
            
            // Early rejection using bounding spheres
            Real centerDist = Length(comp1.centroid - comp2.centroid);
            if (centerDist > comp1.boundingRadius + comp2.boundingRadius)
            {
                return minDistance;
            }
            
            // Find closest pair of boundary edges
            for (auto const& edge1 : comp1.boundaryEdges)
            {
                for (auto const& edge2 : comp2.boundaryEdges)
                {
                    // Compute minimum distance between edge endpoints
                    Real dist = std::min({
                        Length(vertices[edge1.first] - vertices[edge2.first]),
                        Length(vertices[edge1.first] - vertices[edge2.second]),
                        Length(vertices[edge1.second] - vertices[edge2.first]),
                        Length(vertices[edge1.second] - vertices[edge2.second])
                    });
                    
                    if (dist < minDistance)
                    {
                        minDistance = dist;
                        closestEdge1 = edge1;
                        closestEdge2 = edge2;
                    }
                }
            }
            
            return minDistance;
        }
        
        // Compute all candidate edge pairs between two components, sorted by distance
        static void FindCandidateEdgePairs(
            std::vector<Vector3<Real>> const& vertices,
            MeshComponent const& comp1,
            MeshComponent const& comp2,
            Real threshold,
            std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>>& candidates)
        {
            candidates.clear();
            
            // Early rejection using bounding spheres
            Real centerDist = Length(comp1.centroid - comp2.centroid);
            if (centerDist > comp1.boundingRadius + comp2.boundingRadius + threshold)
            {
                return;
            }
            
            // Find all edge pairs within threshold
            for (auto const& edge1 : comp1.boundaryEdges)
            {
                for (auto const& edge2 : comp2.boundaryEdges)
                {
                    // Compute minimum distance between edge endpoints
                    Real dist = std::min({
                        Length(vertices[edge1.first] - vertices[edge2.first]),
                        Length(vertices[edge1.first] - vertices[edge2.second]),
                        Length(vertices[edge1.second] - vertices[edge2.first]),
                        Length(vertices[edge1.second] - vertices[edge2.second])
                    });
                    
                    if (dist <= threshold)
                    {
                        candidates.push_back(std::make_tuple(dist, edge1, edge2));
                    }
                }
            }
            
            // Sort by distance (closest first)
            std::sort(candidates.begin(), candidates.end(),
                [](auto const& a, auto const& b) {
                    return std::get<0>(a) < std::get<0>(b);
                });
        }
        
        // Progressive component merging: largest component bridges to closest component iteratively
        static size_t ProgressiveComponentMerging(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            size_t totalBridges = 0;
            size_t maxAttempts = 100;  // Prevent infinite loops
            
            for (size_t attempt = 0; attempt < maxAttempts; ++attempt)
            {
                // Extract current components
                std::vector<MeshComponent> components;
                ExtractComponents(vertices, triangles, components);
                
                if (components.size() <= 1)
                {
                    if (verbose)
                    {
                        std::cout << "Single component achieved!\n";
                    }
                    break;
                }
                
                // Sort components by size (largest first)
                std::sort(components.begin(), components.end(),
                    [](MeshComponent const& a, MeshComponent const& b) {
                        return a.Size() > b.Size();
                    });
                
                if (verbose)
                {
                    std::cout << "Found " << components.size() << " components. Largest has " 
                              << components[0].Size() << " triangles, " 
                              << components[0].boundaryEdges.size() << " boundary edges\n";
                }
                
                // Skip if largest component has no boundary edges (closed component)
                if (components[0].boundaryEdges.empty())
                {
                    if (verbose)
                    {
                        std::cout << "Largest component is closed (no boundary edges). Cannot bridge.\n";
                    }
                    break;
                }
                
                // Try to find a component to bridge to
                bool bridgeCreated = false;
                
                // Sort other components by distance to largest
                std::vector<std::pair<Real, size_t>> componentsByDistance;
                for (size_t i = 1; i < components.size(); ++i)
                {
                    // Skip components with no boundary edges
                    if (components[i].boundaryEdges.empty())
                    {
                        continue;
                    }
                    
                    Real centerDist = Length(components[0].centroid - components[i].centroid);
                    componentsByDistance.push_back({centerDist, i});
                }
                
                std::sort(componentsByDistance.begin(), componentsByDistance.end());
                
                // Try bridging to each component in order of proximity
                for (auto const& distComp : componentsByDistance)
                {
                    size_t compIdx = distComp.second;
                    
                    // Find candidate edge pairs
                    std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> candidates;
                    FindCandidateEdgePairs(vertices, components[0], components[compIdx], threshold, candidates);
                    
                    if (candidates.empty())
                    {
                        continue;
                    }
                    
                    if (verbose)
                    {
                        std::cout << "Trying to bridge to component " << compIdx 
                                  << " (" << candidates.size() << " edge pairs within threshold)\n";
                    }
                    
                    // Try each edge pair until one succeeds
                    for (auto const& candidate : candidates)
                    {
                        auto const& edge1 = std::get<1>(candidate);
                        auto const& edge2 = std::get<2>(candidate);
                        Real dist = std::get<0>(candidate);
                        
                        if (BridgeBoundaryEdges(vertices, triangles, edge1, edge2, true))
                        {
                            totalBridges++;
                            bridgeCreated = true;
                            
                            if (verbose)
                            {
                                std::cout << "  Bridge created successfully at distance " << dist << "\n";
                            }
                            break;
                        }
                    }
                    
                    if (bridgeCreated)
                    {
                        break;
                    }
                }
                
                if (!bridgeCreated)
                {
                    if (verbose)
                    {
                        std::cout << "No valid bridges found. Stopping.\n";
                    }
                    break;
                }
            }
            
            return totalBridges;
        }
        
        // Optimized topology-aware component bridging with iterative multi-pass strategy
        static void TopologyAwareComponentBridging(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "\n=== Optimized Topology-Aware Component Bridging ===\n";
            }
            
            if (!params.enableIterativeBridging)
            {
                // Fallback to single-pass bridging
                TopologyAwareComponentBridgingSinglePass(vertices, triangles, params);
                return;
            }
            
            // Multi-pass iterative strategy: bridge → hole fill → repeat
            Real avgEdgeLength = EstimateAverageEdgeLength(vertices, triangles);
            Real currentThreshold = avgEdgeLength * params.initialBridgeThreshold;
            Real maxThreshold = avgEdgeLength * params.maxBridgeThreshold;
            
            bool isClosedManifold = false;
            bool hasNonManifold = false;
            size_t boundaryCount = 0;
            size_t componentCount = 0;
            size_t totalBridges = 0;
            size_t totalHolesFilled = 0;
            
            for (size_t iteration = 0; iteration < params.maxIterations; ++iteration)
            {
                if (params.verbose)
                {
                    std::cout << "\n--- Iteration " << (iteration + 1) << " ---\n";
                    std::cout << "Bridge threshold: " << currentThreshold << " (" 
                              << (currentThreshold / avgEdgeLength) << "x avg edge length)\n";
                }
                
                // Step 1: Progressive component merging (bridge largest to closest)
                size_t bridgesThisPass = ProgressiveComponentMerging(
                    vertices, triangles, currentThreshold, params.verbose);
                totalBridges += bridgesThisPass;
                
                if (params.verbose)
                {
                    std::cout << "Created " << bridgesThisPass << " bridges this pass\n";
                }
                
                // Step 2: Fill holes that bridging created or exposed
                size_t holesFilledThisPass = 0;
                if (params.enableHoleFilling)
                {
                    holesFilledThisPass = FillHolesConservative(
                        vertices, triangles, params, params.verbose);
                    totalHolesFilled += holesFilledThisPass;
                    
                    if (params.verbose && holesFilledThisPass > 0)
                    {
                        std::cout << "Filled " << holesFilledThisPass << " holes this pass\n";
                    }
                }
                
                // Step 3: Check progress
                ValidateManifoldDetailed(triangles, isClosedManifold, hasNonManifold, 
                                       boundaryCount, componentCount);
                
                if (params.verbose)
                {
                    std::cout << "Progress: " << triangles.size() << " triangles, "
                              << componentCount << " components, "
                              << boundaryCount << " boundary edges\n";
                }
                
                // Check termination conditions
                if (isClosedManifold)
                {
                    if (params.verbose)
                    {
                        std::cout << "Achieved closed manifold!\n";
                    }
                    break;
                }
                
                if (params.targetSingleComponent && componentCount == 1)
                {
                    if (params.verbose)
                    {
                        std::cout << "Achieved single connected component!\n";
                    }
                    break;
                }
                
                // No progress this iteration - increase threshold
                if (bridgesThisPass == 0 && holesFilledThisPass == 0)
                {
                    currentThreshold *= static_cast<Real>(1.5);
                    
                    if (currentThreshold > maxThreshold)
                    {
                        if (params.verbose)
                        {
                            std::cout << "Reached maximum threshold - stopping\n";
                        }
                        break;
                    }
                }
            }
            
            // Final pass: Cap any remaining small boundary loops
            std::vector<BoundaryLoop> loops;
            ExtractBoundaryLoops(vertices, triangles, loops);
            
            if (!loops.empty() && params.verbose)
            {
                std::cout << "\nCapping " << loops.size() << " remaining boundary loops...\n";
            }
            
            size_t cappedLoops = 0;
            for (auto const& loop : loops)
            {
                if (loop.vertices.size() <= 10)  // Only cap small loops
                {
                    if (CapBoundaryLoop(vertices, triangles, loop, true))
                    {
                        cappedLoops++;
                    }
                }
            }
            
            // Final validation
            ValidateManifoldDetailed(triangles, isClosedManifold, hasNonManifold, 
                                   boundaryCount, componentCount);
            
            if (params.verbose)
            {
                std::cout << "\n=== Final Results ===\n";
                std::cout << "Total bridges created: " << totalBridges << "\n";
                std::cout << "Total holes filled: " << totalHolesFilled << "\n";
                std::cout << "Loops capped: " << cappedLoops << "\n";
                std::cout << "Triangles: " << triangles.size() << "\n";
                std::cout << "Boundary edges: " << boundaryCount << "\n";
                std::cout << "Connected components: " << componentCount << "\n";
                std::cout << "Non-manifold edges: " << (hasNonManifold ? "YES" : "NO") << "\n";
                std::cout << "Closed manifold: " << (isClosedManifold ? "YES" : "NO") << "\n";
            }
        }
        
        // Optimized boundary edge bridging with spatial indexing
        static size_t BridgeBoundaryEdgesOptimized(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            // Extract boundary edges
            std::map<std::pair<int32_t, int32_t>, EdgeType> edgeTypes;
            std::vector<std::pair<int32_t, int32_t>> dummy;
            AnalyzeEdgeTopology(triangles, edgeTypes, dummy);
            
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;
            for (auto const& et : edgeTypes)
            {
                if (et.second == EdgeType::Boundary)
                {
                    boundaryEdges.push_back(et.first);
                }
            }
            
            if (boundaryEdges.empty())
            {
                return 0;
            }
            
            // Build spatial index: simple grid-based approach for O(n) average case
            // Group boundary edges by spatial grid cell
            Real cellSize = threshold;
            std::map<std::array<int, 3>, std::vector<size_t>> grid;
            
            for (size_t i = 0; i < boundaryEdges.size(); ++i)
            {
                auto const& edge = boundaryEdges[i];
                Vector3<Real> midpoint = (vertices[edge.first] + vertices[edge.second]) * static_cast<Real>(0.5);
                
                std::array<int, 3> cell = {
                    static_cast<int>(std::floor(midpoint[0] / cellSize)),
                    static_cast<int>(std::floor(midpoint[1] / cellSize)),
                    static_cast<int>(std::floor(midpoint[2] / cellSize))
                };
                
                grid[cell].push_back(i);
            }
            
            // Find candidate edge pairs using spatial grid
            std::vector<std::pair<size_t, size_t>> candidatePairs;
            
            for (auto const& cell : grid)
            {
                auto const& edgesInCell = cell.second;
                
                // Check pairs within this cell
                for (size_t i = 0; i < edgesInCell.size(); ++i)
                {
                    for (size_t j = i + 1; j < edgesInCell.size(); ++j)
                    {
                        candidatePairs.push_back({edgesInCell[i], edgesInCell[j]});
                    }
                }
                
                // Check neighboring cells (27-connectivity)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dz = -1; dz <= 1; ++dz)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            
                            std::array<int, 3> neighborCell = {
                                cell.first[0] + dx,
                                cell.first[1] + dy,
                                cell.first[2] + dz
                            };
                            
                            auto it = grid.find(neighborCell);
                            if (it != grid.end())
                            {
                                for (size_t i : edgesInCell)
                                {
                                    for (size_t j : it->second)
                                    {
                                        if (i < j)
                                        {
                                            candidatePairs.push_back({i, j});
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Sort pairs by distance (closest first)
            std::sort(candidatePairs.begin(), candidatePairs.end(),
                [&](auto const& p1, auto const& p2) {
                    auto const& e1a = boundaryEdges[p1.first];
                    auto const& e1b = boundaryEdges[p1.second];
                    auto const& e2a = boundaryEdges[p2.first];
                    auto const& e2b = boundaryEdges[p2.second];
                    
                    Real minDist1 = std::numeric_limits<Real>::max();
                    minDist1 = std::min(minDist1, Length(vertices[e1a.first] - vertices[e1b.first]));
                    minDist1 = std::min(minDist1, Length(vertices[e1a.first] - vertices[e1b.second]));
                    minDist1 = std::min(minDist1, Length(vertices[e1a.second] - vertices[e1b.first]));
                    minDist1 = std::min(minDist1, Length(vertices[e1a.second] - vertices[e1b.second]));
                    
                    Real minDist2 = std::numeric_limits<Real>::max();
                    minDist2 = std::min(minDist2, Length(vertices[e2a.first] - vertices[e2b.first]));
                    minDist2 = std::min(minDist2, Length(vertices[e2a.first] - vertices[e2b.second]));
                    minDist2 = std::min(minDist2, Length(vertices[e2a.second] - vertices[e2b.first]));
                    minDist2 = std::min(minDist2, Length(vertices[e2a.second] - vertices[e2b.second]));
                    
                    return minDist1 < minDist2;
                });
            
            // Try to bridge pairs (closest first)
            size_t bridgesCreated = 0;
            std::set<size_t> bridgedEdges;  // Track which edges have been bridged
            
            for (auto const& pair : candidatePairs)
            {
                // Skip if either edge already bridged
                if (bridgedEdges.count(pair.first) || bridgedEdges.count(pair.second))
                {
                    continue;
                }
                
                auto const& e1 = boundaryEdges[pair.first];
                auto const& e2 = boundaryEdges[pair.second];
                
                // Check if edges are close enough
                Real minDist = std::numeric_limits<Real>::max();
                minDist = std::min(minDist, Length(vertices[e1.first] - vertices[e2.first]));
                minDist = std::min(minDist, Length(vertices[e1.first] - vertices[e2.second]));
                minDist = std::min(minDist, Length(vertices[e1.second] - vertices[e2.first]));
                minDist = std::min(minDist, Length(vertices[e1.second] - vertices[e2.second]));
                
                if (minDist < threshold)
                {
                    if (BridgeBoundaryEdges(vertices, triangles, e1, e2, true))
                    {
                        bridgesCreated++;
                        bridgedEdges.insert(pair.first);
                        bridgedEdges.insert(pair.second);
                        
                        if (verbose && bridgesCreated <= 10)
                        {
                            std::cout << "  Bridge " << bridgesCreated 
                                      << ": connected edges at distance " << minDist << "\n";
                        }
                    }
                }
            }
            
            return bridgesCreated;
        }
        
        // Conservative hole filling that validates each fill
        static size_t FillHolesConservative(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            bool verbose = false)
        {
            std::vector<BoundaryLoop> loops;
            ExtractBoundaryLoops(vertices, triangles, loops);
            
            size_t holesFilledCount = 0;
            
            for (auto const& loop : loops)
            {
                // Only fill reasonably sized loops
                if (loop.vertices.size() < 3 || loop.vertices.size() > params.maxHoleEdges)
                {
                    continue;
                }
                
                // Check area if limit is set
                if (params.maxHoleArea > static_cast<Real>(0))
                {
                    // Estimate area from perimeter (rough approximation)
                    Real estimatedArea = loop.perimeter * loop.perimeter / static_cast<Real>(4 * 3.14159265);
                    if (estimatedArea > params.maxHoleArea)
                    {
                        continue;
                    }
                }
                
                // Try to fill the hole
                size_t oldTriCount = triangles.size();
                
                // Use MeshHoleFilling if available, otherwise try simple fan
                bool filled = FillSingleHole(vertices, triangles, loop, params.holeFillingMethod);
                
                if (filled)
                {
                    // Validate manifold property
                    if (ValidateManifold(triangles))
                    {
                        holesFilledCount++;
                        
                        if (verbose && holesFilledCount <= 10)
                        {
                            std::cout << "  Filled hole with " << loop.vertices.size() 
                                      << " vertices (" << (triangles.size() - oldTriCount) 
                                      << " triangles)\n";
                        }
                    }
                    else
                    {
                        // Revert if non-manifold
                        triangles.resize(oldTriCount);
                    }
                }
            }
            
            return holesFilledCount;
        }
        
        // Simple hole filling for a single loop
        static bool FillSingleHole(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& loop,
            typename MeshHoleFilling<Real>::TriangulationMethod method)
        {
            if (loop.vertices.size() < 3)
            {
                return false;
            }
            
            // For small holes, use simple fan triangulation
            if (loop.vertices.size() <= 5)
            {
                // Add centroid
                int32_t centroidIdx = static_cast<int32_t>(vertices.size());
                vertices.push_back(loop.centroid);
                
                // Create fan
                for (size_t i = 0; i < loop.vertices.size(); ++i)
                {
                    size_t j = (i + 1) % loop.vertices.size();
                    triangles.push_back({loop.vertices[i], loop.vertices[j], centroidIdx});
                }
                
                return true;
            }
            
            // For larger holes, would use MeshHoleFilling here
            // For now, skip large holes
            return false;
        }
        
        // Single-pass bridging (legacy fallback)
        static void TopologyAwareComponentBridgingSinglePass(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params)
        {
            Real avgEdgeLength = EstimateAverageEdgeLength(vertices, triangles);
            Real threshold = avgEdgeLength * static_cast<Real>(3);
            
            size_t bridges = BridgeBoundaryEdgesOptimized(vertices, triangles, threshold, params.verbose);
            
            if (params.verbose)
            {
                std::cout << "Single-pass bridging created " << bridges << " bridges\n";
            }
        }
    };
}

#endif // GTE_MATHEMATICS_CO3NE_MANIFOLD_STITCHER_H
