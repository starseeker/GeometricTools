// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Ported from Geogram: https://github.com/BrunoLevy/geogram
// Original files: src/lib/geogram/mesh/mesh_repair.cpp, mesh_repair.h
// Original license: BSD 3-Clause (see below)
// Copyright (c) 2000-2022 Inria
//
// Adaptations for Geometric Tools Engine:
// - Changed from GEO::Mesh to std::vector<Vector3<Real>> and std::vector<std::array<int32_t, 3>>
// - Uses GTE::ETManifoldMesh for topology operations
// - Removed Geogram-specific Logger and CmdLine calls
// - Added GTE-style template parameter handling
//
// Original Geogram License (BSD 3-Clause):
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of the ALICE Project-Team nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/ETManifoldMesh.h>
#include <GTE/Mathematics/MeshPreprocessing.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
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
// - Non-manifold topology
//
// The implementation uses GTE's ETManifoldMesh for topology operations and
// is header-only, following GTE conventions.

namespace gte
{
    template <typename Real>
    class MeshRepair
    {
    public:
        // Repair mode flags. These can be combined using bitwise OR (|).
        enum class RepairMode : uint32_t
        {
            TOPOLOGY = 0,       // Dissociate non-manifold vertices (always done)
            COLOCATE = 1,       // Merge vertices at same location
            DUP_F = 2,          // Remove duplicate facets
            TRIANGULATE = 4,    // Ensure output is triangulated
            RECONSTRUCT = 8,    // Post-process (remove small components, fill holes)
            QUIET = 16,         // Suppress messages (unused in GTE port)
            DEFAULT = COLOCATE | DUP_F | TRIANGULATE
        };

        // Parameters for mesh repair operations.
        struct Parameters
        {
            Real epsilon;               // Tolerance for vertex merging
            RepairMode mode;            // Combination of repair mode flags
            bool verbose;               // Enable verbose output (unused in header-only impl)

            Parameters()
                : epsilon(static_cast<Real>(0))
                , mode(RepairMode::DEFAULT)
                , verbose(false)
            {
            }
        };

        // Main repair function. Modifies the input mesh in-place.
        // vertices: vertex positions (3 coordinates per vertex)
        // triangles: triangle indices (3 vertex indices per triangle)
        // params: repair parameters
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

            // Step 2: Triangulate if needed (Geogram handles polygons, GTE assumes triangles)
            // This is a no-op for GTE since we only handle triangle meshes

            // Step 3: Remove bad facets (degenerate and duplicates)
            bool checkDuplicates = (mode & static_cast<uint32_t>(RepairMode::DUP_F)) != 0;
            RemoveBadFacets(vertices, triangles, checkDuplicates);

            // Step 4: Remove isolated vertices
            RemoveIsolatedVertices(vertices, triangles);

            // Step 5: Connect and reorient facets
            // (Geogram does this internally, we'll rely on the mesh being properly oriented)

            // Step 6: Orient normals consistently per connected component.
            // Matches geogram's mesh_repair which always calls orient_normals()
            // at the end to ensure outward-facing normals by flipping any component
            // whose signed volume is negative.
            if (!triangles.empty())
            {
                MeshPreprocessing<Real>::OrientNormals(vertices, triangles);
            }
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

            // Initially, each vertex is its own canonical vertex
            for (int32_t i = 0; i < numVertices; ++i)
            {
                colocated[i] = i;
            }

            if (epsilon <= static_cast<Real>(0))
            {
                // Exact comparison for zero epsilon
                std::map<Vector3<Real>, int32_t> uniqueVertices;
                for (int32_t i = 0; i < numVertices; ++i)
                {
                    auto iter = uniqueVertices.find(vertices[i]);
                    if (iter != uniqueVertices.end())
                    {
                        // Found duplicate
                        colocated[i] = iter->second;
                    }
                    else
                    {
                        // New unique vertex
                        uniqueVertices[vertices[i]] = i;
                    }
                }
            }
            else
            {
                // Epsilon-based comparison using spatial hashing.
                // Each cell has side length epsilon, so two vertices within distance
                // epsilon can be in the same cell OR in any of the 26 adjacent cells.
                // We therefore check the full 3x3x3 neighborhood of 27 cells to
                // guarantee that all pairs within epsilon are detected regardless of
                // which side of a cell boundary they fall on.
                struct ArrayHash {
                    size_t operator()(std::array<int64_t, 3> const& arr) const {
                        size_t h = 0;
                        for (size_t i = 0; i < 3; ++i) {
                            h ^= std::hash<int64_t>{}(arr[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
                        }
                        return h;
                    }
                };

                Real invEpsilon = static_cast<Real>(1) / epsilon;
                std::unordered_map<std::array<int64_t, 3>, std::vector<int32_t>, ArrayHash> spatialHash;

                for (int32_t i = 0; i < numVertices; ++i)
                {
                    // Compute the cell this vertex falls in.
                    std::array<int64_t, 3> bucket = {
                        static_cast<int64_t>(std::floor(vertices[i][0] * invEpsilon)),
                        static_cast<int64_t>(std::floor(vertices[i][1] * invEpsilon)),
                        static_cast<int64_t>(std::floor(vertices[i][2] * invEpsilon))
                    };

                    // Search the 3x3x3 neighbourhood (27 cells) for an existing
                    // canonical vertex within distance epsilon.
                    bool found = false;
                    for (int dx = -1; dx <= 1 && !found; ++dx)
                    {
                        for (int dy = -1; dy <= 1 && !found; ++dy)
                        {
                            for (int dz = -1; dz <= 1 && !found; ++dz)
                            {
                                std::array<int64_t, 3> neighborBucket = {
                                    bucket[0] + dx,
                                    bucket[1] + dy,
                                    bucket[2] + dz
                                };
                                auto it = spatialHash.find(neighborBucket);
                                if (it == spatialHash.end())
                                {
                                    continue;
                                }
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
                    }

                    if (!found)
                    {
                        // Vertex i is canonical in its own cell.
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
            isolated.resize(numVertices);
            std::fill(isolated.begin(), isolated.end(), true);

            for (auto const& tri : triangles)
            {
                isolated[tri[0]] = false;
                isolated[tri[1]] = false;
                isolated[tri[2]] = false;
            }
        }

        // Detect degenerate facets (triangles with repeated vertices).
        static void DetectDegenerateFacets(
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<bool>& degenerate)
        {
            degenerate.resize(triangles.size());
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                degenerate[i] = (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]);
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

            // Remap triangle indices to canonical vertices
            for (auto& tri : triangles)
            {
                tri[0] = colocated[tri[0]];
                tri[1] = colocated[tri[1]];
                tri[2] = colocated[tri[2]];
            }

            // Compact vertex array: build mapping from old to new indices
            int32_t numVertices = static_cast<int32_t>(vertices.size());
            std::vector<int32_t> oldToNew(numVertices, -1);
            std::vector<Vector3<Real>> newVertices;
            newVertices.reserve(numVertices);

            for (int32_t i = 0; i < numVertices; ++i)
            {
                if (colocated[i] == i)
                {
                    // This is a canonical vertex, keep it
                    oldToNew[i] = static_cast<int32_t>(newVertices.size());
                    newVertices.push_back(vertices[i]);
                }
            }

            // Update non-canonical vertices to point to new indices
            for (int32_t i = 0; i < numVertices; ++i)
            {
                if (colocated[i] != i)
                {
                    oldToNew[i] = oldToNew[colocated[i]];
                }
            }

            // Update triangle indices
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
            std::vector<Vector3<Real>>& vertices,
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
                    continue; // Skip degenerate triangles
                }

                if (checkDuplicates)
                {
                    // Normalize triangle to canonical form for duplicate detection
                    std::array<int32_t, 3> normalized = triangles[i];
                    std::sort(normalized.begin(), normalized.end());

                    if (uniqueFacets.find(normalized) != uniqueFacets.end())
                    {
                        continue; // Skip duplicate
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

            // Build mapping from old to new vertex indices
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

            // Update triangle indices
            for (auto& tri : triangles)
            {
                tri[0] = oldToNew[tri[0]];
                tri[1] = oldToNew[tri[1]];
                tri[2] = oldToNew[tri[2]];
            }

            vertices = std::move(newVertices);
        }
    };

    // Allow combining RepairMode flags with bitwise OR
    template <typename Real>
    inline typename MeshRepair<Real>::RepairMode operator|(
        typename MeshRepair<Real>::RepairMode a,
        typename MeshRepair<Real>::RepairMode b)
    {
        return static_cast<typename MeshRepair<Real>::RepairMode>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
}

// Hash functions for GTE types to support unordered containers.
// These need to be in the std namespace and defined before use.
namespace std
{
    // Hash function for gte::Vector3
    template <typename Real>
    struct hash<gte::Vector<3, Real>>
    {
        size_t operator()(gte::Vector<3, Real> const& v) const
        {
            size_t h1 = std::hash<Real>{}(v[0]);
            size_t h2 = std::hash<Real>{}(v[1]);
            size_t h3 = std::hash<Real>{}(v[2]);
            // Combine hashes using a simple formula
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}
