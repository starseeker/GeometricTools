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
            Real area;                              // Total patch area
            
            Patch() : isManifold(false), area(static_cast<Real>(0)) {}
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
                for (size_t i = 0; i < patches.size(); ++i)
                {
                    std::cout << "  Patch " << i << ": " 
                              << patches[i].triangleIndices.size() << " triangles, "
                              << patches[i].boundaryEdges.size() << " boundary edges, "
                              << (patches[i].isManifold ? "manifold" : "non-manifold") << "\n";
                }
            }
            
            // Step 4: Fill holes using existing hole filling
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
                
                if (params.verbose && trianglesAfter > trianglesBefore)
                {
                    std::cout << "Hole filling added " << (trianglesAfter - trianglesBefore) 
                              << " triangles\n";
                }
            }
            
            // Step 5: Ball pivot stitching (if enabled)
            if (params.enableBallPivot)
            {
                // TODO: Implement ball pivot stitching
                if (params.verbose)
                {
                    std::cout << "Ball pivot stitching not yet implemented\n";
                }
            }
            
            // Step 6: UV parameterization merging (if enabled)
            if (params.enableUVMerging)
            {
                // TODO: Implement UV-based patch merging
                if (params.verbose)
                {
                    std::cout << "UV-based patch merging not yet implemented\n";
                }
            }
            
            // Step 7: Final validation
            bool isManifold = ValidateManifold(triangles);
            
            if (params.verbose)
            {
                std::cout << "Final mesh: " << triangles.size() << " triangles, "
                          << (isManifold ? "manifold" : "non-manifold") << "\n";
            }
            
            return isManifold;
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
            // Compute area
            patch.area = static_cast<Real>(0);
            for (auto triIdx : patch.triangleIndices)
            {
                auto const& tri = triangles[triIdx];
                Vector3<Real> v0 = vertices[tri[0]];
                Vector3<Real> v1 = vertices[tri[1]];
                Vector3<Real> v2 = vertices[tri[2]];
                
                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> cross = Cross(edge1, edge2);
                patch.area += Length(cross) * static_cast<Real>(0.5);
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
            for (auto const& ec : patchEdgeCounts)
            {
                if (ec.second == 1)
                {
                    patch.boundaryEdges.push_back(ec.first);
                }
                else if (ec.second > 2)
                {
                    patch.isManifold = false;
                }
            }
        }
        
        // Validate that mesh is manifold
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
    };
}

#endif // GTE_MATHEMATICS_CO3NE_MANIFOLD_STITCHER_H
