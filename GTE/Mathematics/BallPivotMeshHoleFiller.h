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
#include <detria.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
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
            bool removeEdgeTrianglesOnFailure;  // Remove edge triangles and retry (default: false, deprecated)
            Real edgeTriangleThreshold;         // Edge ratio threshold for "edge triangle" (default: 0.5)
            bool allowNonManifoldEdges;         // Allow creating non-manifold edges if needed (default: false)
            bool removeConflictingTriangles;    // Remove conflicting triangles before filling (default: false)
            bool rejectSmallComponents;         // Remove small closed components and incorporate vertices (default: true)
            int32_t smallComponentThreshold;    // Max vertices for "small" component (default: 20)
            bool verbose;                       // Enable diagnostic output (default: false)
            
            Parameters()
                : edgeMetric(EdgeMetric::Average)
                , radiusScales({static_cast<Real>(1.0), static_cast<Real>(1.5), 
                               static_cast<Real>(2.0), static_cast<Real>(3.0), 
                               static_cast<Real>(5.0)})
                , nsumMinDot(static_cast<Real>(0.5))
                , maxIterations(10)
                , removeEdgeTrianglesOnFailure(false)  // Disabled - validation prevents non-manifold now
                , edgeTriangleThreshold(static_cast<Real>(0.5))
                , allowNonManifoldEdges(false)  // Conservative by default
                , removeConflictingTriangles(false)  // DISABLED by default - tests show it removes more than adds
                , rejectSmallComponents(true)   // Enabled by default per problem statement
                , smallComponentThreshold(20)   // Configurable threshold
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
        
        // Component-aware hole filling that also bridges disconnected components
        // This is a unified approach that treats inter-component gaps as fillable holes
        static bool FillAllHolesWithComponentBridging(
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
        
        // Fill a hole with Steiner points using detria triangulation  
        static bool FillHoleWithSteinerPoints(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& hole,
            std::vector<int32_t> const& steinerVertexIndices,
            Parameters const& params);
        
        // Degeneracy detection and repair structures
        struct DegeneracyInfo
        {
            bool isDegenerate;
            int32_t collinearVertexCount;      // Number of collinear vertices
            int32_t vertexOnEdgeCount;         // Number of vertices on opposite edges
            int32_t nearZeroEdgeCount;         // Number of near-zero length edges
            int32_t nearDuplicateVertexCount;  // Number of near-duplicate vertices
            Real minEdgeLength;                // Minimum edge length
            Real maxAngleDeviation;            // Max deviation from 180° for collinear
            std::vector<int32_t> collinearVertices;  // Indices of collinear vertices
            
            DegeneracyInfo()
                : isDegenerate(false)
                , collinearVertexCount(0)
                , vertexOnEdgeCount(0)
                , nearZeroEdgeCount(0)
                , nearDuplicateVertexCount(0)
                , minEdgeLength(std::numeric_limits<Real>::max())
                , maxAngleDeviation(static_cast<Real>(0))
            {}
        };
        
        // Detect if a hole boundary is degenerate
        static DegeneracyInfo DetectDegenerateHole(
            std::vector<Vector3<Real>> const& vertices,
            BoundaryLoop const& hole,
            Parameters const& params);
        
        // Repair a degenerate hole by simplifying its boundary
        static BoundaryLoop RepairDegenerateHole(
            std::vector<Vector3<Real>> const& vertices,
            BoundaryLoop const& hole,
            DegeneracyInfo const& degeneracy,
            Parameters const& params);
        
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
        
        // Internal hole filling (after conflict removal)
        static bool FillHoleInternal(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            BoundaryLoop const& hole,
            Parameters const& params);
        
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
        
        // Detect connected components in the mesh (vertex-based, conservative)
        static std::vector<std::set<int32_t>> DetectConnectedComponents(
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Count topology components (edge-based, proper manifold definition)
        static int32_t CountTopologyComponents(
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Detect topology component sets (edge-based connectivity)
        static std::vector<std::set<int32_t>> DetectTopologyComponentSets(
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Find close boundary edges between different components
        static std::vector<std::pair<std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>>
        FindComponentGaps(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<std::set<int32_t>> const& components,
            Real maxGapDistance);
        
        // Create virtual boundary loop from component gap
        static BoundaryLoop CreateVirtualBoundaryLoop(
            std::vector<Vector3<Real>> const& vertices,
            std::pair<int32_t, int32_t> const& edge1,
            std::pair<int32_t, int32_t> const& edge2);
        
        // Force bridge remaining components (finds closest edges regardless of distance)
        static bool ForceBridgeRemainingComponents(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params);
        
        // Component analysis and refinement methods
        struct ComponentInfo
        {
            int32_t componentIndex;
            std::set<int32_t> vertices;
            std::vector<int32_t> triangleIndices;
            int32_t boundaryEdgeCount;
            bool isClosed;
        };
        
        // Analyze all components (size, boundaries, etc.)
        static std::vector<ComponentInfo> AnalyzeComponents(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<std::set<int32_t>> const& components);
        
        // Identify main component (largest by vertex count)
        static int32_t IdentifyMainComponent(
            std::vector<ComponentInfo> const& componentInfos);
        
        // Remove small closed components and return their vertex indices
        static std::set<int32_t> RemoveSmallClosedComponents(
            std::vector<std::array<int32_t, 3>>& triangles,
            std::vector<ComponentInfo> const& componentInfos,
            int32_t mainComponentIndex,
            int32_t sizeThreshold);
        
        // Check if vertex normal is compatible with hole (for incorporation)
        static bool IsVertexNormalCompatible(
            Vector3<Real> const& vertexPos,
            Vector3<Real> const& vertexNormal,
            BoundaryLoop const& hole,
            std::vector<Vector3<Real>> const& vertices,
            Real normalThreshold);
        
        // Non-manifold edge repair methods
        // Detect edges with 3+ triangles sharing them
        static std::vector<std::pair<int32_t, int32_t>> DetectNonManifoldEdges(
            std::vector<std::array<int32_t, 3>> const& triangles);
        
        // Locally remesh a region around a non-manifold edge
        static bool LocalRemeshNonManifoldRegion(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            std::pair<int32_t, int32_t> const& nonManifoldEdge,
            Parameters const& params);
        
        // Detect and remove conflicting triangles around a hole
        static std::vector<int32_t> DetectConflictingTriangles(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            BoundaryLoop const& hole);
        
        // 2D Perturbation and Diagnostics Methods
        
        // Check if a 2D polygon self-intersects
        static bool CheckSelfIntersection2D(
            std::vector<detria::PointD> const& points2D,
            std::vector<std::array<uint32_t, 2>> const& edges);
        
        // Check if two line segments intersect in 2D
        static bool DoLineSegmentsIntersect2D(
            detria::PointD const& p1, detria::PointD const& p2,
            detria::PointD const& p3, detria::PointD const& p4);
        
        // Perturb collinear points in 2D parametric space
        static void PerturbCollinearPointsIn2D(
            std::vector<detria::PointD>& points2D,
            size_t numBoundaryVertices,
            Real avgEdgeLength,
            Parameters const& params);
        
        // Diagnose why detria triangulation failed
        static void DiagnoseDetriaFailure(
            std::vector<detria::PointD> const& points2D,
            std::vector<std::array<uint32_t, 2>> const& edges,
            Parameters const& params);
    };
}

#endif // GTE_MATHEMATICS_BALL_PIVOT_MESH_HOLE_FILLER_H
