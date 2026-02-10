// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.10
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
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/TriangulateEC.h>
#include <GTE/Mathematics/TriangulateCDT.h>
#include <GTE/Mathematics/PolygonTree.h>
#include <GTE/Mathematics/ArbitraryPrecision.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

// The MeshHoleFilling class provides hole detection and filling operations
// ported from Geogram. It detects boundary loops in a triangle mesh and
// fills them with new triangles using GTE's robust triangulation algorithms.
//
// Supports two triangulation methods:
// - Ear Clipping (EC): Simple and fast, good for most holes
// - Constrained Delaunay Triangulation (CDT): More robust, handles complex holes

namespace gte
{
    template <typename Real>
    class MeshHoleFilling
    {
    public:
        // Triangulation method for hole filling
        enum class TriangulationMethod
        {
            EarClipping,    // Use ear clipping (fast, simple)
            CDT             // Use Constrained Delaunay Triangulation (robust, handles complex cases)
        };

        // Parameters for hole filling operations.
        struct Parameters
        {
            Real maxArea;                       // Maximum hole area to fill (0 = unlimited)
            size_t maxEdges;                    // Maximum hole perimeter edges (0 = unlimited)
            bool repair;                        // Run mesh repair after filling
            TriangulationMethod method;         // Triangulation algorithm to use

            Parameters()
                : maxArea(static_cast<Real>(0))
                , maxEdges(std::numeric_limits<size_t>::max())
                , repair(true)
                , method(TriangulationMethod::EarClipping)  // Default to EC for compatibility
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
        // Only fills holes with area < maxArea and edges < maxEdges.
        static void FillHoles(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters())
        {
            if (params.maxArea == static_cast<Real>(0) && 
                params.maxEdges == 0)
            {
                return;
            }

            // Step 1: Build edge-to-triangle map to detect boundaries
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>> edgeToTriangles;

            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    
                    // Store directed edge
                    edgeToTriangles[std::make_pair(v0, v1)].push_back(i);
                }
            }

            // Step 2: Detect holes (boundary loops) using edge adjacency
            std::vector<HoleBoundary> holes;
            DetectHolesFromEdges(vertices, triangles, edgeToTriangles, holes);

            if (holes.empty())
            {
                return;
            }

            // Step 3: Triangulate and add holes that meet criteria
            size_t numFilled = 0;
            for (auto const& hole : holes)
            {
                // Check size constraints
                if (params.maxEdges < std::numeric_limits<size_t>::max() &&
                    hole.vertices.size() > params.maxEdges)
                {
                    continue;
                }

                // Triangulate hole using GTE's algorithms
                std::vector<std::array<int32_t, 3>> newTriangles;
                if (TriangulateHole(vertices, hole, newTriangles, params.method))
                {
                    // Compute area of new triangles
                    Real holeArea = ComputeTriangulationArea(vertices, newTriangles);

                    if (params.maxArea <= static_cast<Real>(0) || holeArea < params.maxArea)
                    {
                        // Add new triangles to mesh
                        triangles.insert(triangles.end(), newTriangles.begin(), newTriangles.end());
                        ++numFilled;
                    }
                }
            }

            // Step 4: Optional repair after filling
            if (numFilled > 0 && params.repair)
            {
                typename MeshRepair<Real>::Parameters repairParams;
                repairParams.mode = MeshRepair<Real>::RepairMode::DEFAULT;
                MeshRepair<Real>::Repair(vertices, triangles, repairParams);
            }
        }

    private:
        // Detect boundary loops (holes) using edge adjacency map.
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

        // Triangulate a hole using GTE's triangulation algorithms.
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

        // Compute tangent space (u, v axes) from normal
        static void ComputeTangentSpace(
            Vector3<Real> const& normal,
            Vector3<Real>& uAxis,
            Vector3<Real>& vAxis)
        {
            // Find axis least aligned with normal
            Vector3<Real> candidate{ static_cast<Real>(1), static_cast<Real>(0), static_cast<Real>(0) };
            if (std::abs(Dot(normal, candidate)) > static_cast<Real>(0.9))
            {
                candidate = Vector3<Real>{ static_cast<Real>(0), static_cast<Real>(1), static_cast<Real>(0) };
            }
            
            uAxis = Cross(candidate, normal);
            Normalize(uAxis);
            vAxis = Cross(normal, uAxis);
            Normalize(vAxis);
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
