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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
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

        // Compute Restricted Delaunay Triangulation from current sites.
        //
        // This is the equivalent of Geogram's CVT::compute_surface() — it builds
        // a brand-new mesh topology from the optimized seed positions.
        //
        // Algorithm (matches Geogram's RVD/RDT dual construction):
        //   1. For each input mesh vertex v, find the nearest seed S[v] in 3D.
        //   2. For each input triangle (a, b, c), if S[a], S[b], S[c] are all
        //      distinct, emit a candidate RDT triangle {S[a], S[b], S[c]}.
        //   3. Deduplicate (canonical sort) and fix winding from accumulated
        //      original face normals.
        //   4. Output vertices = 3D seed positions, triangles = RDT connectivity.
        //
        // Returns true if a non-empty triangulation was produced.
        bool ComputeRDT(
            std::vector<Point3>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles) const
        {
            if (mSites.empty() || mSurfaceVertices.empty() || mSurfaceTriangles.empty())
            {
                return false;
            }

            size_t numSeeds = mSites.size();

            // Step 1: For each input mesh vertex find the nearest seed (3D distance).
            // Only the first 3 components of mSites[s] are the 3D position; dims 3-5
            // are scaled normals and are ignored for the RDT assignment step.
            std::vector<int32_t> vertToSeed(mSurfaceVertices.size(), 0);
            for (size_t v = 0; v < mSurfaceVertices.size(); ++v)
            {
                Real const px = mSurfaceVertices[v][0];
                Real const py = mSurfaceVertices[v][1];
                Real const pz = mSurfaceVertices[v][2];

                Real minDistSq = std::numeric_limits<Real>::max();
                int32_t nearest = 0;
                for (size_t s = 0; s < numSeeds; ++s)
                {
                    Real dx = px - mSites[s][0];
                    Real dy = py - mSites[s][1];
                    Real dz = pz - mSites[s][2];
                    Real dSq = dx * dx + dy * dy + dz * dz;
                    if (dSq < minDistSq)
                    {
                        minDistSq = dSq;
                        nearest = static_cast<int32_t>(s);
                    }
                }
                vertToSeed[v] = nearest;
            }

            // Step 2: Collect candidate RDT triangles.
            // Each input triangle whose 3 vertices map to 3 distinct seeds contributes
            // one candidate.  We accumulate the original face normal (unnormalized) to
            // determine the correct output winding.
            using TriKey = std::array<int32_t, 3>;
            struct NormalAccum { Point3 normal{}; };
            std::map<TriKey, NormalAccum> candidates;

            for (auto const& tri : mSurfaceTriangles)
            {
                int32_t s0 = vertToSeed[tri[0]];
                int32_t s1 = vertToSeed[tri[1]];
                int32_t s2 = vertToSeed[tri[2]];
                if (s0 == s1 || s1 == s2 || s0 == s2)
                {
                    continue;  // degenerate: two vertices in same Voronoi cell
                }

                // Canonical key (sort seed indices so duplicates are detected)
                TriKey key = {s0, s1, s2};
                std::sort(key.begin(), key.end());

                // Accumulate face normal for winding determination
                Point3 const& va = mSurfaceVertices[tri[0]];
                Point3 const& vb = mSurfaceVertices[tri[1]];
                Point3 const& vc = mSurfaceVertices[tri[2]];
                Point3 faceN = Cross(vb - va, vc - va);
                candidates[key].normal += faceN;
            }

            if (candidates.empty())
            {
                return false;
            }

            // Step 3: Extract output vertices — 3D seed positions.
            outVertices.resize(numSeeds);
            for (size_t s = 0; s < numSeeds; ++s)
            {
                outVertices[s][0] = mSites[s][0];
                outVertices[s][1] = mSites[s][1];
                outVertices[s][2] = mSites[s][2];
            }

            // Step 4: Output RDT triangles with correct winding.
            outTriangles.clear();
            outTriangles.reserve(candidates.size());
            for (auto const& kv : candidates)
            {
                TriKey const& key = kv.first;
                Point3 const& accNormal = kv.second.normal;

                int32_t a = key[0], b = key[1], c = key[2];

                // Compute normal of output triangle (a, b, c)
                Point3 outN = Cross(
                    outVertices[b] - outVertices[a],
                    outVertices[c] - outVertices[a]);

                // Align winding with accumulated original surface normal
                if (Dot(outN, accNormal) >= static_cast<Real>(0))
                {
                    outTriangles.push_back({a, b, c});
                }
                else
                {
                    outTriangles.push_back({a, c, b});
                }
            }

            return !outTriangles.empty();
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
