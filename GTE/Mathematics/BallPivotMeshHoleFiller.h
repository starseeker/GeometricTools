// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Ball Pivoting Mesh Hole Filler
//
// Unlike BallPivotReconstruction which builds a mesh from a point cloud,
// this class fills holes in an existing mesh by pivoting a ball along
// boundary edges and adding triangles only where needed to close gaps.
//
// Key features:
// - Works with existing mesh vertices (no new vertex creation)
// - Walks mesh boundary edges to identify holes
// - Uses adaptive ball radius based on local edge lengths
// - Multi-scale approach: progressively larger radii until holes close
// - Handles problematic regions by removing edge triangles and retrying
//
// This addresses the problem statement: "Walk an existing mesh, using an
// adaptive r value based on the lengths of the edges connected to the
// vertices it is interacting with, inserting triangles only to close holes
// and leaving existing triangles."

#ifndef GTE_MATHEMATICS_BALL_PIVOT_MESH_HOLE_FILLER_H
#define GTE_MATHEMATICS_BALL_PIVOT_MESH_HOLE_FILLER_H

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
    class BallPivotMeshHoleFiller
    {
    public:
        // Edge metric to use for adaptive radius calculation
        enum class EdgeMetric
        {
            Average,  // Average of connected edge lengths (balanced)
            Minimum,  // Minimum edge length (conservative, fine detail)
            Maximum   // Maximum edge length (aggressive, larger gaps)
        };
        
        // Parameters for hole filling
        struct Parameters
        {
            EdgeMetric edgeMetric;              // Which edge metric to use for radius
            std::vector<Real> radiusScales;     // Scaling factors (e.g., {1.0, 1.5, 2.0, 3.0, 5.0})
            Real nsumMinDot;                    // Normal agreement threshold cos(angle) (default: 0.5 = 60°)
            int32_t maxIterations;              // Max iterations before giving up (default: 10)
            bool removeEdgeTrianglesOnFailure;  // Remove edge triangles and retry (default: true)
            Real edgeTriangleThreshold;         // Edge ratio threshold for "edge triangle" (default: 0.5)
            bool verbose;                       // Enable diagnostic output (default: false)
            
            Parameters()
                : edgeMetric(EdgeMetric::Average)
                , radiusScales({static_cast<Real>(1.0), static_cast<Real>(1.5), 
                               static_cast<Real>(2.0), static_cast<Real>(3.0), 
                               static_cast<Real>(5.0)})
                , nsumMinDot(static_cast<Real>(0.5))
                , maxIterations(10)
                , removeEdgeTrianglesOnFailure(true)
                , edgeTriangleThreshold(static_cast<Real>(0.5))
                , verbose(false)
            {
            }
        };
        
        // Boundary loop representing a hole in the mesh
        struct BoundaryLoop
        {
            std::vector<int32_t> vertexIndices;  // Ordered vertices forming the loop
            bool isClosed;                        // Whether loop is closed
            Real avgEdgeLength;                   // Average edge length in loop
            Real minEdgeLength;                   // Minimum edge length in loop
            Real maxEdgeLength;                   // Maximum edge length in loop
            
            BoundaryLoop() 
                : isClosed(false)
                , avgEdgeLength(static_cast<Real>(0))
                , minEdgeLength(std::numeric_limits<Real>::max())
                , maxEdgeLength(static_cast<Real>(0))
            {}
        };
        
        // Main hole filling function
        // Detects holes in the mesh and fills them using ball pivoting
        static bool FillAllHoles(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters());
        
        // Fill a specific hole given its boundary loop
        static bool FillHole(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& hole,
            Parameters const& params = Parameters());
        
        // Detect boundary loops (holes) in the mesh
        static std::vector<BoundaryLoop> DetectBoundaryLoops(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Calculate adaptive radius for a vertex based on connected edges
        static Real CalculateAdaptiveRadius(
            int32_t vertexIndex,
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            EdgeMetric metric);
        
    private:
        // Edge structure for topology tracking
        struct Edge
        {
            int32_t v0, v1;  // Vertex indices (ordered: v0 < v1)
            int32_t triCount;  // Number of triangles sharing this edge
            std::vector<int32_t> triangles;  // Triangle indices
            
            Edge(int32_t a, int32_t b) 
                : v0(std::min(a, b))
                , v1(std::max(a, b))
                , triCount(0)
            {}
            
            bool operator<(Edge const& other) const
            {
                if (v0 != other.v0) return v0 < other.v0;
                return v1 < other.v1;
            }
        };
        
        // Vertex with connectivity information
        struct VertexInfo
        {
            Vector3<Real> position;
            Vector3<Real> normal;  // Estimated from adjacent triangles
            std::set<int32_t> connectedVertices;  // Adjacent vertex indices
            std::set<int32_t> connectedTriangles;  // Adjacent triangle indices
            bool isBoundary;
            
            VertexInfo()
                : position{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)}
                , normal{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(1)}
                , isBoundary(false)
            {}
        };
        
        // Build edge topology from triangle mesh
        static std::map<Edge, int32_t> BuildEdgeTopology(
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Build vertex connectivity information
        static std::vector<VertexInfo> BuildVertexInfo(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Trace a boundary loop starting from a boundary edge
        static BoundaryLoop TraceBoundaryLoop(
            int32_t startVertex,
            std::set<std::pair<int32_t, int32_t>>& unvisitedBoundaryEdges,
            std::vector<VertexInfo> const& vertexInfo,
            std::vector<Vector3<Real>> const& vertices);
        
        // Fill a hole using ball pivoting with a specific radius
        static bool FillHoleWithRadius(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& hole,
            Real radius,
            Parameters const& params);
        
        // Compute ball center for a triangle
        static bool ComputeBallCenter(
            Vector3<Real> const& p0,
            Vector3<Real> const& p1,
            Vector3<Real> const& p2,
            Vector3<Real> const& n0,
            Vector3<Real> const& n1,
            Vector3<Real> const& n2,
            Real radius,
            Vector3<Real>& center);
        
        // Check if normals are compatible with triangle orientation
        static bool AreNormalsCompatible(
            Vector3<Real> const& n0,
            Vector3<Real> const& n1,
            Vector3<Real> const& n2,
            Vector3<Real> const& p0,
            Vector3<Real> const& p1,
            Vector3<Real> const& p2,
            Real cosThreshold);
        
        // Find next vertex to form triangle from a boundary edge
        static int32_t FindNextVertex(
            int32_t v0,
            int32_t v1,
            Real radius,
            std::vector<VertexInfo> const& vertexInfo,
            std::set<int32_t> const& availableVertices,
            Vector3<Real>& outBallCenter,
            Parameters const& params);
        
        // Check if a ball is empty (contains no other vertices)
        static bool IsBallEmpty(
            Vector3<Real> const& center,
            Real radius,
            std::vector<Vector3<Real>> const& vertices,
            std::set<int32_t> const& excludeVertices);
        
        // Identify edge triangles in problematic regions
        static std::vector<int32_t> IdentifyEdgeTriangles(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            BoundaryLoop const& hole,
            Real threshold);
        
        // Remove triangles by index
        static void RemoveTriangles(
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<int32_t> const& indicesToRemove);
    };
}

#endif // GTE_MATHEMATICS_BALL_PIVOT_MESH_HOLE_FILLER_H
