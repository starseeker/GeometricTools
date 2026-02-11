// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Nearest-neighbor based Delaunay triangulation for N dimensions
//
// This implements DelaunayN using nearest-neighbor search rather than
// full Delaunay triangulation. It's specifically designed for CVT operations
// where full cell structure is not needed.
//
// Based on:
// - geogram/src/lib/geogram/delaunay/delaunay_nn.cpp (reference implementation)
// - GTE's DelaunayN.h (interface specification)
//
// License: Boost Software License 1.0

#pragma once

#include <GTE/Mathematics/DelaunayN.h>
#include <GTE/Mathematics/NearestNeighborSearchN.h>
#include <GTE/Mathematics/Logger.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gte
{
    // Nearest-neighbor based Delaunay for N dimensions
    // 
    // This class doesn't compute full Delaunay triangulation (which would be
    // expensive in high dimensions). Instead, it uses k-nearest neighbors as
    // a proxy for the Delaunay neighborhood. This is sufficient for CVT
    // operations and matches geogram's "NN" Delaunay backend.
    //
    // Key features:
    // - Works for any dimension N
    // - Stores k-nearest neighbors per vertex
    // - Fast neighbor queries
    // - Suitable for CVT/Lloyd relaxation
    //
    template <typename Real, size_t N>
    class DelaunayNN : public DelaunayN<Real, N>
    {
    public:
        using PointN = typename DelaunayN<Real, N>::PointN;
        using Simplex = typename DelaunayN<Real, N>::Simplex;
        
        // Constructor
        // defaultNbNeighbors: default number of neighbors to store per vertex
        DelaunayNN(size_t defaultNbNeighbors = 20)
            : DelaunayN<Real, N>()
            , mDefaultNbNeighbors(defaultNbNeighbors)
            , mNNSearch()
        {
        }
        
        virtual ~DelaunayNN() = default;
        
        // Set vertices and compute neighborhoods
        bool SetVertices(size_t numVertices, PointN const* vertices) override
        {
            LogAssert(
                numVertices > 0 && vertices != nullptr,
                "Invalid arguments.");
            
            this->mNumVertices = numVertices;
            this->mVertices = vertices;
            
            // Copy vertices for NN search
            mVertexCopy.resize(numVertices);
            for (size_t i = 0; i < numVertices; ++i)
            {
                mVertexCopy[i] = vertices[i];
            }
            
            // Build nearest neighbor search structure
            mNNSearch.SetPoints(numVertices, mVertexCopy.data());
            
            // Compute neighborhoods for all vertices
            UpdateNeighborhoods();
            
            return true;
        }
        
        // Find nearest vertex to a query point
        int32_t FindNearestVertex(PointN const& query) const override
        {
            return mNNSearch.FindNearestNeighbor(query);
        }
        
        // Get neighbors of a vertex
        std::vector<int32_t> GetNeighbors(int32_t vertexIndex) const override
        {
            if (vertexIndex < 0 || static_cast<size_t>(vertexIndex) >= this->mNumVertices)
            {
                return {};
            }
            
            if (static_cast<size_t>(vertexIndex) < mNeighborhoods.size())
            {
                return mNeighborhoods[vertexIndex];
            }
            
            return {};
        }
        
        // Get number of neighbors for a vertex
        size_t GetNumNeighbors(int32_t vertexIndex) const
        {
            if (vertexIndex < 0 || static_cast<size_t>(vertexIndex) >= this->mNumVertices)
            {
                return 0;
            }
            
            if (static_cast<size_t>(vertexIndex) < mNeighborhoods.size())
            {
                return mNeighborhoods[vertexIndex].size();
            }
            
            return 0;
        }
        
        // Enlarge neighborhood for a specific vertex
        // This allows dynamic adjustment of neighborhood size for CVT
        void EnlargeNeighborhood(int32_t vertexIndex, size_t newSize)
        {
            if (vertexIndex < 0 || static_cast<size_t>(vertexIndex) >= this->mNumVertices)
            {
                return;
            }
            
            if (static_cast<size_t>(vertexIndex) >= mNeighborhoods.size())
            {
                mNeighborhoods.resize(this->mNumVertices);
            }
            
            auto& neighbors = mNeighborhoods[vertexIndex];
            
            if (newSize > neighbors.size())
            {
                std::vector<int32_t> newNeighbors;
                std::vector<Real> distances;
                
                mNNSearch.FindKNearestNeighborsToPoint(
                    vertexIndex, newSize, newNeighbors, distances);
                
                // Remove the vertex itself from its neighbors
                newNeighbors.erase(
                    std::remove(newNeighbors.begin(), newNeighbors.end(), vertexIndex),
                    newNeighbors.end());
                
                neighbors = newNeighbors;
            }
        }
        
        // Set default number of neighbors
        void SetDefaultNbNeighbors(size_t nb)
        {
            mDefaultNbNeighbors = nb;
        }
        
        // Get default number of neighbors
        size_t GetDefaultNbNeighbors() const
        {
            return mDefaultNbNeighbors;
        }
        
    private:
        // Update neighborhoods for all vertices
        void UpdateNeighborhoods()
        {
            mNeighborhoods.clear();
            mNeighborhoods.resize(this->mNumVertices);
            
            for (size_t i = 0; i < this->mNumVertices; ++i)
            {
                ComputeNeighborhood(static_cast<int32_t>(i));
            }
        }
        
        // Compute neighborhood for a single vertex
        void ComputeNeighborhood(int32_t vertexIndex)
        {
            std::vector<int32_t> neighbors;
            std::vector<Real> distances;
            
            // Query k+1 neighbors (including the vertex itself)
            size_t k = std::min(mDefaultNbNeighbors, this->mNumVertices - 1);
            mNNSearch.FindKNearestNeighborsToPoint(
                vertexIndex, k, neighbors, distances);
            
            // Remove the vertex itself and handle duplicates
            std::vector<int32_t> filteredNeighbors;
            filteredNeighbors.reserve(k);
            
            for (size_t j = 0; j < neighbors.size(); ++j)
            {
                if (neighbors[j] != vertexIndex)
                {
                    // Handle duplicate points (distance ~= 0)
                    if (distances[j] < static_cast<Real>(1e-10))
                    {
                        // Keep only if this vertex has lower index
                        // This ensures consistent handling of duplicates
                        if (neighbors[j] > vertexIndex)
                        {
                            continue;  // Skip this duplicate
                        }
                    }
                    
                    filteredNeighbors.push_back(neighbors[j]);
                }
            }
            
            mNeighborhoods[vertexIndex] = filteredNeighbors;
        }
        
    private:
        size_t mDefaultNbNeighbors;                    // Default number of neighbors
        NearestNeighborSearchN<Real, N> mNNSearch;     // NN search structure
        std::vector<PointN> mVertexCopy;               // Copy of vertices for NN search
        std::vector<std::vector<int32_t>> mNeighborhoods;  // Stored neighborhoods
    };
    
    // Factory function implementation for DelaunayN
    template <typename Real, size_t N>
    std::unique_ptr<DelaunayN<Real, N>> CreateDelaunayN(std::string const& method)
    {
        if (method == "NN" || method == "default")
        {
            return std::make_unique<DelaunayNN<Real, N>>();
        }
        
        LogError("Unknown Delaunay method: " + method);
        return nullptr;
    }
}
