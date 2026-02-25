// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Dimension-generic Delaunay triangulation base class
//
// This is the foundation for N-dimensional Delaunay triangulation,
// following geogram's dimension-generic approach. It provides the
// interface that specific implementations (DelaunayNN, etc.) will implement.
//
// Based on:
// - GTE's Delaunay3.h (provides 3D reference)
// - geogram/src/lib/geogram/delaunay/delaunay.h (dimension-generic design)
//
// License: Boost Software License 1.0 (for GTE integration)
//          BSD 3-Clause compatible with geogram reference

#pragma once

#include <GTE/Mathematics/Vector.h>
#include <GTE/Mathematics/Logger.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace gte
{
    // Dimension-generic Delaunay triangulation interface
    // N = dimension of the space (3, 4, 5, 6, etc.)
    //
    // Key concepts:
    // - Points are N-dimensional
    // - Simplices have N+1 vertices (tetrahedra in 3D, 7-vertex in 6D, etc.)
    // - Cell size = N+1
    //
    // Usage:
    //   DelaunayN<double, 6> delaunay;  // 6D Delaunay
    //   delaunay.SetVertices(points);
    //   int nearest = delaunay.FindNearestVertex(query);
    //
    template <typename Real, size_t N>
    class DelaunayN
    {
    public:
        // Type aliases
        using PointN = Vector<N, Real>;
        using Simplex = std::array<int32_t, N + 1>;
        
        static_assert(
            std::is_floating_point<Real>::value,
            "Real must be float or double.");
        
        static_assert(N >= 2 && N <= 10,
            "Dimension must be between 2 and 10.");
        
        // Constructor
        DelaunayN()
            : mNumVertices(0)
            , mVertices(nullptr)
        {
        }
        
        virtual ~DelaunayN() = default;
        
        // Get dimension (compile-time constant)
        static constexpr size_t GetDimension()
        {
            return N;
        }
        
        // Get cell/simplex size (N+1 vertices per simplex)
        static constexpr size_t GetCellSize()
        {
            return N + 1;
        }
        
        // Set vertices and compute triangulation
        // This is the main computation method
        virtual bool SetVertices(size_t numVertices, PointN const* vertices) = 0;
        
        // Convenience overload for std::vector
        bool SetVertices(std::vector<PointN> const& vertices)
        {
            return SetVertices(vertices.size(), vertices.data());
        }
        
        // Get number of vertices
        size_t GetNumVertices() const
        {
            return mNumVertices;
        }
        
        // Get vertex by index
        PointN const& GetVertex(size_t index) const
        {
            LogAssert(index < mNumVertices, "Index out of range.");
            return mVertices[index];
        }
        
        // Get pointer to vertices array
        PointN const* GetVerticesPtr() const
        {
            return mVertices;
        }
        
        // Find nearest vertex to a query point
        // Returns vertex index, or -1 if no vertices
        virtual int32_t FindNearestVertex(PointN const& query) const = 0;
        
        // Get neighbors of a vertex (indices of neighboring vertices)
        // This is essential for CVT operations
        virtual std::vector<int32_t> GetNeighbors(int32_t vertexIndex) const = 0;
        
        // Compute distance between two N-dimensional points
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
        
        // Compute squared distance (faster, no sqrt)
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
        
    protected:
        // Protected data members for derived classes
        size_t mNumVertices;
        PointN const* mVertices;
    };
    
    // Factory function declaration (implementation in DelaunayNN.h)
    template <typename Real, size_t N>
    std::unique_ptr<DelaunayN<Real, N>> CreateDelaunayN(std::string const& method = "NN");
}
