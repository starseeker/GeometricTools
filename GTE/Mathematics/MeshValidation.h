// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Mesh validation utilities: manifold property, orientation, and optional
// self-intersection detection.

#pragma once

#include <Mathematics/IntrTriangle3Triangle3.h>
#include <Mathematics/Triangle.h>
#include <Mathematics/Vector3.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gte
{
    template <typename Real>
    class MeshValidation
    {
    public:
        struct ValidationResult
        {
            bool isValid;
            bool isManifold;
            bool hasSelfIntersections;
            bool isOriented;
            bool isClosed;

            size_t nonManifoldEdges;
            size_t boundaryEdges;
            size_t intersectingTrianglePairs;

            std::string errorMessage;

            ValidationResult()
                : isValid(false)
                , isManifold(false)
                , hasSelfIntersections(false)
                , isOriented(false)
                , isClosed(false)
                , nonManifoldEdges(0)
                , boundaryEdges(0)
                , intersectingTrianglePairs(0)
            {
            }
        };

        // Full validation: manifoldness, orientation, and optionally
        // self-intersections (O(n²) – use sparingly on large meshes).
        static ValidationResult Validate(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            bool checkSelfIntersections = true)
        {
            ValidationResult result;

            if (vertices.empty() || triangles.empty())
            {
                result.errorMessage = "Empty mesh";
                return result;
            }

            // Validate all vertex indices are in range.
            int32_t numVerts = static_cast<int32_t>(vertices.size());
            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    if (tri[i] < 0 || tri[i] >= numVerts)
                    {
                        result.errorMessage = "Invalid vertex index in triangle";
                        return result;
                    }
                }
            }

            CheckManifold(triangles, result);

            if (result.isManifold)
            {
                CheckOrientation(triangles, result);
            }

            if (checkSelfIntersections)
            {
                CheckSelfIntersections(vertices, triangles, result);
            }

            result.isValid = result.isManifold && !result.hasSelfIntersections;

            if (result.isValid)
            {
                result.errorMessage = "Mesh is valid";
            }
            else if (!result.isManifold)
            {
                result.errorMessage = "Mesh is non-manifold";
            }
            else
            {
                result.errorMessage = "Mesh has self-intersections";
            }

            return result;
        }

        // Returns true if the mesh has no non-manifold edges.
        static bool IsManifold(
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            ValidationResult result;
            CheckManifold(triangles, result);
            return result.isManifold;
        }

        // Returns true if any pair of non-adjacent triangles intersects.
        static bool HasSelfIntersections(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            ValidationResult result;
            CheckSelfIntersections(vertices, triangles, result);
            return result.hasSelfIntersections;
        }

    private:
        // Count edges by their undirected representation.
        // An edge appearing once is a boundary edge; appearing more than twice
        // indicates a non-manifold configuration.
        static void CheckManifold(
            std::vector<std::array<int32_t, 3>> const& triangles,
            ValidationResult& result)
        {
            std::map<std::pair<int32_t, int32_t>, size_t> edgeCount;

            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    auto edge = std::make_pair(
                        std::min(v0, v1), std::max(v0, v1));
                    ++edgeCount[edge];
                }
            }

            result.nonManifoldEdges = 0;
            result.boundaryEdges    = 0;

            for (auto const& ep : edgeCount)
            {
                if (ep.second == 1)
                {
                    ++result.boundaryEdges;
                }
                else if (ep.second > 2)
                {
                    ++result.nonManifoldEdges;
                }
            }

            result.isManifold = (result.nonManifoldEdges == 0);
            result.isClosed   = (result.boundaryEdges == 0);
        }

        // A consistently oriented mesh has each directed edge in exactly one
        // triangle (the reverse direction in the adjacent triangle).
        static void CheckOrientation(
            std::vector<std::array<int32_t, 3>> const& triangles,
            ValidationResult& result)
        {
            std::map<std::pair<int32_t, int32_t>, int> directedEdgeCount;

            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    ++directedEdgeCount[{v0, v1}];
                }
            }

            bool oriented = true;
            for (auto const& ep : directedEdgeCount)
            {
                if (ep.second > 1)
                {
                    oriented = false;
                    break;
                }
            }

            result.isOriented = oriented;
        }

        // Brute-force O(n²) self-intersection check using GTE's triangle-triangle
        // intersection query.  Adjacent triangles (sharing a vertex) are skipped.
        static void CheckSelfIntersections(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            ValidationResult& result)
        {
            result.intersectingTrianglePairs = 0;

            size_t n = triangles.size();
            for (size_t i = 0; i < n; ++i)
            {
                auto const& t1i = triangles[i];
                Triangle3<Real> tri1(
                    vertices[t1i[0]], vertices[t1i[1]], vertices[t1i[2]]);

                for (size_t j = i + 1; j < n; ++j)
                {
                    auto const& t2j = triangles[j];

                    // Skip adjacent (sharing at least one vertex) triangles.
                    bool adjacent = false;
                    for (int a = 0; a < 3 && !adjacent; ++a)
                    for (int b = 0; b < 3 && !adjacent; ++b)
                    {
                        adjacent = (t1i[a] == t2j[b]);
                    }
                    if (adjacent)
                    {
                        continue;
                    }

                    Triangle3<Real> tri2(
                        vertices[t2j[0]], vertices[t2j[1]], vertices[t2j[2]]);

                    FIQuery<Real, Triangle3<Real>, Triangle3<Real>> query;
                    if (query(tri1, tri2).intersect)
                    {
                        ++result.intersectingTrianglePairs;
                    }
                }
            }

            result.hasSelfIntersections =
                (result.intersectingTrianglePairs > 0);
        }
    };
}
