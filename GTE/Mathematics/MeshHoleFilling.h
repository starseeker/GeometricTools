// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Ported from Geogram: https://github.com/BrunoLevy/geogram
// Original files: src/lib/geogram/mesh/mesh_fill_holes.cpp, mesh_fill_holes.h
// Original license: BSD 3-Clause (see below)
// Copyright (c) 2000-2022 Inria
//
// Adaptations for Geometric Tools Engine:
// - Changed from GEO::Mesh to std::vector<Vector3<Real>> and std::vector<std::array<int32_t, 3>>
// - Uses GTE::ETManifoldMesh for topology operations
// - Uses GTE's TriangulateEC and TriangulateCDT for robust triangulation
// - Supports both Ear Clipping and Constrained Delaunay Triangulation
// - Removed Geogram-specific Logger and CmdLine calls
//
// Original Geogram License (BSD 3-Clause):
// [Same license text as in MeshRepair.h]

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Vector2.h>
#include <GTE/Mathematics/ETManifoldMesh.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <GTE/Mathematics/Polygon2Validation.h>
#include <GTE/Mathematics/TriangulateEC.h>
#include <GTE/Mathematics/TriangulateCDT.h>
#include <GTE/Mathematics/PolygonTree.h>
#include <GTE/Mathematics/ArbitraryPrecision.h>
#include <GTE/Mathematics/LSCMParameterization.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The MeshHoleFilling class provides hole detection and filling operations
// ported from Geogram. It detects boundary loops in a triangle mesh and
// fills them with new triangles using GTE's robust triangulation algorithms.
//
// Supports four triangulation methods (applied as a fallback chain):
// - Ear Clipping (EC):     Project to best-fit plane, ear-clip in 2D  (fast)
// - CDT:                   Project to best-fit plane, CDT in 2D       (robust)
// - LSCM:                  Arc-length boundary-to-circle mapping + EC; always
//                          succeeds for any simple closed 3D loop regardless
//                          of planarity (second-to-last resort)
// - EarClipping3D:         Ear clipping directly in 3D                (last resort)

namespace gte
{
    template <typename Real>
    class MeshHoleFilling
    {
    public:
        // Triangulation method for hole filling
        enum class TriangulationMethod
        {
            EarClipping,    // Project to best-fit plane, ear-clip in 2D (fast)
            CDT,            // Project to best-fit plane, CDT in 2D (more robust)
            LSCM,           // Arc-length circle map + EC in 2D; always succeeds for any simple
                            // closed 3D boundary regardless of planarity (second-to-last resort)
            EarClipping3D   // 3D ear clipping without any projection (last resort)
        };

        // Parameters for hole filling operations.
        struct Parameters
        {
            Real maxArea;                       // Maximum hole area to fill (0 = unlimited)
            size_t maxEdges;                    // Maximum hole perimeter edges (0 = unlimited)
            bool repair;                        // Run mesh repair after filling
            TriangulationMethod method;         // Triangulation algorithm to use
            Real planarityThreshold;            // Max planarity deviation before using 3D method (0 = always use selected method)
            bool autoFallback;                  // Automatically fall back to 3D if 2D fails
            bool validateOutput;                // Validate that output is manifold and non-self-intersecting
            bool requireManifold;               // Fail if output is not manifold
            bool requireNoSelfIntersections;    // Fail if output has self-intersections

            Parameters()
                : maxArea(static_cast<Real>(0))
                , maxEdges(0)                               // 0 = unlimited (fill all holes)
                , repair(false)                             // repair is caller's responsibility
                , method(TriangulationMethod::EarClipping)  // Default to EC for compatibility
                , planarityThreshold(static_cast<Real>(0))  // 0 = disabled
                , autoFallback(true)                        // Enable automatic fallback
                , validateOutput(false)                     // No validation by default (Geogram-compatible)
                , requireManifold(false)                    // No manifold requirement by default
                , requireNoSelfIntersections(false)         // Don't require (can be expensive)
            {
            }
        };

        // A boundary loop representing a hole in the mesh.
        struct HoleBoundary
        {
            std::vector<int32_t> vertices;  // Ordered vertex indices forming boundary
            Real area;                       // Approximate area of triangulation

            HoleBoundary() : area(static_cast<Real>(0)) {}
        };

        // Fill holes in the mesh by generating new triangles.
        // Holes with area exceeding maxArea (when > 0) or with more than maxEdges
        // boundary edges (when > 0) are skipped.  A value of 0 for either
        // parameter means no limit (fill all holes of that kind).
        static void FillHoles(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters())
        {
            // Step 1: Build directed-edge existence set using unordered_map for O(T) average.
            // Key: directed edge (v0, v1) encoded as int64_t = (v0<<32)|v1.
            // An undirected edge is a boundary edge iff its reverse (v1, v0) is absent.
            struct DirectedEdgeHash {
                size_t operator()(int64_t key) const noexcept {
                    // Mix high and low halves
                    uint64_t k = static_cast<uint64_t>(key);
                    k ^= k >> 33;
                    k *= 0xff51afd7ed558ccdULL;
                    k ^= k >> 33;
                    k *= 0xc4ceb9fe1a85ec53ULL;
                    k ^= k >> 33;
                    return static_cast<size_t>(k);
                }
            };
            auto encodeEdge = [](int32_t v0, int32_t v1) noexcept -> int64_t {
                return (static_cast<int64_t>(v0) << 32) | static_cast<uint32_t>(v1);
            };

            std::unordered_set<int64_t, DirectedEdgeHash> directedEdges;
            directedEdges.reserve(triangles.size() * 3);
            for (auto const& tri : triangles)
            {
                for (int j = 0; j < 3; ++j)
                {
                    directedEdges.insert(encodeEdge(tri[j], tri[(j + 1) % 3]));
                }
            }

            // Step 2: Build boundary adjacency: for each boundary vertex v, map to its
            // successor in the boundary half-edge ring.  O(T) total, O(1) per step.
            // A directed edge (v0, v1) is a boundary edge iff (v1, v0) is absent.
            std::unordered_map<int32_t, std::vector<int32_t>> boundaryNext;
            for (int64_t de : directedEdges)
            {
                int32_t v0 = static_cast<int32_t>(de >> 32);
                int32_t v1 = static_cast<int32_t>(static_cast<uint32_t>(de));
                if (directedEdges.find(encodeEdge(v1, v0)) == directedEdges.end())
                {
                    // (v0, v1) is a boundary directed edge
                    boundaryNext[v0].push_back(v1);
                }
            }

            // Step 3: Detect holes (boundary loops) using O(1) adjacency lookups.
            std::vector<HoleBoundary> holes;
            DetectHolesFromAdjacency(vertices, boundaryNext, holes);

            if (holes.empty())
            {
                return;
            }

            // Save a snapshot of the mesh so we can roll back on validation
            // failure (only when validateOutput is requested).
            std::vector<Vector3<Real>> savedVertices;
            std::vector<std::array<int32_t, 3>> savedTriangles;
            if (params.validateOutput)
            {
                savedVertices  = vertices;
                savedTriangles = triangles;
            }

            // Step 3: Triangulate and add holes that meet criteria
            size_t numFilled = 0;
            for (auto const& hole : holes)
            {
                // Check size constraints
                if (params.maxEdges > 0 &&
                    hole.vertices.size() > params.maxEdges)
                {
                    continue;
                }

                // Triangulate hole using appropriate method
                std::vector<std::array<int32_t, 3>> newTriangles;
                
                // Determine which method to use
                TriangulationMethod actualMethod = params.method;
                
                // When planarityThreshold is set and exceeded, skip the 2D projection
                // methods and start from LSCM (which always produces a valid 2D polygon)
                // rather than jumping straight to 3D.  LSCM will itself fall back to 3D
                // if it also fails.
                if (params.planarityThreshold > static_cast<Real>(0) &&
                    (params.method == TriangulationMethod::EarClipping ||
                     params.method == TriangulationMethod::CDT))
                {
                    Vector3<Real> normal = ComputeHoleNormal(vertices, hole);
                    Real planarity = ComputeHolePlanarity(vertices, hole, normal);
                    Real holeSize = EstimateHoleSize(vertices, hole);
                    Real relativeDeviation = holeSize > static_cast<Real>(0) ?
                        planarity / holeSize : planarity;

                    if (relativeDeviation > params.planarityThreshold)
                    {
                        // Too non-planar for best-fit-plane projection; try LSCM first
                        actualMethod = TriangulationMethod::LSCM;
                    }
                }
                
                bool success = false;
                if (actualMethod == TriangulationMethod::EarClipping3D)
                {
                    success = TriangulateHole3D(vertices, hole, newTriangles);
                }
                else if (actualMethod == TriangulationMethod::LSCM)
                {
                    // LSCM arc-length circle mapping: always succeeds for any simple loop
                    success = TriangulateHoleLSCM(vertices, hole, newTriangles);
                    // Fall back to 3D if LSCM somehow fails
                    if (!success && params.autoFallback)
                    {
                        success = TriangulateHole3D(vertices, hole, newTriangles);
                    }
                }
                else
                {
                    // EC or CDT: 2D projection methods
                    success = TriangulateHole(vertices, hole, newTriangles, actualMethod);
                    
                    // Automatic fallback chain: LSCM → 3D
                    if (!success && params.autoFallback)
                    {
                        success = TriangulateHoleLSCM(vertices, hole, newTriangles);
                    }
                    if (!success && params.autoFallback)
                    {
                        success = TriangulateHole3D(vertices, hole, newTriangles);
                    }
                }
                
                if (success)
                {
                    // Compute area of new triangles
                    Real holeArea = ComputeTriangulationArea(vertices, newTriangles);

                    if (params.maxArea <= static_cast<Real>(0) || holeArea < params.maxArea)
                    {
                        // Reverse winding of new triangles before adding to mesh.
                        // The hole boundary is traced as a directed boundary loop
                        // (clockwise when viewed from outside for a CCW mesh), so the
                        // triangulation produces triangles with inward-facing normals.
                        // Reversing v1 <-> v2 corrects this, matching geogram's
                        // fill_holes which inserts triangles as
                        // create_triangle(indices[2], indices[1], indices[0]).
                        for (auto& tri : newTriangles)
                        {
                            // tri = {v0, v1, v2}: swap v1 and v2 to flip winding
                            std::swap(tri[1], tri[2]);
                        }
                        triangles.insert(triangles.end(), newTriangles.begin(), newTriangles.end());
                        ++numFilled;
                    }
                }
            }

            // Step 4: (repair after filling is the caller's responsibility)
            // MeshRepair::Repair(RECONSTRUCT) or MeshRepair::Repair(DEFAULT)
            // should be called by the user when topology cleanup is needed after
            // FillHoles.  This avoids a circular include dependency.

            // Step 5: Optional output validation.  When requireManifold is set
            // and the result fails the manifold check, restore the pre-fill
            // snapshot so the caller receives the original (unmodified) mesh.
            // Validation is only performed when validateOutput is true and at
            // least one pass/fail criterion (requireManifold or
            // requireNoSelfIntersections) is enabled, since without a criterion
            // the result cannot be acted upon.
            if (numFilled > 0 && params.validateOutput &&
                (params.requireManifold || params.requireNoSelfIntersections))
            {
                auto validationResult = MeshValidation<Real>::Validate(
                    vertices, triangles, params.requireNoSelfIntersections);
                if (params.requireManifold && !validationResult.isManifold)
                {
                    vertices  = std::move(savedVertices);
                    triangles = std::move(savedTriangles);
                }
            }
        }

    private:
        // Detect boundary loops using a prebuilt boundary adjacency map.
        // Each entry boundaryNext[v] holds the list of vertices reachable from v
        // along a boundary directed edge.  Traversal is O(1) per step.
        static void DetectHolesFromAdjacency(
            std::vector<Vector3<Real>> const& vertices,
            std::unordered_map<int32_t, std::vector<int32_t>> const& boundaryNext,
            std::vector<HoleBoundary>& holes)
        {
            // Track visited directed boundary edges (encoded as int64_t).
            std::unordered_set<int64_t> visitedEdges;
            visitedEdges.reserve(boundaryNext.size() * 2);

            for (auto const& [startV, succs] : boundaryNext)
            {
                for (int32_t firstNext : succs)
                {
                    int64_t startKey = (static_cast<int64_t>(startV) << 32)
                                     | static_cast<uint32_t>(firstNext);
                    if (visitedEdges.count(startKey))
                    {
                        continue;  // Already part of a traced loop
                    }

                    // Trace the loop starting from (startV → firstNext).
                    HoleBoundary hole;
                    int32_t curV = startV;
                    int32_t nxtV = firstNext;
                    // Safety cap: a valid boundary loop cannot be longer than the number
                    // of boundary directed edges (one edge per loop vertex).
                    const int maxIter = static_cast<int>(boundaryNext.size()) + 1;
                    int iter = 0;
                    bool closed = false;

                    do
                    {
                        int64_t edgeKey = (static_cast<int64_t>(curV) << 32)
                                        | static_cast<uint32_t>(nxtV);

                        if (visitedEdges.count(edgeKey))
                        {
                            // Hit a visited edge mid-trace — non-simple boundary; abort.
                            hole.vertices.clear();
                            break;
                        }

                        hole.vertices.push_back(curV);
                        visitedEdges.insert(edgeKey);

                        int32_t prevV = curV;
                        curV = nxtV;
                        nxtV = -1;

                        // O(1) adjacency lookup: find successor from curV that isn't prevV.
                        auto it = boundaryNext.find(curV);
                        if (it != boundaryNext.end())
                        {
                            for (int32_t nb : it->second)
                            {
                                if (nb != prevV)
                                {
                                    nxtV = nb;
                                    break;
                                }
                            }
                        }

                        if (nxtV == -1)
                        {
                            hole.vertices.clear();  // Dead end — broken boundary
                            break;
                        }

                        if (++iter > maxIter)
                        {
                            hole.vertices.clear();  // Safety: unexpectedly long loop
                            break;
                        }

                        if (curV == startV)
                        {
                            closed = true;
                            break;
                        }

                    } while (true);

                    if (closed && hole.vertices.size() >= 3)
                    {
                        holes.push_back(std::move(hole));
                    }
                }
            }
        }

        // Detect boundary loops (holes) using edge adjacency map.
        // LEGACY: kept for reference; replaced by DetectHolesFromAdjacency above.
        static void DetectHolesFromEdges(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> const& edgeToTriangles,
            std::vector<HoleBoundary>& holes)
        {
            // Track visited boundary edges
            std::set<std::pair<int32_t, int32_t>> visitedEdges;

            // Find boundary edges (edges with no opposite edge)
            for (auto const& entry : edgeToTriangles)
            {
                int32_t v0 = entry.first.first;
                int32_t v1 = entry.first.second;

                // Check if the opposite edge exists
                auto oppositeEdge = std::make_pair(v1, v0);
                if (edgeToTriangles.find(oppositeEdge) == edgeToTriangles.end())
                {
                    // This is a boundary edge
                    if (visitedEdges.find(entry.first) != visitedEdges.end())
                    {
                        continue; // Already processed
                    }

                    // Trace the boundary loop starting from this edge
                    HoleBoundary hole;
                    if (TraceHoleBoundaryFromEdges(edgeToTriangles, v0, v1, visitedEdges, hole) &&
                        hole.vertices.size() >= 3)
                    {
                        holes.push_back(hole);
                    }
                }
            }
        }

        // Trace a boundary loop starting from edge (v0, v1) using edge map.
        static bool TraceHoleBoundaryFromEdges(
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> const& edgeToTriangles,
            int32_t startV0,
            int32_t startV1,
            std::set<std::pair<int32_t, int32_t>>& visitedEdges,
            HoleBoundary& hole)
        {
            int32_t currentV = startV0;
            int32_t nextV = startV1;
            int maxIterations = 10000; // Prevent infinite loops
            int iterations = 0;

            do
            {
                hole.vertices.push_back(currentV);

                // Mark edge as visited
                visitedEdges.insert(std::make_pair(currentV, nextV));

                // Find the next boundary edge starting from nextV
                int32_t prevV = currentV;
                currentV = nextV;
                nextV = FindNextBoundaryVertexFromEdges(edgeToTriangles, prevV, currentV);

                if (nextV == -1)
                {
                    // Failed to close the loop
                    return false;
                }

                if (++iterations > maxIterations)
                {
                    // Safety check to prevent infinite loops
                    return false;
                }

            } while (currentV != startV0);

            return true;
        }

        // Find the next vertex along the boundary from (prevV, currentV).
        static int32_t FindNextBoundaryVertexFromEdges(
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> const& edgeToTriangles,
            int32_t prevV,
            int32_t currentV)
        {
            // Look for an outgoing boundary edge from currentV
            for (auto const& entry : edgeToTriangles)
            {
                int32_t v0 = entry.first.first;
                int32_t v1 = entry.first.second;

                // Check if this edge starts at currentV and doesn't go back to prevV
                if (v0 == currentV && v1 != prevV)
                {
                    // Check if the opposite edge exists
                    auto oppositeEdge = std::make_pair(v1, v0);
                    if (edgeToTriangles.find(oppositeEdge) == edgeToTriangles.end())
                    {
                        // This is a boundary edge
                        return v1;
                    }
                }
            }

            return -1; // Not found
        }

        // Detect boundary loops (holes) in the mesh (old ETManifoldMesh-based version).
        static void DetectHoles(
            std::vector<Vector3<Real>> const& vertices,
            ETManifoldMesh const& mesh,
            std::vector<HoleBoundary>& holes)
        {
            // Track visited boundary edges
            std::set<std::pair<int32_t, int32_t>> visitedEdges;

            // Iterate through all edges in the mesh
            for (auto const& edgePair : mesh.GetEdges())
            {
                auto const& edge = *edgePair.second;

                // Check if this is a boundary edge (has only one triangle)
                if (edge.T[1] == nullptr)
                {
                    int32_t v0 = edge.V[0];
                    int32_t v1 = edge.V[1];
                    std::pair<int32_t, int32_t> edgeKey = std::make_pair(
                        std::min(v0, v1), std::max(v0, v1));

                    if (visitedEdges.find(edgeKey) != visitedEdges.end())
                    {
                        continue; // Already processed this boundary edge
                    }

                    // Trace the boundary loop starting from this edge
                    HoleBoundary hole;
                    TraceHoleBoundary(mesh, v0, v1, visitedEdges, hole);

                    if (hole.vertices.size() >= 3)
                    {
                        holes.push_back(hole);
                    }
                }
            }
        }

        // Trace a boundary loop starting from edge (v0, v1).
        static void TraceHoleBoundary(
            ETManifoldMesh const& mesh,
            int32_t startV0,
            int32_t startV1,
            std::set<std::pair<int32_t, int32_t>>& visitedEdges,
            HoleBoundary& hole)
        {
            int32_t currentV = startV0;
            int32_t nextV = startV1;

            do
            {
                hole.vertices.push_back(currentV);

                // Mark edge as visited
                std::pair<int32_t, int32_t> edgeKey = std::make_pair(
                    std::min(currentV, nextV), std::max(currentV, nextV));
                visitedEdges.insert(edgeKey);

                // Find the next boundary edge starting from nextV
                int32_t prevV = currentV;
                currentV = nextV;
                nextV = FindNextBoundaryVertex(mesh, prevV, currentV);

                if (nextV == -1)
                {
                    // Failed to close the loop
                    hole.vertices.clear();
                    return;
                }

            } while (currentV != startV0);
        }

        // Find the next vertex along the boundary from (prevV, currentV).
        static int32_t FindNextBoundaryVertex(
            ETManifoldMesh const& mesh,
            int32_t prevV,
            int32_t currentV)
        {
            // Find all edges connected to currentV
            for (auto const& edgePair : mesh.GetEdges())
            {
                auto const& edge = *edgePair.second;

                // Check if this edge includes currentV and is a boundary edge
                if (edge.T[1] == nullptr)
                {
                    if (edge.V[0] == currentV && edge.V[1] != prevV)
                    {
                        return edge.V[1];
                    }
                    if (edge.V[1] == currentV && edge.V[0] != prevV)
                    {
                        return edge.V[0];
                    }
                }
            }

            return -1; // Not found
        }

        // Estimate hole size (for relative planarity threshold)
        static Real EstimateHoleSize(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole)
        {
            if (hole.vertices.size() < 2)
            {
                return static_cast<Real>(0);
            }
            
            // Use perimeter as hole size estimate
            Real perimeter = static_cast<Real>(0);
            size_t n = hole.vertices.size();
            for (size_t i = 0; i < n; ++i)
            {
                Vector3<Real> const& p0 = vertices[hole.vertices[i]];
                Vector3<Real> const& p1 = vertices[hole.vertices[(i + 1) % n]];
                perimeter += Length(p1 - p0);
            }
            
            return perimeter;
        }

        // Triangulate a hole using LSCM arc-length circle mapping.
        //
        // Maps the 3D boundary loop to a 2D polygon inscribed on a unit circle
        // via arc-length parameterization, then triangulates in 2D using EC.
        // Because a circle-inscribed polygon is always convex (vertices are laid
        // out in arc-length order around the circle), EC never fails, making
        // this method a reliable second-to-last resort before 3D ear clipping.
        //
        // The key advantage over orthographic projection: a non-planar boundary
        // whose orthographic projection self-intersects still maps to a valid
        // (non-self-intersecting) polygon under arc-length circle parameterization.
        static bool TriangulateHoleLSCM(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }

            // Map boundary to 2D circle positions via arc-length parameterization
            std::vector<Vector2<Real>> uv;
            if (!LSCMParameterization<Real>::MapBoundaryToCircle(vertices, hole.vertices, uv))
            {
                return false;
            }

            // uv is parallel to hole.vertices; build local triangles in UV space
            std::vector<std::array<int32_t, 3>> localTriangles;
            if (!TriangulateWithEC(uv, localTriangles))
            {
                return false;
            }

            // Map local indices back to global mesh indices
            for (auto const& tri : localTriangles)
            {
                triangles.push_back({
                    hole.vertices[tri[0]],
                    hole.vertices[tri[1]],
                    hole.vertices[tri[2]]
                });
            }
            return true;
        }

        // Triangulate a hole working directly in 3D (no projection)
        // Ported from Geogram's ear cutting algorithm
        static bool TriangulateHole3D(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }
            
            if (hole.vertices.size() == 3)
            {
                triangles.push_back({hole.vertices[0], hole.vertices[1], hole.vertices[2]});
                return true;
            }
            
            // Create working copy as EdgeTriple (stores 3 consecutive vertices)
            struct EdgeTriple
            {
                int32_t v0, v1, v2;  // Three consecutive vertices
            };
            
            std::vector<EdgeTriple> workingHole;
            workingHole.reserve(hole.vertices.size());
            
            size_t n = hole.vertices.size();
            for (size_t i = 0; i < n; ++i)
            {
                EdgeTriple et;
                et.v0 = hole.vertices[i];
                et.v1 = hole.vertices[(i + 1) % n];
                et.v2 = hole.vertices[(i + 2) % n];
                workingHole.push_back(et);
            }
            
            // Ear cutting loop - select best ear at each iteration
            while (workingHole.size() > 3)
            {
                int32_t bestIdx = -1;
                Real bestScore = -std::numeric_limits<Real>::max();
                
                // Find best ear based on 3D geometric quality
                for (size_t i = 0; i < workingHole.size(); ++i)
                {
                    size_t nextIdx = (i + 1) % workingHole.size();
                    Real score = ComputeEarScore3D(vertices, 
                        workingHole[i], workingHole[nextIdx]);
                    
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIdx = static_cast<int32_t>(i);
                    }
                }
                
                if (bestIdx < 0)
                {
                    return false; // Failed to find valid ear
                }
                
                size_t nextIdx = (bestIdx + 1) % workingHole.size();
                
                // Create triangle from best ear
                EdgeTriple const& t1 = workingHole[bestIdx];
                EdgeTriple const& t2 = workingHole[nextIdx];
                
                triangles.push_back({t1.v0, t2.v1, t1.v1});
                
                // Update working hole by merging the two edge triples
                EdgeTriple merged;
                merged.v0 = t1.v0;
                merged.v1 = t2.v1;
                merged.v2 = workingHole[(nextIdx + 1) % workingHole.size()].v1;
                
                workingHole[bestIdx] = merged;
                workingHole.erase(workingHole.begin() + nextIdx);
            }
            
            // Add final triangle
            if (workingHole.size() == 3)
            {
                triangles.push_back({
                    workingHole[0].v0,
                    workingHole[1].v0,
                    workingHole[2].v0
                });
            }
            
            return true;
        }
        
        // Compute ear quality score in 3D (ported from Geogram)
        // Higher score = better triangle
        template <typename EdgeTriple>
        static Real ComputeEarScore3D(
            std::vector<Vector3<Real>> const& vertices,
            EdgeTriple const& t1,
            EdgeTriple const& t2)
        {
            // Get triangle vertices
            Vector3<Real> const& p10 = vertices[t1.v0];
            Vector3<Real> const& p11 = vertices[t1.v1];
            Vector3<Real> const& p12 = vertices[t1.v2];
            Vector3<Real> const& p20 = vertices[t2.v0];
            Vector3<Real> const& p21 = vertices[t2.v1];
            Vector3<Real> const& p22 = vertices[t2.v2];
            
            // Compute averaged normal
            Vector3<Real> n1 = Cross(p11 - p10, p12 - p10);
            Vector3<Real> n2 = Cross(p21 - p20, p22 - p20);
            Vector3<Real> n = n1 + n2;
            
            Real nlen = Length(n);
            if (nlen < static_cast<Real>(1e-10))
            {
                return -std::numeric_limits<Real>::max(); // Degenerate
            }
            n /= nlen;
            
            // Compute edge directions
            Vector3<Real> a = p11 - p10;
            Vector3<Real> b = p21 - p20;
            
            Real alen = Length(a);
            Real blen = Length(b);
            
            if (alen < static_cast<Real>(1e-10) || blen < static_cast<Real>(1e-10))
            {
                return -std::numeric_limits<Real>::max(); // Degenerate edge
            }
            
            a /= alen;
            b /= blen;
            
            // Score based on angle between edges
            // Prefer ears that maintain smooth transitions
            Vector3<Real> axb = Cross(a, b);
            Real score = -std::atan2(Dot(n, axb), Dot(a, b));
            
            return score;
        }

        // Triangulate a hole using GTE's triangulation algorithms (with projection).
        // Projects the 3D hole to 2D, triangulates, and maps back to 3D indices.
        static bool TriangulateHole(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles,
            TriangulationMethod method = TriangulationMethod::EarClipping)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }

            // Step 1: Compute plane for the hole (using Newell's method for robustness)
            Vector3<Real> normal = ComputeHoleNormal(vertices, hole);
            
            if (Length(normal) < static_cast<Real>(1e-10))
            {
                // Degenerate hole
                return false;
            }

            // Step 2: Create local 2D coordinate system for the hole
            Vector3<Real> uAxis, vAxis;
            ComputeTangentSpace(normal, uAxis, vAxis);

            // Step 3: Project hole vertices to 2D
            std::vector<Vector2<Real>> points2D;
            points2D.reserve(hole.vertices.size());
            
            for (int32_t vIdx : hole.vertices)
            {
                Vector3<Real> const& p3D = vertices[vIdx];
                Real u = Dot(p3D, uAxis);
                Real v = Dot(p3D, vAxis);
                points2D.push_back(Vector2<Real>{u, v});
            }

            // Step 3.5: CRITICAL - Check if projected polygon self-intersects
            // This is expected for highly non-planar holes (e.g., wrapping around sphere)
            if (Polygon2Validation<Real>::HasSelfIntersectingEdges(points2D))
            {
                // Projected polygon self-intersects - MUST use 3D method
                return false;  // Signal failure so auto-fallback can trigger
            }

            // Step 4: Triangulate in 2D using chosen method
            std::vector<std::array<int32_t, 3>> localTriangles;
            
            if (method == TriangulationMethod::EarClipping)
            {
                if (!TriangulateWithEC(points2D, localTriangles))
                {
                    return false;
                }
            }
            else // CDT
            {
                if (!TriangulateWithCDT(points2D, localTriangles))
                {
                    return false;
                }
            }

            // Step 5: Map local indices back to global mesh indices
            for (auto const& tri : localTriangles)
            {
                triangles.push_back({
                    hole.vertices[tri[0]],
                    hole.vertices[tri[1]],
                    hole.vertices[tri[2]]
                });
            }

            return true;
        }

        // Compute hole normal using Newell's method (robust for non-planar holes)
        static Vector3<Real> ComputeHoleNormal(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole)
        {
            Vector3<Real> normal{ static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0) };
            
            size_t n = hole.vertices.size();
            for (size_t i = 0; i < n; ++i)
            {
                Vector3<Real> const& curr = vertices[hole.vertices[i]];
                Vector3<Real> const& next = vertices[hole.vertices[(i + 1) % n]];
                
                normal[0] += (curr[1] - next[1]) * (curr[2] + next[2]);
                normal[1] += (curr[2] - next[2]) * (curr[0] + next[0]);
                normal[2] += (curr[0] - next[0]) * (curr[1] + next[1]);
            }
            
            Normalize(normal);
            return normal;
        }

        // Check how planar the hole is (returns max deviation from average plane)
        static Real ComputeHolePlanarity(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            Vector3<Real> const& normal)
        {
            if (hole.vertices.size() < 3)
            {
                return static_cast<Real>(0);
            }
            
            // Compute centroid
            Vector3<Real> centroid{ static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0) };
            for (int32_t vIdx : hole.vertices)
            {
                centroid += vertices[vIdx];
            }
            centroid /= static_cast<Real>(hole.vertices.size());
            
            // Measure maximum deviation from plane through centroid
            Real maxDeviation = static_cast<Real>(0);
            for (int32_t vIdx : hole.vertices)
            {
                Vector3<Real> const& v = vertices[vIdx];
                Real deviation = std::abs(Dot(v - centroid, normal));
                maxDeviation = std::max(maxDeviation, deviation);
            }
            
            return maxDeviation;
        }

        // Compute tangent space (u, v axes) from normal using GTE's robust method
        static void ComputeTangentSpace(
            Vector3<Real> const& normal,
            Vector3<Real>& uAxis,
            Vector3<Real>& vAxis)
        {
            // Use GTE's robust orthogonal complement computation
            // This is more numerically stable than our custom version
            Vector3<Real> v[3];
            v[0] = normal;
            
            // ComputeOrthogonalComplement generates orthonormal basis
            // with robust=true for better numerical stability
            ComputeOrthogonalComplement(1, v, true);
            
            uAxis = v[1];
            vAxis = v[2];
        }

        // Triangulate using GTE's Ear Clipping
        static bool TriangulateWithEC(
            std::vector<Vector2<Real>> const& points2D,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            try
            {
                // Use BSNumber for exact arithmetic (recommended for robustness)
                using ComputeType = BSNumber<UIntegerFP32<70>>;
                
                TriangulateEC<Real, ComputeType> triangulator(points2D);
                triangulator();  // Triangulate simple polygon
                
                triangles = triangulator.GetTriangles();
                return !triangles.empty();
            }
            catch (...)
            {
                // Fall back to floating-point if exact arithmetic fails
                try
                {
                    TriangulateEC<Real, Real> triangulator(points2D);
                    triangulator();
                    triangles = triangulator.GetTriangles();
                    return !triangles.empty();
                }
                catch (...)
                {
                    return false;
                }
            }
        }

        // Triangulate using GTE's Constrained Delaunay Triangulation
        static bool TriangulateWithCDT(
            std::vector<Vector2<Real>> const& points2D,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            try
            {
                // Create polygon tree (single outer polygon, no holes)
                auto tree = std::make_shared<PolygonTree>();
                tree->polygon.resize(points2D.size());
                std::iota(tree->polygon.begin(), tree->polygon.end(), 0);
                
                // Use BSNumber for exact arithmetic
                using ComputeType = BSNumber<UIntegerFP32<70>>;
                
                TriangulateCDT<Real, ComputeType> triangulator;
                PolygonTreeEx outputTree;
                
                triangulator(points2D, tree, outputTree);
                
                // Extract triangles from output tree
                for (auto const& tri : outputTree.allTriangles)
                {
                    triangles.push_back(tri);
                }
                
                return !triangles.empty();
            }
            catch (...)
            {
                // CDT failed, fall back to EC
                return TriangulateWithEC(points2D, triangles);
            }
        }

        // Compute total area of triangulation.
        static Real ComputeTriangulationArea(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            Real totalArea = static_cast<Real>(0);

            for (auto const& tri : triangles)
            {
                Vector3<Real> const& p0 = vertices[tri[0]];
                Vector3<Real> const& p1 = vertices[tri[1]];
                Vector3<Real> const& p2 = vertices[tri[2]];

                Vector3<Real> edge1 = p1 - p0;
                Vector3<Real> edge2 = p2 - p0;
                Vector3<Real> normal = Cross(edge1, edge2);

                totalArea += Length(normal) * static_cast<Real>(0.5);
            }

            return totalArea;
        }
    };
}
