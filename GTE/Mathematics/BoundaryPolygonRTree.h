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
// overlap queries between two boundary polygon RTrees without BRL-CAD dependencies.
//
// Inspired by the RTree.h Overlaps() concept from the project repository, this
// class implements a grid-based spatial index that achieves similar locality
// detection:
//
//   1. Build an RTree for each boundary polygon's edges (Build()).
//   2. Query which edge pairs from two different polygons are spatially close
//      (Overlaps()), limiting distance checks to candidate pairs only.
//
// This is used to accelerate component gap finding (bridging) from the naive
// O(B_i × B_j) per component-pair to near-linear in the number of close pairs.

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
    // Internally uses a uniform 3D grid so that nearby-edge queries are O(1) average.
    template <typename Real>
    class BoundaryPolygonRTree
    {
    public:
        using EdgeType = std::pair<int32_t, int32_t>;

        BoundaryPolygonRTree() = default;

        // Build the spatial index from a set of boundary edges and the mesh vertices.
        // cellSizeHint: if > 0, used as the grid cell size; otherwise it is auto-derived
        //               from the average edge length so that each cell holds ~maxLeafEdges
        //               edges on average for a uniform distribution.
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

            // Compute midpoints and bounding box
            mMidpoints.resize(edges.size());
            Vector3<Real> bboxMin = vertices[edges[0].first];
            Vector3<Real> bboxMax = bboxMin;

            for (size_t i = 0; i < edges.size(); ++i)
            {
                Vector3<Real> const& v0 = vertices[edges[i].first];
                Vector3<Real> const& v1 = vertices[edges[i].second];
                mMidpoints[i] = (v0 + v1) * static_cast<Real>(0.5);

                for (int k = 0; k < 3; ++k)
                {
                    bboxMin[k] = std::min(bboxMin[k], std::min(v0[k], v1[k]));
                    bboxMax[k] = std::max(bboxMax[k], std::max(v0[k], v1[k]));
                }
            }

            // Determine cell size
            if (cellSizeHint > static_cast<Real>(0))
            {
                mCellSize = cellSizeHint;
            }
            else
            {
                // Auto-derive: use a fraction of the bounding box diagonal
                Real diag = Length(bboxMax - bboxMin);
                mCellSize = (diag > static_cast<Real>(0))
                    ? diag / static_cast<Real>(8)
                    : static_cast<Real>(1);
            }

            mBBoxMin = bboxMin;

            // Insert each edge into its grid cell
            for (size_t i = 0; i < edges.size(); ++i)
            {
                mGrid[CellOf(mMidpoints[i])].push_back(static_cast<int32_t>(i));
            }
        }

        // Find all edge pairs (indexInThis, indexInOther) where the two edges
        // are within maxDist of each other (measured between edge midpoints plus
        // an additional vertex-to-vertex check for precision).
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

            // Cell radius in OTHER's grid to cover maxDist
            // (use other's cell size for the radius calculation since we query other's grid)
            int32_t cellRadius = static_cast<int32_t>(
                std::ceil(maxDist / other.mCellSize)) + 1;

            for (size_t i = 0; i < mEdges.size(); ++i)
            {
                // Compute the cell of this edge's midpoint in OTHER's coordinate frame
                // so the neighbor-cell search is directly in other's grid.
                std::array<int32_t, 3> cellInOther = other.CellOf(mMidpoints[i]);

                // Search all cells within cellRadius in the OTHER tree's grid
                for (int32_t dx = -cellRadius; dx <= cellRadius; ++dx)
                {
                    for (int32_t dy = -cellRadius; dy <= cellRadius; ++dy)
                    {
                        for (int32_t dz = -cellRadius; dz <= cellRadius; ++dz)
                        {
                            std::array<int32_t, 3> neighborCell = {
                                cellInOther[0] + dx,
                                cellInOther[1] + dy,
                                cellInOther[2] + dz
                            };

                            auto it = other.mGrid.find(neighborCell);
                            if (it == other.mGrid.end())
                            {
                                continue;
                            }

                            for (int32_t j : it->second)
                            {
                                // Midpoint distance pre-filter
                                Real midDist = Length(
                                    mMidpoints[i] - other.mMidpoints[j]);
                                if (midDist > maxDist * static_cast<Real>(2))
                                {
                                    continue;
                                }

                                // Precise vertex-to-vertex distance check
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

            return result.size();
        }

        std::vector<EdgeType> const& GetEdges() const { return mEdges; }
        std::vector<Vector3<Real>> const& GetMidpoints() const
        {
            return mMidpoints;
        }

    private:
        using GridCell = std::array<int32_t, 3>;

        struct GridCellHash
        {
            size_t operator()(GridCell const& c) const noexcept
            {
                // Simple hash combining the three integer coordinates
                size_t h = static_cast<size_t>(c[0]);
                h ^= static_cast<size_t>(c[1]) * 2654435761ULL
                    + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                h ^= static_cast<size_t>(c[2]) * 2246822519ULL
                    + 0x9e3779b9ULL + (h << 6) + (h >> 2);
                return h;
            }
        };

        // Map from grid cell to list of edge indices in that cell
        // Uses unordered_map with GridCellHash for O(1) average lookup
        std::unordered_map<GridCell, std::vector<int32_t>, GridCellHash> mGrid;

        std::vector<EdgeType> mEdges;
        std::vector<Vector3<Real>> mMidpoints;
        Vector3<Real> mBBoxMin{};
        Real mCellSize = static_cast<Real>(1);

        // Map a 3D point to its grid cell, anchored at mBBoxMin to keep
        // cell coordinates small regardless of mesh world-space position.
        GridCell CellOf(Vector3<Real> const& pt) const
        {
            return {
                static_cast<int32_t>(std::floor((pt[0] - mBBoxMin[0]) / mCellSize)),
                static_cast<int32_t>(std::floor((pt[1] - mBBoxMin[1]) / mCellSize)),
                static_cast<int32_t>(std::floor((pt[2] - mBBoxMin[2]) / mCellSize))
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
