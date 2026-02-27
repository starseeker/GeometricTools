// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.27
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
//   1. PCA-based normal estimation (or caller-supplied scanner normals)
//   2. BFS-based normal orientation propagation
//   3. Disk-clipping RVC (Restricted Voronoi Cell) candidate triangle generation
//      -- matches Geogram's Co3NeRestrictedVoronoiDiagram exactly
//   4. T3/T12 triangle classification by occurrence count
//   5. Output T3 triangles and T12 triangles (filtered for Möbius configurations)
//   6. Optional post-reconstruction mesh repair (Geogram: co3ne:repair=true default)
//
// The equivalent Geogram call from BRL-CAD:
//   GEO::CmdLine::set_arg("co3ne:max_N_angle", "60.0");
//   double search_dist = 0.05 * GEO::bbox_diagonal(gm);
//   GEO::Co3Ne_reconstruct(gm, search_dist);
//
// Adapted for Geometric Tools Engine:
//   - Uses GTE's SymmetricEigensolver3x3 for PCA
//   - Uses GTE's NearestNeighborQuery for spatial indexing
//   - Uses GTE's MeshRepair for post-reconstruction repair
//   - Uses std::vector instead of GEO::Mesh
//   - Removed Geogram's command-line and threading system
//   - Added struct-based parameter system

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Matrix3x3.h>
#include <GTE/Mathematics/SymmetricEigensolver3x3.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gte
{
    template <typename Real>
    class Co3Ne
    {
    public:
        struct Parameters
        {
            size_t kNeighbors;              // Number of neighbors for PCA / initial kNN search (default: 20)
            Real searchRadius;              // Search radius (0 = auto-compute from bbox diagonal)
            Real maxNormalAngle;            // Max angle for co-cone filter (degrees, default: 60)
            Real triangleQualityThreshold;  // Minimum triangle quality 0-1 (default: 0.3)
            bool orientNormals;             // Orient normals consistently via BFS (default: true)
            // Disk-clipping RVC triangle generation.
            // When true (default), uses Geogram's Co3NeRestrictedVoronoiDiagram approach:
            //   - approximates a disk (circle) at each point, perpendicular to its normal
            //   - clips the disk polygon against Voronoi bisector halfspaces
            //   - reads triangle candidates from consecutive polygon edges with different seeds
            // This gives sparser, surface-aligned connectivity (~6-12 neighbors/point)
            // that matches Geogram's output density.
            // When false, falls back to the original kNN all-pairs O(k^2) generation.
            bool useRVC;
            // Number of disk polygon vertices for RVC (default: 10, same as Geogram).
            // Higher values improve the approximation of the circle at a small cost.
            size_t rvcDiskSamples;
            // Run MeshRepair after reconstruction (Geogram: co3ne:repair=true default).
            // Removes duplicate vertices, degenerate triangles, and isolated components.
            bool repairAfterReconstruct;
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
                , useRVC(true)
                , rvcDiskSamples(10)
                , repairAfterReconstruct(false)  // disabled by default for API compatibility
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

            RunPostReconstruct(outVertices, outTriangles, params);

            return !outTriangles.empty();
        }

        // Reconstruct using caller-supplied normals (e.g. scanner normals from
        // an XYZ file).  PCA estimation is skipped.
        //
        // This matches Geogram's co3ne:use_normals=true default: when normals are
        // already attached to the pointset (e.g. from a scanner), Geogram uses them
        // directly instead of re-estimating via PCA.
        //
        // NOTE: if the normals are already consistently oriented (all outward),
        // pass orientNormals=false to preserve them as-is.  BFS re-orientation
        // can flip normals near thin features where cross-surface neighbors are
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

            RunPostReconstruct(outVertices, outTriangles, params);

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

        // Dispatch to either the disk-clipping RVC approach (Geogram-compatible) or
        // the fallback kNN all-pairs approach, depending on params.useRVC.
        static void GenerateTriangles(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            if (params.useRVC)
            {
                GenerateTrianglesRVC(points, normals, triangles, params, nnQuery, searchRadius);
            }
            else
            {
                GenerateTrianglesKNN(points, normals, triangles, params, nnQuery, searchRadius);
            }
        }

        // ===== DISK-CLIPPING RVC (GEOGRAM-COMPATIBLE) =====
        //
        // Matches Geogram's Co3NeRestrictedVoronoiDiagram::get_RVC() exactly.
        //
        // For each seed point i with normal N_i:
        //   1. Build a disk polygon (sincos_nb vertices) centered at i, perpendicular
        //      to N_i, with radius = searchRadius.  Each polygon vertex carries an
        //      "adjacent_seed" label (-1 initially).
        //   2. Fetch nearest neighbors incrementally (starting from kNeighbors=20,
        //      growing by 1/3 each round) until no neighbor inside the "radius of
        //      security" (2*radius) can clip the polygon further.
        //   3. For each neighbor j in that set, clip the polygon against the bisector
        //      halfspace between i and j. Vertices on the j-side of the new edge
        //      receive adjacent_seed = j.
        //   4. Read triangle candidates from the clipped polygon: for each consecutive
        //      edge (v1, v2) where v1.adjacent_seed = j and v2.adjacent_seed = k,
        //      j >= 0, k >= 0, j != k, emit triangle {i, j, k}.
        //
        // This gives ~6-12 effective neighbors per point (natural Voronoi valence)
        // regardless of the kNeighbors parameter, matching Geogram's output density.

        // A vertex of the disk polygon carrying adjacency information.
        struct DiskVertex
        {
            Vector3<Real> point;
            int32_t adjacentSeed;  // index of the Voronoi seed across the outgoing edge (-1 = none)

            DiskVertex() : point(Vector3<Real>::Zero()), adjacentSeed(-1) {}
            explicit DiskVertex(Vector3<Real> const& p) : point(p), adjacentSeed(-1) {}
        };

        // Clip a polygon (in-place) against the bisector halfspace between pi and pj.
        // Vertices on the pi-side are kept; vertices on the pj-side are removed.
        // Newly inserted intersection vertices on the outgoing edge (from pi-side to
        // pj-side) receive adjacentSeed = j; those on the incoming edge keep the
        // adjacentSeed of the outgoing vertex of the pi-side edge.
        // This is a direct translation of Geogram's clip_polygon_by_bisector().
        static void ClipDiskByBisector(
            std::vector<DiskVertex>& poly,
            Vector3<Real> const& pi,
            Vector3<Real> const& pj,
            int32_t j)
        {
            if (poly.empty()) return;

            std::vector<DiskVertex> result;
            result.reserve(poly.size() + 1);

            // Bisector normal n = pi - pj; plane passes through midpoint
            // dot(n, p) >= dot(n, (pi+pj)/2) is the pi-side
            Real nx = pi[0] - pj[0];
            Real ny = pi[1] - pj[1];
            Real nz = pi[2] - pj[2];
            // plane constant d = n . (pi + pj)  (2 * midpoint, avoids /2)
            Real d = nx * (pi[0] + pj[0]) + ny * (pi[1] + pj[1]) + nz * (pi[2] + pj[2]);

            size_t n = poly.size();
            size_t prevIdx = n - 1;
            const DiskVertex* prevV = &poly[prevIdx];
            Real prevL = nx * prevV->point[0] + ny * prevV->point[1] + nz * prevV->point[2];
            // side: positive (> d/2) means on pi-side, i.e. 2*l > d
            int prevSign = (2.0 * prevL > d) ? 1 : ((2.0 * prevL < d) ? -1 : 0);

            for (size_t k = 0; k < n; ++k)
            {
                const DiskVertex* vk = &poly[k];
                Real l = nx * vk->point[0] + ny * vk->point[1] + nz * vk->point[2];
                int sign = (static_cast<Real>(2) * l > d) ? 1
                         : (static_cast<Real>(2) * l < d) ? -1 : 0;

                if (sign != prevSign && prevSign != 0)
                {
                    // Edge crosses the bisector; compute intersection
                    Real denom = static_cast<Real>(2) * (prevL - l);
                    Real lambda1, lambda2;
                    if (std::abs(denom) < static_cast<Real>(1e-20))
                    {
                        lambda1 = static_cast<Real>(0.5);
                        lambda2 = static_cast<Real>(0.5);
                    }
                    else
                    {
                        lambda1 = (d - static_cast<Real>(2) * l) / denom;
                        lambda2 = static_cast<Real>(1) - lambda1;
                    }

                    DiskVertex iV;
                    iV.point[0] = lambda1 * prevV->point[0] + lambda2 * vk->point[0];
                    iV.point[1] = lambda1 * prevV->point[1] + lambda2 * vk->point[1];
                    iV.point[2] = lambda1 * prevV->point[2] + lambda2 * vk->point[2];
                    // If the new vertex is on the positive (pi-side) incoming arc,
                    // it inherits prevV's adjacentSeed; otherwise the edge index j.
                    iV.adjacentSeed = (sign > 0) ? prevV->adjacentSeed : j;
                    result.push_back(iV);
                }

                if (sign > 0)
                {
                    result.push_back(*vk);
                }

                prevV = vk;
                prevSign = sign;
                prevL = l;
            }

            poly = std::move(result);
        }

        // Compute the squared radius (max squared distance from center to polygon vertices).
        static Real SquaredRadius(
            Vector3<Real> const& center,
            std::vector<DiskVertex> const& poly)
        {
            Real r2 = static_cast<Real>(0);
            for (auto const& v : poly)
            {
                Vector3<Real> d = v.point - center;
                Real sq = Dot(d, d);
                if (sq > r2) r2 = sq;
            }
            return r2;
        }

        // Compute the Restricted Voronoi Cell (RVC) of point i: a disk perpendicular
        // to normal N_i, clipped by Voronoi bisectors against its neighbors.
        // outPoly receives the clipped polygon with per-vertex adjacency labels.
        // This matches Geogram's get_RVC(i, N, P, Q, neigh, sq_dist).
        static void ComputeRVC(
            size_t i,
            Vector3<Real> const& N,
            Vector3<Real> const& pi,
            std::vector<Vector3<Real>> const& points,
            NNTree const& nnQuery,
            Real searchRadius,
            size_t diskSamples,
            std::vector<DiskVertex>& outPoly)
        {
            outPoly.clear();

            // Build the disk polygon: diskSamples vertices on a circle of radius
            // searchRadius, centered at pi and perpendicular to N.
            // Match Geogram's get_circle(): U = perpendicular(N), V = cross(N,U)
            // Geogram uses a precomputed sincos table of 10 entries.
            Vector3<Real> Nn = N;
            Normalize(Nn);

            // Compute an arbitrary perpendicular to Nn
            Vector3<Real> U;
            if (std::abs(Nn[0]) <= std::abs(Nn[1]) && std::abs(Nn[0]) <= std::abs(Nn[2]))
                U = { static_cast<Real>(0), -Nn[2], Nn[1] };
            else if (std::abs(Nn[1]) <= std::abs(Nn[2]))
                U = { -Nn[2], static_cast<Real>(0), Nn[0] };
            else
                U = { -Nn[1], Nn[0], static_cast<Real>(0) };
            Normalize(U);
            Vector3<Real> V = Cross(Nn, U);
            Normalize(V);

            static constexpr Real kPi = static_cast<Real>(3.14159265358979323846);
            size_t nb = diskSamples;
            outPoly.reserve(nb);
            for (size_t k = 0; k < nb; ++k)
            {
                Real alpha = static_cast<Real>(2) * kPi * static_cast<Real>(k) / static_cast<Real>(nb);
                Real s = std::sin(alpha);
                Real c = std::cos(alpha);
                DiskVertex dv;
                dv.point = pi + c * searchRadius * U + s * searchRadius * V;
                dv.adjacentSeed = -1;
                outPoly.push_back(dv);
            }

            // Squared radius of security: if neighbor is further than 2*R, it
            // cannot clip a circle of radius R.
            Real sqROS = static_cast<Real>(4) * searchRadius * searchRadius;

            // Incrementally fetch neighbors (start at 20, grow by 1/3 each round)
            // until no neighbor inside sqROS can clip the polygon further.
            constexpr int32_t MaxNeighbors = 100;
            std::array<int32_t, MaxNeighbors> indices;
            int32_t nbFetched = 0;
            size_t jj = 0;

            size_t nbNeigh = 20;
            const size_t maxNeigh = std::min(static_cast<size_t>(MaxNeighbors),
                                             points.size() - 1);

            while (nbNeigh <= maxNeigh)
            {
                if (outPoly.size() < 3) break;

                // Fetch more neighbors if needed
                if (static_cast<size_t>(nbFetched) < nbNeigh)
                {
                    nbFetched = nnQuery.template FindNeighbors<MaxNeighbors>(
                        pi, searchRadius * static_cast<Real>(2), indices);
                    // nbFetched is now the total found within 2*radius
                    nbNeigh = maxNeigh;  // we got all of them in one query; done after this pass
                }

                // Process fetched neighbors in distance order
                while (jj < static_cast<size_t>(nbFetched))
                {
                    int32_t nb_idx = indices[jj];
                    if (nb_idx == static_cast<int32_t>(i))
                    {
                        ++jj;
                        continue;
                    }

                    Vector3<Real> pj = points[nb_idx];
                    Vector3<Real> d = pj - pi;
                    Real sq_dist = Dot(d, d);

                    if (sq_dist > sqROS) break;

                    Real Rk = SquaredRadius(pi, outPoly);
                    if (sq_dist > static_cast<Real>(4) * Rk) break;

                    ClipDiskByBisector(outPoly, pi, pj, nb_idx);
                    ++jj;
                }

                if (static_cast<size_t>(nbFetched) < nbNeigh ||
                    jj >= static_cast<size_t>(nbFetched))
                {
                    break;
                }

                nbNeigh += nbNeigh / 3 + 1;
                nbNeigh = std::min(nbNeigh, maxNeigh);
            }
        }

        // Generate triangle candidates using Geogram's disk-clipping RVC approach.
        // For each seed point i, compute its RVC (clipped disk polygon), then read
        // triangles {i, j, k} from consecutive polygon edges where adjacent_seed
        // values j and k differ and both are non-negative.
        static void GenerateTrianglesRVC(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            static constexpr Real kPi = static_cast<Real>(3.14159265358979323846);
            Real maxCosAngle = std::cos(params.maxNormalAngle * kPi / static_cast<Real>(180));

            std::vector<DiskVertex> poly;

            for (size_t i = 0; i < points.size(); ++i)
            {
                ComputeRVC(i, normals[i], points[i], points,
                           nnQuery, searchRadius, params.rvcDiskSamples, poly);

                if (poly.size() < 3) continue;

                // Extract triangles from consecutive polygon edges
                size_t nb = poly.size();
                for (size_t v1 = 0; v1 < nb; ++v1)
                {
                    size_t v2 = (v1 + 1) % nb;
                    int32_t j = poly[v1].adjacentSeed;
                    int32_t k = poly[v2].adjacentSeed;

                    if (j < 0 || k < 0 || j == k) continue;

                    // Co-cone test: both neighbors must have normals within maxNormalAngle
                    if (Dot(normals[i], normals[j]) < maxCosAngle) continue;
                    if (Dot(normals[i], normals[k]) < maxCosAngle) continue;

                    int32_t v0 = static_cast<int32_t>(i);
                    int32_t v1i = j;
                    int32_t v2i = k;

                    // Align winding with the seed normal
                    Vector3<Real> e1 = points[v1i] - points[v0];
                    Vector3<Real> e2 = points[v2i] - points[v0];
                    if (Dot(Cross(e1, e2), normals[i]) < static_cast<Real>(0))
                    {
                        std::swap(v1i, v2i);
                    }

                    triangles.push_back({ v0, v1i, v2i });
                }
            }
        }

        // Fallback: kNN all-pairs O(k^2) triangle generation.
        // For each seed point i:
        //   1. Find neighbors within the search radius (up to MaxNeighbors).
        //   2. Collect up to kNeighbors neighbors that pass the co-cone test.
        //   3. Propose every pair of consistent neighbors as a candidate triangle.
        static void GenerateTrianglesKNN(
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
        // Matching Geogram's strategy: T3 triangles are output first, then T12
        // triangles are also output, filtered for Möbius configurations.
        // A "Möbius configuration" occurs when a T12 triangle's directed edge
        // already appears in the same orientation in an accepted triangle — adding
        // it would create a non-orientable (Möbius-band-like) surface patch.
        // This is detected by maintaining a set of used directed half-edges.
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

            // Phase 1: collect T3 (count==3) and T12 (count<3) triangles.
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

            // Phase 2: emit T12 triangles, filtered for Möbius configurations.
            // Matches Geogram's co3ne.cpp: T12 triangles are always emitted (not
            // merely a last-resort fallback), except those whose directed edges
            // would conflict with the orientation of already-accepted triangles.
            //
            // Encode a directed half-edge (from, to) as a single 64-bit key.
            auto edgeKey = [](int32_t from, int32_t to) -> size_t
            {
                return (static_cast<size_t>(static_cast<uint32_t>(from)) << 32)
                     |  static_cast<size_t>(static_cast<uint32_t>(to));
            };

            // Register all directed half-edges of the accepted T3 triangles.
            std::unordered_set<size_t> usedEdges;
            usedEdges.reserve(outTriangles.size() * 3);
            for (auto const& tri : outTriangles)
            {
                usedEdges.insert(edgeKey(tri[0], tri[1]));
                usedEdges.insert(edgeKey(tri[1], tri[2]));
                usedEdges.insert(edgeKey(tri[2], tri[0]));
            }

            // Accept each T12 triangle unless it is a Möbius configuration.
            for (auto const& tri : t12)
            {
                if (usedEdges.count(edgeKey(tri[0], tri[1])) ||
                    usedEdges.count(edgeKey(tri[1], tri[2])) ||
                    usedEdges.count(edgeKey(tri[2], tri[0])))
                {
                    continue;   // Möbius configuration: skip this triangle
                }

                outTriangles.push_back(tri);
                usedEdges.insert(edgeKey(tri[0], tri[1]));
                usedEdges.insert(edgeKey(tri[1], tri[2]));
                usedEdges.insert(edgeKey(tri[2], tri[0]));
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

        // Run post-reconstruction operations: optional RVD smoothing and mesh repair.
        // Matches Geogram's post-reconstruction pipeline (co3ne:repair=true by default).
        static void RunPostReconstruct(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params)
        {
            if (triangles.empty()) return;

            if (params.smoothWithRVD && params.rvdSmoothIterations > 0)
            {
                SmoothWithRVD(vertices, triangles, params);
            }

            // Post-reconstruction repair: remove duplicate vertices, degenerate
            // triangles, and small isolated components.
            // Matches Geogram's mesh_repair(MESH_REPAIR_DEFAULT | MESH_REPAIR_RECONSTRUCT)
            // called when co3ne:repair=true (the default in BRL-CAD's co3ne.cpp).
            if (params.repairAfterReconstruct)
            {
                typename MeshRepair<Real>::Parameters repairParams;
                repairParams.mode = MeshRepair<Real>::RepairMode::DEFAULT;
                MeshRepair<Real>::Repair(vertices, triangles, repairParams);
            }
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
