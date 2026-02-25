// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Ball Pivoting Algorithm - GTE Wrapper
//
// This wraps the BallPivot implementation (ported from Open3D) to provide
// a GTE-style interface for surface reconstruction from point clouds.
//
// The Ball Pivoting Algorithm (BPA) reconstructs surfaces by "rolling" a ball
// of given radius over the point cloud, creating triangles as the ball pivots
// around edges.
//
// Original implementation: Open3D (MIT License)
// Port to BRL-CAD: BallPivot.hpp
// This wrapper: Boost Software License

#ifndef GTE_MATHEMATICS_BALL_PIVOT_WRAPPER_H
#define GTE_MATHEMATICS_BALL_PIVOT_WRAPPER_H

#include <GTE/Mathematics/Vector3.h>
#include <array>
#include <cstdint>
#include <vector>

// Forward declare to avoid including BallPivot.hpp in header
namespace bpiv
{
    class BallPivot;
}

namespace gte
{
    template <typename Real>
    class BallPivotWrapper
    {
    public:
        struct Parameters
        {
            std::vector<Real> radii;    // Ball radii to try (auto-computed if empty)
            bool verbose;                // Enable debug output
            
            Parameters()
                : verbose(false)
            {
            }
        };
        
        // Reconstruct surface from point cloud using ball pivoting
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters());
        
        // Estimate good radii for a point cloud
        static std::vector<Real> EstimateRadii(
            std::vector<Vector3<Real>> const& points);
        
        // Compute average nearest-neighbor spacing
        static Real ComputeAverageSpacing(
            std::vector<Vector3<Real>> const& points);
    };
}

#endif // GTE_MATHEMATICS_BALL_PIVOT_WRAPPER_H
