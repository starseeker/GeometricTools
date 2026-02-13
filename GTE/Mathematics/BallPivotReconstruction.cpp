// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Implementation of BallPivotReconstruction

#include <GTE/Mathematics/BallPivotReconstruction.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace gte
{
    template <typename Real>
    bool BallPivotReconstruction<Real>::Reconstruct(
        std::vector<Vector3<Real>> const& points,
        std::vector<Vector3<Real>> const& normals,
        std::vector<Vector3<Real>>& outVertices,
        std::vector<std::array<int32_t, 3>>& outTriangles,
        Parameters const& params)
    {
        if (points.empty() || normals.empty() || points.size() != normals.size())
        {
            return false;
        }
        
        // Initialize reconstruction state
        ReconstructionState state;
        InitializeVertices(points, normals, state);
        
        // Build nearest neighbor query structure
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites);
        
        // Compute average spacing for heuristics
        state.avgSpacing = ComputeAverageSpacingInternal(points, nnQuery);
        
        if (params.verbose)
        {
            std::cout << "[BallPivot] Starting reconstruction: " << points.size() 
                      << " points, avg spacing = " << state.avgSpacing << "\n";
        }
        
        // Determine radii to use
        std::vector<Real> radii = params.radii;
        if (radii.empty())
        {
            radii = EstimateRadii(points);
            if (params.verbose)
            {
                std::cout << "[BallPivot] Auto radii:";
                for (auto r : radii)
                {
                    std::cout << " " << r;
                }
                std::cout << "\n";
            }
        }
        
        // Process each radius in sequence
        for (auto radius : radii)
        {
            if (radius <= static_cast<Real>(0))
            {
                continue;
            }
            
            if (params.verbose)
            {
                std::cout << "[BallPivot] Processing radius = " << radius << "\n";
            }
            
            // Step 1: Update border edges that become valid at this radius
            UpdateBorderEdges(state, radius, nnQuery, params);
            
            // Step 2: Classify border loops and selectively reopen edges
            ClassifyAndReopenBorderLoops(state, radius, nnQuery, params);
            
            // Step 3: If no front edges, find a seed triangle
            if (state.frontEdges.empty())
            {
                FindSeed(state, radius, nnQuery, params);
            }
            else
            {
                // Step 4: Expand existing front
                ExpandFront(state, radius, nnQuery, params);
            }
            
            if (params.verbose)
            {
                std::cout << "[BallPivot] After radius " << radius << ": "
                          << state.outputTriangles.size() << " triangles, "
                          << state.frontEdges.size() << " front edges, "
                          << state.borderEdges.size() << " border edges\n";
            }
        }
        
        // Final step: Ensure consistent orientation
        FinalizeOrientation(state);
        
        // Copy output
        outVertices = points;  // Use original points as vertices
        outTriangles = state.outputTriangles;
        
        if (params.verbose)
        {
            std::cout << "[BallPivot] Reconstruction complete: " 
                      << outTriangles.size() << " triangles\n";
        }
        
        return !outTriangles.empty();
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::InitializeVertices(
        std::vector<Vector3<Real>> const& points,
        std::vector<Vector3<Real>> const& normals,
        ReconstructionState& state)
    {
        state.vertices.reserve(points.size());
        for (size_t i = 0; i < points.size(); ++i)
        {
            state.vertices.emplace_back(points[i], normals[i]);
        }
    }
    
    template <typename Real>
    Real BallPivotReconstruction<Real>::ComputeAverageSpacing(
        std::vector<Vector3<Real>> const& points)
    {
        if (points.size() < 2)
        {
            return static_cast<Real>(1);
        }
        
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites);
        return ComputeAverageSpacingInternal(points, nnQuery);
    }
    
    template <typename Real>
    Real BallPivotReconstruction<Real>::ComputeAverageSpacingInternal(
        std::vector<Vector3<Real>> const& points,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery)
    {
        if (points.size() < 2)
        {
            return static_cast<Real>(1);
        }
        
        Real totalDistance = static_cast<Real>(0);
        int32_t numSamples = 0;
        
        // Sample up to 1000 points to estimate spacing
        size_t step = std::max<size_t>(1, points.size() / 1000);
        
        for (size_t i = 0; i < points.size(); i += step)
        {
            // Find 2 nearest neighbors (self + 1 other)
            auto results = nnQuery.FindNearest(points[i], 2);
            
            if (results.size() >= 2)
            {
                // Distance to second nearest (first is self with distance 0)
                totalDistance += std::sqrt(results[1].distance);
                ++numSamples;
            }
        }
        
        return numSamples > 0 ? totalDistance / static_cast<Real>(numSamples) : static_cast<Real>(1);
    }
    
    template <typename Real>
    std::vector<Real> BallPivotReconstruction<Real>::EstimateRadii(
        std::vector<Vector3<Real>> const& points)
    {
        if (points.size() < 2)
        {
            return {static_cast<Real>(1.5)};
        }
        
        // Compute nearest neighbor distances
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites);
        
        std::vector<Real> distances;
        distances.reserve(points.size());
        
        for (auto const& p : points)
        {
            auto results = nnQuery.FindNearest(p, 2);
            if (results.size() >= 2)
            {
                distances.push_back(std::sqrt(results[1].distance));
            }
        }
        
        if (distances.empty())
        {
            return {static_cast<Real>(1.5)};
        }
        
        // Compute statistics
        std::sort(distances.begin(), distances.end());
        
        Real sum = static_cast<Real>(0);
        for (auto d : distances)
        {
            sum += d;
        }
        Real avg = sum / static_cast<Real>(distances.size());
        
        size_t medianIdx = distances.size() / 2;
        Real median = distances[medianIdx];
        
        size_t p90Idx = static_cast<size_t>(0.9 * (distances.size() - 1));
        Real p90 = distances[p90Idx];
        
        // Generate 3 radii based on statistics
        Real r0 = static_cast<Real>(0.9) * median;
        Real r1 = static_cast<Real>(1.2) * avg;
        Real r2 = std::min(static_cast<Real>(2.5) * avg, static_cast<Real>(1.1) * p90);
        
        // Ensure increasing order and positive values
        if (r1 <= r0)
        {
            r1 = r0 * static_cast<Real>(1.1);
        }
        if (r2 <= r1)
        {
            r2 = r1 * static_cast<Real>(1.1);
        }
        
        if (r0 <= static_cast<Real>(0))
        {
            r0 = avg * static_cast<Real>(0.8);
        }
        if (r1 <= static_cast<Real>(0))
        {
            r1 = avg * static_cast<Real>(1.2);
        }
        if (r2 <= static_cast<Real>(0))
        {
            r2 = avg * static_cast<Real>(2.0);
        }
        
        return {r0, r1, r2};
    }
    
    // Explicit template instantiations
    template class BallPivotReconstruction<float>;
    template class BallPivotReconstruction<double>;
}
