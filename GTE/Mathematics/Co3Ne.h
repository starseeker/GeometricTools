// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Co3Ne (Concurrent Co-Cones) surface reconstruction ported from Geogram.
//
// Original Geogram Source:
//   geogram/src/lib/geogram/points/co3ne.h
//   geogram/src/lib/geogram/points/co3ne.cpp
//   https://github.com/BrunoLevy/geogram (commit f5abd69)
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// This is a faithful port of Geogram's Co3Ne algorithm:
//   1. PCA-based normal estimation
//   2. BFS-based normal orientation propagation
//   3. Co-cone candidate triangle generation (O(k^2) per seed point)
//   4. T3/T12 triangle classification by occurrence count
//   5. Output T3 triangles; T12 as fallback for sparse input
//
// The equivalent Geogram call from BRL-CAD:
//   GEO::CmdLine::set_arg("co3ne:max_N_angle", "60.0");
//   double search_dist = 0.05 * GEO::bbox_diagonal(gm);
//   GEO::Co3Ne_reconstruct(gm, search_dist);
//
// Adapted for Geometric Tools Engine:
//   - Uses GTE's SymmetricEigensolver3x3 for PCA
//   - Uses GTE's NearestNeighborQuery for spatial indexing
//   - Uses std::vector instead of GEO::Mesh
//   - Removed Geogram's command-line and threading system
//   - Added struct-based parameter system

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Matrix3x3.h>
#include <GTE/Mathematics/SymmetricEigensolver3x3.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace gte
{
    template <typename Real>
    class Co3Ne
    {
    public:
        struct Parameters
        {
            size_t kNeighbors;              // Number of neighbors for PCA (default: 20)
            Real searchRadius;              // Search radius (0 = auto-compute)
            Real maxNormalAngle;            // Max angle for co-cone filter (degrees, default: 60)
            Real triangleQualityThreshold;  // Minimum triangle quality 0-1 (default: 0.3)
            bool orientNormals;             // Orient normals consistently via BFS (default: true)
            // Optional RVD-based smoothing (Lloyd relaxation on the mesh).
            // NOTE: O(n^2) per iteration — disabled by default.
            // Only practical for small meshes (n < ~2000 vertices).
            bool smoothWithRVD;
            size_t rvdSmoothIterations;

            Parameters()
                : kNeighbors(20)
                , searchRadius(static_cast<Real>(0))
                , maxNormalAngle(static_cast<Real>(60))
                , triangleQualityThreshold(static_cast<Real>(0.3))
                , orientNormals(true)
                , smoothWithRVD(false)
                , rvdSmoothIterations(3)
            {
            }
        };

        // Reconstruct a surface mesh from a raw point cloud.
        // Normals are estimated via PCA and optionally oriented via BFS.
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters())
        {
            if (points.empty())
            {
                return false;
            }

            using Site = PositionSite<3, Real>;
            std::vector<Site> sites;
            sites.reserve(points.size());
            for (auto const& p : points)
            {
                sites.emplace_back(p);
            }

            static constexpr int32_t maxLeafSize = 10;
            static constexpr int32_t maxLevel = 20;
            NearestNeighborQuery<3, Real, Site> nnQuery(sites, maxLeafSize, maxLevel);

            Real searchRadius = params.searchRadius;
            if (searchRadius == static_cast<Real>(0))
            {
                searchRadius = ComputeAutomaticSearchRadius(points);
            }

            std::vector<Vector3<Real>> normals;
            if (!ComputeNormals(points, normals, params, nnQuery, searchRadius))
            {
                return false;
            }

            if (params.orientNormals)
            {
                OrientNormalsConsistently(points, normals, params, nnQuery, searchRadius);
            }

            std::vector<std::array<int32_t, 3>> candidateTriangles;
            GenerateTriangles(points, normals, candidateTriangles, params, nnQuery, searchRadius);

            if (candidateTriangles.empty())
            {
                return false;
            }

            ClassifyAndOutput(candidateTriangles, outTriangles);

            outVertices = points;

            if (params.smoothWithRVD && params.rvdSmoothIterations > 0 && !outTriangles.empty())
            {
                SmoothWithRVD(outVertices, outTriangles, params);
            }

            return !outTriangles.empty();
        }

        // Reconstruct using caller-supplied normals (e.g. scanner normals from
        // an XYZ file).  PCA estimation is skipped.
        //
        // NOTE: if the normals are already consistently oriented (all outward),
        // pass orientNormals=false to preserve them as-is.  BFS re-orientation
        // can flip normals near thin features where cross-surface neighbours are
        // geometrically close.
        static bool ReconstructWithNormals(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters())
        {
            if (points.empty() || normals.size() != points.size())
            {
                return false;
            }

            using Site = PositionSite<3, Real>;
            std::vector<Site> sites;
            sites.reserve(points.size());
            for (auto const& p : points)
            {
                sites.emplace_back(p);
            }

            static constexpr int32_t maxLeafSize = 10;
            static constexpr int32_t maxLevel = 20;
            NearestNeighborQuery<3, Real, Site> nnQuery(sites, maxLeafSize, maxLevel);

            Real searchRadius = params.searchRadius;
            if (searchRadius == static_cast<Real>(0))
            {
                searchRadius = ComputeAutomaticSearchRadius(points);
            }

            std::vector<Vector3<Real>> workNormals = normals;
            if (params.orientNormals)
            {
                OrientNormalsConsistently(points, workNormals, params, nnQuery, searchRadius);
            }

            std::vector<std::array<int32_t, 3>> candidateTriangles;
            GenerateTriangles(points, workNormals, candidateTriangles, params, nnQuery, searchRadius);

            if (candidateTriangles.empty())
            {
                return false;
            }

            ClassifyAndOutput(candidateTriangles, outTriangles);

            outVertices = points;

            if (params.smoothWithRVD && params.rvdSmoothIterations > 0 && !outTriangles.empty())
            {
                SmoothWithRVD(outVertices, outTriangles, params);
            }

            return !outTriangles.empty();
        }

    private:
        using NNTree = NearestNeighborQuery<3, Real, PositionSite<3, Real>>;

        // Hash for std::array<int32_t,3> used in triangle-count map.
        struct TriangleArrayHash
        {
            size_t operator()(std::array<int32_t, 3> const& a) const noexcept
            {
                size_t h = 0;
                for (auto v : a)
                {
                    h ^= std::hash<int32_t>{}(v) + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                }
                return h;
            }
        };

        // ===== NORMAL ESTIMATION =====

        static bool ComputeNormals(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& normals,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            normals.resize(points.size());

            for (size_t i = 0; i < points.size(); ++i)
            {
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3)
                {
                    normals[i] = Vector3<Real>::Unit(2);
                    continue;
                }

                int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));

                // Centroid of neighbors
                Vector3<Real> centroid = Vector3<Real>::Zero();
                for (int32_t j = 0; j < k; ++j)
                {
                    centroid += points[indices[j]];
                }
                centroid /= static_cast<Real>(k);

                // Covariance matrix
                Matrix3x3<Real> cov;
                cov.MakeZero();
                for (int32_t j = 0; j < k; ++j)
                {
                    Vector3<Real> d = points[indices[j]] - centroid;
                    cov(0, 0) += d[0] * d[0];
                    cov(0, 1) += d[0] * d[1];
                    cov(0, 2) += d[0] * d[2];
                    cov(1, 1) += d[1] * d[1];
                    cov(1, 2) += d[1] * d[2];
                    cov(2, 2) += d[2] * d[2];
                }
                cov(1, 0) = cov(0, 1);
                cov(2, 0) = cov(0, 2);
                cov(2, 1) = cov(1, 2);
                cov *= static_cast<Real>(1) / static_cast<Real>(k);

                // Normal = eigenvector for the smallest eigenvalue
                SymmetricEigensolver3x3<Real> solver;
                std::array<Real, 3> eigenvalues;
                std::array<std::array<Real, 3>, 3> eigenvectors;
                solver(cov(0,0), cov(0,1), cov(0,2),
                       cov(1,1), cov(1,2), cov(2,2),
                       false, +1, eigenvalues, eigenvectors);

                normals[i][0] = eigenvectors[0][0];
                normals[i][1] = eigenvectors[0][1];
                normals[i][2] = eigenvectors[0][2];
                Normalize(normals[i]);
            }

            return true;
        }

        // ===== NORMAL ORIENTATION =====

        // BFS propagation on the k-NN graph to make normals consistent.
        // Handles disconnected components by restarting from unvisited seeds.
        static void OrientNormalsConsistently(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& normals,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            size_t n = points.size();
            if (n == 0) return;

            struct OrientElem
            {
                Real deviation;
                size_t vertex;
                bool operator<(OrientElem const& o) const { return deviation > o.deviation; }
            };

            std::vector<bool> visited(n, false);

            for (size_t seed = 0; seed < n; ++seed)
            {
                if (visited[seed]) continue;

                std::priority_queue<OrientElem> queue;
                visited[seed] = true;
                queue.push({ static_cast<Real>(0), seed });

                while (!queue.empty())
                {
                    size_t i = queue.top().vertex;
                    queue.pop();

                    constexpr int32_t MaxNeighbors = 100;
                    std::array<int32_t, MaxNeighbors> indices;
                    int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                        points[i], searchRadius, indices);

                    int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));
                    for (int32_t j = 0; j < k; ++j)
                    {
                        size_t nb = static_cast<size_t>(indices[j]);
                        if (nb == i || visited[nb]) continue;
                        visited[nb] = true;

                        Real dot = Dot(normals[i], normals[nb]);
                        if (dot < static_cast<Real>(0))
                        {
                            normals[nb] = -normals[nb];
                            dot = -dot;
                        }
                        queue.push({ static_cast<Real>(1) - dot, nb });
                    }
                }
            }
        }

        // ===== TRIANGLE GENERATION =====

        // For each seed point i:
        //   1. Find neighbors within the search radius (up to MaxNeighbors).
        //   2. Collect up to kNeighbors neighbors that pass the co-cone test
        //      (normal angle < maxNormalAngle).  Scan the full found list so
        //      cross-surface captures on thin geometry don't crowd out valid
        //      same-surface neighbors.
        //   3. Propose every pair of consistent neighbors as a candidate
        //      triangle — O(k^2) per seed.  Degenerate triangles are rejected.
        //
        // The same triangle (i,j,k) is proposed once for each of i, j, k as
        // seed, giving a raw count of up to 3.  ClassifyAndOutput uses that
        // count for T3/T12 classification.
        static void GenerateTriangles(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            static constexpr Real kPi = static_cast<Real>(3.14159265358979323846);
            Real maxCosAngle = std::cos(params.maxNormalAngle * kPi / static_cast<Real>(180));
            int32_t const kTarget = static_cast<int32_t>(params.kNeighbors);

            for (size_t i = 0; i < points.size(); ++i)
            {
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3) continue;

                // Collect up to kTarget co-cone consistent neighbors
                std::vector<int32_t> consistent;
                consistent.reserve(static_cast<size_t>(kTarget));
                for (int32_t j = 0; j < numFound &&
                     static_cast<int32_t>(consistent.size()) < kTarget; ++j)
                {
                    int32_t nb = indices[j];
                    if (nb == static_cast<int32_t>(i)) continue;
                    if (Dot(normals[i], normals[nb]) >= maxCosAngle)
                    {
                        consistent.push_back(nb);
                    }
                }

                if (consistent.size() < 2) continue;

                int32_t const v0 = static_cast<int32_t>(i);

                // Propose all pairs
                for (size_t j = 0; j < consistent.size(); ++j)
                {
                    for (size_t kk = j + 1; kk < consistent.size(); ++kk)
                    {
                        int32_t v1 = consistent[j];
                        int32_t v2 = consistent[kk];

                        if (!IsTriangleValid(points, v0, v1, v2, params)) continue;

                        // Align winding with the seed normal
                        Vector3<Real> e1 = points[v1] - points[v0];
                        Vector3<Real> e2 = points[v2] - points[v0];
                        if (Dot(Cross(e1, e2), normals[i]) < static_cast<Real>(0))
                        {
                            std::swap(v1, v2);
                        }

                        triangles.push_back({ v0, v1, v2 });
                    }
                }
            }
        }

        // ===== T3/T12 CLASSIFICATION =====

        // Count how many seeds proposed each canonical triangle.  Each triangle
        // (i,j,k) can be proposed up to 3 times (once by each vertex as seed):
        //
        //   count == 3  ->  T3  ("good"):  all three vertices agree
        //   count  < 3  ->  T12:           one or two vertices agree
        //   count  > 3  ->  discard:       over-proposed, geometry ambiguous
        //
        // T3 triangles are output first.  If the input is too sparse or normals
        // are poorly oriented to produce any T3 triangles, T12 triangles are
        // used as a fallback so the caller always gets some output.
        static void ClassifyAndOutput(
            std::vector<std::array<int32_t, 3>> const& candidateTriangles,
            std::vector<std::array<int32_t, 3>>& outTriangles)
        {
            struct TriInfo
            {
                int                    count   = 0;
                std::array<int32_t, 3> oriented{};
            };

            std::unordered_map<std::array<int32_t, 3>, TriInfo, TriangleArrayHash> seen;
            seen.reserve(candidateTriangles.size() / 3 + 1);

            for (auto const& tri : candidateTriangles)
            {
                std::array<int32_t, 3> key = tri;
                std::sort(key.begin(), key.end());
                auto& info = seen[key];
                if (info.count == 0)
                {
                    info.oriented = tri;   // preserve first winding-aligned proposal
                }
                ++info.count;
            }

            std::vector<std::array<int32_t, 3>> t12;
            for (auto const& [key, info] : seen)
            {
                if (info.count == 3)
                {
                    outTriangles.push_back(info.oriented);
                }
                else if (info.count < 3)
                {
                    t12.push_back(info.oriented);
                }
                // count > 3: discard
            }

            // Fallback to T12 when no T3 triangles were produced
            if (outTriangles.empty())
            {
                outTriangles = std::move(t12);
            }
        }

        // ===== UTILITIES =====

        static bool IsTriangleValid(
            std::vector<Vector3<Real>> const& points,
            int32_t v0, int32_t v1, int32_t v2,
            Parameters const& params)
        {
            Vector3<Real> e1 = points[v1] - points[v0];
            Vector3<Real> e2 = points[v2] - points[v0];
            Vector3<Real> n  = Cross(e1, e2);

            Real area = Length(n);
            if (area < std::numeric_limits<Real>::epsilon()) return false;

            Real len0 = Length(points[v1] - points[v0]);
            Real len1 = Length(points[v2] - points[v1]);
            Real len2 = Length(points[v0] - points[v2]);
            Real maxLen = std::max({ len0, len1, len2 });
            if (maxLen < std::numeric_limits<Real>::epsilon()) return false;

            // Quality = area / maxEdge^2; equilateral triangle ≈ 0.433
            Real quality = area / (maxLen * maxLen);
            return quality >= params.triangleQualityThreshold * static_cast<Real>(0.433);
        }

        // Use 5% of the bounding-box diagonal as the search radius,
        // matching Geogram's default (0.05 * bbox_diagonal).
        static Real ComputeAutomaticSearchRadius(
            std::vector<Vector3<Real>> const& points)
        {
            if (points.empty()) return static_cast<Real>(1);

            Vector3<Real> lo = points[0], hi = points[0];
            for (auto const& p : points)
            {
                for (int i = 0; i < 3; ++i)
                {
                    lo[i] = std::min(lo[i], p[i]);
                    hi[i] = std::max(hi[i], p[i]);
                }
            }
            return static_cast<Real>(0.05) * Length(hi - lo);
        }

        // Optional RVD-based Lloyd relaxation for improved vertex distribution.
        // O(n^2) per iteration; disabled by default.
        static void SmoothWithRVD(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            Parameters const& params)
        {
            if (vertices.empty() || triangles.empty()) return;

            RestrictedVoronoiDiagram<Real> rvd;
            for (size_t iter = 0; iter < params.rvdSmoothIterations; ++iter)
            {
                if (!rvd.Initialize(vertices, triangles, vertices)) return;

                std::vector<Vector3<Real>> centroids;
                if (!rvd.ComputeCentroids(centroids)) return;

                Real damp = static_cast<Real>(0.5);
                for (size_t i = 0; i < vertices.size(); ++i)
                {
                    vertices[i] += damp * (centroids[i] - vertices[i]);
                }
            }
        }
    };
}
