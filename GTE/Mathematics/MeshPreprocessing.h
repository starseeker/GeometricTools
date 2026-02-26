// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Ported from Geogram: https://github.com/BrunoLevy/geogram
// Original files: src/lib/geogram/mesh/mesh_preprocessing.cpp,
//                 mesh_preprocessing.h
// Original license: BSD 3-Clause
// Copyright (c) 2000-2022 Inria
//
// Adaptations for Geometric Tools Engine:
// - Changed from GEO::Mesh to std::vector<Vector3<Real>> and
//   std::vector<std::array<int32_t, 3>>
// - Removed Geogram-specific Logger calls

#pragma once

#include <Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <vector>

// The MeshPreprocessing class provides mesh preprocessing operations ported
// from Geogram, including:
// - Removal of facets whose area is below a threshold
// - Removal of small connected components
// - Normal orientation consistency
// - Normal inversion

namespace gte
{
    template <typename Real>
    class MeshPreprocessing
    {
    public:
        // Remove triangles with area strictly less than minFacetArea.
        static void RemoveSmallFacets(
            std::vector<Vector3<Real>>& /* vertices */,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<Vector3<Real>> const& verticesRead,
            Real minFacetArea)
        {
            if (minFacetArea <= static_cast<Real>(0))
            {
                return;
            }

            std::vector<std::array<int32_t, 3>> newTriangles;
            newTriangles.reserve(triangles.size());

            for (auto const& tri : triangles)
            {
                if (ComputeTriangleArea(verticesRead, tri) >= minFacetArea)
                {
                    newTriangles.push_back(tri);
                }
            }

            triangles = std::move(newTriangles);
        }

        // Convenience overload: vertices and triangles are both in/out.
        static void RemoveSmallFacets(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real minFacetArea)
        {
            RemoveSmallFacets(vertices, triangles, vertices, minFacetArea);
        }

        // Remove connected components whose total surface area is less than
        // minComponentArea or whose triangle count is less than minComponentFacets.
        // Either threshold can be set to 0 to disable it.
        static void RemoveSmallComponents(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real minComponentArea,
            size_t minComponentFacets = 0)
        {
            if (triangles.empty())
            {
                return;
            }

            std::vector<int32_t> componentIds;
            int32_t numComponents =
                GetConnectedComponents(triangles, componentIds);

            if (numComponents <= 0)
            {
                return;
            }

            std::vector<Real> componentArea(numComponents,
                static_cast<Real>(0));
            std::vector<size_t> componentFacets(numComponents, 0);

            for (size_t i = 0; i < triangles.size(); ++i)
            {
                int32_t c = componentIds[i];
                componentArea[c] +=
                    ComputeTriangleArea(vertices, triangles[i]);
                ++componentFacets[c];
            }

            std::vector<bool> removeComponent(numComponents, false);
            for (int32_t c = 0; c < numComponents; ++c)
            {
                bool tooSmall =
                    (minComponentArea > static_cast<Real>(0) &&
                     componentArea[c] < minComponentArea) ||
                    (minComponentFacets > 0 &&
                     componentFacets[c] < minComponentFacets);
                removeComponent[c] = tooSmall;
            }

            std::vector<std::array<int32_t, 3>> newTriangles;
            newTriangles.reserve(triangles.size());
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                if (!removeComponent[componentIds[i]])
                {
                    newTriangles.push_back(triangles[i]);
                }
            }

            triangles = std::move(newTriangles);
        }

        // Orient normals consistently within each connected component.
        // Each component is flipped so that its signed volume is positive
        // (outward-facing normals for closed surfaces).
        static void OrientNormals(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (triangles.empty())
            {
                return;
            }

            // Propagate orientation within each component via BFS.
            size_t numTriangles = triangles.size();
            std::vector<bool> visited(numTriangles, false);

            // Build undirected adjacency: edge → triangles.
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
                edgeToTriangles;
            for (size_t i = 0; i < numTriangles; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = triangles[i][j];
                    int32_t v1 = triangles[i][(j + 1) % 3];
                    auto key = std::make_pair(
                        std::min(v0, v1), std::max(v0, v1));
                    edgeToTriangles[key].push_back(i);
                }
            }

            for (size_t seed = 0; seed < numTriangles; ++seed)
            {
                if (visited[seed])
                {
                    continue;
                }

                // BFS: propagate consistent orientation through the component.
                std::queue<size_t> bfsQueue;
                bfsQueue.push(seed);
                visited[seed] = true;

                while (!bfsQueue.empty())
                {
                    size_t cur = bfsQueue.front();
                    bfsQueue.pop();

                    for (int j = 0; j < 3; ++j)
                    {
                        int32_t v0 = triangles[cur][j];
                        int32_t v1 = triangles[cur][(j + 1) % 3];
                        auto key = std::make_pair(
                            std::min(v0, v1), std::max(v0, v1));

                        auto it = edgeToTriangles.find(key);
                        if (it == edgeToTriangles.end())
                        {
                            continue;
                        }

                        for (size_t nb : it->second)
                        {
                            if (nb == cur || visited[nb])
                            {
                                continue;
                            }

                            // Determine if nb shares the edge in the same or
                            // opposite direction as cur.  Same direction means
                            // both triangles have the same orientation at this
                            // edge, which violates manifold orientation; flip nb.
                            bool sameDir = false;
                            for (int k = 0; k < 3; ++k)
                            {
                                if (triangles[nb][k] == v0 &&
                                    triangles[nb][(k + 1) % 3] == v1)
                                {
                                    sameDir = true;
                                    break;
                                }
                            }
                            if (sameDir)
                            {
                                std::swap(triangles[nb][1], triangles[nb][2]);
                            }

                            visited[nb] = true;
                            bfsQueue.push(nb);
                        }
                    }
                }

                // Determine the signed volume of this component and flip all of
                // its triangles if the volume is negative (inverted normals).
                std::vector<int32_t> componentIds;
                int32_t numComponents =
                    GetConnectedComponents(triangles, componentIds);

                for (int32_t c = 0; c < numComponents; ++c)
                {
                    Real sv = static_cast<Real>(0);
                    for (size_t i = 0; i < numTriangles; ++i)
                    {
                        if (componentIds[i] == c)
                        {
                            sv += ComputeSignedVolume(vertices, triangles[i]);
                        }
                    }
                    if (sv < static_cast<Real>(0))
                    {
                        for (size_t i = 0; i < numTriangles; ++i)
                        {
                            if (componentIds[i] == c)
                            {
                                std::swap(triangles[i][1], triangles[i][2]);
                            }
                        }
                    }
                }

                // Only the first seed component is processed per call to this
                // outer loop; break to avoid processing sub-components twice.
                break;
            }
        }

        // Flip all triangle normals (reverse winding of every triangle).
        static void InvertNormals(
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            for (auto& tri : triangles)
            {
                std::swap(tri[1], tri[2]);
            }
        }

    private:
        static Real ComputeTriangleArea(
            std::vector<Vector3<Real>> const& vertices,
            std::array<int32_t, 3> const& tri)
        {
            Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
            Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
            return Length(Cross(e1, e2)) * static_cast<Real>(0.5);
        }

        // Signed volume of the tetrahedron from the origin to the triangle,
        // used for orientation detection (Geogram's signed_volume formula).
        static Real ComputeSignedVolume(
            std::vector<Vector3<Real>> const& vertices,
            std::array<int32_t, 3> const& tri)
        {
            Vector3<Real> const& p0 = vertices[tri[0]];
            Vector3<Real> const& p1 = vertices[tri[1]];
            Vector3<Real> const& p2 = vertices[tri[2]];
            return Dot(p0, Cross(p1, p2)) / static_cast<Real>(6);
        }

        // Label each triangle with a connected-component id.
        // Returns the number of components.
        static int32_t GetConnectedComponents(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<int32_t>& componentIds)
        {
            size_t n = triangles.size();
            componentIds.assign(n, -1);

            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
                edgeToTriangles;
            for (size_t i = 0; i < n; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = triangles[i][j];
                    int32_t v1 = triangles[i][(j + 1) % 3];
                    auto key = std::make_pair(
                        std::min(v0, v1), std::max(v0, v1));
                    edgeToTriangles[key].push_back(i);
                }
            }

            int32_t currentComponent = 0;
            for (size_t seed = 0; seed < n; ++seed)
            {
                if (componentIds[seed] >= 0)
                {
                    continue;
                }

                std::queue<size_t> q;
                q.push(seed);
                componentIds[seed] = currentComponent;

                while (!q.empty())
                {
                    size_t cur = q.front();
                    q.pop();

                    for (int j = 0; j < 3; ++j)
                    {
                        int32_t v0 = triangles[cur][j];
                        int32_t v1 = triangles[cur][(j + 1) % 3];
                        auto key = std::make_pair(
                            std::min(v0, v1), std::max(v0, v1));

                        auto it = edgeToTriangles.find(key);
                        if (it == edgeToTriangles.end())
                        {
                            continue;
                        }
                        for (size_t nb : it->second)
                        {
                            if (componentIds[nb] < 0)
                            {
                                componentIds[nb] = currentComponent;
                                q.push(nb);
                            }
                        }
                    }
                }

                ++currentComponent;
            }

            return currentComponent;
        }
    };
}
