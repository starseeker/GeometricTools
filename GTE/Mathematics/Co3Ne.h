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
            // Minimum triangle quality (0-1). Computed as the ratio of the triangle's
            // area to the area of an equilateral triangle with the same longest edge.
            // Effectively filters very thin "sliver" triangles (quality < 0.3 * 0.433).
            Real triangleQualityThreshold;
            bool orientNormals;             // Orient normals consistently via propagation
            bool strictMode;                // Strict manifold extraction (more conservative)
            // Post-process with RVD-based smoothing (improves quality).
            // NOTE: O(n²) per iteration.  Disabled by default.  Only use for
            // small meshes (n < ~2000 vertices).  See SmoothWithRVD for details.
            bool smoothWithRVD;
            size_t rvdSmoothIterations;     // Number of RVD smoothing iterations
            
            Parameters()
                : kNeighbors(20)
                , searchRadius(static_cast<Real>(0))
                , maxNormalAngle(static_cast<Real>(60))
                , triangleQualityThreshold(static_cast<Real>(0.3))
                , orientNormals(true)
                , strictMode(false)
                , smoothWithRVD(false)
                , rvdSmoothIterations(3)
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

            // Copy vertices
            outVertices = points;

            // Step 5: Optional RVD-based smoothing for improved quality
            // NOTE: O(n²) per iteration; disabled by default.  See SmoothWithRVD.
            if (params.smoothWithRVD && params.rvdSmoothIterations > 0 && !outTriangles.empty())
            {
                SmoothWithRVD(outVertices, outTriangles, params);
            }

            return !outTriangles.empty();
        }

        // Reconstruction using caller-supplied precomputed normals.
        // Useful when normals are already available (e.g., from an XYZ file
        // with 6 columns: x y z nx ny nz) and PCA estimation is unnecessary.
        // Step 1 (ComputeNormals) is always skipped; the provided normals are
        // used directly.
        //
        // NOTE on params.orientNormals:
        //   If the provided normals are already consistently oriented (e.g.
        //   scanner normals all pointing outward), set orientNormals = false
        //   to trust them as-is.  Calling OrientNormalsConsistently on an
        //   already-correct normal field can introduce flips near thin features
        //   where cross-surface neighbors are geometrically close: the BFS
        //   propagation may flip normals of points on the opposite face to
        //   "agree" with their nearest neighbor, turning a correct anti-aligned
        //   pair into a falsely consistent one and degrading triangle generation.
        //   The default (orientNormals = true) is kept for backward compatibility but
        //   callers with reliable scanner normals should pass false.
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

            // Use the caller-supplied normals directly (skip PCA estimation).
            std::vector<Vector3<Real>> workNormals = normals;

            // Optionally re-orient normals for consistency.
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

            ExtractManifold(points, workNormals, candidateTriangles, outTriangles, params);

            outVertices = points;

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

            std::vector<bool> visited(n, false);

            // Outer loop handles disconnected components: restart from any
            // unvisited vertex, matching Geogram's reorient_normals logic.
            for (size_t seed = 0; seed < n; ++seed)
            {
                if (visited[seed])
                {
                    continue;
                }

                std::priority_queue<OrientElement> queue;
                // Mark the seed as visited *before* pushing to prevent
                // it from being pushed again by a neighbour.
                visited[seed] = true;
                queue.push({ static_cast<Real>(0), seed });

                while (!queue.empty())
                {
                    OrientElement elem = queue.top();
                    queue.pop();

                    size_t i = elem.vertex;

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

                        // Mark visited before pushing so a vertex can only
                        // be pushed once, preventing double-flip.
                        visited[neighborIdx] = true;

                        // Check if normals point in similar direction
                        Real dot = Dot(normals[i], normals[neighborIdx]);

                        if (dot < static_cast<Real>(0))
                        {
                            // Flip at push time here is safe because visited
                            // ensures this neighbour is enqueued only once.
                            normals[neighborIdx] = -normals[neighborIdx];
                            dot = -dot;
                        }

                        // Add to queue with deviation
                        Real deviation = static_cast<Real>(1) - dot;
                        queue.push({ deviation, neighborIdx });
                    }
                }
            }
        }

        // ===== TRIANGLE GENERATION =====

        // Generate triangles using the real Geogram Co3Ne algorithm:
        //   For each seed i, collect up to k normal-consistent neighbors
        //   (scanning all found neighbors, not just the first k), then
        //   propose every pair as a candidate triangle — O(k²) per seed.
        //   ExtractManifold applies T3/T12 classification to these raw
        //   proposals: triangles seen from all 3 vertex perspectives (T3)
        //   seed the manifold first; T12 triangles fill gaps.
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
            GenerateTrianglesManifoldConstrained(
                points, normals, triangles, params, nnQuery, searchRadius);
        }

        // Real Geogram Co3Ne triangle generation.
        //
        // For each seed point i:
        //   1. Find all neighbors within the search radius (up to MaxNeighbors).
        //   2. Scan the FULL found list and collect up to kNeighbors neighbors
        //      whose surface normal agrees with normal[i] within maxNormalAngle
        //      (co-cone criterion).  Scanning the full list is essential for
        //      geometry with thin features: geometrically close cross-surface
        //      points are filtered by the normal test, so we must look past
        //      them to collect k valid same-surface neighbors.
        //   3. Propose every PAIR from the consistent set as a candidate
        //      triangle (v0=i, v1=j, v2=kk) — O(k²) proposals per seed.
        //      Degenerate / sliver triangles are discarded via IsTriangleValid.
        //
        // The resulting raw candidate list contains duplicates: triangle
        // (i,j,k) is proposed once each when i, j, and k are used as the
        // seed.  ExtractManifold classifies proposals by count:
        //   count == 3  →  T3  ("good"   — all three vertices agree)
        //   count <  3  →  T12 ("not-so-good" — one or two vertices agree)
        //   count >  3  →  discarded (too many proposals, unreliable)
        // T3 triangles seed the manifold mesh; T12 fill gaps.  This is the
        // classification scheme from the original Geogram Co3Ne paper.
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

            int32_t const kTarget = static_cast<int32_t>(params.kNeighbors);

            for (size_t i = 0; i < points.size(); ++i)
            {
                // Step 1: find all neighbors within the search radius.
                constexpr int32_t MaxNeighbors = 100;
                std::array<int32_t, MaxNeighbors> indices;
                int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                    points[i], searchRadius, indices);

                if (numFound < 3)
                {
                    continue;
                }

                // Step 2: collect up to kTarget CONSISTENT (co-cone) neighbors
                // by scanning the full found list rather than just the first k.
                // This avoids under-sampling same-surface neighbors when
                // cross-surface captures appear among the geometrically closest
                // points (common for thin features such as bunny ears).
                std::vector<int32_t> consistent;
                consistent.reserve(static_cast<size_t>(kTarget));
                for (int32_t j = 0; j < numFound && static_cast<int32_t>(consistent.size()) < kTarget; ++j)
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

                int32_t const v0 = static_cast<int32_t>(i);

                // Step 3: propose every pair — O(k²) candidates per seed.
                for (size_t j = 0; j < consistent.size(); ++j)
                {
                    for (size_t kk = j + 1; kk < consistent.size(); ++kk)
                    {
                        int32_t v1 = consistent[j];
                        int32_t v2 = consistent[kk];

                        // Reject degenerate / sliver triangles early.
                        if (!IsTriangleValid(points, v0, v1, v2, params))
                        {
                            continue;
                        }

                        // Align winding with normal[i] so proposals from
                        // different seeds use consistent orientation for the
                        // same triangle.  The manifold extractor also
                        // re-orients during connectivity building, so this
                        // is a soft hint rather than a hard requirement.
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

        // Extract manifold surface from the raw O(k²) candidate triangles
        // produced by GenerateTrianglesManifoldConstrained.
        //
        // Each triangle (i,j,k) appears in the candidate list once for every
        // seed vertex that proposed it.  Geogram's T3/T12 classification counts
        // these appearances after normalizing the vertex order:
        //
        //   count == 3  →  T3  ("good")        all three vertices agree
        //   count <  3  →  T12 ("not-so-good") one or two vertices agree
        //   count >  3  →  discarded           over-proposed, unreliable
        //
        // For each canonical triangle we keep the FIRST oriented (winding-
        // aligned) proposal seen.  The manifold extractor will re-orient any
        // inconsistent patches via its own BFS step, so this is merely a hint.
        //
        // T3 triangles seed Co3NeManifoldExtractor; T12 triangles are added
        // incrementally wherever the manifold can safely accommodate them.
        static void ExtractManifold(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<std::array<int32_t, 3>> const& candidateTriangles,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            (void)normals;   // not used here; kept for API symmetry

            // Count how many seeds proposed each canonical triangle, while
            // retaining the first winding-aligned oriented version seen.
            struct TriCount
            {
                int                     count   = 0;
                std::array<int32_t, 3>  oriented{};
            };
            std::unordered_map<std::array<int32_t, 3>, TriCount, TriangleArrayHash> triangleCounts;
            triangleCounts.reserve(candidateTriangles.size() / 3 + 1);
            for (auto const& tri : candidateTriangles)
            {
                std::array<int32_t, 3> normalized = tri;
                std::sort(normalized.begin(), normalized.end());
                auto& info = triangleCounts[normalized];
                if (info.count == 0)
                {
                    info.oriented = tri;   // save first winding-aligned proposal
                }
                info.count++;
            }

            // Classify into T3 (good) and T12 (not-so-good).
            std::vector<std::array<int32_t, 3>> goodTriangles;
            std::vector<std::array<int32_t, 3>> notSoGoodTriangles;
            goodTriangles.reserve(triangleCounts.size() / 4 + 1);
            notSoGoodTriangles.reserve(triangleCounts.size());

            for (auto const& [normalized, info] : triangleCounts)
            {
                if (info.count == 3)
                {
                    goodTriangles.push_back(info.oriented);
                }
                else if (info.count < 3)
                {
                    notSoGoodTriangles.push_back(info.oriented);
                }
                // count > 3: discard
            }

            // Graceful degradation: if no T3 triangles were found (e.g. when
            // normals have inconsistent orientation and mutual k-NN agreement
            // is low), promote T12 triangles to seeds so the extractor still
            // produces output.
            if (goodTriangles.empty())
            {
                goodTriangles.swap(notSoGoodTriangles);
            }

            typename Co3NeManifoldExtractor<Real>::Parameters extractorParams;
            extractorParams.strictMode    = params.strictMode;
            extractorParams.maxIterations = 50;
            extractorParams.verbose       = false;

            Co3NeManifoldExtractor<Real> extractor(points, extractorParams);
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

            // Use 5% of the bounding-box diagonal, matching Geogram / BRL-CAD.
            return static_cast<Real>(0.05) * diagonal;
        }
    };
}
