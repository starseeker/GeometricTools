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

            // Maximum ratio of smallest PCA eigenvalue to largest for a
            // cluster to be considered "planar enough" for UV re-triangulation.
            // A value of 0 disables the planarity check (always attempt).
            // Typical good value: 0.3 (cluster is planar if its thickness is
            // less than 30% of its lateral extent).
            Real maxPlanarityRatio;

            bool verbose;

            Parameters()
                : numClusters(6)
                , kmeansMaxIterations(50)
                , minPatchesPerCluster(2)
                , maxPlanarityRatio(static_cast<Real>(0.3))
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
        //
        // Key design: ALL clusters are evaluated against the ORIGINAL
        // triangle array (read-only phase), and only AFTER all evaluations
        // are done are the triangles modified (write phase).  This avoids
        // stale triangle-index bugs when processing multiple clusters.
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

            // Step 3: READ-ONLY phase – compute new triangles for each cluster
            // without modifying the triangles array.
            struct ClusterResult
            {
                std::set<int32_t>               oldTriIndices;  // to remove
                std::vector<std::array<int32_t,3>> newTris;     // to add
            };
            std::vector<ClusterResult> results(params.numClusters);
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

                bool ok = ComputeClusterMerge(vertices, triangles,
                    clusterPatches, patches, params,
                    results[c].oldTriIndices, results[c].newTris);
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

            // Step 4: WRITE phase – apply all successful cluster replacements.
            if (mergedCount > 0)
            {
                // Collect all old triangle indices to remove.
                std::set<int32_t> allOldIndices;
                for (auto const& r : results)
                {
                    allOldIndices.insert(r.oldTriIndices.begin(), r.oldTriIndices.end());
                }

                std::vector<std::array<int32_t, 3>> kept;
                kept.reserve(triangles.size());
                for (int32_t ti = 0; ti < static_cast<int32_t>(triangles.size()); ++ti)
                {
                    if (!allOldIndices.count(ti))
                    {
                        kept.push_back(triangles[ti]);
                    }
                }
                for (auto const& r : results)
                {
                    kept.insert(kept.end(), r.newTris.begin(), r.newTris.end());
                }
                triangles = std::move(kept);
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

            // Initialize cluster centres by spreading evenly over sorted patches.
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
        // Step 3: compute new triangles for one cluster (READ-ONLY).
        // ---------------------------------------------------------------
        // Evaluates the cluster merge against the ORIGINAL triangles array
        // and returns the set of old triangle indices to remove and the new
        // triangles to add.  Does NOT modify 'triangles'.
        static bool ComputeClusterMerge(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<int32_t> const& clusterPatchIndices,
            std::vector<PatchInfo> const& allPatches,
            Parameters const& params,
            std::set<int32_t>& outOldTriIndices,
            std::vector<std::array<int32_t, 3>>& outNewTris)
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
            Real planarityRatio = static_cast<Real>(0);
            if (!ProjectToUV(vertices, clusterVertList, uvCoords, origin, uAxis, vAxis,
                    &planarityRatio))
            {
                return false;
            }

            // Skip non-planar clusters: UV projection would distort too much.
            if (params.maxPlanarityRatio > static_cast<Real>(0)
                && planarityRatio > params.maxPlanarityRatio)
            {
                if (params.verbose)
                {
                    std::cout << "[PatchClusterMerger] cluster skipped (planarityRatio="
                              << planarityRatio << " > " << params.maxPlanarityRatio << ")\n";
                }
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

            // --- f. Populate output: old indices to remove + new triangles. ---
            outOldTriIndices = clusterTriSet;

            outNewTris.clear();
            for (auto const& uvTri : uvTriangles)
            {
                int32_t v0 = clusterVertList[uvTri[0]];
                int32_t v1 = clusterVertList[uvTri[1]];
                int32_t v2 = clusterVertList[uvTri[2]];
                outNewTris.push_back({v0, v1, v2});
            }

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
            Vector3<Real>& vAxis,
            Real* outPlanarityRatio = nullptr)
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

            // Planarity ratio: smallest eigenvalue / largest eigenvalue.
            // A truly planar cluster has eval[0] ≈ 0.
            if (outPlanarityRatio != nullptr)
            {
                Real maxEval = eval[2];
                *outPlanarityRatio = (maxEval > static_cast<Real>(1e-15))
                    ? eval[0] / maxEval
                    : static_cast<Real>(0);
            }

            // evec[0] = eigenvector for smallest eigenvalue = surface normal.
            // evec[1] and evec[2] = the two principal tangent directions.
            uAxis = Vector3<Real>{evec[1][0], evec[1][1], evec[1][2]};
            vAxis = Vector3<Real>{evec[2][0], evec[2][1], evec[2][2]};

            // Normalize (should already be unit, but be safe).
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
        // Step 3d: outer boundary (single loop or convex hull of all loops)
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

            // Multiple loops: compute the convex hull of all boundary vertices.
            // The convex hull is a valid simple polygon and is guaranteed to
            // enclose all boundary points.  (Shrinkwrap is omitted to avoid
            // self-intersections in the UV outline.)
            std::vector<int32_t> allBoundaryVerts(
                boundaryVertSet.begin(), boundaryVertSet.end());

            std::vector<Vector2<Real>> boundaryUV;
            boundaryUV.reserve(allBoundaryVerts.size());
            for (int32_t idx : allBoundaryVerts)
            {
                boundaryUV.push_back(uvCoords[idx]);
            }

            ConvexHull2<Real> hull;
            if (!hull(boundaryUV))
            {
                // Degenerate (all collinear); return arbitrary loop.
                return loops[0];
            }

            std::vector<int32_t> const& hullLocalIndices = hull.GetHull();
            if (hullLocalIndices.size() < 3)
            {
                return loops[0];
            }

            // Map hull indices (into boundaryUV) back to cluster-local vertex indices.
            std::vector<int32_t> hullIndices;
            hullIndices.reserve(hullLocalIndices.size());
            for (int32_t h : hullLocalIndices)
            {
                hullIndices.push_back(allBoundaryVerts[h]);
            }

            return hullIndices;
        }

        // ---------------------------------------------------------------
        // Step 3e: triangulate in UV space with detria
        // ---------------------------------------------------------------
        // Only boundary vertices are used for the triangulation: the outline
        // holds the outer boundary, and the remaining boundary vertices (not
        // in the outline) are Steiner points.  Interior vertices are excluded
        // because the simple PCA projection does not guarantee they fall
        // inside the convex/concave boundary polygon.
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

            // Validate outline indices.
            int32_t numVerts = static_cast<int32_t>(uvCoords.size());
            std::set<int32_t> outlineSet(outerBoundary.begin(), outerBoundary.end());
            if (static_cast<int32_t>(outlineSet.size()) < 3)
            {
                return false;  // Degenerate (fewer than 3 unique vertices).
            }
            for (int32_t localIdx : outerBoundary)
            {
                if (localIdx < 0 || localIdx >= numVerts)
                {
                    return false;
                }
            }

            // Build detria point list from outline + remaining boundary vertices.
            // We use a compact index space: outline first, then other boundary verts.
            std::vector<detria::PointD> pts2D;
            std::vector<int32_t> localToCompact(numVerts, -1);  // local → compact index

            // Add outline vertices first.
            // Deduplicate (preserve first occurrence of each unique local index).
            std::vector<uint32_t> outline;
            for (int32_t localIdx : outerBoundary)
            {
                if (localToCompact[localIdx] == -1)
                {
                    localToCompact[localIdx] = static_cast<int32_t>(pts2D.size());
                    auto const& uv = uvCoords[localIdx];
                    pts2D.push_back({static_cast<double>(uv[0]),
                                     static_cast<double>(uv[1])});
                }
                outline.push_back(static_cast<uint32_t>(localToCompact[localIdx]));
            }

            // Add small random perturbation to break collinearity.
            // Scale is 1e-6 of the bounding box diagonal.
            {
                double minU = pts2D[0].x, maxU = pts2D[0].x;
                double minV = pts2D[0].y, maxV = pts2D[0].y;
                for (auto const& p : pts2D)
                {
                    minU = std::min(minU, p.x); maxU = std::max(maxU, p.x);
                    minV = std::min(minV, p.y); maxV = std::max(maxV, p.y);
                }
                double diag = std::sqrt((maxU-minU)*(maxU-minU) + (maxV-minV)*(maxV-minV));
                double eps = diag * 1e-6;
                if (eps > 0.0)
                {
                    // Deterministic perturbation using the golden angle (137.508°
                    // in degrees ≈ 2π/φ² radians) to ensure well-distributed
                    // pseudo-random offsets that break collinearity without
                    // clustering the perturbations at any particular direction.
                    constexpr double kGoldenAngleDeg = 137.508;
                    for (size_t i = 0; i < pts2D.size(); ++i)
                    {
                        double t = static_cast<double>(i) / static_cast<double>(pts2D.size());
                        pts2D[i].x += eps * std::sin(t * kGoldenAngleDeg);
                        pts2D[i].y += eps * std::cos(t * kGoldenAngleDeg);
                    }
                }
            }

            detria::Triangulation tri;
            tri.setPoints(pts2D);
            tri.addOutline(outline);

            bool ok = tri.triangulate(/*delaunay=*/true);
            if (!ok)
            {
                if (verbose)
                {
                    std::cout << "[PatchClusterMerger] detria triangulation failed\n";
                }
                return false;
            }

            // Build compact→local inverse map before collecting triangles.
            std::vector<int32_t> compactToLocal(pts2D.size(), -1);
            for (int32_t li = 0; li < numVerts; ++li)
            {
                int32_t ci = localToCompact[li];
                if (ci >= 0)
                {
                    compactToLocal[ci] = li;
                }
            }

            // Collect triangles inside the outline, mapping compact indices back
            // to local cluster vertex indices.
            bool cwTriangles = true;
            tri.forEachTriangle(
                [&](detria::Triangle<uint32_t> t)
                {
                    int32_t v0 = compactToLocal[t.x];
                    int32_t v1 = compactToLocal[t.y];
                    int32_t v2 = compactToLocal[t.z];
                    if (v0 >= 0 && v1 >= 0 && v2 >= 0)
                    {
                        outLocalTriangles.push_back({v0, v1, v2});
                    }
                },
                cwTriangles);

            return !outLocalTriangles.empty();
        }
    };
}

#endif // GTE_MATHEMATICS_PATCH_CLUSTER_MERGER_H
