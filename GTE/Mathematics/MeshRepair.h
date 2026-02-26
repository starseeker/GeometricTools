// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Ported from Geogram: https://github.com/BrunoLevy/geogram
// Original files: src/lib/geogram/mesh/mesh_repair.cpp, mesh_repair.h
// Original license: BSD 3-Clause
// Copyright (c) 2000-2022 Inria
//
// Adaptations for Geometric Tools Engine:
// - Changed from GEO::Mesh to std::vector<Vector3<Real>> and
//   std::vector<std::array<int32_t, 3>>
// - Removed Geogram-specific Logger and CmdLine calls
// - Added GTE-style template parameter handling

#pragma once

#include <Mathematics/MeshPreprocessing.h>
#include <Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

// The MeshRepair class provides mesh topology repair operations ported from
// Geogram. It operates on triangle meshes represented as vertex and triangle
// arrays, repairing common defects such as:
// - Colocated (duplicate) vertices
// - Degenerate triangles (triangles with repeated vertices)
// - Duplicate triangles
// - Isolated vertices (not referenced by any triangle)
//
// The implementation is header-only, following GTE conventions.

namespace gte
{
    template <typename Real>
    class MeshRepair
    {
    public:
        // Repair mode flags. These can be combined using bitwise OR (|).
        // Use the provided operator| to combine values.
        enum class RepairMode : uint32_t
        {
            TOPOLOGY    = 0,    // Dissociate non-manifold vertices (always done)
            COLOCATE    = 1,    // Merge vertices at same location
            DUP_F       = 2,    // Remove duplicate facets
            TRIANGULATE = 4,    // Ensure output is triangulated
            RECONSTRUCT = 8,    // Post-process (remove small components, fill holes)
            QUIET       = 16,   // Suppress messages (unused in GTE port)
            // Default: colocate vertices + remove duplicates + triangulate.
            // The value 7 equals COLOCATE(1) | DUP_F(2) | TRIANGULATE(4).
            DEFAULT     = 7
        };

        // Parameters for mesh repair operations.
        struct Parameters
        {
            Real epsilon;       // Tolerance for vertex merging (0 = exact)
            RepairMode mode;    // Combination of repair mode flags
            bool verbose;       // Enable verbose output (unused in header-only impl)

            Parameters()
                : epsilon(static_cast<Real>(0))
                , mode(RepairMode::DEFAULT)
                , verbose(false)
            {
            }
        };

        // Main repair function. Modifies the input mesh in-place.
        // vertices:  vertex positions
        // triangles: triangle indices (3 vertex indices per triangle)
        // params:    repair parameters
        static void Repair(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params)
        {
            uint32_t mode = static_cast<uint32_t>(params.mode);

            // Step 1: Colocate vertices if requested
            if (mode & static_cast<uint32_t>(RepairMode::COLOCATE))
            {
                ColocateVertices(vertices, triangles, params.epsilon);
            }

            // Step 2: Remove bad facets (degenerate and duplicates)
            bool checkDuplicates = (mode & static_cast<uint32_t>(RepairMode::DUP_F)) != 0;
            RemoveBadFacets(vertices, triangles, checkDuplicates);

            // Step 3: Remove isolated vertices
            RemoveIsolatedVertices(vertices, triangles);

            // Step 4: Orient normals consistently within each connected component
            // so that the signed volume of each component is positive (outward
            // normals).  Geogram's mesh_repair() always calls orient_normals()
            // for 3-D meshes; we replicate that here.
            MeshPreprocessing<Real>::OrientNormals(vertices, triangles);

            // Step 5: Split non-manifold vertices (matching Geogram's
            // repair_split_non_manifold_vertices).  Vertices where the incident
            // triangles do not form a single connected fan around the vertex are
            // duplicated so that each fan uses its own vertex copy.
            SplitNonManifoldVertices(vertices, triangles);
        }

        // Detect colocated vertices and return mapping to canonical vertex indices.
        // For each vertex i, colocated[i] contains either i (if unique) or
        // the index of the canonical vertex that i should be merged with.
        static void DetectColocatedVertices(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<int32_t>& colocated,
            Real epsilon)
        {
            int32_t numVertices = static_cast<int32_t>(vertices.size());
            colocated.resize(numVertices);

            // Initially, each vertex is its own canonical vertex.
            for (int32_t i = 0; i < numVertices; ++i)
            {
                colocated[i] = i;
            }

            if (epsilon <= static_cast<Real>(0))
            {
                // Exact comparison: use a sorted map keyed by vertex position.
                std::map<Vector3<Real>, int32_t> uniqueVertices;
                for (int32_t i = 0; i < numVertices; ++i)
                {
                    auto iter = uniqueVertices.find(vertices[i]);
                    if (iter != uniqueVertices.end())
                    {
                        colocated[i] = iter->second;
                    }
                    else
                    {
                        uniqueVertices[vertices[i]] = i;
                    }
                }
            }
            else
            {
                // Epsilon-based comparison using spatial hashing.
                // Each vertex is placed in a grid cell of side epsilon. To handle
                // vertices near cell boundaries we check all 27 neighboring cells.
                struct ArrayHash
                {
                    size_t operator()(std::array<int64_t, 3> const& arr) const
                    {
                        size_t h = 0;
                        for (size_t k = 0; k < 3; ++k)
                        {
                            h ^= std::hash<int64_t>{}(arr[k])
                                + static_cast<size_t>(0x9e3779b9)
                                + (h << 6) + (h >> 2);
                        }
                        return h;
                    }
                };

                Real invEpsilon = static_cast<Real>(1) / epsilon;
                // The hash map stores, for each cell, the list of canonical vertex
                // indices (those vertices not yet merged into an earlier one).
                std::unordered_map<std::array<int64_t, 3>,
                    std::vector<int32_t>, ArrayHash> spatialHash;

                for (int32_t i = 0; i < numVertices; ++i)
                {
                    std::array<int64_t, 3> bucket =
                    {
                        static_cast<int64_t>(std::floor(vertices[i][0] * invEpsilon)),
                        static_cast<int64_t>(std::floor(vertices[i][1] * invEpsilon)),
                        static_cast<int64_t>(std::floor(vertices[i][2] * invEpsilon))
                    };

                    bool found = false;

                    // Check the 27 neighboring cells to handle vertices that lie
                    // near a cell boundary and might match a vertex in an
                    // adjacent cell.
                    for (int di = -1; di <= 1 && !found; ++di)
                    for (int dj = -1; dj <= 1 && !found; ++dj)
                    for (int dk = -1; dk <= 1 && !found; ++dk)
                    {
                        std::array<int64_t, 3> neighborBucket =
                        {
                            bucket[0] + di,
                            bucket[1] + dj,
                            bucket[2] + dk
                        };

                        auto it = spatialHash.find(neighborBucket);
                        if (it != spatialHash.end())
                        {
                            for (int32_t j : it->second)
                            {
                                Real distSqr = static_cast<Real>(0);
                                for (int k = 0; k < 3; ++k)
                                {
                                    Real diff = vertices[i][k] - vertices[j][k];
                                    distSqr += diff * diff;
                                }
                                if (distSqr <= epsilon * epsilon)
                                {
                                    colocated[i] = j;
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!found)
                    {
                        // No nearby canonical vertex found; i is its own canonical.
                        spatialHash[bucket].push_back(i);
                    }
                }
            }
        }

        // Detect isolated vertices (not referenced by any triangle).
        static void DetectIsolatedVertices(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<bool>& isolated)
        {
            int32_t numVertices = static_cast<int32_t>(vertices.size());
            isolated.assign(numVertices, true);

            for (auto const& tri : triangles)
            {
                isolated[tri[0]] = false;
                isolated[tri[1]] = false;
                isolated[tri[2]] = false;
            }
        }

        // Detect degenerate facets (triangles with repeated vertex indices).
        static void DetectDegenerateFacets(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<bool>& degenerate)
        {
            degenerate.resize(triangles.size());
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                degenerate[i] =
                    (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]);
            }
        }

    private:
        // Merge colocated vertices and update triangle indices.
        static void ColocateVertices(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Real epsilon)
        {
            std::vector<int32_t> colocated;
            DetectColocatedVertices(vertices, colocated, epsilon);

            // Remap triangle indices to canonical vertices.
            for (auto& tri : triangles)
            {
                tri[0] = colocated[tri[0]];
                tri[1] = colocated[tri[1]];
                tri[2] = colocated[tri[2]];
            }

            // Compact vertex array: assign new indices only to canonical vertices.
            int32_t numVertices = static_cast<int32_t>(vertices.size());
            std::vector<int32_t> oldToNew(numVertices, -1);
            std::vector<Vector3<Real>> newVertices;
            newVertices.reserve(numVertices);

            for (int32_t i = 0; i < numVertices; ++i)
            {
                if (colocated[i] == i)
                {
                    oldToNew[i] = static_cast<int32_t>(newVertices.size());
                    newVertices.push_back(vertices[i]);
                }
            }

            // Map non-canonical vertices to their canonical's new index.
            for (int32_t i = 0; i < numVertices; ++i)
            {
                if (colocated[i] != i)
                {
                    oldToNew[i] = oldToNew[colocated[i]];
                }
            }

            // Update triangle indices to use compact indices.
            for (auto& tri : triangles)
            {
                tri[0] = oldToNew[tri[0]];
                tri[1] = oldToNew[tri[1]];
                tri[2] = oldToNew[tri[2]];
            }

            vertices = std::move(newVertices);
        }

        // Remove degenerate and optionally duplicate facets.
        static void RemoveBadFacets(
            std::vector<Vector3<Real>>& /* vertices */,
            std::vector<std::array<int32_t, 3>>& triangles,
            bool checkDuplicates)
        {
            std::vector<bool> degenerate;
            DetectDegenerateFacets(triangles, degenerate);

            std::set<std::array<int32_t, 3>> uniqueFacets;
            std::vector<std::array<int32_t, 3>> newTriangles;
            newTriangles.reserve(triangles.size());

            for (size_t i = 0; i < triangles.size(); ++i)
            {
                if (degenerate[i])
                {
                    continue;
                }

                if (checkDuplicates)
                {
                    // Normalize to canonical (sorted) form for duplicate detection.
                    std::array<int32_t, 3> normalized = triangles[i];
                    std::sort(normalized.begin(), normalized.end());

                    if (uniqueFacets.find(normalized) != uniqueFacets.end())
                    {
                        continue;
                    }
                    uniqueFacets.insert(normalized);
                }

                newTriangles.push_back(triangles[i]);
            }

            triangles = std::move(newTriangles);
        }

        // Remove vertices not referenced by any triangle.
        static void RemoveIsolatedVertices(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            std::vector<bool> isolated;
            DetectIsolatedVertices(vertices, triangles, isolated);

            int32_t numVertices = static_cast<int32_t>(vertices.size());
            std::vector<int32_t> oldToNew(numVertices, -1);
            std::vector<Vector3<Real>> newVertices;
            newVertices.reserve(numVertices);

            for (int32_t i = 0; i < numVertices; ++i)
            {
                if (!isolated[i])
                {
                    oldToNew[i] = static_cast<int32_t>(newVertices.size());
                    newVertices.push_back(vertices[i]);
                }
            }

            for (auto& tri : triangles)
            {
                tri[0] = oldToNew[tri[0]];
                tri[1] = oldToNew[tri[1]];
                tri[2] = oldToNew[tri[2]];
            }

            vertices = std::move(newVertices);
        }

        // Split non-manifold vertices.
        //
        // A vertex is non-manifold if its incident triangles do not form a
        // single connected fan around it.  This occurs when two separate
        // "cones" of triangles share only the vertex (e.g., a bowtie).
        //
        // For each such vertex the incident triangles are partitioned into
        // connected fans using BFS over edges that contain the vertex.  Fan 0
        // keeps the original vertex; every additional fan receives a new
        // duplicate vertex at the same position.
        //
        // This matches Geogram's repair_split_non_manifold_vertices().
        static void SplitNonManifoldVertices(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            int32_t numVerts = static_cast<int32_t>(vertices.size());
            int32_t numTris  = static_cast<int32_t>(triangles.size());

            // Build vertex → incident-triangle index lists.
            std::vector<std::vector<int32_t>> vertToTris(numVerts);
            for (int32_t t = 0; t < numTris; ++t)
            {
                for (int k = 0; k < 3; ++k)
                {
                    vertToTris[triangles[t][k]].push_back(t);
                }
            }

            for (int32_t v = 0; v < numVerts; ++v)
            {
                auto& inc = vertToTris[v];
                int32_t numInc = static_cast<int32_t>(inc.size());
                if (numInc <= 1)
                {
                    continue;
                }

                // Map each "other vertex u adjacent to v" to the local
                // triangle indices (within inc[]) that contain edge v–u.
                std::map<int32_t, std::vector<int32_t>> neighborToLocal;
                for (int32_t li = 0; li < numInc; ++li)
                {
                    int32_t t = inc[li];
                    for (int k = 0; k < 3; ++k)
                    {
                        int32_t u = triangles[t][k];
                        if (u != v)
                        {
                            neighborToLocal[u].push_back(li);
                        }
                    }
                }

                // BFS: group incident triangles into connected fans.
                // Two triangles in inc[] are adjacent if they share an
                // edge containing v (i.e. they both contain some u ≠ v).
                std::vector<int32_t> comp(numInc, -1);
                int32_t numComp = 0;

                for (int32_t seed = 0; seed < numInc; ++seed)
                {
                    if (comp[seed] >= 0)
                    {
                        continue;
                    }

                    std::queue<int32_t> q;
                    q.push(seed);
                    comp[seed] = numComp;

                    while (!q.empty())
                    {
                        int32_t cur = q.front();
                        q.pop();

                        int32_t curTri = inc[cur];
                        for (int k = 0; k < 3; ++k)
                        {
                            int32_t u = triangles[curTri][k];
                            if (u == v)
                            {
                                continue;
                            }

                            auto it = neighborToLocal.find(u);
                            if (it == neighborToLocal.end())
                            {
                                continue;
                            }

                            for (int32_t adj : it->second)
                            {
                                if (comp[adj] < 0)
                                {
                                    comp[adj] = numComp;
                                    q.push(adj);
                                }
                            }
                        }
                    }

                    ++numComp;
                }

                if (numComp <= 1)
                {
                    continue;
                }

                // Fan 0 keeps vertex v.  Fans 1, 2, ... get new duplicate
                // vertices, and the triangles in those fans are updated.
                for (int32_t c = 1; c < numComp; ++c)
                {
                    int32_t newV = static_cast<int32_t>(vertices.size());
                    vertices.push_back(vertices[v]);

                    for (int32_t li = 0; li < numInc; ++li)
                    {
                        if (comp[li] != c)
                        {
                            continue;
                        }

                        int32_t t = inc[li];
                        for (int k = 0; k < 3; ++k)
                        {
                            if (triangles[t][k] == v)
                            {
                                triangles[t][k] = newV;
                            }
                        }
                    }
                }
            }
        }
    };

    // Allow combining RepairMode flags with bitwise OR.
    template <typename Real>
    inline typename MeshRepair<Real>::RepairMode operator|(
        typename MeshRepair<Real>::RepairMode a,
        typename MeshRepair<Real>::RepairMode b)
    {
        return static_cast<typename MeshRepair<Real>::RepairMode>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
}
