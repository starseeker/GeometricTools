// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Restricted Voronoi Diagram for N-dimensional CVT
//
// Computes centroids of Voronoi cells restricted to a 3D surface mesh,
// where Voronoi sites can be N-dimensional. This is specifically designed
// for CVT operations with anisotropic metrics.
//
// Approach: Instead of full geometric RVD clipping (which is complex in N-D),
// we use a practical approximation:
// - Use DelaunayNN neighborhoods to identify relevant sites
// - Assign mesh triangles to nearest site (N-D distance)
// - Compute centroids by integrating over assigned triangles
//
// This provides what CVT needs (centroids) without full RVD complexity.
//
// Based on:
// - geogram/src/lib/geogram/voronoi/RVD.h (reference architecture)
// - Practical CVT requirements (centroids for Lloyd relaxation)
//
// License: Boost Software License 1.0

#pragma once

#include <GTE/Mathematics/Vector.h>
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/DelaunayNN.h>
#include <GTE/Mathematics/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gte
{
    // Restricted Voronoi Diagram for N-dimensional sites on 3D surface
    //
    // This class computes centroids of Voronoi cells restricted to a
    // 3D surface mesh, where sites can be N-dimensional. The primary use
    // is for CVT with anisotropic metrics (e.g., 6D sites for anisotropic
    // remeshing).
    //
    // Key concept:
    // - Sites are N-dimensional: (x, y, z, ...) where first 3 coords are 3D position
    // - Surface mesh is 3D
    // - Voronoi cells use N-dimensional distance metric
    // - Centroids computed by integrating over mesh triangles
    //
    template <typename Real, size_t N>
    class RestrictedVoronoiDiagramN
    {
    public:
        using PointN = Vector<N, Real>;
        using Point3 = Vector3<Real>;
        
        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");
        
        static_assert(N >= 3,
            "Dimension must be at least 3 (for 3D position).");
        
        RestrictedVoronoiDiagramN()
            : mDelaunay(nullptr)
            , mSites(nullptr)
            , mNumSites(0)
        {
        }
        
        virtual ~RestrictedVoronoiDiagramN() = default;
        
        // Initialize with Delaunay and surface mesh
        bool Initialize(
            DelaunayNN<Real, N>* delaunay,
            std::vector<Point3> const& surfaceVertices,
            std::vector<std::array<int32_t, 3>> const& surfaceTriangles,
            std::vector<PointN> const& sites)
        {
            LogAssert(
                delaunay != nullptr,
                "Delaunay must not be null.");
            
            LogAssert(
                !surfaceVertices.empty() && !surfaceTriangles.empty(),
                "Surface mesh must not be empty.");
            
            LogAssert(
                !sites.empty(),
                "Sites must not be empty.");
            
            mDelaunay = delaunay;
            mSurfaceVertices = surfaceVertices;
            mSurfaceTriangles = surfaceTriangles;
            mSites = &sites[0];
            mNumSites = sites.size();
            
            return true;
        }
        
        // Compute centroids of restricted Voronoi cells
        // This is the main function needed for CVT
        bool ComputeCentroids(std::vector<PointN>& centroids)
        {
            if (!mDelaunay || mNumSites == 0)
            {
                return false;
            }

            // For N > 3 (anisotropic), estimate the normal scale factor from the
            // sites' dims 3-(N-1) magnitudes.  Geogram's set_anisotropy stores
            // (x, y, z, s*bbox_diag*nx, ...) so the magnitude of dims 3-5 equals
            // s*bbox_diag for unit normals.
            Real normalScale = static_cast<Real>(0);
            if constexpr (N > 3)
            {
                for (size_t i = 0; i < mNumSites; ++i)
                {
                    Real normSq = static_cast<Real>(0);
                    for (size_t d = 3; d < N; ++d)
                    {
                        normSq += mSites[i][d] * mSites[i][d];
                    }
                    normalScale += std::sqrt(normSq);
                }
                if (mNumSites > 0)
                {
                    normalScale /= static_cast<Real>(mNumSites);
                }
            }

            // Initialize accumulators for each site
            std::vector<PointN> weightedSum(mNumSites);
            std::vector<Real> totalArea(mNumSites, static_cast<Real>(0));
            
            for (size_t i = 0; i < mNumSites; ++i)
            {
                for (size_t d = 0; d < N; ++d)
                {
                    weightedSum[i][d] = static_cast<Real>(0);
                }
            }
            
            // Process each triangle
            for (auto const& tri : mSurfaceTriangles)
            {
                // Get triangle vertices
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                // Compute triangle centroid in 3D
                Point3 triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);

                // Build the full N-dimensional query for this triangle centroid.
                // Dims 0-2: 3D position.  Dims 3-5: scaled unit normal of the triangle.
                // This matches Geogram's anisotropic embedding where each surface point
                // is represented as (x, y, z, s*nx, s*ny, s*nz).
                PointN queryN;
                queryN[0] = triCentroid[0];
                queryN[1] = triCentroid[1];
                queryN[2] = triCentroid[2];
                if constexpr (N > 3)
                {
                    // Compute unit normal of the triangle
                    Point3 edge1 = v1 - v0;
                    Point3 edge2 = v2 - v0;
                    Point3 rawNormal = Cross(edge1, edge2);
                    Real nlen = Length(rawNormal);
                    if (nlen > static_cast<Real>(1e-10))
                    {
                        rawNormal /= nlen;
                    }
                    for (size_t d = 3; d < N; ++d)
                    {
                        // Dims 3, 4, 5 store the three components of the scaled normal.
                        // Any higher dims (N > 6) are set to zero.
                        queryN[d] = (d < 6) ? rawNormal[d - 3] * normalScale
                                            : static_cast<Real>(0);
                    }
                }
                
                // Find nearest site using full N-dimensional distance
                int32_t nearestSite = FindNearestSiteND(queryN);
                
                if (nearestSite >= 0)
                {
                    // Compute triangle area
                    Real area = ComputeTriangleArea(v0, v1, v2);
                    
                    // Accumulate weighted position (area-weighted centroid in N-D).
                    // Dims 0-2: 3D surface position.
                    // Dims 3-5: scaled surface normal at this triangle (not the site's
                    // own normal) — produces the correct anisotropic CVT centroid.
                    for (size_t d = 0; d < N; ++d)
                    {
                        weightedSum[nearestSite][d] += queryN[d] * area;
                    }
                    
                    totalArea[nearestSite] += area;
                }
            }
            
            // Compute final centroids
            centroids.resize(mNumSites);
            for (size_t i = 0; i < mNumSites; ++i)
            {
                if (totalArea[i] > static_cast<Real>(1e-10))
                {
                    for (size_t d = 0; d < N; ++d)
                    {
                        centroids[i][d] = weightedSum[i][d] / totalArea[i];
                    }
                }
                else
                {
                    // No triangles assigned, keep current site position
                    centroids[i] = mSites[i];
                }
            }
            
            return true;
        }
        
        // Compute total area of each Voronoi cell
        bool ComputeCellAreas(std::vector<Real>& areas)
        {
            if (!mDelaunay || mNumSites == 0)
            {
                return false;
            }
            
            areas.assign(mNumSites, static_cast<Real>(0));
            
            for (auto const& tri : mSurfaceTriangles)
            {
                Point3 const& v0 = mSurfaceVertices[tri[0]];
                Point3 const& v1 = mSurfaceVertices[tri[1]];
                Point3 const& v2 = mSurfaceVertices[tri[2]];
                
                Point3 triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);
                int32_t nearestSite = FindNearestSite(triCentroid);
                
                if (nearestSite >= 0)
                {
                    Real area = ComputeTriangleArea(v0, v1, v2);
                    areas[nearestSite] += area;
                }
            }
            
            return true;
        }
        
        // Get number of sites
        size_t GetNumSites() const
        {
            return mNumSites;
        }
        
    private:
        // Find nearest site to a full N-dimensional query point.
        // This is the correct function to use — it computes distance in the
        // full N-dimensional metric (position + scaled normal for N=6).
        int32_t FindNearestSiteND(PointN const& queryN) const
        {
            if (mNumSites == 0)
            {
                return -1;
            }

            int32_t nearest = 0;
            Real minDist = DistanceSquared(queryN, mSites[0]);

            for (size_t i = 1; i < mNumSites; ++i)
            {
                Real dist = DistanceSquared(queryN, mSites[i]);
                if (dist < minDist)
                {
                    minDist = dist;
                    nearest = static_cast<int32_t>(i);
                }
            }

            return nearest;
        }

        // Find nearest site to a 3D point using only 3D distance (ignoring dims 3+).
        // Used by ComputeCellAreas when an exact surface normal is not available.
        int32_t FindNearestSite(Point3 const& point3D) const
        {
            if (mNumSites == 0)
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

            return FindNearestSiteND(queryN);
        }
        
        // Compute squared N-dimensional distance
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
        DelaunayNN<Real, N>* mDelaunay;              // N-dimensional Delaunay
        std::vector<Point3> mSurfaceVertices;         // 3D surface mesh vertices
        std::vector<std::array<int32_t, 3>> mSurfaceTriangles;  // Triangle indices
        PointN const* mSites;                         // N-dimensional sites
        size_t mNumSites;                             // Number of sites
    };
}
