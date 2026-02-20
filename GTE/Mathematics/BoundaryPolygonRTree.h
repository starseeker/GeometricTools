// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.20
//
// BoundaryPolygonRTree
//
// A standalone spatial index for boundary polygon edges, providing efficient
// overlap queries between two boundary polygon RTrees without BRL-CAD
// dependencies.
//
// Inspired by the RTree.h Overlaps() concept from the project repository, this
// class implements an endpoint-indexed uniform 3D grid that achieves correct
// locality detection regardless of edge length:
//
//   1. Build(): index each edge by BOTH its endpoints (not midpoint).
//   2. Overlaps(): for each edge A[i], search the OTHER tree's cells near
//      each endpoint of A[i] with cellRadius = ceil(maxDist/cellSize)+1.
//      Any B[j] with an endpoint within maxDist of any endpoint of A[i] is
//      guaranteed to be found without any half-edge-length expansion.
//
// This is used to accelerate component gap-finding (bridging) from the naive
// O(B_i × B_j) per component-pair to near-linear in the number of close pairs.
//
// Design note: endpoint-based insertion (rather than midpoint-based) is
// essential for correctness when edges are longer than the query distance.
// With midpoint indexing a long edge's endpoint can be close to a remote
// edge's endpoint even though their midpoints are far apart, requiring a
// prohibitively large cell-search radius.

#ifndef GTE_MATHEMATICS_BOUNDARY_POLYGON_RTREE_H
#define GTE_MATHEMATICS_BOUNDARY_POLYGON_RTREE_H

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gte
{
    // Spatial index for a single boundary polygon's edges.
    // Internally uses a uniform 3D grid indexed by edge endpoints so that
    // nearby-edge queries are O(1) average, independent of edge length.
    template <typename Real>
    class BoundaryPolygonRTree
    {
    public:
        using EdgeType = std::pair<int32_t, int32_t>;

        BoundaryPolygonRTree() = default;

        // Build the spatial index from a set of boundary edges and the mesh
        // vertices.
        // cellSizeHint: if > 0, used as the grid cell size; otherwise it is
        //               auto-derived from the bounding box diagonal.
        void Build(
            std::vector<EdgeType> const& edges,
            std::vector<Vector3<Real>> const& vertices,
            Real cellSizeHint = static_cast<Real>(0))
        {
            mEdges = edges;
            mGrid.clear();

            if (edges.empty())
            {
                return;
            }

            // Compute bounding box over all edge endpoints
            Vector3<Real> bboxMin = vertices[edges[0].first];
            Vector3<Real> bboxMax = bboxMin;

            for (auto const& e : edges)
            {
                for (int32_t vi : {e.first, e.second})
                {
                    for (int k = 0; k < 3; ++k)
                    {
                        bboxMin[k] = std::min(bboxMin[k], vertices[vi][k]);
                        bboxMax[k] = std::max(bboxMax[k], vertices[vi][k]);
                    }
                }
            }

            // Determine cell size
            if (cellSizeHint > static_cast<Real>(0))
            {
                mCellSize = cellSizeHint;
            }
            else
            {
                Real diag = Length(bboxMax - bboxMin);
                mCellSize = (diag > static_cast<Real>(0))
                    ? diag / static_cast<Real>(8)
                    : static_cast<Real>(1);
            }

            mBBoxMin = bboxMin;

            // Insert each edge by BOTH its endpoints.
            // This guarantees that any edge with an endpoint in a given cell
            // neighbourhood is findable by a query anchored at that cell,
            // regardless of how long the edge is.
            for (size_t i = 0; i < edges.size(); ++i)
            {
                GridCell c0 = CellOf(vertices[edges[i].first]);
                GridCell c1 = CellOf(vertices[edges[i].second]);
                mGrid[c0].push_back(static_cast<int32_t>(i));
                // Insert the second endpoint only if it maps to a different cell
                // to avoid storing the same edge index twice in the same cell.
                if (c1 != c0)
                {
                    mGrid[c1].push_back(static_cast<int32_t>(i));
                }
            }
        }

        // Find all edge pairs (indexInThis, indexInOther) where the minimum
        // vertex-to-vertex distance between the two edges is <= maxDist.
        //
        // Algorithm: for each edge A[i], search cells within cellRadius of
        // EACH endpoint of A[i] in OTHER's grid.  Because OTHER's grid is
        // indexed by endpoints, any B[j] that has an endpoint within maxDist
        // of any endpoint of A[i] will be found.
        //
        // Returns the number of candidate pairs found.
        size_t Overlaps(
            BoundaryPolygonRTree<Real> const& other,
            std::vector<Vector3<Real>> const& vertices,
            Real maxDist,
            std::set<std::pair<int32_t, int32_t>>& result) const
        {
            result.clear();

            if (mEdges.empty() || other.mEdges.empty())
            {
                return 0;
            }

            // cellRadius: number of cell steps needed to cover maxDist.
            // Since we query around each endpoint of A[i] and OTHER is indexed
            // by endpoints, this radius is sufficient to find any B[j] with an
            // endpoint within maxDist of any endpoint of A[i].
            int32_t cellRadius = static_cast<int32_t>(
                std::ceil(maxDist / other.mCellSize)) + 1;

            for (size_t i = 0; i < mEdges.size(); ++i)
            {
                // Search around BOTH endpoints of edge A[i]
                for (int32_t vi : {mEdges[i].first, mEdges[i].second})
                {
                    std::array<int32_t, 3> endpointCell =
                        other.CellOf(vertices[vi]);

                    for (int32_t dx = -cellRadius; dx <= cellRadius; ++dx)
                    {
                        for (int32_t dy = -cellRadius; dy <= cellRadius; ++dy)
                        {
                            for (int32_t dz = -cellRadius; dz <= cellRadius; ++dz)
                            {
                                std::array<int32_t, 3> neighborCell = {
                                    endpointCell[0] + dx,
                                    endpointCell[1] + dy,
                                    endpointCell[2] + dz
                                };

                                auto it = other.mGrid.find(neighborCell);
                                if (it == other.mGrid.end())
                                {
                                    continue;
                                }

                                for (int32_t j : it->second)
                                {
                                    // Ground-truth vertex-to-vertex check
                                    Real dist = MinEndpointDistance(
                                        mEdges[i], other.mEdges[j], vertices);
                                    if (dist <= maxDist)
                                    {
                                        result.insert(
                                            std::make_pair(
                                                static_cast<int32_t>(i), j));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return result.size();
        }

        std::vector<EdgeType> const& GetEdges() const { return mEdges; }

    private:
        using GridCell = std::array<int32_t, 3>;

        struct GridCellHash
        {
            size_t operator()(GridCell const& c) const noexcept
            {
                // Hash mixing for three integer coordinates (boost::hash_combine style)
                size_t h = static_cast<size_t>(c[0]);
                h ^= static_cast<size_t>(c[1]) * 2654435761ULL
                    + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                h ^= static_cast<size_t>(c[2]) * 2246822519ULL
                    + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                return h;
            }
        };

        // Map from grid cell to list of edge indices whose endpoints fall in
        // that cell.  Each edge appears at most twice (once per endpoint, only
        // if the two endpoints map to different cells).
        std::unordered_map<GridCell, std::vector<int32_t>, GridCellHash> mGrid;

        std::vector<EdgeType> mEdges;
        Vector3<Real> mBBoxMin{};
        Real mCellSize = static_cast<Real>(1);

        // Map a 3D point to its grid cell, anchored at mBBoxMin to keep
        // cell coordinates small regardless of mesh world-space position.
        GridCell CellOf(Vector3<Real> const& pt) const
        {
            return {
                static_cast<int32_t>(
                    std::floor((pt[0] - mBBoxMin[0]) / mCellSize)),
                static_cast<int32_t>(
                    std::floor((pt[1] - mBBoxMin[1]) / mCellSize)),
                static_cast<int32_t>(
                    std::floor((pt[2] - mBBoxMin[2]) / mCellSize))
            };
        }

        // Minimum distance between any pair of endpoints of two edges
        static Real MinEndpointDistance(
            EdgeType const& e1,
            EdgeType const& e2,
            std::vector<Vector3<Real>> const& vertices)
        {
            return std::min({
                Length(vertices[e1.first]  - vertices[e2.first]),
                Length(vertices[e1.first]  - vertices[e2.second]),
                Length(vertices[e1.second] - vertices[e2.first]),
                Length(vertices[e1.second] - vertices[e2.second])
            });
        }
    };
}

#endif // GTE_MATHEMATICS_BOUNDARY_POLYGON_RTREE_H
