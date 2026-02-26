// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// 2D polygon validation helpers used by MeshHoleFilling to check whether a
// projected hole boundary self-intersects before attempting triangulation.

#pragma once

#include <Mathematics/Vector2.h>
#include <cmath>
#include <vector>

namespace gte
{
    template <typename Real>
    class Polygon2Validation
    {
    public:
        // Returns true if any two non-adjacent edges of the polygon cross in
        // their interior (endpoint touches are not counted as intersections).
        // Polygons with fewer than 4 vertices cannot self-intersect.
        static bool HasSelfIntersectingEdges(
            std::vector<Vector2<Real>> const& polygon)
        {
            size_t n = polygon.size();
            if (n < 4)
            {
                return false;
            }

            for (size_t i = 0; i < n; ++i)
            {
                size_t iNext = (i + 1) % n;
                Vector2<Real> const& p1 = polygon[i];
                Vector2<Real> const& p2 = polygon[iNext];

                // Compare with all edges that are not adjacent to edge (i, iNext).
                for (size_t j = i + 2; j < n; ++j)
                {
                    // Skip the wrap-around adjacent edge.
                    if (j == i || (j + 1) % n == i)
                    {
                        continue;
                    }

                    size_t jNext = (j + 1) % n;
                    Vector2<Real> const& p3 = polygon[j];
                    Vector2<Real> const& p4 = polygon[jNext];

                    if (EdgesIntersect(p1, p2, p3, p4))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        // Returns true if the interior of segment (p1,p2) crosses the interior
        // of segment (p3,p4) (strict intersection; shared endpoints are ignored).
        static bool EdgesIntersect(
            Vector2<Real> const& p1, Vector2<Real> const& p2,
            Vector2<Real> const& p3, Vector2<Real> const& p4)
        {
            Vector2<Real> d1  = p2 - p1;
            Vector2<Real> d2  = p4 - p3;
            Vector2<Real> d3  = p3 - p1;

            Real denom = Cross2D(d1, d2);

            // Parallel or coincident edges are not considered intersecting.
            if (std::abs(denom) < static_cast<Real>(1e-10))
            {
                return false;
            }

            Real t = Cross2D(d3, d2) / denom;
            Real u = Cross2D(d3, d1) / denom;

            // Strict interior intersection: both parameters must be in (0, 1).
            return (t > static_cast<Real>(0) && t < static_cast<Real>(1) &&
                    u > static_cast<Real>(0) && u < static_cast<Real>(1));
        }

    private:
        // Scalar 2D cross product (z-component of the 3D cross product).
        static Real Cross2D(
            Vector2<Real> const& v1,
            Vector2<Real> const& v2)
        {
            return v1[0] * v2[1] - v1[1] * v2[0];
        }
    };
}
