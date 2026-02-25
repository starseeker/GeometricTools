// Helper functions for 2D polygon validation

#pragma once

#include <GTE/Mathematics/Vector2.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace gte
{
    template <typename Real>
    class Polygon2Validation
    {
    public:
        // Check if a 2D polygon has self-intersecting edges
        static bool HasSelfIntersectingEdges(std::vector<Vector2<Real>> const& polygon)
        {
            size_t n = polygon.size();
            if (n < 4) return false;  // Need at least 4 vertices to self-intersect
            
            // Check all pairs of non-adjacent edges
            for (size_t i = 0; i < n; ++i)
            {
                size_t i_next = (i + 1) % n;
                Vector2<Real> const& p1 = polygon[i];
                Vector2<Real> const& p2 = polygon[i_next];
                
                // Check against all non-adjacent edges
                for (size_t j = i + 2; j < n; ++j)
                {
                    // Skip if edges are adjacent
                    if (j == i || j == i_next || (j + 1) % n == i)
                        continue;
                    
                    size_t j_next = (j + 1) % n;
                    Vector2<Real> const& p3 = polygon[j];
                    Vector2<Real> const& p4 = polygon[j_next];
                    
                    if (EdgesIntersect(p1, p2, p3, p4))
                    {
                        return true;
                    }
                }
            }
            
            return false;
        }
        
        // Check if two line segments intersect (excluding endpoints)
        static bool EdgesIntersect(
            Vector2<Real> const& p1, Vector2<Real> const& p2,
            Vector2<Real> const& p3, Vector2<Real> const& p4)
        {
            // Compute direction vectors
            Vector2<Real> d1 = p2 - p1;
            Vector2<Real> d2 = p4 - p3;
            Vector2<Real> d3 = p3 - p1;
            
            // Compute cross products for orientation tests
            Real cross1 = Cross2D(d1, d2);
            
            // Parallel or coincident
            if (std::abs(cross1) < static_cast<Real>(1e-10))
            {
                return false;  // Consider parallel edges as non-intersecting
            }
            
            // Compute parameters
            Real t = Cross2D(d3, d2) / cross1;
            Real u = Cross2D(d3, d1) / cross1;
            
            // Check if intersection is in the interior of both segments
            // Use strict inequalities to exclude endpoint intersections
            if (t > static_cast<Real>(0) && t < static_cast<Real>(1) &&
                u > static_cast<Real>(0) && u < static_cast<Real>(1))
            {
                return true;
            }
            
            return false;
        }
        
    private:
        // 2D cross product (returns z-component of 3D cross product)
        static Real Cross2D(Vector2<Real> const& v1, Vector2<Real> const& v2)
        {
            return v1[0] * v2[1] - v1[1] * v2[0];
        }
    };
}
