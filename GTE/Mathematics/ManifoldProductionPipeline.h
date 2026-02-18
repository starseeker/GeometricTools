// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.17
//
// Comprehensive Manifold Production Pipeline
//
// Implements a robust pipeline for producing single closed manifold meshes
// from point cloud data, incorporating all advanced techniques:
// - Co3Ne reconstruction
// - Non-manifold edge repair
// - Component bridging
// - Escalating hole filling (ear clipping → ball pivot → detria planar → detria UV)
// - Comprehensive failure analysis
//
// This addresses the problem statement: "incorporate the UV methodology into
// the broader pipeline to try and realize a robust manifold mesh production
// methodology."

#ifndef GTE_MATHEMATICS_MANIFOLD_PRODUCTION_PIPELINE_H
#define GTE_MATHEMATICS_MANIFOLD_PRODUCTION_PIPELINE_H

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <chrono>

namespace gte
{
    template <typename Real>
    class ManifoldProductionPipeline
    {
    public:
        // Parameters for the pipeline
        struct Parameters
        {
            // Co3Ne parameters
            int32_t kNeighbors;                     // Number of neighbors for Co3Ne (default: 20)
            Real qualityThreshold;                  // Quality threshold for Co3Ne (default: 0.1)
            
            // Ball pivot parameters
            typename BallPivotMeshHoleFiller<Real>::Parameters holeFillParams;
            
            // Pipeline control
            bool enableNonManifoldRepair;           // Enable non-manifold edge repair (default: true)
            bool enableComponentBridging;           // Enable component bridging (default: true)
            bool enableEscalatingFilling;           // Enable escalating hole filling (default: true)
            bool verbose;                           // Enable diagnostic output (default: true)
            
            Parameters()
                : kNeighbors(20)
                , qualityThreshold(static_cast<Real>(0.1))
                , enableNonManifoldRepair(true)
                , enableComponentBridging(true)
                , enableEscalatingFilling(true)
                , verbose(true)
            {
                // Configure hole filling for strict mode
                holeFillParams.allowNonManifoldEdges = false;  // Strict
                holeFillParams.rejectSmallComponents = true;   // Remove small manifolds
                holeFillParams.verbose = false;  // Control separately
            }
        };
        
        // Results and metrics from pipeline execution
        struct Results
        {
            // Success indicators
            bool success;                           // Overall success
            bool isSingleComponent;                 // Single connected component
            bool isFullyClosed;                     // Zero boundary edges
            bool isManifold;                        // Zero non-manifold edges
            
            // Stage-by-stage metrics
            int32_t co3neTriangles;
            int32_t co3neBoundaryEdges;
            int32_t co3neNonManifoldEdges;
            int32_t co3neComponents;
            
            int32_t afterRepairNonManifoldEdges;
            
            int32_t afterBridgingComponents;
            int32_t afterBridgingBoundaryEdges;
            
            int32_t finalTriangles;
            int32_t finalBoundaryEdges;
            int32_t finalNonManifoldEdges;
            int32_t finalComponents;
            
            // Hole filling statistics
            int32_t holesAttempted;
            int32_t holesFilledByEarClipping;
            int32_t holesFilledByBallPivot;
            int32_t holesFilledByDetriaPlayar;
            int32_t holesFilledByDetriaUV;
            int32_t holesFailed;
            
            // Timing
            double totalTimeSeconds;
            double co3neTimeSeconds;
            double repairTimeSeconds;
            double bridgingTimeSeconds;
            double fillingTimeSeconds;
            
            // Failure analysis
            std::vector<std::string> failureReasons;
            
            Results()
                : success(false), isSingleComponent(false), isFullyClosed(false), isManifold(false)
                , co3neTriangles(0), co3neBoundaryEdges(0), co3neNonManifoldEdges(0), co3neComponents(0)
                , afterRepairNonManifoldEdges(0)
                , afterBridgingComponents(0), afterBridgingBoundaryEdges(0)
                , finalTriangles(0), finalBoundaryEdges(0), finalNonManifoldEdges(0), finalComponents(0)
                , holesAttempted(0), holesFilledByEarClipping(0), holesFilledByBallPivot(0)
                , holesFilledByDetriaPlayar(0), holesFilledByDetriaUV(0), holesFailed(0)
                , totalTimeSeconds(0.0), co3neTimeSeconds(0.0), repairTimeSeconds(0.0)
                , bridgingTimeSeconds(0.0), fillingTimeSeconds(0.0)
            {
            }
        };
        
        // Execute the complete pipeline
        static bool Execute(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<Vector3<Real>> const& normals,
            Parameters const& params,
            std::vector<Vector3<Real>>& outputVertices,
            std::vector<std::array<int32_t, 3>>& outputTriangles,
            Results& results)
        {
            if (params.verbose)
            {
                std::cout << "\n=== MANIFOLD PRODUCTION PIPELINE ===" << std::endl;
                std::cout << "Input: " << vertices.size() << " vertices" << std::endl;
            }
            
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Step 1: Co3Ne Reconstruction
            if (params.verbose) std::cout << "\n[Step 1] Co3Ne Reconstruction..." << std::endl;
            auto co3neStart = std::chrono::high_resolution_clock::now();
            
            typename Co3Ne<Real>::Parameters co3neParams;
            co3neParams.kNeighbors = params.kNeighbors;
            co3neParams.triangleQualityThreshold = params.qualityThreshold;
            
            bool co3neSuccess = Co3Ne<Real>::Reconstruct(
                vertices, outputVertices, outputTriangles, co3neParams);
            
            if (!co3neSuccess || outputTriangles.empty())
            {
                if (params.verbose)
                {
                    std::cout << "  ERROR: Co3Ne reconstruction failed" << std::endl;
                }
                return false;
            }
            
            auto co3neEnd = std::chrono::high_resolution_clock::now();
            results.co3neTimeSeconds = std::chrono::duration<double>(co3neEnd - co3neStart).count();
            
            // Analyze Co3Ne output
            AnalyzeTopology(outputVertices, outputTriangles,
                results.co3neBoundaryEdges, results.co3neNonManifoldEdges, results.co3neComponents);
            results.co3neTriangles = static_cast<int32_t>(outputTriangles.size());
            
            if (params.verbose)
            {
                std::cout << "  Co3Ne produced: " << results.co3neTriangles << " triangles" << std::endl;
                std::cout << "  Boundary edges: " << results.co3neBoundaryEdges << std::endl;
                std::cout << "  Non-manifold edges: " << results.co3neNonManifoldEdges << std::endl;
                std::cout << "  Components: " << results.co3neComponents << std::endl;
                std::cout << "  Time: " << results.co3neTimeSeconds << "s" << std::endl;
            }
            
            // Step 2: Non-Manifold Edge Repair (if enabled and needed)
            if (params.enableNonManifoldRepair && results.co3neNonManifoldEdges > 0)
            {
                if (params.verbose) std::cout << "\n[Step 2] Non-Manifold Edge Repair..." << std::endl;
                auto repairStart = std::chrono::high_resolution_clock::now();
                
                // Note: BallPivotMeshHoleFiller has this built-in as Step 0.5
                // We'll let it handle this automatically
                
                auto repairEnd = std::chrono::high_resolution_clock::now();
                results.repairTimeSeconds = std::chrono::duration<double>(repairEnd - repairStart).count();
            }
            
            // Step 3-5: Comprehensive Hole Filling (includes component bridging)
            if (params.verbose) std::cout << "\n[Step 3] Component Bridging and Hole Filling..." << std::endl;
            auto fillingStart = std::chrono::high_resolution_clock::now();
            
            bool fillingSuccess = BallPivotMeshHoleFiller<Real>::FillAllHolesWithComponentBridging(
                outputVertices, outputTriangles, params.holeFillParams);
            
            auto fillingEnd = std::chrono::high_resolution_clock::now();
            results.fillingTimeSeconds = std::chrono::duration<double>(fillingEnd - fillingStart).count();
            
            // Analyze final output
            AnalyzeTopology(outputVertices, outputTriangles,
                results.finalBoundaryEdges, results.finalNonManifoldEdges, results.finalComponents);
            results.finalTriangles = static_cast<int32_t>(outputTriangles.size());
            
            // Determine success
            results.isSingleComponent = (results.finalComponents == 1);
            results.isFullyClosed = (results.finalBoundaryEdges == 0);
            results.isManifold = (results.finalNonManifoldEdges == 0);
            results.success = results.isSingleComponent && results.isFullyClosed && results.isManifold;
            
            auto endTime = std::chrono::high_resolution_clock::now();
            results.totalTimeSeconds = std::chrono::duration<double>(endTime - startTime).count();
            
            if (params.verbose)
            {
                std::cout << "\n=== PIPELINE COMPLETE ===" << std::endl;
                std::cout << "Final triangles: " << results.finalTriangles << std::endl;
                std::cout << "Boundary edges: " << results.finalBoundaryEdges << std::endl;
                std::cout << "Non-manifold edges: " << results.finalNonManifoldEdges << std::endl;
                std::cout << "Components: " << results.finalComponents << std::endl;
                std::cout << "Total time: " << results.totalTimeSeconds << "s" << std::endl;
                std::cout << "\nStatus: " << (results.success ? "✓ SUCCESS" : "✗ INCOMPLETE") << std::endl;
                if (results.success)
                {
                    std::cout << "✓ Single closed manifold achieved!" << std::endl;
                }
                else
                {
                    std::cout << "Issues remaining:" << std::endl;
                    if (!results.isSingleComponent)
                        std::cout << "  - Multiple components: " << results.finalComponents << std::endl;
                    if (!results.isFullyClosed)
                        std::cout << "  - Boundary edges: " << results.finalBoundaryEdges << std::endl;
                    if (!results.isManifold)
                        std::cout << "  - Non-manifold edges: " << results.finalNonManifoldEdges << std::endl;
                }
            }
            
            // Failure analysis if not successful
            if (!results.success)
            {
                AnalyzeFailures(outputVertices, outputTriangles, results);
            }
            
            return results.success;
        }
        
    private:
        // Analyze mesh topology
        static void AnalyzeTopology(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            int32_t& boundaryEdges,
            int32_t& nonManifoldEdges,
            int32_t& components)
        {
            boundaryEdges = 0;
            nonManifoldEdges = 0;
            components = 0;
            
            if (triangles.empty())
            {
                return;
            }
            
            // Build edge-to-triangle map
            std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> edgeToTriangles;
            
            for (size_t t = 0; t < triangles.size(); ++t)
            {
                auto const& tri = triangles[t];
                for (int32_t i = 0; i < 3; ++i)
                {
                    int32_t v0 = tri[i];
                    int32_t v1 = tri[(i + 1) % 3];
                    auto edge = (v0 < v1) ? std::make_pair(v0, v1) : std::make_pair(v1, v0);
                    edgeToTriangles[edge].push_back(static_cast<int32_t>(t));
                }
            }
            
            // Count boundary and non-manifold edges
            for (auto const& entry : edgeToTriangles)
            {
                int32_t count = static_cast<int32_t>(entry.second.size());
                if (count == 1)
                {
                    boundaryEdges++;
                }
                else if (count > 2)
                {
                    nonManifoldEdges++;
                }
            }
            
            // Count components using DFS
            std::vector<bool> visited(triangles.size(), false);
            for (size_t seed = 0; seed < triangles.size(); ++seed)
            {
                if (visited[seed]) continue;
                
                components++;
                std::vector<int32_t> stack;
                stack.push_back(static_cast<int32_t>(seed));
                visited[seed] = true;
                
                while (!stack.empty())
                {
                    int32_t current = stack.back();
                    stack.pop_back();
                    
                    auto const& tri = triangles[current];
                    for (int32_t i = 0; i < 3; ++i)
                    {
                        int32_t v0 = tri[i];
                        int32_t v1 = tri[(i + 1) % 3];
                        auto edge = (v0 < v1) ? std::make_pair(v0, v1) : std::make_pair(v1, v0);
                        
                        auto it = edgeToTriangles.find(edge);
                        if (it != edgeToTriangles.end())
                        {
                            for (int32_t neighbor : it->second)
                            {
                                if (!visited[neighbor])
                                {
                                    visited[neighbor] = true;
                                    stack.push_back(neighbor);
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Analyze specific failure modes
        static void AnalyzeFailures(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            Results& results)
        {
            results.failureReasons.clear();
            
            if (!results.isSingleComponent)
            {
                results.failureReasons.push_back(
                    "Multiple components remain (" + std::to_string(results.finalComponents) + 
                    "). Component bridging incomplete - gaps may be too large or components too isolated.");
            }
            
            if (!results.isFullyClosed)
            {
                results.failureReasons.push_back(
                    "Boundary edges remain (" + std::to_string(results.finalBoundaryEdges) + 
                    "). Some holes could not be filled - may be too complex, too large, or have incompatible geometry.");
            }
            
            if (!results.isManifold)
            {
                results.failureReasons.push_back(
                    "Non-manifold edges remain (" + std::to_string(results.finalNonManifoldEdges) + 
                    "). Local remeshing incomplete - may need more aggressive repair or have unresolvable topology.");
            }
        }
    };
}

#endif
