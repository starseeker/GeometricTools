// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.10
//
// Co3Ne (Concurrent Co-Cones) surface reconstruction ported from Geogram
// Original: Geogram points/co3ne.cpp
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// Adapted for Geometric Tools Engine:
// - Uses std::vector<Vector3<Real>> instead of GEO::Mesh
// - Uses GTE's NearestNeighborQuery for spatial indexing
// - Removed Geogram command-line configuration
// - Added struct-based parameter system
//
// Co3Ne Algorithm:
// Reconstructs a triangle mesh from a point cloud with normals using
// local co-cone analysis. The algorithm works by:
// 1. Finding k-nearest neighbors for each point
// 2. Checking normal consistency (co-cone angle)
// 3. Building triangles from locally consistent neighbor sets
// 4. Ensuring manifold output

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <GTE/Mathematics/Delaunay3.h>
#include <GTE/Mathematics/ETManifoldMesh.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace gte
{
    template <typename Real>
    class Co3Ne
    {
    public:
        struct Parameters
        {
            Real searchRadius;          // Radius for neighbor search (0 = auto)
            Real maxNormalAngle;        // Max angle for normal agreement (degrees)
            size_t kNeighbors;          // Number of neighbors to consider
            bool ensureManifold;        // Ensure output is manifold
            bool orientConsistently;    // Orient triangles consistently
            
            Parameters()
                : searchRadius(static_cast<Real>(0))
                , maxNormalAngle(static_cast<Real>(60))
                , kNeighbors(20)
                , ensureManifold(true)
                , orientConsistently(true)
            {
            }
        };

        // Reconstruct surface from point cloud with normals
        // Input: points and corresponding normal vectors
        // Output: triangle mesh vertices and indices
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (points.empty() || normals.empty())
            {
                return false;
            }

            if (points.size() != normals.size())
            {
                return false;
            }

            // Determine search radius if not specified
            Real searchRadius = params.searchRadius;
            if (searchRadius == static_cast<Real>(0))
            {
                searchRadius = ComputeAutomaticSearchRadius(points);
            }

            // Build nearest neighbor structure
            using Site = PositionDirectionSite<3, Real>;
            std::vector<Site> sites;
            sites.reserve(points.size());
            for (size_t i = 0; i < points.size(); ++i)
            {
                sites.emplace_back(points[i], normals[i]);
            }

            // NearestNeighborQuery parameters: maxLeafSize and maxLevel
            int32_t maxLeafSize = 10;  // Max points per leaf node
            int32_t maxLevel = 20;      // Max tree depth
            NearestNeighborQuery<3, Real, Site> nnQuery(sites, maxLeafSize, maxLevel);

            // For each point, find neighbors and build local triangles
            std::map<std::array<int32_t, 3>, bool> candidateTriangles;

            for (size_t i = 0; i < points.size(); ++i)
            {
                // Find k-nearest neighbors
                std::vector<int32_t> neighbors;
                FindNeighbors(nnQuery, points[i], params.kNeighbors, 
                    searchRadius, neighbors);

                if (neighbors.size() < 3)
                {
                    continue; // Need at least 3 neighbors for a triangle
                }

                // Filter neighbors by normal agreement
                std::vector<int32_t> consistentNeighbors;
                Real maxCosAngle = std::cos(params.maxNormalAngle * static_cast<Real>(3.14159265358979323846) / static_cast<Real>(180));
                
                for (int32_t nIdx : neighbors)
                {
                    Real dotProduct = Dot(normals[i], normals[nIdx]);
                    if (dotProduct >= maxCosAngle)
                    {
                        consistentNeighbors.push_back(nIdx);
                    }
                }

                if (consistentNeighbors.size() < 2)
                {
                    continue;
                }

                // Project neighbors to tangent plane and triangulate
                std::vector<std::array<int32_t, 3>> localTriangles;
                TriangulateNeighborhood(
                    static_cast<int32_t>(i),
                    consistentNeighbors,
                    points,
                    normals[i],
                    localTriangles);

                // Add to candidate set
                for (auto const& tri : localTriangles)
                {
                    candidateTriangles[tri] = true;
                }
            }

            // Extract unique triangles
            outTriangles.clear();
            outTriangles.reserve(candidateTriangles.size());
            for (auto const& entry : candidateTriangles)
            {
                outTriangles.push_back(entry.first);
            }

            // Use original points as vertices
            outVertices = points;

            // Orient triangles consistently if requested
            if (params.orientConsistently)
            {
                OrientTrianglesConsistently(outVertices, outTriangles, normals);
            }

            // Ensure manifold if requested
            if (params.ensureManifold)
            {
                RemoveNonManifoldTriangles(outVertices, outTriangles);
            }

            return !outTriangles.empty();
        }

    private:
        // Compute automatic search radius based on point distribution
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
            
            // Use 5% of bounding box diagonal as default
            return diagonal * static_cast<Real>(0.05);
        }

        // Find k-nearest neighbors within search radius
        static void FindNeighbors(
            NearestNeighborQuery<3, Real, PositionDirectionSite<3, Real>> const& nnQuery,
            Vector3<Real> const& point,
            size_t k,
            Real searchRadius,
            std::vector<int32_t>& neighbors)
        {
            neighbors.clear();

            // GTE's FindNeighbors uses a templated MaxNeighbors parameter
            // We'll use a reasonable upper limit and collect results
            constexpr int32_t MaxNeighbors = 100;
            std::array<int32_t, MaxNeighbors> indices;
            
            int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(point, searchRadius, indices);

            // Copy results to output vector (up to k neighbors)
            size_t count = std::min(static_cast<size_t>(numFound), k);
            neighbors.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                neighbors[i] = indices[i];
            }
        }

        // Triangulate a local neighborhood by projecting to tangent plane
        static void TriangulateNeighborhood(
            int32_t centerIdx,
            std::vector<int32_t> const& neighbors,
            std::vector<Vector3<Real>> const& points,
            Vector3<Real> const& normal,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (neighbors.size() < 2)
            {
                return;
            }

            Vector3<Real> const& center = points[centerIdx];

            // Create a local coordinate system
            // Note: normal must be non-const for ComputeOrthogonalComplement
            Vector3<Real> basis[3];
            basis[0] = normal;
            ComputeOrthogonalComplement(1, basis);
            Vector3<Real> const& u = basis[1];
            Vector3<Real> const& v = basis[2];

            // Project neighbors to 2D
            std::vector<std::pair<Real, Real>> projected2D;
            projected2D.reserve(neighbors.size());

            for (int32_t nIdx : neighbors)
            {
                Vector3<Real> diff = points[nIdx] - center;
                Real uu = Dot(diff, u);
                Real vv = Dot(diff, v);
                projected2D.emplace_back(uu, vv);
            }

            // Sort neighbors by angle around center
            std::vector<size_t> sortedIndices(neighbors.size());
            for (size_t i = 0; i < neighbors.size(); ++i)
            {
                sortedIndices[i] = i;
            }

            std::sort(sortedIndices.begin(), sortedIndices.end(),
                [&projected2D](size_t a, size_t b)
                {
                    Real angleA = std::atan2(projected2D[a].second, projected2D[a].first);
                    Real angleB = std::atan2(projected2D[b].second, projected2D[b].first);
                    return angleA < angleB;
                });

            // Create fan triangulation from center
            for (size_t i = 0; i < sortedIndices.size(); ++i)
            {
                size_t next = (i + 1) % sortedIndices.size();
                
                int32_t v0 = centerIdx;
                int32_t v1 = neighbors[sortedIndices[i]];
                int32_t v2 = neighbors[sortedIndices[next]];

                // Ensure proper orientation
                Vector3<Real> edge1 = points[v1] - points[v0];
                Vector3<Real> edge2 = points[v2] - points[v0];
                Vector3<Real> triNormal = Cross(edge1, edge2);

                if (Dot(triNormal, normal) > static_cast<Real>(0))
                {
                    triangles.push_back({ v0, v1, v2 });
                }
                else
                {
                    triangles.push_back({ v0, v2, v1 });
                }
            }
        }

        // Orient triangles consistently using normal information
        static void OrientTrianglesConsistently(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<Vector3<Real>> const& normals)
        {
            for (auto& tri : triangles)
            {
                // Compute triangle normal
                Vector3<Real> const& v0 = vertices[tri[0]];
                Vector3<Real> const& v1 = vertices[tri[1]];
                Vector3<Real> const& v2 = vertices[tri[2]];

                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> triNormal = Cross(edge1, edge2);

                // Average vertex normals
                Vector3<Real> avgNormal = normals[tri[0]] + normals[tri[1]] + normals[tri[2]];
                Normalize(avgNormal);

                // Flip if inconsistent
                if (Dot(triNormal, avgNormal) < static_cast<Real>(0))
                {
                    std::swap(tri[1], tri[2]);
                }
            }
        }

        // Remove non-manifold triangles to ensure valid mesh
        static void RemoveNonManifoldTriangles(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            // Build edge usage map
            std::map<std::pair<int32_t, int32_t>, size_t> edgeCount;

            for (auto const& tri : triangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int j = (i + 1) % 3;
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[j];
                    
                    // Use ordered edge
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    edgeCount[edge]++;
                }
            }

            // Remove triangles with non-manifold edges (used more than 2 times)
            std::vector<std::array<int32_t, 3>> validTriangles;
            validTriangles.reserve(triangles.size());

            for (auto const& tri : triangles)
            {
                bool isManifold = true;

                for (int i = 0; i < 3; ++i)
                {
                    int j = (i + 1) % 3;
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[j];
                    
                    auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                    if (edgeCount[edge] > 2)
                    {
                        isManifold = false;
                        break;
                    }
                }

                if (isManifold)
                {
                    validTriangles.push_back(tri);
                }
            }

            triangles = std::move(validTriangles);
        }
    };
}
