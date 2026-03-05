// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.03.05
//
// LSCM (Least Squares Conformal Maps) boundary arc-length parameterization
//
// For hole triangulation in MeshHoleFilling: maps a 3D boundary loop to a
// 2D unit-circle polygon using arc-length parameterization.  A boundary
// sampled uniformly in arc-length always produces a valid (non-self-
// intersecting) convex polygon in UV space, regardless of how non-planar
// the original 3D loop is.  This is used as the second-to-last fallback
// in hole triangulation, between 2D best-fit-plane projection and 3D ear
// clipping:
//
//   1. EC   – project to best-fit plane, ear-clip in 2D          (fastest)
//   2. CDT  – project to best-fit plane, CDT in 2D
//   3. LSCM – arc-length circle map, triangulate in 2D           (this file)
//   4. 3D   – 3D ear clipping directly on the boundary           (last resort)
//
// The approach is derived from the boundary-pinning step of:
//   Lévy, Petitjean, Ray, Maillot, "Least Squares Conformal Maps for
//   Automatic Texture Atlas Generation", SIGGRAPH 2002.
// For a boundary-only polygon (no interior vertices) the LSCM energy
// minimisation collapses to the boundary constraint alone, so the result
// is exactly the arc-length circle map implemented here.

#pragma once

#include <GTE/Mathematics/Vector2.h>
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Constants.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gte
{
    template <typename Real>
    class LSCMParameterization
    {
    public:
        // Map a 3D boundary loop to 2D UV positions on a unit circle using
        // arc-length parameterization.
        //
        // Input:
        //   vertices3D     – 3D positions of ALL mesh vertices
        //   boundaryIndices – ordered vertex indices forming the closed boundary loop
        //
        // Output:
        //   uv – 2D UV coordinates for each boundary vertex (parallel to
        //        boundaryIndices).  The polygon inscribed on the circle is
        //        always simple (non-self-intersecting), so EC/CDT will succeed.
        //
        // Returns true on success.
        static bool MapBoundaryToCircle(
            std::vector<Vector3<Real>> const& vertices3D,
            std::vector<int32_t> const& boundaryIndices,
            std::vector<Vector2<Real>>& uv)
        {
            int32_t n = static_cast<int32_t>(boundaryIndices.size());
            if (n < 3)
            {
                return false;
            }

            // Compute cumulative arc lengths along the boundary.
            std::vector<Real> arcLen(n, static_cast<Real>(0));
            Real totalLen = static_cast<Real>(0);
            for (int32_t i = 0; i < n; ++i)
            {
                int32_t i0 = boundaryIndices[i];
                int32_t i1 = boundaryIndices[(i + 1) % n];
                Real edgeLen = Length(vertices3D[i1] - vertices3D[i0]);
                totalLen += edgeLen;
                arcLen[i] = totalLen;  // cumulative length *after* edge i
            }

            if (totalLen <= static_cast<Real>(0))
            {
                return false;
            }

            // Map each boundary vertex to the unit circle, with the first
            // vertex at angle 0.  Use cumulative length *before* each vertex,
            // so vertex 0 is at arc-fraction 0.
            Real const twoPi = static_cast<Real>(2) * static_cast<Real>(GTE_C_PI);
            uv.resize(n);
            uv[0] = Vector2<Real>{ static_cast<Real>(1), static_cast<Real>(0) };
            for (int32_t i = 1; i < n; ++i)
            {
                // Arc fraction of vertex i = cumulative length reaching vertex i
                // (i.e. the sum of edges 0..i-1) / totalLen
                Real fraction = arcLen[i - 1] / totalLen;
                Real angle = twoPi * fraction;
                uv[i] = Vector2<Real>{ std::cos(angle), std::sin(angle) };
            }
            return true;
        }
    };
}
