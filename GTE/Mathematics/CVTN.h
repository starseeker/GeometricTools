// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Centroidal Voronoi Tessellation for N dimensions
//
// Implements Lloyd relaxation and Newton optimization for CVT in
// arbitrary dimensions. Main use case is anisotropic remeshing with
// 6D sites (position + scaled normal).
//
// Based on:
// - geogram/src/lib/geogram/voronoi/CVT.h (reference architecture)
// - RestrictedVoronoiDiagramN (centroid computation)
// - DelaunayNN (neighborhood structure)
//
// License: Boost Software License 1.0

#pragma once

#include <GTE/Mathematics/Vector.h>
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/DelaunayNN.h>
#include <GTE/Mathematics/RestrictedVoronoiDiagramN.h>
#include <GTE/Mathematics/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace gte
{
    // Centroidal Voronoi Tessellation for N dimensions
    //
    // Distributes sites evenly over a 3D surface mesh using N-dimensional
    // distance metric. The main algorithm is Lloyd relaxation, which
    // iteratively moves sites to centroids of their Voronoi cells.
    //
    // Use cases:
    // - Isotropic CVT: N=3, uniform distribution
    // - Anisotropic CVT: N=6, feature-aligned distribution
    //
    template <typename Real, size_t N>
    class CVTN
    {
    public:
        using PointN = Vector<N, Real>;
        using Point3 = Vector3<Real>;
        
        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");
        
        static_assert(N >= 3,
            "Dimension must be at least 3 (for 3D position).");
        
        // Constructor
        CVTN()
            : mConvergenceThreshold(static_cast<Real>(1e-6))
            , mVerbose(false)
        {
        }
        
        virtual ~CVTN() = default;
        
        // Initialize with surface mesh
        bool Initialize(
            std::vector<Point3> const& surfaceVertices,
            std::vector<std::array<int32_t, 3>> const& surfaceTriangles)
        {
            LogAssert(
                !surfaceVertices.empty() && !surfaceTriangles.empty(),
                "Surface mesh must not be empty.");
            
            mSurfaceVertices = surfaceVertices;
            mSurfaceTriangles = surfaceTriangles;
            
            return true;
        }
        
        // Compute initial random sampling on surface
        bool ComputeInitialSampling(size_t numSites, unsigned int seed = 12345)
        {
            if (mSurfaceVertices.empty() || mSurfaceTriangles.empty())
            {
                return false;
            }
            
            // Random number generator
            std::mt19937 rng(seed);
            std::uniform_real_distribution<Real> dist(static_cast<Real>(0), static_cast<Real>(1));
            
            mSites.clear();
            mSites.reserve(numSites);
            
            // Compute triangle areas for weighted sampling
            std::vector<Real> triangleAreas;
            Real totalArea = static_cast<Real>(0);
            
            for (auto const& tri : mSurfaceTriangles)
            {
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Real area = ComputeTriangleArea(v0, v1, v2);
                triangleAreas.push_back(area);
                totalArea += area;
            }
            
            // Generate random points
            for (size_t i = 0; i < numSites; ++i)
            {
                // Select triangle weighted by area
                Real r = dist(rng) * totalArea;
                Real sum = static_cast<Real>(0);
                size_t triIdx = 0;
                
                for (size_t j = 0; j < triangleAreas.size(); ++j)
                {
                    sum += triangleAreas[j];
                    if (sum >= r)
                    {
                        triIdx = j;
                        break;
                    }
                }
                
                // Random point in triangle
                auto const& tri = mSurfaceTriangles[triIdx];
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Real u = dist(rng);
                Real v = dist(rng);
                if (u + v > static_cast<Real>(1))
                {
                    u = static_cast<Real>(1) - u;
                    v = static_cast<Real>(1) - v;
                }
                
                Point3 p = v0 + u * (v1 - v0) + v * (v2 - v0);
                
                // Create N-dimensional site
                PointN site;
                site[0] = p[0];
                site[1] = p[1];
                site[2] = p[2];
                
                // Initialize other dimensions to zero
                // (caller can modify these for anisotropic metrics)
                for (size_t d = 3; d < N; ++d)
                {
                    site[d] = static_cast<Real>(0);
                }
                
                mSites.push_back(site);
            }
            
            if (mVerbose)
            {
                std::cout << "Generated " << mSites.size() << " initial sites\n";
            }
            
            return true;
        }
        
        // Set sites explicitly
        void SetSites(std::vector<PointN> const& sites)
        {
            mSites = sites;
        }
        
        // Get current sites
        std::vector<PointN> const& GetSites() const
        {
            return mSites;
        }
        
        // Get number of sites
        size_t GetNumSites() const
        {
            return mSites.size();
        }
        
        // Lloyd iterations - move sites to centroids of Voronoi cells
        bool LloydIterations(size_t numIterations)
        {
            if (mSites.empty())
            {
                return false;
            }
            
            for (size_t iter = 0; iter < numIterations; ++iter)
            {
                // Create Delaunay for current sites
                DelaunayNN<Real, N> delaunay(20);
                delaunay.SetVertices(mSites.size(), mSites.data());
                
                // Create RVD
                RestrictedVoronoiDiagramN<Real, N> rvd;
                rvd.Initialize(&delaunay, mSurfaceVertices, mSurfaceTriangles, mSites);
                
                // Compute centroids
                std::vector<PointN> centroids;
                if (!rvd.ComputeCentroids(centroids))
                {
                    return false;
                }
                
                // Compute max movement
                Real maxMovement = static_cast<Real>(0);
                for (size_t i = 0; i < mSites.size(); ++i)
                {
                    Real dist = Distance(mSites[i], centroids[i]);
                    maxMovement = std::max(maxMovement, dist);
                }
                
                // Update sites
                mSites = centroids;
                
                if (mVerbose)
                {
                    std::cout << "Lloyd iteration " << (iter + 1) 
                              << ": max movement = " << maxMovement << "\n";
                }
                
                // Check convergence
                if (maxMovement < mConvergenceThreshold)
                {
                    if (mVerbose)
                    {
                        std::cout << "Converged after " << (iter + 1) 
                                  << " iterations\n";
                    }
                    break;
                }
            }
            
            return true;
        }
        
        // Newton iterations (simplified version focusing on Lloyd)
        // Full Newton requires Hessian computation which is complex
        // For now, this is an alias for Lloyd with tighter convergence
        bool NewtonIterations(size_t numIterations)
        {
            // For a full Newton implementation, we would need:
            // 1. Compute energy gradient
            // 2. Approximate Hessian (BFGS)
            // 3. Solve linear system
            // 4. Line search
            //
            // This is complex and Lloyd works well for our use case.
            // We'll use Lloyd with tighter convergence as a practical alternative.
            
            Real savedThreshold = mConvergenceThreshold;
            mConvergenceThreshold *= static_cast<Real>(0.1);  // Tighter convergence
            
            bool result = LloydIterations(numIterations);
            
            mConvergenceThreshold = savedThreshold;
            return result;
        }
        
        // Set convergence threshold
        void SetConvergenceThreshold(Real threshold)
        {
            mConvergenceThreshold = threshold;
        }
        
        // Get convergence threshold
        Real GetConvergenceThreshold() const
        {
            return mConvergenceThreshold;
        }
        
        // Enable/disable verbose output
        void SetVerbose(bool verbose)
        {
            mVerbose = verbose;
        }
        
        // Compute total CVT energy (for analysis)
        Real ComputeEnergy() const
        {
            if (mSites.empty())
            {
                return static_cast<Real>(0);
            }
            
            Real energy = static_cast<Real>(0);
            
            // For each triangle, find nearest site and accumulate distance
            for (auto const& tri : mSurfaceTriangles)
            {
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Point3 triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);
                Real area = ComputeTriangleArea(v0, v1, v2);
                
                // Find nearest site
                int32_t nearestIdx = FindNearestSite(triCentroid);
                if (nearestIdx >= 0)
                {
                    PointN query;
                    query[0] = triCentroid[0];
                    query[1] = triCentroid[1];
                    query[2] = triCentroid[2];
                    for (size_t d = 3; d < N; ++d)
                    {
                        query[d] = static_cast<Real>(0);
                    }
                    
                    Real dist = Distance(query, mSites[nearestIdx]);
                    energy += dist * dist * area;
                }
            }
            
            return energy;
        }
        
    private:
        // Find nearest site to a 3D point
        int32_t FindNearestSite(Point3 const& point3D) const
        {
            if (mSites.empty())
            {
                return -1;
            }
            
            PointN queryN;
            queryN[0] = point3D[0];
            queryN[1] = point3D[1];
            queryN[2] = point3D[2];
            for (size_t d = 3; d < N; ++d)
            {
                queryN[d] = static_cast<Real>(0);
            }
            
            int32_t nearest = 0;
            Real minDist = Distance(queryN, mSites[0]);
            
            for (size_t i = 1; i < mSites.size(); ++i)
            {
                Real dist = Distance(queryN, mSites[i]);
                if (dist < minDist)
                {
                    minDist = dist;
                    nearest = static_cast<int32_t>(i);
                }
            }
            
            return nearest;
        }
        
        // Compute N-dimensional distance
        static Real Distance(PointN const& p0, PointN const& p1)
        {
            Real sumSq = static_cast<Real>(0);
            for (size_t i = 0; i < N; ++i)
            {
                Real diff = p1[i] - p0[i];
                sumSq += diff * diff;
            }
            return std::sqrt(sumSq);
        }
        
        // Compute area of 3D triangle
        static Real ComputeTriangleArea(
            Point3 const& v0,
            Point3 const& v1,
            Point3 const& v2)
        {
            Point3 edge1 = v1 - v0;
            Point3 edge2 = v2 - v0;
            Point3 cross = Cross(edge1, edge2);
            return Length(cross) * static_cast<Real>(0.5);
        }
        
    private:
        std::vector<Point3> mSurfaceVertices;                    // 3D mesh vertices
        std::vector<std::array<int32_t, 3>> mSurfaceTriangles;   // Triangle indices
        std::vector<PointN> mSites;                              // N-dimensional sites
        Real mConvergenceThreshold;                               // Convergence criterion
        bool mVerbose;                                            // Output progress
    };
}
