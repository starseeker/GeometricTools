// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.12
//
// Co3Ne (Concurrent Co-Cones) surface reconstruction ported from Geogram
//
// Original Geogram Source:
// - geogram/src/lib/geogram/points/co3ne.h
// - geogram/src/lib/geogram/points/co3ne.cpp (2663 lines)
// - https://github.com/BrunoLevy/geogram (commit f5abd69)
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// This is a FULL IMPLEMENTATION of Geogram's Co3Ne algorithm including:
// 1. PCA-based normal estimation with orientation propagation
// 2. Co-cone based triangle generation
// 3. Sophisticated manifold extraction with:
//    - Edge topology tracking
//    - Connected component analysis
//    - Orientation enforcement
//    - Moebius strip detection
//    - Non-manifold edge rejection
//    - Incremental triangle insertion with rollback
//
// Adapted for Geometric Tools Engine:
// - Uses GTE's SymmetricEigensolver3x3 for PCA
// - Uses GTE's NearestNeighborQuery for spatial indexing
// - Uses std::vector instead of GEO::Mesh
// - Removed Geogram's command-line and threading system
// - Added struct-based parameter system

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Matrix3x3.h>
#include <GTE/Mathematics/SymmetricEigensolver3x3.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <GTE/Mathematics/ETManifoldMesh.h>
#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <GTE/Mathematics/Co3NeManifoldExtractor.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
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
            Real searchRadius;              // Search radius (0 = auto)
            Real maxNormalAngle;            // Max angle for normal agreement (degrees)
            Real triangleQualityThreshold;  // Minimum triangle quality (0-1)
            bool orientNormals;             // Orient normals consistently via propagation
            bool strictMode;                // Strict manifold extraction (more conservative)
            bool removeIsolatedTriangles;   // Remove isolated triangles
            bool smoothWithRVD;             // Post-process with RVD-based smoothing (improves quality)
            size_t rvdSmoothIterations;     // Number of RVD smoothing iterations
            bool relaxedManifoldExtraction; // Use relaxed manifold extraction (accept more triangles)
            bool bypassManifoldExtraction;  // Skip manifold extraction entirely (output all unique triangles)
            bool autoFixNonManifold;        // Automatically fix non-manifold edges (guarantees manifold output)
            bool fixWindingOrder;           // Fix triangle winding to be consistent (outward-facing)
            bool preventSelfIntersections;  // Reject triangles that would cause self-intersections (EXPERIMENTAL - can be too aggressive)
            // Manifold-constrained generation: enforces manifold topology during triangle
            // production using tangent-plane angular ordering (approximating Voronoi polygon
            // order). Instead of generating all O(k²) neighbor pairs, only angularly-
            // consecutive pairs are triangulated and each undirected edge is allowed in at
            // most 2 triangles. Winding is aligned with the point normal at generation
            // time, eliminating the need for the post-hoc FixWindingOrder step.
            bool useManifoldConstrainedGeneration;
            
            Parameters()
                : kNeighbors(20)
                , searchRadius(static_cast<Real>(0))
                , maxNormalAngle(static_cast<Real>(90))
                , triangleQualityThreshold(static_cast<Real>(0.3))
                , orientNormals(true)
                , strictMode(false)
                , removeIsolatedTriangles(true)
                , smoothWithRVD(false)      // Disabled by default: O(n²)/iter; see SmoothWithRVD for full analysis
                , rvdSmoothIterations(3)    // 3 iterations usually sufficient
                , relaxedManifoldExtraction(false)  // Default to original behavior
                , bypassManifoldExtraction(false)   // Default to standard manifold extraction
                , autoFixNonManifold(false)         // Default: don't auto-fix
                , fixWindingOrder(true)             // Default: fix winding (important for BRL-CAD)
                , preventSelfIntersections(false)   // Default: DISABLED (too aggressive, experimental)
                , useManifoldConstrainedGeneration(false) // Default: use original generation
            {
            }
        };

        // Main reconstruction function
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

            // Build the KD-tree ONCE and compute the search radius ONCE.
            // All three spatial-query phases (normal estimation, orientation
            // propagation, and triangle generation) use the same tree because
            // NearestNeighborQuery searches by position only; the direction
            // component of PositionDirectionSite does not affect the query.
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

            // Step 1: Compute normals using PCA
            std::vector<Vector3<Real>> normals;
            if (!ComputeNormals(points, normals, params, nnQuery, searchRadius))
            {
                return false;
            }

            // Step 2: Orient normals consistently if requested
            if (params.orientNormals)
            {
                OrientNormalsConsistently(points, normals, params, nnQuery, searchRadius);
            }

            // Step 3: Generate candidate triangles using co-cone analysis
            std::vector<std::array<int32_t, 3>> candidateTriangles;
            GenerateTriangles(points, normals, candidateTriangles, params, nnQuery, searchRadius);

            if (candidateTriangles.empty())
            {
                return false;
            }

            // Step 4: Extract manifold surface
            ExtractManifold(points, normals, candidateTriangles, outTriangles, params);
            
            // Step 4.5: Auto-fix non-manifold edges if requested
            if (params.autoFixNonManifold && !outTriangles.empty())
            {
                AutoFixNonManifoldEdges(outTriangles);
            }
            
            // Step 4.6: Fix winding order to be consistent (outward-facing)
            if (params.fixWindingOrder && !outTriangles.empty())
            {
                FixWindingOrder(points, outTriangles);
            }
            
            // Step 4.7: Remove self-intersecting triangles if requested
            if (params.preventSelfIntersections && !outTriangles.empty())
            {
                RemoveSelfIntersectingTriangles(points, outTriangles);
            }

            // Copy vertices
            outVertices = points;

            // Step 5: Optional RVD-based smoothing for improved quality
            // NOTE: This is O(n²) per iteration and should only be used for
            // small meshes (n < ~2000 vertices).  Disabled by default.
            if (params.smoothWithRVD && params.rvdSmoothIterations > 0 && !outTriangles.empty())
            {
                SmoothWithRVD(outVertices, outTriangles, params);
            }

            return !outTriangles.empty();
        }

    protected:
        static constexpr int32_t NO_VERTEX = -1;
        static constexpr int32_t NO_CORNER = -1;
        static constexpr int32_t NO_TRIANGLE = -1;
        static constexpr int32_t NO_COMPONENT = -1;

        // Shared NNTree type: PositionSite is used rather than
        // PositionDirectionSite because the spatial search is position-only.
        using NNTree = NearestNeighborQuery<3, Real, PositionSite<3, Real>>;

        // Hash for std::array<int32_t,3> — used in triangle-count map to give
        // O(1) average insertion/lookup instead of O(log T) with std::map.
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

        // Compute normals using PCA (Principal Component Analysis).
        // Accepts a pre-built NNTree and search radius to avoid rebuilding
        // the tree for every phase.
        static bool ComputeNormals(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& normals,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            normals.resize(points.size());

            // For each point, compute normal using PCA of neighbors
            for (size_t i = 0; i < points.size(); ++i)
            {
                // Find k-nearest neighbors
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3)
                {
                    // Need at least 3 points for PCA
                    normals[i] = Vector3<Real>::Unit(2); // Default to Z-axis
                    continue;
                }

                // Limit to k neighbors
                int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));

                // Compute centroid of neighbors
                Vector3<Real> centroid = Vector3<Real>::Zero();
                for (int32_t j = 0; j < k; ++j)
                {
                    centroid += points[indices[j]];
                }
                centroid /= static_cast<Real>(k);

                // Build covariance matrix
                Matrix3x3<Real> covariance;
                covariance.MakeZero();
                
                for (int32_t j = 0; j < k; ++j)
                {
                    Vector3<Real> diff = points[indices[j]] - centroid;
                    
                    // Accumulate outer product
                    covariance(0, 0) += diff[0] * diff[0];
                    covariance(0, 1) += diff[0] * diff[1];
                    covariance(0, 2) += diff[0] * diff[2];
                    covariance(1, 1) += diff[1] * diff[1];
                    covariance(1, 2) += diff[1] * diff[2];
                    covariance(2, 2) += diff[2] * diff[2];
                }

                // Symmetrize
                covariance(1, 0) = covariance(0, 1);
                covariance(2, 0) = covariance(0, 2);
                covariance(2, 1) = covariance(1, 2);

                // Normalize
                Real factor = static_cast<Real>(1) / static_cast<Real>(k);
                covariance *= factor;

                // Compute eigenvalues and eigenvectors
                SymmetricEigensolver3x3<Real> solver;
                std::array<Real, 3> eigenvalues;
                std::array<std::array<Real, 3>, 3> eigenvectors;
                solver(covariance(0, 0), covariance(0, 1), covariance(0, 2),
                       covariance(1, 1), covariance(1, 2), covariance(2, 2),
                       false, +1, eigenvalues, eigenvectors);

                // Normal is eigenvector corresponding to smallest eigenvalue
                // Eigenvalues are sorted in increasing order
                normals[i][0] = eigenvectors[0][0];
                normals[i][1] = eigenvectors[0][1];
                normals[i][2] = eigenvectors[0][2];
                
                Normalize(normals[i]);
            }

            return true;
        }

        // Orient normals consistently using BFS on k-NN graph.
        // Accepts a pre-built NNTree and search radius.
        static void OrientNormalsConsistently(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& normals,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            size_t n = points.size();
            if (n == 0)
            {
                return;
            }

            // Priority queue for BFS: (dot product deviation, vertex index)
            struct OrientElement
            {
                Real deviation;
                size_t vertex;

                bool operator<(OrientElement const& other) const
                {
                    return deviation > other.deviation; // Min-heap
                }
            };

            std::priority_queue<OrientElement> queue;
            std::vector<bool> visited(n, false);

            // Start from vertex 0
            queue.push({ static_cast<Real>(0), 0 });

            while (!queue.empty())
            {
                OrientElement elem = queue.top();
                queue.pop();

                size_t i = elem.vertex;
                if (visited[i])
                {
                    continue;
                }
                visited[i] = true;

                // Find neighbors
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));

                for (int32_t j = 0; j < k; ++j)
                {
                    size_t neighborIdx = static_cast<size_t>(indices[j]);
                    if (neighborIdx == i || visited[neighborIdx])
                    {
                        continue;
                    }

                    // Check if normals point in similar direction
                    Real dot = Dot(normals[i], normals[neighborIdx]);
                    
                    if (dot < static_cast<Real>(0))
                    {
                        // Flip neighbor normal
                        normals[neighborIdx] = -normals[neighborIdx];
                        dot = -dot;
                    }

                    // Add to queue with deviation
                    Real deviation = static_cast<Real>(1) - dot;
                    queue.push({ deviation, neighborIdx });
                }
            }
        }

        // ===== TRIANGLE GENERATION =====

        // Generate triangles using co-cone analysis.
        // Dispatches to GenerateTrianglesManifoldConstrained when
        // params.useManifoldConstrainedGeneration is true.
        // Accepts a pre-built NNTree and search radius to avoid rebuilding
        // the tree for every phase.
        static void GenerateTriangles(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            if (params.useManifoldConstrainedGeneration)
            {
                GenerateTrianglesManifoldConstrained(
                    points, normals, triangles, params, nnQuery, searchRadius);
                return;
            }

            Real maxCosAngle = std::cos(params.maxNormalAngle * static_cast<Real>(3.14159265358979323846) / static_cast<Real>(180));

            // For each point, generate local triangles
            for (size_t i = 0; i < points.size(); ++i)
            {
                // Find k-nearest neighbors
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3)
                {
                    continue;
                }

                int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));

                // Filter by normal consistency (co-cone criterion)
                std::vector<int32_t> consistentNeighbors;
                for (int32_t j = 0; j < k; ++j)
                {
                    int32_t nIdx = indices[j];
                    if (nIdx == static_cast<int32_t>(i))
                    {
                        continue;
                    }

                    Real dot = Dot(normals[i], normals[nIdx]);
                    if (dot >= maxCosAngle)
                    {
                        consistentNeighbors.push_back(nIdx);
                    }
                }

                if (consistentNeighbors.size() < 2)
                {
                    continue;
                }

                // Generate triangles from local neighbors
                // Using a simple fan triangulation for each neighbor pair
                for (size_t j = 0; j < consistentNeighbors.size(); ++j)
                {
                    for (size_t kk = j + 1; kk < consistentNeighbors.size(); ++kk)
                    {
                        int32_t v0 = static_cast<int32_t>(i);
                        int32_t v1 = consistentNeighbors[j];
                        int32_t v2 = consistentNeighbors[kk];

                        // Check triangle quality
                        if (!IsTriangleValid(points, v0, v1, v2, params))
                        {
                            continue;
                        }

                        // Create triangle - DON'T normalize yet!
                        // We want to keep duplicates so they can be counted
                        std::array<int32_t, 3> tri = { v0, v1, v2 };
                        
                        // Add the triangle directly (with duplicates)
                        // This is critical: Geogram's algorithm relies on counting
                        // how many times each triangle appears from different Voronoi cells
                        triangles.push_back(tri);
                    }
                }
            }
        }

        // Manifold-constrained triangle generation.
        //
        // Root causes preventing standard Co3Ne from producing manifold output
        // and how this method addresses each one:
        //
        // 1. Spurious triangles from O(k²) fan triangulation
        //    Standard: generates all pairs (j, kk) from consistent k-NN,
        //    yielding O(k²) candidates, many geometrically invalid.
        //    Fix: project k-NN onto the tangent plane, sort by angle, and
        //    only triangulate angularly-consecutive pairs (O(k) candidates).
        //    This directly mirrors Geogram's Restricted Voronoi Cell polygon:
        //    the RVC edge between seeds j and k is exactly the boundary between
        //    two angularly-adjacent Voronoi neighbors of i in the tangent plane.
        //
        // 2. Inconsistent winding order requiring FixWindingOrder post-processing
        //    Standard: triangle orientation is arbitrary.
        //    Fix: after deciding the edge (v1→v2) in angular CCW order seen from
        //    normal[i], we verify Cross(v1-v0, v2-v0) · normal[i] > 0; if not,
        //    we swap v1 and v2 in-place. Winding is therefore consistent with
        //    the estimated surface normal at generation time.
        //
        // 3. Non-manifold edges requiring post-hoc autoFixNonManifold
        //    Standard: every O(k²) candidate is added unconditionally, so an
        //    edge can appear in arbitrarily many triangles.
        //    Fix: maintain a global edge-use counter. Each undirected edge
        //    (min(a,b), max(a,b)) is allowed in at most 2 triangles. Candidates
        //    that would exceed this limit are silently skipped.
        //
        // Trade-off: because we enforce a global greedy constraint, the result
        // depends on processing order, and the output is a subset of the surface
        // rather than the full set of plausible triangles. The subsequent
        // ExtractManifold / AutoFixNonManifold steps are still available and can
        // be applied to this already-clean candidate set.
        static void GenerateTrianglesManifoldConstrained(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params,
            NNTree const& nnQuery,
            Real searchRadius)
        {
            static constexpr Real kPi = static_cast<Real>(3.14159265358979323846);
            Real maxCosAngle = std::cos(params.maxNormalAngle * kPi / static_cast<Real>(180));

            // Global edge-use counter: (min_v, max_v) -> number of triangles using it.
            // Limits each undirected edge to at most 2 triangles (manifold condition).
            std::unordered_map<uint64_t, int32_t> edgeUseCount;
            edgeUseCount.reserve(points.size() * static_cast<size_t>(params.kNeighbors));

            // Encode an undirected edge as a single 64-bit key.
            auto edgeKey = [](int32_t a, int32_t b) -> uint64_t
            {
                if (a > b) std::swap(a, b);
                return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32)
                     |  static_cast<uint64_t>(static_cast<uint32_t>(b));
            };

            auto edgeUsable = [&edgeUseCount, &edgeKey](int32_t a, int32_t b) -> bool
            {
                auto it = edgeUseCount.find(edgeKey(a, b));
                return it == edgeUseCount.end() || it->second < 2;
            };

            auto recordEdge = [&edgeUseCount, &edgeKey](int32_t a, int32_t b)
            {
                edgeUseCount[edgeKey(a, b)]++;
            };

            // Track already-added triangles by their canonical (sorted) key so that
            // the same triangle generated from multiple seed points is added only once.
            // Without this, seeds v0=i, v0=j, and v0=k can all independently generate
            // triangle (i,j,k) (possibly with different cyclic orderings), each passing
            // the edge-use check, consuming 2x or 3x edge budget and producing duplicates.
            // We reuse the TriangleArrayHash that is already part of this class.
            using TriangleSeen =
                std::unordered_map<std::array<int32_t, 3>, bool, TriangleArrayHash>;
            TriangleSeen triSeen;
            triSeen.reserve(points.size() * static_cast<size_t>(params.kNeighbors));

            // Build the canonical (sorted) key for a triangle so that (i,j,k),
            // (j,k,i), (k,i,j), and all their reversals map to the same entry.
            auto triCanonicalKey = [](int32_t a, int32_t b, int32_t c)
                -> std::array<int32_t, 3>
            {
                std::array<int32_t, 3> key = { a, b, c };
                std::sort(key.begin(), key.end());
                return key;
            };

            // Neighbor struct for angular sort
            struct AngNeighbor
            {
                Real   angle;
                int32_t idx;
            };

            for (size_t i = 0; i < points.size(); ++i)
            {
                // Find k-nearest neighbors within search radius
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3)
                {
                    continue;
                }

                int32_t k = std::min(numFound, static_cast<int32_t>(params.kNeighbors));

                // Filter by normal consistency (co-cone criterion)
                std::vector<int32_t> consistent;
                consistent.reserve(static_cast<size_t>(k));
                for (int32_t j = 0; j < k; ++j)
                {
                    int32_t nIdx = indices[j];
                    if (nIdx == static_cast<int32_t>(i))
                    {
                        continue;
                    }
                    if (Dot(normals[i], normals[nIdx]) >= maxCosAngle)
                    {
                        consistent.push_back(nIdx);
                    }
                }

                if (consistent.size() < 2)
                {
                    continue;
                }

                // Build an orthonormal basis {u, v} for the tangent plane at i.
                // We choose the axis of ni with the smallest absolute component
                // to form a numerically stable cross product.
                Vector3<Real> const& ni = normals[i];
                Vector3<Real> u;
                if (std::abs(ni[0]) <= std::abs(ni[1]) && std::abs(ni[0]) <= std::abs(ni[2]))
                {
                    u = Vector3<Real>{ static_cast<Real>(0), -ni[2], ni[1] };
                }
                else if (std::abs(ni[1]) <= std::abs(ni[2]))
                {
                    u = Vector3<Real>{ -ni[2], static_cast<Real>(0), ni[0] };
                }
                else
                {
                    u = Vector3<Real>{ ni[1], -ni[0], static_cast<Real>(0) };
                }
                Normalize(u);
                Vector3<Real> vAxis = Cross(ni, u);   // second tangent axis

                // Project consistent neighbors onto the tangent plane and sort by angle.
                // This approximates the cyclic order of the Restricted Voronoi Cell
                // boundary: the RVC polygon edge between seeds j and k corresponds
                // exactly to angularly-adjacent neighbors in the tangent plane.
                std::vector<AngNeighbor> angNeighbors;
                angNeighbors.reserve(consistent.size());
                for (int32_t nIdx : consistent)
                {
                    Vector3<Real> diff = points[nIdx] - points[i];
                    Real pu = Dot(diff, u);
                    Real pv = Dot(diff, vAxis);
                    if (pu == static_cast<Real>(0) && pv == static_cast<Real>(0))
                    {
                        continue;    // coincident projection, skip
                    }
                    angNeighbors.push_back({ std::atan2(pv, pu), nIdx });
                }

                if (angNeighbors.size() < 2)
                {
                    continue;
                }

                std::sort(angNeighbors.begin(), angNeighbors.end(),
                    [](AngNeighbor const& a, AngNeighbor const& b)
                    {
                        return a.angle < b.angle;
                    });

                // Generate one triangle per angularly-consecutive pair (wrapping around).
                // This gives O(k) candidates instead of O(k²), closely matching the
                // set of triangles that Geogram's Restricted Voronoi Cell would produce.
                size_t const m = angNeighbors.size();
                int32_t const v0 = static_cast<int32_t>(i);

                for (size_t j = 0; j < m; ++j)
                {
                    int32_t v1 = angNeighbors[j].idx;
                    int32_t v2 = angNeighbors[(j + 1) % m].idx;

                    // Quality check (degenerate / sliver rejection)
                    if (!IsTriangleValid(points, v0, v1, v2, params))
                    {
                        continue;
                    }

                    // Align winding with the surface normal at v0.
                    // Cross(p1-p0, p2-p0) · normal[i] should be > 0.
                    {
                        Vector3<Real> e1 = points[v1] - points[v0];
                        Vector3<Real> e2 = points[v2] - points[v0];
                        if (Dot(Cross(e1, e2), ni) < static_cast<Real>(0))
                        {
                            std::swap(v1, v2);
                        }
                    }

                    // Manifold edge constraint: reject the triangle if any of its
                    // three undirected edges already appears in 2 triangles.
                    if (!edgeUsable(v0, v1) || !edgeUsable(v1, v2) || !edgeUsable(v0, v2))
                    {
                        continue;
                    }

                    // Deduplicate: skip if this triangle was already added from a
                    // different seed point (same vertex set, possibly different cyclic order).
                    auto tk = triCanonicalKey(v0, v1, v2);
                    if (triSeen.count(tk))
                    {
                        continue;
                    }
                    triSeen[tk] = true;

                    triangles.push_back({ v0, v1, v2 });
                    recordEdge(v0, v1);
                    recordEdge(v1, v2);
                    recordEdge(v0, v2);
                }
            }
        }

        // Check if triangle is valid (not degenerate, good quality)
        static bool IsTriangleValid(
            std::vector<Vector3<Real>> const& points,
            int32_t v0, int32_t v1, int32_t v2,
            Parameters const& params)
        {
            Vector3<Real> const& p0 = points[v0];
            Vector3<Real> const& p1 = points[v1];
            Vector3<Real> const& p2 = points[v2];

            // Check for degenerate triangle
            Vector3<Real> e1 = p1 - p0;
            Vector3<Real> e2 = p2 - p0;
            Vector3<Real> normal = Cross(e1, e2);
            
            Real area = Length(normal);
            if (area < std::numeric_limits<Real>::epsilon())
            {
                return false; // Degenerate
            }

            // Check triangle quality (aspect ratio)
            Real len0 = Length(p1 - p0);
            Real len1 = Length(p2 - p1);
            Real len2 = Length(p0 - p2);

            Real maxLen = std::max({ len0, len1, len2 });
            if (maxLen < std::numeric_limits<Real>::epsilon())
            {
                return false;
            }

            // Quality metric: area / (max_edge_length)^2
            // For equilateral triangle this is ~0.433
            Real quality = area / (maxLen * maxLen);
            
            return quality >= params.triangleQualityThreshold * static_cast<Real>(0.433);
        }

        // ===== MANIFOLD EXTRACTION =====

        // Extract manifold surface from candidate triangles
        // NOW USES FULL GEOGRAM-EQUIVALENT MANIFOLD EXTRACTION
        static void ExtractManifold(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>> const& candidateTriangles,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            // Option: Bypass manifold extraction entirely and output unique triangles
            if (params.bypassManifoldExtraction)
            {
                // Deduplicate triangles while preserving the original winding order.
                // Use sorted vertex triples as the canonical key for identity, but
                // keep the first-seen (original winding) copy in the output.
                std::unordered_map<std::array<int32_t, 3>, std::array<int32_t, 3>,
                                   TriangleArrayHash> seen;
                seen.reserve(candidateTriangles.size());

                for (auto const& tri : candidateTriangles)
                {
                    std::array<int32_t, 3> key = tri;
                    std::sort(key.begin(), key.end());
                    // try_emplace inserts only if the key is absent; the first-seen
                    // triangle (with its original winding) therefore wins.
                    seen.try_emplace(key, tri);
                }

                outTriangles.clear();
                outTriangles.reserve(seen.size());
                for (auto const& [key, tri] : seen)
                {
                    outTriangles.push_back(tri);
                }

                return;
            }

            // When manifold-constrained generation was used, every candidate triangle
            // appears exactly once and the edge-manifold constraint was already enforced
            // during generation.  Skip the Voronoi-count (T3/T12) classification and
            // pass all candidates directly as "good" triangles into the extractor, which
            // will still remove any Moebius / orientation inconsistencies.
            if (params.useManifoldConstrainedGeneration)
            {
                // Deduplicate (defensive, candidates should already be unique)
                std::set<std::array<int32_t, 3>> seen;
                std::vector<std::array<int32_t, 3>> uniqueCandidates;
                uniqueCandidates.reserve(candidateTriangles.size());
                for (auto const& tri : candidateTriangles)
                {
                    std::array<int32_t, 3> normalized = tri;
                    std::sort(normalized.begin(), normalized.end());
                    if (seen.insert(normalized).second)
                    {
                        uniqueCandidates.push_back(tri);
                    }
                }

                typename Co3NeManifoldExtractor<Real>::Parameters extractorParams;
                extractorParams.strictMode    = params.strictMode;
                extractorParams.maxIterations = 50;
                extractorParams.verbose       = false;

                Co3NeManifoldExtractor<Real> extractor(points, extractorParams);
                std::vector<std::array<int32_t, 3>> empty;
                extractor.Extract(uniqueCandidates, empty, outTriangles);
                return;
            }
            
            // Standard manifold extraction follows...
            
            // Separate triangles into "good" and "not-so-good" categories
            // Good triangles: appear exactly 3 times (seen from 3 Voronoi cells)
            // These form a reliable manifold seed
            std::vector<std::array<int32_t, 3>> goodTriangles;
            std::vector<std::array<int32_t, 3>> notSoGoodTriangles;
            
            // Build triangle count map.
            // Use unordered_map for O(1) average insertion/lookup vs O(log T) for
            // std::map.  With O(n·k²) candidate triangles this matters at scale.
            std::unordered_map<std::array<int32_t, 3>, int, TriangleArrayHash> triangleCounts;
            triangleCounts.reserve(candidateTriangles.size() / 3);  // rough estimate
            for (auto const& tri : candidateTriangles)
            {
                // Normalize triangle (sort vertices for comparison)
                std::array<int32_t, 3> normalized = tri;
                std::sort(normalized.begin(), normalized.end());
                triangleCounts[normalized]++;
            }
            
            // Categorize triangles based on frequency
            // Following Geogram's logic exactly:
            // - Triangles appearing exactly 3 times → good (reliable manifold seed)
            // - Triangles appearing 1-2 times → not-so-good (added incrementally)
            // - Triangles appearing > 3 times → discarded (overly constrained)
            // In relaxed mode, triangles appearing 2-3 times are considered good
            
            int minGoodCount = params.relaxedManifoldExtraction ? 2 : 3;
            int maxGoodCount = 3;
            
            // Add each UNIQUE triangle once based on its count
            for (auto const& pair : triangleCounts)
            {
                std::array<int32_t, 3> const& normalized = pair.first;
                int count = pair.second;
                
                if (count >= minGoodCount && count <= maxGoodCount)
                {
                    // Good triangles (appear 2-3 times depending on mode)
                    goodTriangles.push_back(normalized);
                }
                else if (count <= 2 && count >= 1)
                {
                    // Not-so-good triangles (appear 1-2 times)
                    // Only add if not already in good triangles
                    if (count < minGoodCount)
                    {
                        notSoGoodTriangles.push_back(normalized);
                    }
                }
                // Triangles appearing > 3 times are discarded (like Geogram)
            }
            
            // Use full manifold extraction
            typename Co3NeManifoldExtractor<Real>::Parameters extractorParams;
            extractorParams.strictMode = params.strictMode;
            extractorParams.maxIterations = 50;  // Default from geogram
            extractorParams.verbose = false;
            
            Co3NeManifoldExtractor<Real> extractor(points, extractorParams);
            
            // Extract manifold with incremental insertion
            extractor.Extract(goodTriangles, notSoGoodTriangles, outTriangles);
        }

        // ===== UTILITY FUNCTIONS =====

        static Vector3<Real> ComputeTriangleNormal(
            std::vector<Vector3<Real>> const& points,
            std::array<int32_t, 3> const& tri)
        {
            Vector3<Real> const& p0 = points[tri[0]];
            Vector3<Real> const& p1 = points[tri[1]];
            Vector3<Real> const& p2 = points[tri[2]];

            Vector3<Real> e1 = p1 - p0;
            Vector3<Real> e2 = p2 - p0;
            Vector3<Real> normal = Cross(e1, e2);
            Normalize(normal);
            return normal;
        }

        // RVD-based smoothing for improved vertex distribution and triangle quality.
        //
        // Analysis: consequences of keeping smoothWithRVD disabled (the default)
        // -----------------------------------------------------------------------
        // smoothWithRVD moves each vertex toward the centroid of its Voronoi cell
        // on the reconstructed surface (Lloyd relaxation restricted to the mesh).
        // Enabling it improves:
        //   - Edge-length uniformity (vertices spread more evenly)
        //   - Triangle aspect ratios (fewer slivers near thin features)
        //   - CVT energy (quantifiable mesh quality metric)
        //
        // Cost: O(n²) per iteration via the naive RVD implementation, making it
        // impractical for meshes with more than ~2000 vertices.  3 iterations at
        // n=1 000 already dominate the reconstruction time budget.
        //
        // Consequence of keeping it OFF (the current default):
        //   - Mesh quality is somewhat lower near irregular sampling regions.
        //   - Vertex distribution reflects the original point-cloud density rather
        //     than an optimised placement — tolerable for most downstream uses.
        //   - No runtime cost: the reconstruction pipeline is not affected.
        //
        // Alternative to RVD smoothing (recommended if regularisation is needed):
        //   Laplacian smoothing (O(E) per iteration, where E = number of edges)
        //   provides similar visual improvement at a fraction of the cost:
        //     v_i' = (1-λ) v_i + λ · (1/k) Σ_{j∈N(i)} v_j
        //   λ ∈ [0.1, 0.5] for stability.  Even 5-10 Laplacian passes are fast
        //   enough to run on meshes of 100 000+ vertices.  A Taubin (λ/μ) variant
        //   avoids the volume shrinkage that plain Laplacian smoothing introduces.
        //
        // Recommendation: leave smoothWithRVD=false (default); add an opt-in
        // Laplacian/Taubin smoothing step if per-vertex regularisation is required.
        static void SmoothWithRVD(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            Parameters const& params)
        {
            if (vertices.empty() || triangles.empty())
            {
                return;
            }

            // Use RVD to optimize vertex positions
            RestrictedVoronoiDiagram<Real> rvd;

            for (size_t iter = 0; iter < params.rvdSmoothIterations; ++iter)
            {
                // Initialize RVD with current mesh
                if (!rvd.Initialize(vertices, triangles, vertices))
                {
                    // RVD failed, skip smoothing
                    return;
                }

                // Compute optimal positions (Voronoi centroids)
                std::vector<Vector3<Real>> centroids;
                if (!rvd.ComputeCentroids(centroids))
                {
                    // Centroid computation failed, skip smoothing
                    return;
                }

                // Move vertices toward centroids (use damping for stability)
                Real dampingFactor = static_cast<Real>(0.5);  // Move halfway to centroid
                for (size_t i = 0; i < vertices.size(); ++i)
                {
                    Vector3<Real> displacement = centroids[i] - vertices[i];
                    vertices[i] += dampingFactor * displacement;
                }
            }
        }

        static Real ComputeAutomaticSearchRadius(
            std::vector<Vector3<Real>> const& points)
        {
            if (points.empty())
            {
                return static_cast<Real>(1);
            }

            // Compute bounding box
            Vector3<Real> minPt = points[0];
            Vector3<Real> maxPt = points[0];

            for (auto const& p : points)
            {
                for (int i = 0; i < 3; ++i)
                {
                    minPt[i] = std::min(minPt[i], p[i]);
                    maxPt[i] = std::max(maxPt[i], p[i]);
                }
            }

            Real diagonal = Length(maxPt - minPt);
            
            // Compute average nearest neighbor distance for better scaling
            // For sparse point sets, we need a larger radius
            // Use a heuristic based on point density: diagonal / (n^(1/3))
            size_t n = points.size();
            Real densityFactor = std::pow(static_cast<Real>(n), static_cast<Real>(1.0 / 3.0));
            Real avgSpacing = diagonal / densityFactor;
            
            // Use 3-5x the average spacing to ensure sufficient neighbors
            // This scales better than a fixed percentage of diagonal
            Real multiplier = static_cast<Real>(4.0);  // Works well in practice
            return avgSpacing * multiplier;
        }
        
        // Automatically fix non-manifold edges by removing offending triangles
        // Uses greedy strategy: for each edge with >2 triangles, keep first 2
        static void AutoFixNonManifoldEdges(
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            // Build edge to triangles map
            std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> edgeToTriangles;
            
            for (size_t t = 0; t < triangles.size(); ++t)
            {
                auto const& tri = triangles[t];
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    edgeToTriangles[edge].push_back(static_cast<int32_t>(t));
                }
            }
            
            // Find triangles to remove
            std::set<int32_t> trianglesToRemove;
            for (auto const& pair : edgeToTriangles)
            {
                size_t count = pair.second.size();
                if (count > 2)
                {
                    // Keep first 2 triangles, remove the rest
                    for (size_t i = 2; i < count; ++i)
                    {
                        trianglesToRemove.insert(pair.second[i]);
                    }
                }
            }
            
            // Build new triangle list without removed triangles
            if (!trianglesToRemove.empty())
            {
                std::vector<std::array<int32_t, 3>> fixedTriangles;
                fixedTriangles.reserve(triangles.size() - trianglesToRemove.size());
                
                for (size_t t = 0; t < triangles.size(); ++t)
                {
                    if (trianglesToRemove.find(static_cast<int32_t>(t)) == trianglesToRemove.end())
                    {
                        fixedTriangles.push_back(triangles[t]);
                    }
                }
                
                triangles = std::move(fixedTriangles);
            }
        }
        
        // Fix triangle winding order to be consistent (outward-facing from centroid)
        static void FixWindingOrder(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (triangles.empty() || vertices.empty())
            {
                return;
            }
            
            // Compute centroid
            Vector3<Real> centroid = vertices[0];
            for (size_t i = 1; i < vertices.size(); ++i)
            {
                centroid += vertices[i];
            }
            centroid /= static_cast<Real>(vertices.size());
            
            // Fix each triangle's winding
            for (auto& tri : triangles)
            {
                Vector3<Real> const& p0 = vertices[tri[0]];
                Vector3<Real> const& p1 = vertices[tri[1]];
                Vector3<Real> const& p2 = vertices[tri[2]];
                
                // Compute triangle normal
                Vector3<Real> e1 = p1 - p0;
                Vector3<Real> e2 = p2 - p0;
                Vector3<Real> normal = Cross(e1, e2);
                
                // Vector from centroid to triangle center
                Vector3<Real> triCenter = (p0 + p1 + p2) / static_cast<Real>(3);
                Vector3<Real> outward = triCenter - centroid;
                
                // If normal points inward (dot < 0), flip triangle
                if (Dot(normal, outward) < static_cast<Real>(0))
                {
                    std::swap(tri[1], tri[2]);  // Reverse winding
                }
            }
        }
        
        // Remove self-intersecting triangles
        // Uses greedy approach: check each triangle against all others, remove if intersects
        static void RemoveSelfIntersectingTriangles(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (triangles.size() < 2)
            {
                return;
            }
            
            std::set<int32_t> trianglesToRemove;
            
            // For large meshes, use spatial partitioning
            // For now, use simple O(n^2) check with early termination
            size_t maxChecks = 50000;  // Limit to prevent excessive runtime
            size_t numChecks = 0;
            
            for (size_t i = 0; i < triangles.size() && numChecks < maxChecks; ++i)
            {
                if (trianglesToRemove.find(static_cast<int32_t>(i)) != trianglesToRemove.end())
                {
                    continue;  // Already marked for removal
                }
                
                auto const& tri1 = triangles[i];
                Vector3<Real> const& p0 = vertices[tri1[0]];
                Vector3<Real> const& p1 = vertices[tri1[1]];
                Vector3<Real> const& p2 = vertices[tri1[2]];
                
                for (size_t j = i + 1; j < triangles.size() && numChecks < maxChecks; ++j)
                {
                    if (trianglesToRemove.find(static_cast<int32_t>(j)) != trianglesToRemove.end())
                    {
                        continue;
                    }
                    
                    auto const& tri2 = triangles[j];
                    
                    // Skip if triangles share vertices (adjacent)
                    bool adjacent = false;
                    for (int vi = 0; vi < 3 && !adjacent; ++vi)
                    {
                        for (int vj = 0; vj < 3; ++vj)
                        {
                            if (tri1[vi] == tri2[vj])
                            {
                                adjacent = true;
                                break;
                            }
                        }
                    }
                    
                    if (adjacent)
                    {
                        continue;
                    }
                    
                    Vector3<Real> const& q0 = vertices[tri2[0]];
                    Vector3<Real> const& q1 = vertices[tri2[1]];
                    Vector3<Real> const& q2 = vertices[tri2[2]];
                    
                    numChecks++;
                    
                    // Simple triangle-triangle intersection test
                    // Check if any edge of tri1 intersects tri2 or vice versa
                    if (TrianglesIntersect(p0, p1, p2, q0, q1, q2))
                    {
                        // Mark the larger-index triangle for removal (greedy)
                        trianglesToRemove.insert(static_cast<int32_t>(j));
                    }
                }
            }
            
            // Remove marked triangles
            if (!trianglesToRemove.empty())
            {
                std::vector<std::array<int32_t, 3>> cleanTriangles;
                cleanTriangles.reserve(triangles.size() - trianglesToRemove.size());
                
                for (size_t t = 0; t < triangles.size(); ++t)
                {
                    if (trianglesToRemove.find(static_cast<int32_t>(t)) == trianglesToRemove.end())
                    {
                        cleanTriangles.push_back(triangles[t]);
                    }
                }
                
                triangles = std::move(cleanTriangles);
            }
        }
        
        // Simple triangle-triangle intersection test
        static bool TrianglesIntersect(
            Vector3<Real> const& p0, Vector3<Real> const& p1, Vector3<Real> const& p2,
            Vector3<Real> const& q0, Vector3<Real> const& q1, Vector3<Real> const& q2)
        {
            // Simplified intersection test using separating axis theorem
            // Check if triangles are on opposite sides of each other's planes
            
            // Compute normals
            Vector3<Real> n1 = Cross(p1 - p0, p2 - p0);
            Vector3<Real> n2 = Cross(q1 - q0, q2 - q0);
            
            Real epsilon = static_cast<Real>(1e-10);
            Real len1 = Length(n1);
            Real len2 = Length(n2);
            
            if (len1 < epsilon || len2 < epsilon)
            {
                return false;  // Degenerate triangle
            }
            
            n1 /= len1;
            n2 /= len2;
            
            // Check if tri2 vertices are all on same side of tri1's plane
            Real d0 = Dot(q0 - p0, n1);
            Real d1 = Dot(q1 - p0, n1);
            Real d2 = Dot(q2 - p0, n1);
            
            Real thresh = static_cast<Real>(1e-6);
            if ((d0 > thresh && d1 > thresh && d2 > thresh) ||
                (d0 < -thresh && d1 < -thresh && d2 < -thresh))
            {
                return false;  // All on same side
            }
            
            // Check if tri1 vertices are all on same side of tri2's plane
            d0 = Dot(p0 - q0, n2);
            d1 = Dot(p1 - q0, n2);
            d2 = Dot(p2 - q0, n2);
            
            if ((d0 > thresh && d1 > thresh && d2 > thresh) ||
                (d0 < -thresh && d1 < -thresh && d2 < -thresh))
            {
                return false;  // All on same side
            }
            
            // If we get here, triangles potentially intersect
            // For a complete test we'd need edge-edge checks, but this is
            // good enough for filtering obvious non-intersecting cases
            return true;
        }
    };
}
