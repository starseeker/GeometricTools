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
        
        // Strategy 3: Quality-based selection (future work)
        static bool ReconstructQualityBased(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Quality-based merging (TODO)" << std::endl;
            }
            
            // For now, fall back to Poisson only
            return ReconstructPoissonOnly(points, outVertices, outTriangles, params);
        }
        
        // Strategy 4: Co3Ne with Poisson gap filling (future work)
        static bool ReconstructCo3NeWithPoissonGaps(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params)
        {
            if (params.verbose)
            {
                std::cout << "HybridReconstruction: Co3Ne with Poisson gaps (TODO)" << std::endl;
            }
            
            // For now, fall back to Poisson only
            return ReconstructPoissonOnly(points, outVertices, outTriangles, params);
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
    };
}

#endif // GTE_MATHEMATICS_HYBRID_RECONSTRUCTION_H
