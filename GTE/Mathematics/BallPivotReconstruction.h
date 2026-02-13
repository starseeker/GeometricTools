// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Ball Pivoting Algorithm for surface reconstruction from point clouds
//
// Derived and adapted from the Ball Pivoting Algorithm implementation in
// Open3D: https://github.com/isl-org/Open3D
// Original Copyright (c) 2018-2024 www.open3d.org (MIT License)
//
// Ported to BRL-CAD as BallPivot.hpp, now adapted for Geometric Tools Engine:
// - Uses GTE's Vector3<Real> instead of std::array<fastf_t, 3>
// - Uses GTE's NearestNeighborQuery instead of nanoflann
// - Uses GTE's vector operations (Cross, Dot, Length, Normalize) instead of vmath macros
// - Removed BRL-CAD dependencies (vmath.h, bu/log.h)
// - Template-based for Real type (float, double) similar to Co3Ne
// - Parameter-based configuration instead of global state
// - Index-based data structures for better memory management
//
// The Ball Pivoting Algorithm reconstructs a surface by "rolling" a ball of
// given radius over the point cloud. Starting from a seed triangle, the ball
// pivots around edges, creating new triangles when it touches unused points.
//
// Key features:
// - Multi-radius expansion for handling varying point densities
// - Normal-guided ball center selection for consistent orientation
// - Loop detection to distinguish true boundaries from closeable gaps
// - Adaptive edge classification (Front, Border, Inner)

#ifndef GTE_MATHEMATICS_BALL_PIVOT_RECONSTRUCTION_H
#define GTE_MATHEMATICS_BALL_PIVOT_RECONSTRUCTION_H

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/NearestNeighborQuery.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace gte
{
    template <typename Real>
    class BallPivotReconstruction
    {
    public:
        // Edge types in the reconstruction process
        enum class EdgeType
        {
            Border,  // Boundary edge (may be closeable or true boundary)
            Front,   // Active edge ready for expansion
            Inner    // Completed edge (shared by 2 triangles)
        };
        
        // Vertex classification during reconstruction
        enum class VertexType
        {
            Orphan,  // Not yet part of any triangle
            Front,   // On the expansion frontier
            Inner    // Completely surrounded by triangles
        };
        
        // Parameters for ball pivoting reconstruction
        struct Parameters
        {
            std::vector<Real> radii;            // Ball radii to try (auto-computed if empty)
            Real alphaProbe;                    // Neighbor radius factor for trimmed probe (default: 1.5)
            int32_t maxKnnProbe;                // Cap candidate count for probe (default: 256)
            Real oneSidedMajority;              // Loop one-sided test majority fraction (default: 0.90)
            Real oneSidedMinority;              // Loop one-sided test minority fraction (default: 0.05)
            Real perimToSpacingMax;             // Large loop perimeter threshold (default: 100.0)
            bool allowCloseTrueBoundaries;      // Allow closing detected true boundaries (default: false)
            Real nsumMinDot;                    // Normal agreement threshold cos(angle) (default: 0.5 = 60°)
            bool verbose;                       // Enable diagnostic output (default: false)
            
            Parameters()
                : alphaProbe(static_cast<Real>(1.5))
                , maxKnnProbe(256)
                , oneSidedMajority(static_cast<Real>(0.90))
                , oneSidedMinority(static_cast<Real>(0.05))
                , perimToSpacingMax(static_cast<Real>(100.0))
                , allowCloseTrueBoundaries(false)
                , nsumMinDot(static_cast<Real>(0.5))
                , verbose(false)
            {
            }
        };
        
        // Main reconstruction function
        static bool Reconstruct(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            std::vector<Vector3<Real>>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            Parameters const& params = Parameters());
        
        // Estimate good radii for a point cloud based on nearest neighbor distances
        static std::vector<Real> EstimateRadii(
            std::vector<Vector3<Real>> const& points);
        
        // Compute average nearest-neighbor spacing
        static Real ComputeAverageSpacing(
            std::vector<Vector3<Real>> const& points);
        
    private:
        // Internal edge structure
        struct Edge
        {
            int32_t vertexA;
            int32_t vertexB;
            int32_t face0;  // First adjacent face (-1 if none)
            int32_t face1;  // Second adjacent face (-1 if none)
            EdgeType type;
            int32_t orientSign;  // +1 or -1 for pivot angle orientation
            
            Edge(int32_t a, int32_t b)
                : vertexA(a)
                , vertexB(b)
                , face0(-1)
                , face1(-1)
                , type(EdgeType::Front)
                , orientSign(1)
            {
            }
        };
        
        // Internal face (triangle) structure
        struct Face
        {
            int32_t v0, v1, v2;
            Vector3<Real> ballCenter;  // Ball center that created this face
            
            Face(int32_t a, int32_t b, int32_t c, Vector3<Real> const& center)
                : v0(a), v1(b), v2(c), ballCenter(center)
            {
            }
        };
        
        // Internal vertex structure
        struct Vertex
        {
            Vector3<Real> position;
            Vector3<Real> normal;
            VertexType type;
            std::set<int32_t> edgeIndices;  // Indices of edges connected to this vertex
            
            Vertex(Vector3<Real> const& pos, Vector3<Real> const& nrm)
                : position(pos)
                , normal(nrm)
                , type(VertexType::Orphan)
            {
            }
        };
        
        // Border loop structure for gap classification
        struct BorderLoop
        {
            std::vector<int32_t> edgeIndices;
            std::vector<int32_t> vertexIndices;
            bool isClosed;
            
            BorderLoop() : isClosed(false) {}
        };
        
        // Loop classification
        enum class LoopClass
        {
            Closeable,      // Can be filled
            TrueBoundary,   // Should remain open
            Unknown         // Uncertain
        };
        
        // Reconstruction state
        struct ReconstructionState
        {
            std::vector<Vertex> vertices;
            std::vector<Edge> edges;
            std::vector<Face> faces;
            std::list<int32_t> frontEdges;   // Indices of edges on the expansion front
            std::list<int32_t> borderEdges;  // Indices of boundary edges
            std::vector<std::array<int32_t, 3>> outputTriangles;
            std::vector<Vector3<Real>> outputNormals;
            Real avgSpacing;
            
            ReconstructionState() : avgSpacing(static_cast<Real>(0)) {}
        };
        
        // Implementation functions
        static void InitializeVertices(
            std::vector<Vector3<Real>> const& points,
            std::vector<Vector3<Real>> const& normals,
            ReconstructionState& state);
        
        static Real ComputeAverageSpacingInternal(
            std::vector<Vector3<Real>> const& points,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery);
        
        static bool ComputeBallCenter(
            Vector3<Real> const& p0,
            Vector3<Real> const& p1,
            Vector3<Real> const& p2,
            Vector3<Real> const& n0,
            Vector3<Real> const& n1,
            Vector3<Real> const& n2,
            Real radius,
            Vector3<Real> const* midpoint,
            Vector3<Real> const* preferredCenter,
            Vector3<Real>& center);
        
        static bool AreNormalsCompatible(
            Vector3<Real> const& n0,
            Vector3<Real> const& n1,
            Vector3<Real> const& n2,
            Vector3<Real> const& p0,
            Vector3<Real> const& p1,
            Vector3<Real> const& p2,
            Real cosThreshold);
        
        static void FindSeed(
            ReconstructionState& state,
            Real radius,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
            Parameters const& params);
        
        static void ExpandFront(
            ReconstructionState& state,
            Real radius,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
            Parameters const& params);
        
        static void UpdateBorderEdges(
            ReconstructionState& state,
            Real radius,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
            Parameters const& params);
        
        static void ClassifyAndReopenBorderLoops(
            ReconstructionState& state,
            Real radius,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
            Parameters const& params);
        
        static void FinalizeOrientation(ReconstructionState& state);
        
        static int32_t FindEdge(
            int32_t v0,
            int32_t v1,
            ReconstructionState const& state);
        
        static int32_t GetOrCreateEdge(
            int32_t v0,
            int32_t v1,
            int32_t faceIndex,
            ReconstructionState& state);
        
        static void CreateFace(
            int32_t v0,
            int32_t v1,
            int32_t v2,
            Vector3<Real> const& ballCenter,
            ReconstructionState& state,
            Parameters const& params);
        
        static int32_t FindNextVertex(
            int32_t edgeIndex,
            Real radius,
            Vector3<Real>& outCenter,
            ReconstructionState const& state,
            NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
            Parameters const& params);
    };
}

#endif // GTE_MATHEMATICS_BALL_PIVOT_RECONSTRUCTION_H
