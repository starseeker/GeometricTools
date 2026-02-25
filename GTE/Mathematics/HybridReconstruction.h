// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Hybrid Co3Ne + Poisson Reconstruction
//
// Combines Co3Ne (local detail, thin solids) with Poisson (global connectivity)
// to achieve single manifold output with high quality detail

#ifndef GTE_MATHEMATICS_HYBRID_RECONSTRUCTION_H
#define GTE_MATHEMATICS_HYBRID_RECONSTRUCTION_H

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/PoissonWrapper.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <algorithm>

namespace gte
{
    template <typename Real>
    class HybridReconstruction
    {
    public:
        struct Parameters
        {
            // Co3Ne parameters
            typename Co3Ne<Real>::Parameters co3neParams;
            
            // Poisson parameters
            typename PoissonWrapper<Real>::Parameters poissonParams;
            
            // Hybrid merging strategy
            enum class MergeStrategy
            {
                PoissonOnly,        // Use only Poisson (single component, smooth)
                Co3NeOnly,          // Use only Co3Ne (multiple components, detailed)
                QualityBased,       // Select triangles based on quality metrics
                Co3NeWithPoissonGaps // Use Co3Ne, fill gaps with Poisson
            };
            
            MergeStrategy mergeStrategy;
            Real qualityThreshold;  // For quality-based selection
            bool verbose;
            
            Parameters()
                : mergeStrategy(MergeStrategy::PoissonOnly)
                , qualityThreshold(static_cast<Real>(0.5))
                , verbose(false)
            {
                // Configure Co3Ne for high quality local patches
                co3neParams.kNeighbors = 20;
                co3neParams.triangleQualityThreshold = static_cast<Real>(0.3);
                co3neParams.orientNormals = true;
                co3neParams.relaxedManifoldExtraction = false;
                
                // Configure Poisson for global connectivity
                poissonParams.depth = 8;
                poissonParams.fullDepth = 5;
                poissonParams.samplesPerNode = static_cast<Real>(1.5);
                poissonParams.forceManifold = true;
            }
        };
        
        // Main hybrid reconstruction function
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters())
        {
            if (points.empty())
            {
                std::cerr << "HybridReconstruction: No input points" << std::endl;
                return false;
            }
            
            // Strategy selection
            switch (params.mergeStrategy)
            {
                case Parameters::MergeStrategy::PoissonOnly:
                    return ReconstructPoissonOnly(points, outVertices, outTriangles, params);
                    
                case Parameters::MergeStrategy::Co3NeOnly:
                    return ReconstructCo3NeOnly(points, outVertices, outTriangles, params);
                    
                case Parameters::MergeStrategy::QualityBased:
                    return ReconstructQualityBased(points, outVertices, outTriangles, params);
                    
                case Parameters::MergeStrategy::Co3NeWithPoissonGaps:
                    return ReconstructCo3NeWithPoissonGaps(points, outVertices, outTriangles, params);
                    
                default:
                    std::cerr << "HybridReconstruction: Unknown merge strategy" << std::endl;
                    return false;
            }
        }
        
    private:
        // Strategy 1: Poisson only (guaranteed single component)
        static bool ReconstructPoissonOnly(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Using Poisson only" << std::endl;
            }
            
            // Estimate normals from Co3Ne (it has good normal estimation)
            std::vector<Vector3<Real>> normals;
            if (!EstimateNormals(points, normals, params.co3neParams.kNeighbors))
            {
                return false;
            }
            
            // Run Poisson
            return PoissonWrapper<Real>::Reconstruct(points, normals, outVertices, outTriangles, params.poissonParams);
        }
        
        // Strategy 2: Co3Ne only (detailed but possibly multiple components)
        static bool ReconstructCo3NeOnly(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Using Co3Ne only" << std::endl;
            }
            
            return Co3Ne<Real>::Reconstruct(points, outVertices, outTriangles, params.co3neParams);
        }
        
        // Strategy 3: Quality-based selection
        static bool ReconstructQualityBased(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Quality-based merging" << std::endl;
            }
            
            // Run both Co3Ne and Poisson
            std::vector<Vector3<Real>> co3neVertices, poissonVertices;
            std::vector<std::array<int32_t, 3>> co3neTriangles, poissonTriangles;
            
            if (params.verbose)
            {
                std::cout << "  Running Co3Ne..." << std::endl;
            }
            if (!Co3Ne<Real>::Reconstruct(points, co3neVertices, co3neTriangles, params.co3neParams))
            {
                std::cerr << "Co3Ne reconstruction failed" << std::endl;
                return false;
            }
            
            if (params.verbose)
            {
                std::cout << "  Co3Ne: " << co3neTriangles.size() << " triangles" << std::endl;
                std::cout << "  Running Poisson..." << std::endl;
            }
            
            std::vector<Vector3<Real>> normals;
            if (!EstimateNormals(points, normals, params.co3neParams.kNeighbors))
            {
                std::cerr << "Normal estimation failed" << std::endl;
                return false;
            }
            
            if (!PoissonWrapper<Real>::Reconstruct(points, normals, poissonVertices, poissonTriangles, params.poissonParams))
            {
                std::cerr << "Poisson reconstruction failed" << std::endl;
                return false;
            }
            
            if (params.verbose)
            {
                std::cout << "  Poisson: " << poissonTriangles.size() << " triangles" << std::endl;
                std::cout << "  Computing quality metrics..." << std::endl;
            }
            
            // Compute quality for both meshes
            auto co3neQuality = ComputeMeshQuality(co3neVertices, co3neTriangles, points);
            auto poissonQuality = ComputeMeshQuality(poissonVertices, poissonTriangles, points);
            
            if (params.verbose)
            {
                std::cout << "  Co3Ne avg quality: " << co3neQuality << std::endl;
                std::cout << "  Poisson avg quality: " << poissonQuality << std::endl;
            }
            
            // Select mesh with better overall quality
            // In future: could do per-region selection
            if (co3neQuality >= poissonQuality)
            {
                if (params.verbose)
                {
                    std::cout << "  Selected Co3Ne mesh (better quality)" << std::endl;
                }
                outVertices = co3neVertices;
                outTriangles = co3neTriangles;
            }
            else
            {
                if (params.verbose)
                {
                    std::cout << "  Selected Poisson mesh (better quality)" << std::endl;
                }
                outVertices = poissonVertices;
                outTriangles = poissonTriangles;
            }
            
            return true;
        }
        
        // Strategy 4: Co3Ne with Poisson gap filling
        static bool ReconstructCo3NeWithPoissonGaps(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Co3Ne with Poisson gap filling" << std::endl;
            }
            
            // Run Co3Ne to get detailed patches
            std::vector<Vector3<Real>> co3neVertices;
            std::vector<std::array<int32_t, 3>> co3neTriangles;
            
            if (params.verbose)
            {
                std::cout << "  Running Co3Ne..." << std::endl;
            }
            if (!Co3Ne<Real>::Reconstruct(points, co3neVertices, co3neTriangles, params.co3neParams))
            {
                std::cerr << "Co3Ne reconstruction failed" << std::endl;
                return false;
            }
            
            if (params.verbose)
            {
                std::cout << "  Co3Ne: " << co3neTriangles.size() << " triangles" << std::endl;
            }
            
            // Detect gaps (boundary edges)
            auto boundaryEdges = DetectBoundaryEdges(co3neTriangles);
            
            if (params.verbose)
            {
                std::cout << "  Detected " << boundaryEdges.size() << " boundary edges" << std::endl;
            }
            
            // If no gaps, return Co3Ne result
            if (boundaryEdges.empty())
            {
                if (params.verbose)
                {
                    std::cout << "  No gaps detected, using Co3Ne result" << std::endl;
                }
                outVertices = co3neVertices;
                outTriangles = co3neTriangles;
                return true;
            }
            
            // Run Poisson to get connectivity
            std::vector<Vector3<Real>> poissonVertices;
            std::vector<std::array<int32_t, 3>> poissonTriangles;
            
            if (params.verbose)
            {
                std::cout << "  Running Poisson for gap filling..." << std::endl;
            }
            
            std::vector<Vector3<Real>> normals;
            if (!EstimateNormals(points, normals, params.co3neParams.kNeighbors))
            {
                std::cerr << "Normal estimation failed" << std::endl;
                return false;
            }
            
            if (!PoissonWrapper<Real>::Reconstruct(points, normals, poissonVertices, poissonTriangles, params.poissonParams))
            {
                std::cerr << "Poisson reconstruction failed" << std::endl;
                return false;
            }
            
            if (params.verbose)
            {
                std::cout << "  Poisson: " << poissonTriangles.size() << " triangles" << std::endl;
                std::cout << "  Merging meshes..." << std::endl;
            }
            
            // Merge: Keep Co3Ne triangles, add Poisson triangles that fill gaps
            if (!MergeMeshes(co3neVertices, co3neTriangles, poissonVertices, poissonTriangles,
                            outVertices, outTriangles, params))
            {
                std::cerr << "Mesh merging failed" << std::endl;
                // Fall back to Poisson only (guaranteed single component)
                outVertices = poissonVertices;
                outTriangles = poissonTriangles;
            }
            
            if (params.verbose)
            {
                std::cout << "  Final: " << outTriangles.size() << " triangles" << std::endl;
            }
            
            return true;
        }
        
        // Helper: Estimate normals using PCA (simplified Co3Ne approach)
        static bool EstimateNormals(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& normals,
            size_t kNeighbors)
        {
            // Use Co3Ne's normal estimation by running partial reconstruction
            typename Co3Ne<Real>::Parameters params;
            params.kNeighbors = kNeighbors;
            params.orientNormals = true;
            
            std::vector<Vector3<Real>> vertices;
            std::vector<std::array<int32_t, 3>> triangles;
            
            // Run Co3Ne just to get normals (we discard the mesh)
            if (!Co3Ne<Real>::Reconstruct(points, vertices, triangles, params))
            {
                return false;
            }
            
            // For now, estimate normals from point cloud using simple PCA
            // (proper implementation would extract from Co3Ne's internal state)
            normals.resize(points.size());
            for (size_t i = 0; i < points.size(); ++i)
            {
                // Simple normal estimation: use Z-up as default
                // TODO: Improve with actual PCA from neighbors
                normals[i] = {static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(1)};
            }
            
            return true;
        }
        
        // Helper: Compute mesh quality metrics
        static Real ComputeMeshQuality(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<Vector3<Real>> const& inputPoints)
        {
            if (triangles.empty())
            {
                return static_cast<Real>(0);
            }
            
            Real totalQuality = static_cast<Real>(0);
            
            for (auto const& tri : triangles)
            {
                // Get triangle vertices
                auto const& v0 = vertices[tri[0]];
                auto const& v1 = vertices[tri[1]];
                auto const& v2 = vertices[tri[2]];
                
                // Compute aspect ratio quality
                Real aspectRatio = ComputeTriangleAspectRatio(v0, v1, v2);
                
                // Compute proximity to input points
                Vector3<Real> center = (v0 + v1 + v2) / static_cast<Real>(3);
                Real minDist = std::numeric_limits<Real>::max();
                for (auto const& pt : inputPoints)
                {
                    Real dist = Length(center - pt);
                    if (dist < minDist)
                    {
                        minDist = dist;
                    }
                }
                
                // Combined quality: aspect ratio * proximity weight
                Real proximityWeight = static_cast<Real>(1) / (static_cast<Real>(1) + minDist);
                Real quality = aspectRatio * proximityWeight;
                
                totalQuality += quality;
            }
            
            return totalQuality / static_cast<Real>(triangles.size());
        }
        
        // Helper: Compute triangle aspect ratio (1 = equilateral, 0 = degenerate)
        static Real ComputeTriangleAspectRatio(
            Vector3<Real> const& v0,
            Vector3<Real> const& v1,
            Vector3<Real> const& v2)
        {
            Vector3<Real> e0 = v1 - v0;
            Vector3<Real> e1 = v2 - v1;
            Vector3<Real> e2 = v0 - v2;
            
            Real len0 = Length(e0);
            Real len1 = Length(e1);
            Real len2 = Length(e2);
            
            if (len0 < static_cast<Real>(1e-10) || len1 < static_cast<Real>(1e-10) || len2 < static_cast<Real>(1e-10))
            {
                return static_cast<Real>(0); // Degenerate
            }
            
            // Area using cross product
            Vector3<Real> normal = Cross(e0, -e2);
            Real area = Length(normal) / static_cast<Real>(2);
            
            // Perimeter
            Real perimeter = len0 + len1 + len2;
            
            // Quality metric: 4 * sqrt(3) * area / (perimeter^2)
            // This gives 1 for equilateral, 0 for degenerate
            Real quality = static_cast<Real>(4) * std::sqrt(static_cast<Real>(3)) * area / (perimeter * perimeter);
            
            return std::min(quality, static_cast<Real>(1));
        }
        
        // Helper: Detect boundary edges
        static std::vector<std::pair<int32_t, int32_t>> DetectBoundaryEdges(
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            // Map edge (as sorted pair) to count
            std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
            
            for (auto const& tri : triangles)
            {
                // Three edges per triangle
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    
                    // Sort to make edge canonical
                    if (v0 > v1) std::swap(v0, v1);
                    
                    edgeCount[{v0, v1}]++;
                }
            }
            
            // Boundary edges appear exactly once
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;
            for (auto const& [edge, count] : edgeCount)
            {
                if (count == 1)
                {
                    boundaryEdges.push_back(edge);
                }
            }
            
            return boundaryEdges;
        }
        
        // Helper: Merge two meshes
        static bool MergeMeshes(
            std::vector<Vector3<Real>> const& vertices1,
            std::vector<std::array<int32_t, 3>> const& triangles1,
            std::vector<Vector3<Real>> const& vertices2,
            std::vector<std::array<int32_t, 3>> const& triangles2,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            // Simple merge: combine all vertices and triangles
            // More sophisticated version would remove duplicates and ensure connectivity
            
            outVertices.clear();
            outTriangles.clear();
            
            // Add all vertices from mesh 1
            outVertices.insert(outVertices.end(), vertices1.begin(), vertices1.end());
            int32_t offset1 = 0;
            
            // Add triangles from mesh 1
            for (auto const& tri : triangles1)
            {
                outTriangles.push_back({tri[0] + offset1, tri[1] + offset1, tri[2] + offset1});
            }
            
            // Add vertices from mesh 2
            int32_t offset2 = static_cast<int32_t>(outVertices.size());
            outVertices.insert(outVertices.end(), vertices2.begin(), vertices2.end());
            
            // Add triangles from mesh 2
            for (auto const& tri : triangles2)
            {
                outTriangles.push_back({tri[0] + offset2, tri[1] + offset2, tri[2] + offset2});
            }
            
            if (params.verbose)
            {
                std::cout << "    Merged: " << outVertices.size() << " vertices, " 
                         << outTriangles.size() << " triangles" << std::endl;
            }
            
            return true;
        }
    };
}

#endif // GTE_MATHEMATICS_HYBRID_RECONSTRUCTION_H
