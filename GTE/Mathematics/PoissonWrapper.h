// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Wrapper for PoissonRecon library
//
// Provides GTE-compatible interface to mkazhdan/PoissonRecon for surface reconstruction
// Based on brlcad_spsr/spsr.cpp implementation pattern

#ifndef GTE_MATHEMATICS_POISSON_WRAPPER_H
#define GTE_MATHEMATICS_POISSON_WRAPPER_H

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <array>
#include <vector>
#include <cstdint>
#include <iostream>

// Disable warnings for PoissonRecon headers
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wdeprecated"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Weverything"
#endif

// Include PoissonRecon headers
#include "../../PoissonRecon/Src/PreProcessor.h"
#include "../../PoissonRecon/Src/Reconstructors.h"

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#elif defined(__clang__)
#  pragma clang diagnostic pop
#endif

namespace gte
{
    template <typename Real>
    class PoissonWrapper
    {
    public:
        // Parameters for Poisson reconstruction
        struct Parameters
        {
            unsigned int depth;              // Octree depth (default: 8, typical range: 6-12)
            unsigned int fullDepth;          // Full depth for solving (default: 5)
            Real samplesPerNode;             // Samples per octree node (default: 1.5)
            bool exactInterpolation;         // Exact vs smooth interpolation (default: false)
            bool forceManifold;              // Force manifold output (default: true)
            bool polygonMesh;                // Output quads/polygons vs triangles (default: false)
            bool verbose;                    // Verbose output (default: false)
            
            Parameters()
                : depth(8)
                , fullDepth(5)
                , samplesPerNode(static_cast<Real>(1.5))
                , exactInterpolation(false)
                , forceManifold(true)
                , polygonMesh(false)
                , verbose(false)
            {
            }
        };
        
        // Main reconstruction function
        // Input: points with normals
        // Output: vertices and triangles forming single manifold mesh
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters())
        {
            if (points.size() != normals.size())
            {
                std::cerr << "PoissonWrapper: points and normals must have same size" << std::endl;
                return false;
            }
            
            if (points.empty())
            {
                std::cerr << "PoissonWrapper: no input points" << std::endl;
                return false;
            }
            
            using namespace PoissonRecon;
            
            // Set FEM signature for Poisson reconstruction
            static const unsigned int FEMSig = FEMDegreeAndBType<
                Reconstructor::Poisson::DefaultFEMDegree, 
                Reconstructor::Poisson::DefaultFEMBoundary
            >::Signature;
            using FEMSigs = IsotropicUIntPack<3, FEMSig>;
            
            // Set up multithreading
            ThreadPool::ParallelizationType = ThreadPool::ASYNC;
            
            // Configure solver parameters
            Reconstructor::Poisson::SolutionParameters<Real> solverParams;
            solverParams.verbose = params.verbose;
            solverParams.depth = params.depth;
            solverParams.fullDepth = params.fullDepth;
            solverParams.samplesPerNode = params.samplesPerNode;
            solverParams.exactInterpolation = params.exactInterpolation;
            
            // Configure extraction parameters
            Reconstructor::LevelSetExtractionParameters extractionParams;
            extractionParams.forceManifold = params.forceManifold;
            extractionParams.linearFit = false;
            extractionParams.polygonMesh = params.polygonMesh;
            extractionParams.verbose = params.verbose;
            
            // Create input stream adapter
            GTEPointStream<Real> pointStream(points, normals);
            
            // Run Poisson solver
            using Implicit = Reconstructor::Implicit<Real, 3, FEMSigs>;
            using Solver = Reconstructor::Poisson::Solver<Real, 3, FEMSigs>;
            
            Implicit *implicit = Solver::Solve(pointStream, solverParams);
            
            if (!implicit)
            {
                std::cerr << "PoissonWrapper: Solver::Solve failed" << std::endl;
                return false;
            }
            
            // Extract level set mesh
            std::vector<std::vector<int32_t>> polygons;
            std::vector<Real> coordinates;
            
            GTEPolygonStream<int32_t> polygonStream(polygons);
            GTEVertexStream<Real> vertexStream(coordinates);
            
            implicit->extractLevelSet(vertexStream, polygonStream, extractionParams);
            
            delete implicit;
            
            // Convert to GTE format
            size_t numVertices = coordinates.size() / 3;
            outVertices.resize(numVertices);
            for (size_t i = 0; i < numVertices; ++i)
            {
                outVertices[i][0] = coordinates[3 * i + 0];
                outVertices[i][1] = coordinates[3 * i + 1];
                outVertices[i][2] = coordinates[3 * i + 2];
            }
            
            // Convert polygons to triangles
            outTriangles.clear();
            for (auto const& poly : polygons)
            {
                if (poly.size() == 3)
                {
                    // Already a triangle
                    outTriangles.push_back({poly[0], poly[1], poly[2]});
                }
                else if (poly.size() == 4)
                {
                    // Quad -> split into two triangles
                    outTriangles.push_back({poly[0], poly[1], poly[2]});
                    outTriangles.push_back({poly[0], poly[2], poly[3]});
                }
                else
                {
                    // General polygon -> fan triangulation from first vertex
                    for (size_t i = 1; i + 1 < poly.size(); ++i)
                    {
                        outTriangles.push_back({poly[0], poly[i], poly[i + 1]});
                    }
                }
            }
            
            if (params.verbose)
            {
                std::cout << "PoissonWrapper: Generated " << outVertices.size() 
                          << " vertices, " << outTriangles.size() << " triangles" << std::endl;
            }
            
            return true;
        }
        
    private:
        // Input stream adapter: GTE vectors → Poisson Point
        template <typename RealType>
        struct GTEPointStream : public PoissonRecon::Reconstructor::InputOrientedSampleStream<RealType, 3>
        {
            GTEPointStream(std::vector<Vector3<RealType>> const& pts, 
                          std::vector<Vector3<RealType>> const& nrms)
                : points(pts), normals(nrms), current(0)
            {
            }
            
            void reset() override
            {
                current = 0;
            }
            
            bool read(PoissonRecon::Point<RealType, 3>& p, PoissonRecon::Point<RealType, 3>& n) override
            {
                if (current < points.size())
                {
                    // Copy position
                    p[0] = points[current][0];
                    p[1] = points[current][1];
                    p[2] = points[current][2];
                    
                    // Copy normal
                    n[0] = normals[current][0];
                    n[1] = normals[current][1];
                    n[2] = normals[current][2];
                    
                    ++current;
                    return true;
                }
                return false;
            }
            
        private:
            std::vector<Vector3<RealType>> const& points;
            std::vector<Vector3<RealType>> const& normals;
            size_t current;
        };
        
        // Output polygon stream adapter
        template <typename Index>
        struct GTEPolygonStream : public PoissonRecon::Reconstructor::OutputFaceStream<2>
        {
            GTEPolygonStream(std::vector<std::vector<Index>>& polys)
                : polygons(polys)
            {
            }
            
            size_t size() const override
            {
                return polygons.size();
            }
            
            size_t write(std::vector<PoissonRecon::node_index_type> const& polygon) override
            {
                std::vector<Index> poly(polygon.size());
                for (size_t i = 0; i < polygon.size(); ++i)
                {
                    poly[i] = static_cast<Index>(polygon[i]);
                }
                polygons.push_back(poly);
                return polygons.size() - 1;
            }
            
        private:
            std::vector<std::vector<Index>>& polygons;
        };
        
        // Output vertex stream adapter
        template <typename RealType>
        struct GTEVertexStream : public PoissonRecon::Reconstructor::OutputLevelSetVertexStream<RealType, 3>
        {
            GTEVertexStream(std::vector<RealType>& coords)
                : coordinates(coords)
            {
            }
            
            size_t size() const override
            {
                return coordinates.size() / 3;
            }
            
            size_t write(PoissonRecon::Point<RealType, 3> const& p, 
                        PoissonRecon::Point<RealType, 3> const&,
                        RealType const&) override
            {
                coordinates.push_back(p[0]);
                coordinates.push_back(p[1]);
                coordinates.push_back(p[2]);
                return coordinates.size() / 3 - 1;
            }
            
        private:
            std::vector<RealType>& coordinates;
        };
    };
}

#endif // GTE_MATHEMATICS_POISSON_WRAPPER_H
