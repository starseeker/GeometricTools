// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// 6D CVT (Centroidal Voronoi Tessellation) for anisotropic remeshing
//
// This implements Centroidal Voronoi Tessellation in 6D specifically for
// anisotropic mesh remeshing. Instead of a full Delaunay triangulation,
// this uses a more practical nearest-neighbor approach suitable for CVT.
//
// Based on geogram's CVT implementation and the approach described in
// the anisotropic remeshing documentation.

#pragma once

#include <GTE/Mathematics/Vector.h>
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace gte
{
    // 6D CVT specifically for anisotropic remeshing
    // Points are (x, y, z, nx*s, ny*s, nz*s) where (x,y,z) is position
    // and (nx,ny,nz)*s is scaled normal
    template <typename Real>
    class CVT6D
    {
    public:
        using Point6 = Vector<6, Real>;
        using Point3 = Vector3<Real>;
        
        struct Parameters
        {
            size_t lloydIterations;      // Number of Lloyd iterations
            Real convergenceThreshold;   // Convergence threshold
            bool verbose;                // Print progress
            
            Parameters()
                : lloydIterations(10)
                , convergenceThreshold(static_cast<Real>(1e-6))
                , verbose(false)
            {
            }
        };

        CVT6D()
        {
            static_assert(
                std::is_floating_point<Real>::value,
                "Real must be float or double.");
        }

        virtual ~CVT6D() = default;

        // Compute CVT given 6D sites and 6D sample points
        // sites: Voronoi sites (will be updated to centroids)
        // samples: Sample points for integration
        // params: CVT parameters
        bool ComputeCVT(
            std::vector<Point6>& sites,
            std::vector<Point6> const& samples,
            Parameters const& params = Parameters())
        {
            if (sites.empty() || samples.empty())
            {
                return false;
            }

            mSites = sites;
            mSamples = samples;

            // Lloyd iterations
            for (size_t iter = 0; iter < params.lloydIterations; ++iter)
            {
                // Compute Voronoi cells (assign samples to nearest site)
                std::vector<std::vector<size_t>> cells;
                ComputeVoronoiCells(cells);

                // Compute new centroids
                std::vector<Point6> newCentroids;
                ComputeCentroids(cells, newCentroids);

                // Check convergence
                Real maxMove = static_cast<Real>(0);
                for (size_t i = 0; i < mSites.size(); ++i)
                {
                    Real dist = Distance(mSites[i], newCentroids[i]);
                    maxMove = std::max(maxMove, dist);
                }

                // Update sites
                mSites = newCentroids;

                if (params.verbose)
                {
                    std::cout << "Iteration " << iter << ": max movement = " << maxMove << "\n";
                }

                if (maxMove < params.convergenceThreshold)
                {
                    if (params.verbose)
                    {
                        std::cout << "Converged after " << iter + 1 << " iterations\n";
                    }
                    break;
                }
            }

            // Return optimized sites
            sites = mSites;
            return true;
        }

        // Compute CVT for anisotropic mesh remeshing
        // Takes 3D mesh with normals and returns optimized 3D sites
        bool ComputeAnisotropicCVT(
            std::vector<Point3> const& meshVertices,
            std::vector<Point3> const& meshNormals,
            std::vector<Point3>& sites,
            Real anisotropyScale,
            Parameters const& params = Parameters())
        {
            if (meshVertices.size() != meshNormals.size() || sites.empty())
            {
                return false;
            }

            // Create 6D sample points from mesh
            std::vector<Point6> samples6D;
            samples6D.reserve(meshVertices.size());
            
            for (size_t i = 0; i < meshVertices.size(); ++i)
            {
                Point6 p;
                // Position
                p[0] = meshVertices[i][0];
                p[1] = meshVertices[i][1];
                p[2] = meshVertices[i][2];
                // Scaled normal
                p[3] = meshNormals[i][0] * anisotropyScale;
                p[4] = meshNormals[i][1] * anisotropyScale;
                p[5] = meshNormals[i][2] * anisotropyScale;
                samples6D.push_back(p);
            }

            // Create 6D sites from 3D sites (assume normals from nearest mesh point)
            std::vector<Point6> sites6D;
            sites6D.reserve(sites.size());
            
            for (auto const& site3D : sites)
            {
                // Find nearest mesh vertex to get normal
                size_t nearestIdx = FindNearest3D(meshVertices, site3D);
                
                Point6 p;
                p[0] = site3D[0];
                p[1] = site3D[1];
                p[2] = site3D[2];
                p[3] = meshNormals[nearestIdx][0] * anisotropyScale;
                p[4] = meshNormals[nearestIdx][1] * anisotropyScale;
                p[5] = meshNormals[nearestIdx][2] * anisotropyScale;
                sites6D.push_back(p);
            }

            // Run 6D CVT
            bool success = ComputeCVT(sites6D, samples6D, params);
            
            if (!success)
            {
                return false;
            }

            // Extract 3D positions from optimized 6D sites
            sites.clear();
            sites.reserve(sites6D.size());
            for (auto const& site6D : sites6D)
            {
                sites.push_back(Point3{site6D[0], site6D[1], site6D[2]});
            }

            return true;
        }

        // Get Voronoi cell for a site index
        std::vector<size_t> const& GetVoronoiCell(size_t siteIndex) const
        {
            static std::vector<size_t> empty;
            if (siteIndex < mCells.size())
            {
                return mCells[siteIndex];
            }
            return empty;
        }

    private:
        // Compute 6D distance
        static Real Distance(Point6 const& p0, Point6 const& p1)
        {
            Real sum = static_cast<Real>(0);
            for (int i = 0; i < 6; ++i)
            {
                Real diff = p1[i] - p0[i];
                sum += diff * diff;
            }
            return std::sqrt(sum);
        }

        // Compute 3D distance
        static Real Distance3D(Point3 const& p0, Point3 const& p1)
        {
            return Length(p1 - p0);
        }

        // Find nearest vertex in 3D
        static size_t FindNearest3D(
            std::vector<Point3> const& points,
            Point3 const& query)
        {
            size_t nearest = 0;
            Real minDist = Distance3D(points[0], query);
            
            for (size_t i = 1; i < points.size(); ++i)
            {
                Real dist = Distance3D(points[i], query);
                if (dist < minDist)
                {
                    minDist = dist;
                    nearest = i;
                }
            }
            
            return nearest;
        }

        // Assign samples to nearest sites (Voronoi cells)
        void ComputeVoronoiCells(std::vector<std::vector<size_t>>& cells)
        {
            cells.clear();
            cells.resize(mSites.size());
            
            for (size_t sampleIdx = 0; sampleIdx < mSamples.size(); ++sampleIdx)
            {
                // Find nearest site
                size_t nearestSite = 0;
                Real minDist = Distance(mSamples[sampleIdx], mSites[0]);
                
                for (size_t siteIdx = 1; siteIdx < mSites.size(); ++siteIdx)
                {
                    Real dist = Distance(mSamples[sampleIdx], mSites[siteIdx]);
                    if (dist < minDist)
                    {
                        minDist = dist;
                        nearestSite = siteIdx;
                    }
                }
                
                cells[nearestSite].push_back(sampleIdx);
            }
            
            mCells = cells;
        }

        // Compute centroids of Voronoi cells
        void ComputeCentroids(
            std::vector<std::vector<size_t>> const& cells,
            std::vector<Point6>& centroids)
        {
            centroids.clear();
            centroids.reserve(cells.size());
            
            for (auto const& cell : cells)
            {
                Point6 centroid;
                for (int d = 0; d < 6; ++d)
                {
                    centroid[d] = static_cast<Real>(0);
                }
                
                if (cell.empty())
                {
                    // Keep current site if cell is empty
                    centroids.push_back(mSites[centroids.size()]);
                    continue;
                }
                
                // Compute average of samples in cell
                for (size_t sampleIdx : cell)
                {
                    for (int d = 0; d < 6; ++d)
                    {
                        centroid[d] += mSamples[sampleIdx][d];
                    }
                }
                
                Real invCount = static_cast<Real>(1) / static_cast<Real>(cell.size());
                for (int d = 0; d < 6; ++d)
                {
                    centroid[d] *= invCount;
                }
                
                centroids.push_back(centroid);
            }
        }

    private:
        std::vector<Point6> mSites;
        std::vector<Point6> mSamples;
        std::vector<std::vector<size_t>> mCells;  // Voronoi cells
    };
}
