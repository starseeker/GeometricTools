// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Restricted Voronoi Diagram (RVD) - Voronoi cells restricted to surface mesh
//
// Based on Geogram's RVD implementation:
// - geogram/src/lib/geogram/voronoi/RVD.h
// - geogram/src/lib/geogram/voronoi/RVD.cpp (~2000 lines)
// - https://github.com/BrunoLevy/geogram (commit f5abd69)
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// The Restricted Voronoi Diagram computes Voronoi cells restricted to a
// surface mesh rather than the full 3D space. This is essential for:
// - Centroidal Voronoi Tessellation (CVT) surface remeshing
// - Lloyd relaxation with exact centroids
// - Gradient computation for Newton optimization
//
// Key Algorithm:
// 1. For each Voronoi site (point), define halfspaces to other sites
// 2. Clip mesh triangles against these halfspaces
// 3. Compute area and centroid of restricted polygons
// 4. Aggregate to get cell centroids for CVT
//
// Adapted for Geometric Tools Engine:
// - Uses GTE's IntrConvexPolygonHyperplane for clipping
// - Uses GTE's Delaunay3 for Voronoi site connectivity
// - Uses std::vector instead of GEO::vector
// - Removed Geogram's parameter system
// - Added struct-based configuration

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Hyperplane.h>
#include <GTE/Mathematics/Delaunay3.h>
#include <GTE/Mathematics/IntrConvexPolygonHyperplane.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace gte
{
    template <typename Real>
    class RestrictedVoronoiDiagram
    {
    public:
        // ===== DATA STRUCTURES =====

        // A polygon resulting from clipping a mesh triangle to a Voronoi cell
        struct RVD_Polygon
        {
            std::vector<Vector3<Real>> vertices;  // Vertices of clipped polygon
            Real area;                             // Polygon area
            Vector3<Real> centroid;                // Polygon centroid
            int32_t triangleIndex;                 // Which mesh triangle this came from
            int32_t siteIndex;                     // Which Voronoi site this belongs to

            RVD_Polygon()
                : area(static_cast<Real>(0))
                , centroid(Vector3<Real>::Zero())
                , triangleIndex(-1)
                , siteIndex(-1)
            {
            }
        };

        // A Voronoi cell restricted to the surface mesh
        struct RVD_Cell
        {
            int32_t siteIndex;                     // Which Voronoi site
            std::vector<RVD_Polygon> polygons;     // Clipped polygons on mesh triangles
            Real mass;                             // Total area of cell
            Vector3<Real> centroid;                // Weighted centroid of cell

            RVD_Cell()
                : siteIndex(-1)
                , mass(static_cast<Real>(0))
                , centroid(Vector3<Real>::Zero())
            {
            }
        };

        // Configuration parameters
        struct Parameters
        {
            bool computeIntegration;    // Compute mass and centroids
            bool useExactArithmetic;    // Use exact arithmetic for robustness
            Real epsilon;               // Tolerance for geometric tests
            
            Parameters()
                : computeIntegration(true)
                , useExactArithmetic(false)
                , epsilon(static_cast<Real>(1e-10))
            {
            }
        };

        // ===== CONSTRUCTION =====

        RestrictedVoronoiDiagram()
            : mParameters()
        {
        }

        // Initialize with mesh and Voronoi sites
        bool Initialize(
            std::vector<Vector3<Real>> const& meshVertices,
            std::vector<std::array<int32_t, 3>> const& meshTriangles,
            std::vector<Vector3<Real>> const& voronoiSites,
            Parameters const& params = Parameters())
        {
            if (meshVertices.empty() || meshTriangles.empty() || voronoiSites.empty())
            {
                return false;
            }

            mMeshVertices = meshVertices;
            mMeshTriangles = meshTriangles;
            mVoronoiSites = voronoiSites;
            mParameters = params;

            // Build Delaunay triangulation of Voronoi sites
            // This gives us the connectivity for determining which sites are neighbors
            BuildDelaunay();

            return true;
        }

        // ===== MAIN OPERATIONS =====

        // Compute restricted Voronoi cells for all sites
        bool ComputeCells(std::vector<RVD_Cell>& cells)
        {
            cells.clear();
            cells.resize(mVoronoiSites.size());

            for (size_t i = 0; i < mVoronoiSites.size(); ++i)
            {
                cells[i].siteIndex = static_cast<int32_t>(i);
                if (!ComputeCell(static_cast<int32_t>(i), cells[i]))
                {
                    return false;
                }
            }

            return true;
        }

        // Compute restricted Voronoi cell for a single site
        bool ComputeCell(int32_t siteIndex, RVD_Cell& cell)
        {
            if (siteIndex < 0 || siteIndex >= static_cast<int32_t>(mVoronoiSites.size()))
            {
                return false;
            }

            cell.siteIndex = siteIndex;
            cell.polygons.clear();
            cell.mass = static_cast<Real>(0);
            cell.centroid = Vector3<Real>::Zero();

            // Get Voronoi neighbors (from Delaunay)
            std::vector<int32_t> neighbors;
            GetVoronoiNeighbors(siteIndex, neighbors);

            // Create halfspaces for this Voronoi cell
            std::vector<Hyperplane<3, Real>> halfspaces;
            CreateHalfspaces(siteIndex, neighbors, halfspaces);

            // Clip each mesh triangle against the halfspaces
            for (size_t ti = 0; ti < mMeshTriangles.size(); ++ti)
            {
                RVD_Polygon polygon;
                if (ClipTriangleToCell(static_cast<int32_t>(ti), halfspaces, polygon))
                {
                    polygon.triangleIndex = static_cast<int32_t>(ti);
                    polygon.siteIndex = siteIndex;
                    
                    if (mParameters.computeIntegration)
                    {
                        ComputePolygonProperties(polygon);
                    }
                    
                    cell.polygons.push_back(polygon);
                    cell.mass += polygon.area;
                    cell.centroid += polygon.area * polygon.centroid;
                }
            }

            // Finalize centroid
            if (cell.mass > mParameters.epsilon)
            {
                cell.centroid /= cell.mass;
            }
            else
            {
                // Degenerate cell, use site position
                cell.centroid = mVoronoiSites[siteIndex];
            }

            return true;
        }

        // Compute centroids of all Voronoi cells (for Lloyd relaxation)
        bool ComputeCentroids(std::vector<Vector3<Real>>& centroids)
        {
            std::vector<RVD_Cell> cells;
            if (!ComputeCells(cells))
            {
                return false;
            }

            centroids.resize(cells.size());
            for (size_t i = 0; i < cells.size(); ++i)
            {
                centroids[i] = cells[i].centroid;
            }

            return true;
        }

    private:
        // ===== INTERNAL METHODS =====

        // Build Delaunay triangulation of Voronoi sites
        // Uses GTE's Delaunay3 to get exact neighbor relationships
        void BuildDelaunay()
        {
            int32_t numSites = static_cast<int32_t>(mVoronoiSites.size());
            
            if (numSites < 4)
            {
                // Not enough sites for 3D Delaunay - use all-pairs fallback
                mDelaunayBuilt = false;
                return;
            }
            
            // Build Delaunay triangulation
            mDelaunay = std::make_unique<Delaunay3<Real>>();
            
            // Insert all sites - call Delaunay operator() on the dereferenced unique_ptr
            Delaunay3<Real>& delRef = *mDelaunay;
            bool success = delRef(mVoronoiSites);
            if (!success)
            {
                // Delaunay failed (e.g., coplanar points) - fallback to all-pairs
                mDelaunay.reset();
                mDelaunayBuilt = false;
                return;
            }
            
            // Extract neighbor relationships from Delaunay tetrahedra
            mSiteNeighbors.clear();
            
            auto const& tetrahedra = mDelaunay->GetIndices();
            int32_t numTets = static_cast<int32_t>(tetrahedra.size()) / 4;
            
            // Each tetrahedron connects 4 sites - all pairs in the tetrahedron are neighbors
            for (int32_t t = 0; t < numTets; ++t)
            {
                std::array<int32_t, 4> tet;
                for (int i = 0; i < 4; ++i)
                {
                    tet[i] = tetrahedra[t * 4 + i];
                }
                
                // Add all 6 edges (pairs) from this tetrahedron as neighbor relationships
                for (int i = 0; i < 4; ++i)
                {
                    for (int j = i + 1; j < 4; ++j)
                    {
                        int32_t site1 = tet[i];
                        int32_t site2 = tet[j];
                        
                        // Add bidirectional neighbor relationship
                        if (mSiteNeighbors.find(site1) == mSiteNeighbors.end())
                        {
                            mSiteNeighbors[site1] = std::vector<int32_t>();
                        }
                        if (mSiteNeighbors.find(site2) == mSiteNeighbors.end())
                        {
                            mSiteNeighbors[site2] = std::vector<int32_t>();
                        }
                        
                        // Avoid duplicates using a set temporarily
                        auto& neighbors1 = mSiteNeighbors[site1];
                        if (std::find(neighbors1.begin(), neighbors1.end(), site2) == neighbors1.end())
                        {
                            neighbors1.push_back(site2);
                        }
                        
                        auto& neighbors2 = mSiteNeighbors[site2];
                        if (std::find(neighbors2.begin(), neighbors2.end(), site1) == neighbors2.end())
                        {
                            neighbors2.push_back(site1);
                        }
                    }
                }
            }
            
            mDelaunayBuilt = true;
        }

        // Get Voronoi neighbors of a site (sites sharing Delaunay edge)
        void GetVoronoiNeighbors(int32_t siteIndex, std::vector<int32_t>& neighbors)
        {
            neighbors.clear();

            if (mDelaunayBuilt && mSiteNeighbors.find(siteIndex) != mSiteNeighbors.end())
            {
                // Use exact Delaunay neighbors
                neighbors = mSiteNeighbors[siteIndex];
            }
            else
            {
                // Fallback: use all other sites as potential neighbors
                // This is conservative but correct (just slower)
                for (size_t i = 0; i < mVoronoiSites.size(); ++i)
                {
                    if (static_cast<int32_t>(i) != siteIndex)
                    {
                        neighbors.push_back(static_cast<int32_t>(i));
                    }
                }
            }
        }

        // Create halfspaces defining a Voronoi cell
        void CreateHalfspaces(
            int32_t siteIndex,
            std::vector<int32_t> const& neighbors,
            std::vector<Hyperplane<3, Real>>& halfspaces)
        {
            halfspaces.clear();
            Vector3<Real> const& site = mVoronoiSites[siteIndex];

            for (int32_t neighborIndex : neighbors)
            {
                Vector3<Real> const& neighbor = mVoronoiSites[neighborIndex];
                
                // Bisecting plane between site and neighbor
                // Normal points from neighbor to site
                Vector3<Real> normal = site - neighbor;
                Normalize(normal);
                
                // Plane passes through midpoint
                Vector3<Real> midpoint = (site + neighbor) * static_cast<Real>(0.5);
                Real constant = Dot(normal, midpoint);
                
                // Halfspace: dot(normal, p) >= constant
                // Points closer to site than neighbor
                Hyperplane<3, Real> halfspace;
                halfspace.normal = normal;
                halfspace.constant = constant;
                
                halfspaces.push_back(halfspace);
            }
        }

        // Clip a mesh triangle to a Voronoi cell (defined by halfspaces)
        bool ClipTriangleToCell(
            int32_t triangleIndex,
            std::vector<Hyperplane<3, Real>> const& halfspaces,
            RVD_Polygon& polygon)
        {
            auto const& tri = mMeshTriangles[triangleIndex];
            
            // Start with original triangle vertices
            std::vector<Vector3<Real>> vertices = {
                mMeshVertices[tri[0]],
                mMeshVertices[tri[1]],
                mMeshVertices[tri[2]]
            };

            // Clip against each halfspace
            for (auto const& halfspace : halfspaces)
            {
                if (!ClipPolygonToHalfspace(vertices, halfspace))
                {
                    // Polygon completely clipped away
                    return false;
                }

                if (vertices.size() < 3)
                {
                    // Degenerate polygon
                    return false;
                }
            }

            // Store result
            polygon.vertices = vertices;
            return true;
        }

        // Clip a polygon to a halfspace
        bool ClipPolygonToHalfspace(
            std::vector<Vector3<Real>>& vertices,
            Hyperplane<3, Real> const& halfspace)
        {
            if (vertices.size() < 3)
            {
                return false;
            }

            // Use GTE's polygon-hyperplane intersection
            using FIQuery = gte::FIQuery<Real, std::vector<Vector3<Real>>, Hyperplane<3, Real>>;
            
            FIQuery fiQuery;
            auto result = fiQuery(vertices, halfspace);
            
            // Check configuration
            using Config = typename FIQuery::Configuration;
            
            switch (result.configuration)
            {
            case Config::POSITIVE_SIDE_STRICT:
            case Config::POSITIVE_SIDE_VERTEX:
            case Config::POSITIVE_SIDE_EDGE:
                // Polygon entirely on positive side, keep unchanged
                return true;
                
            case Config::NEGATIVE_SIDE_STRICT:
            case Config::NEGATIVE_SIDE_VERTEX:
            case Config::NEGATIVE_SIDE_EDGE:
                // Polygon entirely on negative side, discard
                vertices.clear();
                return false;
                
            case Config::SPLIT:
                // Polygon split by hyperplane, keep positive side
                vertices = result.positivePolygon;
                return !vertices.empty();
                
            case Config::CONTAINED:
                // Polygon contained in hyperplane, keep it
                return true;
                
            case Config::INVALID_POLYGON:
            default:
                // Invalid, discard
                vertices.clear();
                return false;
            }
        }

        // Compute area and centroid of a polygon
        void ComputePolygonProperties(RVD_Polygon& polygon)
        {
            if (polygon.vertices.size() < 3)
            {
                polygon.area = static_cast<Real>(0);
                polygon.centroid = Vector3<Real>::Zero();
                return;
            }

            // Compute polygon normal (for area calculation)
            Vector3<Real> v0 = polygon.vertices[0];
            Vector3<Real> normal = Vector3<Real>::Zero();
            
            // Use fan triangulation from first vertex
            for (size_t i = 1; i + 1 < polygon.vertices.size(); ++i)
            {
                Vector3<Real> v1 = polygon.vertices[i];
                Vector3<Real> v2 = polygon.vertices[i + 1];
                
                Vector3<Real> e1 = v1 - v0;
                Vector3<Real> e2 = v2 - v0;
                Vector3<Real> triNormal = Cross(e1, e2);
                
                normal += triNormal;
            }

            polygon.area = Length(normal) * static_cast<Real>(0.5);

            // Compute centroid (weighted average)
            Vector3<Real> centroid = Vector3<Real>::Zero();
            Real totalArea = static_cast<Real>(0);
            
            for (size_t i = 1; i + 1 < polygon.vertices.size(); ++i)
            {
                Vector3<Real> v1 = polygon.vertices[i];
                Vector3<Real> v2 = polygon.vertices[i + 1];
                
                Vector3<Real> e1 = v1 - v0;
                Vector3<Real> e2 = v2 - v0;
                Vector3<Real> triNormal = Cross(e1, e2);
                Real triArea = Length(triNormal) * static_cast<Real>(0.5);
                
                // Triangle centroid
                Vector3<Real> triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);
                
                centroid += triArea * triCentroid;
                totalArea += triArea;
            }

            if (totalArea > mParameters.epsilon)
            {
                polygon.centroid = centroid / totalArea;
            }
            else
            {
                // Degenerate polygon, use first vertex
                polygon.centroid = polygon.vertices[0];
            }
        }

        // ===== MEMBER VARIABLES =====

        std::vector<Vector3<Real>> mMeshVertices;
        std::vector<std::array<int32_t, 3>> mMeshTriangles;
        std::vector<Vector3<Real>> mVoronoiSites;
        Parameters mParameters;
        bool mDelaunayBuilt = false;
        
        // Delaunay triangulation for exact neighbor detection
        std::unique_ptr<Delaunay3<Real>> mDelaunay;
        
        // Site neighbor map (site index -> list of neighbor site indices)
        std::map<int32_t, std::vector<int32_t>> mSiteNeighbors;
    };
}
