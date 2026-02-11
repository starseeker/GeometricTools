// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// N-dimensional nearest neighbor search
//
// Simple but efficient nearest neighbor search for N-dimensional points.
// Uses a basic spatial partitioning approach suitable for CVT operations.
//
// Based on:
// - geogram/src/lib/geogram/points/nn_search.h
// - GTE's NearestNeighborQuery.h (provides reference)
//
// License: Boost Software License 1.0

#pragma once

#include <GTE/Mathematics/Vector.h>
#include <GTE/Mathematics/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace gte
{
    // N-dimensional nearest neighbor search
    // Provides k-nearest neighbor queries for CVT operations
    template <typename Real, size_t N>
    class NearestNeighborSearchN
    {
    public:
        using PointN = Vector<N, Real>;
        
        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");
        
        NearestNeighborSearchN()
            : mNumPoints(0)
            , mPoints(nullptr)
        {
        }
        
        virtual ~NearestNeighborSearchN() = default;
        
        // Set the point set for searching
        void SetPoints(size_t numPoints, PointN const* points)
        {
            mNumPoints = numPoints;
            mPoints = points;
            
            // Build spatial data structure
            BuildIndex();
        }
        
        // Find single nearest neighbor to query point
        // Returns index of nearest point, or -1 if no points
        int32_t FindNearestNeighbor(PointN const& query) const
        {
            if (mNumPoints == 0)
            {
                return -1;
            }
            
            int32_t nearest = 0;
            Real minDistSq = DistanceSquared(query, mPoints[0]);
            
            for (size_t i = 1; i < mNumPoints; ++i)
            {
                Real distSq = DistanceSquared(query, mPoints[i]);
                if (distSq < minDistSq)
                {
                    minDistSq = distSq;
                    nearest = static_cast<int32_t>(i);
                }
            }
            
            return nearest;
        }
        
        // Find k nearest neighbors to query point
        // Returns actual number of neighbors found (may be less than k)
        size_t FindKNearestNeighbors(
            PointN const& query,
            size_t k,
            std::vector<int32_t>& neighbors,
            std::vector<Real>& distances) const
        {
            neighbors.clear();
            distances.clear();
            
            if (mNumPoints == 0 || k == 0)
            {
                return 0;
            }
            
            // Limit k to available points
            k = std::min(k, mNumPoints);
            
            // Use a max-heap to track k nearest neighbors
            using NeighborPair = std::pair<Real, int32_t>; // (distance_sq, index)
            std::priority_queue<NeighborPair> maxHeap;
            
            // Initial k points
            for (size_t i = 0; i < k; ++i)
            {
                Real distSq = DistanceSquared(query, mPoints[i]);
                maxHeap.push(NeighborPair(distSq, static_cast<int32_t>(i)));
            }
            
            // Check remaining points
            for (size_t i = k; i < mNumPoints; ++i)
            {
                Real distSq = DistanceSquared(query, mPoints[i]);
                if (distSq < maxHeap.top().first)
                {
                    maxHeap.pop();
                    maxHeap.push(NeighborPair(distSq, static_cast<int32_t>(i)));
                }
            }
            
            // Extract results (will be in reverse order initially)
            neighbors.resize(k);
            distances.resize(k);
            
            for (int32_t i = static_cast<int32_t>(k) - 1; i >= 0; --i)
            {
                neighbors[i] = maxHeap.top().second;
                distances[i] = std::sqrt(maxHeap.top().first);
                maxHeap.pop();
            }
            
            return k;
        }
        
        // Find k nearest neighbors to an existing point (by index)
        // Useful for computing neighborhoods in Delaunay
        size_t FindKNearestNeighborsToPoint(
            int32_t queryIndex,
            size_t k,
            std::vector<int32_t>& neighbors,
            std::vector<Real>& distances) const
        {
            if (queryIndex < 0 || static_cast<size_t>(queryIndex) >= mNumPoints)
            {
                neighbors.clear();
                distances.clear();
                return 0;
            }
            
            return FindKNearestNeighbors(mPoints[queryIndex], k + 1, neighbors, distances);
        }
        
        // Get number of points
        size_t GetNumPoints() const
        {
            return mNumPoints;
        }
        
        // Compute squared distance between two points
        static Real DistanceSquared(PointN const& p0, PointN const& p1)
        {
            Real sumSq = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real diff = p1[i] - p0[i];
                sumSq += diff * diff;
            }
            return sumSq;
        }
        
    private:
        // Build spatial index (currently just stores points, could be enhanced with KD-tree)
        void BuildIndex()
        {
            // For now, uses brute force search
            // Future enhancement: Implement KD-tree for N dimensions
            // This is sufficient for CVT with moderate point counts
        }
        
    private:
        size_t mNumPoints;
        PointN const* mPoints;
    };
}
