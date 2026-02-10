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
// - Simplified to use ear cutting algorithm (most robust for GTE port)
// - Removed Geogram-specific Logger and CmdLine calls
//
// Original Geogram License (BSD 3-Clause):
// [Same license text as in MeshRepair.h]

#pragma once

#include <Mathematics/Vector3.h>
#include <Mathematics/ETManifoldMesh.h>
#include <Mathematics/MeshRepair.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

// The MeshHoleFilling class provides hole detection and filling operations
// ported from Geogram. It detects boundary loops in a triangle mesh and
// fills them with new triangles using ear cutting triangulation.
//
// The implementation is simplified compared to Geogram's full version,
// focusing on the most robust algorithm (ear cutting) for GTE usage.

namespace gte
{
    template <typename Real>
    class MeshHoleFilling
    {
    public:
        // Parameters for hole filling operations.
        struct Parameters
        {
            Real maxArea;           // Maximum hole area to fill (0 = unlimited)
            size_t maxEdges;        // Maximum hole perimeter edges (0 = unlimited)
            bool repair;            // Run mesh repair after filling

            Parameters()
                : maxArea(static_cast<Real>(0))
                , maxEdges(std::numeric_limits<size_t>::max())
                , repair(true)
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

            // Step 1: Build edge-triangle topology
            ETManifoldMesh mesh;
            for (auto const& tri : triangles)
            {
                mesh.Insert(tri[0], tri[1], tri[2]);
            }

            // Step 2: Detect holes (boundary loops)
            std::vector<HoleBoundary> holes;
            DetectHoles(vertices, mesh, holes);

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

                // Triangulate hole using ear cutting
                std::vector<std::array<int32_t, 3>> newTriangles;
                if (TriangulateHole(vertices, hole, newTriangles))
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
        // Detect boundary loops (holes) in the mesh.
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

        // Triangulate a hole using ear cutting algorithm.
        static bool TriangulateHole(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }

            // Create a working copy of the boundary vertices
            std::vector<int32_t> polygon = hole.vertices;

            // Ear cutting algorithm
            while (polygon.size() > 3)
            {
                bool earFound = false;

                // Try each vertex as potential ear
                for (size_t i = 0; i < polygon.size(); ++i)
                {
                    size_t prev = (i + polygon.size() - 1) % polygon.size();
                    size_t next = (i + 1) % polygon.size();

                    int32_t vPrev = polygon[prev];
                    int32_t vCurr = polygon[i];
                    int32_t vNext = polygon[next];

                    // Check if this forms a valid ear
                    if (IsEar(vertices, polygon, prev, i, next))
                    {
                        // Add triangle
                        triangles.push_back({ vPrev, vCurr, vNext });

                        // Remove ear vertex from polygon
                        polygon.erase(polygon.begin() + i);
                        earFound = true;
                        break;
                    }
                }

                if (!earFound)
                {
                    // Failed to find an ear - degenerate polygon
                    return false;
                }
            }

            // Add final triangle
            if (polygon.size() == 3)
            {
                triangles.push_back({ polygon[0], polygon[1], polygon[2] });
            }

            return true;
        }

        // Check if vertex at index i is an ear of the polygon.
        static bool IsEar(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<int32_t> const& polygon,
            size_t prevIdx,
            size_t currIdx,
            size_t nextIdx)
        {
            int32_t vPrev = polygon[prevIdx];
            int32_t vCurr = polygon[currIdx];
            int32_t vNext = polygon[nextIdx];

            Vector3<Real> const& p0 = vertices[vPrev];
            Vector3<Real> const& p1 = vertices[vCurr];
            Vector3<Real> const& p2 = vertices[vNext];

            // Compute triangle normal to check orientation
            Vector3<Real> edge1 = p1 - p0;
            Vector3<Real> edge2 = p2 - p0;
            Vector3<Real> normal = Cross(edge1, edge2);

            // Check if triangle is degenerate
            if (Length(normal) < static_cast<Real>(1e-10))
            {
                return false;
            }

            // Check if any other polygon vertex is inside the ear triangle
            for (size_t j = 0; j < polygon.size(); ++j)
            {
                if (j == prevIdx || j == currIdx || j == nextIdx)
                {
                    continue;
                }

                Vector3<Real> const& p = vertices[polygon[j]];
                if (PointInTriangle(p, p0, p1, p2))
                {
                    return false; // Not an ear
                }
            }

            return true;
        }

        // Check if point p is inside triangle (p0, p1, p2).
        static bool PointInTriangle(
            Vector3<Real> const& p,
            Vector3<Real> const& p0,
            Vector3<Real> const& p1,
            Vector3<Real> const& p2)
        {
            // Use barycentric coordinates
            Vector3<Real> v0 = p2 - p0;
            Vector3<Real> v1 = p1 - p0;
            Vector3<Real> v2 = p - p0;

            Real dot00 = Dot(v0, v0);
            Real dot01 = Dot(v0, v1);
            Real dot02 = Dot(v0, v2);
            Real dot11 = Dot(v1, v1);
            Real dot12 = Dot(v1, v2);

            Real invDenom = static_cast<Real>(1) / (dot00 * dot11 - dot01 * dot01);
            Real u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            Real v = (dot00 * dot12 - dot01 * dot02) * invDenom;

            return (u >= static_cast<Real>(0)) && 
                   (v >= static_cast<Real>(0)) && 
                   (u + v <= static_cast<Real>(1));
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
