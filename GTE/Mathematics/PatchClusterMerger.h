// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.20
//
// PatchClusterMerger: Reduce Co3Ne patch count via spatial clustering +
// UV re-triangulation.
//
// Algorithm (per the alternative-idea proposal):
//  1. Identify connected patches in the Co3Ne output mesh.
//  2. Assign each patch uniquely to one of N clusters using k-means on
//     patch centroids.
//  3. For each cluster:
//     a. Project all cluster vertices to 2D UV space using PCA (best-fit
//        plane of cluster vertices).
//     b. Find boundary edges of the merged cluster (edges used by exactly
//        one triangle within the cluster).
//     c. Trace one or more boundary loops from those edges.
//     d. Compute an outer boundary polygon:
//        - Single loop  → use it directly.
//        - Multiple loops → compute the convex hull of all boundary
//          vertices, then shrink-wrap inward to find the concave hull
//          that tightly encloses all loops.
//     e. Triangulate in UV space using detria (Delaunay with constraints):
//        outer boundary as the outline; all remaining cluster vertices
//        as unconstrained Steiner points.
//     f. Map the UV triangles back to original 3D vertex indices and
//        replace the cluster's original triangles with the new ones.
//
// The result is a mesh with at most N patches, ready for the existing
// StitchPatches / component-bridging pipeline.

#ifndef GTE_MATHEMATICS_PATCH_CLUSTER_MERGER_H
#define GTE_MATHEMATICS_PATCH_CLUSTER_MERGER_H

#pragma once

#include <GTE/Mathematics/Vector2.h>
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/ConvexHull2.h>
#include <GTE/Mathematics/SymmetricEigensolver3x3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>
#include <vector>

// detria.hpp lives at the repository root; the Makefile adds "-I." so this
// relative-from-root path works from any translation unit.
#include <detria.hpp>

namespace gte
{
    template <typename Real>
    class PatchClusterMerger
    {
    public:
        // ---------------------------------------------------------------
        // Parameters
        // ---------------------------------------------------------------
        struct Parameters
        {
            // Desired number of merged clusters produced.  Each cluster
            // becomes one (larger) patch replacing many small Co3Ne patches.
            int32_t numClusters;

            // Maximum iterations for the k-means clustering step.
            int32_t kmeansMaxIterations;

            // Minimum number of patches a cluster must contain before
            // re-triangulation is attempted.  Clusters with fewer patches
            // are left unchanged.
            int32_t minPatchesPerCluster;

            // Fraction of the average cluster boundary-edge length used as
            // the minimum indentation threshold for the shrinkwrap step.
            Real shrinkwrapEpsilonFactor;

            bool verbose;

            Parameters()
                : numClusters(6)
                , kmeansMaxIterations(50)
                , minPatchesPerCluster(2)
                , shrinkwrapEpsilonFactor(static_cast<Real>(0.05))
                , verbose(false)
            {}
        };

        // ---------------------------------------------------------------
        // Main entry point
        // ---------------------------------------------------------------
        // Modifies 'vertices' and 'triangles' in-place: for each cluster
        // the original triangles are removed and replaced by the re-
        // triangulated Delaunay patch.  Returns the number of clusters
        // that were successfully merged.
        static int32_t MergeClusteredPatches(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters())
        {
            if (triangles.empty() || vertices.empty())
            {
                return 0;
            }

            // Step 1: identify connected patches.
            std::vector<PatchInfo> patches;
            FindPatches(vertices, triangles, patches);

            if (params.verbose)
            {
                std::cout << "[PatchClusterMerger] " << patches.size()
                          << " patches identified\n";
            }

            if (static_cast<int32_t>(patches.size()) <= params.numClusters)
            {
                // Already few enough patches – nothing to do.
                if (params.verbose)
                {
                    std::cout << "[PatchClusterMerger] patch count <= numClusters"
                                 ", skipping merge\n";
                }
                return 0;
            }

            // Step 2: k-means cluster the patches.
            std::vector<int32_t> clusterOf =
                KMeansClusterPatches(patches, params.numClusters,
                    params.kmeansMaxIterations);

            // Step 3: for each cluster, re-triangulate.
            int32_t mergedCount = 0;
            for (int32_t c = 0; c < params.numClusters; ++c)
            {
                // Collect patches in this cluster.
                std::vector<int32_t> clusterPatches;
                for (int32_t p = 0; p < static_cast<int32_t>(patches.size()); ++p)
                {
                    if (clusterOf[p] == c)
                    {
                        clusterPatches.push_back(p);
                    }
                }

                if (static_cast<int32_t>(clusterPatches.size()) < params.minPatchesPerCluster)
                {
                    continue;
                }

                bool ok = MergeCluster(vertices, triangles,
                    clusterPatches, patches, params);
                if (ok)
                {
                    ++mergedCount;
                }
                else if (params.verbose)
                {
                    std::cout << "[PatchClusterMerger] cluster " << c
                              << " merge failed, keeping original triangles\n";
                }
            }

            if (params.verbose)
            {
                std::cout << "[PatchClusterMerger] merged " << mergedCount
                          << " / " << params.numClusters << " clusters\n";
                std::cout << "[PatchClusterMerger] final mesh: "
                          << triangles.size() << " triangles\n";
            }

            return mergedCount;
        }

    private:
        // ---------------------------------------------------------------
        // Internal data structures
        // ---------------------------------------------------------------
        struct PatchInfo
        {
            std::vector<int32_t> triIndices;     // Triangle indices (into global array)
            std::set<int32_t>    vertexIndices;  // Vertex indices (into global array)
            Vector3<Real>        centroid;

            PatchInfo()
                : centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)}
            {}
        };

        // ---------------------------------------------------------------
        // Step 1: flood-fill connected components
        // ---------------------------------------------------------------
        static void FindPatches(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<PatchInfo>& patches)
        {
            patches.clear();

            // Build edge→triangles adjacency (unordered for O(1)).
            using EdgeKey = std::pair<int32_t, int32_t>;
            auto makeEdge = [](int32_t a, int32_t b) -> EdgeKey
            {
                return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
            };

            // Build edge→triangles adjacency.
            // Re-use a map keyed by EdgeKey.
            std::map<EdgeKey, std::vector<int32_t>> edgeMap;
            for (int32_t t = 0; t < static_cast<int32_t>(triangles.size()); ++t)
            {
                auto const& tri = triangles[t];
                for (int32_t k = 0; k < 3; ++k)
                {
                    edgeMap[makeEdge(tri[k], tri[(k + 1) % 3])].push_back(t);
                }
            }

            // BFS flood fill.
            std::vector<bool> visited(triangles.size(), false);
            for (int32_t seed = 0; seed < static_cast<int32_t>(triangles.size()); ++seed)
            {
                if (visited[seed])
                {
                    continue;
                }

                PatchInfo patch;
                std::vector<int32_t> queue;
                queue.push_back(seed);
                visited[seed] = true;

                while (!queue.empty())
                {
                    int32_t cur = queue.back();
                    queue.pop_back();
                    patch.triIndices.push_back(cur);

                    auto const& tri = triangles[cur];
                    for (int32_t k = 0; k < 3; ++k)
                    {
                        patch.vertexIndices.insert(tri[k]);
                        auto it = edgeMap.find(makeEdge(tri[k], tri[(k + 1) % 3]));
                        if (it != edgeMap.end())
                        {
                            for (int32_t nb : it->second)
                            {
                                if (!visited[nb])
                                {
                                    visited[nb] = true;
                                    queue.push_back(nb);
                                }
                            }
                        }
                    }
                }

                // Compute centroid.
                Vector3<Real> c{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
                for (int32_t vi : patch.vertexIndices)
                {
                    c = c + vertices[vi];
                }
                if (!patch.vertexIndices.empty())
                {
                    c = c / static_cast<Real>(patch.vertexIndices.size());
                }
                patch.centroid = c;

                patches.push_back(std::move(patch));
            }
        }

        // ---------------------------------------------------------------
        // Step 2: k-means clustering of patches by centroid
        // ---------------------------------------------------------------
        static std::vector<int32_t> KMeansClusterPatches(
            std::vector<PatchInfo> const& patches,
            int32_t numClusters,
            int32_t maxIterations)
        {
            int32_t N = static_cast<int32_t>(patches.size());
            numClusters = std::min(numClusters, N);

            // Initialise cluster centres by spreading evenly over sorted patches.
            std::vector<Vector3<Real>> centres(numClusters);
            for (int32_t k = 0; k < numClusters; ++k)
            {
                int32_t idx = (k * N) / numClusters;
                centres[k] = patches[idx].centroid;
            }

            std::vector<int32_t> assignment(N, 0);
            Real const INF = std::numeric_limits<Real>::max();

            for (int32_t iter = 0; iter < maxIterations; ++iter)
            {
                // Assignment step.
                bool changed = false;
                for (int32_t p = 0; p < N; ++p)
                {
                    Real bestDist = INF;
                    int32_t bestK = 0;
                    for (int32_t k = 0; k < numClusters; ++k)
                    {
                        Vector3<Real> d = patches[p].centroid - centres[k];
                        Real dist = Dot(d, d);
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            bestK = k;
                        }
                    }
                    if (assignment[p] != bestK)
                    {
                        assignment[p] = bestK;
                        changed = true;
                    }
                }

                if (!changed)
                {
                    break;
                }

                // Update step.
                std::vector<Vector3<Real>> sums(numClusters,
                    Vector3<Real>{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)});
                std::vector<int32_t> counts(numClusters, 0);

                for (int32_t p = 0; p < N; ++p)
                {
                    int32_t k = assignment[p];
                    sums[k] = sums[k] + patches[p].centroid;
                    ++counts[k];
                }

                for (int32_t k = 0; k < numClusters; ++k)
                {
                    if (counts[k] > 0)
                    {
                        centres[k] = sums[k] / static_cast<Real>(counts[k]);
                    }
                }
            }

            return assignment;
        }

        // ---------------------------------------------------------------
        // Step 3: merge one cluster
        // ---------------------------------------------------------------
        // Returns true if re-triangulation succeeded and triangles were
        // replaced; false on any failure (original triangles kept).
        static bool MergeCluster(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<int32_t> const& clusterPatchIndices,
            std::vector<PatchInfo> const& allPatches,
            Parameters const& params)
        {
            // Collect the set of triangle indices and vertex indices for
            // this cluster.
            std::set<int32_t> clusterTriSet;
            std::vector<int32_t> clusterVertList;
            {
                std::set<int32_t> vertSet;
                for (int32_t pi : clusterPatchIndices)
                {
                    for (int32_t ti : allPatches[pi].triIndices)
                    {
                        clusterTriSet.insert(ti);
                    }
                    for (int32_t vi : allPatches[pi].vertexIndices)
                    {
                        vertSet.insert(vi);
                    }
                }
                clusterVertList.assign(vertSet.begin(), vertSet.end());
            }

            if (clusterVertList.size() < 3)
            {
                return false;
            }

            // --- a. PCA projection to UV. ---
            Vector3<Real> origin, uAxis, vAxis;
            std::vector<Vector2<Real>> uvCoords;
            if (!ProjectToUV(vertices, clusterVertList, uvCoords, origin, uAxis, vAxis))
            {
                return false;
            }

            // Build global-index → local-index map (into clusterVertList / uvCoords).
            std::unordered_map<int32_t, int32_t> globalToLocal;
            globalToLocal.reserve(clusterVertList.size());
            for (int32_t i = 0; i < static_cast<int32_t>(clusterVertList.size()); ++i)
            {
                globalToLocal[clusterVertList[i]] = i;
            }

            // --- b. Boundary edges of the cluster. ---
            // An edge is on the cluster boundary if it appears in exactly one
            // cluster triangle.
            std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
            for (int32_t ti : clusterTriSet)
            {
                auto const& tri = triangles[ti];
                for (int32_t k = 0; k < 3; ++k)
                {
                    int32_t a = tri[k], b = tri[(k + 1) % 3];
                    if (a > b) std::swap(a, b);
                    ++edgeCount[{a, b}];
                }
            }

            // Adjacency list of boundary edges (directed, using local indices).
            std::map<int32_t, std::vector<int32_t>> boundaryAdj;
            std::set<int32_t> boundaryVertSet;
            for (auto const& entry : edgeCount)
            {
                if (entry.second == 1)
                {
                    int32_t a = globalToLocal.at(entry.first.first);
                    int32_t b = globalToLocal.at(entry.first.second);
                    boundaryAdj[a].push_back(b);
                    boundaryAdj[b].push_back(a);
                    boundaryVertSet.insert(a);
                    boundaryVertSet.insert(b);
                }
            }

            if (boundaryVertSet.empty())
            {
                // Cluster is a closed surface – nothing to re-parameterise.
                return false;
            }

            // --- c. Trace boundary loops. ---
            std::vector<std::vector<int32_t>> loops;
            TraceBoundaryLoops(boundaryAdj, boundaryVertSet, loops);

            if (loops.empty())
            {
                return false;
            }

            // --- d. Compute outer boundary. ---
            std::vector<int32_t> outerBoundary =
                ComputeOuterBoundary(uvCoords, loops, boundaryVertSet, params);

            if (outerBoundary.size() < 3)
            {
                return false;
            }

            // --- e. Triangulate in UV space. ---
            std::vector<std::array<int32_t, 3>> uvTriangles;
            if (!TriangulateClusterUV(uvCoords, outerBoundary,
                    clusterVertList, globalToLocal, uvTriangles, params.verbose))
            {
                return false;
            }

            if (uvTriangles.empty())
            {
                return false;
            }

            // --- f. Replace original triangles. ---
            // Remove cluster triangles (mark as degenerate {0,0,0} then
            // compact, but compact is expensive; instead build a replacement
            // list and swap).
            // We rebuild the triangles array: keep non-cluster triangles,
            // then append new cluster triangles.
            std::vector<std::array<int32_t, 3>> kept;
            kept.reserve(triangles.size());
            for (int32_t ti = 0; ti < static_cast<int32_t>(triangles.size()); ++ti)
            {
                if (clusterTriSet.find(ti) == clusterTriSet.end())
                {
                    kept.push_back(triangles[ti]);
                }
            }

            // Append new triangles (map local UV indices back to global).
            for (auto const& uvTri : uvTriangles)
            {
                int32_t v0 = clusterVertList[uvTri[0]];
                int32_t v1 = clusterVertList[uvTri[1]];
                int32_t v2 = clusterVertList[uvTri[2]];
                kept.push_back({v0, v1, v2});
            }

            triangles = std::move(kept);
            return true;
        }

        // ---------------------------------------------------------------
        // Step 3a: PCA projection to UV
        // ---------------------------------------------------------------
        static bool ProjectToUV(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<int32_t> const& vertIdxList,
            std::vector<Vector2<Real>>& uvCoords,
            Vector3<Real>& origin,
            Vector3<Real>& uAxis,
            Vector3<Real>& vAxis)
        {
            int32_t N = static_cast<int32_t>(vertIdxList.size());
            if (N < 3)
            {
                return false;
            }

            // Centroid.
            origin = Vector3<Real>{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
            for (int32_t vi : vertIdxList)
            {
                origin = origin + vertices[vi];
            }
            origin = origin / static_cast<Real>(N);

            // Covariance matrix (symmetric, upper triangle).
            Real c00(0), c01(0), c02(0), c11(0), c12(0), c22(0);
            for (int32_t vi : vertIdxList)
            {
                Vector3<Real> d = vertices[vi] - origin;
                c00 += d[0] * d[0];
                c01 += d[0] * d[1];
                c02 += d[0] * d[2];
                c11 += d[1] * d[1];
                c12 += d[1] * d[2];
                c22 += d[2] * d[2];
            }
            Real invN = static_cast<Real>(1) / static_cast<Real>(N);
            c00 *= invN; c01 *= invN; c02 *= invN;
            c11 *= invN; c12 *= invN; c22 *= invN;

            // Eigendecomposition (sortType +1 → smallest eigenvalue first;
            // the normal is the eigenvector of the smallest eigenvalue).
            std::array<Real, 3> eval{};
            std::array<std::array<Real, 3>, 3> evec{};
            SymmetricEigensolver3x3<Real> solver;
            solver(c00, c01, c02, c11, c12, c22, false, +1, eval, evec);

            // evec[0] = eigenvector for smallest eigenvalue = surface normal.
            // evec[1] and evec[2] = the two principal tangent directions.
            uAxis = Vector3<Real>{evec[1][0], evec[1][1], evec[1][2]};
            vAxis = Vector3<Real>{evec[2][0], evec[2][1], evec[2][2]};

            // Normalise (should already be unit, but be safe).
            Real uLen = Length(uAxis);
            Real vLen = Length(vAxis);
            if (uLen < static_cast<Real>(1e-10) || vLen < static_cast<Real>(1e-10))
            {
                return false;
            }
            uAxis = uAxis / uLen;
            vAxis = vAxis / vLen;

            // Project all cluster vertices.
            uvCoords.resize(vertIdxList.size());
            for (int32_t i = 0; i < N; ++i)
            {
                Vector3<Real> d = vertices[vertIdxList[i]] - origin;
                uvCoords[i][0] = Dot(d, uAxis);
                uvCoords[i][1] = Dot(d, vAxis);
            }

            return true;
        }

        // ---------------------------------------------------------------
        // Step 3c: trace boundary loops
        // ---------------------------------------------------------------
        static void TraceBoundaryLoops(
            std::map<int32_t, std::vector<int32_t>> const& adj,
            std::set<int32_t> const& boundaryVerts,
            std::vector<std::vector<int32_t>>& loops)
        {
            loops.clear();
            std::set<int32_t> visited;

            for (int32_t start : boundaryVerts)
            {
                if (visited.count(start))
                {
                    continue;
                }

                // Walk the loop.
                std::vector<int32_t> loop;
                int32_t cur = start;
                int32_t prev = -1;

                while (true)
                {
                    loop.push_back(cur);
                    visited.insert(cur);

                    // Pick the next unvisited (or the start) neighbour.
                    auto it = adj.find(cur);
                    if (it == adj.end())
                    {
                        break;
                    }

                    int32_t next = -1;
                    for (int32_t nb : it->second)
                    {
                        if (nb == prev)
                        {
                            continue;
                        }
                        if (nb == start && loop.size() >= 3)
                        {
                            next = nb;  // Closed the loop.
                            break;
                        }
                        if (!visited.count(nb))
                        {
                            next = nb;
                            break;
                        }
                    }

                    if (next == start || next == -1)
                    {
                        break;
                    }

                    prev = cur;
                    cur = next;
                }

                if (loop.size() >= 3)
                {
                    loops.push_back(std::move(loop));
                }
            }
        }

        // ---------------------------------------------------------------
        // Step 3d: outer boundary (single loop or shrinkwrap of convex hull)
        // ---------------------------------------------------------------
        static std::vector<int32_t> ComputeOuterBoundary(
            std::vector<Vector2<Real>> const& uvCoords,
            std::vector<std::vector<int32_t>> const& loops,
            std::set<int32_t> const& boundaryVertSet,
            Parameters const& params)
        {
            if (loops.size() == 1)
            {
                return loops[0];
            }

            // Multiple loops: collect all boundary vertices and build the
            // convex hull, then shrink-wrap inward to tighten it.
            std::vector<int32_t> allBoundaryVerts(
                boundaryVertSet.begin(), boundaryVertSet.end());

            std::vector<Vector2<Real>> boundaryUV;
            boundaryUV.reserve(allBoundaryVerts.size());
            for (int32_t idx : allBoundaryVerts)
            {
                boundaryUV.push_back(uvCoords[idx]);
            }

            // Convex hull.
            ConvexHull2<Real> hull;
            if (!hull(boundaryUV))
            {
                // Degenerate (all collinear); return arbitrary loop.
                return loops[0];
            }

            std::vector<int32_t> const& hullLocalIndices = hull.GetHull();
            // Map back to cluster-local vertex indices.
            std::vector<int32_t> hullIndices;
            hullIndices.reserve(hullLocalIndices.size());
            for (int32_t h : hullLocalIndices)
            {
                hullIndices.push_back(allBoundaryVerts[h]);
            }

            // Shrink-wrap the convex hull inward to capture concavities.
            return ShrinkwrapHull(uvCoords, hullIndices, boundaryVertSet, params);
        }

        // ---------------------------------------------------------------
        // Shrink-wrap: refine convex hull toward concave hull
        // ---------------------------------------------------------------
        // For each hull edge (A,B) we find the boundary vertex that lies on
        // the interior side and is furthest from the edge.  If its distance
        // exceeds epsilon we insert it into the hull, splitting the edge.
        // We repeat until no more vertices can be inserted.
        static std::vector<int32_t> ShrinkwrapHull(
            std::vector<Vector2<Real>> const& uvCoords,
            std::vector<int32_t> const& initialHull,
            std::set<int32_t> const& boundaryVertSet,
            Parameters const& params)
        {
            std::vector<int32_t> hull = initialHull;

            // Estimate epsilon from average hull edge length.
            Real totalLen = static_cast<Real>(0);
            for (size_t i = 0; i < hull.size(); ++i)
            {
                Vector2<Real> d = uvCoords[hull[(i + 1) % hull.size()]]
                                - uvCoords[hull[i]];
                totalLen += std::sqrt(Dot(d, d));
            }
            Real epsilon = (hull.empty() ? static_cast<Real>(0) :
                totalLen / static_cast<Real>(hull.size()))
                * params.shrinkwrapEpsilonFactor;

            std::set<int32_t> hullSet(hull.begin(), hull.end());

            bool changed = true;
            // Limit total insertions to avoid O(N²) blow-up in degenerate cases.
            int32_t maxInsertions = static_cast<int32_t>(boundaryVertSet.size()) * 2;

            while (changed && maxInsertions > 0)
            {
                changed = false;
                for (int32_t ei = 0; ei < static_cast<int32_t>(hull.size()); ++ei)
                {
                    int32_t iA = hull[ei];
                    int32_t iB = hull[(ei + 1) % hull.size()];
                    Vector2<Real> A = uvCoords[iA];
                    Vector2<Real> B = uvCoords[iB];
                    Vector2<Real> AB = B - A;
                    Real ABlen = std::sqrt(Dot(AB, AB));
                    if (ABlen < static_cast<Real>(1e-15))
                    {
                        continue;
                    }

                    // Inward-pointing perpendicular (to the left of A→B
                    // when the hull is counter-clockwise).
                    Vector2<Real> inward{-AB[1] / ABlen, AB[0] / ABlen};

                    // Find the centroid of the hull to determine "inward".
                    Vector2<Real> hullCentroid{static_cast<Real>(0), static_cast<Real>(0)};
                    for (int32_t h : hull)
                    {
                        hullCentroid = hullCentroid + uvCoords[h];
                    }
                    hullCentroid = hullCentroid / static_cast<Real>(hull.size());

                    // Ensure inward points toward centroid.
                    Vector2<Real> toCenter = hullCentroid - A;
                    if (Dot(toCenter, inward) < static_cast<Real>(0))
                    {
                        inward[0] = -inward[0];
                        inward[1] = -inward[1];
                    }

                    // Find best boundary vertex to insert.
                    int32_t bestV = -1;
                    Real bestDist = epsilon;
                    for (int32_t v : boundaryVertSet)
                    {
                        if (hullSet.count(v))
                        {
                            continue;
                        }
                        Vector2<Real> AV = uvCoords[v] - A;

                        // Must lie on the interior side of edge AB.
                        if (Dot(AV, inward) <= epsilon)
                        {
                            continue;
                        }

                        // Must lie between A and B along the edge direction
                        // (not beyond either endpoint).
                        Real t = Dot(AV, AB) / Dot(AB, AB);
                        if (t <= static_cast<Real>(0) || t >= static_cast<Real>(1))
                        {
                            continue;
                        }

                        Real dist = Dot(AV, inward);
                        if (dist > bestDist)
                        {
                            bestDist = dist;
                            bestV = v;
                        }
                    }

                    if (bestV >= 0)
                    {
                        hull.insert(hull.begin() + ei + 1, bestV);
                        hullSet.insert(bestV);
                        changed = true;
                        --maxInsertions;
                        break;  // Restart scan with updated hull.
                    }
                }
            }

            return hull;
        }

        // ---------------------------------------------------------------
        // Step 3e: triangulate in UV space with detria
        // ---------------------------------------------------------------
        static bool TriangulateClusterUV(
            std::vector<Vector2<Real>> const& uvCoords,
            std::vector<int32_t> const& outerBoundary,
            std::vector<int32_t> const& clusterVertList,
            std::unordered_map<int32_t, int32_t> const& globalToLocal,
            std::vector<std::array<int32_t, 3>>& outLocalTriangles,
            bool verbose)
        {
            outLocalTriangles.clear();

            if (outerBoundary.size() < 3 || uvCoords.empty())
            {
                return false;
            }

            int32_t numVerts = static_cast<int32_t>(uvCoords.size());

            // Build detria point list (all cluster vertices in UV).
            std::vector<detria::PointD> pts2D;
            pts2D.reserve(numVerts);
            for (auto const& uv : uvCoords)
            {
                pts2D.push_back({static_cast<double>(uv[0]),
                                 static_cast<double>(uv[1])});
            }

            // Build outline (uses local indices from outerBoundary).
            std::vector<uint32_t> outline;
            outline.reserve(outerBoundary.size());
            for (int32_t localIdx : outerBoundary)
            {
                if (localIdx < 0 || localIdx >= numVerts)
                {
                    return false;
                }
                outline.push_back(static_cast<uint32_t>(localIdx));
            }

            detria::Triangulation tri;
            tri.setPoints(pts2D);
            tri.addOutline(outline);
            // All other vertices are automatically treated as Steiner points.

            bool ok = tri.triangulate(/*delaunay=*/true);
            if (!ok)
            {
                if (verbose)
                {
                    std::cout << "[PatchClusterMerger] detria triangulation failed\n";
                }
                return false;
            }

            // Collect triangles that are inside the outline
            // (detria classifies each triangle by location).
            bool cwTriangles = true;
            tri.forEachTriangle(
                [&](detria::Triangle<uint32_t> t)
                {
                    outLocalTriangles.push_back({static_cast<int32_t>(t.x),
                                                 static_cast<int32_t>(t.y),
                                                 static_cast<int32_t>(t.z)});
                },
                cwTriangles);

            return !outLocalTriangles.empty();
        }
    };
}

#endif // GTE_MATHEMATICS_PATCH_CLUSTER_MERGER_H
