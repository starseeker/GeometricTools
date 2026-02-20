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
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>

namespace gte
{
    // Hash for std::pair<int32_t, int32_t> edges – used by all edge count maps
    // in Co3NeManifoldStitcher to give O(1) average lookups instead of O(log n).
    struct EdgePairHash
    {
        size_t operator()(std::pair<int32_t, int32_t> const& p) const noexcept
        {
            // boost::hash_combine style mixing for two 32-bit integers.
            // The constants (2654435761, 0x9e3779b9) are derived from the
            // golden ratio and are widely used for this purpose.
            size_t h = static_cast<size_t>(static_cast<uint32_t>(p.first));
            h ^= static_cast<size_t>(static_cast<uint32_t>(p.second)) * 2654435761ULL
                 + 0x9e3779b9ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

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
            bool removeSmallClosedComponents;  // Remove small closed sub-meshes (artifacts)
            size_t smallClosedComponentThreshold;  // Max triangles for "small" closed component
            bool verbose;
            
            Parameters()
                : enableHoleFilling(true)
                , maxHoleArea(static_cast<Real>(0))  // No limit
                , maxHoleEdges(100)
                , holeFillingMethod(MeshHoleFilling<Real>::TriangulationMethod::CDT)
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
                // 20 iterations handles log₂(~1M) initial components; sufficient for
                // all tested datasets while remaining practical in runtime.
                , maxIterations(20)
                , initialBridgeThreshold(static_cast<Real>(2.0))
                , maxBridgeThreshold(static_cast<Real>(10.0))
                , targetSingleComponent(true)
                , removeNonManifoldEdges(true)
                , removeSmallClosedComponents(true)
                , smallClosedComponentThreshold(20)  // Remove closed sub-meshes ≤20 triangles
                , verbose(false)
            {
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
            
            // Step 1: Analyze mesh topology.
            // For non-verbose mode we only need the non-manifold edge list (for Step 2),
            // so avoid building the expensive std::map output in that case.
            std::map<std::pair<int32_t, int32_t>, EdgeType> edgeTypes;
            std::vector<std::pair<int32_t, int32_t>> nonManifoldEdges;
            if (params.verbose)
            {
                AnalyzeEdgeTopology(triangles, edgeTypes, nonManifoldEdges);

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
            else
            {
                FindNonManifoldEdges(triangles, nonManifoldEdges);
            }
            
            // Step 2: If enabled, remove non-manifold edges first
            if (params.removeNonManifoldEdges && !nonManifoldEdges.empty())
            {
                size_t removed = RemoveNonManifoldEdges(triangles, nonManifoldEdges);
                
                if (params.verbose)
                {
                    std::cout << "Removed " << removed << " triangles to fix non-manifold edges\n";
                }
                
                // Re-analyze after removal (only need the full map when verbose)
                edgeTypes.clear();
                nonManifoldEdges.clear();
                if (params.verbose)
                {
                    AnalyzeEdgeTopology(triangles, edgeTypes, nonManifoldEdges);
                }
                else
                {
                    FindNonManifoldEdges(triangles, nonManifoldEdges);
                }
            }
            
            // Step 3: Identify connected patches (diagnostic/verbose only — patchPairs is
            // never used downstream, so skip entirely in non-verbose mode).
            if (params.verbose)
            {
                std::vector<Patch> patches;
                IdentifyPatches(vertices, triangles, edgeTypes, patches);

                std::cout << "Found " << patches.size() << " connected patches\n";
                size_t closedPatches = 0;
                for (size_t i = 0; i < patches.size(); ++i)
                {
                    std::cout << "  Patch " << i << ": "
                              << patches[i].triangleIndices.size() << " triangles, "
                              << patches[i].boundaryEdges.size() << " boundary edges, "
                              << (patches[i].isManifold ? "manifold" : "non-manifold")
                              << (patches[i].isClosed ? ", closed" : ", open") << "\n";
                    if (patches[i].isClosed) closedPatches++;
                }
                std::cout << "  " << closedPatches << " closed patches (no boundaries)\n";
                std::cout << "  " << (patches.size() - closedPatches) << " open patches (have boundaries)\n";

                // Step 4a: Analyze patch pairs to identify stitching opportunities
                std::vector<PatchPair> patchPairs;
                if (patches.size() > 1)
                {
                    Real avgEdgeLength = EstimateAverageEdgeLength(vertices, triangles);
                    Real maxPairDistance = avgEdgeLength * static_cast<Real>(3);
                    AnalyzePatchPairs(vertices, patches, patchPairs, maxPairDistance);

                    if (!patchPairs.empty())
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
            }  // end verbose Steps 3 & 4a

            // Step 3b: Remove small closed components that are Co3Ne reconstruction
            // artifacts (e.g. the 8-triangle octahedral fragments produced for dense
            // local point clusters).  These are identified early — before any hole
            // filling — because genuine closed surfaces added later by the hole
            // fillers should not be treated as artifacts.
            if (params.removeSmallClosedComponents
                && params.smallClosedComponentThreshold > 0)
            {
                RemoveSmallClosedComponents(triangles,
                    params.smallClosedComponentThreshold, params.verbose);
            }

            // Step 4: Fill holes using existing hole filling (with validation)
            if (params.enableHoleFilling)
            {
                auto t4start = std::chrono::steady_clock::now();
                typename MeshHoleFilling<Real>::Parameters holeParams;
                holeParams.maxArea = params.maxHoleArea;
                holeParams.maxEdges = params.maxHoleEdges;
                holeParams.method = params.holeFillingMethod;
                holeParams.repair = true;
                
                size_t trianglesBefore = triangles.size();
                MeshHoleFilling<Real>::FillHoles(vertices, triangles, holeParams);
                size_t trianglesAfter = triangles.size();
                
                // Validate no non-manifold edges were created
                std::vector<std::pair<int32_t, int32_t>> checkNonManifold;
                FindNonManifoldEdges(triangles, checkNonManifold);
                
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
                if (params.verbose)
                {
                    auto t4ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t4start).count();
                    std::cout << "  [Profiling] Step 4 (hole fill): " << t4ms << " ms\n";
                }
            }
            
            // Step 5: Adaptive Mesh Hole Filler (iterative ear-clipping with detria fallback)
            if (params.enableBallPivotHoleFiller)
            {
                auto t5start = std::chrono::steady_clock::now();
                if (params.verbose)
                {
                    std::cout << "[Stitcher] Step 5: Adaptive Mesh Hole Filler\n";
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
                    std::cout << "  Adaptive hole filler: " 
                              << (holeFilled ? "SUCCESS" : "NO CHANGES") 
                              << ", added " << (trianglesAfter - trianglesBefore) 
                              << " triangles\n";
                    auto t5ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t5start).count();
                    std::cout << "  [Profiling] Step 5 (ball pivot hole fill): " << t5ms << " ms\n";
                }
            }
            
            // Step 6: Final hole filling (conservative, not aggressive)
            if (params.enableHoleFilling)
            {
                auto t6start = std::chrono::steady_clock::now();
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
                std::vector<std::pair<int32_t, int32_t>> checkNonManifoldFinal;
                FindNonManifoldEdges(triangles, checkNonManifoldFinal);
                
                if (!checkNonManifoldFinal.empty())
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
                if (params.verbose)
                {
                    auto t6ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t6start).count();
                    std::cout << "  [Profiling] Step 6 (final hole fill): " << t6ms << " ms\n";
                }
            }
            
            // Step 7: Topology-Aware Component Bridging with RTree-accelerated gap detection
            {
                auto t7start = std::chrono::steady_clock::now();
                TopologyAwareComponentBridging(vertices, triangles, params);
                if (params.verbose)
                {
                    auto t7ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t7start).count();
                    std::cout << "  [Profiling] Step 7 (component bridging): " << t7ms << " ms\n";
                }
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
            // Count triangle occurrences for each edge using unordered_map (O(T) avg).
            EdgeCountMap edgeCounts;
            edgeCounts.reserve(triangles.size() * 3);

            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    edgeCounts[MakeEdgeKey(tri[i], tri[(i + 1) % 3])]++;
                }
            }

            // Classify edges and populate the caller's std::map output.
            for (auto const& [edge, count] : edgeCounts)
            {
                if (count == 1)
                {
                    edgeTypes[edge] = EdgeType::Boundary;
                }
                else if (count == 2)
                {
                    edgeTypes[edge] = EdgeType::Interior;
                }
                else  // >= 3
                {
                    edgeTypes[edge] = EdgeType::NonManifold;
                    nonManifoldEdges.push_back(edge);
                }
            }
        }

        // Lightweight variant that only identifies non-manifold edges (count >= 3).
        // Avoids the expensive std::map output entirely — O(T) with hash map.
        static void FindNonManifoldEdges(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<std::pair<int32_t, int32_t>>& nonManifoldEdges)
        {
            EdgeCountMap edgeCounts;
            edgeCounts.reserve(triangles.size() * 3);
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    edgeCounts[MakeEdgeKey(tri[i], tri[(i + 1) % 3])]++;
                }
            }
            for (auto const& [edge, count] : edgeCounts)
            {
                if (count >= 3) nonManifoldEdges.push_back(edge);
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
        // Uses a centroid KD-tree to reduce from O(P²) to O(P·k) when
        // maxDistance > 0 provides a meaningful spatial cutoff.
        static void AnalyzePatchPairs(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<Patch> const& patches,
            std::vector<PatchPair>& patchPairs,
            Real maxDistance = static_cast<Real>(0))
        {
            patchPairs.clear();

            if (patches.empty()) return;

            // When maxDistance is provided we can use a centroid spatial index to
            // skip pairs that are definitely beyond the distance cutoff.
            // Fall back to the O(P²) brute-force only when maxDistance == 0.
            if (maxDistance > static_cast<Real>(0))
            {
                using CentroidSite = PositionSite<3, Real>;
                std::vector<CentroidSite> centroidSites;
                centroidSites.reserve(patches.size());
                for (auto const& p : patches)
                {
                    centroidSites.emplace_back(p.centroid);
                }

                // The centroid-pair cutoff used in the original code was 5×maxDistance.
                Real searchRadius = maxDistance * static_cast<Real>(5);

                static constexpr int32_t maxLeafSize = 10;
                static constexpr int32_t maxLevel = 20;
                NearestNeighborQuery<3, Real, CentroidSite> centroidTree(
                    centroidSites, maxLeafSize, maxLevel);

                constexpr int32_t MaxNearby = 512;
                std::array<int32_t, MaxNearby> nearbyIndices;

                for (size_t i = 0; i < patches.size(); ++i)
                {
                    int32_t numNearby = centroidTree.template FindNeighbors<MaxNearby>(
                        patches[i].centroid, searchRadius, nearbyIndices);

                    for (int32_t kk = 0; kk < numNearby; ++kk)
                    {
                        size_t j = static_cast<size_t>(nearbyIndices[kk]);
                        if (j <= i) continue;  // Process each pair once

                        PatchPair pair;
                        pair.patch1Index = i;
                        pair.patch2Index = j;

                        // Collect unique boundary vertices for each patch
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
                                if (dist < maxDistance) pair.closeBoundaryVertices++;
                            }
                        }

                        if (pair.minDistance < maxDistance)
                        {
                            patchPairs.push_back(pair);
                        }
                    }
                }
            }
            else
            {
                // Brute-force O(P²) fallback when no spatial cutoff is available
                for (size_t i = 0; i < patches.size(); ++i)
                {
                    for (size_t j = i + 1; j < patches.size(); ++j)
                    {
                        PatchPair pair;
                        pair.patch1Index = i;
                        pair.patch2Index = j;

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
                            }
                        }

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
        
        // ======================================================================
        // Public helpers: types and local-check utilities exposed for testing
        // and for callers that wish to maintain a persistent EdgeCountMap.
        // ======================================================================
        public:

        // Persistent edge count map used to avoid O(T) full-mesh rebuilds.
        // An entry with count 1 = boundary edge, count 2 = interior, count > 2 = non-manifold.
        using EdgeCountMap = std::unordered_map<std::pair<int32_t, int32_t>, size_t, EdgePairHash>;

        // Canonical edge key: always (min, max) so direction doesn't matter.
        static std::pair<int32_t, int32_t> MakeEdgeKey(int32_t a, int32_t b) noexcept
        {
            return {std::min(a, b), std::max(a, b)};
        }

        // Build an edge count map for the entire triangle list in O(T).
        static EdgeCountMap BuildEdgeCountMap(
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            EdgeCountMap ecm;
            ecm.reserve(triangles.size() * 3);
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    ecm[MakeEdgeKey(tri[i], tri[(i + 1) % 3])]++;
                }
            }
            return ecm;
        }

        // Count existing occurrences of only the edges in the given set.
        // O(T * |targetEdges|) but with a very small constant when |targetEdges| <= 6.
        // Used by the legacy BridgeBoundaryEdges(validate=true) path so it does not
        // need to build a full O(T)-entry ECM just to check 6 edges.
        static EdgeCountMap BuildEdgeCountMapFor(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::array<std::pair<int32_t, int32_t>, 6> const& targetEdges,
            int numTargetEdges)
        {
            EdgeCountMap ecm;
            ecm.reserve(numTargetEdges);
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    auto key = MakeEdgeKey(tri[i], tri[(i + 1) % 3]);
                    for (int k = 0; k < numTargetEdges; ++k)
                    {
                        if (key == targetEdges[k])
                        {
                            ecm[key]++;
                            break;
                        }
                    }
                }
            }
            return ecm;
        }

        // Validate that the mesh is manifold: no edge has more than 2 incident triangles.
        // Uses an unordered_map for O(T) average time (was O(T log T) with std::map).
        static bool ValidateManifold(std::vector<std::array<int32_t, 3>> const& triangles)
        {
            EdgeCountMap ecm;
            ecm.reserve(triangles.size() * 3);
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    if (++ecm[MakeEdgeKey(v0, v1)] > 2)
                    {
                        return false;  // Non-manifold edge found early-out
                    }
                }
            }
            return true;
        }

        // Local manifold check for adding exactly two bridge triangles.
        // Returns true if both triangles can be added without creating a non-manifold
        // edge (count > 2).  If valid, appends the triangles and updates ecm in-place;
        // otherwise leaves both triangles and ecm unchanged.
        //
        // This is the key performance fix: avoids the O(T) full-mesh rebuild that the
        // old validate-after-append approach required.
        static bool ValidateBridgeLocal(
            std::vector<std::array<int32_t, 3>>& triangles,
            std::array<int32_t, 3> const& tri1,
            std::array<int32_t, 3> const& tri2,
            EdgeCountMap& ecm)
        {
            // Collect the 6 edges from the two new triangles using std::array for type safety
            std::array<std::pair<int32_t, int32_t>, 6> edges;
            int ne = 0;
            for (auto const& tri : {tri1, tri2})
            {
                for (int i = 0; i < 3; ++i)
                {
                    edges[ne++] = MakeEdgeKey(tri[i], tri[(i + 1) % 3]);
                }
            }

            // Use size_t consistently for counts to match EdgeCountMap.
            // Temporary delta accumulates how many times each edge appears
            // among the 6 new edges (handles degenerate cases where two of
            // the six edges are identical).
            std::unordered_map<std::pair<int32_t,int32_t>, size_t, EdgePairHash> delta;
            delta.reserve(6);
            for (auto const& e : edges)
            {
                delta[e]++;
            }
            for (auto const& [e, add] : delta)
            {
                auto it = ecm.find(e);
                size_t cur = (it != ecm.end()) ? it->second : 0;
                if (cur + add > 2)
                {
                    return false;
                }
            }

            // Valid – commit
            triangles.push_back(tri1);
            triangles.push_back(tri2);
            for (auto const& [e, add] : delta)
            {
                ecm[e] += add;
            }
            return true;
        }

        // Detailed manifold validation: O(T) time using hash maps and a proper
        // edge-adjacency BFS (was O(T²) due to the nested-loop BFS approach).
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

            
            // Build edge count map and edge-to-triangles adjacency simultaneously
            // in a single O(T) pass (was O(T²) due to the nested-loop BFS below).
            EdgeCountMap edgeCounts;
            std::unordered_map<std::pair<int32_t,int32_t>,
                               std::vector<int32_t>, EdgePairHash> edgeToTris;
            edgeCounts.reserve(triangles.size() * 3);
            edgeToTris.reserve(triangles.size() * 3);

            for (int32_t ti = 0; ti < static_cast<int32_t>(triangles.size()); ++ti)
            {
                auto const& tri = triangles[ti];
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    edgeCounts[edge]++;
                    edgeToTris[edge].push_back(ti);
                }
            }
            
            // Analyze edges
            for (auto const& [edge, cnt] : edgeCounts)
            {
                if (cnt == 1)
                {
                    boundaryEdgeCount++;
                }
                else if (cnt > 2)
                {
                    hasNonManifoldEdges = true;
                }
            }
            
            // Count connected components using O(T) edge-adjacency BFS.
            // Each triangle is visited at most once; each edge lookup is O(1).
            std::vector<bool> visited(triangles.size(), false);
            componentCount = 0;
            
            for (size_t startIdx = 0; startIdx < triangles.size(); ++startIdx)
            {
                if (visited[startIdx])
                {
                    continue;
                }
                
                componentCount++;
                std::vector<int32_t> queue;
                queue.push_back(static_cast<int32_t>(startIdx));
                visited[startIdx] = true;
                
                for (size_t qi = 0; qi < queue.size(); ++qi)
                {
                    int32_t currentIdx = queue[qi];
                    auto const& currentTri = triangles[currentIdx];
                    
                    for (int i = 0; i < 3; ++i)
                    {
                        int32_t v0 = currentTri[i];
                        int32_t v1 = currentTri[(i + 1) % 3];
                        auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                        
                        auto it = edgeToTris.find(edge);
                        if (it == edgeToTris.end()) continue;
                        
                        for (int32_t neighborIdx : it->second)
                        {
                            if (!visited[neighborIdx])
                            {
                                visited[neighborIdx] = true;
                                queue.push_back(neighborIdx);
                            }
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

            // Build boundary adjacency directly from the ECM in one O(T) pass,
            // without going through the full std::map-based AnalyzeEdgeTopology.
            EdgeCountMap ecm = BuildEdgeCountMap(triangles);

            std::unordered_map<int32_t, std::vector<int32_t>> boundaryAdj;
            for (auto const& [edge, count] : ecm)
            {
                if (count == 1)
                {
                    boundaryAdj[edge.first].push_back(edge.second);
                    boundaryAdj[edge.second].push_back(edge.first);
                }
            }

            // Extract loops using O(1) unordered_set visited tracking.
            std::unordered_set<int32_t> visited;
            visited.reserve(boundaryAdj.size());

            for (auto const& [startVertex, _] : boundaryAdj)
            {
                if (visited.count(startVertex)) continue;

                BoundaryLoop loop;
                loop.vertices.push_back(startVertex);
                visited.insert(startVertex);

                int32_t current = startVertex;
                int32_t previous = -1;

                while (true)
                {
                    int32_t next = -1;
                    auto it = boundaryAdj.find(current);
                    if (it != boundaryAdj.end())
                    {
                        for (int32_t neighbor : it->second)
                        {
                            if (neighbor != previous)
                            {
                                next = neighbor;
                                break;
                            }
                        }
                    }

                    if (next == -1 || next == startVertex)
                    {
                        break;
                    }

                    if (visited.count(next))
                    {
                        break;
                    }

                    loop.vertices.push_back(next);
                    visited.insert(next);
                    previous = current;
                    current = next;
                }

                if (loop.vertices.size() >= 3)
                {
                    // Verify the loop is truly closed: the edge connecting the last
                    // vertex back to the first must exist as a boundary edge (count==1
                    // in the ECM).  A premature traversal stop (e.g. due to a shared
                    // "pinch-point" vertex) produces an open chain whose virtual
                    // closing edge is absent; such chains must be discarded because
                    // ValidateFanLocal would reject them anyway.
                    auto closingEdge = MakeEdgeKey(
                        loop.vertices.back(), loop.vertices.front());
                    auto itClose = ecm.find(closingEdge);
                    if (itClose == ecm.end() || itClose->second != 1)
                    {
                        continue;  // Non-closed partial chain — skip
                    }

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
        
        // BridgeBoundaryEdges overload 1: legacy path.
        // When validate=true, builds a minimal local ECM from only the new edges
        // rather than scanning the full mesh.
        //
        // When edge1 and edge2 share a vertex the standard two-triangle bridge creates
        // a degenerate triangle.  Fall back to a single fan triangle in that case.
        static bool BridgeBoundaryEdges(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::pair<int32_t, int32_t> const& edge1,
            std::pair<int32_t, int32_t> const& edge2,
            bool validate = true)
        {
            // Create two triangles to bridge edge1 and edge2
            // edge1: (a, b), edge2: (c, d)
            
            int32_t a = edge1.first;
            int32_t b = edge1.second;
            int32_t c = edge2.first;
            int32_t d = edge2.second;

            // When the two edges share exactly one vertex, use a single triangle
            // rather than the standard two-triangle quad bridge to avoid degeneracy.
            auto TryOneTriLegacy =
                [&](std::array<int32_t, 3> const& tri) -> bool
            {
                if (!validate)
                {
                    triangles.push_back(tri);
                    return true;
                }
                std::array<std::pair<int32_t, int32_t>, 3> edges3 = {
                    MakeEdgeKey(tri[0], tri[1]),
                    MakeEdgeKey(tri[1], tri[2]),
                    MakeEdgeKey(tri[0], tri[2])
                };
                // BuildEdgeCountMapFor accepts a 6-slot array; pad with sentinel
                // {-1,-1} entries that cannot match any real edge.
                EdgeCountMap localEcm =
                    BuildEdgeCountMapFor(triangles,
                        {edges3[0], edges3[1], edges3[2], {-1,-1}, {-1,-1}, {-1,-1}}, 3);
                for (auto const& e : edges3)
                {
                    auto it = localEcm.find(e);
                    size_t cur = (it != localEcm.end()) ? it->second : 0;
                    if (cur + 1 > 2) return false;
                }
                triangles.push_back(tri);
                return true;
            };

            // When the two edges share exactly one vertex, use a single triangle
            // rather than the standard two-triangle quad bridge to avoid degeneracy.
            // The shared vertex is placed in the middle position so the free endpoints
            // of edge1 and edge2 flank it, preserving a consistent winding order:
            //   a==c → {b, a, d}  closes (a,b) and (a,d), opens (b,d)
            //   a==d → {b, a, c}  closes (a,b) and (a,c), opens (b,c)
            //   b==c → {a, b, d}  closes (a,b) and (b,d), opens (a,d)
            //   b==d → {a, b, c}  closes (a,b) and (b,c), opens (a,c)
            if (a == c) return TryOneTriLegacy({b, a, d});
            if (a == d) return TryOneTriLegacy({b, a, c});
            if (b == c) return TryOneTriLegacy({a, b, d});
            if (b == d) return TryOneTriLegacy({a, b, c});

            // Check edge orientations and distances
            Real dist_ac = Length(vertices[c] - vertices[a]);
            Real dist_bd = Length(vertices[d] - vertices[b]);
            Real dist_ad = Length(vertices[d] - vertices[a]);
            Real dist_bc = Length(vertices[c] - vertices[b]);
            
            // Choose better pairing
            std::array<int32_t, 3> tri1, tri2;
            if (dist_ac + dist_bd < dist_ad + dist_bc)
            {
                tri1 = {a, b, c};
                tri2 = {b, d, c};
            }
            else
            {
                tri1 = {a, b, d};
                tri2 = {a, d, c};
            }
            
            if (validate)
            {
                // Build a targeted ECM for ONLY the 6 edges of the two new triangles
                // by scanning existing triangles once.  This is still O(T) but avoids
                // allocating a full T-entry map — only 6 entries are ever created.
                std::array<std::pair<int32_t,int32_t>, 6> checkEdges;
                int ne = 0;
                for (auto const& t : {tri1, tri2})
                    for (int i = 0; i < 3; ++i)
                        checkEdges[ne++] = MakeEdgeKey(t[i], t[(i+1)%3]);

                EdgeCountMap localEcm = BuildEdgeCountMapFor(triangles, checkEdges, 6);
                return ValidateBridgeLocal(triangles, tri1, tri2, localEcm);
            }

            // No validation requested — just append.
            triangles.push_back(tri1);
            triangles.push_back(tri2);
            return true;
        }

        // Overload 2: fast path – validates only the 6 new edges against ecm (O(1)).
        // Updates ecm and appends to triangles only on success.
        //
        // When edge1 and edge2 share a vertex (components connected at a point but
        // not at an edge), the standard two-triangle bridge would produce a degenerate
        // triangle with a repeated vertex, causing ValidateBridgeLocal to reject it
        // (the shared diagonal edge appears three times, making add=3).  In that case
        // we fall back to a single fan triangle at the shared vertex, which correctly
        // closes both boundary edges and leaves a new boundary edge between the two
        // free endpoints.
        static bool BridgeBoundaryEdges(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::pair<int32_t, int32_t> const& edge1,
            std::pair<int32_t, int32_t> const& edge2,
            EdgeCountMap& ecm)
        {
            int32_t a = edge1.first;
            int32_t b = edge1.second;
            int32_t c = edge2.first;
            int32_t d = edge2.second;

            // Helper: validate and commit a single triangle.
            auto TryOneTri = [&](std::array<int32_t, 3> const& tri) -> bool
            {
                std::array<std::pair<int32_t, int32_t>, 3> edges3 = {
                    MakeEdgeKey(tri[0], tri[1]),
                    MakeEdgeKey(tri[1], tri[2]),
                    MakeEdgeKey(tri[0], tri[2])
                };
                for (auto const& e : edges3)
                {
                    auto it = ecm.find(e);
                    size_t cur = (it != ecm.end()) ? it->second : 0;
                    if (cur + 1 > 2) return false;
                }
                triangles.push_back(tri);
                for (auto const& e : edges3) ecm[e]++;
                return true;
            };

            // When the two edges share exactly one vertex, use a single triangle
            // rather than the two-triangle quad bridge.
            // When the two edges share exactly one vertex, use a single triangle
            // rather than the standard two-triangle quad bridge to avoid degeneracy.
            // The shared vertex is placed in the middle position so the free endpoints
            // of edge1 and edge2 flank it, preserving a consistent winding order:
            //   a==c → {b, a, d}  closes (a,b) and (a,d), opens (b,d)
            //   a==d → {b, a, c}  closes (a,b) and (a,c), opens (b,c)
            //   b==c → {a, b, d}  closes (a,b) and (b,d), opens (a,d)
            //   b==d → {a, b, c}  closes (a,b) and (b,c), opens (a,c)
            if (a == c) return TryOneTri({b, a, d});
            if (a == d) return TryOneTri({b, a, c});
            if (b == c) return TryOneTri({a, b, d});
            if (b == d) return TryOneTri({a, b, c});

            Real dist_ac = Length(vertices[c] - vertices[a]);
            Real dist_bd = Length(vertices[d] - vertices[b]);
            Real dist_ad = Length(vertices[d] - vertices[a]);
            Real dist_bc = Length(vertices[c] - vertices[b]);

            std::array<int32_t, 3> tri1, tri2;
            if (dist_ac + dist_bd < dist_ad + dist_bc)
            {
                tri1 = {a, b, c};
                tri2 = {b, d, c};
            }
            else
            {
                tri1 = {a, b, d};
                tri2 = {a, d, c};
            }

            return ValidateBridgeLocal(triangles, tri1, tri2, ecm);
        }
        // Local quality check for a fan triangulation of a boundary loop:
        // O(H) where H = number of loop vertices, rather than O(T) full-mesh scan.
        //
        // A fan fill adds H triangles: {loop[i], loop[(i+1)%H], centroid}.
        // The only edges that could become non-manifold are:
        //   - boundary edges {loop[i], loop[(i+1)%H]}: must have count == 1 in ecm
        //     (they are about to be "closed" to count 2 by the fan triangle)
        //   - centroid edges {loop[i], centroid}: must have count == 0 in ecm
        //     (centroid is a brand-new vertex)
        //
        // If centroidIdx == -1 the centroid has not been added yet; only the
        // loop boundary edge counts are checked.
        static bool ValidateFanLocal(
            BoundaryLoop const& loop,
            EdgeCountMap const& ecm,
            int32_t centroidIdx = -1)
        {
            int32_t H = static_cast<int32_t>(loop.vertices.size());
            for (int32_t i = 0; i < H; ++i)
            {
                int32_t v0 = loop.vertices[i];
                int32_t v1 = loop.vertices[(i + 1) % H];

                // Each boundary edge of the loop must appear exactly once in the mesh.
                // If it already appears twice, adding a fan triangle would push it to
                // count 3 — a non-manifold edge.
                auto it = ecm.find(MakeEdgeKey(v0, v1));
                size_t count = (it != ecm.end()) ? it->second : 0;
                if (count != 1)
                {
                    return false;  // Boundary edge not manifold-safe to close
                }

                // Centroid spoke edges must be absent (centroid is a brand-new vertex,
                // so its edges could not already exist — but check anyway for safety).
                if (centroidIdx >= 0)
                {
                    auto it2 = ecm.find(MakeEdgeKey(v0, centroidIdx));
                    if (it2 != ecm.end() && it2->second > 0)
                    {
                        return false;  // Spoke already exists – would be non-manifold
                    }
                }
            }
            return true;
        }

        // Fill a boundary loop with a centroid fan and update ecm atomically.
        // Precondition: ValidateFanLocal(loop, ecm) returned true.
        // Adds one new centroid vertex, appends H triangles, and updates ecm —
        // all in a single pass, so no separate append-then-revert is needed.
        static void FillFanWithECM(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& loop,
            EdgeCountMap& ecm)
        {
            int32_t H = static_cast<int32_t>(loop.vertices.size());

            // Add centroid vertex
            int32_t centroidIdx = static_cast<int32_t>(vertices.size());
            vertices.push_back(loop.centroid);

            for (int32_t i = 0; i < H; ++i)
            {
                int32_t v0 = loop.vertices[i];
                int32_t v1 = loop.vertices[(i + 1) % H];

                triangles.push_back({v0, v1, centroidIdx});

                // Boundary edge v0-v1: count 1 → 2 (becomes interior).
                ecm[MakeEdgeKey(v0, v1)]++;

                // Spoke edges: each spoke (v_i, centroid) is shared between two
                // adjacent fan triangles and is therefore an interior edge with
                // final count 2.  Adding both v0→c and v1→c here gives each
                // spoke its two increments across all iterations: the v1→c
                // increment at iteration i is the same as the v0→c increment
                // at iteration i+1, so each spoke accumulates count 2 overall.
                ecm[MakeEdgeKey(v0, centroidIdx)]++;
                ecm[MakeEdgeKey(v1, centroidIdx)]++;
            }
        }

        // CapBoundaryLoop overload 1: legacy path — full-mesh ValidateManifold (O(T)).
        // Kept for callers that do not maintain a persistent ECM.
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

        // CapBoundaryLoop overload 2: local ECM path — O(H) check instead of O(T).
        // Uses ValidateFanLocal to verify only the H boundary edges and new spokes,
        // then calls FillFanWithECM to commit atomically.  No append-then-revert.
        static bool CapBoundaryLoop(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& loop,
            EdgeCountMap& ecm)
        {
            if (loop.vertices.size() < 3)
            {
                return false;
            }

            if (!ValidateFanLocal(loop, ecm))
            {
                return false;
            }

            FillFanWithECM(vertices, triangles, loop, ecm);
            return true;
        }
        
        // ======================================================================
        // COMPONENT-AWARE PROGRESSIVE MERGING
        // ======================================================================
        
        // Union-Find data structure for incremental component tracking
        class UnionFind
        {
        private:
            std::vector<size_t> parent;
            std::vector<size_t> rank;
            std::vector<size_t> size;
            size_t numComponents;
            
        public:
            UnionFind(size_t n) 
                : parent(n), rank(n, 0), size(n, 1), numComponents(n)
            {
                for (size_t i = 0; i < n; ++i)
                {
                    parent[i] = i;
                }
            }
            
            // Find with path compression
            size_t Find(size_t x)
            {
                if (parent[x] != x)
                {
                    parent[x] = Find(parent[x]);  // Path compression
                }
                return parent[x];
            }
            
            // Union by rank
            bool Unite(size_t x, size_t y)
            {
                size_t rootX = Find(x);
                size_t rootY = Find(y);
                
                if (rootX == rootY)
                {
                    return false;  // Already in same set
                }
                
                // Union by rank
                if (rank[rootX] < rank[rootY])
                {
                    parent[rootX] = rootY;
                    size[rootY] += size[rootX];
                }
                else if (rank[rootX] > rank[rootY])
                {
                    parent[rootY] = rootX;
                    size[rootX] += size[rootY];
                }
                else
                {
                    parent[rootY] = rootX;
                    size[rootX] += size[rootY];
                    rank[rootX]++;
                }
                
                numComponents--;
                return true;
            }
            
            size_t GetSize(size_t x)
            {
                return size[Find(x)];
            }
            
            size_t GetNumComponents() const
            {
                return numComponents;
            }
            
            bool InSameComponent(size_t x, size_t y)
            {
                return Find(x) == Find(y);
            }
        };
        
        // Structure to represent a connected component with incremental update support
        struct MeshComponent
        {
            std::vector<size_t> triangleIndices;  // Indices of triangles in this component
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;  // Boundary edges
            std::set<int32_t> vertices;  // All vertices in component
            Vector3<Real> centroid;  // Component centroid
            Real boundingRadius;  // Bounding sphere radius
            size_t vertexCount;  // Number of vertices (for weighted centroid merging)
            
            MeshComponent() 
                : centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)}
                , boundingRadius(static_cast<Real>(0))
                , vertexCount(0)
            {}
            
            size_t Size() const { return triangleIndices.size(); }
            
            // Merge another component into this one (incremental update)
            void MergeWith(MeshComponent const& other, 
                          std::vector<Vector3<Real>> const& verticesArray)
            {
                // Merge triangle indices
                triangleIndices.insert(triangleIndices.end(), 
                                      other.triangleIndices.begin(), 
                                      other.triangleIndices.end());
                
                // Merge vertex sets (this->vertices is the member variable)
                this->vertices.insert(other.vertices.begin(), other.vertices.end());
                
                // Update centroid as weighted average
                if (vertexCount + other.vertexCount > 0)
                {
                    Real w1 = static_cast<Real>(vertexCount);
                    Real w2 = static_cast<Real>(other.vertexCount);
                    Real totalWeight = w1 + w2;
                    
                    centroid = (centroid * w1 + other.centroid * w2) / totalWeight;
                    vertexCount += other.vertexCount;
                }
                
                // Update bounding sphere: compute bounding sphere of two bounding spheres
                UpdateBoundingSphereAfterMerge(other, verticesArray);
                
                // Note: Boundary edges will be updated separately after bridging
            }
            
            // Update bounding sphere after merging with another component
            void UpdateBoundingSphereAfterMerge(MeshComponent const& other,
                                                std::vector<Vector3<Real>> const& vertices)
            {
                // Distance between sphere centers
                Vector3<Real> centerDiff = other.centroid - centroid;
                Real centerDist = Length(centerDiff);
                
                // If one sphere contains the other, use the larger one
                if (centerDist + other.boundingRadius <= boundingRadius)
                {
                    // Other sphere is inside this one
                    return;
                }
                if (centerDist + boundingRadius <= other.boundingRadius)
                {
                    // This sphere is inside the other one
                    centroid = other.centroid;
                    boundingRadius = other.boundingRadius;
                    return;
                }
                
                // Compute bounding sphere that contains both spheres
                // The new sphere's diameter is the distance between the farthest points
                Real newRadius = (boundingRadius + centerDist + other.boundingRadius) * static_cast<Real>(0.5);
                
                // New center is along the line between centers
                if (centerDist > static_cast<Real>(0))
                {
                    Real t = (newRadius - boundingRadius) / centerDist;
                    centroid = centroid + centerDiff * t;
                }
                
                boundingRadius = newRadius;
            }
            
            // Remove edges that are no longer boundaries after bridging
            void RemoveBoundaryEdges(std::set<std::pair<int32_t, int32_t>> const& edgesToRemove)
            {
                auto it = boundaryEdges.begin();
                while (it != boundaryEdges.end())
                {
                    if (edgesToRemove.count(*it))
                    {
                        it = boundaryEdges.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
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
            
            // Build edge-to-triangles adjacency using unordered_map for O(1) lookups
            std::unordered_map<std::pair<int32_t,int32_t>, std::vector<int32_t>, EdgePairHash>
                edgeToTriangles;
            edgeToTriangles.reserve(triangles.size() * 3);
            for (int32_t triIdx = 0; triIdx < static_cast<int32_t>(triangles.size()); ++triIdx)
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
            
            // BFS to find connected components – O(1) per triangle using vector<bool>
            std::vector<bool> visited(triangles.size(), false);
            
            for (size_t startIdx = 0; startIdx < triangles.size(); ++startIdx)
            {
                if (visited[startIdx])
                {
                    continue;
                }
                
                // Start new component
                MeshComponent component;
                std::vector<int32_t> queue;
                queue.push_back(static_cast<int32_t>(startIdx));
                visited[startIdx] = true;
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
                    int32_t currentIdx = queue[queuePos++];
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
                            for (int32_t neighborIdx : it->second)
                            {
                                if (!visited[neighborIdx])
                                {
                                    visited[neighborIdx] = true;
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
                
                // Extract boundary edges for this component using unordered_map
                EdgeCountMap edgeCounts;
                edgeCounts.reserve(component.triangleIndices.size() * 3);
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
                
                // Set vertex count for weighted centroid merging
                component.vertexCount = component.vertices.size();
                
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
        
        // Spatial index for boundary edges to accelerate O(B²) queries
        struct EdgeSpatialIndex
        {
            struct EdgeEntry
            {
                std::pair<int32_t, int32_t> edge;
                Vector3<Real> midpoint;
                Vector3<Real> boxMin;
                Vector3<Real> boxMax;
                
                EdgeEntry(std::pair<int32_t, int32_t> const& e,
                         std::vector<Vector3<Real>> const& vertices,
                         Real padding = static_cast<Real>(0))
                {
                    edge = e;
                    Vector3<Real> const& v0 = vertices[e.first];
                    Vector3<Real> const& v1 = vertices[e.second];
                    midpoint = (v0 + v1) * static_cast<Real>(0.5);
                    
                    // Create bounding box with padding
                    for (int i = 0; i < 3; ++i)
                    {
                        boxMin[i] = std::min(v0[i], v1[i]) - padding;
                        boxMax[i] = std::max(v0[i], v1[i]) + padding;
                    }
                }
            };
            
            std::vector<EdgeEntry> edges;
            
            void Build(std::vector<std::pair<int32_t, int32_t>> const& boundaryEdges,
                      std::vector<Vector3<Real>> const& vertices,
                      Real padding = static_cast<Real>(0))
            {
                edges.clear();
                edges.reserve(boundaryEdges.size());
                
                for (auto const& edge : boundaryEdges)
                {
                    edges.emplace_back(edge, vertices, padding);
                }
            }
            
            // Query edges within threshold distance of a point
            void QueryNearPoint(Vector3<Real> const& point,
                               Real threshold,
                               std::vector<size_t>& results) const
            {
                results.clear();
                Real thresholdSq = threshold * threshold;
                
                for (size_t i = 0; i < edges.size(); ++i)
                {
                    // Quick box test
                    bool outsideBox = false;
                    for (int j = 0; j < 3; ++j)
                    {
                        if (point[j] < edges[i].boxMin[j] - threshold ||
                            point[j] > edges[i].boxMax[j] + threshold)
                        {
                            outsideBox = true;
                            break;
                        }
                    }
                    
                    if (!outsideBox)
                    {
                        // More precise distance check to midpoint
                        Vector3<Real> diff = point - edges[i].midpoint;
                        Real distSq = Dot(diff, diff);
                        if (distSq <= thresholdSq)
                        {
                            results.push_back(i);
                        }
                    }
                }
            }
            
            // Query edges within threshold of another edge
            void QueryNearEdge(std::pair<int32_t, int32_t> const& queryEdge,
                              std::vector<Vector3<Real>> const& vertices,
                              Real threshold,
                              std::vector<size_t>& results) const
            {
                results.clear();
                
                Vector3<Real> const& v0 = vertices[queryEdge.first];
                Vector3<Real> const& v1 = vertices[queryEdge.second];
                Vector3<Real> midpoint = (v0 + v1) * static_cast<Real>(0.5);
                
                // First, query based on midpoint
                Real expandedThreshold = threshold + Length(v1 - v0) * static_cast<Real>(0.5);
                QueryNearPoint(midpoint, expandedThreshold, results);
            }
        };
        
        // Compute all candidate edge pairs between two components, sorted by distance
        // Uses spatial indexing to reduce from O(B1 × B2) to O(B1 × log B2)
        static void FindCandidateEdgePairs(
            std::vector<Vector3<Real>> const& vertices,
            MeshComponent const& comp1,
            MeshComponent const& comp2,
            Real threshold,
            std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>>& candidates)
        {
            candidates.clear();
            
            // Early rejection using squared bounding-sphere distance (avoids sqrt).
            Vector3<Real> centDiff = comp1.centroid - comp2.centroid;
            Real centerDistSq = Dot(centDiff, centDiff);
            Real maxReach = comp1.boundingRadius + comp2.boundingRadius + threshold;
            if (centerDistSq > maxReach * maxReach)
            {
                return;
            }
            
            // For small edge counts, brute force is faster
            if (comp1.boundaryEdges.size() * comp2.boundaryEdges.size() < 100)
            {
                // Brute force for small cases
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
            }
            else
            {
                // Use spatial index for larger cases
                EdgeSpatialIndex index;
                index.Build(comp2.boundaryEdges, vertices, threshold);
                
                std::vector<size_t> nearbyIndices;
                
                for (auto const& edge1 : comp1.boundaryEdges)
                {
                    // Query spatial index for edges near edge1
                    index.QueryNearEdge(edge1, vertices, threshold, nearbyIndices);
                    
                    // Check actual distances to nearby edges
                    for (size_t idx : nearbyIndices)
                    {
                        auto const& edge2 = comp2.boundaryEdges[idx];
                        
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
        
        // Optimized progressive component merging with batching
        // Reduces component re-extraction overhead by batching bridges
        static size_t ProgressiveComponentMergingBatched(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            size_t totalBridges = 0;
            size_t maxBatches = 20;  // Prevent infinite loops
            
            for (size_t batchNum = 0; batchNum < maxBatches; ++batchNum)
            {
                // Extract current components ONCE per batch
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
                
                if (verbose)
                {
                    std::cout << "Batch " << (batchNum + 1) << ": " << components.size() << " components\n";
                }
                
                // Sort components by size (largest first)
                std::sort(components.begin(), components.end(),
                    [](MeshComponent const& a, MeshComponent const& b) {
                        return a.Size() > b.Size();
                    });
                
                // Collect ALL viable bridges in this batch
                std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> allBridgeCandidates;
                
                // For each component, find bridges to other components
                for (size_t i = 0; i < components.size(); ++i)
                {
                    if (components[i].boundaryEdges.empty())
                    {
                        continue;
                    }
                    
                    // Find closest component with boundary edges
                    Real minDist = std::numeric_limits<Real>::max();
                    size_t closestIdx = components.size();
                    
                    for (size_t j = i + 1; j < components.size(); ++j)
                    {
                        if (components[j].boundaryEdges.empty())
                        {
                            continue;
                        }
                        
                        Real centerDist = Length(components[i].centroid - components[j].centroid);
                        if (centerDist < minDist)
                        {
                            minDist = centerDist;
                            closestIdx = j;
                        }
                    }
                    
                    if (closestIdx < components.size())
                    {
                        // Find candidate edge pairs between these components
                        std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> candidates;
                        FindCandidateEdgePairs(vertices, components[i], components[closestIdx], threshold, candidates);
                        
                        if (!candidates.empty())
                        {
                            // Take the closest edge pair for this component pair
                            allBridgeCandidates.push_back(candidates[0]);
                        }
                    }
                }
                
                if (allBridgeCandidates.empty())
                {
                    if (verbose)
                    {
                        std::cout << "No valid bridges found in batch. Stopping.\n";
                    }
                    break;
                }
                
                // Sort all candidates by distance
                std::sort(allBridgeCandidates.begin(), allBridgeCandidates.end(),
                    [](auto const& a, auto const& b) {
                        return std::get<0>(a) < std::get<0>(b);
                    });
                
                // Apply bridges in order, avoiding conflicts
                std::set<std::pair<int32_t, int32_t>> usedEdges;
                size_t bridgesThisBatch = 0;
                
                for (auto const& candidate : allBridgeCandidates)
                {
                    auto const& edge1 = std::get<1>(candidate);
                    auto const& edge2 = std::get<2>(candidate);
                    Real dist = std::get<0>(candidate);
                    
                    // Check if either edge already used
                    if (usedEdges.count(edge1) || usedEdges.count(edge2))
                    {
                        continue;
                    }
                    
                    if (BridgeBoundaryEdges(vertices, triangles, edge1, edge2, true))
                    {
                        usedEdges.insert(edge1);
                        usedEdges.insert(edge2);
                        bridgesThisBatch++;
                        totalBridges++;
                        
                        if (verbose && bridgesThisBatch <= 5)
                        {
                            std::cout << "  Bridge " << bridgesThisBatch 
                                      << ": distance " << dist << "\n";
                        }
                    }
                }
                
                if (verbose)
                {
                    std::cout << "  Created " << bridgesThisBatch << " bridges in batch\n";
                }
                
                if (bridgesThisBatch == 0)
                {
                    if (verbose)
                    {
                        std::cout << "No successful bridges in batch. Stopping.\n";
                    }
                    break;
                }
            }
            
            return totalBridges;
        }
        
        // Aggressive multi-merge: bridges multiple component pairs simultaneously
        // Reduces iterations from O(C) to O(log C) where C = number of components
        static size_t ProgressiveComponentMergingAggressiveBatch(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            if (triangles.empty())
            {
                return 0;
            }
            
            size_t totalBridges = 0;
            size_t maxIterations = 10;  // Much fewer iterations needed
            
            for (size_t iter = 0; iter < maxIterations; ++iter)
            {
                // Extract components ONCE per iteration
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
                
                if (verbose)
                {
                    std::cout << "Iteration " << (iter + 1) << ": " 
                              << components.size() << " components\n";
                }
                
                // Build a list of ALL possible bridges between ALL component pairs
                std::vector<std::tuple<Real, size_t, size_t, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> allPossibleBridges;
                
                for (size_t i = 0; i < components.size(); ++i)
                {
                    if (components[i].boundaryEdges.empty())
                    {
                        continue;
                    }
                    
                    for (size_t j = i + 1; j < components.size(); ++j)
                    {
                        if (components[j].boundaryEdges.empty())
                        {
                            continue;
                        }
                        
                        // Quick distance check using centroids
                        Real centerDist = Length(components[i].centroid - components[j].centroid);
                        Real maxReach = components[i].boundingRadius + components[j].boundingRadius + threshold;
                        
                        if (centerDist > maxReach)
                        {
                            continue;  // Too far apart
                        }
                        
                        // Find best edge pair between these components
                        std::vector<std::tuple<Real, std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> candidates;
                        FindCandidateEdgePairs(vertices, components[i], components[j], threshold, candidates);
                        
                        if (!candidates.empty())
                        {
                            // Store best bridge candidate for this component pair
                            auto const& best = candidates[0];
                            Real dist = std::get<0>(best);
                            auto const& edge1 = std::get<1>(best);
                            auto const& edge2 = std::get<2>(best);
                            
                            allPossibleBridges.push_back(std::make_tuple(dist, i, j, edge1, edge2));
                        }
                    }
                }
                
                if (allPossibleBridges.empty())
                {
                    if (verbose)
                    {
                        std::cout << "No valid bridges found. Stopping.\n";
                    }
                    break;
                }
                
                // Sort by distance (closest first)
                std::sort(allPossibleBridges.begin(), allPossibleBridges.end(),
                    [](auto const& a, auto const& b) {
                        return std::get<0>(a) < std::get<0>(b);
                    });
                
                // Greedily apply bridges, avoiding conflicts
                std::set<size_t> mergedComponents;  // Track which components have been merged
                std::set<std::pair<int32_t, int32_t>> usedEdges;
                size_t bridgesThisIter = 0;
                
                for (auto const& bridge : allPossibleBridges)
                {
                    Real dist = std::get<0>(bridge);
                    size_t comp1 = std::get<1>(bridge);
                    size_t comp2 = std::get<2>(bridge);
                    auto const& edge1 = std::get<3>(bridge);
                    auto const& edge2 = std::get<4>(bridge);
                    
                    // Skip if either component already merged in this iteration
                    if (mergedComponents.count(comp1) || mergedComponents.count(comp2))
                    {
                        continue;
                    }
                    
                    // Skip if edges already used
                    if (usedEdges.count(edge1) || usedEdges.count(edge2))
                    {
                        continue;
                    }
                    
                    // Try to create bridge
                    if (BridgeBoundaryEdges(vertices, triangles, edge1, edge2, true))
                    {
                        mergedComponents.insert(comp1);
                        mergedComponents.insert(comp2);
                        usedEdges.insert(edge1);
                        usedEdges.insert(edge2);
                        bridgesThisIter++;
                        totalBridges++;
                        
                        if (verbose && bridgesThisIter <= 10)
                        {
                            std::cout << "  Bridge " << bridgesThisIter 
                                      << ": comp " << comp1 << " <-> " << comp2
                                      << " at distance " << dist << "\n";
                        }
                    }
                }
                
                if (verbose)
                {
                    std::cout << "  Created " << bridgesThisIter << " bridges this iteration\n";
                }
                
                if (bridgesThisIter == 0)
                {
                    if (verbose)
                    {
                        std::cout << "No successful bridges. Stopping.\n";
                    }
                    break;
                }
            }
            
            return totalBridges;
        }
        
        // Incremental component merging using a global boundary-edge NNTree.
        // Each call does ONE pass: build tree, find all candidate bridges,
        // apply greedily.  Iteration is handled by the outer
        // TopologyAwareComponentBridging loop.
        static size_t ProgressiveComponentMergingIncremental(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            if (triangles.empty())
            {
                return 0;
            }

            // Extract connected components (O(T)).
            std::vector<MeshComponent> components;
            ExtractComponents(vertices, triangles, components);

            if (verbose)
            {
                std::cout << "\n=== Global-Edge-NNTree Component Merging ===\n";
                std::cout << "Components: " << components.size() << "\n";
            }

            if (components.size() <= 1)
            {
                return 0;
            }

            // Build ECM once; update incrementally after each bridge.
            EdgeCountMap ecm = BuildEdgeCountMap(triangles);

            // ---------------------------------------------------------------
            // Build a global NNTree of ALL boundary-edge midpoints (ONE time).
            // ---------------------------------------------------------------
            struct GlobalEdgeEntry
            {
                size_t componentIndex;
                size_t localEdgeIndex;
            };

            std::vector<PositionSite<3, Real>> edgeMidSites;
            std::vector<GlobalEdgeEntry> globalEdgeIdx;

            for (size_t i = 0; i < components.size(); ++i)
            {
                for (size_t ei = 0; ei < components[i].boundaryEdges.size(); ++ei)
                {
                    auto const& e = components[i].boundaryEdges[ei];
                    Vector3<Real> mid =
                        (vertices[e.first] + vertices[e.second]) * static_cast<Real>(0.5);
                    edgeMidSites.emplace_back(mid);
                    globalEdgeIdx.push_back({i, ei});
                }
            }

            if (edgeMidSites.empty())
            {
                return 0;
            }

            static constexpr int32_t maxLeafSize = 10;
            static constexpr int32_t maxLevel = 20;
            NearestNeighborQuery<3, Real, PositionSite<3, Real>> edgeTree(
                edgeMidSites, maxLeafSize, maxLevel);

            // Query radius: midpoints within threshold + half-edge-length.
            // A factor of 1.5 is conservative and avoids missed bridges.
            Real queryRadius = threshold * static_cast<Real>(1.5);

            // Map componentPair → best (dist, edge1, edge2)
            using BridgeKey = std::pair<size_t, size_t>;
            struct BridgeKeyHash
            {
                size_t operator()(BridgeKey const& k) const noexcept
                {
                    size_t h = k.first;
                    h ^= k.second + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                    return h;
                }
            };
            using BridgeVal = std::tuple<Real,
                                         std::pair<int32_t,int32_t>,
                                         std::pair<int32_t,int32_t>>;

            constexpr int32_t MaxNearbyEdges = 32;

            // ---------------------------------------------------------------
            // Parallelise the NNTree query loop across boundary edges.
            // FindNeighbors is read-only on the tree and therefore thread-safe.
            // Each thread accumulates candidates into its own local map; the
            // maps are merged in O(components) time after all threads finish.
            // ---------------------------------------------------------------
            unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
            size_t totalEdges = edgeMidSites.size();

            // One local bestBridge map per thread.
            std::vector<std::unordered_map<BridgeKey, BridgeVal, BridgeKeyHash>> localBridges(numThreads);

            auto queryChunk = [&](unsigned int t)
            {
                size_t chunkStart = (totalEdges * t) / numThreads;
                size_t chunkEnd   = (totalEdges * (t + 1u)) / numThreads;
                auto& localBridge = localBridges[t];
                std::array<int32_t, MaxNearbyEdges> nearbyIdxLocal;

                for (size_t gi = chunkStart; gi < chunkEnd; ++gi)
                {
                    size_t compI = globalEdgeIdx[gi].componentIndex;
                    auto const& edge1 =
                        components[compI].boundaryEdges[globalEdgeIdx[gi].localEdgeIndex];

                    int32_t numNearby = edgeTree.template FindNeighbors<MaxNearbyEdges>(
                        edgeMidSites[gi].position, queryRadius, nearbyIdxLocal);

                    for (int32_t kk = 0; kk < numNearby; ++kk)
                    {
                        size_t gj = static_cast<size_t>(nearbyIdxLocal[kk]);
                        size_t compJ = globalEdgeIdx[gj].componentIndex;
                        if (compI == compJ) continue;

                        size_t ci = std::min(compI, compJ);
                        size_t cj = std::max(compI, compJ);

                        auto const& edge2 =
                            components[compJ].boundaryEdges[globalEdgeIdx[gj].localEdgeIndex];

                        Real dist = std::min({
                            Length(vertices[edge1.first]  - vertices[edge2.first]),
                            Length(vertices[edge1.first]  - vertices[edge2.second]),
                            Length(vertices[edge1.second] - vertices[edge2.first]),
                            Length(vertices[edge1.second] - vertices[edge2.second])
                        });

                        if (dist > threshold) continue;

                        BridgeKey key{ci, cj};
                        auto it = localBridge.find(key);
                        if (it == localBridge.end() || dist < std::get<0>(it->second))
                        {
                            auto const& eA = (compI == ci) ? edge1 : edge2;
                            auto const& eB = (compI == ci) ? edge2 : edge1;
                            localBridge[key] = {dist, eA, eB};
                        }
                    }
                }
            };

            // Launch worker threads for chunks [1..numThreads-1]; run chunk 0 on caller.
            std::vector<std::thread> threads;
            threads.reserve(numThreads - 1);
            for (unsigned int t = 1; t < numThreads; ++t)
            {
                threads.emplace_back(queryChunk, t);
            }
            queryChunk(0);
            for (auto& th : threads) th.join();

            // Merge thread-local maps into bestBridge.
            std::unordered_map<BridgeKey, BridgeVal, BridgeKeyHash> bestBridge;
            bestBridge.reserve(components.size());
            for (auto const& localBridge : localBridges)
            {
                for (auto const& [key, val] : localBridge)
                {
                    auto it = bestBridge.find(key);
                    if (it == bestBridge.end() || std::get<0>(val) < std::get<0>(it->second))
                    {
                        bestBridge[key] = val;
                    }
                }
            }
            if (verbose)
            {
                std::cout << "  [Profiling] NNTree parallel query: " << numThreads
                          << " thread(s), " << totalEdges << " boundary edges\n";
            }

            if (bestBridge.empty())
            {
                if (verbose) std::cout << "No candidate bridges found.\n";
                return 0;
            }

            // Sort candidates by distance and apply greedily.
            std::vector<std::tuple<Real, size_t, size_t,
                                   std::pair<int32_t,int32_t>,
                                   std::pair<int32_t,int32_t>>> candidates;
            candidates.reserve(bestBridge.size());
            for (auto const& [key, val] : bestBridge)
            {
                candidates.emplace_back(std::get<0>(val), key.first, key.second,
                                        std::get<1>(val), std::get<2>(val));
            }
            std::sort(candidates.begin(), candidates.end(),
                [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });

            std::vector<bool> mergedComponents(components.size(), false);
            size_t totalBridges = 0;

            for (auto const& cand : candidates)
            {
                size_t ci = std::get<1>(cand);
                size_t cj = std::get<2>(cand);

                if (mergedComponents[ci] || mergedComponents[cj]) continue;

                auto const& e1 = std::get<3>(cand);
                auto const& e2 = std::get<4>(cand);

                if (BridgeBoundaryEdges(vertices, triangles, e1, e2, ecm))
                {
                    mergedComponents[ci] = true;
                    mergedComponents[cj] = true;
                    totalBridges++;

                    if (verbose && totalBridges <= 10)
                    {
                        std::cout << "  Bridge " << totalBridges
                                  << ": comp " << ci << " <-> " << cj
                                  << " at distance " << std::get<0>(cand) << "\n";
                    }
                }
            }

            if (verbose)
            {
                std::cout << "Bridges created: " << totalBridges << "\n";
            }

            return totalBridges;
        }
        
        // Union-Find optimized progressive component merging
        // Uses Union-Find to track which triangles belong together, minimizing re-extraction
        static size_t ProgressiveComponentMergingUnionFind(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real threshold,
            bool verbose = false)
        {
            // Use incremental strategy with cached properties
            return ProgressiveComponentMergingIncremental(vertices, triangles, threshold, verbose);
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
            
            // Multi-pass iterative strategy: bridge components first, then fill holes
            Real avgEdgeLength = EstimateAverageEdgeLength(vertices, triangles);
            Real currentThreshold = avgEdgeLength * params.initialBridgeThreshold;
            Real maxThreshold = avgEdgeLength * params.maxBridgeThreshold;
            
            bool isClosedManifold = false;
            bool hasNonManifold = false;
            size_t boundaryCount = 0;
            size_t componentCount = 0;
            size_t totalBridges = 0;
            size_t totalHolesFilled = 0;
            
            // Phase 1: Component bridging (defer hole filling)
            for (size_t iteration = 0; iteration < params.maxIterations; ++iteration)
            {
                if (params.verbose)
                {
                    std::cout << "\n--- Iteration " << (iteration + 1) << " ---\n";
                    std::cout << "Bridge threshold: " << currentThreshold << " (" 
                              << (currentThreshold / avgEdgeLength) << "x avg edge length)\n";
                }
                
                // Step 1: Progressive component merging with Union-Find (optimized)
                size_t bridgesThisPass = ProgressiveComponentMergingUnionFind(
                    vertices, triangles, currentThreshold, params.verbose);
                totalBridges += bridgesThisPass;
                
                if (params.verbose)
                {
                    std::cout << "Created " << bridgesThisPass << " bridges this pass\n";
                }
                
                // Step 2: Skip hole filling during bridging phase (deferred to end)
                size_t holesFilledThisPass = 0;
                
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
            
            // Phase 2: Hole filling (now that component bridging is complete).
            // Build ECM once here and pass it into both the fill and cap steps so
            // each individual hole/cap check is O(H) local instead of O(T) full-mesh.
            EdgeCountMap ecm = BuildEdgeCountMap(triangles);

            if (params.enableHoleFilling)
            {
                if (params.verbose)
                {
                    std::cout << "\n=== Phase 2: Hole Filling ===\n";
                }
                
                totalHolesFilled = FillHolesConservative(
                    vertices, triangles, params, ecm, params.verbose);
                
                if (params.verbose)
                {
                    std::cout << "Filled " << totalHolesFilled << " holes total\n";
                }
            }
            
            // Final pass: Cap any remaining small boundary loops using the same ECM.
            std::vector<BoundaryLoop> loops;
            ExtractBoundaryLoops(vertices, triangles, loops);
            
            if (!loops.empty() && params.verbose)
            {
                std::cout << "\nCapping " << loops.size() << " remaining boundary loops...\n";
            }
            
            size_t cappedLoops = 0;
            for (auto const& loop : loops)
            {
                if (loop.vertices.size() <= static_cast<size_t>(params.maxHoleEdges))
                {
                    // ECM overload: O(H) local check per cap, not O(T) full-mesh.
                    if (CapBoundaryLoop(vertices, triangles, loop, ecm))
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
        // FillHolesConservative: fills small boundary loops with fan triangulation.
        //
        // The ECM-based overload (below) is the fast path: it builds the edge count
        // map once before iterating over all loops, then uses ValidateFanLocal + 
        // FillFanWithECM for each candidate — O(H) per hole instead of O(T).
        //
        // Overload 1: builds its own ECM internally (used when caller has no ECM).
        static size_t FillHolesConservative(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            bool verbose = false)
        {
            // Build ECM once for the whole fill pass — O(T) — then reuse it.
            EdgeCountMap ecm = BuildEdgeCountMap(triangles);
            return FillHolesConservative(vertices, triangles, params, ecm, verbose);
        }

        // Overload 2: caller supplies (and owns) the ECM.  Fills holes and keeps
        // ecm up-to-date so the caller's ECM remains valid after this call returns.
        static size_t FillHolesConservative(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            EdgeCountMap& ecm,
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
                
                // Local check: O(H) — verifies only the H boundary edges and new
                // spoke edges in this loop, not the full mesh.
                if (!ValidateFanLocal(loop, ecm))
                {
                    continue;
                }

                // Fill and update ECM atomically — no append-then-revert needed.
                size_t trisBefore = triangles.size();
                FillFanWithECM(vertices, triangles, loop, ecm);

                holesFilledCount++;

                if (verbose && holesFilledCount <= 10)
                {
                    std::cout << "  Filled hole with " << loop.vertices.size()
                              << " vertices (" << (triangles.size() - trisBefore)
                              << " triangles)\n";
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
        
        // Remove connected components that are closed (no boundary edges) and
        // contain at most maxTriangles triangles.  These fragments are artifacts
        // produced by Co3Ne for dense local regions; they have no boundary to bridge
        // with and would prevent the final closed-manifold check from passing.
        static void RemoveSmallClosedComponents(
            std::vector<std::array<int32_t, 3>>& triangles,
            size_t maxTriangles,
            bool verbose = false)
        {
            if (triangles.empty()) return;

            // Build edge-to-triangle adjacency.
            std::unordered_map<std::pair<int32_t, int32_t>,
                               std::vector<int32_t>, EdgePairHash> edgeToTri;
            edgeToTri.reserve(triangles.size() * 3);
            for (int32_t ti = 0; ti < static_cast<int32_t>(triangles.size()); ++ti)
            {
                auto const& t = triangles[ti];
                for (int i = 0; i < 3; ++i)
                {
                    auto e = std::make_pair(std::min(t[i], t[(i + 1) % 3]),
                                            std::max(t[i], t[(i + 1) % 3]));
                    edgeToTri[e].push_back(ti);
                }
            }

            // Identify connected components via BFS.
            std::vector<bool> visited(triangles.size(), false);
            std::vector<bool> toRemove(triangles.size(), false);
            size_t removedComps = 0;

            for (size_t seed = 0; seed < triangles.size(); ++seed)
            {
                if (visited[seed]) continue;

                // DFS (stack) traversal over this component.
                std::vector<size_t> comp;
                std::vector<int32_t> stack = {static_cast<int32_t>(seed)};
                visited[seed] = true;
                size_t boundaryEdges = 0;

                while (!stack.empty())
                {
                    int32_t cur = stack.back(); stack.pop_back();
                    comp.push_back(static_cast<size_t>(cur));
                    auto const& t = triangles[cur];
                    for (int i = 0; i < 3; ++i)
                    {
                        auto e = std::make_pair(std::min(t[i], t[(i + 1) % 3]),
                                                std::max(t[i], t[(i + 1) % 3]));
                        auto it = edgeToTri.find(e);
                        if (it == edgeToTri.end()) continue;
                        if (it->second.size() == 1) ++boundaryEdges;  // boundary
                        for (int32_t nb : it->second)
                        {
                            if (!visited[nb])
                            {
                                visited[nb] = true;
                                stack.push_back(nb);
                            }
                        }
                    }
                }

                // Remove if: closed (no boundary edges) AND small.
                if (boundaryEdges == 0 && comp.size() <= maxTriangles)
                {
                    for (size_t ti : comp) toRemove[ti] = true;
                    ++removedComps;
                    if (verbose)
                    {
                        std::cout << "  Removing small closed component ("
                                  << comp.size() << " triangles)\n";
                    }
                }
            }

            if (removedComps == 0) return;

            // Compact the triangle array.
            std::vector<std::array<int32_t, 3>> kept;
            kept.reserve(triangles.size());
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                if (!toRemove[i]) kept.push_back(triangles[i]);
            }
            triangles = std::move(kept);

            if (verbose)
            {
                std::cout << "Removed " << removedComps
                          << " small closed component(s)\n";
            }
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
